// keys.h -- Codigos de tecla neutrales, compartidos por los dos backends.
//
// Los valores coinciden a proposito con los SDL_Scancode de SDL2 (codigos de
// uso USB HID). Asi los WsCampReborn.ini ya escritos, que guardan los nombres
// de SDL ("W", "Up", "Return"), se siguen leyendo igual aunque el backend de
// Windows ya no use SDL.
#pragma once

enum WsKey {
    WSK_UNKNOWN = 0,

    WSK_A = 4, WSK_B, WSK_C, WSK_D, WSK_E, WSK_F, WSK_G, WSK_H, WSK_I, WSK_J,
    WSK_K, WSK_L, WSK_M, WSK_N, WSK_O, WSK_P, WSK_Q, WSK_R, WSK_S, WSK_T,
    WSK_U, WSK_V, WSK_W, WSK_X, WSK_Y, WSK_Z,

    WSK_1 = 30, WSK_2, WSK_3, WSK_4, WSK_5, WSK_6, WSK_7, WSK_8, WSK_9, WSK_0,

    WSK_RETURN    = 40,
    WSK_ESCAPE    = 41,
    WSK_BACKSPACE = 42,
    WSK_TAB       = 43,
    WSK_SPACE     = 44,
    WSK_MINUS     = 45,
    WSK_EQUALS    = 46,
    WSK_LBRACKET  = 47,
    WSK_RBRACKET  = 48,
    WSK_BACKSLASH = 49,
    WSK_SEMICOLON = 51,
    WSK_APOSTROPHE= 52,
    WSK_GRAVE     = 53,
    WSK_COMMA     = 54,
    WSK_PERIOD    = 55,
    WSK_SLASH     = 56,
    WSK_CAPSLOCK  = 57,

    WSK_F1 = 58, WSK_F2, WSK_F3, WSK_F4, WSK_F5, WSK_F6, WSK_F7, WSK_F8,
    WSK_F9, WSK_F10, WSK_F11, WSK_F12,

    WSK_PRINTSCREEN = 70,
    WSK_SCROLLLOCK  = 71,
    WSK_PAUSE       = 72,
    WSK_INSERT      = 73,
    WSK_HOME        = 74,
    WSK_PAGEUP      = 75,
    WSK_DELETE      = 76,
    WSK_END         = 77,
    WSK_PAGEDOWN    = 78,
    WSK_RIGHT       = 79,
    WSK_LEFT        = 80,
    WSK_DOWN        = 81,
    WSK_UP          = 82,
    WSK_NUMLOCK     = 83,
    WSK_KP_DIVIDE   = 84,
    WSK_KP_MULTIPLY = 85,
    WSK_KP_MINUS    = 86,
    WSK_KP_PLUS     = 87,
    WSK_KP_ENTER    = 88,
    WSK_KP_1 = 89, WSK_KP_2, WSK_KP_3, WSK_KP_4, WSK_KP_5,
    WSK_KP_6, WSK_KP_7, WSK_KP_8, WSK_KP_9, WSK_KP_0,
    WSK_KP_PERIOD   = 99,

    WSK_LCTRL  = 224,
    WSK_LSHIFT = 225,
    WSK_LALT   = 226,
    WSK_LGUI   = 227,
    WSK_RCTRL  = 228,
    WSK_RSHIFT = 229,
    WSK_RALT   = 230,
    WSK_RGUI   = 231
};

// Nombre legible de una tecla, en la nomenclatura de SDL ("W", "Up", "F1",
// "Return"...). Devuelve "Desconocida" si no esta en la tabla.
const char *WsKeyName(WsKey key);

// Nombre a tecla. Se aceptan los nombres de SDL y un numero directo, para
// poder editar el INI a mano. Devuelve WSK_UNKNOWN si no se reconoce.
WsKey WsKeyFromName(const char *name);
