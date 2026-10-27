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
│   ├── main.cpp        # Frontend SDL2: ventana, video, audio, menu, explorador
│   ├── menu.h          # IDs de comandos y geometria de la interfaz
│   ├── version.h       # Fuente unica de la version (About, titulo, releases)
│   ├── font5x7.h       # Fuente de mapa de bits integrada (sin dependencias)
│   ├── ws_core.h       # ABI minima de libretro + clase cargadora WsCore
│   └── ws_core.cpp     # Carga dinamica portable (SDL_LoadObject)
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

## Instalar el nucleo

Copia el nucleo junto al ejecutable o en `lib/`:

| Sistema | Fichero |
|---------|---------|
| Linux   | `mednafen_wswan_libretro.so` |
| Windows | `mednafen_wswan_libretro.dll` |
| macOS   | `mednafen_wswan_libretro.dylib` |

Se carga en tiempo de ejecucion (`SDL_LoadObject` -> `dlopen` / `LoadLibrary`),
asi que **no hace falta** ninguna libreria de importacion al compilar.

En Windows, el paquete publicado ya incluye `SDL2.dll`; en Linux/macOS instala
SDL2 con el gestor de paquetes.

## Uso

- **Archivo -> Abrir ROM...** (Ctrl+O): explorador integrado de `.ws` / `.wsc`
- **Estado -> Guardar/Cargar estado 0..9**: 10 ranuras en `saves/slotNN.sts`
- **Emulacion -> Reiniciar / Pausa** (Espacio)
- **Ayuda -> Acerca de...**: version del emulador
- ROM por linea de comandos: `WsCampReborn juego.ws`

### Teclas

| Tecla | Boton WonderSwan |
|-------|------------------|
| Flechas | Cruceta |
| X | A |
| Z | B |
| S | Y |
| A | X |
| Q / W | L / R |
| Enter | Start |
| Retroceso | Select |

## Licencia

El nucleo beetle-wswan se distribuye bajo GPLv2; este frontend sigue la misma
licencia.
