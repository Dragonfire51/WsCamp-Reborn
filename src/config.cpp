// config.cpp -- Lectura y escritura de WsCampReborn.ini.
#include "config.h"
#include "ws_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <map>

Config      g_cfg;
std::string g_config_path;
bool        g_options_dirty = false;

namespace {

struct ButtonInfo { const char *key; const char *name; unsigned retro_id; SDL_Scancode def; };

// Orden fijo: el indice es el valor que se guarda en el INI.
const ButtonInfo BUTTONS[WSB_COUNT] = {
    { "x_up",   "X Arriba",    RETRO_DEVICE_ID_JOYPAD_UP,    SDL_SCANCODE_W },
    { "x_down", "X Abajo",     RETRO_DEVICE_ID_JOYPAD_DOWN,  SDL_SCANCODE_S },
    { "x_left", "X Izquierda", RETRO_DEVICE_ID_JOYPAD_LEFT,  SDL_SCANCODE_A },
    { "x_right","X Derecha",   RETRO_DEVICE_ID_JOYPAD_RIGHT, SDL_SCANCODE_D },
    { "y_up",   "Y Arriba",    RETRO_DEVICE_ID_JOYPAD_L,     SDL_SCANCODE_UP },
    { "y_down", "Y Abajo",     RETRO_DEVICE_ID_JOYPAD_L2,    SDL_SCANCODE_DOWN },
    { "y_left", "Y Izquierda", RETRO_DEVICE_ID_JOYPAD_R,     SDL_SCANCODE_LEFT },
    { "y_right","Y Derecha",   RETRO_DEVICE_ID_JOYPAD_R2,    SDL_SCANCODE_RIGHT },
    { "a",      "Boton A",     RETRO_DEVICE_ID_JOYPAD_A,     SDL_SCANCODE_X },
    { "b",      "Boton B",     RETRO_DEVICE_ID_JOYPAD_B,     SDL_SCANCODE_Z },
    { "start",  "Start",       RETRO_DEVICE_ID_JOYPAD_START, SDL_SCANCODE_RETURN },
    { "select", "Select",      RETRO_DEVICE_ID_JOYPAD_SELECT,SDL_SCANCODE_BACKSPACE },
};

struct SoundInfo { const char *key; const char *name; };

const SoundInfo SOUNDS[SND_COUNT] = {
    { "ch1",   "Canal 1" },
    { "ch2",   "Canal 2" },
    { "ch3",   "Canal 3" },
    { "ch4",   "Canal 4" },
    { "noise", "Noise"   },
    { "pcm",   "PCM"     },
    { "dma",   "DMA"     },
};

std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Un nombre de tecla se guarda como numero; se aceptan los nombres de SDL
// (ej. SDLK_F1) y un valor numerico directo, para poder editar el INI a mano.
SDL_Scancode ParseKey(const std::string &value) {
    if (value.empty())
        return SDL_SCANCODE_UNKNOWN;
    if (value.find_first_not_of("0123456789") == std::string::npos)
        return (SDL_Scancode)atoi(value.c_str());
    SDL_Scancode sc = SDL_GetScancodeFromName(value.c_str());
    return sc;
}

std::string KeyName(SDL_Scancode sc) {
    const char *name = SDL_GetScancodeName(sc);
    return (name != nullptr && name[0] != '\0') ? name : "Desconocida";
}

bool ParseBool(const std::string &v, bool def) {
    if (v == "1" || v == "true" || v == "si" || v == "yes")
        return true;
    if (v == "0" || v == "false" || v == "no")
        return false;
    return def;
}

} // namespace

Config::Config() { Reset(); }

void Config::Reset() {
    zoom     = 2;
    flip     = false;
    master   = true;
    rotate   = ROT_MANUAL;
    mode_60hz = false;
    key_flip = SDL_SCANCODE_F1;
    for (int i = 0; i < SND_COUNT; ++i)
        sound[i] = true;
    for (int i = 0; i < WSB_COUNT; ++i)
        bind[i] = BUTTONS[i].def;
}

