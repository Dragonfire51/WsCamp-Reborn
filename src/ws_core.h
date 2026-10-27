// ws_core.h -- Declaraciones y cargador dinamico del nucleo libretro
// (beetle-wswan / mednafen_wswan_libretro.dll)
//
// IMPORTANTE: aqui NO se usa __declspec(dllimport) ni ninguna biblioteca de
// importacion. Todas las funciones del nucleo se resuelven en tiempo de
// ejecucion con GetProcAddress, por lo que el enlazador solo necesita las
// librerias de Windows (user32, gdi32, comdlg32, winmm).
#pragma once

#include <windows.h>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// ABI minima de libretro
// ---------------------------------------------------------------------------
#define RETRO_API_VERSION 1

enum retro_pixel_format {
    RETRO_PIXEL_FORMAT_0RGB1555 = 0, // formato por defecto si el nucleo no lo cambia
    RETRO_PIXEL_FORMAT_XRGB8888 = 1,
    RETRO_PIXEL_FORMAT_RGB565   = 2
};

enum retro_log_level {
    RETRO_LOG_DEBUG = 0,
    RETRO_LOG_INFO,
    RETRO_LOG_WARN,
    RETRO_LOG_ERROR,
    RETRO_LOG_DUMMY = INT_MAX
};

typedef void (*retro_log_printf_t)(enum retro_log_level level, const char *fmt, ...);
struct retro_log_callback { retro_log_printf_t log; };

struct retro_variable { const char *key; const char *value; };

struct retro_game_info {
    const char *path;
    const void *data;
    size_t      size;
    const char *meta;
};

struct retro_game_geometry {
    unsigned base_width;
    unsigned base_height;
    unsigned max_width;
    unsigned max_height;
    float    aspect_ratio;
};

struct retro_system_timing { double fps; double sample_rate; };

struct retro_system_av_info {
    struct retro_game_geometry geometry;
    struct retro_system_timing timing;
};

struct retro_system_info {
    const char *library_name;
    const char *library_version;
    const char *valid_extensions;
    bool        need_fullpath;
    bool        block_extract;
};

