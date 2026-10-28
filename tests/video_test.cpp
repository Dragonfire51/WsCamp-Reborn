// Comprueba la conversion de formato y las cuatro rotaciones con un fotograma
// cuyos pixeles son unicos y reconocibles.
#include <cstdio>
#include <cstring>
#include <vector>
#include "video.h"

// El pixel de origen ocupa 2 bytes en 0RGB1555/RGB565 y 4 en XRGB8888, asi que
// el buffer debe ser del tamano correcto.
static bool TestFormat(enum retro_pixel_format fmt, uint32_t raw,
                       uint32_t expect) {
    uint32_t px = raw;
    size_t pitch = (fmt == RETRO_PIXEL_FORMAT_XRGB8888) ? 4 : 2;
    std::vector<uint32_t> out;
    unsigned w = 0, h = 0;
    ConvertFrame(&px, 1, 1, pitch, fmt, 0, out, w, h);
    return out.size() == 1 && out[0] == expect;
}

int main() {
    bool ok = true;

    // 0RGB1555: 0x7C00 = rojo puro -> FF0000
    ok &= TestFormat(RETRO_PIXEL_FORMAT_0RGB1555, 0x7C00, 0xFFFF0000u);
    // 0RGB1555: 0x03E0 = verde puro -> 00FF00
    ok &= TestFormat(RETRO_PIXEL_FORMAT_0RGB1555, 0x03E0, 0xFF00FF00u);
    // 0RGB1555: 0x001F = azul puro -> 0000FF
    ok &= TestFormat(RETRO_PIXEL_FORMAT_0RGB1555, 0x001F, 0xFF0000FFu);
    // RGB565: 0xF800 = rojo puro
    ok &= TestFormat(RETRO_PIXEL_FORMAT_RGB565, 0xF800, 0xFFFF0000u);
    // XRGB8888: el canal alfa de origen se descarta y se pone opaco.
    ok &= TestFormat(RETRO_PIXEL_FORMAT_XRGB8888, 0x00ABCDEFu, 0xFFABCDEFu);
    printf("formatos: %s\n", ok ? "OK" : "FALLO");

    // Fotograma 2x3 con un valor distinto por pixel (RGB565).
    const unsigned W = 2, H = 3;
    uint16_t frame[W * H];
    // El canal verde (bits 5..10) lleva un valor distinto por pixel, de modo
    // que el orden de los pixeles se puede comprobar sin ambiguedad.
    for (unsigned i = 0; i < W * H; ++i)
        frame[i] = (uint16_t)((i + 1) << 5);

    // Sin rotacion: el orden se conserva y el tamano no cambia.
    {
        std::vector<uint32_t> out; unsigned w = 0, h = 0;
        ConvertFrame(frame, W, H, W * sizeof(uint16_t), RETRO_PIXEL_FORMAT_RGB565,
                     0, out, w, h);
        bool good = (w == W && h == H);
        // El canal verde de RGB565 tiene 6 bits y se expande a 8 con
        // (v << 2) | (v >> 4); el rojo y el azul quedan a cero.
        for (unsigned i = 0; i < W * H && good; ++i) {
            uint32_t g6 = i + 1;
            uint32_t g8 = (g6 << 2) | (g6 >> 4);
            good = (out[i] == (0xFF000000u | (g8 << 8)));
        }
        printf("rotacion 0:   %s (w=%u h=%u)\n", good ? "OK" : "FALLO", w, h);
        ok &= good;
    }

    // 180 grados: tamano igual y orden invertido.
    {
        std::vector<uint32_t> out; unsigned w = 0, h = 0;
        ConvertFrame(frame, W, H, W * sizeof(uint16_t), RETRO_PIXEL_FORMAT_RGB565,
                     180, out, w, h);
        std::vector<uint32_t> ref; unsigned rw = 0, rh = 0;
        ConvertFrame(frame, W, H, W * sizeof(uint16_t), RETRO_PIXEL_FORMAT_RGB565,
                     0, ref, rw, rh);
        bool good = (w == W && h == H);
        for (unsigned i = 0; i < W * H && good; ++i)
            good = (out[i] == ref[W * H - 1 - i]);
        printf("rotacion 180: %s (w=%u h=%u)\n", good ? "OK" : "FALLO", w, h);
        ok &= good;
    }

    // 90 y 270: ancho y alto se intercambian.
    {
        std::vector<uint32_t> out; unsigned w = 0, h = 0;
        ConvertFrame(frame, W, H, W * sizeof(uint16_t), RETRO_PIXEL_FORMAT_RGB565,
                     90, out, w, h);
        bool good = (w == H && h == W);
        printf("rotacion 90:  %s (w=%u h=%u)\n", good ? "OK" : "FALLO", w, h);
        ok &= good;
    }
    {
        std::vector<uint32_t> out; unsigned w = 0, h = 0;
        ConvertFrame(frame, W, H, W * sizeof(uint16_t), RETRO_PIXEL_FORMAT_RGB565,
                     270, out, w, h);
        bool good = (w == H && h == W);
        printf("rotacion 270: %s (w=%u h=%u)\n", good ? "OK" : "FALLO", w, h);
        ok &= good;
    }

    // Un fotograma nulo debe dejar la salida vacia, no fallar.
    {
        std::vector<uint32_t> out; unsigned w = 0, h = 0;
        ConvertFrame(nullptr, W, H, W * sizeof(uint16_t),
                     RETRO_PIXEL_FORMAT_RGB565, 0, out, w, h);
        bool good = (out.empty() && w == 0 && h == 0);
        printf("fotograma nulo: %s\n", good ? "OK" : "FALLO");
        ok &= good;
    }

    // --- Encaje de la imagen en la ventana (escalado) ---
    // Es lo que fallaba al maximizar: la imagen se encogia pero nunca crecia,
    // asi que el juego se quedaba pequeno en medio de una ventana grande.
    {
        int x, y, w, h;

        // Ventana pequena (224x144): se reduce manteniendo el aspecto 14:9.
        FitGameRect(112, 72, 224, 144, &x, &y, &w, &h);
        bool good = (w == 112 && h == 72 && x == 0 && y == 0);
        printf("encaje ventana pequena: %s (%dx%d en %d,%d)\n",
               good ? "OK" : "FALLO", w, h, x, y);
        ok &= good;

        // Ventana maximizada (1920x1080 con 22 px de barra): la imagen crece
        // hasta el alto util (1080-22) sin deformarse y queda centrada.
        FitGameRect(1920, 1080 - 22, 224, 144, &x, &y, &w, &h);
        int expect_h = 1058;
        int expect_w = (int)(224.0 * 1058.0 / 144.0 + 0.5);
        good = (h == expect_h && w == expect_w && x == (1920 - expect_w) / 2 && y == 0);
        printf("encaje ventana maximizada: %s (%dx%d en %d,%d)\n",
               good ? "OK" : "FALLO", w, h, x, y);
        ok &= good;

        // En vertical (144x224) el alto manda: la imagen crece con aspecto 9:14.
        FitGameRect(1920, 1058, 144, 224, &x, &y, &w, &h);
        expect_h = 1058;
        expect_w = (int)(144.0 * 1058.0 / 224.0 + 0.5);
        good = (h == expect_h && w == expect_w);
        printf("encaje vertical: %s (%dx%d)\n", good ? "OK" : "FALLO", w, h);
        ok &= good;

        // Area util cero (ventana minimizada): rectangulo vacio, sin dividir
        // por cero.
        FitGameRect(0, 0, 224, 144, &x, &y, &w, &h);
        good = (w == 0 && h == 0 && x == 0 && y == 0);
        printf("encaje minimizada: %s (%dx%d)\n", good ? "OK" : "FALLO", w, h);
        ok &= good;
    }

    printf(ok ? "VIDEOTEST: OK\n" : "VIDEOTEST: FALLO\n");
    return ok ? 0 : 1;
}
