# WsCamp-Reborn

WonderSwan and WonderSwan Color Emulator.

Frontend libretro para el nucleo **beetle-wswan** (mednafen/wswan), con 10
ranuras de Save State / Load State.

La interfaz tiene dos backends sobre una misma capa de plataforma:

- **Windows**: interfaz nativa (Win32 + GDI). El texto usa una fuente del
  sistema, la barra de menu y los cuadros de dialogo son los del propio
  Windows; **no necesita SDL**. El juego se escala al pixel mas cercano (sin
  filtro), que es lo correcto para el pixel art del WonderSwan.
- **Linux y macOS**: interfaz SDL2, con fuente de mapa de bits integrada.

## Descargas

Los binarios se publican en la pagina de releases:

- **Latest (siempre la ultima version):**
  https://github.com/Dragonfire51/WsCamp-Reborn/releases/latest

| Plataforma | Fichero |
|------------|---------|
| Linux x86_64   | `WsCampReborn-<version>-linux-x86_64.tar.gz` |
| Windows x86_64 | `WsCampReborn-<version>-windows-x86_64.zip` |
| Windows i686   | `WsCampReborn-<version>-windows-i686.zip` |
| macOS x86_64   | `WsCampReborn-<version>-macos-x86_64.tar.gz` |
| macOS arm64    | `WsCampReborn-<version>-macos-arm64.tar.gz` |

## Estructura

```
WsCamp-Reborn/
├── src/
│   ├── main.cpp        # Frontend comun: menu, explorador, emulacion, estados
│   ├── platform.h      # Interfaz de la capa de plataforma
│   ├── platform_win32.cpp  # Backend nativo de Windows (GDI, menu, waveOut)
│   ├── platform_sdl.cpp    # Backend SDL2 (Linux y macOS)
│   ├── menu_model.cpp/.h   # Modelo de menu compartido por los dos backends
│   ├── keys.cpp/.h     # Codigos de tecla neutrales y nombres
│   ├── video.cpp/.h    # Conversion de fotogramas y escalado
│   ├── config.cpp/.h   # Configuracion persistente (WsCampReborn.ini)
│   ├── menu.h          # IDs de comandos y geometria de la interfaz
│   ├── version.h       # Fuente unica de la version (About, titulo, releases)
│   ├── font5x7.h       # Fuente de mapa de bits integrada (backend SDL)
│   ├── nfd.cpp/.h      # Dialogo nativo de apertura de ficheros
│   ├── ws_core.h       # ABI minima de libretro + clase cargadora WsCore
│   └── ws_core.cpp     # Carga dinamica portable (dlopen / LoadLibrary)
├── tests/              # Pruebas de video, de sonido y autocomprobacion
├── tools/
│   ├── build_core.sh   # Compila el nucleo (con control por canal) en lib/
│   └── patch_core_sound.py  # Anade el control por canal al nucleo
├── lib/                # Aqui va el nucleo libretro
├── .github/workflows/  # CI: compila las 5 plataformas y publica "latest"
├── Makefile
└── README.md
```

## Version

La version vive en un unico sitio: `src/version.h`
(`WSCAMP_VERSION_MAJOR` / `MINOR` / `PATCH`). De ahi se alimentan:

1. El cuadro de dialogo **Ayuda -> Acerca de...** (muestra `Version: X.Y.Z`).
2. El titulo de la ventana.
3. El nombre del release `latest` y de los ficheros publicados.

Para publicar una version nueva, edita `src/version.h` y sube a `main`; el
workflow compila y actualiza el release `latest`. Tambien se puede lanzar a
mano con `workflow_dispatch` indicando `version`.

## Requisitos

- **SDL2** de desarrollo, solo en Linux y macOS (en Windows la interfaz es
  nativa y no lo necesita):
  - Debian/Ubuntu: `sudo apt install libsdl2-dev`
  - macOS: `brew install sdl2`
- El nucleo libretro **beetle-wswan**

Para compilar para Windows hace falta MinGW-w64 (`g++-mingw-w64-x86-64` /
`g++-mingw-w64-i686`); no hace falta ninguna libreria extra, solo las de
Windows (`gdi32`, `comdlg32`, `winmm`, `user32`).

## Compilar

```sh
make                       # plataforma actual
make CROSS=x86_64          # Windows 64 bits (MinGW-w64)
make CROSS=i686            # Windows 32 bits (MinGW-w64)
make VERSION=1.2.3         # sobreescribe la version
make core                  # compila el nucleo con control por canal en lib/
make test ROM=juego.ws     # pruebas de video, escalado, rotacion, sonido y selftest
make info                  # muestra plataforma, compilador y version
make clean
```

> **Notas sobre errores de compilacion**
>
> - `missing separator` -> las lineas de receta del `Makefile` deben empezar con
>   un **tabulador real**, no con espacios. Algunos editores los convierten.
> - `undefined reference to ...basic_string<wchar_t>...` -> falta libstdc++. En
>   Windows el `Makefile` usa `-static-libstdc++ -static-libgcc`, de modo que el
>   `.exe` no depende de `libstdc++-6.dll`.

## El nucleo

