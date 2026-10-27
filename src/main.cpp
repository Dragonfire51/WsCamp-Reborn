// main.cpp -- WsCampReborn: frontend libretro Win32 para WonderSwan.
//
// Carga el nucleo beetle-wswan (mednafen_wswan_libretro.dll) en tiempo de
// ejecucion con LoadLibrary/GetProcAddress, dibuja el video con GDI y ofrece
// 10 ranuras de Save/Load State.
//
// Compilar con: mingw32-make   (ver Makefile)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "menu.h"
#include "ws_core.h"

// ---------------------------------------------------------------------------
// Estado global
// ---------------------------------------------------------------------------
static WsCore       g_core;
static HWND         g_hwnd            = nullptr;
static HINSTANCE    g_hinst           = nullptr;
static bool         g_running         = true;
static bool         g_game_loaded     = false;
static bool         g_paused          = false;

// Video
static enum retro_pixel_format g_pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static unsigned     g_rotation        = 0;      // 0, 90, 180, 270
static std::vector<uint32_t> g_raw;             // frame nativo en BGRA
static std::vector<uint32_t> g_display;         // frame tras rotacion
static unsigned     g_raw_w = 0, g_raw_h = 0;
static unsigned     g_display_w = 0, g_display_h = 0;
static bool         g_have_frame = false;

// Info del sistema / juego
static struct retro_system_av_info g_av = {};
static std::wstring g_rom_path;

// Directorios (deben vivir mientras el nucleo los consulte)
static std::string  g_system_dir_utf8;
static std::string  g_save_dir_utf8;
static std::string  g_assets_dir_utf8;
static std::string  g_core_path_utf8;

// Audio
static const size_t RING_MASK = (1u << 16) - 1;   // 65536 muestras
static const size_t RING_SIZE = RING_MASK + 1;
static std::vector<int16_t> g_ring(RING_SIZE, 0);
static size_t       g_ring_head = 0, g_ring_tail = 0;
static CRITICAL_SECTION g_audio_cs;
static bool         g_audio_cs_init = false;
static HWAVEOUT     g_waveout = nullptr;
static bool         g_audio_on = false;

static const int    AUDIO_FRAMES  = 1024;   // frames por buffer
static const int    AUDIO_BUFFERS = 4;

struct AudioBuffer {
    WAVEHDR               hdr;
    std::vector<int16_t>  data;
};
static AudioBuffer  g_abuf[AUDIO_BUFFERS];

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------
static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf);
    size_t p = s.find_last_of(L"\\/");
    if (p != std::wstring::npos)
        s.resize(p);
    return s;
}

static std::string WideToUtf8(const std::wstring &w) {
    if (w.empty())
        return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        &out[0], n, nullptr, nullptr);
    return out;
}

static void LogPrintf(enum retro_log_level level, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    const char *tag = "INFO";
    switch (level) {
    case RETRO_LOG_DEBUG: tag = "DEBUG"; break;
    case RETRO_LOG_WARN:  tag = "WARN";  break;
    case RETRO_LOG_ERROR: tag = "ERROR"; break;
    default: break;
    }

    char line[1100];
    snprintf(line, sizeof(line), "[%s] %s", tag, buf);
    OutputDebugStringA(line);
    OutputDebugStringA("\n");

    if (FILE *f = fopen("wscamp.log", "a")) {
        fputs(line, f);
        fputc('\n', f);
        fclose(f);
    }
}

// ---------------------------------------------------------------------------
// Audio (waveOut con ring buffer)
// ---------------------------------------------------------------------------
static void FillAudioBuffer(AudioBuffer &b) {
    int16_t *dst = b.data.data();
    const size_t want = (size_t)AUDIO_FRAMES * 2;
    size_t got = 0;

    size_t avail = (g_ring_head - g_ring_tail) & RING_MASK;
    while (got < want && avail > 0) {
        dst[got++] = g_ring[g_ring_tail];
        g_ring_tail = (g_ring_tail + 1) & RING_MASK;
        --avail;
    }
    while (got < want)
        dst[got++] = 0;

    b.hdr.lpData         = reinterpret_cast<LPSTR>(b.data.data());
    b.hdr.dwBufferLength = (DWORD)(want * sizeof(int16_t));
    b.hdr.dwFlags        = 0;
}

