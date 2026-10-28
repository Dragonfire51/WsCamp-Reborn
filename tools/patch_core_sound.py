#!/usr/bin/env python3
"""Aplica a beetle-wswan el control por canal de sonido que usa WsCampReborn.

El nucleo original no ofrece ningun ajuste para silenciar canales sueltos: el
sonido del WonderSwan se genera en mednafen/wswan/sound.c y se mezcla sin
intermediarios. Este parche anade siete opciones de nucleo
(wswan_sound_ch1..ch4, noise, pcm, dma) y consulta su valor en los puntos donde
cada fuente entrega su muestra.

Se aplica sobre el arbol limpio del nucleo y es idempotente: si ya esta
aplicado, no hace nada. Las sustituciones comprueban su ancla para que un
cambio de upstream falle de forma visible en lugar de corromper el fichero.

Uso:  python3 patch_core_sound.py <ruta-al-arbol-del-nucleo>
"""

import re
import sys
import os

MARK = "WSCAMP_SOUND_CHANNELS"

OPTIONS_ANCHOR = "   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },\n};\n"
OPTIONS_BLOCK = '''   /* WSCAMP_SOUND_CHANNELS */
   {
      "wswan_sound_ch1",
      "Sound Channel 1",
      NULL,
      "Enable the first wave channel.",
      NULL,
      NULL,
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled",
   },
   {
      "wswan_sound_ch2",
      "Sound Channel 2",
      NULL,
      "Enable the second wave channel (also used for voice and sweep).",
      NULL,
      NULL,
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled",
   },
   {
      "wswan_sound_ch3",
      "Sound Channel 3",
      NULL,
      "Enable the third wave channel.",
      NULL,
      NULL,
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled",
   },
   {
      "wswan_sound_ch4",
      "Sound Channel 4 / Noise",
      NULL,
      "Enable the fourth channel, which doubles as the noise generator.",
      NULL,
      NULL,
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled",
   },
   {
      "wswan_sound_noise",
      "Sound Noise",
      NULL,
      "Enable the noise generator.",
      NULL,
      NULL,
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled",
   },
   {
      "wswan_sound_pcm",
      "Sound PCM / Hyper Voice",
      NULL,
      "Enable PCM playback, including the Hyper Voice channel.",
      NULL,
      NULL,
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled",
   },
   {
      "wswan_sound_dma",
      "Sound DMA",
      NULL,
      "Enable the sound DMA channel.",
      NULL,
      NULL,
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled",
   },
'''

SOUND_ANCHOR = '#include "../include/blip/Blip_Buffer.h"\n'
SOUND_ADDITION = SOUND_ANCHOR + '''
/* WSCAMP_SOUND_CHANNELS: el frontend silencia canales sueltos con estas
 * banderas; WSwan_SetSoundChannelEnabled() las actualiza y el nucleo avisa del
 * cambio con RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE. */
int ws_sound_ch_enabled[7] = { 1, 1, 1, 1, 1, 1, 1 };
#define WSCAMP_CH(ch) (ws_sound_ch_enabled[ch] ? 1 : 0)

void WSwan_SetSoundChannelEnabled(int channel, int enabled);
'''

# El fichero del nucleo mezcla espacios y tabuladores, asi que estas dos
# ediciones van por expresion regular y no por texto literal.
MACRO_CH_PATTERN = r"(#define MK_SAMPLE_CACHE[ \t]*\\\n)(.*?)(    sample_cache\[ch\]\[0\])"
MACRO_CH_REPL = r"\1\2    sample *= WSCAMP_CH(ch);    \\\n\3"

MACRO_NOISE_PATTERN = r"(#define MK_SAMPLE_CACHE_NOISE[ \t]*\\\n)(.*?)(    sample_cache\[ch\]\[0\])"
MACRO_NOISE_REPL = r"\1\2    sample *= WSCAMP_CH(3) * WSCAMP_CH(4);    \\\n\3"

