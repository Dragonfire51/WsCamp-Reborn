// main.cpp -- WsCampReborn: frontend libretro portable (SDL2).
//
// Carga el nucleo beetle-wswan (mednafen_wswan_libretro) en tiempo de ejecucion
// y ofrece 10 ranuras de Save/Load State, menu de Opciones (video, sonido y
// controles) y guardado de la configuracion en WsCampReborn.ini.
//
// Compilar con: make        (ver Makefile)

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "config.h"
#include "font5x7.h"
#include "menu.h"
#include "nfd.h"
#include "version.h"
#include "video.h"
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
static int               g_menu_open  = -1;      // indice de menu abierto o -1
static int               g_submenu     = SUB_NONE;
static int               g_menu_sel    = 0;      // entrada con el foco en el menu
static int               g_submenu_sel = 0;      // entrada con el foco en el submenu
static std::string       g_status;               // mensaje temporal
static Uint32            g_status_until = 0;

// Mapeo de teclas
static int               g_capture_button = -1;  // boton que espera una tecla

// Ultimo tamano base con el que se ajusto la ventana; sirve para reaccionar
// cuando el nucleo cambia de orientacion (juegos verticales).
static unsigned          g_sized_base_w = 0, g_sized_base_h = 0;

// Se pone a true cuando la ventana cambia de tamano, para forzar un redibujado
// completo aunque la emulacion este en pausa o en un menu.
static bool              g_window_dirty = false;

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

static std::string DirName(const std::string &path) {
    size_t p = path.find_last_of("/\\");
    if (p == std::string::npos)
        return std::string();
    return path.substr(0, p);
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

static int TextWidth(const std::string &text, int scale) {
    return (int)text.size() * FONT_ADVANCE * scale;
}

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
// Audio (SDL_QueueAudio)
// ---------------------------------------------------------------------------
static void CloseAudio() {
    if (g_audio_dev != 0) {
        SDL_CloseAudioDevice(g_audio_dev);
        g_audio_dev = 0;
    }
    g_audio_on = false;
}

// El silencio general se aplica pausando el dispositivo: asi el corte es
// inmediato y no depende de lo que quede en la cola.
static void ApplyMasterMute() {
    if (g_audio_dev != 0)
        SDL_PauseAudioDevice(g_audio_dev, g_cfg.master ? 0 : 1);
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

    g_audio_on = true;
    ApplyMasterMute();
}

static void PushAudio(const int16_t *data, size_t frames) {
    if (!g_audio_on || !g_cfg.master || data == nullptr || frames == 0)
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
    ConvertFrame(data, width, height, pitch, g_pixel_format, 0,
                 g_display, g_display_w, g_display_h);
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

    for (int b = 0; b < WSB_COUNT; ++b) {
        if (WsButtonRetroId(b) == id) {
            SDL_Scancode sc = g_cfg.bind[b];
            if (sc != SDL_SCANCODE_UNKNOWN && keys[sc])
                return 1;
            return 0;
        }
    }
    return 0;
}

// Valores de las opciones que el nucleo pide con GET_VARIABLE. Las claves de
// sonido solo existen si se usa el nucleo parcheado que las define; con el
// nucleo original simplemente no se consultan.
static const char *OptionValue(const char *key) {
    if (strcmp(key, "wswan_sound_ch1") == 0)   return g_cfg.sound[SND_CH1]   ? "enabled" : "disabled";
    if (strcmp(key, "wswan_sound_ch2") == 0)   return g_cfg.sound[SND_CH2]   ? "enabled" : "disabled";
    if (strcmp(key, "wswan_sound_ch3") == 0)   return g_cfg.sound[SND_CH3]   ? "enabled" : "disabled";
    if (strcmp(key, "wswan_sound_ch4") == 0)   return g_cfg.sound[SND_CH4]   ? "enabled" : "disabled";
    if (strcmp(key, "wswan_sound_noise") == 0) return g_cfg.sound[SND_NOISE] ? "enabled" : "disabled";
    if (strcmp(key, "wswan_sound_pcm") == 0)   return g_cfg.sound[SND_PCM]   ? "enabled" : "disabled";
    if (strcmp(key, "wswan_sound_dma") == 0)   return g_cfg.sound[SND_DMA]   ? "enabled" : "disabled";
    if (strcmp(key, "wswan_rotate_display") == 0) return RotationName(g_cfg.rotate);
    if (strcmp(key, "wswan_rotate_keymap") == 0)  return KeymapName(g_cfg.keymap);
    if (strcmp(key, "wswan_60hz_mode") == 0)   return g_cfg.mode_60hz ? "enabled" : "disabled";
    return nullptr;
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
        // Se responde que no: asi el nucleo rota el fotograma el mismo (es la
        // ruta que usa con los frontends sin rotacion por hardware) y lo
        // entrega ya con la geometria definitiva, 144x224 en vertical. Si se
        // aceptara, el nucleo delegaria en el frontend y habria que replicar
        // su convencion de giro (0/1/2/3 = 0/90/180/270), con riesgo de
        // dejarlo invertido.
        return false;

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

    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = reinterpret_cast<struct retro_variable *>(data);
        if (var == nullptr || var->key == nullptr)
            return false;
        const char *value = OptionValue(var->key);
        if (value == nullptr)
            return false;   // el nucleo usara su valor por defecto
        var->value = value;
        return true;
    }

    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
        // El nucleo relee las opciones cuando ve "true". Los valores se
        // sirven desde g_cfg, asi que basta con avisar una vez.
        *reinterpret_cast<bool *>(data) = g_options_dirty;
        g_options_dirty = false;
        return true;
    }

    // El nucleo puede llamar a esta consulta con data == NULL. No es un
    // puntero de salida obligatorio: NULL significa "soportas bitmasks?".
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        if (data != nullptr)
            *reinterpret_cast<bool *>(data) = false;
        return false;   // false = usa el sondeo por boton (retro_input_state)

    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *reinterpret_cast<unsigned *>(data) = 0;   // interfaz clasica (SET_VARIABLES)
        return true;

    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        // Solo trae geometria. Leerlo como av_info completo machacaria fps y
        // sample_rate con basura, asi que se copia unicamente el bloque de
        // geometria y se deja intacto el resto.
        const struct retro_game_geometry *geo =
            reinterpret_cast<const struct retro_game_geometry *>(data);
        if (geo)
            g_av.geometry = *geo;
        return true;
    }
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
static void ApplyWindowSize();