static void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT msg, DWORD_PTR inst,
                                 DWORD_PTR p1, DWORD_PTR p2) {
    (void)inst; (void)p2;
    if (msg != WOM_DONE)
        return;

    AudioBuffer *b = reinterpret_cast<AudioBuffer *>(p1);
    if (b == nullptr || !g_audio_on)
        return;

    EnterCriticalSection(&g_audio_cs);
    FillAudioBuffer(*b);
    waveOutWrite(hwo, &b->hdr, sizeof(WAVEHDR));
    LeaveCriticalSection(&g_audio_cs);
}

static void ShutdownAudio() {
    if (g_audio_on) {
        g_audio_on = false;
        waveOutReset(g_waveout);
        for (int i = 0; i < AUDIO_BUFFERS; ++i)
            waveOutUnprepareHeader(g_waveout, &g_abuf[i].hdr, sizeof(WAVEHDR));
        waveOutClose(g_waveout);
        g_waveout = nullptr;
    }
}

static void InitAudio(double sample_rate) {
    ShutdownAudio();

    if (!g_audio_cs_init) {
        InitializeCriticalSection(&g_audio_cs);
        g_audio_cs_init = true;
    }
    g_ring_head = g_ring_tail = 0;

    if (sample_rate <= 0.0)
        sample_rate = 44100.0;

    WAVEFORMATEX wf = {};
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 2;
    wf.nSamplesPerSec  = (DWORD)(sample_rate + 0.5);
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = (WORD)(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    if (waveOutOpen(&g_waveout, WAVE_MAPPER, &wf,
                    (DWORD_PTR)WaveOutProc, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        LogPrintf(RETRO_LOG_WARN, "waveOutOpen fallo; se continua sin audio");
        g_waveout = nullptr;
        return;
    }

    g_audio_on = true;
    for (int i = 0; i < AUDIO_BUFFERS; ++i) {
        g_abuf[i].data.assign((size_t)AUDIO_FRAMES * 2, 0);
        ZeroMemory(&g_abuf[i].hdr, sizeof(WAVEHDR));
        FillAudioBuffer(g_abuf[i]);
        waveOutPrepareHeader(g_waveout, &g_abuf[i].hdr, sizeof(WAVEHDR));
        waveOutWrite(g_waveout, &g_abuf[i].hdr, sizeof(WAVEHDR));
    }
}

static void PushAudio(const int16_t *data, size_t frames) {
    if (!g_audio_on || data == nullptr)
        return;

    EnterCriticalSection(&g_audio_cs);
    size_t avail = (g_ring_head - g_ring_tail) & RING_MASK;
    size_t free_slots = RING_MASK - avail;

    size_t total = frames * 2;
    for (size_t i = 0; i < total && free_slots > 0; ++i, --free_slots) {
        g_ring[g_ring_head] = data[i];
        g_ring_head = (g_ring_head + 1) & RING_MASK;
    }
    LeaveCriticalSection(&g_audio_cs);
}

// ---------------------------------------------------------------------------
// Callbacks del nucleo
// ---------------------------------------------------------------------------
static void VideoRefresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (data == nullptr) {
        // El nucleo reutiliza el frame anterior (dupe).
        if (g_hwnd)
            InvalidateRect(g_hwnd, nullptr, FALSE);
        return;
    }

    g_raw_w = width;
    g_raw_h = height;
    g_raw.resize((size_t)width * height);

    switch (g_pixel_format) {
    case RETRO_PIXEL_FORMAT_XRGB8888: {
        const uint8_t *base = reinterpret_cast<const uint8_t *>(data);
        for (unsigned y = 0; y < height; ++y) {
            const uint32_t *row = reinterpret_cast<const uint32_t *>(base + y * pitch);
            uint32_t *dst = &g_raw[(size_t)y * width];
            for (unsigned x = 0; x < width; ++x)
                dst[x] = 0xFF000000u | (row[x] & 0x00FFFFFFu);
        }
        break;
    }
    case RETRO_PIXEL_FORMAT_RGB565: {
        const uint8_t *base = reinterpret_cast<const uint8_t *>(data);
        for (unsigned y = 0; y < height; ++y) {
            const uint16_t *row = reinterpret_cast<const uint16_t *>(base + y * pitch);
            uint32_t *dst = &g_raw[(size_t)y * width];
            for (unsigned x = 0; x < width; ++x) {
                uint16_t p = row[x];
                unsigned r5 = (p >> 11) & 0x1F;
                unsigned g6 = (p >> 5)  & 0x3F;
                unsigned b5 =  p        & 0x1F;
                unsigned r8 = (r5 << 3) | (r5 >> 2);
                unsigned g8 = (g6 << 2) | (g6 >> 4);
                unsigned b8 = (b5 << 3) | (b5 >> 2);
                dst[x] = 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
            }
        }
        break;
    }
    case RETRO_PIXEL_FORMAT_0RGB1555:
    default: {
        const uint8_t *base = reinterpret_cast<const uint8_t *>(data);
        for (unsigned y = 0; y < height; ++y) {
            const uint16_t *row = reinterpret_cast<const uint16_t *>(base + y * pitch);
            uint32_t *dst = &g_raw[(size_t)y * width];
            for (unsigned x = 0; x < width; ++x) {
                uint16_t p = row[x];
                unsigned r5 = (p >> 10) & 0x1F;
                unsigned g5 = (p >> 5)  & 0x1F;
                unsigned b5 =  p        & 0x1F;
                unsigned r8 = (r5 << 3) | (r5 >> 2);
                unsigned g8 = (g5 << 3) | (g5 >> 2);
                unsigned b8 = (b5 << 3) | (b5 >> 2);
                dst[x] = 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
            }
        }
        break;
    }
    }

    // Aplica rotacion si el nucleo la ha pedido.
    if (g_rotation == 90 || g_rotation == 270) {
        g_display_w = height;
        g_display_h = width;
    } else {
        g_display_w = width;
        g_display_h = height;
    }
    g_display.resize((size_t)g_display_w * g_display_h);

    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            uint32_t px = g_raw[(size_t)y * width + x];
            unsigned dx, dy;
            switch (g_rotation) {
            case 90:  dx = height - 1 - y; dy = x;             break;
            case 180: dx = width - 1 - x;  dy = height - 1 - y; break;
            case 270: dx = y;              dy = width - 1 - x;  break;
            default:  dx = x;              dy = y;              break;
            }
            g_display[(size_t)dy * g_display_w + dx] = px;
        }
    }

    g_have_frame = true;
    if (g_hwnd)
        InvalidateRect(g_hwnd, nullptr, FALSE);
}