typedef bool    (*retro_environment_t)(unsigned cmd, void *data);
typedef void    (*retro_video_refresh_t)(const void *data, unsigned width, unsigned height, size_t pitch);
typedef void    (*retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t  (*retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void    (*retro_input_poll_t)(void);
typedef int16_t (*retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);

// Los comandos de entorno pueden llegar con el bit EXPERIMENTAL (0x10000)
// activado; hay que enmascarar con 0xFFFF antes de comparar.
#define RETRO_ENVIRONMENT_EXPERIMENTAL 0x10000u

enum {
    RETRO_ENVIRONMENT_SET_ROTATION                     = 1,
    RETRO_ENVIRONMENT_GET_OVERSCAN                     = 2,
    RETRO_ENVIRONMENT_GET_CAN_DUPE                     = 3,
    RETRO_ENVIRONMENT_SET_MESSAGE                      = 6,
    RETRO_ENVIRONMENT_SHUTDOWN                         = 7,
    RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL            = 8,
    RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY             = 9,
    RETRO_ENVIRONMENT_SET_PIXEL_FORMAT                 = 10,
    RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS            = 11,
    RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK            = 12,
    RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE       = 13,
    RETRO_ENVIRONMENT_SET_HW_RENDER                    = 14,
    RETRO_ENVIRONMENT_GET_VARIABLE                     = 15,
    RETRO_ENVIRONMENT_SET_VARIABLES                    = 16,
    RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE              = 17,
    RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME              = 18,
    RETRO_ENVIRONMENT_GET_LIBRETRO_PATH                = 19,
    RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK          = 21,
    RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK               = 22,
    RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE             = 23,
    RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES    = 24,
    RETRO_ENVIRONMENT_GET_LOG_INTERFACE                = 27,
    RETRO_ENVIRONMENT_GET_PERF_INTERFACE               = 28,
    RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY        = 30,
    RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY               = 31,
    RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO               = 32,
    RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO               = 34,
    RETRO_ENVIRONMENT_SET_CONTROLLER_INFO              = 35,
    RETRO_ENVIRONMENT_SET_MEMORY_MAPS                  = 36,
    RETRO_ENVIRONMENT_SET_GEOMETRY                     = 37,
    RETRO_ENVIRONMENT_GET_USERNAME                     = 38,
    RETRO_ENVIRONMENT_GET_LANGUAGE                     = 39,
    RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER = 40,
    RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS         = 42,
    RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS         = 44,
    RETRO_ENVIRONMENT_GET_VFS_INTERFACE                = 45,
    RETRO_ENVIRONMENT_GET_LED_INTERFACE                = 46,
    RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE           = 47,
    RETRO_ENVIRONMENT_GET_FASTFORWARDING               = 49,
    RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE          = 50,
    RETRO_ENVIRONMENT_GET_INPUT_BITMASKS               = 51,
    RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION         = 52,
    RETRO_ENVIRONMENT_SET_CORE_OPTIONS                 = 53,
    RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL            = 54,
    RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY         = 55,
    RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2              = 67,
    RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL         = 68
};

// Dispositivos y botones
enum {
    RETRO_DEVICE_NONE     = 0,
    RETRO_DEVICE_JOYPAD   = 1,
    RETRO_DEVICE_MOUSE    = 2,
    RETRO_DEVICE_KEYBOARD = 3,
    RETRO_DEVICE_LIGHTGUN = 4,
    RETRO_DEVICE_ANALOG   = 5,
    RETRO_DEVICE_POINTER  = 6
};

enum {
    RETRO_DEVICE_ID_JOYPAD_B      = 0,
    RETRO_DEVICE_ID_JOYPAD_Y      = 1,
    RETRO_DEVICE_ID_JOYPAD_SELECT = 2,
    RETRO_DEVICE_ID_JOYPAD_START  = 3,
    RETRO_DEVICE_ID_JOYPAD_UP     = 4,
    RETRO_DEVICE_ID_JOYPAD_DOWN   = 5,
    RETRO_DEVICE_ID_JOYPAD_LEFT   = 6,
    RETRO_DEVICE_ID_JOYPAD_RIGHT  = 7,
    RETRO_DEVICE_ID_JOYPAD_A      = 8,
    RETRO_DEVICE_ID_JOYPAD_X      = 9,
    RETRO_DEVICE_ID_JOYPAD_L      = 10,
    RETRO_DEVICE_ID_JOYPAD_R      = 11,
    RETRO_DEVICE_ID_JOYPAD_L2     = 12,
    RETRO_DEVICE_ID_JOYPAD_R2     = 13,
    RETRO_DEVICE_ID_JOYPAD_L3     = 14,
    RETRO_DEVICE_ID_JOYPAD_R3     = 15,
    RETRO_DEVICE_ID_JOYPAD_MASK   = 256
};

enum {
    RETRO_MEMORY_SAVE_RAM   = 0,
    RETRO_MEMORY_RTC        = 1,
    RETRO_MEMORY_SYSTEM_RAM = 2,
    RETRO_MEMORY_VIDEO_RAM  = 3
};

// ---------------------------------------------------------------------------
// Cargador del nucleo
// ---------------------------------------------------------------------------
class WsCore {
public:
    // Carga la DLL y resuelve todos los simbolos. Si falta alguno, devuelve
    // false y deja su nombre en last_error().
    bool Load(const std::wstring &dll_path);
    void Unload();
    bool IsLoaded() const { return module_ != nullptr; }

    const std::wstring &last_error() const { return last_error_; }

    // --- funciones del nucleo ---
    void     (*retro_init)(void)                                        = nullptr;
    void     (*retro_deinit)(void)                                      = nullptr;
    unsigned (*retro_api_version)(void)                                 = nullptr;
    void     (*retro_get_system_info)(struct retro_system_info *)       = nullptr;
    void     (*retro_get_system_av_info)(struct retro_system_av_info *) = nullptr;
    void     (*retro_set_controller_port_device)(unsigned, unsigned)    = nullptr;
    void     (*retro_reset)(void)                                       = nullptr;
    void     (*retro_run)(void)                                         = nullptr;
    size_t   (*retro_serialize_size)(void)                              = nullptr;
    bool     (*retro_serialize)(void *, size_t)                         = nullptr;
    bool     (*retro_unserialize)(const void *, size_t)                 = nullptr;
    bool     (*retro_load_game)(const struct retro_game_info *)         = nullptr;
    void     (*retro_unload_game)(void)                                 = nullptr;
    unsigned (*retro_get_region)(void)                                  = nullptr;
    void    *(*retro_get_memory_data)(unsigned)                         = nullptr;
    size_t   (*retro_get_memory_size)(unsigned)                         = nullptr;

    void (*retro_set_environment)(retro_environment_t)                  = nullptr;
    void (*retro_set_video_refresh)(retro_video_refresh_t)              = nullptr;
    void (*retro_set_audio_sample)(retro_audio_sample_t)                = nullptr;
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t)    = nullptr;
    void (*retro_set_input_poll)(retro_input_poll_t)                    = nullptr;
    void (*retro_set_input_state)(retro_input_state_t)                  = nullptr;

private:
    HMODULE      module_ = nullptr;
    std::wstring last_error_;
};
