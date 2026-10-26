// Befaco Oneiroi (the hardware module's OWL patch by Roberto Noris, GPL-3.0-or-later) as an MPC / Force instrument:
// an ambient / drone synth with a sine oscillator, a supersaw or wavetable oscillator, a looper, a filter, a
// resonator, an echo, a reverb ("ambience") and one modulator. (first written for vcv-rack/oneiroi in saustin2010/vst_instruments, 2026-10-08;
// mpc-vst-oneiroi since 2026-10-26, against the wrapper contract of sd88me/mpc-vst-plugins; GPL-3.0-or-later.)
//
// The sound is Oneiroi's own code: src/befaco/Oneiroi.h and the classes it uses, with the parts of Rebel
// Technology's OWL library they are written against (src/owl, GPL-3.0-or-later). Two changes to it
// (upstream-changes.diff): the two ARMv8 assembler lines in Commons.h are switched off (the MPC's CPU is ARMv7), and
// the echo follows an external clock whose period changes.
//
// The module's front panel is not: src/befaco/Ui.h reads knobs, faders, buttons and CV jacks through shift layers,
// catch-up, calibration and LEDs. This file stands in for it and writes the same PatchCtrls / PatchState values
// that Ui.h produces, straight from the plugin's parameters:
//  - every knob, its SHIFT function and its MOD amount is a parameter of its own (no layers, no catch-up);
//  - the patch runs at its own 48 kHz in 32-sample blocks (its constants assume both) and is resampled to the
//    MPC's 44.1 kHz (mpc_mi::Resampler);
//  - MIDI in: PLAY = GATE opens the two oscillators while a note is held (ATTACK / RELEASE), so echo and ambience
//    ring out; PLAY = DRONE leaves them on, as the module. KEY TRACK adds the last note to the pitch (C3 = the
//    knobs' own pitch at OCTAVE 4, TUNE 0), GLIDE slides, pitch bend is +/- 2 semitones;
//  - SYNC clocks the module from MPC's tempo, a pulse every beat, two beats or bar into its SYNC input: echo,
//    modulator and auto-pan then follow that clock and the looper restarts on it, as with a cable in SYNC IN.
//    OFF is the module's internal clock (the looper's cycle);
//  - the looper records the module's own output (the RESAMPLE position of the IN / RESAMPLE switch): an instrument
//    has no audio input on the MPC. RECORD and CLEAR are the two button functions.
// Built with -DONEIROI_INPUT (the "Oneiroi FX" insert effect, vst-fx/vst.json) the plugin has
// the module's audio input as well: IN VOL is its fader, and LOOP SOURCE is the IN / RESAMPLE switch. The input comes
// in at 44.1 kHz and is brought to the patch's 48 kHz through a short FIFO (about 1.5 ms).
// -DONEIROI_MIDI_PORT (the insert effect) adds a MIDI input port of its own, "Oneiroi FX MIDI In" (mpc/alsa_midi_in.h):
// MPC sends an insert effect no notes (tried on a Force), so a MIDI track plays it through that port instead.
// Not here: (without ONEIROI_INPUT) the audio input and its fader; the CV inputs, the RANDOM button with its switches (use presets), the
// settings saved on the module, MIDI out. The looper's recording is not saved with a project.
#include <new>

extern "C" {
#include "engine.h"
}
#include "mi_engine.h"
#ifdef ONEIROI_MIDI_PORT   // the insert effect: MPC sends an effect no notes, so it opens a MIDI input port of its own
#include "alsa_midi_in.h"
#ifndef ONEIROI_PORT_NAME
#define ONEIROI_PORT_NAME "Oneiroi FX"
#endif
#endif

// The OWL's heap hands out zeroed memory (OwlProgram Source/heap_5.c, behind its operator new), and the patch counts
// on that: many of its classes never set their members in their constructors. So every `new` in the patch's and the
// library's code is a zero-filled one here: a placement form of operator new, put in by a macro while their files
// are compiled. Every standard header they use is included first, so only their own code sees the macro.
// GCC would drop that zero-fill as a "dead store" ahead of a constructor (-flifetime-dse, on by default), so that
// optimisation is off for this file; the functions are kept out of line, as the OWL's allocator is.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("no-lifetime-dse")
#endif
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef __cpp_lib_clamp   // a C++ library older than C++17 (the clang build's ARM sysroot): the OWL library uses std::clamp
namespace std {
template <class T> constexpr const T &clamp(const T &v, const T &lo, const T &hi) { return v < lo ? lo : (hi < v ? hi : v); }
}
#endif
struct OwlZeroed {};
static const OwlZeroed owl_zeroed = OwlZeroed();
__attribute__((noinline)) void *operator new(size_t n, const OwlZeroed &) {
  void *p = ::operator new(n);
  memset(p, 0, n);
  return p;
}
__attribute__((noinline)) void *operator new[](size_t n, const OwlZeroed &) {
  void *p = ::operator new[](n);
  memset(p, 0, n);
  return p;
}
void operator delete(void *p, const OwlZeroed &) { ::operator delete(p); }
void operator delete[](void *p, const OwlZeroed &) { ::operator delete[](p); }
#define new new (owl_zeroed)

