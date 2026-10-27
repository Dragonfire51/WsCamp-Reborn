#!/bin/sh
# Compila el nucleo beetle-wswan con el control por canal de sonido que usa
# WsCampReborn y deja el resultado en lib/.
#
# El nucleo original no permite silenciar canales sueltos, asi que se compila
# desde fuente con tools/patch_core_sound.py aplicado. El arbol se guarda en
# build/core/ para no recompilar lo mismo en cada pasada.
#
# Uso:
#   tools/build_core.sh [plataforma]
#
# Plataformas: linux-x86_64 (por defecto), windows-x86_64, windows-i686,
#              macos-x86_64, macos-arm64
#
# Variables:
#   CORE_SRC   arbol del nucleo ya preparado (se salta la descarga)
#   CORE_REPO  repositorio a clonar
#   CORE_REV   revision a compilar
set -eu

CORE_REPO=${CORE_REPO:-https://github.com/libretro/beetle-wswan-libretro.git}
CORE_REV=${CORE_REV:-4b01295838ea89e3f1355bbe4cb5cf98aa6108cd}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PLATFORM=${1:-linux-x86_64}
SRC=${CORE_SRC:-$ROOT/build/core}
JOBS=$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null) || echo 4)

# Nombre del fichero que espera el frontend en lib/.
case "$PLATFORM" in
    linux-x86_64)    ARTIFACT=mednafen_wswan_libretro.so ;;
    macos-x86_64|macos-arm64) ARTIFACT=mednafen_wswan_libretro.dylib ;;
    windows-x86_64|windows-i686) ARTIFACT=mednafen_wswan_libretro.dll ;;
    *) echo "plataforma desconocida: $PLATFORM" >&2; exit 2 ;;
esac

# El nucleo solo trae objetivos para su propio sistema. Windows se compila
# reutilizando la ruta de Unix con el compilador cruzado y el nombre del DLL.
case "$PLATFORM" in
    linux-x86_64)
        MAKE_ARGS="platform=unix"
        ;;
    macos-x86_64|macos-arm64)
        MAKE_ARGS="platform=osx"
        ;;
    windows-x86_64)
        MAKE_ARGS="platform=unix CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++
                   TARGET=$ARTIFACT fpic= SHARED=-shared"
        ;;
    windows-i686)
        MAKE_ARGS="platform=unix CC=i686-w64-mingw32-gcc CXX=i686-w64-mingw32-g++
                   TARGET=$ARTIFACT fpic= SHARED=-shared"
        ;;
esac

# 1) Conseguir el arbol del nucleo.
if [ ! -d "$SRC/.git" ]; then
    echo "==> Clonando el nucleo en $SRC"
    mkdir -p "$(dirname "$SRC")"
    git clone --quiet "$CORE_REPO" "$SRC"
fi

echo "==> Revision $CORE_REV"
git -C "$SRC" fetch --quiet origin || true
git -C "$SRC" checkout --quiet "$CORE_REV"
git -C "$SRC" clean -qfdx

# 2) Aplicar el control por canal de sonido.
python3 "$ROOT/tools/patch_core_sound.py" "$SRC"

# 3) Compilar.
echo "==> Compilando para $PLATFORM"
# shellcheck disable=SC2086
make -C "$SRC" -j"$JOBS" $MAKE_ARGS

if [ ! -f "$SRC/$ARTIFACT" ]; then
    echo "ERROR: no se genero $SRC/$ARTIFACT" >&2
    exit 1
fi

# 4) Instalar en lib/.
mkdir -p "$ROOT/lib"
cp "$SRC/$ARTIFACT" "$ROOT/lib/$ARTIFACT"
echo "==> Instalado: lib/$ARTIFACT"
