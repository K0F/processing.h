#ifndef _PDE_LIB_GEOMERATIVE_H
#define _PDE_LIB_GEOMERATIVE_H

typedef struct { float x, y; } RPoint;

typedef struct {
  RPoint *(*getPoints)(void);
  int (*countPaths)(void);
} RShape;

static inline RPoint *_rs_getPoints(void) { static RPoint pts[1] = {{0,0}}; return pts; }
static inline int _rs_countPaths(void) { return 0; }
static inline RShape RShape_ctor_impl(void *parent, ...) {
  (void)parent;
  RShape s;
  s.getPoints = _rs_getPoints;
  s.countPaths = _rs_countPaths;
  return s;
}
#define RShape_ctor(...) RShape_ctor_impl(0, ##__VA_ARGS__)

typedef struct { void (*init)(void *parent); } _Pde_RG;
static inline void _rg_init(void *p) { (void)p; }
static _Pde_RG RG = { .init = _rg_init };

#endif
