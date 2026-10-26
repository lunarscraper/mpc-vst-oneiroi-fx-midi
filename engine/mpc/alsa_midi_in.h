/* A plugin's own MIDI INPUT port on MPC OS (written for the Oneiroi port in saustin2010/vst_instruments, 2026-10-08).
 * MPC OS sends an insert effect no notes (tried on a Force, 2026-10-08), so an effect that wants to be played opens an
 * ALSA sequencer port others can write to (client = its name, port "MIDI In"; a second instance "<name> 2"). MPC
 * mirrors every sequencer port it detects, a writable one as a MIDI output (framework docs/NOTES.md, 2026-10-04), so a
 * MIDI track can pick "<name> MIDI In" as its MIDI output. libasound is dlopen()ed on the device, so the build needs no
 * ALSA headers; without it (offline tests) the port simply doesn't exist. Header-only, C or C++.
 *   mi_open(&p, "name");   mi_poll(&p, callback, user) once per render: callback(user, msg, 3) per event;   mi_close(&p)
 * Notes, pitch bend, control changes and channel pressure are passed on; everything else is dropped. */
#pragma once
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char client, port; } mi_addr_t;
typedef struct {   /* the kernel's stable snd_seq_event_t layout (28 bytes) */
    unsigned char type, flags, tag, queue;
    unsigned int time[2];
    mi_addr_t source, dest;
    unsigned char data[12];
} mi_ev_t;

typedef struct { void *seq; int port; } mi_port_t;

static struct {
    int ok;
    int (*open)(void **, const char *, int, int);
    int (*set_name)(void *, const char *);
    int (*port)(void *, const char *, unsigned, unsigned);
    int (*nonblock)(void *, int);
    int (*input)(void *, mi_ev_t **);
    int (*close)(void *);
} mi_A;

#define MI_SYM(f, n) (*(void **)(&mi_A.f) = dlsym(h, n))
static inline void mi_load(void) {
    if (mi_A.ok) return;
    void *h = dlopen("libasound.so.2", RTLD_NOW);
    if (!h) { mi_A.ok = -1; return; }
    MI_SYM(open, "snd_seq_open"); MI_SYM(set_name, "snd_seq_set_client_name");
    MI_SYM(port, "snd_seq_create_simple_port"); MI_SYM(nonblock, "snd_seq_nonblock");
    MI_SYM(input, "snd_seq_event_input"); MI_SYM(close, "snd_seq_close");
    mi_A.ok = (mi_A.open && mi_A.set_name && mi_A.port && mi_A.nonblock && mi_A.input && mi_A.close) ? 1 : -1;
}

static int mi_instances;
static inline void mi_open(mi_port_t *p, const char *name) {
    memset(p, 0, sizeof *p);
    mi_load();
    /* SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK: reading must never wait (it happens on the audio thread) */
    if (mi_A.ok != 1 || mi_A.open(&p->seq, "default", 2, 1) < 0) { p->seq = NULL; return; }
    char n[64];
    int k = ++mi_instances;
    if (k > 1) snprintf(n, sizeof n, "%s %d", name, k);
    else snprintf(n, sizeof n, "%s", name);
    mi_A.set_name(p->seq, n);
    p->port = mi_A.port(p->seq, "MIDI In", (1u << 1) | (1u << 6) /* WRITE | SUBS_WRITE */,
                        (1u << 1) | (1u << 20) /* MIDI_GENERIC | APPLICATION */);
    mi_A.nonblock(p->seq, 1);
    if (p->port < 0) { mi_A.close(p->seq); p->seq = NULL; }
}

/* One sequencer event as a 3-byte MIDI message; 0 if it is not one we pass on. */
static inline int mi_decode(const mi_ev_t *ev, uint8_t m[3]) {
    unsigned char ch = ev->data[0] & 0x0F;
    int v;
    switch (ev->type) {
        case 6:   /* NOTEON: channel, note, velocity */
            m[0] = (uint8_t)(0x90 | ch); m[1] = ev->data[1] & 0x7F; m[2] = ev->data[2] & 0x7F; return 3;
        case 7:   /* NOTEOFF */
            m[0] = (uint8_t)(0x80 | ch); m[1] = ev->data[1] & 0x7F; m[2] = ev->data[2] & 0x7F; return 3;
        case 10:  /* CONTROLLER: channel, 3 unused, unsigned param, int value */
            memcpy(&v, ev->data + 4, 4); m[1] = (uint8_t)(v & 0x7F);
            memcpy(&v, ev->data + 8, 4); m[2] = (uint8_t)(v & 0x7F);
            m[0] = (uint8_t)(0xB0 | ch); return 3;
        case 12:  /* CHANPRESS: value */
            memcpy(&v, ev->data + 8, 4); m[0] = (uint8_t)(0xD0 | ch); m[1] = (uint8_t)(v & 0x7F); m[2] = 0; return 3;
        case 13:  /* PITCHBEND: value -8192..8191 */
            memcpy(&v, ev->data + 8, 4); v += 8192; if (v < 0) v = 0; if (v > 16383) v = 16383;
            m[0] = (uint8_t)(0xE0 | ch); m[1] = (uint8_t)(v & 0x7F); m[2] = (uint8_t)(v >> 7); return 3;
    }
    return 0;
}

static inline void mi_poll(mi_port_t *p, void (*cb)(void *, const uint8_t *, int), void *user) {
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("MIDIIN_DEBUG") != NULL;
    if (!p->seq) return;
    for (int n = 0; n < 64; ++n) {   /* at most 64 events a render, so a flood can't hold the audio thread */
        mi_ev_t *ev = NULL;
        if (mi_A.input(p->seq, &ev) < 0 || !ev) break;   /* -EAGAIN: nothing waiting */
        uint8_t m[3];
        if (mi_decode(ev, m)) {
            if (dbg) fprintf(stderr, "midi in: %02X %02X %02X\n", m[0], m[1], m[2]);
            cb(user, m, 3);
        }
    }
}

static inline void mi_close(mi_port_t *p) {
    if (p->seq) mi_A.close(p->seq);
    memset(p, 0, sizeof *p);
}
