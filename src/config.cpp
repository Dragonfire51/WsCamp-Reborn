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

struct ButtonInfo { const char *key; const char *name; unsigned retro_id; WsKey def; };

// Orden fijo: el indice es el valor que se guarda en el INI.
const ButtonInfo BUTTONS[WSB_COUNT] = {
    { "x_up",   "X Arriba",    RETRO_DEVICE_ID_JOYPAD_UP,    WSK_W },
    { "x_down", "X Abajo",     RETRO_DEVICE_ID_JOYPAD_DOWN,  WSK_S },
    { "x_left", "X Izquierda", RETRO_DEVICE_ID_JOYPAD_LEFT,  WSK_A },
    { "x_right","X Derecha",   RETRO_DEVICE_ID_JOYPAD_RIGHT, WSK_D },
    { "y_up",   "Y Arriba",    RETRO_DEVICE_ID_JOYPAD_L,     WSK_UP },
    { "y_down", "Y Abajo",     RETRO_DEVICE_ID_JOYPAD_L2,    WSK_DOWN },
    { "y_left", "Y Izquierda", RETRO_DEVICE_ID_JOYPAD_R,     WSK_LEFT },
    { "y_right","Y Derecha",   RETRO_DEVICE_ID_JOYPAD_R2,    WSK_RIGHT },
    { "a",      "Boton A",     RETRO_DEVICE_ID_JOYPAD_A,     WSK_X },
    { "b",      "Boton B",     RETRO_DEVICE_ID_JOYPAD_B,     WSK_Z },
    { "start",  "Start",       RETRO_DEVICE_ID_JOYPAD_START, WSK_RETURN },
    { "select", "Select",      RETRO_DEVICE_ID_JOYPAD_SELECT,WSK_BACKSPACE },
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

// Un nombre de tecla se guarda como numero o con el nombre de SDL (ej. "F1");
// keys.cpp mantiene la tabla, que es la misma que usaba SDL, de modo que los
// INI ya escritos se siguen leyendo.
WsKey ParseKey(const std::string &value) {
    return WsKeyFromName(value.c_str());
}

std::string KeyName(WsKey key) { return WsKeyName(key); }

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
    master   = true;
    rotate   = ROT_MANUAL;
    keymap   = KEYMAP_AUTO;
    mode_60hz = false;
    key_rotate = WSK_F1;
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
    mode_60hz = ParseBool(get("video.mode_60hz", "0"), false);
    key_rotate = ParseKey(get("video.rotate_key", get("video.flip_key", "F1")));
    if (key_rotate == WSK_UNKNOWN)
        key_rotate = WSK_F1;

    rotate = ROT_MANUAL;
    std::string rot = get("video.rotate", "manual");
    if (rot == "portrait")       rotate = ROT_PORTRAIT;
    else if (rot == "landscape") rotate = ROT_LANDSCAPE;

    keymap = KEYMAP_AUTO;
    std::string km = get("input.rotate_keymap", "auto");
    if (km == "landscape" || km == "disabled") keymap = KEYMAP_LANDSCAPE;
    else if (km == "portrait" || km == "enabled") keymap = KEYMAP_PORTRAIT;

    master = ParseBool(get("sound.master", "1"), true);
    for (int i = 0; i < SND_COUNT; ++i) {
        char key[64];
        snprintf(key, sizeof(key), "sound.%s", SOUNDS[i].key);
        sound[i] = ParseBool(get(key, "1"), true);
    }

    for (int i = 0; i < WSB_COUNT; ++i) {
        char key[64];
        snprintf(key, sizeof(key), "input.%s", BUTTONS[i].key);
        WsKey k = ParseKey(get(key, ""));
        if (k != WSK_UNKNOWN)
            bind[i] = k;
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
    fprintf(f, "mode_60hz = %d\n", mode_60hz ? 1 : 0);
    fprintf(f, "rotate = %s\n", RotationName(rotate));
    fprintf(f, "rotate_key = %s\n\n", KeyName(key_rotate).c_str());

    fprintf(f, "[Sound]\n");
    fprintf(f, "master = %d\n", master ? 1 : 0);
    for (int i = 0; i < SND_COUNT; ++i)
        fprintf(f, "%s = %d\n", SOUNDS[i].key, sound[i] ? 1 : 0);
    fprintf(f, "\n");

    fprintf(f, "[Input]\n");
    fprintf(f, "rotate_keymap = %s\n", KeymapName(keymap));
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

// Nombre que entiende el nucleo (wswan_rotate_keymap). El INI admite ademas
// "landscape" y "portrait", que son mas legibles.
const char *KeymapName(int m) {
    switch (m) {
    case KEYMAP_LANDSCAPE: return "disabled";
    case KEYMAP_PORTRAIT:  return "enabled";
    default:               return "auto";
    }
}

const char *SoundItemName(int i) {
    if (i < 0 || i >= SND_COUNT)
        return "?";
    return SOUNDS[i].name;
}

std::string KeyDisplayName(WsKey key) { return KeyName(key); }
