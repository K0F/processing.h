#ifndef _PDE_LIB_MIDI_H
#define _PDE_LIB_MIDI_H

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

#endif