static size_t AudioSampleBatch(const int16_t *data, size_t frames) {
    PushAudio(data, frames);
    return frames;
}

static void AudioSample(int16_t left, int16_t right) {
    int16_t pair[2] = { left, right };
    PushAudio(pair, 1);
}

static void InputPoll(void) {
    // La entrada se lee con GetAsyncKeyState en InputState.
}

static int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port != 0 || index != 0 || device != RETRO_DEVICE_JOYPAD)
        return 0;

    int vk = 0;
    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_A:      vk = 'X';       break;
    case RETRO_DEVICE_ID_JOYPAD_B:      vk = 'Z';       break;
    case RETRO_DEVICE_ID_JOYPAD_Y:      vk = 'S';       break;
    case RETRO_DEVICE_ID_JOYPAD_X:      vk = 'A';       break;
    case RETRO_DEVICE_ID_JOYPAD_L:      vk = 'Q';       break;
    case RETRO_DEVICE_ID_JOYPAD_R:      vk = 'W';       break;
    case RETRO_DEVICE_ID_JOYPAD_START:  vk = VK_RETURN; break;
    case RETRO_DEVICE_ID_JOYPAD_SELECT: vk = VK_BACK;   break;
    case RETRO_DEVICE_ID_JOYPAD_UP:     vk = VK_UP;     break;
    case RETRO_DEVICE_ID_JOYPAD_DOWN:   vk = VK_DOWN;   break;
    case RETRO_DEVICE_ID_JOYPAD_LEFT:   vk = VK_LEFT;   break;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT:  vk = VK_RIGHT;  break;
    default: return 0;
    }

    return (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
}

