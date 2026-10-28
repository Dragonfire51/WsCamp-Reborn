# WsCampReborn -- Makefile multiplataforma (Linux, macOS, Windows x86/x64)
#
# IMPORTANTE: cada linea de receta empieza con un TABULADOR real, nunca con
# espacios. Si make dice "missing separator", el editor ha convertido los tabs.
#
# Uso:
#   make                     -> compila para la plataforma actual
#   make CROSS=x86_64        -> Windows 64 bits (MinGW-w64)
#   make CROSS=i686          -> Windows 32 bits (MinGW-w64)
#   make VERSION=1.2.3       -> sobreescribe la version del binario
#   make core                -> compila el nucleo (con control por canal) en lib/
#   make test                -> ejecuta las pruebas de video y la autocomprobacion
#   make clean
#
# Requiere SDL2 (dev). La version se define en src/version.h; VERSION la
# sobreescribe sin tocar el codigo.

TARGET_NAME := WsCampReborn
SRCDIR      := src
OBJDIR      := build

# --- Version -----------------------------------------------------------------
# src/version.h es la fuente unica de verdad. Si se pasa VERSION=1.2.3 se usa
# ese valor en lugar del fichero.
ifneq ($(origin VERSION),undefined)
  VMAJ := $(word 1,$(subst ., ,$(VERSION)))
  VMIN := $(word 2,$(subst ., ,$(VERSION)))
  VPAT := $(word 3,$(subst ., ,$(VERSION)))
  VFLAGS := -DWSCAMP_VERSION_MAJOR=$(VMAJ) -DWSCAMP_VERSION_MINOR=$(VMIN) \
            -DWSCAMP_VERSION_PATCH=$(if $(VPAT),$(VPAT),0)
else
  VERSION := $(shell sed -n 's/^#define WSCAMP_VERSION_MAJOR \([0-9]*\)/\1/p' src/version.h | head -1).$(shell sed -n 's/^#define WSCAMP_VERSION_MINOR \([0-9]*\)/\1/p' src/version.h | head -1).$(shell sed -n 's/^#define WSCAMP_VERSION_PATCH \([0-9]*\)/\1/p' src/version.h | head -1)
  VFLAGS :=
endif

# --- Toolchain ---------------------------------------------------------------
CXXFLAGS_COMMON := -std=c++17 -O2 -Wall -Wextra -Isrc $(VFLAGS)

ifeq ($(CROSS),x86_64)
  CXX      := x86_64-w64-mingw32-g++
  PLATFORM := windows-x86_64
  EXT      := .exe
  PLATFLAGS := -DUNICODE -D_UNICODE
  LDLIBS   := -mwindows -lgdi32 -lcomdlg32 -lwinmm -luser32 \
              -static-libgcc -static-libstdc++
else ifeq ($(CROSS),i686)
  CXX      := i686-w64-mingw32-g++
  PLATFORM := windows-i686
  EXT      := .exe
  PLATFLAGS := -DUNICODE -D_UNICODE
  LDLIBS   := -mwindows -lgdi32 -lcomdlg32 -lwinmm -luser32 \
              -static-libgcc -static-libstdc++
else
  CXX      := c++
  EXT      :=
  PLATFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
  LDLIBS    := $(shell pkg-config --libs sdl2 2>/dev/null)
  UNAME_S  := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos-$(shell uname -m)
  else
    PLATFORM := linux-$(shell uname -m)
  endif
endif

# Extension del nucleo para la plataforma en curso (la usan las pruebas).
ifneq (,$(filter windows%,$(PLATFORM)))
  CORE_EXT := .dll
else ifneq (,$(filter macos%,$(PLATFORM)))
  CORE_EXT := .dylib
else
  CORE_EXT := .so
endif

TARGET := $(TARGET_NAME)$(EXT)

# La capa de plataforma tiene dos implementaciones. En Windows manda la nativa
# (GDI + menu del sistema) y no se compila nada de SDL; en el resto, SDL2.
SRCS   := $(wildcard $(SRCDIR)/*.cpp)
ifneq (,$(filter windows%,$(PLATFORM)))
  SRCS := $(filter-out $(SRCDIR)/platform_sdl.cpp,$(SRCS))
else
  SRCS := $(filter-out $(SRCDIR)/platform_win32.cpp,$(SRCS))
endif
OBJS   := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all clean info core test

all: $(TARGET)

info:
	@echo "PLATAFORMA: $(PLATFORM)"
	@echo "COMPILADOR: $(CXX)"
	@echo "VERSION   : $(VERSION)"
	@echo "SALIDA    : $(TARGET)"

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS_COMMON) $(PLATFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Compila el nucleo beetle-wswan con el control por canal de sonido aplicado
# y lo deja en lib/. La plataforma de destino se pasa como argumento.
core:
	sh tools/build_core.sh $(CORE_PLATFORM)

# Pruebas: conversion/rotacion de video y autocomprobacion con el nucleo
# real. Requiere el nucleo en lib/ (make core) y una ROM en ROM=<ruta>.
test: $(TARGET)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Isrc $(PLATFLAGS) \
		tests/video_test.cpp $(SRCDIR)/video.cpp -o $(OBJDIR)/video_test $(LDLIBS)
	$(OBJDIR)/video_test
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Isrc $(PLATFLAGS) \
		tests/sound_test.cpp $(SRCDIR)/ws_core.cpp -o $(OBJDIR)/sound_test $(LDLIBS)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Isrc $(PLATFLAGS) \
		tests/rotation_test.cpp $(SRCDIR)/ws_core.cpp -o $(OBJDIR)/rotation_test $(LDLIBS)
	@CORE=$$(ls lib/mednafen_wswan_libretro$(CORE_EXT) 2>/dev/null | head -1); \
	if [ -z "$$CORE" ]; then \
		echo "test: sin nucleo en lib/, se omiten sound_test y rotation_test"; \
	elif [ -n "$(ROM)" ]; then \
		$(OBJDIR)/sound_test "$$CORE" "$(ROM)"; \
		$(OBJDIR)/rotation_test "$$CORE" "$(ROM)"; \
	else \
		echo "test: sin ROM=<ruta>, se omiten sound_test y rotation_test"; \
	fi
	@if [ -n "$(ROM)" ]; then \
		./$(TARGET) --selftest "$(ROM)"; \
	else \
		echo "test: sin ROM=<ruta>, se omite la autocomprobacion"; \
	fi

clean:
	rm -rf $(OBJDIR) $(TARGET)
