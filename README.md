# WsCamp-Reborn

WonderSwan and WonderSwan Color Emulator.

Frontend Win32 para el nucleo libretro **beetle-wswan** (mednafen/wswan), con
10 ranuras de Save State / Load State.

## Estructura

```
WsCamp-Reborn/
├── src/
│   ├── main.cpp     # Frontend Win32: ventana, video GDI, audio waveOut, menu
│   ├── menu.h       # IDs de los comandos de menu
│   ├── ws_core.h    # ABI minima de libretro + clase cargadora WsCore
│   └── ws_core.cpp  # Implementacion del cargador dinamico (LoadLibrary)
├── lib/             # Aqui va mednafen_wswan_libretro.dll
├── Makefile
└── README.md
```

## Requisitos

- **MinGW-w64** (GCC para Windows). En MSYS2: `pacman -S mingw-w64-x86_64-gcc make`
- El nucleo `mednafen_wswan_libretro.dll` (build `beetle-wswan-libretro`)

## Compilar

```sh
mingw32-make
```

Genera `WsCampReborn.exe`.

> **Nota sobre los errores de compilacion**
>
> - `missing separator` -> las lineas de receta del `Makefile` deben empezar con
>   un **tabulador real**, no con espacios. Algunos editores los convierten.
> - `undefined reference to ...basic_string<wchar_t>..._M_replace_cold` -> falta
>   enlazar libstdc++. El `Makefile` ya usa `-static-libstdc++ -static-libgcc`
>   para enlazarlas estaticamente, de modo que el `.exe` no depende de
>   `libstdc++-6.dll` ni de `libgcc_s_seh-1.dll`.

## Instalar el nucleo

Copia `mednafen_wswan_libretro.dll` en la **misma carpeta** que el ejecutable o
en `lib\`. El programa la busca, en este orden, en:

1. `<carpeta del exe>\mednafen_wswan_libretro.dll`
2. `<carpeta del exe>\lib\mednafen_wswan_libretro.dll`

(tambien acepta `beetle_wswan_libretro.dll` y `libretro.dll`)

La DLL se carga en tiempo de ejecucion con `LoadLibrary`/`GetProcAddress`, asi
que **no hace falta** ninguna libreria de importacion ni `-lmednafen_wswan`.

## Uso

- **Archivo -> Abrir ROM...** (Ctrl+O): carga un `.ws` o `.wsc`
- **Estado -> Guardar/Cargar estado 0..9**: 10 ranuras, en `saves\slotNN.sts`
- **Emulacion -> Reiniciar / Pausa** (Espacio)
- Tambien se puede pasar una ROM por linea de comandos: `WsCampReborn.exe juego.ws`

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
