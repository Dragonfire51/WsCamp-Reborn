// platform_win32.cpp -- Capa de plataforma nativa de Windows (Win32 + GDI).
//
// Windows no usa SDL: la ventana, el menu, el texto y la presentacion del
// juego son nativos. El motivo es el texto: GDI dibuja con una fuente del
// sistema que se adapta al tamano de la ventana, asi que al maximizar no se
// corta ni queda diminuto, que era el problema con la fuente de mapa de bits
// escalada a mano.
//
// El menu tambien es el nativo del sistema (barra y desplegables), con lo que
// se comporta como cualquier programa de Windows.
#include "platform.h"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "menu.h"
#include "menu_model.h"

// El frontend implementa estos dos puntos de entrada que el backend necesita.
// ExecuteCommand(int) aplica un comando de menu; MenuStateForUi() describe el
// estado para marcar las entradas.
void ExecuteCommand(int cmd);
MenuState MenuStateForUi();

namespace {

const char *kWindowClass = "WsCampRebornWnd";

HWND    g_hwnd        = nullptr;
HMENU   g_menubar     = nullptr;
bool    g_running     = true;
bool    g_need_menu   = false;   // el menu se reconstruye antes de mostrarse

// Dibujo en memoria (doble buffer).
HDC     g_mem_dc      = nullptr;
HBITMAP g_mem_bmp     = nullptr;
uint32_t *g_bits      = nullptr;
int     g_buf_w = 0, g_buf_h = 0;

HFONT   g_font        = nullptr;
int     g_font_scale  = 0;

// Cola de eventos: los mensajes de Windows se traducen aqui y se consumen
// desde WsPollEvent.
std::vector<WsEvent> g_events;

// Estado del teclado: se guarda por codigo de tecla neutral.
bool g_key_state[256] = { false };

// Audio (waveOut).
HWAVEOUT g_wave      = nullptr;
bool     g_audio_ok  = false;
bool     g_audio_paused = false;
int      g_wave_rate = 44100;

struct WaveBlock {
    WAVEHDR hdr;
    std::vector<int16_t> data;
};
std::vector<WaveBlock *> g_wave_blocks;
CRITICAL_SECTION g_audio_lock;

// --- Traduccion de teclas ---------------------------------------------------
// Los codigos neutrales son los de SDL (uso USB HID); Windows da scancodes y
// codigos virtuales, asi que se traduce por tabla.
WsKey FromVirtualKey(WPARAM vk, LPARAM lp) {
    const UINT scancode = (UINT)((lp >> 16) & 0xFF);
    const bool extended = (lp & (1 << 24)) != 0;

    // Teclas con scancode propio y sin ambiguedad.
    switch (scancode) {
    case 0x01: return WSK_ESCAPE;
    case 0x02: return WSK_1;  case 0x03: return WSK_2;  case 0x04: return WSK_3;
    case 0x05: return WSK_4;  case 0x06: return WSK_5;  case 0x07: return WSK_6;
    case 0x08: return WSK_7;  case 0x09: return WSK_8;  case 0x0A: return WSK_9;
    case 0x0B: return WSK_0;
    case 0x0E: return WSK_BACKSPACE;
    case 0x0F: return WSK_TAB;
    case 0x1C: return extended ? WSK_KP_ENTER : WSK_RETURN;
    case 0x39: return WSK_SPACE;
    case 0x0C: return WSK_MINUS;    case 0x0D: return WSK_EQUALS;
    case 0x1A: return WSK_LBRACKET; case 0x1B: return WSK_RBRACKET;
    case 0x2B: return WSK_BACKSLASH;
    case 0x27: return WSK_SEMICOLON; case 0x28: return WSK_APOSTROPHE;
    case 0x29: return WSK_GRAVE;
    case 0x33: return WSK_COMMA;    case 0x34: return WSK_PERIOD;
    case 0x35: return WSK_SLASH;    case 0x3A: return WSK_CAPSLOCK;
    case 0x3B: return WSK_F1;  case 0x3C: return WSK_F2;
    case 0x3D: return WSK_F3;  case 0x3E: return WSK_F4;
    case 0x3F: return WSK_F5;  case 0x40: return WSK_F6;
    case 0x41: return WSK_F7;  case 0x42: return WSK_F8;
    case 0x43: return WSK_F9;  case 0x44: return WSK_F10;
    case 0x57: return WSK_F11; case 0x58: return WSK_F12;
    case 0x45: return WSK_NUMLOCK; case 0x46: return WSK_SCROLLLOCK;
    case 0x37: return extended ? WSK_PRINTSCREEN : WSK_KP_MULTIPLY;
    case 0x4A: return WSK_KP_MINUS;  case 0x4E: return WSK_KP_PLUS;
    case 0x53: return WSK_KP_PERIOD;
    case 0x47: return extended ? WSK_HOME : WSK_KP_7;
    case 0x48: return extended ? WSK_UP : WSK_KP_8;
    case 0x49: return extended ? WSK_PAGEUP : WSK_KP_9;
    case 0x4B: return extended ? WSK_LEFT : WSK_KP_4;
    case 0x4D: return extended ? WSK_RIGHT : WSK_KP_6;
    case 0x4F: return extended ? WSK_END : WSK_KP_1;
    case 0x50: return extended ? WSK_DOWN : WSK_KP_2;
    case 0x51: return extended ? WSK_PAGEDOWN : WSK_KP_3;
    case 0x52: return extended ? WSK_INSERT : WSK_KP_0;
    default: break;
    }

    if (vk >= 'A' && vk <= 'Z')
        return (WsKey)(WSK_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9')
        return (WsKey)(WSK_1 + (vk - '1'));

    switch (vk) {
    case VK_LEFT:  return WSK_LEFT;
    case VK_RIGHT: return WSK_RIGHT;
    case VK_UP:    return WSK_UP;
    case VK_DOWN:  return WSK_DOWN;
    case VK_DELETE: return WSK_DELETE;
    case VK_INSERT: return WSK_INSERT;
    case VK_HOME:   return WSK_HOME;
    case VK_END:    return WSK_END;
    case VK_PRIOR:  return WSK_PAGEUP;
    case VK_NEXT:   return WSK_PAGEDOWN;
    case VK_PAUSE:  return WSK_PAUSE;
    case VK_DIVIDE: return WSK_KP_DIVIDE;
    case VK_DECIMAL: return WSK_KP_PERIOD;
    case VK_CONTROL: return extended ? WSK_RCTRL : WSK_LCTRL;
    case VK_SHIFT:
        return (GetKeyState(VK_RSHIFT) & 0x8000) ? WSK_RSHIFT : WSK_LSHIFT;
    case VK_MENU:    return extended ? WSK_RALT : WSK_LALT;
    default: break;
    }
    return WSK_UNKNOWN;
}

// --- Fuente -----------------------------------------------------------------
// Una fuente monoespaciada del sistema, del tamano que pide la escala. Se
// crea una sola vez por escala y se reutiliza.
HFONT FontForScale(int scale) {
    if (g_font != nullptr && g_font_scale == scale)
        return g_font;
    if (g_font != nullptr)
        DeleteObject(g_font);

    // La fuente integrada mide 7x6; GDI necesita el alto de celda, que incluye
    // el interlineado, de ahi el factor.
    const int height = -(7 * scale * 16 / 10);
    g_font = CreateFontA(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    if (g_font == nullptr)
        g_font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    g_font_scale = scale;
    return g_font;
}

// --- Menu -------------------------------------------------------------------
// Cada entrada guarda su comando en el id del item, desplazado para dejar
// sitio a los ids que Windows reserva (0 y los de sistema).
const int kMenuIdBase = 1000;

int MenuIdForCommand(int cmd) { return kMenuIdBase + cmd; }
int CommandForMenuId(int id) { return id - kMenuIdBase; }

HMENU BuildSubMenuNative(int submenu, const MenuState &st) {
    HMENU h = CreatePopupMenu();
    std::vector<MenuEntry> items = BuildSubMenu(submenu, st);
    for (const MenuEntry &e : items) {
        if (e.separator) {
            AppendMenuA(h, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        UINT flags = MF_STRING;
        if (e.checked)
            flags |= MF_CHECKED;
        AppendMenuA(h, flags, (UINT_PTR)MenuIdForCommand(e.command), e.label.c_str());
    }
    return h;
}

// Reconstruye la barra entera. Se hace cada vez que se va a desplegar un menu,
// para que las marcas de verificacion reflejen el estado actual.
void RebuildMenuBar() {
    MenuState st = MenuStateForUi();

    if (g_menubar != nullptr)
        DestroyMenu(g_menubar);
    g_menubar = CreateMenu();

    for (int m = 0; m < MENU_COUNT; ++m) {
        HMENU top = CreatePopupMenu();
        std::vector<MenuEntry> items = BuildTopMenu(m, st);
        for (const MenuEntry &e : items) {
            if (e.separator) {
                AppendMenuA(top, MF_SEPARATOR, 0, nullptr);
                continue;
            }
            if (e.submenu != SUB_NONE) {
                HMENU sub = BuildSubMenuNative(e.submenu, st);
                AppendMenuA(top, MF_STRING | MF_POPUP, (UINT_PTR)sub, e.label.c_str());
                continue;
            }
            UINT flags = MF_STRING;
            if (e.checked)
                flags |= MF_CHECKED;
            AppendMenuA(top, flags, (UINT_PTR)MenuIdForCommand(e.command),
                        e.label.c_str());
        }
        AppendMenuA(g_menubar, MF_STRING | MF_POPUP, (UINT_PTR)top, MenuTitle(m));
    }

    SetMenu(g_hwnd, g_menubar);
    DrawMenuBar(g_hwnd);
    g_need_menu = false;
}

// --- Ventana ----------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;

    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id >= kMenuIdBase) {
            WsEvent ev;
            ev.type = WSE_NONE;   // el comando se aplica directamente
            ExecuteCommand(CommandForMenuId(id));
            g_need_menu = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_ENTERMENULOOP:
    case WM_INITMENUPOPUP:
        // Antes de desplegar, se refrescan las marcas.
        if (g_need_menu)
            RebuildMenuBar();
        return 0;

    case WM_SIZE:
    case WM_EXITSIZEMOVE:
    case WM_WINDOWPOSCHANGED: {
        WsEvent ev;
        ev.type = WSE_RESIZE;
        g_events.push_back(ev);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const WsKey k = FromVirtualKey(wp, lp);
        if (k != WSK_UNKNOWN) {
            g_key_state[(unsigned)k & 0xFF] = true;
            WsEvent ev;
            ev.type = WSE_KEYDOWN;
            ev.key  = k;
            ev.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            g_events.push_back(ev);
        }
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
        const WsKey k = FromVirtualKey(wp, lp);
        if (k != WSK_UNKNOWN)
            g_key_state[(unsigned)k & 0xFF] = false;
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        WsEvent ev;
        ev.type   = WSE_MOUSEDOWN;
        ev.x      = GET_X_LPARAM(lp);
        ev.y      = GET_Y_LPARAM(lp);
        ev.clicks = (msg == WM_LBUTTONDBLCLK) ? 2 : 1;
        g_events.push_back(ev);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;   // todo el fondo lo pinta el doble buffer

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (g_mem_dc != nullptr && g_buf_w > 0 && g_buf_h > 0)
            BitBlt(dc, 0, 0, g_buf_w, g_buf_h, g_mem_dc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }

    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// --- Audio ------------------------------------------------------------------
// El sistema marca WHDR_DONE en el bloque cuando termina de reproducirlo, asi
// que aqui no hay nada que hacer: WsQueueAudio busca un bloque con esa marca.
void CALLBACK WaveProc(HWAVEOUT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR) {
}

} // namespace

// ---------------------------------------------------------------------------
// Ventana
// ---------------------------------------------------------------------------
bool WsInit(const char *title, int width, int height) {
    InitializeCriticalSection(&g_audio_lock);

    WNDCLASSA wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kWindowClass;
    wc.style         = CS_DBLCLKS;   // para que llegue WM_LBUTTONDBLCLK
    if (RegisterClassA(&wc) == 0)
        return false;

    // El tamano que se pide es el del area de vision; se ajusta para que el
    // marco y la barra de menu queden fuera.
    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

    g_hwnd = CreateWindowExA(0, kWindowClass, title, WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (g_hwnd == nullptr)
        return false;

    RebuildMenuBar();
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    return true;
}

std::string WsExeDir() {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return std::string(".");
    std::string dir(path, n);
    size_t p = dir.find_last_of("/\\");
    if (p != std::string::npos)
        dir = dir.substr(0, p);
    return dir;
}

bool WsHasNativeMenu() { return true; }

void WsShutdown() {
    WsCloseAudio();
    if (g_font != nullptr) {
        DeleteObject(g_font);
        g_font = nullptr;
    }
    if (g_mem_dc != nullptr) {
        DeleteDC(g_mem_dc);
        g_mem_dc = nullptr;
    }
    if (g_mem_bmp != nullptr) {
        DeleteObject(g_mem_bmp);
        g_mem_bmp = nullptr;
    }
    if (g_hwnd != nullptr) {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
    DeleteCriticalSection(&g_audio_lock);
}

void WsGetSize(int &w, int &h) {
    w = h = 0;
    if (g_hwnd == nullptr)
        return;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
}

void WsSetSize(int w, int h) {
    if (g_hwnd == nullptr)
        return;
    RECT rc = { 0, 0, w, h };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
    SetWindowPos(g_hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool WsIsMaximized() {
    if (g_hwnd == nullptr)
        return false;
    return IsZoomed(g_hwnd) != 0;
}

void WsSetTitle(const char *title) {
    if (g_hwnd != nullptr)
        SetWindowTextA(g_hwnd, title);
}

// ---------------------------------------------------------------------------
// Dibujo
// ---------------------------------------------------------------------------
void WsBeginFrame(int w, int h) {
    if (w <= 0 || h <= 0)
        return;

    if (g_mem_dc == nullptr || g_buf_w != w || g_buf_h != h) {
        if (g_mem_dc != nullptr) {
            DeleteDC(g_mem_dc);
            g_mem_dc = nullptr;
        }
        if (g_mem_bmp != nullptr) {
            DeleteObject(g_mem_bmp);
            g_mem_bmp = nullptr;
        }

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h;   // negativo: fila 0 arriba
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        HDC screen = GetDC(g_hwnd);
        g_mem_dc   = CreateCompatibleDC(screen);
        g_mem_bmp  = CreateDIBSection(screen, &bi, DIB_RGB_COLORS,
                                      (void **)&g_bits, nullptr, 0);
        ReleaseDC(g_hwnd, screen);

        SelectObject(g_mem_dc, g_mem_bmp);
        g_buf_w = w;
        g_buf_h = h;
    }

    // Fondo negro del area de vision.
    RECT rc = { 0, 0, w, h };
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(g_mem_dc, &rc, black);
    SetBkMode(g_mem_dc, TRANSPARENT);
}

void WsEndFrame() {
    if (g_hwnd != nullptr)
        InvalidateRect(g_hwnd, nullptr, FALSE);
}

void WsFillRect(int x, int y, int w, int h, WsColor c) {
    if (g_mem_dc == nullptr)
        return;
    RECT rc = { x, y, x + w, y + h };
    HBRUSH brush = CreateSolidBrush(RGB(c.r, c.g, c.b));
    FillRect(g_mem_dc, &rc, brush);
    DeleteObject(brush);
}

int WsTextHeight(int scale) {
    // Fuera del primer fotograma aun no hay DC; se responde con la medida de
    // la fuente integrada para no depender de GDI.
    if (g_mem_dc == nullptr)
        return 7 * scale;
    HFONT font = FontForScale(scale);
    HGDIOBJ old = SelectObject(g_mem_dc, font);
    TEXTMETRICA tm;
    GetTextMetricsA(g_mem_dc, &tm);
    SelectObject(g_mem_dc, old);
    return tm.tmHeight;
}

int WsTextWidth(const std::string &text, int scale) {
    if (g_mem_dc == nullptr)
        return (int)text.size() * 6 * scale;
    HFONT font = FontForScale(scale);
    HGDIOBJ old = SelectObject(g_mem_dc, font);
    SIZE sz = {};
    GetTextExtentPoint32A(g_mem_dc, text.c_str(), (int)text.size(), &sz);
    SelectObject(g_mem_dc, old);
    return (int)sz.cx;
}

void WsDrawText(int x, int y, const std::string &text, int scale, WsColor c) {
    if (g_mem_dc == nullptr || text.empty())
        return;
    HFONT font = FontForScale(scale);
    HGDIOBJ old = SelectObject(g_mem_dc, font);
    SetTextColor(g_mem_dc, RGB(c.r, c.g, c.b));
    SetBkMode(g_mem_dc, TRANSPARENT);
    TextOutA(g_mem_dc, x, y, text.c_str(), (int)text.size());
    SelectObject(g_mem_dc, old);
}

void WsDrawGame(const uint32_t *pixels, int disp_w, int disp_h,
                int dx, int dy, int dw, int dh) {
    if (g_mem_dc == nullptr || pixels == nullptr ||
        disp_w <= 0 || disp_h <= 0 || dw <= 0 || dh <= 0)
        return;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = disp_w;
    bi.bmiHeader.biHeight      = -disp_h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    // COLORONCOLOR copia el pixel mas cercano, sin interpolar. El WonderSwan
    // es pixel art de 224x144: con HALFTONE (interpolacion bilineal) los
    // bordes se emborronan y el juego se ve "derretido". Los filtros suaves
    // quedan para mas adelante, como opcion.
    const int old_mode = SetStretchBltMode(g_mem_dc, COLORONCOLOR);
    StretchDIBits(g_mem_dc, dx, dy, dw, dh,
                  0, 0, disp_w, disp_h,
                  pixels, &bi, DIB_RGB_COLORS, SRCCOPY);
    SetStretchBltMode(g_mem_dc, old_mode);
}

// ---------------------------------------------------------------------------
// Eventos
// ---------------------------------------------------------------------------
bool WsPollEvent(WsEvent &out) {
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_running = false;
            out.type = WSE_QUIT;
            return true;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (!g_events.empty()) {
        out = g_events.front();
        g_events.erase(g_events.begin());
        return true;
    }
    return false;
}

bool WsKeyDown(WsKey key) {
    if (key == WSK_UNKNOWN)
        return false;
    return g_key_state[(unsigned)key & 0xFF];
}

// ---------------------------------------------------------------------------
// Tiempo
// ---------------------------------------------------------------------------
uint64_t WsTicks() { return (uint64_t)GetTickCount64(); }
void     WsDelay(int ms) { Sleep((DWORD)ms); }

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
bool WsOpenAudio(int sample_rate) {
    WsCloseAudio();
    if (sample_rate <= 0)
        sample_rate = 44100;

    WAVEFORMATEX wf = {};
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 2;
    wf.nSamplesPerSec  = (DWORD)sample_rate;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = (WORD)(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    if (waveOutOpen(&g_wave, WAVE_MAPPER, &wf, (DWORD_PTR)WaveProc, 0,
                    CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        WsLog("waveOutOpen fallo; se continua sin audio");
        g_wave = nullptr;
        return false;
    }

    g_wave_rate  = sample_rate;
    g_audio_ok   = true;
    g_audio_paused = false;

    // Cuatro bloques de un cuarto de segundo: suficiente para que el callback
    // tenga siempre uno libre sin retrasar el hilo de emulacion.
    const size_t frames = (size_t)sample_rate / 4;
    for (int i = 0; i < 4; ++i) {
        auto *b = new WaveBlock();
        b->data.resize(frames * 2);
        b->hdr.lpData         = (LPSTR)b->data.data();
        b->hdr.dwBufferLength = (DWORD)(b->data.size() * sizeof(int16_t));
        waveOutPrepareHeader(g_wave, &b->hdr, sizeof(WAVEHDR));
        b->hdr.dwFlags |= WHDR_DONE;
        g_wave_blocks.push_back(b);
    }
    return true;
}

void WsCloseAudio() {
    if (g_wave != nullptr) {
        waveOutReset(g_wave);
        for (WaveBlock *b : g_wave_blocks) {
            waveOutUnprepareHeader(g_wave, &b->hdr, sizeof(WAVEHDR));
            delete b;
        }
        g_wave_blocks.clear();
        waveOutClose(g_wave);
        g_wave = nullptr;
    }
    g_audio_ok = false;
}

void WsQueueAudio(const int16_t *data, size_t frames) {
    if (g_wave == nullptr || data == nullptr || frames == 0)
        return;

    // Se copia a un bloque libre. Si no hay ninguno, se descarta el sobrante:
    // es preferible a bloquear la emulacion.
    EnterCriticalSection(&g_audio_lock);
    for (WaveBlock *b : g_wave_blocks) {
        if (b->hdr.dwFlags & WHDR_DONE) {
            size_t max_frames = b->data.size() / 2;
            size_t n = (frames < max_frames) ? frames : max_frames;
            memcpy(b->data.data(), data, n * 2 * sizeof(int16_t));
            b->hdr.dwBufferLength = (DWORD)(n * 2 * sizeof(int16_t));
            b->hdr.dwFlags &= ~WHDR_DONE;
            if (!g_audio_paused)
                waveOutWrite(g_wave, &b->hdr, sizeof(WAVEHDR));
            else
                b->hdr.dwFlags |= WHDR_DONE;
            break;
        }
    }
    LeaveCriticalSection(&g_audio_lock);
}

void WsSetAudioPaused(bool paused) {
    g_audio_paused = paused;
    if (g_wave != nullptr) {
        if (paused)
            waveOutReset(g_wave);
    }
}

// ---------------------------------------------------------------------------
// Dialogos y registro
// ---------------------------------------------------------------------------
void WsMessageBox(const char *title, const char *text) {
    MessageBoxA(g_hwnd, text, title, MB_OK | MB_ICONERROR);
}

void WsAboutDialog(const char *title, const char *body) {
    // Cuadro nativo del sistema, como el de cualquier programa de Windows.
    MessageBoxA(g_hwnd, body, title, MB_OK | MB_ICONINFORMATION);
}

void WsLog(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}