HV_ANCHOR = '''      left  = (HVoiceChanCtrl & 0x40) ? sample : 0;
      right = (HVoiceChanCtrl & 0x20) ? sample : 0;
'''
HV_REPLACEMENT = '''      if(!WSCAMP_CH(5))
         sample = 0;

      left  = (HVoiceChanCtrl & 0x40) ? sample : 0;
      right = (HVoiceChanCtrl & 0x20) ? sample : 0;
'''

FUNC_ANCHOR = "void WSwan_SoundWrite(uint32 A, uint8 V)\n"
FUNC_ADDITION = '''void WSwan_SetSoundChannelEnabled(int channel, int enabled)
{
   if(channel < 0 || channel >= 7)
      return;

   ws_sound_ch_enabled[channel] = enabled ? 1 : 0;
}

void WSwan_SoundWrite(uint32 A, uint8 V)
'''

# WSCAMP_SOUND_CHANNELS: introspeccion minima para que las pruebas del
# frontend puedan comprobar el remapeo de la cruceta al rotar sin depender de
# la salida de video. Devuelve el registro de botones y si la pantalla esta en
# vertical.
INTROSPECT_ANCHOR = "void retro_deinit(void)\n{\n"
INTROSPECT_ADDITION = '''/* WSCAMP_SOUND_CHANNELS: introspeccion para las pruebas del frontend. */
void wscamp_get_input_state(unsigned short *out_buf, int *out_rotate_tall)
{
   if (out_buf)
      *out_buf = input_buf;
   if (out_rotate_tall)
      *out_rotate_tall = rotate_tall ? 1 : 0;
}

void retro_deinit(void)
{
'''

LIBRETRO_ANCHOR = "static void check_variables(int startup)\n{\n"
LIBRETRO_ADDITION = '''/* WSCAMP_SOUND_CHANNELS: el frontend puede silenciar cada canal por separado.
 * check_variables() es el unico punto que consulta las opciones, asi que la
 * aplicacion de las banderas vive aqui. */
void WSwan_SetSoundChannelEnabled(int channel, int enabled);

static void wscamp_apply_sound_channels(void)
{
   static const char *keys[7] = {
      "wswan_sound_ch1", "wswan_sound_ch2", "wswan_sound_ch3", "wswan_sound_ch4",
      "wswan_sound_noise", "wswan_sound_pcm", "wswan_sound_dma"
   };
   int i;

   for(i = 0; i < 7; i++)
   {
      struct retro_variable var = {0};
      int enabled = 1;

      var.key   = keys[i];
      var.value = NULL;
      if(environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
         enabled = strcmp(var.value, "disabled") ? 1 : 0;

      WSwan_SetSoundChannelEnabled(i, enabled);
   }
}

static void check_variables(int startup)
{
   wscamp_apply_sound_channels();
'''

DMA_ANCHOR = "   if(!SoundDMATimer)\n   {\n"
DMA_REPLACEMENT = "   if(!ws_sound_ch_enabled[6])\n      return;\n\n   if(!SoundDMATimer)\n   {\n"

LINK_T_ANCHOR = """{
   global: retro_*;
   local: *;
};
"""
LINK_T_REPLACEMENT = """{
   global:
      retro_*;
      /* WSCAMP_SOUND_CHANNELS: el frontend puede leer las banderas para
       * comprobar que el control por canal esta activo, y consultar el estado
       * de entrada en las pruebas. */
      ws_sound_ch_enabled;
      wscamp_get_input_state;
   local: *;
};
"""

# La introspeccion anade su simbolo al version-script. Va con su propia marca
# porque link.T ya puede llevar la del parche de sonido (arboles ya parcheados).
LINK_T_INTROSPECT_ANCHOR = "      ws_sound_ch_enabled;\n"
LINK_T_INTROSPECT_REPL = ("      ws_sound_ch_enabled;\n"
                          "      wscamp_get_input_state;\n")

DMA_SRC_ANCHOR = "static uint32 SoundDMASource, SoundDMASourceSaved;\n"
DMA_SRC_REPLACEMENT = "static uint32 SoundDMASource, SoundDMASourceSaved;\nextern int ws_sound_ch_enabled[7]; /* WSCAMP_SOUND_CHANNELS */\n"


