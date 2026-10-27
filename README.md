# WsCamp-Reborn

WonderSwan and WonderSwan Color Emulator.

Frontend libretro sobre **SDL2** para el nucleo **beetle-wswan**
(mednafen/wswan), con 10 ranuras de Save State / Load State.

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
│   ├── main.cpp        # Frontend SDL2: ventana, audio, menu, explorador
│   ├── video.cpp/.h    # Conversion de fotogramas y rotacion
│   ├── config.cpp/.h   # Configuracion persistente (WsCampReborn.ini)
│   ├── menu.h          # IDs de comandos y geometria de la interfaz
│   ├── version.h       # Fuente unica de la version (About, titulo, releases)
│   ├── font5x7.h       # Fuente de mapa de bits integrada (sin dependencias)
│   ├── nfd.cpp/.h      # Dialogo nativo de apertura de ficheros
│   ├── ws_core.h       # ABI minima de libretro + clase cargadora WsCore
│   └── ws_core.cpp     # Carga dinamica portable (SDL_LoadObject)
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

- **SDL2** de desarrollo:
  - Debian/Ubuntu: `sudo apt install libsdl2-dev`
  - macOS: `brew install sdl2`
  - MSYS2: `pacman -S mingw-w64-x86_64-SDL2`
- El nucleo libretro **beetle-wswan**

## Compilar

```sh
make                       # plataforma actual
make CROSS=x86_64          # Windows 64 bits (MinGW-w64)
make CROSS=i686            # Windows 32 bits (MinGW-w64)
make VERSION=1.2.3         # sobreescribe la version
make core                  # compila el nucleo con control por canal en lib/
make test ROM=juego.ws     # pruebas de video, de sonido y autocomprobacion
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

El nucleo se carga en tiempo de ejecucion (`SDL_LoadObject` -> `dlopen` /
`LoadLibrary`), asi que **no hace falta** ninguna libreria de importacion al
compilar. Se busca junto al ejecutable y en `lib/`:

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

En Windows, el paquete publicado ya incluye `SDL2.dll` y el nucleo; en
Linux/macOS instala SDL2 con el gestor de paquetes.

## Uso

- **Archivo -> Abrir ROM...** (Ctrl+O): explorador integrado de `.ws` / `.wsc`
- **Estado -> Guardar/Cargar estado 0..9**: 10 ranuras en `saves/slotNN.sts`
- **Emulacion -> Reiniciar / Pausa** (Espacio)
- **Opciones -> Video**: zoom 1x..4x y volteo de imagen (F1)
- **Opciones -> Sonido**: silencio general y canal a canal (1..4, ruido, PCM y
  DMA); requiere el nucleo de `make core`
- **Opciones -> Controles**: remapeo de botones y restauracion de los valores
  por defecto
- **Ayuda -> Acerca de...**: version del emulador
- ROM por linea de comandos: `WsCampReborn juego.ws`

Todo lo elegido en el menu se guarda en `WsCampReborn.ini`, junto al
ejecutable, y se recupera al arrancar.

## Pruebas

```sh
make core                  # nucleo con control por canal, en lib/
make test ROM=juego.ws     # video + control por canal + autocomprobacion
```

`make test` comprueba la conversion de color y las cuatro rotaciones, verifica
que las opciones de canal llegan al nucleo parcheado y ejecuta una
autocomprobacion completa (carga una ROM, guarda un estado, avanza y lo vuelve a
cargar). Sin `ROM=` se omiten las pruebas que necesitan un juego.

### Teclas

| Tecla | Boton |
|-------|-------|
| W / A / S / D | Cruceta X (arriba, izquierda, abajo, derecha) |
| Flechas | Cruceta Y |
| X | A |
| Z | B |
| Enter | Start |
| Retroceso | Select |
| F1 | Voltear imagen (configurable) |
| Ctrl+O | Abrir ROM |
| Espacio | Pausa |

Los botones se pueden reasignar en **Opciones -> Controles**; el cambio se
guarda en `WsCampReborn.ini`.

## Licencia

El nucleo beetle-wswan se distribuye bajo GPLv2; este frontend sigue la misma
licencia.
