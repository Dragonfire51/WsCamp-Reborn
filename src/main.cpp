// main.cpp -- WsCampReborn: frontend libretro portable (SDL2).
//
// Carga el nucleo beetle-wswan (mednafen_wswan_libretro) en tiempo de ejecucion
// y ofrece 10 ranuras de Save/Load State. Compila en Linux, macOS y Windows.
//
// Compilar con: make        (ver Makefile)

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "font5x7.h"
#include "menu.h"
#include "version.h"
#include "ws_core.h"

// ---------------------------------------------------------------------------
// Estado global
// ---------------------------------------------------------------------------
static WsCore            g_core;
static SDL_Window       *g_window      = nullptr;
static SDL_Renderer     *g_renderer    = nullptr;
static SDL_Texture      *g_texture     = nullptr;
static bool              g_running     = true;
static bool              g_game_loaded = false;
static bool              g_paused      = false;

// Video
static enum retro_pixel_format g_pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static unsigned          g_rotation    = 0;      // 0, 90, 180, 270
static std::vector<uint32_t> g_display;          // ARGB8888 listo para SDL
static unsigned          g_display_w = 0, g_display_h = 0;
static bool              g_have_frame = false;

// Info del sistema
static struct retro_system_av_info g_av = {};
static std::string       g_rom_path;
static std::string       g_core_path;

// Directorios que se entregan al nucleo (deben sobrevivir a la llamada)
static std::string       g_system_dir;
static std::string       g_save_dir;
static std::string       g_assets_dir;

// Audio
static SDL_AudioDeviceID g_audio_dev = 0;
static bool              g_audio_on  = false;

// Interfaz
static std::string       g_exe_dir;
static int               g_menu_open = -1;       // indice de menu abierto o -1
static std::string       g_status;               // mensaje temporal
static Uint32            g_status_until = 0;

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------
static std::string ExeDir() {
    char *base = SDL_GetBasePath();
    if (base == nullptr)
        return std::string(".");
    std::string dir(base);
    SDL_free(base);
    // Quita el separador final para poder concatenar.
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    return dir;
}

static std::string JoinPath(const std::string &dir, const std::string &name) {
    if (dir.empty())
        return name;
    char last = dir.back();
    if (last == '/' || last == '\\')
        return dir + name;
    return dir + "/" + name;
}

static bool FileExists(const std::string &path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

static std::string BaseName(const std::string &path) {
    size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? path : path.substr(p + 1);
}

static void SetStatus(const std::string &msg) {
    g_status = msg;
    g_status_until = SDL_GetTicks() + 2500;
    SDL_Log("%s", msg.c_str());
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
    SDL_Log("[%s] %s", tag, buf);
}

// ---------------------------------------------------------------------------
// Dibujo de texto (fuente integrada)
// ---------------------------------------------------------------------------
static const FontGlyph *FindGlyph(char c) {
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (const FontGlyph &g : FONT_GLYPHS)
        if (g.ch == c)
            return &g;
    return nullptr;
}

static int TextHeight(int scale) { return FONT_GLYPH_H * scale; }

static void DrawText(SDL_Renderer *r, int x, int y, const std::string &text,
                     int scale, SDL_Color color) {
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
    int cx = x;
    for (char c : text) {
        const FontGlyph *g = FindGlyph(c);
        if (g != nullptr) {
            for (int row = 0; row < FONT_GLYPH_H; ++row) {
                const char *bits = g->rows[row];
                for (int col = 0; col < FONT_GLYPH_W; ++col) {
                    if (bits[col] != '#')
                        continue;
                    SDL_Rect px = { cx + col * scale, y + row * scale, scale, scale };
                    SDL_RenderFillRect(r, &px);
                }
            }
        }
        cx += FONT_ADVANCE * scale;
    }
}

static void FillRect(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderFillRect(r, &rc);
}

// ---------------------------------------------------------------------------
// Audio (SDL_AudioStream con conversion automatica)
// ---------------------------------------------------------------------------
static void CloseAudio() {
    if (g_audio_dev != 0) {
        SDL_CloseAudioDevice(g_audio_dev);
        g_audio_dev = 0;
    }
    g_audio_on = false;
}

static void OpenAudio(double sample_rate) {
    CloseAudio();

    if (sample_rate <= 0.0)
        sample_rate = 44100.0;

    SDL_AudioSpec want = {};
    want.freq     = (int)(sample_rate + 0.5);
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;

    SDL_AudioSpec have = {};
    g_audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audio_dev == 0) {
        SDL_Log("SDL_OpenAudioDevice fallo: %s; se continua sin audio", SDL_GetError());
        return;
    }

    SDL_PauseAudioDevice(g_audio_dev, 0);
    g_audio_on = true;
}