def patch_regex(path, pattern, replacement, label, done_marker):
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()

    # La marca general del parche no sirve aqui: patch_file ya la ha escrito en
    # este mismo fichero, asi que cada edicion por expresion regular lleva su
    # propia comprobacion.
    if done_marker in text:
        print("  %-32s ya parcheado" % label)
        return False

    matches = list(re.finditer(pattern, text, re.DOTALL))
    if len(matches) != 1:
        raise SystemExit(
            "ERROR: el patron '%s' casa %d veces en %s (se esperaba 1). "
            "El nucleo ha cambiado: revisa patch_core_sound.py."
            % (label, len(matches), path))

    text = re.sub(pattern, replacement, text, count=1, flags=re.DOTALL)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("  %-32s parcheado" % label)
    return True


def patch_file(path, edits, name):
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()

    if MARK in text:
        print("  %-32s ya parcheado" % name)
        return False

    for anchor, replacement, label in edits:
        if text.count(anchor) != 1:
            raise SystemExit(
                "ERROR: el ancla '%s' aparece %d veces en %s (se esperaba 1). "
                "El nucleo ha cambiado: revisa patch_core_sound.py."
                % (label, text.count(anchor), path))
        text = text.replace(anchor, replacement, 1)

    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("  %-32s parcheado" % name)
    return True


def main():
    if len(sys.argv) != 2:
        raise SystemExit("uso: patch_core_sound.py <ruta-al-arbol-del-nucleo>")

    root = sys.argv[1]
    options = os.path.join(root, "libretro_core_options.h")
    sound_c = os.path.join(root, "mednafen", "wswan", "sound.c")
    memory_c = os.path.join(root, "mednafen", "wswan", "wswan-memory.c")
    libretro_c = os.path.join(root, "libretro.c")
    link_t = os.path.join(root, "link.T")

    for path in (options, sound_c, memory_c, libretro_c, link_t):
        if not os.path.isfile(path):
            raise SystemExit("ERROR: no existe %s" % path)

    print("Aplicando control por canal de sonido:")

    patch_file(options, [(OPTIONS_ANCHOR, OPTIONS_BLOCK + OPTIONS_ANCHOR,
                          "opciones de nucleo")], "libretro_core_options.h")

    patch_file(sound_c, [
        (SOUND_ANCHOR, SOUND_ADDITION, "estado de canales"),
        (HV_ANCHOR, HV_REPLACEMENT, "Hyper Voice"),
        (FUNC_ANCHOR, FUNC_ADDITION, "setter"),
    ], "mednafen/wswan/sound.c")
    patch_regex(sound_c, MACRO_CH_PATTERN, MACRO_CH_REPL, "canal de onda",
                "sample *= WSCAMP_CH(ch);")
    patch_regex(sound_c, MACRO_NOISE_PATTERN, MACRO_NOISE_REPL, "ruido",
                "sample *= WSCAMP_CH(3)")

    patch_file(memory_c, [
        (DMA_SRC_ANCHOR, DMA_SRC_REPLACEMENT, "declaracion del DMA"),
        (DMA_ANCHOR, DMA_REPLACEMENT, "puerta del DMA"),
    ], "mednafen/wswan/wswan-memory.c")

    patch_file(libretro_c, [
        (LIBRETRO_ANCHOR, LIBRETRO_ADDITION, "aplicacion de opciones"),
    ], "libretro.c")

    patch_regex(libretro_c, re.escape(INTROSPECT_ANCHOR),
                INTROSPECT_ADDITION, "introspeccion",
                "wscamp_get_input_state")
    patch_regex(link_t, re.escape(LINK_T_INTROSPECT_ANCHOR),
                LINK_T_INTROSPECT_REPL, "exportacion de introspeccion",
                "wscamp_get_input_state")

    patch_file(link_t, [
        (LINK_T_ANCHOR, LINK_T_REPLACEMENT, "exportacion de banderas"),
    ], "link.T")

    print("Listo.")


if __name__ == "__main__":
    main()
