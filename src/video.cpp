// video.cpp -- Conversion de fotogramas del nucleo a ARGB8888 y rotacion.
#include "video.h"

#include <cstring>

namespace {

uint32_t PixelToArgb(enum retro_pixel_format fmt, const uint8_t *base, size_t pitch,
                     unsigned x, unsigned y) {
    switch (fmt) {
    case RETRO_PIXEL_FORMAT_XRGB8888: {
        const uint32_t *row = reinterpret_cast<const uint32_t *>(base + y * pitch);
        return 0xFF000000u | (row[x] & 0x00FFFFFFu);
    }
    case RETRO_PIXEL_FORMAT_RGB565: {
        const uint16_t *row = reinterpret_cast<const uint16_t *>(base + y * pitch);
        uint16_t p = row[x];
        unsigned r5 = (p >> 11) & 0x1F;
        unsigned g6 = (p >> 5)  & 0x3F;
        unsigned b5 =  p        & 0x1F;
        unsigned r8 = (r5 << 3) | (r5 >> 2);
        unsigned g8 = (g6 << 2) | (g6 >> 4);
        unsigned b8 = (b5 << 3) | (b5 >> 2);
        return 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
    }
    case RETRO_PIXEL_FORMAT_0RGB1555:
    default: {
        const uint16_t *row = reinterpret_cast<const uint16_t *>(base + y * pitch);
        uint16_t p = row[x];
        unsigned r5 = (p >> 10) & 0x1F;
        unsigned g5 = (p >> 5)  & 0x1F;
        unsigned b5 =  p        & 0x1F;
        unsigned r8 = (r5 << 3) | (r5 >> 2);
        unsigned g8 = (g5 << 3) | (g5 >> 2);
        unsigned b8 = (b5 << 3) | (b5 >> 2);
        return 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
    }
    }
}

} // namespace

void ConvertFrame(const void *data, unsigned width, unsigned height, size_t pitch,
                  enum retro_pixel_format fmt, unsigned rotation,
                  std::vector<uint32_t> &out, unsigned &out_w, unsigned &out_h) {
    if (data == nullptr || width == 0 || height == 0) {
        out.clear();
        out_w = out_h = 0;
        return;
    }

    switch (rotation) {
    case 90:
    case 180:
    case 270:
        break;
    default:
        rotation = 0;
        break;
    }

    if (rotation == 90 || rotation == 270) {
        out_w = height;
        out_h = width;
    } else {
        out_w = width;
        out_h = height;
    }
    out.resize((size_t)out_w * out_h);

    const uint8_t *base = reinterpret_cast<const uint8_t *>(data);

    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            uint32_t px = PixelToArgb(fmt, base, pitch, x, y);

            unsigned dx, dy;
            switch (rotation) {
            case 90:  dx = height - 1 - y; dy = x;              break;
            case 180: dx = width - 1 - x;  dy = height - 1 - y; break;
            case 270: dx = y;              dy = width - 1 - x;  break;
            default:  dx = x;              dy = y;              break;
            }
            out[(size_t)dy * out_w + dx] = px;
        }
    }
}

void FitGameRect(int view_w, int view_h, int disp_w, int disp_h,
                 int *x, int *y, int *w, int *h) {
    *x = *y = *w = *h = 0;
    if (view_w <= 0 || view_h <= 0 || disp_w <= 0 || disp_h <= 0)
        return;

    // El menor de los dos factores llena la ventana sin deformar la imagen.
    double sx = (double)view_w / disp_w;
    double sy = (double)view_h / disp_h;
    double scale = (sx < sy) ? sx : sy;
    if (scale <= 0.0)
        return;

    int dw = (int)(disp_w * scale + 0.5);
    int dh = (int)(disp_h * scale + 0.5);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    *w = dw;
    *h = dh;
    *x = (view_w - dw) / 2;
    *y = (view_h - dh) / 2;
}
