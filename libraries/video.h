#ifndef _PDE_LIB_VIDEO_H
#define _PDE_LIB_VIDEO_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <glob.h>

#ifndef _PDE_VIDEO_DEFINED
#define _PDE_VIDEO_DEFINED

typedef struct _Pde_Video _Pde_Video;

struct _Pde_Video {
  // Raylib Image-compatible header layout:
  void *data;
  int width;
  int height;
  int mipmaps;
  int format;

  // Processing pixel array view
  uint32_t *pixels;

  // Video State
  bool isPlaying;
  bool isLooping;
  bool isAvailable;
  bool isPaused;
  float currentTime;
  float durationSec;
  int fps;
  char deviceOrPath[256];

  // Function pointers matching Processing object method calls
  bool (*available)(void);
  void (*read)(void);
  void (*play)(void);
  void (*loop)(void);
  void (*stop)(void);
  void (*pause)(void);
  float (*time)(void);
  float (*duration)(void);
  void (*loadPixels)(void);
  void (*updatePixels)(void);
  void (*filter)(int kind, ...);
  void (*mask)(void *m);
};

typedef _Pde_Video Capture;
typedef _Pde_Video Movie;
typedef _Pde_Video GSPipeline;

static _Pde_Video *_pde_last_video_ctx = NULL;

static inline bool _pde_video_avail_stub(void) {
  return _pde_last_video_ctx ? _pde_last_video_ctx->isAvailable : true;
}
static inline void _pde_video_read_stub(void) {
  if (_pde_last_video_ctx) _pde_last_video_ctx->isAvailable = false;
}
static inline void _pde_video_play_stub(void) {
  if (_pde_last_video_ctx) {
    _pde_last_video_ctx->isPlaying = true;
    _pde_last_video_ctx->isPaused = false;
  }
}
static inline void _pde_video_loop_stub(void) {
  if (_pde_last_video_ctx) {
    _pde_last_video_ctx->isPlaying = true;
    _pde_last_video_ctx->isLooping = true;
    _pde_last_video_ctx->isPaused = false;
  }
}
static inline void _pde_video_stop_stub(void) {
  if (_pde_last_video_ctx) {
    _pde_last_video_ctx->isPlaying = false;
    _pde_last_video_ctx->isLooping = false;
    _pde_last_video_ctx->isPaused = false;
    _pde_last_video_ctx->currentTime = 0.0f;
  }
}
static inline void _pde_video_pause_stub(void) {
  if (_pde_last_video_ctx) {
    _pde_last_video_ctx->isPlaying = false;
    _pde_last_video_ctx->isPaused = true;
  }
}
static inline float _pde_video_time_stub(void) {
  return _pde_last_video_ctx ? _pde_last_video_ctx->currentTime : 0.0f;
}
static inline float _pde_video_duration_stub(void) {
  return _pde_last_video_ctx ? _pde_last_video_ctx->durationSec : 0.0f;
}
static inline void _pde_video_loadPixels_stub(void) {}
static inline void _pde_video_updatePixels_stub(void) {}
static inline void _pde_video_filter_stub(int kind, ...) { (void)kind; }
static inline void _pde_video_mask_stub(void *m) { (void)m; }

static inline bool _pde_video_available(void *p) {
  if (!p) return false;
  _Pde_Video *v = (_Pde_Video *)p;
  return v->isAvailable;
}

static inline void _pde_video_read(void *p) {
  if (!p) return;
  _Pde_Video *v = (_Pde_Video *)p;
  v->isAvailable = false;
  _pde_last_video_ctx = v;
  if (v->read && v->read != _pde_video_read_stub) v->read();
}

static inline void _pde_video_loop(void *p) {
  if (!p) return;
  _Pde_Video *v = (_Pde_Video *)p;
  v->isPlaying = true;
  v->isLooping = true;
  v->isPaused = false;
  _pde_last_video_ctx = v;
  if (v->loop && v->loop != _pde_video_loop_stub) v->loop();
}

static inline void _pde_video_play(void *p) {
  if (!p) return;
  _Pde_Video *v = (_Pde_Video *)p;
  v->isPlaying = true;
  v->isPaused = false;
  _pde_last_video_ctx = v;
  if (v->play && v->play != _pde_video_play_stub) v->play();
}

static inline void _pde_video_stop(void *p) {
  if (!p) return;
  _Pde_Video *v = (_Pde_Video *)p;
  v->isPlaying = false;
  v->isLooping = false;
  v->isPaused = false;
  v->currentTime = 0.0f;
  _pde_last_video_ctx = v;
  if (v->stop && v->stop != _pde_video_stop_stub) v->stop();
}

static inline void _pde_video_pause(void *p) {
  if (!p) return;
  _Pde_Video *v = (_Pde_Video *)p;
  v->isPlaying = false;
  v->isPaused = true;
  _pde_last_video_ctx = v;
  if (v->pause && v->pause != _pde_video_pause_stub) v->pause();
}

static inline float _pde_video_time(void *p) {
  if (!p) return 0.0f;
  _Pde_Video *v = (_Pde_Video *)p;
  return v->currentTime;
}

