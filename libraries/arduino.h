#ifndef _PDE_LIB_ARDUINO_H
#define _PDE_LIB_ARDUINO_H

#include "serial.h"

#define ARDUINO_INPUT 0
#define ARDUINO_OUTPUT 1
#define ARDUINO_LOW 0
#define ARDUINO_HIGH 1

#ifndef INPUT
#define INPUT 0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif
#ifndef LOW
#define LOW 0
#endif
#ifndef HIGH
#define HIGH 1
#endif

typedef struct Arduino Arduino;

struct Arduino {
  Serial serial;
  int pinModes[64];
  int pinValues[64];
  int analogValues[64];

  void (*pinMode)(int pin, int mode);
  void (*digitalWrite)(int pin, int val);
  int (*digitalRead)(int pin);
  int (*analogRead)(int pin);
  void (*analogWrite)(int pin, int val);
};

static Arduino *_pde_last_arduino_ctx = NULL;

static inline void _ard_fn_pinMode(int pin, int mode);
static inline void _ard_fn_digitalWrite(int pin, int val);
static inline int _ard_fn_digitalRead(int pin);
static inline int _ard_fn_analogRead(int pin);
static inline void _ard_fn_analogWrite(int pin, int val);

#define Arduino_list Serial_list

static inline void Arduino_pinMode(Arduino *a, int pin, int mode) {
  if (a && pin >= 0 && pin < 64) {
    a->pinModes[pin] = mode;
    _pde_last_arduino_ctx = a;
  }
}

static inline void Arduino_digitalWrite(Arduino *a, int pin, int val) {
  if (a && pin >= 0 && pin < 64) {
    a->pinValues[pin] = val;
    _pde_last_arduino_ctx = a;
  }
}

static inline int Arduino_digitalRead(Arduino *a, int pin) {
  if (a && pin >= 0 && pin < 64) {
    _pde_last_arduino_ctx = a;
    return a->pinValues[pin];
  }
  return 0;
}

static inline int Arduino_analogRead(Arduino *a, int pin) {
  if (a && pin >= 0 && pin < 64) {
    _pde_last_arduino_ctx = a;
    return a->analogValues[pin];
  }
  return 0;
}

static inline void Arduino_analogWrite(Arduino *a, int pin, int val) {
  if (a && pin >= 0 && pin < 64) {
    a->analogValues[pin] = val;
    a->pinValues[pin] = val;
    _pde_last_arduino_ctx = a;
  }
}

static inline void _ard_fn_pinMode(int pin, int mode) {
  Arduino_pinMode(_pde_last_arduino_ctx, pin, mode);
}
static inline void _ard_fn_digitalWrite(int pin, int val) {
  Arduino_digitalWrite(_pde_last_arduino_ctx, pin, val);
}
static inline int _ard_fn_digitalRead(int pin) {
  return Arduino_digitalRead(_pde_last_arduino_ctx, pin);
}
static inline int _ard_fn_analogRead(int pin) {
  return Arduino_analogRead(_pde_last_arduino_ctx, pin);
}
static inline void _ard_fn_analogWrite(int pin, int val) {
  Arduino_analogWrite(_pde_last_arduino_ctx, pin, val);
}

static inline Arduino Arduino_ctor_impl(void *parent, const char *port, int baud) {
  Arduino a;
  memset(&a, 0, sizeof(a));
  a.serial = Serial_ctor_impl(parent, port, baud);
  a.pinMode = _ard_fn_pinMode;
  a.digitalWrite = _ard_fn_digitalWrite;
  a.digitalRead = _ard_fn_digitalRead;
  a.analogRead = _ard_fn_analogRead;
  a.analogWrite = _ard_fn_analogWrite;
  _pde_last_arduino_ctx = NULL;
  return a;
}

static inline Arduino _ard_ctor_2(const char *port, int baud) {
  return Arduino_ctor_impl(NULL, port, baud);
}
static inline Arduino _ard_ctor_3(void *parent, const char *port, int baud) {
  return Arduino_ctor_impl(parent, port, baud);
}

#define _ARD_CTOR_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define Arduino_ctor(...) _ARD_CTOR_CHOOSER(__VA_ARGS__, _ard_ctor_3, _ard_ctor_2)(__VA_ARGS__)

#endif // _PDE_LIB_ARDUINO_H
