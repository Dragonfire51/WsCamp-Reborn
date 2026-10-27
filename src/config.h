// config.h -- Configuracion persistente de WsCampReborn (WsCampReborn.ini).
//
// Todo lo que el usuario puede cambiar desde el menu (zoom, volteo, canales de
// sonido y mapeo de botones) vive en esta estructura. Se escribe a disco al
// salir y despues de cada cambio hecho desde el menu.
#pragma once

#include <SDL.h>

#include <string>

// Botones mapeables. El indice es estable porque se guarda en el INI.
enum WsButton {
    WSB_X_UP = 0,
    WSB_X_DOWN,
    WSB_X_LEFT,
    WSB_X_RIGHT,
    WSB_Y_UP,
    WSB_Y_DOWN,
    WSB_Y_LEFT,
    WSB_Y_RIGHT,
    WSB_A,
    WSB_B,
    WSB_START,
    WSB_SELECT,
    WSB_COUNT
};

// Canales de sonido del WonderSwan.
enum SoundItem {
    SND_CH1 = 0,
    SND_CH2,
    SND_CH3,
    SND_CH4,
    SND_NOISE,
    SND_PCM,
    SND_DMA,
    SND_COUNT
};

// Orientacion que se le pide al nucleo (opcion wswan_rotate_display). Los
// juegos verticales, como Wonder Classic o Digimon, se disenan para una
// pantalla de 144x224 y necesitan ROT_PORTRAIT para verse en su sitio.
enum DisplayRotation {
    ROT_MANUAL = 0,     // se gira al pulsar Select (por defecto del nucleo)
    ROT_PORTRAIT,       // juegos verticales
    ROT_LANDSCAPE       // juegos horizontales
};

struct Config {
    int          zoom     = 2;      // 1..4
    bool         flip     = false;  // F1: voltea la ventana entera 180 grados
    bool         master   = true;   // silencio general
    int          rotate   = ROT_MANUAL;
    bool         mode_60hz = false; // el nucleo baja de 75 a 60 Hz
    bool         sound[SND_COUNT];
    SDL_Scancode bind[WSB_COUNT];
    SDL_Scancode key_flip = SDL_SCANCODE_F1;

    Config();

    void Reset();
    void Load(const std::string &path);
    void Save(const std::string &path) const;
};

extern Config      g_cfg;
extern std::string g_config_path;

// Se pone a true cuando cambia una opcion que el nucleo debe releer
// (por ejemplo un canal de sonido).
extern bool g_options_dirty;

const char *WsButtonName(int b);
unsigned    WsButtonRetroId(int b);
const char *SoundItemName(int i);
const char *RotationName(int r);
std::string KeyDisplayName(SDL_Scancode sc);
