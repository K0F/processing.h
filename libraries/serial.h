#ifndef _PDE_LIB_SERIAL_H
#define _PDE_LIB_SERIAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <glob.h>

typedef struct {
  int fd;
  int baud;
  char portName[128];
  char buffer[1024];
  int bufLen;
} Serial;

static const char *_pde_serial_ports[32];
static int _pde_num_serial_ports = 0;

static inline const char **Serial_list(void) {
  glob_t g;
  _pde_num_serial_ports = 0;
  if (glob("/dev/ttyUSB*", 0, NULL, &g) == 0) {
    for (size_t i = 0; i < g.gl_pathc && _pde_num_serial_ports < 31; i++) {
      _pde_serial_ports[_pde_num_serial_ports++] = strdup(g.gl_pathv[i]);
    }
    globfree(&g);
  }
  if (glob("/dev/ttyACM*", 0, NULL, &g) == 0) {
    for (size_t i = 0; i < g.gl_pathc && _pde_num_serial_ports < 31; i++) {
      _pde_serial_ports[_pde_num_serial_ports++] = strdup(g.gl_pathv[i]);
    }
    globfree(&g);
  }
  _pde_serial_ports[_pde_num_serial_ports] = NULL;
  return _pde_serial_ports;
}

static inline Serial Serial_ctor_impl(void *parent, const char *port, int baud) {
  (void)parent;
  Serial s;
  memset(&s, 0, sizeof(s));
  s.fd = -1;
  s.baud = baud;
  if (port) {
    snprintf(s.portName, sizeof(s.portName), "%s", port);
    s.fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (s.fd >= 0) {
      struct termios options;
      memset(&options, 0, sizeof(options));
      tcgetattr(s.fd, &options);
      speed_t spd = B9600;
      if (baud == 57600) spd = B57600;
      else if (baud == 115200) spd = B115200;
      else if (baud == 19200) spd = B19200;
      else if (baud == 38400) spd = B38400;
      cfsetispeed(&options, spd);
      cfsetospeed(&options, spd);
      options.c_cflag |= (CLOCAL | CREAD | CS8);
      options.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
      options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
      options.c_iflag &= ~(IXON | IXOFF | IXANY);
      options.c_oflag &= ~OPOST;
      tcsetattr(s.fd, TCSANOW, &options);
    }
  }
  return s;
}
#define Serial_ctor(...) Serial_ctor_impl(0, ##__VA_ARGS__)

static inline int Serial_available(Serial *s) {
  if (!s || s->fd < 0) return 0;
  return 0;
}
static inline int Serial_read(Serial *s) {
  if (!s || s->fd < 0) return -1;
  uint8_t b = 0;
  if (read(s->fd, &b, 1) == 1) return (int)b;
  return -1;
}
static inline void Serial_write(Serial *s, int val) {
  if (!s || s->fd < 0) return;
  uint8_t b = (uint8_t)val;
  (void)write(s->fd, &b, 1);
}
static inline void Serial_write_str(Serial *s, const char *str) {
  if (!s || s->fd < 0 || !str) return;
  (void)write(s->fd, str, strlen(str));
}
static inline void Serial_bufferUntil(Serial *s, int c) { (void)s; (void)c; }
static inline void Serial_clear(Serial *s) { (void)s; }
static inline void Serial_stop(Serial *s) {
  if (s && s->fd >= 0) { close(s->fd); s->fd = -1; }
}
static inline const char *Serial_readString(Serial *s) { (void)s; return ""; }
static inline const char *Serial_readStringUntil(Serial *s, int c) { (void)s; (void)c; return ""; }

#endif
