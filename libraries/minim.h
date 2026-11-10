#ifndef _PDE_LIB_MINIM_H
#define _PDE_LIB_MINIM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <raylib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef _PDE_MINIM_DEFINED
#define _PDE_MINIM_DEFINED

#ifndef STEREO
#define STEREO 2
#endif
#ifndef MONO
#define MONO 1
#endif

// Forward declarations
typedef struct _AudioPlayer AudioPlayer;
typedef struct _AudioInput AudioInput;
typedef struct _AudioSample AudioSample;
typedef struct _FFT FFT;

// AudioBuffer (left, right, mix)
typedef struct AudioBuffer {
  float *samples;
  int size;
  float (*get)(int idx);
} AudioBuffer;

static inline float _ab_sample_get(int idx);

static AudioBuffer *_pde_active_ab = NULL;

static inline float _ab_sample_get(int idx) {
  if (_pde_active_ab && _pde_active_ab->samples && idx >= 0 && idx < _pde_active_ab->size) {
    return _pde_active_ab->samples[idx];
  }
  return 0.0f;
}

static inline float AudioBuffer_get(const AudioBuffer *b, int idx) {
  if (b && b->samples && idx >= 0 && idx < b->size) {
    return b->samples[idx];
  }
  return 0.0f;
}

// AudioStream
typedef struct {
  bool smoothPan;
  int size;
  float bufferStartTime;
  float duration;
  float *buffer2;
  float sampleRate;
  void (*start)(void);
  void (*stop)(void);
} _Pde_AudioStream;
#define AudioStream _Pde_AudioStream

static inline void _astream_void(void) {}

static inline _Pde_AudioStream AudioStream_ctor(void) {
  _Pde_AudioStream s;
  memset(&s, 0, sizeof(s));
  s.size = 1024;
  s.sampleRate = 44100.0f;
  s.duration = 1.0f;
  s.buffer2 = (float *)calloc(s.size, sizeof(float));
  s.start = _astream_void;
  s.stop = _astream_void;
  return s;
}

// FadeOut and FadeIn
typedef struct {
  void (*filter)(AudioStream s);
} FadeOut;

static inline void _fade_filter(AudioStream s) { (void)s; }
static inline FadeOut FadeOut_ctor(void) {
  FadeOut f;
  f.filter = _fade_filter;
  return f;
}
#define FadeOut(...) FadeOut_ctor()

typedef struct {
  void (*filter)(AudioStream s);
} FadeIn;

static inline FadeIn FadeIn_ctor(void) {
  FadeIn f;
  f.filter = _fade_filter;
  return f;
}
#define FadeIn(...) FadeIn_ctor()

// SineWave
typedef struct {
  float phase;
  float frequency;
  float amplitude;
  float sampleRate;
  void (*setFreq)(float f);
  void (*generate)(AudioStream s, ...);
} SineWave;

static SineWave *_pde_last_sinewave_ctx = NULL;

static inline void _sw_fn_setFreq(float f) {
  if (_pde_last_sinewave_ctx) _pde_last_sinewave_ctx->frequency = f;
}
static inline void _sw_fn_generate(AudioStream s, ...) { (void)s; }

static inline SineWave SineWave_ctor_impl(float freq, float amp, float sampleRate) {
  SineWave sw;
  memset(&sw, 0, sizeof(sw));
  sw.frequency = freq;
  sw.amplitude = amp;
  sw.sampleRate = (sampleRate > 0.0f) ? sampleRate : 44100.0f;
  sw.setFreq = _sw_fn_setFreq;
  sw.generate = _sw_fn_generate;
  _pde_last_sinewave_ctx = NULL;
  return sw;
}
#define SineWave_ctor(...) SineWave_ctor_impl(__VA_ARGS__)
#define SineWave(...) SineWave_ctor_impl(__VA_ARGS__)

static inline void _ap_addsignal(SineWave sig, ...) { (void)sig; }

// AudioPlayer
struct _AudioPlayer {
  AudioBuffer left;
  AudioBuffer right;
  AudioBuffer mix;
  int (*bufferSize)(void);
  float (*sampleRate)(void);
  void (*addSignal)(SineWave sig, ...);
  void (*play)(void);
  void (*pause)(void);
  void (*loop)(void);
  void (*rewind)(void);
  bool (*isPlaying)(void);
  int (*length)(void);
  int (*position)(void);
  void (*close)(void);

