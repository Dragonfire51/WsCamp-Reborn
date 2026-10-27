// sound_test.cpp -- Comprueba que las opciones de canal de sonido llegan al
// nucleo parcheado.
//
// El nucleo original no expone ningun ajuste de canal. Con el nucleo que
// construye "make core" (tools/patch_core_sound.py) existe la opcion
// wswan_sound_chN y el nucleo la consulta a traves del callback de entorno. El
// parche guarda el resultado en el array global ws_sound_ch_enabled, que esta
// prueba lee para confirmar que el valor de la opcion llega hasta el mezclador.
//
// Con el nucleo original la prueba se marca como OMITIDA: no hay banderas que
// leer y el emulador sigue funcionando con el sonido completo.
//
// Uso: sound_test <nucleo> <rom>

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ws_core.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

// Valores que se sirven al nucleo: canal 2 y ruido desactivados.
const char *OptionValue(const char *key) {
    if (strcmp(key, "wswan_sound_ch2") == 0)   return "disabled";
    if (strcmp(key, "wswan_sound_noise") == 0) return "disabled";
    if (strncmp(key, "wswan_sound_", 12) == 0) return "enabled";
    return nullptr;
}

bool g_dirty = true;

bool Environment(unsigned cmd, void *data) {
    switch (cmd & 0xFFFFu) {
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto *var = reinterpret_cast<retro_variable *>(data);
        if (var == nullptr || var->key == nullptr)
            return false;
        const char *value = OptionValue(var->key);
        if (value == nullptr)
            return false;
        var->value = value;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *reinterpret_cast<bool *>(data) = g_dirty;
        g_dirty = false;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        *reinterpret_cast<const char **>(data) = ".";
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *reinterpret_cast<bool *>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_GET_OVERSCAN:
        *reinterpret_cast<bool *>(data) = false;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        if (data != nullptr)
            *reinterpret_cast<bool *>(data) = false;
        return false;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return true;
    default:
        return false;
    }
}

// Busca el array de banderas en la biblioteca ya cargada. Con el nucleo
// original el simbolo no existe y se devuelve nullptr.
int *FindFlags(const std::string &library) {
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(library.c_str());
    if (h == nullptr)
        return nullptr;
    return reinterpret_cast<int *>(GetProcAddress(h, "ws_sound_ch_enabled"));
#else
    void *h = dlopen(library.c_str(), RTLD_NOW);
    if (h == nullptr)
        return nullptr;
    return reinterpret_cast<int *>(dlsym(h, "ws_sound_ch_enabled"));
#endif
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "uso: sound_test <nucleo> <rom>\n");
        return 2;
    }
    const std::string core_path = argv[1];
    const std::string rom_path  = argv[2];

    if (SDL_Init(SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init fallo: %s\n", SDL_GetError());
        return 1;
    }

    WsCore core;
    if (!core.Load(core_path)) {
        fprintf(stderr, "no se pudo cargar el nucleo: %s\n",
                core.last_error().c_str());
        SDL_Quit();
        return 1;
    }

    core.retro_set_environment(Environment);
    core.retro_set_video_refresh([](const void *, unsigned, unsigned, size_t) {});
    core.retro_set_audio_sample_batch([](const int16_t *, size_t n) { return n; });
    core.retro_set_audio_sample([](int16_t, int16_t) {});
    core.retro_set_input_poll([]() {});
    core.retro_set_input_state([](unsigned, unsigned, unsigned, unsigned) {
        return (int16_t)0;
    });
    core.retro_init();

    FILE *f = fopen(rom_path.c_str(), "rb");
    if (f == nullptr) {
        fprintf(stderr, "no se pudo abrir la ROM: %s\n", rom_path.c_str());
        core.retro_deinit();
        SDL_Quit();
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom((size_t)len);
    size_t rd = fread(rom.data(), 1, rom.size(), f);
    fclose(f);
    if (rd != rom.size()) {
        fprintf(stderr, "no se pudo leer la ROM\n");
        core.retro_deinit();
        SDL_Quit();
        return 1;
    }

    retro_game_info info = {};
    info.path = rom_path.c_str();
    info.data = rom.data();
    info.size = rom.size();
    if (!core.retro_load_game(&info)) {
        fprintf(stderr, "no se pudo cargar la ROM en el nucleo\n");
        core.retro_deinit();
        SDL_Quit();
        return 1;
    }

    // El nucleo consulta las opciones en el primer retro_run.
    core.retro_run();

    int *flags = FindFlags(core_path);
    if (flags == nullptr) {
        printf("SOUNDTEST: OMITIDO (el nucleo no tiene control por canal; "
               "compila con 'make core' para probarlo)\n");
        core.retro_deinit();
        SDL_Quit();
        return 0;
    }

    printf("banderas: ch1=%d ch2=%d ch3=%d ch4=%d noise=%d pcm=%d dma=%d\n",
           flags[0], flags[1], flags[2], flags[3], flags[4], flags[5], flags[6]);

    bool ok = flags[0] == 1 && flags[1] == 0 && flags[2] == 1 && flags[3] == 1 &&
              flags[4] == 0 && flags[5] == 1 && flags[6] == 1;

    core.retro_deinit();
    SDL_Quit();

    printf(ok ? "SOUNDTEST: OK\n" : "SOUNDTEST: FALLO\n");
    return ok ? 0 : 1;
}