static bool LoadGame(const std::string &rom_path) {
    UnloadGame();

    FILE *f = fopen(rom_path.c_str(), "rb");
    if (f == nullptr) {
        SetStatus("No se pudo abrir la ROM: " + BaseName(rom_path));
        return false;
    }

    // beetle-wswan declara need_fullpath = false, asi que espera los bytes de
    // la ROM en info.data; hay que mantener el buffer vivo durante la partida.
    static std::vector<uint8_t> rom;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        fclose(f);
        SetStatus("La ROM esta vacia: " + BaseName(rom_path));
        return false;
    }
    rom.resize((size_t)len);
    size_t rd = fread(rom.data(), 1, rom.size(), f);
    fclose(f);
    if (rd != rom.size()) {
        SetStatus("No se pudo leer la ROM: " + BaseName(rom_path));
        return false;
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

    g_rom_path    = rom_path;
    g_game_loaded = true;
    g_paused      = false;

    g_core.retro_get_system_av_info(&g_av);
    OpenAudio(g_av.timing.sample_rate);
    ApplyWindowSize();
    UpdateWindowTitle();
    SetStatus("ROM cargada: " + BaseName(rom_path));
    return true;
}

// ---------------------------------------------------------------------------
// Explorador de archivos integrado (respaldo del dialogo nativo)
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

// Devuelve el directorio de partida del explorador.
static std::string BrowseStartDir() {
    if (!g_rom_path.empty()) {
        std::string dir = DirName(g_rom_path);
        if (!dir.empty())
            return dir;
    }
    return g_exe_dir;
}