  // Internal state and raylib Music context
  Music music;
  bool isLoaded;
  bool isPlayingState;
  bool isLoopingState;
  bool isPausedState;
  int simulatedLengthMs;
  int simulatedPosMs;
  int bufLen;
  float sampleRateVal;
  char filePath[256];
};

static AudioPlayer *_pde_last_player_ctx = NULL;

static inline void AudioPlayer_play(AudioPlayer *p) {
  if (!p) return;
  _pde_last_player_ctx = p;
  p->isPlayingState = true;
  p->isPausedState = false;
  if (p->isLoaded && IsAudioDeviceReady()) {
    if (p->isPausedState) ResumeMusicStream(p->music);
    else PlayMusicStream(p->music);
  }
}

static inline void AudioPlayer_pause(AudioPlayer *p) {
  if (!p) return;
  _pde_last_player_ctx = p;
  p->isPlayingState = false;
  p->isPausedState = true;
  if (p->isLoaded && IsAudioDeviceReady()) {
    PauseMusicStream(p->music);
  }
}

static inline void AudioPlayer_loop(AudioPlayer *p) {
  if (!p) return;
  _pde_last_player_ctx = p;
  p->isPlayingState = true;
  p->isLoopingState = true;
  p->isPausedState = false;
  if (p->isLoaded && IsAudioDeviceReady()) {
    p->music.looping = true;
    PlayMusicStream(p->music);
  }
}

static inline void AudioPlayer_rewind(AudioPlayer *p) {
  if (!p) return;
  _pde_last_player_ctx = p;
  p->simulatedPosMs = 0;
  if (p->isLoaded && IsAudioDeviceReady()) {
    SeekMusicStream(p->music, 0.0f);
  }
}

static inline bool AudioPlayer_isPlaying(const AudioPlayer *p) {
  if (!p) return false;
  if (p->isLoaded && IsAudioDeviceReady()) {
    return IsMusicStreamPlaying(p->music);
  }
  return p->isPlayingState;
}

static inline int AudioPlayer_length(const AudioPlayer *p) {
  if (!p) return 0;
  if (p->isLoaded && IsAudioDeviceReady()) {
    return (int)(GetMusicTimeLength(p->music) * 1000.0f);
  }
  return p->simulatedLengthMs;
}

static inline int AudioPlayer_position(const AudioPlayer *p) {
  if (!p) return 0;
  if (p->isLoaded && IsAudioDeviceReady()) {
    return (int)(GetMusicTimePlayed(p->music) * 1000.0f);
  }
  return p->simulatedPosMs;
}

static inline void AudioPlayer_close(AudioPlayer *p) {
  if (!p) return;
  if (p->isLoaded && IsAudioDeviceReady()) {
    StopMusicStream(p->music);
    UnloadMusicStream(p->music);
    p->isLoaded = false;
  }
  p->isPlayingState = false;
  if (p->mix.samples) { free(p->mix.samples); p->mix.samples = NULL; }
  if (p->left.samples) { free(p->left.samples); p->left.samples = NULL; }
  if (p->right.samples) { free(p->right.samples); p->right.samples = NULL; }
}

static inline int _ap_fn_bufsize(void) {
  return _pde_last_player_ctx ? _pde_last_player_ctx->bufLen : 1024;
}
static inline float _ap_fn_samplerate(void) {
  return _pde_last_player_ctx ? _pde_last_player_ctx->sampleRateVal : 44100.0f;
}
static inline void _ap_fn_play(void) { AudioPlayer_play(_pde_last_player_ctx); }
static inline void _ap_fn_pause(void) { AudioPlayer_pause(_pde_last_player_ctx); }
static inline void _ap_fn_loop(void) { AudioPlayer_loop(_pde_last_player_ctx); }
static inline void _ap_fn_rewind(void) { AudioPlayer_rewind(_pde_last_player_ctx); }
static inline bool _ap_fn_isPlaying(void) { return AudioPlayer_isPlaying(_pde_last_player_ctx); }
static inline int _ap_fn_length(void) { return AudioPlayer_length(_pde_last_player_ctx); }
static inline int _ap_fn_position(void) { return AudioPlayer_position(_pde_last_player_ctx); }
static inline void _ap_fn_close(void) { AudioPlayer_close(_pde_last_player_ctx); }