static void PushAudio(const int16_t *data, size_t frames) {
    if (!g_audio_on || data == nullptr || frames == 0)
        return;
    // Si el buffer del dispositivo se llena, SDL descarta el exceso; es
    // preferible a bloquear el hilo de emulacion.
    SDL_QueueAudio(g_audio_dev, data, (Uint32)(frames * 2 * sizeof(int16_t)));
}

// ---------------------------------------------------------------------------
// Callbacks del nucleo
// ---------------------------------------------------------------------------
static void VideoRefresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (data == nullptr) {
        // El nucleo reutiliza el frame anterior (dupe).
        return;
    }

    if (g_rotation == 90 || g_rotation == 270) {
        g_display_w = height;
        g_display_h = width;
    } else {
        g_display_w = width;
        g_display_h = height;
    }
    g_display.resize((size_t)g_display_w * g_display_h);

    const uint8_t *base = reinterpret_cast<const uint8_t *>(data);

    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            uint32_t px;
            switch (g_pixel_format) {
            case RETRO_PIXEL_FORMAT_XRGB8888: {
                const uint32_t *row = reinterpret_cast<const uint32_t *>(base + y * pitch);
                px = 0xFF000000u | (row[x] & 0x00FFFFFFu);
                break;
            }
            case RETRO_PIXEL_FORMAT_RGB565: {
                const uint16_t *row = reinterpret_cast<const uint16_t *>(base + y * pitch);
                uint16_t p = row[x];
                unsigned r5 = (p >> 11) & 0x1F;
                unsigned g6 = (p >> 5)  & 0x3F;
                unsigned b5 =  p        & 0x1F;
                unsigned r8 = (r5 << 3) | (r5 >> 2);
                unsigned g8 = (g6 << 2) | (g6 >> 4);
                unsigned b8 = (b5 << 3) | (b5 >> 2);
                px = 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
                break;
            }
            case RETRO_PIXEL_FORMAT_0RGB1555:
            default: {
                const uint16_t *row = reinterpret_cast<const uint16_t *>(base + y * pitch);
                uint16_t p = row[x];
                unsigned r5 = (p >> 10) & 0x1F;
                unsigned g5 = (p >> 5)  & 0x1F;
                unsigned b5 =  p        & 0x1F;
                unsigned r8 = (r5 << 3) | (r5 >> 2);
                unsigned g8 = (g5 << 3) | (g5 >> 2);
                unsigned b8 = (b5 << 3) | (b5 >> 2);
                px = 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
                break;
            }
            }

            unsigned dx, dy;
            switch (g_rotation) {
            case 90:  dx = height - 1 - y; dy = x;              break;
            case 180: dx = width - 1 - x;  dy = height - 1 - y; break;
            case 270: dx = y;              dy = width - 1 - x;  break;
            default:  dx = x;              dy = y;              break;
            }
            g_display[(size_t)dy * g_display_w + dx] = px;
        }
    }

    g_have_frame = true;
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
    // La entrada se lee de SDL_GetKeyboardState en InputState.
}

static int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port != 0 || index != 0 || device != RETRO_DEVICE_JOYPAD)
        return 0;

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    if (keys == nullptr)
        return 0;

    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_A:      return keys[SDL_SCANCODE_X]      ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_B:      return keys[SDL_SCANCODE_Z]      ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_Y:      return keys[SDL_SCANCODE_S]      ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_X:      return keys[SDL_SCANCODE_A]      ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_L:      return keys[SDL_SCANCODE_Q]      ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_R:      return keys[SDL_SCANCODE_W]      ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_START:  return keys[SDL_SCANCODE_RETURN] ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return keys[SDL_SCANCODE_BACKSPACE] ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_UP:     return keys[SDL_SCANCODE_UP]     ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_DOWN:   return keys[SDL_SCANCODE_DOWN]   ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_LEFT:   return keys[SDL_SCANCODE_LEFT]   ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return keys[SDL_SCANCODE_RIGHT]  ? 1 : 0;
    default: return 0;
    }
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
        *reinterpret_cast<const char **>(data) = g_system_dir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *reinterpret_cast<const char **>(data) = g_save_dir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *reinterpret_cast<const char **>(data) = g_assets_dir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        *reinterpret_cast<const char **>(data) = g_core_path.c_str();
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

    // El nucleo puede llamar a esta consulta con data == NULL. No es un
    // puntero de salida obligatorio: NULL significa "¿soportas bitmasks?".
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        if (data != nullptr)
            *reinterpret_cast<bool *>(data) = false;
        return false;   // false = usa el sondeo por boton (retro_input_state)

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
static void EnsureSaveDir() {
    std::error_code ec;
    std::filesystem::create_directories(g_save_dir, ec);
}

