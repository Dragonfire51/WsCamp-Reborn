// platform.h -- Capa de plataforma del frontend.
//
// El codigo de la interfaz (menu, explorador, dibujo del juego) no llama a SDL
// ni a Win32 directamente: usa estas primitivas. Hay dos implementaciones:
//
//   platform_sdl.cpp    Linux y macOS, sobre SDL2.
//   platform_win32.cpp  Windows, sobre Win32 y GDI.
//
// En Windows el texto lo dibuja GDI con una fuente del sistema, que se adapta
// al tamano de la ventana y no se corta al maximizar o minimizar. En Linux y
// macOS se usa la fuente de mapa de bits integrada.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "keys.h"

struct WsColor {
    uint8_t r, g, b, a;
};

// ---------------------------------------------------------------------------
// Ventana
// ---------------------------------------------------------------------------
bool WsInit(const char *title, int width, int height);
void WsShutdown();

// Directorio del ejecutable, sin separador final.
std::string WsExeDir();

// true si el backend pone el menu del sistema (Windows). En ese caso el
// frontend no dibuja barra de menus propia ni la maneja con el teclado: lo hace
// el sistema, y los comandos llegan por ExecuteCommand().
bool WsHasNativeMenu();

void WsGetSize(int &w, int &h);
void WsSetSize(int w, int h);
bool WsIsMaximized();
void WsSetTitle(const char *title);

// ---------------------------------------------------------------------------
// Dibujo
// ---------------------------------------------------------------------------
// Cada fotograma empieza con WsBeginFrame y termina con WsEndFrame, que
// presenta lo dibujado. Todo lo que se pinta entre medias va al area de
// vision completa (sin contar decoraciones de la ventana).
void WsBeginFrame(int w, int h);
void WsEndFrame();

void WsFillRect(int x, int y, int w, int h, WsColor c);

// Texto. La escala es la de la fuente de mapa de bits; el backend nativo la
// usa como referencia para elegir el tamano de la fuente del sistema.
void WsDrawText(int x, int y, const std::string &text, int scale, WsColor c);
int  WsTextWidth(const std::string &text, int scale);
int  WsTextHeight(int scale);

// Imagen del juego: pixeles ARGB8888 (0xAARRGGBB) de disp_w x disp_h, que se
// dibujan escalados al rectangulo indicado.
void WsDrawGame(const uint32_t *pixels, int disp_w, int disp_h,
                int dx, int dy, int dw, int dh);

// ---------------------------------------------------------------------------
// Eventos
// ---------------------------------------------------------------------------
enum WsEventType {
    WSE_NONE = 0,
    WSE_QUIT,
    WSE_KEYDOWN,
    WSE_MOUSEDOWN,
    WSE_RESIZE
};

struct WsEvent {
    WsEventType type   = WSE_NONE;
    WsKey       key    = WSK_UNKNOWN;
    bool        ctrl   = false;
    int         x      = 0;
    int         y      = 0;
    int         clicks = 1;   // 2 en el segundo clic de un doble clic
};

// Devuelve false cuando no queda ningun evento pendiente.
bool WsPollEvent(WsEvent &ev);

// Estado actual de una tecla (para la emulacion).
bool WsKeyDown(WsKey key);

// ---------------------------------------------------------------------------
// Tiempo
// ---------------------------------------------------------------------------
uint64_t WsTicks();          // milisegundos desde un origen arbitrario
void     WsDelay(int ms);

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
bool WsOpenAudio(int sample_rate);
void WsCloseAudio();
void WsQueueAudio(const int16_t *data, size_t frames);
void WsSetAudioPaused(bool paused);

// ---------------------------------------------------------------------------
// Dialogos
// ---------------------------------------------------------------------------
void WsMessageBox(const char *title, const char *text);

// Cuadro "Acerca de..." nativo. Se apoya en el dialogo del sistema, no en el
// dibujado dentro de la ventana: asi se ve como el resto de programas.
void WsAboutDialog(const char *title, const char *body);

// ---------------------------------------------------------------------------
// Registro
// ---------------------------------------------------------------------------
void WsLog(const char *fmt, ...);