void Config::Load(const std::string &path) {
    Reset();

    FILE *f = fopen(path.c_str(), "rb");
    if (f == nullptr)
        return;   // sin fichero: se usan los valores por defecto

    std::map<std::string, std::string> kv;
    std::string section;
    char line[512];
    while (fgets(line, sizeof(line), f) != nullptr) {
        std::string s = Trim(line);
        if (s.empty() || s[0] == ';' || s[0] == '#')
            continue;
        if (s[0] == '[') {
            size_t end = s.find(']');
            section = (end == std::string::npos) ? s.substr(1) : s.substr(1, end - 1);
            section = Trim(section);
            for (char &c : section)
                c = (char)tolower((unsigned char)c);
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos)
            continue;
        // Las claves se leen como "seccion.clave", igual que se escriben.
        std::string key = Trim(s.substr(0, eq));
        if (!section.empty())
            key = section + "." + key;
        kv[key] = Trim(s.substr(eq + 1));
    }
    fclose(f);

    auto get = [&](const char *k, const std::string &def) {
        auto it = kv.find(k);
        return (it == kv.end()) ? def : it->second;
    };

    zoom   = atoi(get("video.zoom", "2").c_str());
    if (zoom < 1) zoom = 1;
    if (zoom > 4) zoom = 4;
    flip     = ParseBool(get("video.flip", "0"), false);
    mode_60hz = ParseBool(get("video.mode_60hz", "0"), false);
    key_flip = ParseKey(get("video.flip_key", "F1"));

    rotate = ROT_MANUAL;
    std::string rot = get("video.rotate", "manual");
    if (rot == "portrait")       rotate = ROT_PORTRAIT;
    else if (rot == "landscape") rotate = ROT_LANDSCAPE;

    master = ParseBool(get("sound.master", "1"), true);
    for (int i = 0; i < SND_COUNT; ++i) {
        char key[64];
        snprintf(key, sizeof(key), "sound.%s", SOUNDS[i].key);
        sound[i] = ParseBool(get(key, "1"), true);
    }

    for (int i = 0; i < WSB_COUNT; ++i) {
        char key[64];
        snprintf(key, sizeof(key), "input.%s", BUTTONS[i].key);
        SDL_Scancode sc = ParseKey(get(key, ""));
        if (sc != SDL_SCANCODE_UNKNOWN)
            bind[i] = sc;
    }
}

void Config::Save(const std::string &path) const {
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr)
        return;

    fprintf(f, "; WsCampReborn.ini -- se reescribe al salir y tras cada cambio\n");
    fprintf(f, "; de configuracion hecho desde el menu.\n\n");

    fprintf(f, "[Video]\n");
    fprintf(f, "zoom = %d\n", zoom);
    fprintf(f, "flip = %d\n", flip ? 1 : 0);
    fprintf(f, "mode_60hz = %d\n", mode_60hz ? 1 : 0);
    fprintf(f, "rotate = %s\n", RotationName(rotate));
    fprintf(f, "flip_key = %s\n\n", SDL_GetScancodeName(key_flip));

    fprintf(f, "[Sound]\n");
    fprintf(f, "master = %d\n", master ? 1 : 0);
    for (int i = 0; i < SND_COUNT; ++i)
        fprintf(f, "%s = %d\n", SOUNDS[i].key, sound[i] ? 1 : 0);
    fprintf(f, "\n");

    fprintf(f, "[Input]\n");
    for (int i = 0; i < WSB_COUNT; ++i)
        fprintf(f, "%s = %s\n", BUTTONS[i].key, KeyName(bind[i]).c_str());

    fclose(f);
}

const char *WsButtonName(int b) {
    if (b < 0 || b >= WSB_COUNT)
        return "?";
    return BUTTONS[b].name;
}

unsigned WsButtonRetroId(int b) {
    if (b < 0 || b >= WSB_COUNT)
        return 0xFFFFFFFFu;
    return BUTTONS[b].retro_id;
}

const char *RotationName(int r) {
    switch (r) {
    case ROT_PORTRAIT:  return "portrait";
    case ROT_LANDSCAPE: return "landscape";
    default:            return "manual";
    }
}

const char *SoundItemName(int i) {
    if (i < 0 || i >= SND_COUNT)
        return "?";
    return SOUNDS[i].name;
}

std::string KeyDisplayName(SDL_Scancode sc) { return KeyName(sc); }
