// platform_sdl.cpp -- Capa de plataforma sobre SDL2 (Linux y macOS).
//
// Windows no usa este fichero: alli manda platform_win32.cpp, que dibuja con
// GDI. Aqui el texto se pinta con la fuente de mapa de bits integrada, que da
// el aspecto retro y no depende de ficheros .ttf externos.
#include "platform.h"

#include <SDL.h>

#include <cstdarg>
#include <cstdio>
#include <vector>

#include "font5x7.h"

namespace {

SDL_Window   *g_window   = nullptr;
SDL_Renderer *g_renderer = nullptr;
SDL_Texture  *g_game_tex = nullptr;
int           g_game_tex_w = 0, g_game_tex_h = 0;

SDL_AudioDeviceID g_audio_dev = 0;
bool              g_audio_paused = false;

const FontGlyph *FindGlyph(char c) {
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (const FontGlyph &g : FONT_GLYPHS)
        if (g.ch == c)
            return &g;
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Ventana
// ---------------------------------------------------------------------------
bool WsInit(const char *title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init fallo: %s\n", SDL_GetError());
        return false;
    }

    g_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width, height,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (g_window == nullptr) {
        fprintf(stderr, "SDL_CreateWindow fallo: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    g_renderer = SDL_CreateRenderer(g_window, -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (g_renderer == nullptr)
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    if (g_renderer == nullptr) {
        fprintf(stderr, "SDL_CreateRenderer fallo: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return false;
    }

    return true;
}

std::string WsExeDir() {
    char *base = SDL_GetBasePath();
    if (base == nullptr)
        return std::string(".");
    std::string dir(base);
    SDL_free(base);
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    return dir;
}

bool WsHasNativeMenu() { return false; }

void WsShutdown() {
    if (g_game_tex != nullptr) {
        SDL_DestroyTexture(g_game_tex);
        g_game_tex = nullptr;
    }
    if (g_renderer != nullptr) {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = nullptr;
    }
    if (g_window != nullptr) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_Quit();
}

void WsGetSize(int &w, int &h) {
    w = h = 0;
    if (g_window != nullptr)
        SDL_GetWindowSize(g_window, &w, &h);
}

void WsSetSize(int w, int h) {
    if (g_window != nullptr)
        SDL_SetWindowSize(g_window, w, h);
}

bool WsIsMaximized() {
    if (g_window == nullptr)
        return false;
    return (SDL_GetWindowFlags(g_window) &
            (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN |
             SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
}

void WsSetTitle(const char *title) {
    if (g_window != nullptr)
        SDL_SetWindowTitle(g_window, title);
}

// ---------------------------------------------------------------------------
// Dibujo
// ---------------------------------------------------------------------------
void WsBeginFrame(int w, int h) {
    (void)w; (void)h;
    SDL_SetRenderTarget(g_renderer, nullptr);
}

void WsEndFrame() {
    SDL_RenderPresent(g_renderer);
}

void WsFillRect(int x, int y, int w, int h, WsColor c) {
    SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, c.a);
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderFillRect(g_renderer, &rc);
}

int WsTextHeight(int scale) { return FONT_GLYPH_H * scale; }

int WsTextWidth(const std::string &text, int scale) {
    return (int)text.size() * FONT_ADVANCE * scale;
}

void WsDrawText(int x, int y, const std::string &text, int scale, WsColor color) {
    SDL_SetRenderDrawColor(g_renderer, color.r, color.g, color.b, color.a);
    int cx = x;
    for (char c : text) {
        const FontGlyph *g = FindGlyph(c);
        if (g != nullptr) {
            for (int row = 0; row < FONT_GLYPH_H; ++row) {
                const char *bits = g->rows[row];
                for (int col = 0; col < FONT_GLYPH_W; ++col) {
                    if (bits[col] != '#')
                        continue;
                    SDL_Rect px = { cx + col * scale, y + row * scale, scale, scale };
                    SDL_RenderFillRect(g_renderer, &px);
                }
            }
        }
        cx += FONT_ADVANCE * scale;
    }
}

void WsDrawGame(const uint32_t *pixels, int disp_w, int disp_h,
                int dx, int dy, int dw, int dh) {
    if (pixels == nullptr || disp_w <= 0 || disp_h <= 0 || dw <= 0 || dh <= 0)
        return;

    if (g_game_tex == nullptr || g_game_tex_w != disp_w || g_game_tex_h != disp_h) {
        if (g_game_tex != nullptr)
            SDL_DestroyTexture(g_game_tex);
        g_game_tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, disp_w, disp_h);
        g_game_tex_w = disp_w;
        g_game_tex_h = disp_h;
        if (g_game_tex != nullptr)
            SDL_SetTextureBlendMode(g_game_tex, SDL_BLENDMODE_NONE);
    }
    if (g_game_tex == nullptr)
        return;

    SDL_UpdateTexture(g_game_tex, nullptr, pixels, disp_w * 4);
    SDL_Rect dst = { dx, dy, dw, dh };
    SDL_RenderCopy(g_renderer, g_game_tex, nullptr, &dst);
}

// ---------------------------------------------------------------------------
// Eventos
// ---------------------------------------------------------------------------
bool WsPollEvent(WsEvent &out) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            out.type = WSE_QUIT;
            return true;
        case SDL_KEYDOWN:
            out.type = WSE_KEYDOWN;
            out.key  = (WsKey)ev.key.keysym.scancode;
            out.ctrl = (ev.key.keysym.mod & KMOD_CTRL) != 0;
            return true;
        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                out.type   = WSE_MOUSEDOWN;
                out.x      = ev.button.x;
                out.y      = ev.button.y;
                out.clicks = ev.button.clicks;
                return true;
            }
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                ev.window.event == SDL_WINDOWEVENT_RESTORED ||
                ev.window.event == SDL_WINDOWEVENT_MAXIMIZED) {
                out.type = WSE_RESIZE;
                return true;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

bool WsKeyDown(WsKey key) {
    const Uint8 *state = SDL_GetKeyboardState(nullptr);
    if (state == nullptr || key == WSK_UNKNOWN)
        return false;
    return state[(SDL_Scancode)key] != 0;
}

// ---------------------------------------------------------------------------
// Tiempo
// ---------------------------------------------------------------------------
uint64_t WsTicks() { return (uint64_t)SDL_GetTicks64(); }
void     WsDelay(int ms) { SDL_Delay((Uint32)ms); }

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
bool WsOpenAudio(int sample_rate) {
    WsCloseAudio();
    if (sample_rate <= 0)
        sample_rate = 44100;

    SDL_AudioSpec want = {};
    want.freq     = sample_rate;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;

    SDL_AudioSpec have = {};
    g_audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audio_dev == 0) {
        WsLog("SDL_OpenAudioDevice fallo: %s; se continua sin audio", SDL_GetError());
        return false;
    }
    g_audio_paused = false;
    return true;
}

void WsCloseAudio() {
    if (g_audio_dev != 0) {
        SDL_CloseAudioDevice(g_audio_dev);
        g_audio_dev = 0;
    }
}

void WsQueueAudio(const int16_t *data, size_t frames) {
    if (g_audio_dev == 0 || data == nullptr || frames == 0)
        return;
    // Si el buffer del dispositivo se llena, SDL descarta el exceso; es
    // preferible a bloquear el hilo de emulacion.
    SDL_QueueAudio(g_audio_dev, data, (Uint32)(frames * 2 * sizeof(int16_t)));
}

void WsSetAudioPaused(bool paused) {
    if (g_audio_dev == 0)
        return;
    g_audio_paused = paused;
    SDL_PauseAudioDevice(g_audio_dev, paused ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Dialogos y registro
// ---------------------------------------------------------------------------
void WsMessageBox(const char *title, const char *text) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, g_window);
}

void WsLog(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SDL_Log("%s", buf);
}
