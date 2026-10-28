// rotation_test.cpp -- Comprueba que la orientacion llega al nucleo y que la
// cruceta se remapea al girar la pantalla.
//
// Los juegos verticales (Wonder Classic, Digimon) se disenan para 144x224.
// El frontend sirve wswan_rotate_display y wswan_rotate_keymap; el nucleo
// responde cambiando la geometria y, en modo automatico, intercambiando la
// cruceta X y la Y.
//
// La prueba no se fia solo de la geometria: carga el nucleo de verdad, pulsa
// el pad por el mismo camino que el frontend (retro_set_input_state) y lee el
// registro de botones que el nucleo entrega al emulador. Asi se comprueba lo
// que le importa al usuario: que al girar la consola los controles siguen
// haciendo lo mismo.
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
std::string g_keymap_opt   = "auto";
bool        g_hw_rotate    = false;
bool        g_rot_seen     = false;

// El registro de entrada del WonderSwan empaqueta las dos crucetas y los
// botones. Los cuatro bits bajos son la cruceta X y los cuatro siguientes la
// Y (ver update_input() en el nucleo): al girar la consola, el nucleo mueve la
// pulsacion de un grupo al otro.
const unsigned short BITS_X_CURSOR = 0x0Fu;   // bits 0..3
const unsigned short BITS_Y_CURSOR = 0xF0u;   // bits 4..7

const char *OptionValue(const char *key) {
    if (strcmp(key, "wswan_rotate_display") == 0) return g_rotation_opt.c_str();
    if (strcmp(key, "wswan_rotate_keymap") == 0)  return g_keymap_opt.c_str();
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

// Mando pulsado: solo "arriba". El nucleo lo lleva de la cruceta X a la Y al
// girar la consola, que es justo lo que hay que comprobar.
int16_t InputState(unsigned, unsigned device, unsigned, unsigned id) {
    if (device != RETRO_DEVICE_JOYPAD)
        return 0;
    return (id == RETRO_DEVICE_ID_JOYPAD_UP) ? 1 : 0;
}

typedef void (*GetInputStateFn)(unsigned short *, int *);

struct Probe {
    unsigned       w = 0, h = 0;
    unsigned short input_buf = 0;
    int            rotate_tall = 0;
};

bool ProbeCore(const std::string &core_path, const std::string &rom_path,
               Probe &out) {
    WsCore core;
    if (!core.Load(core_path))
        return false;

    core.retro_set_environment(Environment);
    core.retro_set_video_refresh([](const void *, unsigned, unsigned, size_t) {});
    core.retro_set_audio_sample_batch([](const int16_t *, size_t n) { return n; });
    core.retro_set_audio_sample([](int16_t, int16_t) {});
    core.retro_set_input_poll([]() {});
    core.retro_set_input_state(InputState);
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

    // La orientacion se consulta en el primer retro_run; unos cuantos mas
    // dejan el pad asentado.
    for (int i = 0; i < 4; ++i)
        core.retro_run();

    struct retro_system_av_info av = {};
    core.retro_get_system_av_info(&av);
    out.w = av.geometry.base_width;
    out.h = av.geometry.base_height;

    // La introspeccion es opcional: sin ella aun se puede comprobar la
    // geometria, asi que no se falla si el nucleo no la exporta.
    void *sym = core.Symbol("wscamp_get_input_state");
    if (sym != nullptr) {
        auto fn = reinterpret_cast<GetInputStateFn>(sym);
        unsigned short buf = 0;
        int tall = 0;
        fn(&buf, &tall);
        out.input_buf   = buf;
        out.rotate_tall = tall;
    }

    core.retro_deinit();
    return true;
}

bool LoadOk(const std::string &core_path, const std::string &rom_path, Probe &p) {
    if (!ProbeCore(core_path, rom_path, p)) {
        fprintf(stderr, "no se pudo cargar el nucleo o la ROM\n");
        return false;
    }
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

    bool  ok = true;
    Probe p;

    // --- Geometria segun la orientacion pedida ---
    g_hw_rotate    = false;   // el nucleo rota en software
    g_keymap_opt   = "auto";
    g_rotation_opt = "landscape";
    if (!LoadOk(core_path, rom_path, p)) { SDL_Quit(); return 1; }
    printf("horizontal: %ux%u (se espera 224x144)\n", p.w, p.h);
    ok &= (p.w == 224 && p.h == 144);

    g_rotation_opt = "portrait";
    if (!LoadOk(core_path, rom_path, p)) { SDL_Quit(); return 1; }
    printf("vertical  : %ux%u (se espera 144x224)\n", p.w, p.h);
    ok &= (p.w == 144 && p.h == 224);

    // --- Remapeo de la cruceta con wswan_rotate_keymap=auto ---
    // En horizontal manda la cruceta X; al girar a vertical el nucleo pone la
    // Y en su lugar, para que "arriba" siga siendo arriba.
    g_keymap_opt   = "auto";
    g_rotation_opt = "landscape";
    if (!LoadOk(core_path, rom_path, p)) { SDL_Quit(); return 1; }
    const unsigned short auto_landscape = p.input_buf;

    g_keymap_opt   = "auto";
    g_rotation_opt = "portrait";
    if (!LoadOk(core_path, rom_path, p)) { SDL_Quit(); return 1; }
    const unsigned short auto_portrait = p.input_buf;

    printf("auto: horizontal=0x%04x vertical=0x%04x (deben cambiar)\n",
           auto_landscape, auto_portrait);
    // En horizontal la pulsacion cae en la cruceta X; al girar, el nucleo la
    // lleva a la cruceta Y, que es la que manda en vertical.
    ok &= (auto_landscape != auto_portrait);
    ok &= ((auto_landscape & BITS_X_CURSOR) != 0);
    ok &= ((auto_landscape & BITS_Y_CURSOR) == 0);
    ok &= ((auto_portrait  & BITS_Y_CURSOR) != 0);
    ok &= ((auto_portrait  & BITS_X_CURSOR) == 0);

    // --- Con el remapeo fijado, el registro no cambia al girar ---
    g_keymap_opt   = "disabled";
    g_rotation_opt = "landscape";
    if (!LoadOk(core_path, rom_path, p)) { SDL_Quit(); return 1; }
    const unsigned short fixed_landscape = p.input_buf;

    g_keymap_opt   = "disabled";
    g_rotation_opt = "portrait";
    if (!LoadOk(core_path, rom_path, p)) { SDL_Quit(); return 1; }
    const unsigned short fixed_portrait = p.input_buf;

    printf("fijo: horizontal=0x%04x vertical=0x%04x (deben coincidir)\n",
           fixed_landscape, fixed_portrait);
    // Con el remapeo fijado, girar no cambia la cruceta.
    ok &= (fixed_landscape == fixed_portrait);
    ok &= (fixed_landscape == auto_landscape);

    // El nucleo solo pide SET_ROTATION si el frontend dice que puede rotar por
    // hardware; WsCampReborn lo rechaza para que rote en software y entregue el
    // fotograma ya en su sitio.
    g_hw_rotate = true;
    g_rot_seen  = false;
    g_rotation_opt = "portrait";
    if (!LoadOk(core_path, rom_path, p)) { SDL_Quit(); return 1; }
    printf("SET_ROTATION pedido: %s\n", g_rot_seen ? "si" : "no");
    ok &= g_rot_seen;

    SDL_Quit();
    printf(ok ? "ROTATIONTEST: OK\n" : "ROTATIONTEST: FALLO\n");
    return ok ? 0 : 1;
}
