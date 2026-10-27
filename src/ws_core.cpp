// ws_core.cpp -- Implementacion del cargador dinamico del nucleo libretro.
#include "ws_core.h"

namespace {

// Solo se usan nombres de simbolos ASCII, asi que una conversion directa basta.
std::wstring Widen(const char *s) {
    std::wstring w;
    for (; *s != '\0'; ++s)
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}

} // namespace

bool WsCore::Load(const std::wstring &dll_path) {
    Unload();
    last_error_.clear();

    module_ = ::LoadLibraryW(dll_path.c_str());
    if (module_ == nullptr) {
        last_error_ = L"No se pudo cargar la DLL: " + dll_path;
        return false;
    }

    // GetProcAddress devuelve FARPROC; convertir a un puntero de funcion
    // tipado siempre dispara -Wcast-function-type, pero es el patron estandar
    // de carga dinamica, asi que se silencia solo aqui.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"

    // Resuelve un simbolo y aborta con un mensaje claro si no existe.
#define WS_BIND(fn)                                                        \
    do {                                                                   \
        FARPROC proc = ::GetProcAddress(module_, #fn);                     \
        if (proc == nullptr) {                                             \
            last_error_ = L"Simbolo no exportado: " + Widen(#fn);          \
            Unload();                                                      \
            return false;                                                  \
        }                                                                  \
        fn = reinterpret_cast<decltype(fn)>(proc);                         \
    } while (false)

    WS_BIND(retro_init);
    WS_BIND(retro_deinit);
    WS_BIND(retro_api_version);
    WS_BIND(retro_get_system_info);
    WS_BIND(retro_get_system_av_info);
    WS_BIND(retro_set_controller_port_device);
    WS_BIND(retro_reset);
    WS_BIND(retro_run);
    WS_BIND(retro_serialize_size);
    WS_BIND(retro_serialize);
    WS_BIND(retro_unserialize);
    WS_BIND(retro_load_game);
    WS_BIND(retro_unload_game);
    WS_BIND(retro_get_region);
    WS_BIND(retro_get_memory_data);
    WS_BIND(retro_get_memory_size);
    WS_BIND(retro_set_environment);
    WS_BIND(retro_set_video_refresh);
    WS_BIND(retro_set_audio_sample);
    WS_BIND(retro_set_audio_sample_batch);
    WS_BIND(retro_set_input_poll);
    WS_BIND(retro_set_input_state);

#undef WS_BIND

#pragma GCC diagnostic pop

    return true;
}

void WsCore::Unload() {
    if (module_ != nullptr) {
        ::FreeLibrary(module_);
        module_ = nullptr;
    }

    retro_init                       = nullptr;
    retro_deinit                     = nullptr;
    retro_api_version                = nullptr;
    retro_get_system_info            = nullptr;
    retro_get_system_av_info         = nullptr;
    retro_set_controller_port_device = nullptr;
    retro_reset                      = nullptr;
    retro_run                        = nullptr;
    retro_serialize_size             = nullptr;
    retro_serialize                  = nullptr;
    retro_unserialize                = nullptr;
    retro_load_game                  = nullptr;
    retro_unload_game                = nullptr;
    retro_get_region                 = nullptr;
    retro_get_memory_data            = nullptr;
    retro_get_memory_size            = nullptr;
    retro_set_environment            = nullptr;
    retro_set_video_refresh          = nullptr;
    retro_set_audio_sample           = nullptr;
    retro_set_audio_sample_batch     = nullptr;
    retro_set_input_poll             = nullptr;
    retro_set_input_state            = nullptr;
}
