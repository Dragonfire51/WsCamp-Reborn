// menu_model.h -- Descripcion del menu, compartida por los dos backends.
//
// El menu se describe una sola vez aqui. El backend de Windows construye con
// esta lista la barra de menu nativa del sistema; el de SDL dibuja la barra y
// los desplegables a mano. Asi no hay dos definiciones del menu que se puedan
// desincronizar.
#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "menu.h"

struct MenuEntry {
    std::string label;
    int         command   = CMD_NONE;
    bool        separator = false;
    bool        checked   = false;
    // Si es distinto de SUB_NONE, la entrada abre ese submenu y command no se
    // usa.
    int         submenu   = SUB_NONE;
};

// Todo lo que cambia la marca de verificacion o el texto de una entrada. Se
// pasa desde el frontend para que el modelo no dependa de sus globales.
struct MenuState {
    int  zoom      = 2;
    bool paused    = false;
    bool mode_60hz = false;
    int  rotate    = ROT_MANUAL;
    int  keymap    = KEYMAP_AUTO;
    bool master    = true;
    bool sound[SND_COUNT];
    // "Nombre del boton: tecla", ya formateado para cada boton mapeable.
    std::vector<std::string> bind_labels;

    MenuState();
};

// Entradas de un menu de primer nivel (Archivo, Emulacion...).
std::vector<MenuEntry> BuildTopMenu(int menu, const MenuState &st);

// Entradas de un submenu de Opciones (video, sonido, controles).
std::vector<MenuEntry> BuildSubMenu(int submenu, const MenuState &st);

// Titulo de la barra para un menu de primer nivel.
const char *MenuTitle(int menu);

// Rellena st.bind_labels a partir de la configuracion actual.
void FillBindLabels(MenuState &st);