static bool Environment(unsigned cmd, void *data) {
    switch (cmd & 0xFFFFu) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format fmt =
            *reinterpret_cast<enum retro_pixel_format *>(data);
        if (fmt != RETRO_PIXEL_FORMAT_0RGB1555 &&
            fmt != RETRO_PIXEL_FORMAT_XRGB8888 &&
            fmt != RETRO_PIXEL_FORMAT_RGB565)
            return false;
        g_pixel_format = fmt;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_ROTATION:
        g_rotation = *reinterpret_cast<const unsigned *>(data);
        return true;

    case RETRO_ENVIRONMENT_GET_OVERSCAN:
        *reinterpret_cast<bool *>(data) = false;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *reinterpret_cast<bool *>(data) = true;
        return true;

    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *reinterpret_cast<const char **>(data) = g_system_dir_utf8.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *reinterpret_cast<const char **>(data) = g_save_dir_utf8.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *reinterpret_cast<const char **>(data) = g_assets_dir_utf8.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        *reinterpret_cast<const char **>(data) = g_core_path_utf8.c_str();
        return true;

    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        reinterpret_cast<struct retro_log_callback *>(data)->log = LogPrintf;
        return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE:
        return false;   // el nucleo usara sus valores por defecto

    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *reinterpret_cast<bool *>(data) = false;
        return true;

    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        *reinterpret_cast<bool *>(data) = false;
        return true;

    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *reinterpret_cast<unsigned *>(data) = 0;   // interfaz clasica (SET_VARIABLES)
        return true;

    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        const struct retro_system_av_info *av =
            reinterpret_cast<const struct retro_system_av_info *>(data);
        if (av)
            g_av = *av;
        return true;
    }

    case RETRO_ENVIRONMENT_SHUTDOWN:
        g_running = false;
        if (g_hwnd)
            PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
        return true;

    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *reinterpret_cast<unsigned *>(data) = 0;   // ingles
        return true;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Save / Load State (10 ranuras)
// ---------------------------------------------------------------------------
static std::wstring StatePath(int slot) {
    wchar_t name[64];
    swprintf(name, 64, L"slot%02d.sts", slot);
    return GetExeDir() + L"\\saves\\" + name;
}

static void EnsureSaveDir() {
    CreateDirectoryW((GetExeDir() + L"\\saves").c_str(), nullptr);
}

static bool SaveState(int slot) {
    if (!g_game_loaded) return false;

    size_t size = g_core.retro_serialize_size();
    if (size == 0) return false;

    std::vector<uint8_t> buf(size);
    if (!g_core.retro_serialize(buf.data(), buf.size()))
        return false;

    EnsureSaveDir();
    std::wstring path = StatePath(slot);
    FILE *f = _wfopen(path.c_str(), L"wb");
    if (f == nullptr) return false;

    bool ok = fwrite(buf.data(), 1, buf.size(), f) == buf.size();
    fclose(f);
    return ok;
}

static bool LoadState(int slot) {
    if (!g_game_loaded) return false;

    std::wstring path = StatePath(slot);
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (f == nullptr) return false;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return false; }

    std::vector<uint8_t> buf((size_t)len);
    size_t rd = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    if (rd != buf.size()) return false;

    return g_core.retro_unserialize(buf.data(), buf.size());
}

// ---------------------------------------------------------------------------
// Carga de ROM
// ---------------------------------------------------------------------------
static void UnloadGame() {
    if (g_game_loaded) {
        ShutdownAudio();
        g_core.retro_unload_game();
        g_game_loaded = false;
        g_have_frame = false;
        g_display_w = g_display_h = 0;
        g_raw_w = g_raw_h = 0;
    }
}

static bool LoadGame(const std::wstring &rom_path) {
    UnloadGame();

    std::string utf8 = WideToUtf8(rom_path);
    struct retro_game_info info = {};
    info.path = utf8.c_str();
    info.data = nullptr;
    info.size = 0;
    info.meta = nullptr;

    if (!g_core.retro_load_game(&info)) {
        MessageBoxW(g_hwnd, L"No se pudo cargar la ROM.", L"WsCampReborn",
                    MB_ICONERROR | MB_OK);
        return false;
    }

    g_game_loaded = true;
    g_rom_path = rom_path;

    g_core.retro_get_system_av_info(&g_av);
    InitAudio(g_av.timing.sample_rate);

    if (g_av.geometry.base_width > 0 && g_av.geometry.base_height > 0) {
        unsigned w = g_av.geometry.base_width;
        unsigned h = g_av.geometry.base_height;
        if (g_rotation == 90 || g_rotation == 270) {
            unsigned t = w; w = h; h = t;
        }
        RECT rc = { 0, 0, (LONG)w * 2, (LONG)h * 2 };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
        SetWindowPos(g_hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOMOVE | SWP_NOZORDER);
    }

    std::wstring title = L"WsCampReborn - " + rom_path;
    size_t slash = title.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        title = L"WsCampReborn - " + title.substr(slash + 1);
    SetWindowTextW(g_hwnd, title.c_str());

    return true;
}

static void OpenRomDialog() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"WonderSwan (*.ws;*.wsc)\0*.ws;*.wsc\0Todos los archivos\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn))
        LoadGame(file);
}

