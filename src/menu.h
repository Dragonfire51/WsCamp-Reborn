// menu.h -- Identificadores de los comandos de menu e IDs de los menus.
#pragma once

// 10 ranuras de estado: Guardar 0..9 y Cargar 0..9
#define WSCAMP_STATE_SLOTS 10

enum MenuId {
    MENU_FILE = 0,
    MENU_EMU,
    MENU_STATE,
    MENU_OPTIONS,
    MENU_HELP,
    MENU_COUNT
};

// Submenus de "Opciones".
enum MenuSubmenu {
    SUB_NONE = 0,
    SUB_VIDEO,
    SUB_SOUND,
    SUB_INPUT
};

enum MenuCommand {
    CMD_NONE = 0,

    // Archivo
    CMD_FILE_OPEN,
    CMD_FILE_EXIT,

    // Emulacion
    CMD_EMU_RESET,
    CMD_EMU_PAUSE,

    // Opciones -> Video (zoom y volteo)
    CMD_VIDEO_ZOOM_1 = 100,
    CMD_VIDEO_ZOOM_2,
    CMD_VIDEO_ZOOM_3,
    CMD_VIDEO_ZOOM_4,
    CMD_VIDEO_FLIP,

    // Opciones -> Sonido (silencio y canales)
    CMD_SOUND_MASTER = 200,
    CMD_SOUND_CH_BASE,

    // Opciones -> Controles (remapeo de botones)
    CMD_INPUT_BIND_BASE = 300,
    CMD_INPUT_RESET = 400,

    // Ayuda
    CMD_HELP_ABOUT = 500,

    // Estado (se suman las ranuras)
    CMD_SAVE_STATE_BASE = 1000,
    CMD_LOAD_STATE_BASE = 2000
};

// Geometria de la interfaz (en pixeles de ventana)
// La fuente integrada mide 5x7 con paso 6, asi que la escala 2 da texto de
// 12x14: cabe en la barra y no se solapa entre entradas.
static const int MENUBAR_H     = 22;
static const int MENU_ITEM_H   = 20;
static const int MENU_ITEM_W   = 200;
static const int UI_TEXT_SCALE = 2;
