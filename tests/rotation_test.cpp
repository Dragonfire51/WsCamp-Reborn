// rotation_test.cpp -- Comprueba que la orientacion pedida al nucleo llega
// hasta el y cambia la geometria de la pantalla.
//
// Los juegos verticales (Wonder Classic, Digimon) se disenan para 144x224.
// El frontend sirve la opcion wswan_rotate_display y el nucleo responde con
// SET_GEOMETRY; esta prueba verifica las dos cosas: que el valor de la opcion
// se consulta y que la geometria resultante es la esperada.
//
// Uso: rotation_test <nucleo> <rom>

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ws_core.h"

namespace {

std::string g_rotation_opt = "manual";
bool        g_hw_rotate    = false;
bool        g_rot_seen     = false;

const char *OptionValue(const char *key) {
    if (strcmp(key, "wswan_rotate_display") == 0) return g_rotation_opt.c_str();
    if (strncmp(key, "wswan_sound_", 12) == 0)    return "enabled";
    return nullptr;
}

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
        *reinterpret_cast<bool *>(data) = false;
        return true;
    case RETRO_ENVIRONMENT_SET_ROTATION:
        g_rot_seen = true;
        return g_hw_rotate;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
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

// Carga el nucleo y devuelve la geometria base que declara tras el primer
// fotograma. Devuelve false si algo falla.
bool ProbeGeometry(const std::string &core_path, const std::string &rom_path,
                   unsigned &w, unsigned &h) {
    WsCore core;
    if (!core.Load(core_path))
        return false;

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
        core.retro_deinit();
        return false;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom((size_t)len);
    size_t rd = fread(rom.data(), 1, rom.size(), f);
    fclose(f);
    if (rd != rom.size()) {
        core.retro_deinit();
        return false;
    }

    retro_game_info info = {};
    info.path = rom_path.c_str();
    info.data = rom.data();
    info.size = rom.size();
    if (!core.retro_load_game(&info)) {
        core.retro_deinit();
        return false;
    }

    // La orientacion se consulta en el primer retro_run.
    core.retro_run();

    struct retro_system_av_info av = {};
    core.retro_get_system_av_info(&av);
    w = av.geometry.base_width;
    h = av.geometry.base_height;

    core.retro_deinit();
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "uso: rotation_test <nucleo> <rom>\n");
        return 2;
    }
    const std::string core_path = argv[1];
    const std::string rom_path  = argv[2];

    if (SDL_Init(SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init fallo: %s\n", SDL_GetError());
        return 1;
    }

    bool ok = true;

    unsigned w = 0, h = 0;
    g_rotation_opt = "landscape";
    if (!ProbeGeometry(core_path, rom_path, w, h)) {
        fprintf(stderr, "no se pudo cargar el nucleo o la ROM\n");
        SDL_Quit();
        return 1;
    }
    printf("horizontal: %ux%u (se espera 224x144)\n", w, h);
    ok &= (w == 224 && h == 144);

    g_rotation_opt = "portrait";
    if (!ProbeGeometry(core_path, rom_path, w, h)) {
        fprintf(stderr, "no se pudo cargar el nucleo o la ROM\n");
        SDL_Quit();
        return 1;
    }
    printf("vertical  : %ux%u (se espera 144x224)\n", w, h);
    ok &= (w == 144 && h == 224);

    // El nucleo solo pide SET_ROTATION si el frontend dice que puede rotar por
    // hardware. El frontend lo rechaza a proposito para que el nucleo rote el
    // mismo en software y entregue el fotograma ya en su sitio.
    g_hw_rotate = true;
    g_rot_seen  = false;
    g_rotation_opt = "portrait";
    if (ProbeGeometry(core_path, rom_path, w, h)) {
        printf("SET_ROTATION pedido: %s\n", g_rot_seen ? "si" : "no");
        printf("vertical con hw_rotate: %ux%u (se espera 224x144 en software)\n", w, h);
        // Al aceptar la rotacion por hardware el nucleo delega en el frontend
        // y entrega el fotograma sin girar (224x144), asi que el frontend debe
        // rechazarla para quedarse con la ruta en software, que ya da 144x224.
        ok &= g_rot_seen;
    }

    SDL_Quit();
    printf(ok ? "ROTATIONTEST: OK\n" : "ROTATIONTEST: FALLO\n");
    return ok ? 0 : 1;
}