// ---------------------------------------------------------------------------
// Nucleo: carga y arranque
// ---------------------------------------------------------------------------
static std::wstring FindCoreDll() {
    std::wstring dir = GetExeDir();
    const wchar_t *names[] = {
        L"mednafen_wswan_libretro.dll",
        L"beetle_wswan_libretro.dll",
        L"libretro.dll"
    };
    for (const wchar_t *n : names) {
        std::wstring a = dir + L"\\" + n;
        if (GetFileAttributesW(a.c_str()) != INVALID_FILE_ATTRIBUTES)
            return a;
        std::wstring b = dir + L"\\lib\\" + n;
        if (GetFileAttributesW(b.c_str()) != INVALID_FILE_ATTRIBUTES)
            return b;
    }
    return std::wstring();
}

static bool StartCore() {
    std::wstring dll = FindCoreDll();
    if (dll.empty()) {
        MessageBoxW(g_hwnd,
                    L"No se encontro mednafen_wswan_libretro.dll.\n\n"
                    L"Colocala junto al ejecutable o en la carpeta lib\\.\n"
                    L"Descargala como nucleo \"beetle-wswan-libretro\".",
                    L"WsCampReborn", MB_ICONERROR | MB_OK);
        return false;
    }

    if (!g_core.Load(dll)) {
        MessageBoxW(g_hwnd, g_core.last_error().c_str(), L"WsCampReborn",
                    MB_ICONERROR | MB_OK);
        return false;
    }

    g_core_path_utf8 = WideToUtf8(dll);

    unsigned api = g_core.retro_api_version();
    if (api != RETRO_API_VERSION) {
        MessageBoxW(g_hwnd, L"Version de API libretro incompatible.",
                    L"WsCampReborn", MB_ICONERROR | MB_OK);
        g_core.Unload();
        return false;
    }

    struct retro_system_info si = {};
    g_core.retro_get_system_info(&si);

    g_core.retro_set_environment(Environment);
    g_core.retro_set_video_refresh(VideoRefresh);
    g_core.retro_set_audio_sample(AudioSample);
    g_core.retro_set_audio_sample_batch(AudioSampleBatch);
    g_core.retro_set_input_poll(InputPoll);
    g_core.retro_set_input_state(InputState);

    g_core.retro_init();
    return true;
}

static void StopCore() {
    if (g_core.IsLoaded()) {
        UnloadGame();
        g_core.retro_deinit();
        g_core.Unload();
    }
}

// ---------------------------------------------------------------------------
// Interfaz
// ---------------------------------------------------------------------------
static void UpdatePauseMenu(HMENU menu) {
    CheckMenuItem(menu, IDM_EMU_PAUSE, MF_BYCOMMAND | (g_paused ? MF_CHECKED : MF_UNCHECKED));
}

static void BuildMenu() {
    HMENU bar = CreateMenu();

    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_FILE_OPEN, L"Abrir ROM...\tCtrl+O");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_FILE_EXIT, L"Salir");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"Archivo");

    HMENU emu = CreatePopupMenu();
    AppendMenuW(emu, MF_STRING, IDM_EMU_RESET, L"Reiniciar");
    AppendMenuW(emu, MF_STRING, IDM_EMU_PAUSE, L"Pausa\tEspacio");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)emu, L"Emulacion");

    HMENU state = CreatePopupMenu();
    for (int i = 0; i < WSCAMP_STATE_SLOTS; ++i) {
        wchar_t label[32];
        swprintf(label, 32, L"Guardar estado %d", i);
        AppendMenuW(state, MF_STRING, IDM_SAVE_STATE_BASE + i, label);
    }
    AppendMenuW(state, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < WSCAMP_STATE_SLOTS; ++i) {
        wchar_t label[32];
        swprintf(label, 32, L"Cargar estado %d", i);
        AppendMenuW(state, MF_STRING, IDM_LOAD_STATE_BASE + i, label);
    }
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)state, L"Estado");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_HELP_ABOUT, L"Acerca de...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"Ayuda");

    SetMenu(g_hwnd, bar);
}