static std::string StatePath(int slot) {
    char name[64];
    snprintf(name, sizeof(name), "slot%02d.sts", slot);
    return JoinPath(g_save_dir, name);
}

static bool SaveState(int slot) {
    if (!g_game_loaded)
        return false;

    size_t size = g_core.retro_serialize_size();
    if (size == 0)
        return false;

    std::vector<uint8_t> buf(size);
    if (!g_core.retro_serialize(buf.data(), buf.size()))
        return false;

    EnsureSaveDir();
    std::string path = StatePath(slot);
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr)
        return false;

    bool ok = fwrite(buf.data(), 1, buf.size(), f) == buf.size();
    fclose(f);
    return ok;
}

static bool LoadState(int slot) {
    if (!g_game_loaded)
        return false;

    std::string path = StatePath(slot);
    FILE *f = fopen(path.c_str(), "rb");
    if (f == nullptr)
        return false;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return false; }

    std::vector<uint8_t> buf((size_t)len);
    size_t rd = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    if (rd != buf.size())
        return false;

    return g_core.retro_unserialize(buf.data(), buf.size());
}

// ---------------------------------------------------------------------------
// Carga de ROM
// ---------------------------------------------------------------------------
static void UnloadGame() {
    if (g_game_loaded) {
        CloseAudio();
        g_core.retro_unload_game();
        g_game_loaded = false;
        g_have_frame = false;
        g_display_w = g_display_h = 0;
    }
}

static void UpdateWindowTitle();

static bool LoadGame(const std::string &rom_path) {
    UnloadGame();

    // El nucleo declara need_fullpath = false, asi que espera los bytes de la
    // ROM en info.data/info.size; solo con info.path no carga nada.
    std::vector<uint8_t> rom;
    {
        FILE *f = fopen(rom_path.c_str(), "rb");
        if (f == nullptr) {
            SetStatus("No se pudo abrir la ROM: " + BaseName(rom_path));
            return false;
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len <= 0) {
            fclose(f);
            SetStatus("ROM vacia: " + BaseName(rom_path));
            return false;
        }
        rom.resize((size_t)len);
        size_t rd = fread(rom.data(), 1, rom.size(), f);
        fclose(f);
        if (rd != rom.size()) {
            SetStatus("Error leyendo la ROM: " + BaseName(rom_path));
            return false;
        }
    }

    struct retro_game_info info = {};
    info.path = rom_path.c_str();
    info.data = rom.data();
    info.size = rom.size();
    info.meta = nullptr;

    if (!g_core.retro_load_game(&info)) {
        SetStatus("No se pudo cargar la ROM: " + BaseName(rom_path));
        return false;
    }

    g_game_loaded = true;
    g_rom_path = rom_path;

    g_core.retro_get_system_av_info(&g_av);
    OpenAudio(g_av.timing.sample_rate);

    if (g_av.geometry.base_width > 0 && g_av.geometry.base_height > 0 && g_window != nullptr) {
        unsigned w = g_av.geometry.base_width;
        unsigned h = g_av.geometry.base_height;
        if (g_rotation == 90 || g_rotation == 270)
            std::swap(w, h);
        SDL_SetWindowSize(g_window, (int)w * 2, (int)h * 2 + MENUBAR_H);
    }

    UpdateWindowTitle();
    SetStatus("ROM cargada: " + BaseName(rom_path));
    return true;
}

// ---------------------------------------------------------------------------
// Explorador de archivos (simple, basado en SDL_EnumerateDirectory)
// ---------------------------------------------------------------------------
struct FileEntry {
    std::string name;
    bool        is_dir;
};

static std::string              g_browse_dir;
static std::vector<FileEntry>   g_browse_entries;
static int                      g_browse_sel = 0;
static bool                     g_browsing = false;

