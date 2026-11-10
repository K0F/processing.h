#ifndef _PDE_LIB_MIDI_H
#define _PDE_LIB_MIDI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _PDE_MIDI_DEFINED
#define _PDE_MIDI_DEFINED

typedef struct {
  void (*openInput)(int port, int channel);
  void (*openOutput)(int port, int channel);
  void (*sendNoteOn)(int note, int vel);
  void (*sendNoteOff)(int note, int vel);
  void (*sendController)(int num, int val);
} MidiIO;

static inline void _midi_noop(int a, int b) { (void)a; (void)b; }
static inline void _midi_noop_open(int a, int b) { (void)a; (void)b; }

static inline MidiIO MidiIO_getInstance_impl(void *parent) {
  (void)parent;
  MidiIO m;
  m.openInput = _midi_noop_open;
  m.openOutput = _midi_noop_open;
  m.sendNoteOn = _midi_noop;
  m.sendNoteOff = _midi_noop;
  m.sendController = _midi_noop;
  return m;
}
#define MidiIO_getInstance(...) MidiIO_getInstance_impl(0)

typedef struct {
  void (*sendNote)(int note, int vel, int dur);
  void (*sendNoteOn)(int note, int vel);
  void (*sendNoteOff)(int note, int vel);
} MidiOut;

static inline void _midi_out_note(int a, int b, int c) { (void)a; (void)b; (void)c; }
static inline MidiOut MidiOut_ctor_impl(void *parent, ...) {
  (void)parent;
  MidiOut o;
  o.sendNote = _midi_out_note;
  o.sendNoteOn = _midi_noop;
  o.sendNoteOff = _midi_noop;
  return o;
}
#define MidiOut_ctor(...) MidiOut_ctor_impl(0, ##__VA_ARGS__)

// MidiBus support (themidibus Processing library)
typedef struct _MidiBus MidiBus;

struct _MidiBus {
  int inDevice;
  int outDevice;
  char inDeviceName[128];
  char outDeviceName[128];
  int lastChannel;
  int lastPitch;
  int lastVelocity;
  int lastControllerNum;
  int lastControllerVal;
  void (*sendNoteOn)(int channel, int pitch, int velocity);
  void (*sendNoteOff)(int channel, int pitch, int velocity);
  void (*sendControllerChange)(int channel, int number, int value);
  void (*close)(void);
};

static MidiBus *_pde_last_midibus_ctx = NULL;

static inline void _midibus_fn_noteOn(int channel, int pitch, int velocity) {
  if (_pde_last_midibus_ctx) {
    _pde_last_midibus_ctx->lastChannel = channel;
    _pde_last_midibus_ctx->lastPitch = pitch;
    _pde_last_midibus_ctx->lastVelocity = velocity;
  }
}

static inline void _midibus_fn_noteOff(int channel, int pitch, int velocity) {
  if (_pde_last_midibus_ctx) {
    _pde_last_midibus_ctx->lastChannel = channel;
    _pde_last_midibus_ctx->lastPitch = pitch;
    _pde_last_midibus_ctx->lastVelocity = velocity;
  }
}

static inline void _midibus_fn_cc(int channel, int number, int value) {
  if (_pde_last_midibus_ctx) {
    _pde_last_midibus_ctx->lastChannel = channel;
    _pde_last_midibus_ctx->lastControllerNum = number;
    _pde_last_midibus_ctx->lastControllerVal = value;
  }
}

static inline void _midibus_fn_close(void) {}

static inline void MidiBus_sendNoteOn(MidiBus *b, int channel, int pitch, int velocity) {
  if (!b) return;
  b->lastChannel = channel;
  b->lastPitch = pitch;
  b->lastVelocity = velocity;
  _pde_last_midibus_ctx = b;
}

static inline void MidiBus_sendNoteOff(MidiBus *b, int channel, int pitch, int velocity) {
  if (!b) return;
  b->lastChannel = channel;
  b->lastPitch = pitch;
  b->lastVelocity = velocity;
  _pde_last_midibus_ctx = b;
}

static inline void MidiBus_sendControllerChange(MidiBus *b, int channel, int number, int value) {
  if (!b) return;
  b->lastChannel = channel;
  b->lastControllerNum = number;
  b->lastControllerVal = value;
  _pde_last_midibus_ctx = b;
}

static inline void MidiBus_close(MidiBus *b) {
  (void)b;
}

static inline MidiBus MidiBus_ctor_impl(void *parent, int inDevice, int outDevice) {
  (void)parent;
  MidiBus b;
  memset(&b, 0, sizeof(b));
  b.inDevice = inDevice;
  b.outDevice = outDevice;
  b.sendNoteOn = _midibus_fn_noteOn;
  b.sendNoteOff = _midibus_fn_noteOff;
  b.sendControllerChange = _midibus_fn_cc;
  b.close = _midibus_fn_close;
  _pde_last_midibus_ctx = NULL;
  return b;
}

static inline MidiBus MidiBus_ctor_str_impl(void *parent, const char *inDev, const char *outDev) {
  (void)parent;
  MidiBus b;
  memset(&b, 0, sizeof(b));
  b.inDevice = -1;
  b.outDevice = -1;
  if (inDev) snprintf(b.inDeviceName, sizeof(b.inDeviceName), "%s", inDev);
  if (outDev) snprintf(b.outDeviceName, sizeof(b.outDeviceName), "%s", outDev);
  b.sendNoteOn = _midibus_fn_noteOn;
  b.sendNoteOff = _midibus_fn_noteOff;
  b.sendControllerChange = _midibus_fn_cc;
  b.close = _midibus_fn_close;
  _pde_last_midibus_ctx = NULL;
  return b;
}

static inline MidiBus _midibus_ctor_int(void *p, int in, int out) {
  return MidiBus_ctor_impl(p, in, out);
}
static inline MidiBus _midibus_ctor_str(void *p, const char *in, const char *out) {
  return MidiBus_ctor_str_impl(p, in, out);
}

#define MidiBus_ctor(p, in, out) _Generic((in), \
  const char *: _midibus_ctor_str, \
  char *: (MidiBus (*)(void *, const char *, const char *))_midibus_ctor_str, \
  default: _midibus_ctor_int \
)((p), (in), (out))

static const char *_pde_midi_devices[4] = { "MIDI Input 1", "MIDI Output 1", NULL, NULL };
static inline const char **MidiBus_list(void) {
  return _pde_midi_devices;
}

#endif // _PDE_MIDI_DEFINED

#endif // _PDE_LIB_MIDI_H
