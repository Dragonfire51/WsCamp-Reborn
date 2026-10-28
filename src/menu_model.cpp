// menu_model.cpp -- Construye la lista de entradas del menu.
#include "menu_model.h"

#include <cstdio>

#include "version.h"

namespace {

MenuEntry Item(const std::string &label, int command) {
    MenuEntry e;
    e.label   = label;
    e.command = command;
    return e;
}

MenuEntry Checked(const std::string &label, int command, bool on) {
    MenuEntry e = Item(label, command);
    e.checked   = on;
    return e;
}

MenuEntry Separator() {
    MenuEntry e;
    e.separator = true;
    return e;
}

MenuEntry SubmenuItem(const std::string &label, int submenu) {
    MenuEntry e = Item(label, submenu);
    e.submenu   = submenu;
    return e;
}

} // namespace

MenuState::MenuState() {
    for (int i = 0; i < SND_COUNT; ++i)
        sound[i] = true;
}

const char *MenuTitle(int menu) {
    static const char *titles[MENU_COUNT] = {
        "Archivo", "Emulacion", "Estado", "Opciones", "Ayuda"
    };
    return (menu >= 0 && menu < MENU_COUNT) ? titles[menu] : "";
}

void FillBindLabels(MenuState &st) {
    st.bind_labels.clear();
    for (int b = 0; b < WSB_COUNT; ++b)
        st.bind_labels.push_back(std::string(WsButtonName(b)) + ": " +
                                 KeyDisplayName(g_cfg.bind[b]));
}

std::vector<MenuEntry> BuildTopMenu(int menu, const MenuState &st) {
    std::vector<MenuEntry> items;
    switch (menu) {
    case MENU_FILE:
        items.push_back(Item("Abrir ROM...  (Ctrl+O)", CMD_FILE_OPEN));
        items.push_back(Separator());
        items.push_back(Item("Salir", CMD_FILE_EXIT));
        break;
    case MENU_EMU:
        items.push_back(Item("Reiniciar", CMD_EMU_RESET));
        items.push_back(Checked("Pausa  (Espacio)", CMD_EMU_PAUSE, st.paused));
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

std::vector<MenuEntry> BuildSubMenu(int submenu, const MenuState &st) {
    std::vector<MenuEntry> items;
    switch (submenu) {
    case SUB_VIDEO:
        items.push_back(Checked("Zoom 1x", CMD_VIDEO_ZOOM_1, st.zoom == 1));
        items.push_back(Checked("Zoom 2x", CMD_VIDEO_ZOOM_2, st.zoom == 2));
        items.push_back(Checked("Zoom 3x", CMD_VIDEO_ZOOM_3, st.zoom == 3));
        items.push_back(Checked("Zoom 4x", CMD_VIDEO_ZOOM_4, st.zoom == 4));
        items.push_back(Separator());
        items.push_back(Item("Alternar horizontal / vertical  (F1)",
                             CMD_VIDEO_ROT_TOGGLE));
        items.push_back(Checked("Orientacion: automatica (Select)",
                                CMD_VIDEO_ROT_BASE + ROT_MANUAL,
                                st.rotate == ROT_MANUAL));
        items.push_back(Checked("Orientacion: vertical",
                                CMD_VIDEO_ROT_BASE + ROT_PORTRAIT,
                                st.rotate == ROT_PORTRAIT));
        items.push_back(Checked("Orientacion: horizontal",
                                CMD_VIDEO_ROT_BASE + ROT_LANDSCAPE,
                                st.rotate == ROT_LANDSCAPE));
        items.push_back(Separator());
        items.push_back(Checked("Modo 60 Hz", CMD_VIDEO_60HZ, st.mode_60hz));
        break;
    case SUB_SOUND:
        items.push_back(Checked("Silencio general", CMD_SOUND_MASTER, !st.master));
        items.push_back(Separator());
        for (int i = 0; i < SND_COUNT; ++i)
            items.push_back(Checked(SoundItemName(i), CMD_SOUND_CH_BASE + i,
                                    st.sound[i]));
        break;
    case SUB_INPUT: {
        for (int b = 0; b < WSB_COUNT; ++b) {
            const std::string label = ((size_t)b < st.bind_labels.size())
                ? st.bind_labels[(size_t)b]
                : std::string(WsButtonName(b));
            items.push_back(Item(label, CMD_INPUT_BIND_BASE + b));
        }
        items.push_back(Separator());
        items.push_back(Checked("Cruceta al rotar: automatica",
                                CMD_INPUT_KEYMAP_BASE + KEYMAP_AUTO,
                                st.keymap == KEYMAP_AUTO));
        items.push_back(Checked("Cruceta al rotar: solo horizontal",
                                CMD_INPUT_KEYMAP_BASE + KEYMAP_LANDSCAPE,
                                st.keymap == KEYMAP_LANDSCAPE));
        items.push_back(Checked("Cruceta al rotar: solo vertical",
                                CMD_INPUT_KEYMAP_BASE + KEYMAP_PORTRAIT,
                                st.keymap == KEYMAP_PORTRAIT));
        items.push_back(Separator());
        items.push_back(Item("Restaurar valores por defecto", CMD_INPUT_RESET));
        break;
    }
    default: break;
    }
    return items;
}