El nucleo se carga en tiempo de ejecucion (`dlopen` en Linux/macOS,
`LoadLibrary` en Windows), asi que **no hace falta** ninguna libreria de
importacion al compilar. Se busca junto al ejecutable y en `lib/`:

| Sistema | Fichero |
|---------|---------|
| Linux   | `mednafen_wswan_libretro.so` |
| Windows | `mednafen_wswan_libretro.dll` |
| macOS   | `mednafen_wswan_libretro.dylib` |

El control por canal de sonido no existe en el nucleo original, asi que se
compila desde fuente con el parche aplicado:

```sh
make core                              # plataforma actual
make core CORE_PLATFORM=windows-x86_64 # destino concreto
```

`tools/build_core.sh` clona `beetle-wswan-libretro`, aplica
`tools/patch_core_sound.py` y copia el resultado a `lib/`. El arbol de trabajo
queda en `build/core/`. Si ya hay una copia del nucleo, basta con ponerla en
`lib/`: el emulador funciona igual, solo que sin el control por canal (las
opciones aparecen pero no tienen efecto).

En Windows, el paquete publicado incluye el nucleo y **ninguna DLL extra**
(la interfaz es nativa). En Linux/macOS instala SDL2 con el gestor de paquetes
y coloca el nucleo junto al ejecutable.

## Uso

- **Archivo -> Abrir ROM...** (Ctrl+O): explorador integrado de `.ws` / `.wsc`
- **Estado -> Guardar/Cargar estado 0..9**: 10 ranuras en `saves/slotNN.sts`
- **Emulacion -> Reiniciar / Pausa** (Espacio)
- **Opciones -> Video**: zoom 1x..4x, alternar horizontal/vertical (**F1**,
  como girar la consola), orientacion de la pantalla (manual, vertical u
  horizontal) y modo 60 Hz
- **Opciones -> Sonido**: silencio general y canal a canal (1..4, ruido, PCM y
  DMA); requiere el nucleo de `make core`
- **Opciones -> Controles**: remapeo de botones, remapeo de la cruceta al rotar
  (automatico, solo horizontal, solo vertical) y restauracion de los valores por
  defecto
- **Ayuda -> Acerca de...**: cuadro de dialogo nativo del sistema con la
  version del emulador
- ROM por linea de comandos: `WsCampReborn juego.ws`

Todo lo elegido en el menu se guarda en `WsCampReborn.ini`, junto al
ejecutable, y se recupera al arrancar.

### Orientacion de la pantalla

El WonderSwan emite a 75,47 Hz y dibuja 224x144. Los juegos disenados en
vertical, como **Wonder Classic** o **Digimon Digital Monsters**, esperan una
pantalla de 144x224, asi que en horizontal salen girados o recortados.

**Opciones -> Video -> Orientacion** elige que le pide el emulador al nucleo:

| Opcion | Efecto |
|--------|--------|
| manual | como el hardware: se gira al pulsar Select (valor por defecto) |
| vertical | 144x224, para juegos verticales |
| horizontal | 224x144, para el resto |

Al cambiarla, el nucleo reajusta la ventana y la imagen sola. El ajuste se
guarda en el INI, de modo que cada juego conserva su orientacion.

El modo 60 Hz hace que el nucleo baje de 75 a 60 Hz descartando uno de cada
cinco fotogramas. Sirve para pantallas que no aceptan mas de 60 Hz, a costa de
perder fluidez; no hace falta para la velocidad, que ya es correcta.

## Pruebas

```sh
make core                  # nucleo con control por canal, en lib/
make test ROM=juego.ws     # video, escalado, rotacion, sonido y autocomprobacion
```

`make test` comprueba la conversion de color, las cuatro rotaciones y el
escalado (que la imagen tambien crece al maximizar, no solo se reduce), que la
orientacion llega al nucleo (224x144 en horizontal y 144x224 en vertical), que
el remapeo de la cruceta cambia al girar, que las opciones de canal llegan al
nucleo parcheado y ejecuta una autocomprobacion completa (carga una ROM, guarda un
estado, avanza y lo vuelve a cargar). Sin `ROM=` se omiten las pruebas que
necesitan un juego.

### Velocidad

El nucleo emite a la frecuencia real del WonderSwan, 75,47 Hz, no a 60. El
bucle de emulacion cuenta el tiempo transcurrido y ejecuta los fotogramas que
tocan, en lugar de atarse a un fotograma por refresco de pantalla: asi el juego
va a velocidad real aunque el monitor sea de 60 Hz.

### Teclas

| Tecla | Boton |
|-------|-------|
| W / A / S / D | Cruceta X (arriba, izquierda, abajo, derecha) |
| Flechas | Cruceta Y |
| X | A |
| Z | B |
| Enter | Start |
| Retroceso | Select |
| F1 | Alternar horizontal / vertical, como girar la consola (configurable) |
| Ctrl+O | Abrir ROM |
| Espacio | Pausa |

Los botones se pueden reasignar en **Opciones -> Controles**; el cambio se
guarda en `WsCampReborn.ini`.

## Licencia

El nucleo beetle-wswan se distribuye bajo GPLv2; este frontend sigue la misma
licencia.
