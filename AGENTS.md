# Notas para agentes

Frontend SDL2 en C++17 para el nucleo libretro beetle-wswan (mednafen/wswan).
Emulador WonderSwan / WonderSwan Color con 10 ranuras de Save State.

## Compilar y probar

```sh
make                       # plataforma actual
make CROSS=x86_64          # Windows 64 bits (MinGW-w64)
make CROSS=i686            # Windows 32 bits (MinGW-w64)
make core                  # nucleo con control por canal, en lib/
make test ROM=juego.ws     # video + control por canal + autocomprobacion
```

En un contenedor Linux hacen falta `libsdl2-dev`, `g++-mingw-w64-x86-64` y
`g++-mingw-w64-i686`. Las librerias SDL2 para MinGW ya estan en
`/opt/sdl2-mingw/<triple>/`; el Makefile las busca ahi cuando `CROSS` no esta
vacio. Comprobar la arquitectura de un `.exe`: el campo `Machine` de la cabecera
PE esta en el offset `0x3C` + 4 (`0x8664` = x64, `0x14c` = i386).

## El nucleo

`lib/` no se versiona: lo genera `tools/build_core.sh`, que clona
beetle-wswan-libretro en `build/core/` (commit fijado), aplica
`tools/patch_core_sound.py` y copia el resultado a `lib/`. El nucleo se carga
en tiempo de ejecucion (`SDL_LoadObject`), nunca se enlaza.

El parche anade siete opciones (`wswan_sound_ch1..ch4`, `noise`, `pcm`, `dma`),
las consulta en `check_variables()` de `libretro.c` y guarda el resultado en
`ws_sound_ch_enabled[]`, que el mezclador de `sound.c` lee al generar cada
muestra. PCM comparte la bandera del canal 2. El simbolo se exporta desde
`link.T` para que las pruebas puedan comprobarlo.

Dos trampas al tocar el parche:

- Es idempotente, pero la marca general no sirve en `sound.c`: `patch_file` ya
  la ha escrito antes de las ediciones por expresion regular, asi que cada una
  lleva su propio marcador (`done_marker`).
- Cada sustitucion comprueba que su ancla aparezca exactamente una vez. Si
  upstream cambia, falla con un mensaje claro en lugar de corromper el fichero.

Sin el nucleo parcheado el emulador funciona igual, pero las opciones de canal
no tienen efecto; `sound_test` lo detecta y se marca como omitida.

## Convenciones

- Identificadores y nombres de fichero sin tildes ni enes (ASCII), tambien en
  los comentarios; el texto que ve el usuario si lleva acentos.
- La version vive solo en `src/version.h`. El CI la pasa con `-D` y de ahi
  salen el titulo, el dialogo "Acerca de..." y el nombre del release.
- `WsCampReborn.ini` guarda las opciones del menu y se relee al arrancar.

## CI

`.github/workflows/build.yml` se dispara en `main`, tags y `workflow_dispatch`
(no en ramas normales: para probar una rama hay que lanzarlo a mano con
`gh workflow run build --ref <rama>`). Compila el nucleo por plataforma, lo
empaqueta junto al ejecutable y ejecuta `make test` en Linux.

El release `latest` se reemplaza entero: el job borra los paquetes cuyo nombre
no lleva la version actual, porque subir con `--clobber` no elimina los
sobrantes.