static inline AudioPlayer AudioPlayer_loadFile(const char *path) {
  AudioPlayer p;
  memset(&p, 0, sizeof(p));
  p.bufLen = 1024;
  p.sampleRateVal = 44100.0f;
  p.simulatedLengthMs = 1000;

  p.mix.size = p.bufLen;
  p.mix.samples = (float *)calloc(p.bufLen, sizeof(float));
  p.mix.get = _ab_sample_get;

  p.left.size = p.bufLen;
  p.left.samples = (float *)calloc(p.bufLen, sizeof(float));
  p.left.get = _ab_sample_get;

  p.right.size = p.bufLen;
  p.right.samples = (float *)calloc(p.bufLen, sizeof(float));
  p.right.get = _ab_sample_get;

  p.bufferSize = _ap_fn_bufsize;
  p.sampleRate = _ap_fn_samplerate;
  p.addSignal = _ap_addsignal;
  p.play = _ap_fn_play;
  p.pause = _ap_fn_pause;
  p.loop = _ap_fn_loop;
  p.rewind = _ap_fn_rewind;
  p.isPlaying = _ap_fn_isPlaying;
  p.length = _ap_fn_length;
  p.position = _ap_fn_position;
  p.close = _ap_fn_close;

  if (path && strlen(path) > 0) {
    snprintf(p.filePath, sizeof(p.filePath), "%s", path);
    if (IsAudioDeviceReady()) {
      p.music = LoadMusicStream(path);
      if (p.music.ctxData != NULL) {
        p.isLoaded = true;
      }
    }
  }

  _pde_last_player_ctx = NULL;
  return p;
}

// AudioInput
struct _AudioInput {
  AudioBuffer left;
  AudioBuffer right;
  AudioBuffer mix;
  int (*bufferSize)(void);
  float (*sampleRate)(void);
  void (*close)(void);
};

static inline void AudioInput_close(AudioInput *ai) {
  if (!ai) return;
  if (ai->mix.samples) { free(ai->mix.samples); ai->mix.samples = NULL; }
  if (ai->left.samples) { free(ai->left.samples); ai->left.samples = NULL; }
  if (ai->right.samples) { free(ai->right.samples); ai->right.samples = NULL; }
}

static inline AudioInput AudioInput_getLineIn_impl(int type, int bufferSize, float sampleRate, int bitDepth) {
  (void)type; (void)sampleRate; (void)bitDepth;
  AudioInput ai;
  memset(&ai, 0, sizeof(ai));
  int bsize = (bufferSize > 0) ? bufferSize : 1024;
  ai.mix.size = bsize;
  ai.mix.samples = (float *)calloc(bsize, sizeof(float));
  ai.mix.get = _ab_sample_get;

  ai.left.size = bsize;
  ai.left.samples = (float *)calloc(bsize, sizeof(float));
  ai.left.get = _ab_sample_get;

  ai.right.size = bsize;
  ai.right.samples = (float *)calloc(bsize, sizeof(float));
  ai.right.get = _ab_sample_get;

  ai.bufferSize = _ap_fn_bufsize;
  ai.sampleRate = _ap_fn_samplerate;
  ai.close = _ap_fn_close;
  return ai;
}

#define AudioInput_getLineIn(...) AudioInput_getLineIn_impl(0, 1024, 44100.0f, 16)

// AudioSample
struct _AudioSample {
  Sound sound;
  bool isLoaded;
  bool isPlayingState;
  char filePath[256];
  void (*trigger)(void);
  void (*stop)(void);
};

static AudioSample *_pde_last_sample_ctx = NULL;

static inline void AudioSample_trigger(AudioSample *as) {
  if (!as) return;
  _pde_last_sample_ctx = as;
  as->isPlayingState = true;
  if (as->isLoaded && IsAudioDeviceReady()) {
    PlaySound(as->sound);
  }
}

static inline void AudioSample_stop(AudioSample *as) {
  if (!as) return;
  _pde_last_sample_ctx = as;
  as->isPlayingState = false;
  if (as->isLoaded && IsAudioDeviceReady()) {
    StopSound(as->sound);
  }
}

