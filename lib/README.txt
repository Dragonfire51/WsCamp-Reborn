Coloca aqui el nucleo libretro (se busca tambien junto al ejecutable):

    Linux    mednafen_wswan_libretro.so
    Windows  mednafen_wswan_libretro.dll
    macOS    mednafen_wswan_libretro.dylib

Se carga en tiempo de ejecucion (SDL_LoadObject -> dlopen/LoadLibrary), asi que
no hace falta enlazarlo al compilar. Tambien se acepta el prefijo beetle_.

Para el control por canal de sonido (canales 1..4, ruido, PCM y DMA) hay que
usar el nucleo que compila "make core", que parte de beetle-wswan-libretro y le
aplica tools/patch_core_sound.py. Un nucleo sin parchear funciona igual, pero
las opciones de canal no tendran efecto.

Build recomendado: beetle-wswan-libretro (mednafen/wswan), GPLv2.
