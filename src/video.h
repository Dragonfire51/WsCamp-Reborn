// video.h -- Conversion de fotogramas del nucleo a ARGB8888 y rotacion.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ws_core.h"

// Convierte un fotograma del nucleo al formato que espera SDL (ARGB8888) y le
// aplica la rotacion indicada (0, 90, 180 o 270 grados).
//
// out recibe los pixeles y se redimensiona si hace falta. out_w y out_h
// reciben el tamano resultante: con 90 y 270 grados se intercambian ancho y
// alto. Un valor de rotacion distinto de 0/90/180/270 se trata como 0.
void ConvertFrame(const void *data, unsigned width, unsigned height, size_t pitch,
                  enum retro_pixel_format fmt, unsigned rotation,
                  std::vector<uint32_t> &out, unsigned &out_w, unsigned &out_h);