static void BrowseRefresh() {
    g_browse_entries.clear();

    FileEntry up;
    up.name = "..";
    up.is_dir = true;
    g_browse_entries.push_back(up);

    std::error_code ec;
    std::filesystem::directory_iterator it(g_browse_dir, ec);
    if (!ec) {
        for (const auto &entry : it) {
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.')
                continue;   // oculta entradas ocultas

            std::error_code ec2;
            bool is_dir = entry.is_directory(ec2);
            if (!is_dir) {
                std::string lower = name;
                for (char &c : lower) c = (char)tolower((unsigned char)c);
                bool is_rom = (lower.size() > 3 &&
                               lower.compare(lower.size() - 3, 3, ".ws") == 0) ||
                              (lower.size() > 4 &&
                               lower.compare(lower.size() - 4, 4, ".wsc") == 0);
                if (!is_rom)
                    continue;
            }

            FileEntry e;
            e.name = name;
            e.is_dir = is_dir;
            g_browse_entries.push_back(e);
        }
    }

    std::sort(g_browse_entries.begin() + 1, g_browse_entries.end(),
              [](const FileEntry &a, const FileEntry &b) {
                  if (a.is_dir != b.is_dir) return a.is_dir;
                  return a.name < b.name;
              });

    if (g_browse_sel >= (int)g_browse_entries.size())
        g_browse_sel = 0;
}

static void BrowseEnterDir(const std::string &name) {
    if (name == "..") {
        size_t p = g_browse_dir.find_last_of("/\\");
        if (p != std::string::npos && p > 0)
            g_browse_dir = g_browse_dir.substr(0, p);
    } else {
        g_browse_dir = JoinPath(g_browse_dir, name);
    }
    g_browse_sel = 0;
    BrowseRefresh();
}

static void BrowseActivate() {
    if (g_browse_entries.empty())
        return;
    const FileEntry &e = g_browse_entries[(size_t)g_browse_sel];
    if (e.is_dir) {
        BrowseEnterDir(e.name);
    } else {
        LoadGame(JoinPath(g_browse_dir, e.name));
    }
}

static void OpenRomBrowser() {
    g_browse_dir = g_rom_path.empty() ? g_exe_dir : g_rom_path;
    size_t p = g_browse_dir.find_last_of("/\\");
    if (p != std::string::npos)
        g_browse_dir = g_browse_dir.substr(0, p);
    g_browse_sel = 0;
    BrowseRefresh();
    g_browsing = true;
    g_menu_open = -1;
}

// ---------------------------------------------------------------------------
// Dialogo "Acerca de..."
// ---------------------------------------------------------------------------
static void ShowAbout() {
    bool open = true;
    while (open && g_running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                g_running = false;
                open = false;
            } else if (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEBUTTONDOWN)
                open = false;
        }

        int ww = 0, wh = 0;
        SDL_GetWindowSize(g_window, &ww, &wh);

        const int box_w = 460;
        const int box_h = 220;
        const int bx = (ww - box_w) / 2;
        const int by = (wh - box_h) / 2;

        SDL_SetRenderDrawColor(g_renderer, 20, 20, 24, 255);
        SDL_RenderClear(g_renderer);

        FillRect(g_renderer, bx, by, box_w, box_h, SDL_Color{ 40, 40, 48, 255 });
        FillRect(g_renderer, bx, by, box_w, 2, SDL_Color{ 200, 200, 210, 255 });
        FillRect(g_renderer, bx, by + box_h - 2, box_w, 2, SDL_Color{ 200, 200, 210, 255 });
        FillRect(g_renderer, bx, by, 2, box_h, SDL_Color{ 200, 200, 210, 255 });
        FillRect(g_renderer, bx + box_w - 2, by, 2, box_h, SDL_Color{ 200, 200, 210, 255 });

        SDL_Color title = { 255, 220, 120, 255 };
        SDL_Color body  = { 230, 230, 235, 255 };
        SDL_Color dim   = { 170, 170, 180, 255 };

        int ty = by + 22;
        DrawText(g_renderer, bx + 24, ty, WSCAMP_VERSION_FULL, UI_TEXT_SCALE, title);
        ty += TextHeight(UI_TEXT_SCALE) + 14;

        DrawText(g_renderer, bx + 24, ty, "Version: " WSCAMP_VERSION_STRING,
                 UI_TEXT_SCALE, body);
        ty += TextHeight(UI_TEXT_SCALE) + 10;

        DrawText(g_renderer, bx + 24, ty, "Emulador WonderSwan / WonderSwan Color",
                 UI_TEXT_SCALE, body);
        ty += TextHeight(UI_TEXT_SCALE) + 8;
        DrawText(g_renderer, bx + 24, ty, "Nucleo: beetle-wswan (mednafen/wswan)",
                 UI_TEXT_SCALE, dim);
        ty += TextHeight(UI_TEXT_SCALE) + 8;
        DrawText(g_renderer, bx + 24, ty, "Frontend libretro sobre SDL2",
                 UI_TEXT_SCALE, dim);
        ty += TextHeight(UI_TEXT_SCALE) + 18;
        DrawText(g_renderer, bx + 24, ty, "Pulsa cualquier tecla para cerrar",
                 UI_TEXT_SCALE, dim);

        SDL_RenderPresent(g_renderer);
        SDL_Delay(16);
    }
}

