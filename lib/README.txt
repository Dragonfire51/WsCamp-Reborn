Coloca aqui el nucleo libretro:

    mednafen_wswan_libretro.dll

(tambien se acepta beetle_wswan_libretro.dll o libretro.dll)

El programa tambien lo busca junto al ejecutable. La DLL se carga en tiempo de
ejecucion con LoadLibrary/GetProcAddress; no hace falta enlazarla.

Build recomendado: beetle-wswan-libretro (mednafen/wswan), GPLv2.