#include "Oneiroi.h"
#include "Clock.h"
#include "MemoryBuffer.hpp"
#include "FastLogTable.h"
#include "FastPowTable.h"
#include "FloatArray.cpp"   // the library's own sources this patch needs, built into this file: the framework
extern "C" {                // compiles a plugin's sources with one set of flags, and these are C and C++
#define register            // (C's `register`, which C++17 dropped)
#include "basicmaths.c"
#include "fastlog.c"
#include "fastpow.c"
#undef register
}

// ---- what the OWL firmware provides around a patch
AudioBuffer *AudioBuffer::create(int channels, int samples) { return new ManagedMemoryBuffer(channels, samples); }
AudioBuffer::~AudioBuffer() {}
void AudioBuffer::destroy(AudioBuffer *buffer) { delete buffer; }
#undef new
extern "C" {
void error(int8_t, const char *) {}
void debugMessage(const char *) {}
void assert_failed(const char *, const char *, int) {}
}

namespace {

const char *const kOnOff[] = {"OFF", "ON"};
const char *const kOsc2[] = {"SUPERSAW", "WAVETABLE"};
const char *const kFilterMode[] = {"LOWPASS", "BANDPASS", "HIGHPASS", "COMB"};
const char *const kFilterPos[] = {"PRE RESO", "PRE ECHO", "PRE AMB", "LAST"};
const char *const kModType[] = {"CHAOS", "SINE", "RAMP", "SAW", "SQUARE", "RANDOM", "ENVELOPE"};
const char *const kPlay[] = {"GATE", "DRONE"};
const char *const kSource[] = {"INPUT", "RESAMPLE"};
const char *const kSync[] = {"OFF", "BEAT", "2 BEATS", "BAR"};

enum { P_TUNE, P_OCTAVE, P_DETUNE, P_UNISON, P_OSC2_TYPE, P_SINE_VOL, P_OSC2_VOL, P_LOOPER_VOL,
       P_L_SPEED, P_L_START, P_L_LENGTH, P_L_SOS, P_L_FILTER, P_RECORD, P_CLEAR,
       P_F_VOL, P_F_CUTOFF, P_F_RESO, P_F_MODE, P_R_VOL, P_R_TUNE, P_R_FEEDBACK, P_R_DISSONANCE,
       P_E_VOL, P_E_DENSITY, P_E_REPEATS, P_E_FILTER, P_A_VOL, P_A_SPACETIME, P_A_DECAY, P_A_AUTOPAN,
       P_MOD_LEVEL, P_MOD_SPEED, P_MOD_TYPE,
       P_M_PITCH, P_M_DETUNE, P_M_SPEED, P_M_START, P_M_LENGTH,
       P_M_CUTOFF, P_M_RESO, P_M_TUNE, P_M_FEEDBACK, P_M_DENSITY, P_M_REPEATS, P_M_SPACETIME, P_M_DECAY,
       P_PLAY, P_KEYS, P_GLIDE, P_ATTACK, P_RELEASE, P_F_POSITION, P_SYNC, P_VOLUME,
#ifdef ONEIROI_INPUT
       P_IN_VOL, P_L_SOURCE,   // appended: the instrument without an input keeps its parameter numbers
#endif
       P_COUNT };

#ifndef ONEIROI_PLAY_DEFAULT
#define ONEIROI_PLAY_DEFAULT 0   // GATE
#endif

const mpc_mi::Param kParams[P_COUNT] = {
  {"tune", -6, 6, 0, 0, 0, false},
  {"octave", 1, 8, 4, 0, 0, true},
  {"detune", 0, 1, 0.2f, 0, 0, false},
  {"unison", -1, 1, 0, 0, 0, false},
  {"osc2_type", 0, 1, 0, kOsc2, 2, true},
  {"sine_vol", 0, 1, 0.7f, 0, 0, false},
  {"osc2_vol", 0, 1, 0.5f, 0, 0, false},
  {"looper_vol", 0, 1, 0, 0, 0, false},
  {"looper_speed", -2, 2, 1, 0, 0, false},
  {"looper_start", 0, 1, 0, 0, 0, false},
  {"looper_length", 0, 1, 1, 0, 0, false},
  {"looper_sos", 0, 1, 0, 0, 0, false},
  {"looper_filter", -1, 1, 0, 0, 0, false},
  {"record", 0, 1, 0, kOnOff, 2, true},
  {"clear", 0, 1, 0, 0, 0, true},
  {"filter_vol", 0, 1, 0.7f, 0, 0, false},
  {"cutoff", 0, 1, 0.6f, 0, 0, false},
  {"resonance", 0, 1, 0.2f, 0, 0, false},
  {"filter_mode", 0, 3, 0, kFilterMode, 4, true},
  {"reso_vol", 0, 1, 0, 0, 0, false},
  {"reso_tune", 0, 1, 0.5f, 0, 0, false},
  {"reso_feedback", 0, 1, 0.5f, 0, 0, false},
  {"dissonance", 0, 1, 0, 0, 0, false},
  {"echo_vol", 0, 1, 0.25f, 0, 0, false},
  {"echo_density", 0, 1, 0.5f, 0, 0, false},
  {"echo_repeats", 0, 1, 0.5f, 0, 0, false},
  {"echo_filter", -1, 1, 0, 0, 0, false},
  {"ambience_vol", 0, 1, 0.5f, 0, 0, false},
  {"spacetime", -1, 1, 0.3f, 0, 0, false},
  {"decay", 0, 1, 0.6f, 0, 0, false},
  {"autopan", 0, 1, 0, 0, 0, false},
  {"mod_level", 0, 1, 0, 0, 0, false},
  {"mod_speed", 0, 1, 0.5f, 0, 0, false},
  {"mod_type", 0, 6, 1, kModType, 7, true},
  {"mod_pitch", 0, 1, 0, 0, 0, false},
  {"mod_detune", 0, 1, 0, 0, 0, false},
  {"mod_speed_amt", 0, 1, 0, 0, 0, false},
  {"mod_start", 0, 1, 0, 0, 0, false},
  {"mod_length", 0, 1, 0, 0, 0, false},
  {"mod_cutoff", 0, 1, 0.5f, 0, 0, false},
  {"mod_resonance", 0, 1, 0, 0, 0, false},
  {"mod_reso_tune", 0, 1, 0, 0, 0, false},
  {"mod_feedback", 0, 1, 0, 0, 0, false},
  {"mod_density", 0, 1, 0, 0, 0, false},
  {"mod_repeats", 0, 1, 0, 0, 0, false},
  {"mod_spacetime", 0, 1, 0, 0, 0, false},
  {"mod_decay", 0, 1, 0, 0, 0, false},
  {"play", 0, 1, ONEIROI_PLAY_DEFAULT, kPlay, 2, true},
  {"keytrack", 0, 1, 1, kOnOff, 2, true},
  {"glide", 0, 1, 0, 0, 0, false},
  {"attack", 0, 1, 0.3f, 0, 0, false},
  {"release", 0, 1, 0.5f, 0, 0, false},
  {"filter_position", 0, 3, 0, kFilterPos, 4, true},
  {"sync", 0, 3, 0, kSync, 4, true},
  {"volume", 0, 1, 0.8f, 0, 0, false},
#ifdef ONEIROI_INPUT
  {"input_vol", 0, 1, 0.7f, 0, 0, false},
  {"looper_source", 0, 1, 0, kSource, 2, true},
#endif
};

enum { kBlock = 32 };                       // the OWL's block size: the patch's "block rate" constants assume 1500 Hz
const float kRate = 48000.0f;
const float kBlockRate = kRate / kBlock;
const float kSpeedZero = 2192.f / 4041.f;   // Ui.h's defaults for an uncalibrated module
const float kPitchZero = 2128.f / 4041.f;

struct Inst {
  mpc_mi::ParamSet ps;
  PatchCtrls ctrls;
  PatchCvs cvs;
  PatchState state;
  Oneiroi *oneiroi;
  Clock *clock;
  AudioBuffer *buffer;
  mpc_mi::Resampler<kBlock> rs;
  uint8_t held[16];
  int nheld, note;
  float bend;
  float note_cv;       // KEY TRACK's semitones, after GLIDE
  float note_pot;      // the knobs' pitch, smoothed as Ui.h does
  bool note_set;
  float env;           // GATE level for the oscillators
  double bpm, ppq, beat;
  int playing;
  bool pulse;          // the SYNC pulse is high
  int was_sync;
#ifdef ONEIROI_MIDI_PORT
  mi_port_t midi_in;
#endif
#ifdef ONEIROI_INPUT
  enum { kFifo = 1024 };          // the audio input, as it arrives (44.1 kHz)
  float fifo_l[kFifo], fifo_r[kFifo];
  uint64_t fifo_w;                // frames written so far
  double fifo_pos;                // where the patch reads (absolute, fractional)
  bool fifo_started;
#endif
};

// The output stage: straight up to -2.5 dB, then a soft knee into full scale instead of a hard clip
float Knee(float y) {
  float a = y < 0.0f ? -y : y;
  if (a <= 0.75f) return y;
  a = 0.75f + 0.25f * SoftClip((a - 0.75f) * 4.0f);
  return y < 0.0f ? -a : a;
}

// 0..1 -> seconds, 1 ms to 8 s (ATTACK, RELEASE, GLIDE)
float Time(float x) { return 0.001f * powf(8000.0f, x); }

// A bipolar knob with the module's off-centre detent: the inverse of Commons.h's CenterMap()
float FromCenter(float v, float lo, float hi, float center) {
  return v < 0.0f ? center * (1.0f - v / lo) : center + v / hi * (0.99f - center);
}
// The DJ filter knobs (looper, echo): low-pass below 0.45, nothing up to 0.55, high-pass above (DjFilter.h)
float DjKnob(float v) { return v < 0.0f ? 0.45f * (1.0f + v) : (v > 0.0f ? 0.55f + 0.45f * v : 0.5f); }
// A fader: Ui.h takes anything from kOne up as fully open
float Fader(float v) { return v >= kOne ? 1.0f : v; }
// A four-way SHIFT function: the middle of its quarter
float Quarter(int i) { return 0.125f + 0.25f * i; }

// The parameters, as Ui.h leaves them in PatchCtrls once a block (the pitch and the two oscillator levels follow
// in RenderBlock).
void Apply(Inst *in) {
  const mpc_mi::ParamSet &ps = in->ps;
  PatchCtrls &c = in->ctrls;
#ifdef ONEIROI_INPUT
  c.inputVol = MapExpo(ps[P_IN_VOL]);
#else
  c.inputVol = 0.0f;
#endif
  c.looperVol = MapExpo(ps[P_LOOPER_VOL]);
  c.looperSos = ps[P_L_SOS];
  c.looperFilter = DjKnob(ps[P_L_FILTER]);
  c.looperSpeed = FromCenter(ps[P_L_SPEED], -2.0f, 2.0f, kSpeedZero);
  c.looperStart = ps[P_L_START];
  c.looperLength = ps[P_L_LENGTH];
  c.looperRecording = ps.index(P_RECORD) ? 1.0f : 0.0f;
#ifdef ONEIROI_INPUT
  c.looperResampling = ps.index(P_L_SOURCE) ? 1.0f : 0.0f;
#else
  c.looperResampling = 1.0f;
#endif
  c.oscOctave = (float)ps.index(P_OCTAVE);
  c.oscUnison = ps[P_UNISON];
  in->state.oscUnisonCenterFlag = c.oscUnison >= -0.03f && c.oscUnison <= 0.03f;
  if (in->state.oscUnisonCenterFlag) c.oscUnison = 0.0f;
  c.oscDetune = ps[P_DETUNE];
  c.oscUseWavetable = ps.index(P_OSC2_TYPE) ? 1.0f : 0.0f;
  c.filterVol = Fader(ps[P_F_VOL]);
  c.filterMode = Quarter(ps.index(P_F_MODE));
  c.filterCutoff = ps[P_F_CUTOFF];
  c.filterResonance = ps[P_F_RESO];
  c.filterPosition = Quarter(ps.index(P_F_POSITION));
  c.resonatorVol = Fader(ps[P_R_VOL]);
  c.resonatorTune = ps[P_R_TUNE];
  c.resonatorFeedback = ps[P_R_FEEDBACK];
  c.resonatorDissonance = ps[P_R_DISSONANCE];
  c.echoVol = Fader(ps[P_E_VOL]);
  c.echoRepeats = ps[P_E_REPEATS];
  c.echoDensity = ps[P_E_DENSITY];
  c.echoFilter = DjKnob(ps[P_E_FILTER]);
  c.ambienceVol = Fader(ps[P_A_VOL]);
  c.ambienceDecay = ps[P_A_DECAY];
  c.ambienceSpacetime = FromCenter(ps[P_A_SPACETIME], -1.0f, 1.0f, 0.55f);
  c.ambienceAutoPan = ps[P_A_AUTOPAN];
  c.modLevel = ps[P_MOD_LEVEL];
  c.modSpeed = ps[P_MOD_SPEED];
  c.modType = ps.index(P_MOD_TYPE) / 6.0f;
  c.oscPitchModAmount = ps[P_M_PITCH];
  c.oscDetuneModAmount = ps[P_M_DETUNE];
  c.looperSpeedModAmount = ps[P_M_SPEED];
  c.looperStartModAmount = ps[P_M_START];
  c.looperLengthModAmount = ps[P_M_LENGTH];
  c.filterCutoffModAmount = ps[P_M_CUTOFF];
  c.filterResonanceModAmount = ps[P_M_RESO];
  c.resonatorTuneModAmount = ps[P_M_TUNE];
  c.resonatorFeedbackModAmount = ps[P_M_FEEDBACK];
  c.echoDensityModAmount = ps[P_M_DENSITY];
  c.echoRepeatsModAmount = ps[P_M_REPEATS];
  c.ambienceSpacetimeModAmount = ps[P_M_SPACETIME];
  c.ambienceDecayModAmount = ps[P_M_DECAY];
  in->state.modActive = c.modLevel > 0.1f;
}

void *Create(const char *) {
  // the OWL firmware hands the patch its lookup tables; here they are the library's own (the same values every time)
  fast_log_set_table(fast_log_table, fast_log_table_size);
  fast_pow_set_table(fast_pow_table, fast_pow_table_size);
  Inst *in = new (std::nothrow) Inst();
  if (!in) return NULL;
  in->ps.Init(kParams, P_COUNT);
  memset(&in->ctrls, 0, sizeof in->ctrls);
  memset(&in->cvs, 0, sizeof in->cvs);
  PatchState &s = in->state;
  s = PatchState();
  s.sampleRate = kRate;
  s.blockRate = kBlockRate;
  s.blockSize = kBlock;
  // what Ui.h sets up
  s.funcMode = FuncMode::FUNC_MODE_NONE;
  s.inputLevel = FloatArray::create(kBlock);
  s.efModLevel = FloatArray::create(kBlock);
  s.inputLevel.clear();
  s.efModLevel.clear();
  s.outLevel = 1.0f;
  s.randomSlew = kRandomSlewSamples;
  s.randomHasSlew = false;
  s.softTakeover = false;
  s.modAttenuverters = false;
  s.cvAttenuverters = false;
  s.c2 = 1102.f / 4095.f;
  s.c5 = 2334.f / 4095.f;
  s.pitchZero = kPitchZero;
  s.speedZero = kSpeedZero;
  s.clockSource = ClockSource::CLOCK_SOURCE_INTERNAL;
  s.startupPhase = StartupPhase::STARTUP_DONE;
  // the CV amounts are at their defaults; nothing is patched, so every CV reads 0
  PatchCtrls &c = in->ctrls;
  c.looperSpeedCvAmount = c.looperStartCvAmount = c.looperLengthCvAmount = 1.0f;
  c.oscPitchCvAmount = c.oscDetuneCvAmount = c.filterCutoffCvAmount = 1.0f;
  c.resonatorTuneCvAmount = c.echoDensityCvAmount = c.ambienceSpacetimeCvAmount = 1.0f;
  Apply(in);
  c.osc1Vol = c.osc2Vol = 0.0f;
  c.oscPitch = M2F(12.0f * in->ps.index(P_OCTAVE));
  in->oneiroi = Oneiroi::create(&in->ctrls, &in->cvs, &in->state);
  in->clock = Clock::create(&in->ctrls, &in->state);   // (it makes state.tempo)
  in->buffer = AudioBuffer::create(2, kBlock);
  in->rs.Init(kRate);
  in->nheld = 0;
  in->note = 48;
  in->bend = 0.0f;
  in->note_cv = 0.0f;
  in->note_pot = 12.0f * in->ps.index(P_OCTAVE);
  in->note_set = false;
  in->env = 0.0f;
  in->bpm = 120.0;
  in->ppq = -1.0;
  in->beat = 0.0;
  in->playing = 0;
  in->pulse = false;
  in->was_sync = 0;
#ifdef ONEIROI_MIDI_PORT
  mi_open(&in->midi_in, ONEIROI_PORT_NAME);
#endif
  return in;
}

void Destroy(void *p) {
  Inst *in = (Inst *)p;
#ifdef ONEIROI_MIDI_PORT
  mi_close(&in->midi_in);
#endif
  Oneiroi::destroy(in->oneiroi);
  Clock::destroy(in->clock);
  TapTempo::destroy(in->state.tempo);
  AudioBuffer::destroy(in->buffer);
  FloatArray::destroy(in->state.inputLevel);
  FloatArray::destroy(in->state.efModLevel);
  delete in;
}

void Midi(void *p, const uint8_t *m, int len) {
  Inst *in = (Inst *)p;
  if (len < 3) return;
  uint8_t st = m[0] & 0xF0;
  bool on = st == 0x90 && m[2] > 0, off = st == 0x80 || (st == 0x90 && m[2] == 0);
  if (on || off) {
    int j = 0;
    for (int i = 0; i < in->nheld; ++i)
      if (in->held[i] != m[1]) in->held[j++] = in->held[i];
    in->nheld = j;
  }
  if (on) {
    if (in->nheld == 16) {
      memmove(in->held, in->held + 1, 15);
      in->nheld = 15;
    }
    in->held[in->nheld++] = m[1];
    in->note = m[1];
  } else if (off && in->nheld) {
    in->note = in->held[in->nheld - 1];
  } else if (st == 0xE0) {
    in->bend = (((m[2] << 7) | m[1]) - 8192) / 8192.0f * 2.0f;
  } else if (st == 0xB0 && (m[1] == 123 || m[1] == 120)) {
    in->nheld = 0;
  }
}

void SetParam(void *p, const char *k, const char *v) { ((Inst *)p)->ps.Set(k, v); }

int Percent(char *b, int n, float v) { return snprintf(b, n, "%.0f %%", v * 100.0f); }

// What the screen shows where a bare number says little
int Display(Inst *in, int i, char *b, int n) {
  const mpc_mi::ParamSet &ps = in->ps;
  float v = ps[i];
  switch (i) {
    case P_TUNE: return snprintf(b, n, "%+.2f st", v);
    case P_UNISON:
      if (v >= -0.03f && v <= 0.03f) return snprintf(b, n, "UNISON");
      return snprintf(b, n, "x%.2f", v < 0.0f ? 1.0f + 0.5f * v : 1.0f + v);
    case P_L_SPEED:
      if (v >= -0.1f && v <= 0.1f) return snprintf(b, n, "STOP");
      return snprintf(b, n, "x%+.2f", v);
    case P_L_FILTER: case P_E_FILTER:
      if (v == 0.0f) return snprintf(b, n, "OFF");
      return v < 0.0f ? snprintf(b, n, "LP %.0f %%", -v * 100.0f) : snprintf(b, n, "HP %.0f %%", v * 100.0f);
    case P_A_SPACETIME: return snprintf(b, n, "%+.0f %%", v * 100.0f);
    case P_MOD_SPEED: {
      if (v >= 0.47f && v <= 0.53f) return snprintf(b, n, "x1");
      float r = kModClockRatios[QuantizeInt(v, kClockNofRatios)];
      return r >= 1.0f ? snprintf(b, n, "x%.0f", r) : snprintf(b, n, "/%.0f", roundf(1.0f / r));
    }
    case P_GLIDE:
      if (v <= 0.0f) return snprintf(b, n, "OFF");
      // fall through
    case P_ATTACK: case P_RELEASE: {
      float s = Time(v);
      return s < 1.0f ? snprintf(b, n, "%.0f ms", s * 1000.0f) : snprintf(b, n, "%.2f s", s);
    }
    case P_VOLUME:
      if (v <= 0.001f) return snprintf(b, n, "-inf dB");
      return snprintf(b, n, "%+.1f dB", 20.0f * log10f(3.125f * v * v));
    case P_CLEAR: case P_OCTAVE: return -1;
  }
  if (!kParams[i].options) return Percent(b, n, v);
  return -1;
}

int GetParam(void *p, const char *k, char *b, int n) {
  Inst *in = (Inst *)p;
  size_t len = strlen(k);
  if (len > 8 && !strcmp(k + len - 8, "_display")) {
    char key[64];
    if (len - 8 >= sizeof key) return -1;
    memcpy(key, k, len - 8);
    key[len - 8] = 0;
    int i = in->ps.Find(key);
    return i < 0 ? -1 : Display(in, i, b, n);
  }
  return in->ps.Get(k, b, n);
}

// One block of the patch: 32 frames at 48 kHz.
void RenderBlock(Inst *in, float *l, float *r) {
  const mpc_mi::ParamSet &ps = in->ps;
  PatchCtrls &c = in->ctrls;
  PatchState &s = in->state;

  // SYNC: MPC's tempo as a clock patched into SYNC IN (Ui.h's ProcessButton): a pulse every beat, two beats or bar,
  // on the song position. The module drops an external clock slower than 2 s, so a longer period is halved.
  int sync = ps.index(P_SYNC);
  if (in->pulse) {
    s.tempo->trigger(false, 0);
    in->pulse = false;
  }
  s.syncIn = false;
  size_t period = 0;   // in blocks
  if (sync) {
    double bpm = in->bpm > 20.0 ? in->bpm : 120.0;
    double beats = sync == 1 ? 1.0 : (sync == 2 ? 2.0 : 4.0);
    while (beats > 0.25 && beats * 60.0 / bpm > 1.9) beats *= 0.5;
    period = (size_t)lround(beats * 60.0 / bpm * kBlockRate);
    double before = in->beat;
    in->beat += bpm / 60.0 * kBlock / kRate;
    if (in->was_sync != sync || floor(in->beat / beats) != floor(before / beats)) {
      s.tempo->trigger(true, 0);
      s.syncIn = true;
      in->pulse = true;
    }
    s.tempo->setPeriodInSamples(period);   // the tempo itself is known: no need to measure it from the pulses
  }
  in->was_sync = sync;

  // the pitch, as Ui.h's Poll(): the knobs (with their modulation) smoothed, plus the note
  float key = ps.index(P_KEYS) ? (float)(in->note - 48) + in->bend : 0.0f;
  if (!in->note_set || ps[P_GLIDE] <= 0.0f) {
    in->note_cv = key;
  } else {
    in->note_cv += (key - in->note_cv) * (1.0f - expf(-3.0f / (Time(ps[P_GLIDE]) * kBlockRate)));
  }
  in->note_set = true;
  float note = Modulate(ps[P_TUNE] / 12.0f, c.oscPitchModAmount, s.modValue, 0, 0, -1.f, 1.f, s.modAttenuverters);
  note = 12.0f * note + 12.0f * c.oscOctave;
  in->note_pot += 0.1f * (note - in->note_pot);
  c.oscPitch = M2F(in->note_pot + in->note_cv);

  // the oscillator faders, through the gate
  bool gate = ps.index(P_PLAY) == 1 || in->nheld > 0;
  float t = Time(ps[gate ? P_ATTACK : P_RELEASE]);
  in->env += ((gate ? 1.0f : 0.0f) - in->env) * (1.0f - expf(-3.0f / (t * kBlockRate)));
  c.osc1Vol = MapExpo(ps[P_SINE_VOL]) * in->env;
  c.osc2Vol = MapExpo(ps[P_OSC2_VOL]) * in->env;

#ifdef ONEIROI_INPUT
  {   // 32 frames of the input at 48 kHz: 4-point interpolation through the FIFO, 29.4 input frames a block
    FloatArray il = in->buffer->getSamples(LEFT_CHANNEL), ir = in->buffer->getSamples(RIGHT_CHANNEL);
    const double step = 44100.0 / kRate;
    if (in->fifo_w > (uint64_t)in->fifo_pos + 700) in->fifo_pos = (double)in->fifo_w - 192.0;   // fell behind: catch up
    for (int i = 0; i < kBlock; ++i) {
      uint64_t k = (uint64_t)in->fifo_pos;
      if (!in->fifo_started || k + 3 > in->fifo_w || k < 1) {   // nothing (yet) to read: silence, and wait
        il[i] = ir[i] = 0.0f;
        continue;
      }
      float t = (float)(in->fifo_pos - (double)k);
      float x[4], y[4];
      for (int j = 0; j < 4; ++j) {
        x[j] = in->fifo_l[(k - 1 + j) & (Inst::kFifo - 1)];
        y[j] = in->fifo_r[(k - 1 + j) & (Inst::kFifo - 1)];
      }
      il[i] = ((0.5f * (x[3] - x[0]) + 1.5f * (x[1] - x[2])) * t + (x[0] - 2.5f * x[1] + 2.0f * x[2] - 0.5f * x[3])) * t * t
              + 0.5f * (x[2] - x[0]) * t + x[1];
      ir[i] = ((0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2])) * t + (y[0] - 2.5f * y[1] + 2.0f * y[2] - 0.5f * y[3])) * t * t
              + 0.5f * (y[2] - y[0]) * t + y[1];
      in->fifo_pos += step;
    }
  }