// ---------------------------------------------------------------------------
// Nucleo: carga y arranque
// ---------------------------------------------------------------------------
static const char *CoreLibraryName() {
#if defined(_WIN32)
    return "mednafen_wswan_libretro.dll";
#elif defined(__APPLE__)
    return "mednafen_wswan_libretro.dylib";
#else
    return "mednafen_wswan_libretro.so";
#endif
}

static std::string FindCoreLibrary() {
    const char *name = CoreLibraryName();

    // Alternativa habitual: mismo nombre con el prefijo "beetle_".
    std::string alt(name);
    size_t slash = alt.find_last_of("/\\");
    size_t dot = alt.find_last_of('.');
    if (dot != std::string::npos)
        alt = alt.substr(0, slash + 1) + "beetle_wswan_libretro" + alt.substr(dot);

    const std::string paths[] = {
        JoinPath(g_exe_dir, name),
        JoinPath(JoinPath(g_exe_dir, "lib"), name),
        JoinPath(g_exe_dir, alt),
        JoinPath(JoinPath(g_exe_dir, "lib"), alt),
    };

    for (const std::string &p : paths) {
        if (FileExists(p))
            return p;
    }
    return std::string();
}

static bool StartCore() {
    std::string dll = FindCoreLibrary();
    if (dll.empty()) {
        SetStatus(std::string("No se encontro el nucleo ") + CoreLibraryName() +
                  " junto al ejecutable ni en lib/");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, WSCAMP_APP_NAME,
            "No se encontro el nucleo libretro.\n\n"
            "Copia mednafen_wswan_libretro junto al ejecutable o en la "
            "carpeta lib/.\n\n"
            "Descargalo como build \"beetle-wswan-libretro\".", g_window);
        return false;
    }

    if (!g_core.Load(dll)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, WSCAMP_APP_NAME,
                                 g_core.last_error().c_str(), g_window);
        return false;
    }

    g_core_path = dll;

    unsigned api = g_core.retro_api_version();
    if (api != RETRO_API_VERSION) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, WSCAMP_APP_NAME,
                                 "Version de API libretro incompatible.", g_window);
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
// Interfaz: barra de menus
// ---------------------------------------------------------------------------
static const char *MENU_TITLES[MENU_COUNT] = { "Archivo", "Emulacion", "Estado", "Ayuda" };

struct MenuItem {
    std::string label;
    int         command;
    bool        separator;
};

static std::vector<MenuItem> MenuItems(int menu) {
    std::vector<MenuItem> items;
    switch (menu) {
    case MENU_FILE:
        items.push_back({ "Abrir ROM...  (Ctrl+O)", CMD_FILE_OPEN, false });
        items.push_back({ "", 0, true });
        items.push_back({ "Salir", CMD_FILE_EXIT, false });
        break;
    case MENU_EMU:
        items.push_back({ "Reiniciar", CMD_EMU_RESET, false });
        items.push_back({ "Pausa  (Espacio)", CMD_EMU_PAUSE, false });
        break;
    case MENU_STATE:
        for (int i = 0; i < WSCAMP_STATE_SLOTS; ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Guardar estado %d", i);
            items.push_back({ buf, CMD_SAVE_STATE_BASE + i, false });
        }
        items.push_back({ "", 0, true });
        for (int i = 0; i < WSCAMP_STATE_SLOTS; ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Cargar estado %d", i);
            items.push_back({ buf, CMD_LOAD_STATE_BASE + i, false });
        }
        break;
    case MENU_HELP:
        items.push_back({ "Acerca de " WSCAMP_APP_NAME "...", CMD_HELP_ABOUT, false });
        break;
    default: break;
    }
    return items;
}

static void UpdateWindowTitle() {
    if (g_window == nullptr)
        return;
    std::string title = std::string(WSCAMP_APP_NAME) + " " + WSCAMP_VERSION_STRING;
    if (g_game_loaded)
        title += " - " + BaseName(g_rom_path);
    else
        title += " - sin ROM";
    if (g_paused)
        title += " [PAUSA]";
    SDL_SetWindowTitle(g_window, title.c_str());
}

