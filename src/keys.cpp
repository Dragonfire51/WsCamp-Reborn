// keys.cpp -- Tabla de nombres de teclas.
//
// Los nombres son los mismos que devolvia SDL_GetScancodeName(), de modo que
// un WsCampReborn.ini escrito por una version anterior se sigue leyendo.
#include "keys.h"

#include <cstdlib>
#include <cstring>

namespace {

struct KeyName { WsKey key; const char *name; };

const KeyName KEY_NAMES[] = {
    { WSK_A, "A" }, { WSK_B, "B" }, { WSK_C, "C" }, { WSK_D, "D" },
    { WSK_E, "E" }, { WSK_F, "F" }, { WSK_G, "G" }, { WSK_H, "H" },
    { WSK_I, "I" }, { WSK_J, "J" }, { WSK_K, "K" }, { WSK_L, "L" },
    { WSK_M, "M" }, { WSK_N, "N" }, { WSK_O, "O" }, { WSK_P, "P" },
    { WSK_Q, "Q" }, { WSK_R, "R" }, { WSK_S, "S" }, { WSK_T, "T" },
    { WSK_U, "U" }, { WSK_V, "V" }, { WSK_W, "W" }, { WSK_X, "X" },
    { WSK_Y, "Y" }, { WSK_Z, "Z" },

    { WSK_1, "1" }, { WSK_2, "2" }, { WSK_3, "3" }, { WSK_4, "4" },
    { WSK_5, "5" }, { WSK_6, "6" }, { WSK_7, "7" }, { WSK_8, "8" },
    { WSK_9, "9" }, { WSK_0, "0" },

    { WSK_RETURN, "Return" }, { WSK_ESCAPE, "Escape" },
    { WSK_BACKSPACE, "Backspace" }, { WSK_TAB, "Tab" }, { WSK_SPACE, "Space" },
    { WSK_MINUS, "-" }, { WSK_EQUALS, "=" }, { WSK_LBRACKET, "[" },
    { WSK_RBRACKET, "]" }, { WSK_BACKSLASH, "\\" }, { WSK_SEMICOLON, ";" },
    { WSK_APOSTROPHE, "'" }, { WSK_GRAVE, "`" }, { WSK_COMMA, "," },
    { WSK_PERIOD, "." }, { WSK_SLASH, "/" }, { WSK_CAPSLOCK, "CapsLock" },

    { WSK_F1, "F1" }, { WSK_F2, "F2" }, { WSK_F3, "F3" }, { WSK_F4, "F4" },
    { WSK_F5, "F5" }, { WSK_F6, "F6" }, { WSK_F7, "F7" }, { WSK_F8, "F8" },
    { WSK_F9, "F9" }, { WSK_F10, "F10" }, { WSK_F11, "F11" }, { WSK_F12, "F12" },

    { WSK_PRINTSCREEN, "PrintScreen" }, { WSK_SCROLLLOCK, "ScrollLock" },
    { WSK_PAUSE, "Pause" }, { WSK_INSERT, "Insert" }, { WSK_HOME, "Home" },
    { WSK_PAGEUP, "PageUp" }, { WSK_DELETE, "Delete" }, { WSK_END, "End" },
    { WSK_PAGEDOWN, "PageDown" }, { WSK_RIGHT, "Right" }, { WSK_LEFT, "Left" },
    { WSK_DOWN, "Down" }, { WSK_UP, "Up" }, { WSK_NUMLOCK, "NumlockClear" },

    { WSK_KP_DIVIDE, "Kp /" }, { WSK_KP_MULTIPLY, "Kp *" },
    { WSK_KP_MINUS, "Kp -" }, { WSK_KP_PLUS, "Kp +" },
    { WSK_KP_ENTER, "Kp Enter" },
    { WSK_KP_1, "Kp 1" }, { WSK_KP_2, "Kp 2" }, { WSK_KP_3, "Kp 3" },
    { WSK_KP_4, "Kp 4" }, { WSK_KP_5, "Kp 5" }, { WSK_KP_6, "Kp 6" },
    { WSK_KP_7, "Kp 7" }, { WSK_KP_8, "Kp 8" }, { WSK_KP_9, "Kp 9" },
    { WSK_KP_0, "Kp 0" }, { WSK_KP_PERIOD, "Kp ." },

    { WSK_LCTRL, "Left Ctrl" }, { WSK_LSHIFT, "Left Shift" },
    { WSK_LALT, "Left Alt" }, { WSK_LGUI, "Left GUI" },
    { WSK_RCTRL, "Right Ctrl" }, { WSK_RSHIFT, "Right Shift" },
    { WSK_RALT, "Right Alt" }, { WSK_RGUI, "Right GUI" },
};

// Algunos alias que la gente escribe a mano en el INI.
const KeyName KEY_ALIASES[] = {
    { WSK_RETURN, "Enter" }, { WSK_ESCAPE, "Esc" }, { WSK_SPACE, " " },
    { WSK_UP, "Arriba" }, { WSK_DOWN, "Abajo" },
    { WSK_LEFT, "Izquierda" }, { WSK_RIGHT, "Derecha" },
    { WSK_KP_ENTER, "KP_Enter" }, { WSK_KP_PLUS, "KP_Plus" },
    { WSK_KP_MINUS, "KP_Minus" },
};

bool EqualsIgnoreCase(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
        if (ca != cb)
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

} // namespace

const char *WsKeyName(WsKey key) {
    for (const KeyName &k : KEY_NAMES)
        if (k.key == key)
            return k.name;
    return "Desconocida";
}

WsKey WsKeyFromName(const char *name) {
    if (name == nullptr || name[0] == '\0')
        return WSK_UNKNOWN;

    // Un valor puramente numerico se acepta tal cual, para editar a mano.
    bool numeric = true;
    for (const char *p = name; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            numeric = false;
            break;
        }
    }
    if (numeric)
        return (WsKey)atoi(name);

    for (const KeyName &k : KEY_NAMES)
        if (EqualsIgnoreCase(k.name, name))
            return k.key;
    for (const KeyName &k : KEY_ALIASES)
        if (EqualsIgnoreCase(k.name, name))
            return k.key;

    // Una sola letra o digito en minuscula ya la cubre la tabla, pero se
    // admite cualquier grafia suelta por comodidad.
    if (name[1] == '\0') {
        char c = name[0];
        if (c >= 'a' && c <= 'z')
            return (WsKey)(WSK_A + (c - 'a'));
        if (c >= 'A' && c <= 'Z')
            return (WsKey)(WSK_A + (c - 'A'));
    }
    return WSK_UNKNOWN;
}
