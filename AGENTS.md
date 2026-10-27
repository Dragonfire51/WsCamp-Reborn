# Notas para agentes

Frontend SDL2 en C++17 para el nucleo libretro beetle-wswan (mednafen/wswan).
Emulador WonderSwan / WonderSwan Color con 10 ranuras de Save State.

## Compilar y probar

```sh
make                       # plataforma actual
make CROSS=x86_64          # Windows 64 bits (MinGW-w64)
make CROSS=i686            # Windows 32 bits (MinGW-w64)
make core                  # nucleo con control por canal, en lib/
make test ROM=juego.ws     # video, volteo, rotacion, sonido y autocomprobacion
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

## Bucle de emulacion

El WonderSwan emite a 75,47 Hz, no a 60. El bucle no puede presentar una vez por
fotograma emulado: con vsync a 60 Hz eso ataria la emulacion al monitor y el
juego correria al 79%. En su lugar acumula el tiempo real transcurrido
(`frame_accum`) y ejecuta los `retro_run` que correspondan, dejando que la
presentacion siga el ritmo del monitor. Si se toca el bucle, hay que conservar
ese desacople; medirlo es facil con un contador de `retro_run` durante un par de
segundos (debe dar ~75/s, no ~60/s).

El nucleo tarda ~0,3 ms por fotograma (mas de 3000 fps de margen), asi que la
emulacion nunca es el cuello de botella.

## Volteo y orientacion

Son dos cosas distintas y conviene no mezclarlas:

- El volteo (F1, `g_cfg.flip`) gira **la ventana entera** 180 grados. Se aplica
  al presentar: `BeginFrame` redirige el dibujo a una textura y `EndFrame` la
  presenta con `SDL_RenderCopyEx`. Por eso el menu y el explorador tambien
  giran, y por eso las coordenadas del raton se reflejan antes de usarlas.
- La orientacion (`g_cfg.rotate`) es la opcion `wswan_rotate_display` del
  nucleo. En vertical entrega 144x224 y en horizontal 224x144.

El frontend **rechaza** `RETRO_ENVIRONMENT_SET_ROTATION` a proposito: asi el
nucleo rota el fotograma el mismo en software y lo entrega ya en su sitio. Si se
aceptara, el nucleo delegaria en el frontend y habria que replicar su
codificacion de giro (0/1/2/3, no grados) con riesgo de dejarlo invertido.
Cuando el nucleo cambia de geometria, el bucle lo detecta y reajusta la ventana.

## Convenciones

- Identificadores y nombres de fichero sin tildes ni enes (ASCII), tambien en
  los comentarios; el texto que ve el usuario si lleva acentos.
- La version vive solo en `src/version.h`. El CI la pasa con `-D` y de ahi
  salen el titulo, el dialogo "Acerca de..." y el nombre del release.
- `WsCampReborn.ini` guarda las opciones del menu y se relee al arrancar.

## Trampas ya encontradas

- `RETRO_ENVIRONMENT_SET_GEOMETRY` solo trae `retro_game_geometry`, no un
  `retro_system_av_info`. Leerlo como este ultimo machaca `fps` y
  `sample_rate` con basura.
- Un `Environment` que devuelve `true` a comandos desconocidos hace que el
  nucleo use punteros basura (el de log, por ejemplo) y segfaulta. Devolver
  `false` en el `default`.
- El Makefile usa tabuladores reales en las recetas. Un reemplazo de texto que
  use espacios no encaja y falla en silencio.

## CI

`.github/workflows/build.yml` se dispara en `main`, tags y `workflow_dispatch`
(no en ramas normales: para probar una rama hay que lanzarlo a mano con
`gh workflow run build --ref <rama>`). Compila el nucleo por plataforma, lo
empaqueta junto al ejecutable y ejecuta `make test` en Linux.

El release `latest` se reemplaza entero: el job borra los paquetes cuyo nombre
no lleva la version actual, porque subir con `--clobber` no elimina los
sobrantes.