static void ExecuteCommand(int cmd) {
    if (cmd == CMD_FILE_OPEN) {
        OpenRomBrowser();
    } else if (cmd == CMD_FILE_EXIT) {
        g_running = false;
    } else if (cmd == CMD_EMU_RESET) {
        if (g_game_loaded) g_core.retro_reset();
    } else if (cmd == CMD_EMU_PAUSE) {
        g_paused = !g_paused;
        UpdateWindowTitle();
    } else if (cmd == CMD_HELP_ABOUT) {
        ShowAbout();
    } else if (cmd >= CMD_SAVE_STATE_BASE && cmd < CMD_SAVE_STATE_BASE + WSCAMP_STATE_SLOTS) {
        int slot = cmd - CMD_SAVE_STATE_BASE;
        SetStatus(SaveState(slot) ? ("Estado guardado en ranura " + std::to_string(slot))
                                  : ("No se pudo guardar la ranura " + std::to_string(slot)));
    } else if (cmd >= CMD_LOAD_STATE_BASE && cmd < CMD_LOAD_STATE_BASE + WSCAMP_STATE_SLOTS) {
        int slot = cmd - CMD_LOAD_STATE_BASE;
        SetStatus(LoadState(slot) ? ("Estado cargado de la ranura " + std::to_string(slot))
                                  : ("No se pudo cargar la ranura " + std::to_string(slot)));
    }
}

// ---------------------------------------------------------------------------
// Dibujo de la interfaz
// ---------------------------------------------------------------------------
static void RenderEmulator(int ww, int wh) {
    FillRect(g_renderer, 0, 0, ww, wh, SDL_Color{ 0, 0, 0, 255 });

    int view_h = wh - MENUBAR_H;
    if (g_have_frame && g_display_w > 0 && g_display_h > 0 && view_h > 0) {
        int tex_w = 0, tex_h = 0;
        if (g_texture != nullptr)
            SDL_QueryTexture(g_texture, nullptr, nullptr, &tex_w, &tex_h);

        if (g_texture == nullptr || tex_w != (int)g_display_w || tex_h != (int)g_display_h) {
            if (g_texture != nullptr)
                SDL_DestroyTexture(g_texture);
            g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          (int)g_display_w, (int)g_display_h);
            if (g_texture != nullptr)
                SDL_SetTextureBlendMode(g_texture, SDL_BLENDMODE_NONE);
        }

        if (g_texture != nullptr) {
            SDL_UpdateTexture(g_texture, nullptr, g_display.data(),
                              (int)g_display_w * 4);

            double scale = (double)ww / g_display_w;
            double sy = (double)view_h / g_display_h;
            if (sy < scale) scale = sy;
            if (scale <= 0.0) scale = 1.0;

            int dw = (int)(g_display_w * scale);
            int dh = (int)(g_display_h * scale);
            int dx = (ww - dw) / 2;
            int dy = MENUBAR_H + (view_h - dh) / 2;

            SDL_Rect dst = { dx, dy, dw, dh };
            SDL_RenderCopy(g_renderer, g_texture, nullptr, &dst);
        }
    }

    // Barra de menus
    FillRect(g_renderer, 0, 0, ww, MENUBAR_H, SDL_Color{ 55, 55, 65, 255 });
    SDL_Color fg = { 230, 230, 235, 255 };
    for (int i = 0; i < MENU_COUNT; ++i) {
        int x = 6 + i * MENUBAR_ITEM_W;
        bool hot = (g_menu_open == i);
        if (hot)
            FillRect(g_renderer, x - 4, 2, MENUBAR_ITEM_W - 4, MENUBAR_H - 4,
                     SDL_Color{ 90, 90, 105, 255 });
        DrawText(g_renderer, x, 7, MENU_TITLES[i], UI_TEXT_SCALE, fg);
    }

    // Mensaje de estado
    if (!g_status.empty() && SDL_GetTicks() < g_status_until) {
        FillRect(g_renderer, 0, wh - 24, ww, 24, SDL_Color{ 30, 30, 36, 220 });
        DrawText(g_renderer, 8, wh - 20, g_status, UI_TEXT_SCALE,
                 SDL_Color{ 200, 220, 255, 255 });
    }
}