static void PaintFrame(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

    if (g_have_frame && g_display_w > 0 && g_display_h > 0) {
        int cw = rc.right - rc.left;
        int ch = rc.bottom - rc.top;

        double scale = (double)cw / g_display_w;
        double sy = (double)ch / g_display_h;
        if (sy < scale) scale = sy;
        if (scale <= 0.0) scale = 1.0;

        int dw = (int)(g_display_w * scale);
        int dh = (int)(g_display_h * scale);
        int dx = (cw - dw) / 2;
        int dy = (ch - dh) / 2;

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = (LONG)g_display_w;
        bmi.bmiHeader.biHeight      = -(LONG)g_display_h;   // top-down
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        StretchDIBits(hdc, dx, dy, dw, dh,
                      0, 0, (int)g_display_w, (int)g_display_h,
                      g_display.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintFrame(hwnd);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);

        if (id == IDM_FILE_OPEN) {
            OpenRomDialog();
        } else if (id == IDM_FILE_EXIT) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else if (id == IDM_EMU_RESET) {
            if (g_game_loaded) g_core.retro_reset();
        } else if (id == IDM_EMU_PAUSE) {
            g_paused = !g_paused;
            UpdatePauseMenu(GetMenu(hwnd));
        } else if (id == IDM_HELP_ABOUT) {
            MessageBoxW(hwnd,
                        L"WsCampReborn\n\n"
                        L"Emulador WonderSwan basado en el nucleo libretro\n"
                        L"beetle-wswan (mednafen/wswan).\n\n"
                        L"Teclas: flechas = cruceta, X = A, Z = B, S = Y, A = X,\n"
                        L"Q/R = L/R, Enter = Start, Retroceso = Select.",
                        L"Acerca de WsCampReborn", MB_ICONINFORMATION | MB_OK);
        } else if (id >= IDM_SAVE_STATE_BASE && id < IDM_SAVE_STATE_BASE + WSCAMP_STATE_SLOTS) {
            int slot = id - IDM_SAVE_STATE_BASE;
            if (!SaveState(slot))
                MessageBoxW(hwnd, L"No se pudo guardar el estado.", L"WsCampReborn",
                            MB_ICONWARNING | MB_OK);
        } else if (id >= IDM_LOAD_STATE_BASE && id < IDM_LOAD_STATE_BASE + WSCAMP_STATE_SLOTS) {
            int slot = id - IDM_LOAD_STATE_BASE;
            if (!LoadState(slot))
                MessageBoxW(hwnd, L"No se pudo cargar el estado.", L"WsCampReborn",
                            MB_ICONWARNING | MB_OK);
        }
        return 0;
    }

    case WM_CLOSE:
        StopCore();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Punto de entrada
// ---------------------------------------------------------------------------
static double NowMs() {
    static LARGE_INTEGER freq = {};
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    PWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;

    g_hinst = hInstance;

    // Directorios que se entregan al nucleo.
    std::wstring dir = GetExeDir();
    g_system_dir_utf8 = WideToUtf8(dir);
    g_save_dir_utf8   = WideToUtf8(dir + L"\\saves");
    g_assets_dir_utf8 = WideToUtf8(dir + L"\\system");

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"WsCampRebornWnd";
    if (!RegisterClassExW(&wc))
        return 1;

    RECT rc = { 0, 0, 448, 288 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"WsCampReborn",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, hInstance, nullptr);
    if (g_hwnd == nullptr)
        return 1;

    BuildMenu();
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    if (!StartCore()) {
        DestroyWindow(g_hwnd);
        return 1;
    }

    // ROM pasada en la linea de comandos.
    if (lpCmdLine != nullptr && lpCmdLine[0] != L'\0') {
        std::wstring rom(lpCmdLine);
        if (rom.size() >= 2 && rom.front() == L'"' && rom.back() == L'"')
            rom = rom.substr(1, rom.size() - 2);
        LoadGame(rom);
    }

    double fps = 60.0;
    double next_frame = NowMs();

    MSG msg;
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running)
            break;

        if (g_game_loaded && !g_paused) {
            g_core.retro_run();

            fps = (g_av.timing.fps > 0.0) ? g_av.timing.fps : 60.0;
            double frame_ms = 1000.0 / fps;
            next_frame += frame_ms;

            double now = NowMs();
            double wait = next_frame - now;
            if (wait > 1.0) {
                Sleep((DWORD)(wait - 0.5));
            } else if (wait < -frame_ms * 4.0) {
                next_frame = now;   // se ha quedado atras: resincroniza
            }
        } else {
            WaitMessage();
            next_frame = NowMs();
        }
    }

    StopCore();
    if (g_audio_cs_init)
        DeleteCriticalSection(&g_audio_cs);

    return 0;
}
