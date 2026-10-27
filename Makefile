# WsCampReborn -- Makefile para MinGW-w64
#
# IMPORTANTE: cada linea de receta debe empezar con un TABULADOR real (\t),
# nunca con espacios. Si make dice "missing separator", es que el editor ha
# convertido los tabuladores en espacios.
#
# Uso:
#   mingw32-make            -> compila WsCampReborn.exe
#   mingw32-make clean      -> borra build/ y el ejecutable
#
# Se enlazan estaticamente libstdc++ y libgcc para que el .exe no dependa de
# libstdc++-6.dll / libgcc_s_seh-1.dll. Esto evita el clasico error
# "undefined reference to ...basic_string<wchar_t>..._M_replace_cold".

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -municode -DUNICODE -D_UNICODE -Wall -Wextra -Isrc
LDFLAGS  := -mwindows -municode -static-libgcc -static-libstdc++ \
            -lcomdlg32 -lwinmm -lgdi32 -luser32

TARGET   := WsCampReborn.exe
SRCDIR   := src
OBJDIR   := build

SRCS     := $(wildcard $(SRCDIR)/*.cpp)
OBJS     := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)