static void RenderMenuDropdown() {
    if (g_menu_open < 0)
        return;

    std::vector<MenuItem> items = MenuItems(g_menu_open);
    int x = 6 + g_menu_open * MENUBAR_ITEM_W - 4;
    int y = MENUBAR_H;

    FillRect(g_renderer, x, y, MENU_ITEM_W, (int)items.size() * MENU_ITEM_H + 4,
             SDL_Color{ 45, 45, 55, 255 });
    SDL_Color border = { 140, 140, 155, 255 };
    FillRect(g_renderer, x, y, MENU_ITEM_W, 1, border);

    int my = y + 2;
    for (const MenuItem &it : items) {
        if (!it.separator) {
            SDL_Color c = { 230, 230, 235, 255 };
            DrawText(g_renderer, x + 8, my + 4, it.label, UI_TEXT_SCALE, c);
        } else {
            FillRect(g_renderer, x + 4, my + MENU_ITEM_H / 2, MENU_ITEM_W - 8, 1,
                     SDL_Color{ 100, 100, 115, 255 });
        }
        my += MENU_ITEM_H;
    }
}

static void RenderBrowser(int ww, int wh) {
    FillRect(g_renderer, 0, 0, ww, wh, SDL_Color{ 18, 18, 22, 255 });

    DrawText(g_renderer, 12, 12, "Abrir ROM - " + g_browse_dir, UI_TEXT_SCALE,
             SDL_Color{ 255, 220, 120, 255 });

    int y = 48;
    for (size_t i = 0; i < g_browse_entries.size(); ++i) {
        if (y > wh - 40)
            break;
        bool sel = ((int)i == g_browse_sel);
        if (sel)
            FillRect(g_renderer, 8, y - 3, ww - 16, MENU_ITEM_H + 4,
                     SDL_Color{ 70, 70, 90, 255 });

        std::string label = g_browse_entries[i].name;
        if (g_browse_entries[i].is_dir)
            label = "[DIR] " + label;
        DrawText(g_renderer, 16, y, label, UI_TEXT_SCALE,
                 sel ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 210, 210, 220, 255 });
        y += MENU_ITEM_H + 4;
    }

    DrawText(g_renderer, 12, wh - 26, "Enter: abrir   Flechas: mover   Esc: cancelar",
             UI_TEXT_SCALE, SDL_Color{ 160, 160, 175, 255 });
}

// ---------------------------------------------------------------------------
// Bucle principal
// ---------------------------------------------------------------------------


static void HandleKeyDown(const SDL_KeyboardEvent &k) {
    bool ctrl = (k.keysym.mod & KMOD_CTRL) != 0;

    if (g_browsing) {
        if (k.keysym.sym == SDLK_ESCAPE) {
            g_browsing = false;
        } else if (k.keysym.sym == SDLK_DOWN) {
            if (g_browse_sel + 1 < (int)g_browse_entries.size()) g_browse_sel++;
        } else if (k.keysym.sym == SDLK_UP) {
            if (g_browse_sel > 0) g_browse_sel--;
        } else if (k.keysym.sym == SDLK_RETURN || k.keysym.sym == SDLK_KP_ENTER) {
            BrowseActivate();
            // Si se cargo una ROM, se cierra el explorador.
            if (g_game_loaded)
                g_browsing = false;
        }
        return;
    }

    if (k.keysym.sym == SDLK_ESCAPE) {
        g_menu_open = -1;
    } else if (ctrl && k.keysym.sym == SDLK_o) {
        ExecuteCommand(CMD_FILE_OPEN);
    } else if (k.keysym.sym == SDLK_SPACE) {
        ExecuteCommand(CMD_EMU_PAUSE);
    }
}

static void HandleMouseDown(const SDL_MouseButtonEvent &m) {
    if (g_browsing)
        return;

    if (m.y < MENUBAR_H) {
        int idx = (m.x - 6 + 4) / MENUBAR_ITEM_W;
        if (idx >= 0 && idx < MENU_COUNT) {
            g_menu_open = (g_menu_open == idx) ? -1 : idx;
            return;
        }
    }

    if (g_menu_open >= 0) {
        std::vector<MenuItem> items = MenuItems(g_menu_open);
        int x = 6 + g_menu_open * MENUBAR_ITEM_W - 4;
        int y = MENUBAR_H + 2;
        int hit = (m.y - y) / MENU_ITEM_H;
        if (m.x >= x && m.x < x + MENU_ITEM_W && hit >= 0 && hit < (int)items.size()) {
            const MenuItem &it = items[(size_t)hit];
            if (!it.separator) {
                g_menu_open = -1;
                ExecuteCommand(it.command);
            }
        } else {
            g_menu_open = -1;
        }
        return;
    }
}

static void HandleBrowserMouse(const SDL_MouseButtonEvent &m) {
    int y = 48;
    for (size_t i = 0; i < g_browse_entries.size(); ++i) {
        int h = MENU_ITEM_H + 4;
        if (m.y >= y - 3 && m.y < y - 3 + h) {
            g_browse_sel = (int)i;
            if (m.clicks >= 2)
                BrowseActivate();
            return;
        }
        y += h;
    }
}

