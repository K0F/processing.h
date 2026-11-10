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

typedef struct {
  Serial serial;
  int pinModes[64];
  int pinValues[64];
  int analogValues[16];
} Arduino;

#define Arduino_list Serial_list

static inline Arduino Arduino_ctor_impl(void *parent, const char *port, int baud) {
  Arduino a;
  memset(&a, 0, sizeof(a));
  a.serial = Serial_ctor_impl(parent, port, baud);
  return a;
}
#define Arduino_ctor(...) Arduino_ctor_impl(0, ##__VA_ARGS__)

static inline void Arduino_pinMode(Arduino *a, int pin, int mode) {
  if (a && pin >= 0 && pin < 64) a->pinModes[pin] = mode;
}

static inline void Arduino_digitalWrite(Arduino *a, int pin, int val) {
  if (a && pin >= 0 && pin < 64) a->pinValues[pin] = val;
}

static inline int Arduino_digitalRead(Arduino *a, int pin) {
  if (a && pin >= 0 && pin < 64) return a->pinValues[pin];
  return 0;
}

static inline int Arduino_analogRead(Arduino *a, int pin) {
  if (a && pin >= 0 && pin < 16) return a->analogValues[pin];
  return 0;
}

static inline void Arduino_analogWrite(Arduino *a, int pin, int val) {
  if (a && pin >= 0 && pin < 64) a->pinValues[pin] = val;
}

#endif
