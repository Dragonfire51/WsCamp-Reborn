// flip_test.cpp -- Comprueba el volteo de la ventana completa (F1).
//
// El volteo se aplica al presentar: toda la interfaz se dibuja en una textura
// y se gira 180 grados, de modo que el menu y el explorador giran junto con la
// imagen del juego. Se dibuja una marca asimetrica en la esquina superior
// izquierda y se lee el framebuffer para confirmar que acaba en la esquina
// opuesta y sin espejo horizontal (que seria un giro de 180 mal hecho).
//
// No necesita el nucleo ni una ROM.
#include <SDL.h>
#include <cstdio>

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    const int W = 200, H = 120;
    SDL_Window *win = SDL_CreateWindow("t", 0, 0, W, H, SDL_WINDOW_HIDDEN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { printf("sin renderer: %s\n", SDL_GetError()); return 1; }

    SDL_Texture *target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_TARGET, W, H);
    if (!target) { printf("sin textura: %s\n", SDL_GetError()); return 1; }

    // Dibuja: fondo negro, marca roja en la esquina superior izquierda.
    SDL_SetRenderTarget(ren, target);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    SDL_Rect mark = { 0, 0, 10, 10 };
    SDL_RenderFillRect(ren, &mark);

    // Presenta girado 180 (igual que EndFrame con el volteo activo).
    SDL_SetRenderTarget(ren, nullptr);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopyEx(ren, target, nullptr, nullptr, 180.0, nullptr, SDL_FLIP_NONE);

    // Lee el framebuffer: la marca debe aparecer en la esquina INFERIOR derecha.
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32,
                                                       SDL_PIXELFORMAT_ARGB8888);
    bool read_ok = SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                        surf->pixels, surf->pitch) == 0;
    if (!read_ok) { printf("no se pudo leer el framebuffer\n"); return 1; }

    Uint32 *px = (Uint32 *)surf->pixels;
    int pitch_px = surf->pitch / 4;
    Uint32 top_left     = px[5 * pitch_px + 5];                 // centro de la marca
    Uint32 bottom_right = px[(H - 6) * pitch_px + (W - 6)];     // esquina opuesta
    Uint32 bottom_left  = px[(H - 6) * pitch_px + 5];

    bool red_tl = (top_left & 0x00FFFFFF) == 0x00FF0000;
    bool red_br = (bottom_right & 0x00FFFFFF) == 0x00FF0000;
    bool red_bl = (bottom_left & 0x00FFFFFF) == 0x00FF0000;

    printf("arriba-izquierda queda vacia tras voltear : %s\n", red_tl ? "no" : "si");
    printf("tras voltear 180 aparece abajo-derecha : %s\n", red_br ? "si" : "no");
    printf("no aparece abajo-izquierda (sin espejo) : %s\n", red_bl ? "no" : "si");

    bool ok = !red_tl && red_br && !red_bl;
    printf(ok ? "FLIPTEST: OK\n" : "FLIPTEST: FALLO\n");
    SDL_Quit();
    return ok ? 0 : 1;
}