static inline float _pde_video_duration(void *p) {
  if (!p) return 0.0f;
  _Pde_Video *v = (_Pde_Video *)p;
  return v->durationSec;
}

static inline void _pde_video_init_fields(_Pde_Video *v, int w, int h, int fps, const char *dev) {
  v->width = (w > 0) ? w : 320;
  v->height = (h > 0) ? h : 240;
  v->mipmaps = 1;
  v->format = 7; // PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
  v->fps = (fps > 0) ? fps : 30;
  v->isAvailable = true;
  v->isPlaying = false;
  v->isLooping = false;
  v->isPaused = false;
  v->currentTime = 0.0f;
  v->durationSec = 0.0f;
  if (dev) snprintf(v->deviceOrPath, sizeof(v->deviceOrPath), "%s", dev);
  size_t sz = (size_t)v->width * v->height;
  v->data = calloc(sz, sizeof(uint32_t));
  v->pixels = (uint32_t *)v->data;

  v->available = _pde_video_avail_stub;
  v->read = _pde_video_read_stub;
  v->play = _pde_video_play_stub;
  v->loop = _pde_video_loop_stub;
  v->stop = _pde_video_stop_stub;
  v->pause = _pde_video_pause_stub;
  v->time = _pde_video_time_stub;
  v->duration = _pde_video_duration_stub;
  v->loadPixels = _pde_video_loadPixels_stub;
  v->updatePixels = _pde_video_updatePixels_stub;
  v->filter = _pde_video_filter_stub;
  v->mask = _pde_video_mask_stub;
}

static inline Capture Capture_ctor_dev(void *parent, int w, int h, const char *dev, int fps) {
  (void)parent;
  Capture c;
  memset(&c, 0, sizeof(c));
  _pde_video_init_fields(&c, w, h, fps, dev);
  c.isPlaying = true;
  _pde_last_video_ctx = NULL;
  return c;
}

static inline Capture Capture_ctor(void *parent, int w, int h, int fps) {
  return Capture_ctor_dev(parent, w, h, NULL, fps);
}

static inline Capture _pde_capture_ctor_3(void *parent, int w, int h) {
  return Capture_ctor_dev(parent, w, h, NULL, 30);
}

#define _CAPTURE_CTOR_CHOOSER(_1, _2, _3, _4, NAME, ...) NAME
#define Capture_ctor(...) _CAPTURE_CTOR_CHOOSER(__VA_ARGS__, Capture_ctor, _pde_capture_ctor_3)(__VA_ARGS__)

static inline Movie Movie_ctor_impl(void *parent, const char *path) {
  (void)parent;
  Movie m;
  memset(&m, 0, sizeof(m));
  _pde_video_init_fields(&m, 320, 240, 30, path);
  m.durationSec = 10.0f;
  _pde_last_video_ctx = NULL;
  return m;
}

static inline Movie _pde_movie_ctor_1(const char *path) {
  return Movie_ctor_impl(NULL, path);
}
static inline Movie _pde_movie_ctor_2(void *parent, const char *path) {
  return Movie_ctor_impl(parent, path);
}
#define _MOVIE_CTOR_CHOOSER(_1, _2, NAME, ...) NAME
#define Movie_ctor(...) _MOVIE_CTOR_CHOOSER(__VA_ARGS__, _pde_movie_ctor_2, _pde_movie_ctor_1)(__VA_ARGS__)
#define GSPipeline_ctor(...) _MOVIE_CTOR_CHOOSER(__VA_ARGS__, _pde_movie_ctor_2, _pde_movie_ctor_1)(__VA_ARGS__)

typedef struct {
  char dir[128];
  char filename[128];
  int frameCount;
  void (*add)(void);
  void (*finish)(void);
} Recorder;

static inline void _recorder_add(void) {}
static inline void _recorder_finish(void) {}

static inline Recorder Recorder_ctor_impl(const char *dir, const char *file) {
  Recorder r;
  memset(&r, 0, sizeof(r));
  if (dir) snprintf(r.dir, sizeof(r.dir), "%s", dir);
  if (file) snprintf(r.filename, sizeof(r.filename), "%s", file);
  r.add = _recorder_add;
  r.finish = _recorder_finish;
  return r;
}
#define Recorder_ctor(...) Recorder_ctor_impl(__VA_ARGS__)

#endif // _PDE_VIDEO_DEFINED

static const char *_pde_capture_devices[64];
static int _pde_num_capture_devices = 0;

static inline const char **Capture_list(void) {
  glob_t g;
  for (int i = 0; i < _pde_num_capture_devices; i++) {
    if (_pde_capture_devices[i]) {
      free((void *)_pde_capture_devices[i]);
      _pde_capture_devices[i] = NULL;
    }
  }
  _pde_num_capture_devices = 0;
  if (glob("/dev/video*", 0, NULL, &g) == 0) {
    for (size_t i = 0; i < g.gl_pathc && _pde_num_capture_devices < 63; i++) {
      _pde_capture_devices[_pde_num_capture_devices++] = strdup(g.gl_pathv[i]);
    }
    globfree(&g);
  }
  _pde_capture_devices[_pde_num_capture_devices] = NULL;
  return _pde_capture_devices;
}

#endif // _PDE_LIB_VIDEO_H
