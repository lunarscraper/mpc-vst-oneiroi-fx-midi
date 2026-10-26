// Oneiroi Pocket: the same engine as the Force plugins (../engine/mpc/oneiroi_engine.cc, built with ONEIROI_INPUT)
// as a WebAssembly module for the browser (pocket/build.sh). The page's AudioWorklet runs it at 44.1 kHz, the Force's
// rate, so it sounds as on the device. Exports work on float buffers in the module's memory; no WASI calls are made.
extern "C" {
#include "engine.h"
}
#include "../engine/mpc/oneiroi_engine.cc"

namespace {
Inst *g_in = nullptr;
float g_inl[256], g_inr[256], g_outl[256], g_outr[256];
char g_text[2048];
}

#define EXPORT extern "C" __attribute__((visibility("default")))

EXPORT int pk_init() {
  if (!g_in) g_in = (Inst *)Create(nullptr);
  return g_in ? 1 : 0;
}
EXPORT float *pk_in_l() { return g_inl; }
EXPORT float *pk_in_r() { return g_inr; }
EXPORT float *pk_out_l() { return g_outl; }
EXPORT float *pk_out_r() { return g_outr; }
EXPORT char *pk_text() { return g_text; }      // shared string buffer: keys, values, state
EXPORT int pk_count() { return P_COUNT; }

// parameter i: its key into pk_text(); min/max/default/options
EXPORT int pk_key(int i) { return snprintf(g_text, sizeof g_text, "%s", kParams[i].key); }
EXPORT float pk_min(int i) { return kParams[i].min; }
EXPORT float pk_max(int i) { return kParams[i].max; }
EXPORT float pk_def(int i) { return kParams[i].def; }
EXPORT int pk_nopts(int i) { return kParams[i].options ? kParams[i].nopts : 0; }
EXPORT int pk_opt(int i, int o) { return snprintf(g_text, sizeof g_text, "%s", kParams[i].options[o]); }
EXPORT int pk_integer(int i) { return kParams[i].integer ? 1 : 0; }

EXPORT void pk_set(int i, float v) {
  char b[32];
  if (kParams[i].options || kParams[i].integer) snprintf(b, sizeof b, "%d", (int)lroundf(v));
  else snprintf(b, sizeof b, "%.5f", v);
  g_in->ps.Set(kParams[i].key, b);
}
EXPORT float pk_get(int i) { return g_in->ps[i]; }
// what the Force's screen shows for parameter i, into pk_text(); 0 if it has no special text
EXPORT int pk_display(int i) {
  int n = Display(g_in, i, g_text, sizeof g_text);
  if (n <= 0) n = 0, g_text[0] = 0;
  return n;
}
EXPORT void pk_midi(int a, int b, int c) {
  uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
  Midi(g_in, m, 3);
}
EXPORT void pk_tempo(float bpm) { SongPos(g_in, -1.0, bpm, 0, 128); }

// frames (<= 128) of audio: pk_in_l/r -> pk_out_l/r. The engine's own int16 path, as on the Force.
EXPORT void pk_process(int frames, int has_input) {
  int16_t in[256], out[256];
  if (frames > 128) frames = 128;
  if (has_input) {
    for (int i = 0; i < frames; ++i) { in[2 * i] = mpc_mi::ToS16(g_inl[i]); in[2 * i + 1] = mpc_mi::ToS16(g_inr[i]); }
    Input(g_in, in, frames);
  }
  Render(g_in, out, frames);
  for (int i = 0; i < frames; ++i) { g_outl[i] = out[2 * i] / 32768.0f; g_outr[i] = out[2 * i + 1] / 32768.0f; }
}
// looper peak per pixel column is not exposed by the patch; an output meter is enough