#else
  in->buffer->clear();   // no audio input
#endif
  in->clock->Process();
  if (period) s.clockSamples = period;   // (Clock.h only takes a new period when it differs by 48 blocks)
  in->oneiroi->Process(*in->buffer);
  FloatArray left = in->buffer->getSamples(LEFT_CHANNEL), right = in->buffer->getSamples(RIGHT_CHANNEL);
  float g = 3.125f * ps[P_VOLUME] * ps[P_VOLUME];   // the module's own level sits low for a plugin: +6 dB at 0.8
  for (int i = 0; i < kBlock; ++i) {
    l[i] = Knee(left[i] * g);
    r[i] = Knee(right[i] * g);
  }
}

void Render(void *p, int16_t *out, int frames) {
  Inst *in = (Inst *)p;
  if (frames > 256) frames = 256;
#ifdef ONEIROI_MIDI_PORT
  mi_poll(&in->midi_in, [](void *u, const uint8_t *m, int n) { Midi(u, m, n); }, in);   // notes sent to its own port
#endif
  Apply(in);
  if (in->ps[P_CLEAR] > 0.5f) {   // the CLEAR button: once
    in->state.clearLooperFlag = true;
    in->ps.Set("clear", "0");
  }
  if (in->playing && in->ppq >= 0.0 && in->ps.index(P_SYNC)) {   // stay on MPC's beats while it plays
    double d = in->ppq - in->beat;
    if (d < -0.02 || d > 0.02) in->beat = in->ppq;
  }
  float l[256], r[256];
  in->rs.Render(l, r, frames, [in](float *bl, float *br) { RenderBlock(in, bl, br); });
  for (int i = 0; i < frames; ++i) {
    out[2 * i] = mpc_mi::ToS16(l[i]);
    out[2 * i + 1] = mpc_mi::ToS16(r[i]);
  }
}

