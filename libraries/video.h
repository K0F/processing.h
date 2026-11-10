#ifndef _PDE_LIB_VIDEO_H
#define _PDE_LIB_VIDEO_H

#include "processing.h"

typedef Image GSPipeline;
typedef Image Movie;

static inline GSPipeline GSPipeline_ctor_impl(void *parent, ...) {
  (void)parent;
  return createImage(320, 240, RGB);
}
#define GSPipeline_ctor(...) GSPipeline_ctor_impl(0, ##__VA_ARGS__)
#define Movie_ctor(...) GSPipeline_ctor_impl(0, ##__VA_ARGS__)

static inline bool _pde_video_available(void *p) { (void)p; return false; }
static inline void _pde_video_read(void *p) { (void)p; }
static inline void _pde_video_loop(void *p) { (void)p; }
static inline void _pde_video_play(void *p) { (void)p; }
static inline void _pde_video_stop(void *p) { (void)p; }
static inline void _pde_video_pause(void *p) { (void)p; }
static inline float _pde_video_time(void *p) { (void)p; return 0.0f; }
static inline float _pde_video_duration(void *p) { (void)p; return 0.0f; }

#endif
