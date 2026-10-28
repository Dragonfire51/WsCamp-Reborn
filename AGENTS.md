# Notas para agentes

Frontend en C++17 para el nucleo libretro beetle-wswan (mednafen/wswan).
Emulador WonderSwan / WonderSwan Color con 10 ranuras de Save State.

La interfaz no llama a SDL ni a Win32 directamente: usa la capa de plataforma
(`src/platform.h`). En Windows manda `platform_win32.cpp`, que dibuja con GDI,
pone la barra de menu del sistema y presenta el juego con `StretchDIBits`; en
Linux y macOS manda `platform_sdl.cpp`, sobre SDL2. El resto del codigo (menu,
explorador, emulacion, configuracion) es comun.

## Compilar y probar

```sh
make                       # plataforma actual
make CROSS=x86_64          # Windows 64 bits (MinGW-w64)
make CROSS=i686            # Windows 32 bits (MinGW-w64)
make core                  # nucleo con control por canal, en lib/
make test ROM=juego.ws     # video, escalado, rotacion, sonido y autocomprobacion
```

`make test` no necesita SDL en Windows: las pruebas del nucleo y el propio
emulador usan solo la capa de plataforma.

En un contenedor Linux hacen falta `libsdl2-dev`, `g++-mingw-w64-x86-64` y
`g++-mingw-w64-i686`. En Windows **no se enlaza SDL**: se usan `gdi32`,
`comdlg32`, `winmm` y `user32`, que ya vienen con MinGW-w64. Comprobar la
arquitectura de un `.exe`: el campo `Machine` de la cabecera PE esta en el offset
`0x3C` + 4 (`0x8664` = x64, `0x14c` = i386).

Para probar el binario de Windows sin Windows: `wine WsCampReborn.exe
--selftest rom.ws` funciona sin escritorio. La interfaz grafica se puede mirar
con `Xvfb` + un gestor de ventanas (`openbox`) e `import` para capturar.

## El nucleo

`lib/` no se versiona: lo genera `tools/build_core.sh`, que clona
beetle-wswan-libretro en `build/core/` (commit fijado), aplica
`tools/patch_core_sound.py` y copia el resultado a `lib/`. El nucleo se carga
en tiempo de ejecucion (`LoadLibrary` en Windows, `dlopen` en el resto; ver
`ws_core.cpp`), nunca se enlaza.

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

## Orientacion y remapeo de la cruceta

F1 (configurable, `g_cfg.key_rotate`) alterna entre horizontal y vertical, que es
lo que hace girar la consola, igual que Cygne (el emulador Win32 de referencia).
No es un volteo de 180 grados: eso giraba la ventana entera, incluido el menu, y
no es lo que hace la consola.

La orientacion (`g_cfg.rotate`) es la opcion `wswan_rotate_display` del nucleo.
En vertical entrega 144x224 y en horizontal 224x144.

El remapeo de la cruceta lo hace el nucleo con `wswan_rotate_keymap`
(`g_cfg.keymap`): en automatico intercambia la cruceta X y la Y al girar, de
modo que "arriba" sigue siendo arriba. Ojo: el nucleo solo entiende
`auto`/`disabled`/`enabled`, asi que `KeymapName()` traduce los nombres mas
legibles del INI (`landscape`/`portrait`) a esos valores.

## Escalado del juego

La imagen llena el area de vision manteniendo su aspecto (`FitGameRect`), tanto
si hay que encogerla como si hay que agrandarla. El zoom solo decide el tamano
inicial de la ventana. Si se cambia, no volver a un esquema que solo encoja: al
maximizar, el juego se quedaba pequeno en el centro.

Al maximizar o restaurar no se fuerza el tamano de la ventana (`ApplyWindowSize`
lo respeta) y no se presenta nada con la ventana minimizada, que dejaria el area
util a cero.

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

## Interfaz nativa de Windows

El texto se dibuja con GDI y una fuente del sistema (Consolas), no con la fuente
de mapa de bits escalada a mano. Por eso al maximizar el texto crece y no se
corta, que era el problema de la version SDL2 en Windows. El backend nativo
dibuja en un mapa de bits en memoria y lo vuelca en `WM_PAINT`.

La barra de menus es la del sistema: `RebuildMenuBar()` la reconstruye antes de
cada desplegado para refrescar las marcas de verificacion, y los comandos llegan
por `WM_COMMAND` a `ExecuteCommand()`. Con menu nativo (`WsHasNativeMenu()`) el
frontend no dibuja barra propia ni reserva su altura dentro del area de cliente.

El audio usa `waveOut` con cuatro bloques; el sistema marca `WHDR_DONE` al
terminar cada uno y `WsQueueAudio` busca uno libre. Si no lo hay, descarta: es
preferible a bloquear la emulacion.

## Trampas ya encontradas

- `RETRO_ENVIRONMENT_SET_GEOMETRY` solo trae `retro_game_geometry`, no un
  `retro_system_av_info`. Leerlo como este ultimo machaca `fps` y
  `sample_rate` con basura.
- Un `Environment` que devuelve `true` a comandos desconocidos hace que el
  nucleo use punteros basura (el de log, por ejemplo) y segfaulta. Devolver
  `false` en el `default`.
- El Makefile usa tabuladores reales en las recetas. Un reemplazo de texto que
  use espacios no encaja y falla en silencio.
- El parche del nucleo anade `wscamp_get_input_state()` (y su simbolo en
  `link.T`) para que `rotation_test` compruebe el remapeo de la cruceta leyendo
  el registro de entrada real, no solo la geometria.
- El registro de entrada del WonderSwan empaqueta las dos crucetas: los cuatro
  bits bajos son la X y los cuatro siguientes la Y. Al girar, el nucleo mueve la
  pulsacion de un grupo al otro.

## CI

`.github/workflows/build.yml` se dispara en `main`, tags y `workflow_dispatch`
(no en ramas normales: para probar una rama hay que lanzarlo a mano con
`gh workflow run build --ref <rama>`). Compila el nucleo por plataforma, lo
empaqueta junto al ejecutable y ejecuta `make test` en Linux.

El release `latest` se reemplaza entero: el job borra los paquetes cuyo nombre
no lleva la version actual, porque subir con `--clobber` no elimina los
sobrantes.