#ifdef ONEIROI_INPUT
// The audio input, as it arrives (44.1 kHz): into the FIFO the patch reads at 48 kHz (RenderBlock)
void Input(Inst *in, const int16_t *in_lr, int frames) {
  if (!in_lr) return;
  if (!in->fifo_started) {   // start 64 frames behind: a block of the patch may ask for a little more than has arrived
    in->fifo_started = true;
    in->fifo_w = 256;
    in->fifo_pos = 256.0 - 64.0;
  }
  for (int i = 0; i < frames; ++i) {
    in->fifo_l[in->fifo_w & (Inst::kFifo - 1)] = in_lr[2 * i] / 32768.0f;
    in->fifo_r[in->fifo_w & (Inst::kFifo - 1)] = in_lr[2 * i + 1] / 32768.0f;
    in->fifo_w++;
  }
}

// The insert effect (wrapper/engine.h process()): the track's block in, Oneiroi's block out
void Process(void *p, const int16_t *in_lr, int16_t *out_lr, int frames) {
  Inst *in = (Inst *)p;
  if (frames > 256) frames = 256;
  Input(in, in_lr, frames);
  Render(p, out_lr, frames);
}
#endif

// The host's tempo and song position, once per host block (vst.json "defines": {"HAS_SONG_POS": 1}): SYNC follows it
void SongPos(void *p, double ppq, double bpm, int playing, int frames) {
  (void)frames;
  Inst *in = (Inst *)p;
  if (bpm > 0.0) in->bpm = bpm;
  in->ppq = ppq;
  in->playing = playing;
}

#ifdef ONEIROI_INPUT
const mpc_engine_t kEngine = {Create, Destroy, Midi, SetParam, GetParam, Render, Process, SongPos};
#else
const mpc_engine_t kEngine = {Create, Destroy, Midi, SetParam, GetParam, Render, NULL, SongPos};
#endif

}  // namespace

const mpc_engine_t *mpc_engine(void) { return &kEngine; }