static inline void AudioSample_close(AudioSample *as) {
  if (!as) return;
  if (as->isLoaded && IsAudioDeviceReady()) {
    StopSound(as->sound);
    UnloadSound(as->sound);
    as->isLoaded = false;
  }
  as->isPlayingState = false;
}

static inline void _as_fn_trigger(void) { AudioSample_trigger(_pde_last_sample_ctx); }
static inline void _as_fn_stop(void) { AudioSample_stop(_pde_last_sample_ctx); }

static inline AudioSample AudioSample_loadSample(const char *path) {
  AudioSample as;
  memset(&as, 0, sizeof(as));
  if (path && strlen(path) > 0) {
    snprintf(as.filePath, sizeof(as.filePath), "%s", path);
    if (IsAudioDeviceReady()) {
      as.sound = LoadSound(path);
      if (as.sound.frameCount > 0) as.isLoaded = true;
    }
  }
  as.trigger = _as_fn_trigger;
  as.stop = _as_fn_stop;
  _pde_last_sample_ctx = NULL;
  return as;
}

typedef AudioPlayer AudioOutput;
typedef AudioPlayer AudioSnippet;

// FFT Analysis
struct _FFT {
  int timeSize;
  float sampleRate;
  int numBands;
  float *real;
  float *imag;
  float *spectrum;

  void (*forward)(AudioBuffer b);
  float (*getBand)(int i);
  int (*specSize)(void);
};

static FFT *_pde_active_fft_ctx = NULL;

static inline void _pde_fft_calc(float *real, float *imag, int n) {
  // Bit-reversal permutation
  int j = 0;
  for (int i = 0; i < n - 1; i++) {
    if (i < j) {
      float tr = real[i]; real[i] = real[j]; real[j] = tr;
      float ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
    }
    int k = n >> 1;
    while (k <= j) {
      j -= k;
      k >>= 1;
    }
    j += k;
  }
  // Cooley-Tukey decimation-in-time radix-2
  for (int len = 2; len <= n; len <<= 1) {
    float angle = -2.0f * (float)M_PI / (float)len;
    float wstep_r = cosf(angle);
    float wstep_i = sinf(angle);
    for (int i = 0; i < n; i += len) {
      float wr = 1.0f;
      float wi = 0.0f;
      int half = len >> 1;
      for (int k = 0; k < half; k++) {
        float ur = real[i + k];
        float ui = imag[i + k];
        float vr = real[i + k + half] * wr - imag[i + k + half] * wi;
        float vi = real[i + k + half] * wi + imag[i + k + half] * wr;
        real[i + k] = ur + vr;
        imag[i + k] = ui + vi;
        real[i + k + half] = ur - vr;
        imag[i + k + half] = ui - vi;
        float next_wr = wr * wstep_r - wi * wstep_i;
        wi = wr * wstep_i + wi * wstep_r;
        wr = next_wr;
      }
    }
  }
}

static inline int FFT_specSize(const FFT *f) {
  return f ? f->numBands : 0;
}

static inline float FFT_getBand(const FFT *f, int i) {
  if (!f || !f->spectrum || i < 0 || i >= f->numBands) return 0.0f;
  return f->spectrum[i];
}

static inline void FFT_forward_floats(FFT *f, const float *buf, int len) {
  if (!f || !buf || len <= 0) return;
  int n = f->timeSize;
  int copyLen = (len < n) ? len : n;
  for (int i = 0; i < copyLen; i++) {
    f->real[i] = buf[i];
    f->imag[i] = 0.0f;
  }
  for (int i = copyLen; i < n; i++) {
    f->real[i] = 0.0f;
    f->imag[i] = 0.0f;
  }
  _pde_fft_calc(f->real, f->imag, n);
  for (int i = 0; i < f->numBands; i++) {
    float r = f->real[i];
    float im = f->imag[i];
    f->spectrum[i] = sqrtf(r * r + im * im) * (2.0f / (float)n);
  }
  _pde_active_fft_ctx = f;
}

static inline void FFT_forward_buf(FFT *f, AudioBuffer b) {
  FFT_forward_floats(f, b.samples, b.size);
}

static inline void FFT_forward_buf_ptr(FFT *f, const AudioBuffer *b) {
  if (b) FFT_forward_floats(f, b->samples, b->size);
}

