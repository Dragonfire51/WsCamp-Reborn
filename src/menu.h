// menu.h -- Identificadores de los comandos de menu e IDs de los menus.
#pragma once

// 10 ranuras de estado: Guardar 0..9 y Cargar 0..9
#define WSCAMP_STATE_SLOTS 10

enum MenuId {
    MENU_FILE = 0,
    MENU_EMU,
    MENU_STATE,
    MENU_HELP,
    MENU_COUNT
};

enum MenuCommand {
    CMD_NONE = 0,

    // Archivo
    CMD_FILE_OPEN,
    CMD_FILE_EXIT,

    // Emulacion
    CMD_EMU_RESET,
    CMD_EMU_PAUSE,

    // Ayuda
    CMD_HELP_ABOUT,

    // Estado (se suman las ranuras)
    CMD_SAVE_STATE_BASE = 1000,
    CMD_LOAD_STATE_BASE = 2000
};

// Geometria de la interfaz (en pixeles de ventana)
static const int MENUBAR_H      = 20;
static const int MENUBAR_ITEM_W = 84;
static const int MENU_ITEM_H    = 18;
static const int MENU_ITEM_W    = 190;
static const int UI_TEXT_SCALE  = 2;