// ---------------------------------------------------------------------------
// Autocomprobacion (--selftest): carga una ROM, guarda un estado, avanza y lo
// vuelve a cargar. Sirve como smoke test sin interfaz.
// ---------------------------------------------------------------------------
static int SelfTest(const std::string &rom_path) {
    if (!StartCore()) {
        fprintf(stderr, "SELFTEST FALLO: no se pudo iniciar el nucleo\n");
        return 1;
    }
    if (!LoadGame(rom_path)) {
        fprintf(stderr, "SELFTEST FALLO: no se pudo cargar %s\n", rom_path.c_str());
        StopCore();
        return 1;
    }

    size_t sz = g_core.retro_serialize_size();
    printf("SELFTEST: serialize_size = %zu\n", sz);
    if (sz == 0) {
        fprintf(stderr, "SELFTEST FALLO: el nucleo no soporta serializacion\n");
        StopCore();
        return 1;
    }

    for (int i = 0; i < 120; ++i)
        g_core.retro_run();

    if (!SaveState(0)) {
        fprintf(stderr, "SELFTEST FALLO: no se pudo guardar el estado\n");
        StopCore();
        return 1;
    }
    printf("SELFTEST: estado guardado en %s\n", StatePath(0).c_str());

    for (int i = 0; i < 60; ++i)
        g_core.retro_run();

    if (!LoadState(0)) {
        fprintf(stderr, "SELFTEST FALLO: no se pudo cargar el estado\n");
        StopCore();
        return 1;
    }

    printf("SELFTEST: OK (version %s)\n", WSCAMP_VERSION_STRING);
    StopCore();
    return 0;
}

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init fallo: %s\n", SDL_GetError());
        return 1;
    }

    g_exe_dir = ExeDir();
    g_system_dir = g_exe_dir;
    g_save_dir   = JoinPath(g_exe_dir, "saves");
    g_assets_dir = JoinPath(g_exe_dir, "system");

    if (argc > 2 && strcmp(argv[1], "--selftest") == 0) {
        int rc = SelfTest(argv[2]);
        SDL_Quit();
        return rc;
    }

    g_window = SDL_CreateWindow(WSCAMP_VERSION_FULL,
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                448 * 2, 288 * 2 + MENUBAR_H,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (g_window == nullptr) {
        fprintf(stderr, "SDL_CreateWindow fallo: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    g_renderer = SDL_CreateRenderer(g_window, -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (g_renderer == nullptr) {
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (g_renderer == nullptr) {
        fprintf(stderr, "SDL_CreateRenderer fallo: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }

    UpdateWindowTitle();

    if (!StartCore()) {
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }

    // ROM pasada por linea de comandos.
    if (argc > 1)
        LoadGame(argv[1]);

    double fps = 60.0;
    Uint64 next_frame = SDL_GetTicks64();

    while (g_running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                g_running = false;
                break;
            case SDL_KEYDOWN:
                if (!ev.key.repeat)
                    HandleKeyDown(ev.key);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    if (g_browsing) HandleBrowserMouse(ev.button);
                    else            HandleMouseDown(ev.button);
                }
                break;
            default:
                break;
            }
        }
        if (!g_running)
            break;

        if (g_game_loaded && !g_paused) {
            g_core.retro_run();
            fps = (g_av.timing.fps > 0.0) ? g_av.timing.fps : 60.0;
        }

        int ww = 0, wh = 0;
        SDL_GetWindowSize(g_window, &ww, &wh);

        if (g_browsing) {
            RenderBrowser(ww, wh);
        } else {
            RenderEmulator(ww, wh);
            RenderMenuDropdown();
        }
        SDL_RenderPresent(g_renderer);

        // Sincronizacion de frames.
        if (g_game_loaded && !g_paused && !g_browsing) {
            double frame_ms = 1000.0 / fps;
            next_frame += (Uint64)frame_ms;
            Uint64 now = SDL_GetTicks64();
            if (next_frame > now) {
                Uint32 wait = (Uint32)(next_frame - now);
                if (wait > 1) SDL_Delay(wait);
            } else if (now - next_frame > (Uint64)(frame_ms * 4)) {
                next_frame = now;   // se ha quedado atras: resincroniza
            }
        } else {
            next_frame = SDL_GetTicks64();
            SDL_Delay(16);
        }
    }

    StopCore();
    if (g_texture) SDL_DestroyTexture(g_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