static inline void FFT_forward_floats_auto(FFT *f, const float *buf) {
  if (f && buf) FFT_forward_floats(f, buf, f->timeSize);
}

#define FFT_forward(fft, buf) _Generic((buf), \
  AudioBuffer: FFT_forward_buf, \
  AudioBuffer *: FFT_forward_buf_ptr, \
  float *: FFT_forward_floats_auto, \
  const float *: FFT_forward_floats_auto \
)((fft), (buf))

static inline void _fft_fn_forward(AudioBuffer b) {
  if (_pde_active_fft_ctx) FFT_forward_buf(_pde_active_fft_ctx, b);
}

static inline float _fft_fn_getBand(int i) {
  return FFT_getBand(_pde_active_fft_ctx, i);
}

static inline int _fft_fn_specSize(void) {
  return FFT_specSize(_pde_active_fft_ctx);
}

static inline void FFT_close(FFT *f) {
  if (!f) return;
  if (f->real) { free(f->real); f->real = NULL; }
  if (f->imag) { free(f->imag); f->imag = NULL; }
  if (f->spectrum) { free(f->spectrum); f->spectrum = NULL; }
}

static inline FFT FFT_ctor_impl(int timeSize, float sampleRate) {
  FFT f;
  memset(&f, 0, sizeof(f));
  f.timeSize = (timeSize > 0) ? timeSize : 512;
  f.sampleRate = (sampleRate > 0.0f) ? sampleRate : 44100.0f;
  f.numBands = f.timeSize / 2 + 1;

  f.real = (float *)calloc(f.timeSize, sizeof(float));
  f.imag = (float *)calloc(f.timeSize, sizeof(float));
  f.spectrum = (float *)calloc(f.numBands, sizeof(float));

  f.forward = _fft_fn_forward;
  f.getBand = _fft_fn_getBand;
  f.specSize = _fft_fn_specSize;
  _pde_active_fft_ctx = NULL;
  return f;
}

static inline FFT _pde_fft_ctor_2(int timeSize, float sampleRate) {
  return FFT_ctor_impl(timeSize, sampleRate);
}
static inline FFT _pde_fft_ctor_3(void *parent, int timeSize, float sampleRate) {
  (void)parent;
  return FFT_ctor_impl(timeSize, sampleRate);
}
#define _FFT_CTOR_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define FFT_ctor(...) _FFT_CTOR_CHOOSER(__VA_ARGS__, _pde_fft_ctor_3, _pde_fft_ctor_2)(__VA_ARGS__)

// Minim class
typedef struct {
  AudioInput (*getLineIn)();
  AudioOutput (*getLineOut)();
  AudioPlayer (*loadFile)(const char *path);
  AudioSample (*loadSample)(const char *path);
  AudioSnippet (*loadSnippet)(const char *path);
  void (*stop)(void);
  void (*debugOn)(void);
  void (*debugOff)(void);
} Minim;

static inline AudioInput _minim_linein_call() {
  return AudioInput_getLineIn_impl(0, 1024, 44100.0f, 16);
}
static inline AudioOutput _minim_lineout_call() {
  return AudioPlayer_loadFile("");
}
static inline AudioPlayer _minim_load_call(const char *path) {
  return AudioPlayer_loadFile(path);
}
static inline AudioSample _minim_load_sample_call(const char *path) {
  return AudioSample_loadSample(path);
}

static inline void _minim_noop(void) {}

static inline Minim Minim_ctor_impl(void *parent) {
  (void)parent;
  if (!IsAudioDeviceReady()) {
    InitAudioDevice();
  }
  Minim m;
  m.getLineIn = _minim_linein_call;
  m.getLineOut = _minim_lineout_call;
  m.loadFile = _minim_load_call;
  m.loadSample = _minim_load_sample_call;
  m.loadSnippet = _minim_load_call;
  m.stop = _minim_noop;
  m.debugOn = _minim_noop;
  m.debugOff = _minim_noop;
  return m;
}

#define Minim_ctor(...) Minim_ctor_impl(0)

static inline void Minim_stop(Minim *m) {
  (void)m;
  if (IsAudioDeviceReady()) {
    CloseAudioDevice();
  }
}

#endif // _PDE_MINIM_DEFINED

#endif // _PDE_LIB_MINIM_H