static void OpenBuiltinBrowser() {
    g_browse_dir = BrowseStartDir();
    g_browse_sel = 0;
    BrowseRefresh();
    g_browsing = true;
    g_menu_open = -1;
    g_submenu   = SUB_NONE;
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
static const char *MENU_TITLES[MENU_COUNT] = {
    "Archivo", "Emulacion", "Estado", "Opciones", "Ayuda"
};

// Ancho de la barra: se calcula para que las etiquetas no se solapen.
static int MenuBarItemWidth() {
    int widest = 0;
    for (int i = 0; i < MENU_COUNT; ++i) {
        int w = TextWidth(MENU_TITLES[i], UI_TEXT_SCALE);
        if (w > widest)
            widest = w;
    }
    return widest + 18;
}

static int MenuBarItemX(int index) {
    return 6 + index * MenuBarItemWidth();
}

struct MenuItem {
    std::string label;
    int         command;
    bool        separator;
    bool        checked;
    bool        has_submenu;
};

static MenuItem Item(const std::string &label, int command) {
    MenuItem it;
    it.label       = label;
    it.command     = command;
    it.separator   = false;
    it.checked     = false;
    it.has_submenu = false;
    return it;
}

static MenuItem Checked(const std::string &label, int command, bool on) {
    MenuItem it = Item(label, command);
    it.checked  = on;
    return it;
}

static MenuItem Separator() {
    MenuItem it = Item("", 0);
    it.separator = true;
    return it;
}

static MenuItem SubmenuItem(const std::string &label, int submenu) {
    MenuItem it = Item(label, submenu);
    it.has_submenu = true;
    return it;
}

static std::vector<MenuItem> MenuItems(int menu) {
    std::vector<MenuItem> items;
    switch (menu) {
    case MENU_FILE:
        items.push_back(Item("Abrir ROM...  (Ctrl+O)", CMD_FILE_OPEN));
        items.push_back(Separator());
        items.push_back(Item("Salir", CMD_FILE_EXIT));
        break;
    case MENU_EMU:
        items.push_back(Item("Reiniciar", CMD_EMU_RESET));
        items.push_back(Checked("Pausa  (Espacio)", CMD_EMU_PAUSE, g_paused));
        break;
    case MENU_STATE:
        for (int i = 0; i < WSCAMP_STATE_SLOTS; ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Guardar estado %d", i);
            items.push_back(Item(buf, CMD_SAVE_STATE_BASE + i));
        }
        items.push_back(Separator());
        for (int i = 0; i < WSCAMP_STATE_SLOTS; ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Cargar estado %d", i);
            items.push_back(Item(buf, CMD_LOAD_STATE_BASE + i));
        }
        break;
    case MENU_OPTIONS:
        items.push_back(SubmenuItem("Video", SUB_VIDEO));
        items.push_back(SubmenuItem("Sonido", SUB_SOUND));
        items.push_back(SubmenuItem("Controles", SUB_INPUT));
        break;
    case MENU_HELP:
        items.push_back(Item("Acerca de " WSCAMP_APP_NAME "...", CMD_HELP_ABOUT));
        break;
    default: break;
    }
    return items;
}

static std::vector<MenuItem> SubMenuItems(int submenu) {
    std::vector<MenuItem> items;
    switch (submenu) {
    case SUB_VIDEO:
        items.push_back(Checked("Zoom 1x", CMD_VIDEO_ZOOM_1, g_cfg.zoom == 1));
        items.push_back(Checked("Zoom 2x", CMD_VIDEO_ZOOM_2, g_cfg.zoom == 2));
        items.push_back(Checked("Zoom 3x", CMD_VIDEO_ZOOM_3, g_cfg.zoom == 3));
        items.push_back(Checked("Zoom 4x", CMD_VIDEO_ZOOM_4, g_cfg.zoom == 4));
        items.push_back(Separator());
        items.push_back(Item("Alternar horizontal / vertical  (F1)", CMD_VIDEO_ROT_TOGGLE));
        items.push_back(Checked("Orientacion: automatica (Select)", CMD_VIDEO_ROT_BASE + ROT_MANUAL,
                                g_cfg.rotate == ROT_MANUAL));
        items.push_back(Checked("Orientacion: vertical", CMD_VIDEO_ROT_BASE + ROT_PORTRAIT,
                                g_cfg.rotate == ROT_PORTRAIT));
        items.push_back(Checked("Orientacion: horizontal", CMD_VIDEO_ROT_BASE + ROT_LANDSCAPE,
                                g_cfg.rotate == ROT_LANDSCAPE));
        items.push_back(Separator());
        items.push_back(Checked("Modo 60 Hz", CMD_VIDEO_60HZ, g_cfg.mode_60hz));
        break;
    case SUB_SOUND:
        items.push_back(Checked("Silencio general", CMD_SOUND_MASTER, !g_cfg.master));
        items.push_back(Separator());
        for (int i = 0; i < SND_COUNT; ++i)
            items.push_back(Checked(SoundItemName(i), CMD_SOUND_CH_BASE + i,
                                    g_cfg.sound[i]));
        break;
    case SUB_INPUT: {
        for (int b = 0; b < WSB_COUNT; ++b) {
            std::string label = std::string(WsButtonName(b)) + ": " +
                                KeyDisplayName(g_cfg.bind[b]);
            items.push_back(Item(label, CMD_INPUT_BIND_BASE + b));
        }
        items.push_back(Separator());
        items.push_back(Checked("Cruceta al rotar: automatica",
                                CMD_INPUT_KEYMAP_BASE + KEYMAP_AUTO,
                                g_cfg.keymap == KEYMAP_AUTO));
        items.push_back(Checked("Cruceta al rotar: solo horizontal",
                                CMD_INPUT_KEYMAP_BASE + KEYMAP_LANDSCAPE,
                                g_cfg.keymap == KEYMAP_LANDSCAPE));
        items.push_back(Checked("Cruceta al rotar: solo vertical",
                                CMD_INPUT_KEYMAP_BASE + KEYMAP_PORTRAIT,
                                g_cfg.keymap == KEYMAP_PORTRAIT));
        items.push_back(Separator());
        items.push_back(Item("Restaurar valores por defecto", CMD_INPUT_RESET));
        break;
    }
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

// Tamano base del juego segun la orientacion actual (224x144 o 144x224).
static void BaseGameSize(unsigned &w, unsigned &h) {
    w = (g_av.geometry.base_width  > 0) ? g_av.geometry.base_width  : 224;
    h = (g_av.geometry.base_height > 0) ? g_av.geometry.base_height : 144;
}

// Ajusta la ventana al zoom elegido. Si el usuario la ha maximizado (o la ha
// redimensionado a mano), no se le cambia el tamano: solo se toca cuando la
// ventana es la que fijo el zoom. Antes se forzaba siempre y por eso al
// maximizar y restaurar saltaba a un tamano equivocado.
static void ApplyWindowSize() {
    if (g_window == nullptr)
        return;
    if ((SDL_GetWindowFlags(g_window) &
         (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN |
          SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0)
        return;

    unsigned base_w = 0, base_h = 0;
    BaseGameSize(base_w, base_h);
    SDL_SetWindowSize(g_window,
                      (int)base_w * g_cfg.zoom,
                      (int)base_h * g_cfg.zoom + MENUBAR_H);
}

static void SaveConfig() {
    if (!g_config_path.empty())
        g_cfg.Save(g_config_path);
}

// ---------------------------------------------------------------------------
// Orientacion
// ---------------------------------------------------------------------------
// El WonderSwan se gira 90 grados para los juegos verticales. F1 hace lo mismo
// que girar la consola: alterna entre horizontal y vertical, y el nucleo se
// encarga de la imagen y de remapear la cruceta. Cygne (emulador Win32 de
// referencia) lo hace igual con su tecla de flip.
static bool OrientationIsPortrait() {
    if (g_cfg.rotate == ROT_PORTRAIT)
        return true;
    if (g_cfg.rotate == ROT_LANDSCAPE)
        return false;
    // En automatico manda el nucleo: la pantalla es vertical si es mas alta
    // que ancha.
    return g_av.geometry.base_height > g_av.geometry.base_width;
}

static void ToggleOrientation() {
    // Se parte de la orientacion efectiva para que el primer F1 haga lo que
    // el usuario espera aunque la opcion este en automatico.
    g_cfg.rotate = OrientationIsPortrait() ? ROT_LANDSCAPE : ROT_PORTRAIT;
    g_options_dirty = true;
    SaveConfig();
    SetStatus(g_cfg.rotate == ROT_PORTRAIT ? "Pantalla vertical (144x224)"
                                           : "Pantalla horizontal (224x144)");
}

static void BeginFrame(int, int) {
    SDL_SetRenderTarget(g_renderer, nullptr);
}

static void EndFrame() {
    SDL_RenderPresent(g_renderer);
}

static void ExecuteCommand(int cmd);

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

            // La imagen llena el area de vision manteniendo su aspecto. El
            // zoom solo decide el tamano inicial de la ventana.
            int dx = 0, dy = 0, dw = 0, dh = 0;
            FitGameRect(ww, view_h, (int)g_display_w, (int)g_display_h,
                        &dx, &dy, &dw, &dh);
            dy += MENUBAR_H;

            SDL_Rect dst = { dx, dy, dw, dh };
            SDL_RenderCopy(g_renderer, g_texture, nullptr, &dst);
        }
    }

    // Barra de menus
    FillRect(g_renderer, 0, 0, ww, MENUBAR_H, SDL_Color{ 55, 55, 65, 255 });
    SDL_Color fg = { 230, 230, 235, 255 };
    int item_w = MenuBarItemWidth();
    for (int i = 0; i < MENU_COUNT; ++i) {
        int x = MenuBarItemX(i);
        bool hot = (g_menu_open == i);
        if (hot)
            FillRect(g_renderer, x - 4, 2, item_w - 4, MENUBAR_H - 4,
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

// Ancho que necesita una lista desplegable.
static int ListWidth(const std::vector<MenuItem> &items) {
    int w = MENU_ITEM_W;
    for (const MenuItem &it : items) {
        int lw = TextWidth(it.label, UI_TEXT_SCALE) + 46;
        if (lw > w)
            w = lw;
    }
    return w;
}

// Dibuja una lista desplegable y devuelve su altura total.
static int RenderList(int x, int y, const std::vector<MenuItem> &items, int focus) {
    int w = ListWidth(items);
    int h = (int)items.size() * MENU_ITEM_H + 4;
    FillRect(g_renderer, x, y, w, h, SDL_Color{ 45, 45, 55, 255 });
    SDL_Color border = { 140, 140, 155, 255 };
    FillRect(g_renderer, x, y, w, 1, border);
    FillRect(g_renderer, x, y, 1, h, border);
    FillRect(g_renderer, x + w - 1, y, 1, h, border);
    FillRect(g_renderer, x, y + h - 1, w, 1, border);

    int my = y + 2;
    for (size_t i = 0; i < items.size(); ++i) {
        const MenuItem &it = items[i];
        if (focus >= 0 && (int)i == focus)
            FillRect(g_renderer, x + 1, my, w - 2, MENU_ITEM_H,
                     SDL_Color{ 80, 80, 100, 255 });

        if (it.separator) {
            FillRect(g_renderer, x + 4, my + MENU_ITEM_H / 2, w - 8, 1,
                     SDL_Color{ 100, 100, 115, 255 });
        } else {
            SDL_Color c = { 230, 230, 235, 255 };
            if (it.checked)
                DrawText(g_renderer, x + 6, my + 4, ">", UI_TEXT_SCALE,
                         SDL_Color{ 150, 230, 150, 255 });
            DrawText(g_renderer, x + 22, my + 4, it.label, UI_TEXT_SCALE, c);
            if (it.has_submenu)
                DrawText(g_renderer, x + w - 14, my + 4, ">", UI_TEXT_SCALE,
                         SDL_Color{ 200, 200, 210, 255 });
        }
        my += MENU_ITEM_H;
    }
    return h;
}

static void RenderMenuDropdown() {
    if (g_menu_open < 0)
        return;

    std::vector<MenuItem> items = MenuItems(g_menu_open);
    int x = MenuBarItemX(g_menu_open) - 4;
    int y = MENUBAR_H;
    int parent_w = ListWidth(items);
    RenderList(x, y, items, g_submenu == SUB_NONE ? g_menu_sel : -1);

    if (g_submenu != SUB_NONE) {
        std::vector<MenuItem> sub = SubMenuItems(g_submenu);
        int sy = y + 2 + g_menu_sel * MENU_ITEM_H;
        RenderList(x + parent_w, sy, sub, g_submenu_sel);
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
// Navegacion del menu
// ---------------------------------------------------------------------------
static void MoveMenuSelection(int delta) {
    bool in_sub = (g_submenu != SUB_NONE);
    std::vector<MenuItem> items = in_sub ? SubMenuItems(g_submenu)
                                         : MenuItems(g_menu_open);
    if (items.empty())
        return;

    int &sel = in_sub ? g_submenu_sel : g_menu_sel;
    int count = (int)items.size();
    for (int guard = 0; guard < count; ++guard) {
        sel = (sel + delta + count) % count;
        if (!items[(size_t)sel].separator)
            break;
    }
}

static void ActivateMenuSelection() {
    if (g_submenu != SUB_NONE) {
        std::vector<MenuItem> sub = SubMenuItems(g_submenu);
        if (g_submenu_sel >= 0 && g_submenu_sel < (int)sub.size() &&
            !sub[(size_t)g_submenu_sel].separator)
            ExecuteCommand(sub[(size_t)g_submenu_sel].command);
        return;
    }

    std::vector<MenuItem> items = MenuItems(g_menu_open);
    if (g_menu_sel < 0 || g_menu_sel >= (int)items.size())
        return;
    const MenuItem &it = items[(size_t)g_menu_sel];
    if (it.separator)
        return;
    if (it.has_submenu) {
        g_submenu     = it.command;
        g_submenu_sel = 0;
        return;
    }
    ExecuteCommand(it.command);
}

static void HandleKeyDown(const SDL_KeyboardEvent &k) {
    bool ctrl = (k.keysym.mod & KMOD_CTRL) != 0;

    // Captura de tecla para el remapeo de un boton.
    if (g_capture_button >= 0) {
        if (k.keysym.sym != SDLK_ESCAPE) {
            SDL_Scancode sc = k.keysym.scancode;
            // No se admite robar las teclas de la propia interfaz.
            if (sc != SDL_SCANCODE_ESCAPE) {
                g_cfg.bind[g_capture_button] = sc;
                SetStatus(std::string("Boton ") + WsButtonName(g_capture_button) +
                          " = " + KeyDisplayName(sc));
                SaveConfig();
            }
        } else {
            SetStatus("Remapeo cancelado");
        }
        g_capture_button = -1;
        return;
    }

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

    // F1 (configurable) alterna entre horizontal y vertical, como girar la
    // consola. El nucleo remapea la cruceta el solo si esta en automatico.
    if (k.keysym.scancode == g_cfg.key_rotate && g_menu_open < 0) {
        ToggleOrientation();
        return;
    }

    if (g_menu_open >= 0) {
        if (k.keysym.sym == SDLK_ESCAPE) {
            if (g_submenu != SUB_NONE) {
                g_submenu     = SUB_NONE;
                g_submenu_sel = 0;
            } else {
                g_menu_open = -1;
            }
        } else if (k.keysym.sym == SDLK_DOWN) {
            MoveMenuSelection(1);
        } else if (k.keysym.sym == SDLK_UP) {
            MoveMenuSelection(-1);
        } else if (k.keysym.sym == SDLK_RIGHT) {
            if (g_submenu == SUB_NONE) {
                std::vector<MenuItem> items = MenuItems(g_menu_open);
                if (g_menu_sel < (int)items.size() &&
                    items[(size_t)g_menu_sel].has_submenu) {
                    g_submenu     = items[(size_t)g_menu_sel].command;
                    g_submenu_sel = 0;
                }
            }
        } else if (k.keysym.sym == SDLK_LEFT) {
            if (g_submenu != SUB_NONE) {
                g_submenu     = SUB_NONE;
                g_submenu_sel = 0;
            } else {
                g_menu_open = (g_menu_open + MENU_COUNT - 1) % MENU_COUNT;
                g_menu_sel  = 0;
                g_submenu_sel = 0;
            }
        } else if (k.keysym.sym == SDLK_RETURN || k.keysym.sym == SDLK_KP_ENTER) {
            ActivateMenuSelection();
        }
        return;
    }

    if (ctrl && k.keysym.sym == SDLK_o) {
        ExecuteCommand(CMD_FILE_OPEN);
    } else if (k.keysym.sym == SDLK_SPACE) {
        ExecuteCommand(CMD_EMU_PAUSE);
    } else if (k.keysym.sym == SDLK_ESCAPE) {
        g_menu_open = -1;
        g_submenu   = SUB_NONE;
    }
}

// Rectangulo de una lista desplegable, para saber si el raton la toca.
static bool ListHit(int x, int y, const std::vector<MenuItem> &items,
                    int mx, int my, int *index) {
    int w = ListWidth(items);
    int h = (int)items.size() * MENU_ITEM_H + 4;
    if (mx < x || mx >= x + w || my < y || my >= y + h)
        return false;

    int idx = (my - (y + 2)) / MENU_ITEM_H;
    if (idx < 0 || idx >= (int)items.size())
        return false;
    if (items[(size_t)idx].separator)
        return false;
    if (index != nullptr)
        *index = idx;
    return true;
}

static void HandleMouseDown(const SDL_MouseButtonEvent &m) {
    if (g_browsing)
        return;

    if (m.y < MENUBAR_H) {
        int item_w = MenuBarItemWidth();
        int idx = (m.x - 6 + 4) / item_w;
        if (idx >= 0 && idx < MENU_COUNT) {
            g_menu_open   = (g_menu_open == idx) ? -1 : idx;
            g_submenu     = SUB_NONE;
            g_menu_sel    = 0;
            g_submenu_sel = 0;
            return;
        }
    }

    if (g_menu_open >= 0) {
        std::vector<MenuItem> items = MenuItems(g_menu_open);
        int x = MenuBarItemX(g_menu_open) - 4;
        int y = MENUBAR_H;
        int idx = -1;

        if (ListHit(x, y, items, m.x, m.y, &idx)) {
            g_menu_sel = idx;
            const MenuItem &it = items[(size_t)idx];
            if (it.has_submenu) {
                g_submenu     = it.command;
                g_submenu_sel = 0;
                return;
            }
            g_menu_open  = -1;
            g_submenu    = SUB_NONE;
            ExecuteCommand(it.command);
            return;
        }

        if (g_submenu != SUB_NONE) {
            std::vector<MenuItem> sub = SubMenuItems(g_submenu);
            int sx = x + ListWidth(items);
            int sy = MENUBAR_H + 2 + g_menu_sel * MENU_ITEM_H;
            if (ListHit(sx, sy, sub, m.x, m.y, &idx)) {
                ExecuteCommand(sub[(size_t)idx].command);
                return;
            }
        }

        g_menu_open = -1;
        g_submenu   = SUB_NONE;
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
// Ejecucion de comandos de menu
// ---------------------------------------------------------------------------
static void ExecuteCommand(int cmd) {
    if (cmd == CMD_FILE_OPEN) {
        g_menu_open = -1;
        g_submenu   = SUB_NONE;

        // Primero el dialogo nativo; si no esta disponible o se cancela,
        // se ofrece el explorador integrado.
        std::string picked = OpenFileDialog(g_window, BrowseStartDir());
        if (!picked.empty())
            LoadGame(picked);
        else
            OpenBuiltinBrowser();
    } else if (cmd == CMD_FILE_EXIT) {
        g_running = false;
    } else if (cmd == CMD_EMU_RESET) {
        if (g_game_loaded) g_core.retro_reset();
    } else if (cmd == CMD_EMU_PAUSE) {
        g_paused = !g_paused;
        UpdateWindowTitle();
    } else if (cmd == CMD_HELP_ABOUT) {
        g_menu_open = -1;
        g_submenu   = SUB_NONE;
        ShowAbout();
    } else if (cmd >= CMD_VIDEO_ZOOM_1 && cmd <= CMD_VIDEO_ZOOM_4) {
        g_cfg.zoom = cmd - CMD_VIDEO_ZOOM_1 + 1;
        ApplyWindowSize();
        SaveConfig();
        SetStatus("Zoom " + std::to_string(g_cfg.zoom) + "x");
    } else if (cmd == CMD_VIDEO_ROT_TOGGLE) {
        // F1: alterna horizontal y vertical, igual que girar la consola.
        ToggleOrientation();
    } else if (cmd >= CMD_VIDEO_ROT_BASE &&
               cmd < CMD_VIDEO_ROT_BASE + 3) {
        g_cfg.rotate = cmd - CMD_VIDEO_ROT_BASE;
        // El nucleo vuelve a leer las opciones al ver el aviso y cambia la
        // orientacion el solo (SET_GEOMETRY / SET_ROTATION).
        g_options_dirty = true;
        SaveConfig();
        const char *nombres[] = { "manual", "vertical", "horizontal" };
        SetStatus(std::string("Orientacion: ") + nombres[g_cfg.rotate]);
    } else if (cmd == CMD_VIDEO_60HZ) {
        g_cfg.mode_60hz = !g_cfg.mode_60hz;
        g_options_dirty = true;
        SaveConfig();
        SetStatus(g_cfg.mode_60hz ? "Modo 60 Hz activado"
                                  : "Modo 60 Hz desactivado (75 Hz)");
    } else if (cmd == CMD_SOUND_MASTER) {
        g_cfg.master = !g_cfg.master;
        g_options_dirty = true;
        ApplyMasterMute();
        SaveConfig();
        SetStatus(g_cfg.master ? "Sonido activado" : "Sonido silenciado");
    } else if (cmd >= CMD_SOUND_CH_BASE && cmd < CMD_SOUND_CH_BASE + SND_COUNT) {
        int i = cmd - CMD_SOUND_CH_BASE;
        g_cfg.sound[i] = !g_cfg.sound[i];
        g_options_dirty = true;
        SaveConfig();
        SetStatus(std::string(SoundItemName(i)) +
                  (g_cfg.sound[i] ? ": activado" : ": desactivado"));
    } else if (cmd >= CMD_INPUT_BIND_BASE && cmd < CMD_INPUT_BIND_BASE + WSB_COUNT) {
        g_capture_button = cmd - CMD_INPUT_BIND_BASE;
        SetStatus(std::string("Pulsa la tecla para ") +
                  WsButtonName(g_capture_button) + "  (Esc cancela)");
    } else if (cmd >= CMD_INPUT_KEYMAP_BASE && cmd < CMD_INPUT_KEYMAP_BASE + 3) {
        g_cfg.keymap = cmd - CMD_INPUT_KEYMAP_BASE;
        g_options_dirty = true;
        SaveConfig();
        const char *nombres[] = { "automatica", "solo horizontal", "solo vertical" };
        SetStatus(std::string("Cruceta al rotar: ") + nombres[g_cfg.keymap]);
    } else if (cmd == CMD_INPUT_RESET) {
        Config defaults;
        for (int b = 0; b < WSB_COUNT; ++b)
            g_cfg.bind[b] = defaults.bind[b];
        g_cfg.key_rotate = defaults.key_rotate;
        g_cfg.keymap     = defaults.keymap;
        g_options_dirty  = true;
        SaveConfig();
        SetStatus("Controles restaurados");
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

    // Configuracion: guardar, releer y comprobar que se conserva.
    g_cfg.zoom   = 3;
    g_cfg.rotate = ROT_PORTRAIT;
    g_cfg.keymap = KEYMAP_LANDSCAPE;
    g_cfg.sound[SND_NOISE] = false;
    g_cfg.bind[WSB_A] = SDL_SCANCODE_K;
    g_cfg.Save(g_config_path);

    Config reread;
    reread.Load(g_config_path);
    if (reread.zoom != 3 || reread.rotate != ROT_PORTRAIT ||
        reread.keymap != KEYMAP_LANDSCAPE || reread.sound[SND_NOISE] ||
        reread.bind[WSB_A] != SDL_SCANCODE_K) {
        fprintf(stderr, "SELFTEST FALLO: el INI no conserva la configuracion\n");
        StopCore();
        return 1;
    }
    printf("SELFTEST: configuracion guardada y releida de %s\n", g_config_path.c_str());

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
    g_config_path = JoinPath(g_exe_dir, "WsCampReborn.ini");

    // La configuracion se lee antes de crear la ventana: el zoom decide su
    // tamano inicial.
    g_cfg.Load(g_config_path);

    if (argc > 2 && strcmp(argv[1], "--selftest") == 0) {
        int rc = SelfTest(argv[2]);
        SDL_Quit();
        return rc;
    }

    g_window = SDL_CreateWindow(WSCAMP_VERSION_FULL,
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                224 * g_cfg.zoom, 144 * g_cfg.zoom + MENUBAR_H,
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

    // El nucleo emite a su propia frecuencia (el WonderSwan va a 75,47 Hz, no a
    // 60). Presentar una vez por fotograma emulado ata la velocidad al refresco
    // del monitor: con vsync a 60 Hz el juego correria al 79%. En su lugar se
    // cuenta el tiempo real transcurrido y se ejecutan los retro_run que
    // correspondan; la presentacion sigue el ritmo del monitor.
    double  fps         = 60.0;
    double  frame_accum = 0.0;
    Uint64  last_tick   = SDL_GetTicks64();

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
            case SDL_WINDOWEVENT:
                // Al redimensionar, maximizar o restaurar se redibuja con el
                // tamano nuevo. SDL ya reajusta el renderer; aqui solo hay que
                // no dejar el estado a medias.
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    ev.window.event == SDL_WINDOWEVENT_RESTORED ||
                    ev.window.event == SDL_WINDOWEVENT_MAXIMIZED)
                    g_window_dirty = true;
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

        bool running_emu = g_game_loaded && !g_paused && !g_browsing && g_menu_open < 0;

        if (running_emu) {
            fps = (g_av.timing.fps > 0.0) ? g_av.timing.fps : 60.0;

            Uint64 now = SDL_GetTicks64();
            frame_accum += (double)(now - last_tick) * fps / 1000.0;
            last_tick = now;

            int to_run = (int)frame_accum;
            frame_accum -= to_run;
            if (to_run > 4) {
                // Muy por detras (ventana movida, carga del sistema): se
                // descarta el retraso en lugar de encadenar emulacion.
                to_run      = 4;
                frame_accum = 0.0;
            }
            for (int i = 0; i < to_run; ++i)
                g_core.retro_run();
        } else {
            last_tick   = SDL_GetTicks64();
            frame_accum = 0.0;
        }

        int ww = 0, wh = 0;
        SDL_GetWindowSize(g_window, &ww, &wh);

        // El nucleo puede cambiar de orientacion al vuelo (juegos verticales).
        // Si la geometria base cambia, se reajusta la ventana; pero si el
        // usuario la ha maximizado o redimensionado, se respeta su tamano.
        unsigned bw = 0, bh = 0;
        BaseGameSize(bw, bh);
        if (bw != g_sized_base_w || bh != g_sized_base_h) {
            g_sized_base_w = bw;
            g_sized_base_h = bh;
            ApplyWindowSize();
        }

        // Con la ventana minimizada no hay area util: presentar con tamano
        // cero dejaria basura en el backbuffer, asi que se omite el dibujado
        // hasta que vuelva a tener tamano.
        if (ww > 0 && wh > 0) {
            BeginFrame(ww, wh);
            if (g_browsing) {
                RenderBrowser(ww, wh);
            } else {
                RenderEmulator(ww, wh);
                RenderMenuDropdown();
            }
            EndFrame();
        }

        // Con la emulacion en marcha el ritmo lo marca el vsync de la
        // presentacion; en pausa o en los menus se limita a ~60 Hz para no
        // consumir CPU girando en vacio.
        if (!running_emu) {
            Uint64 now = SDL_GetTicks64();
            Uint64 spent = now - last_tick;
            if (spent < 16)
                SDL_Delay((Uint32)(16 - spent));
            last_tick = SDL_GetTicks64();
        }
    }

    // Al salir se guarda toda la configuracion.
    SaveConfig();

    StopCore();
    if (g_texture) SDL_DestroyTexture(g_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
