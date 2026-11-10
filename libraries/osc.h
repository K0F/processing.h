#ifndef _PDE_LIB_OSC_H
#define _PDE_LIB_OSC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

typedef struct {
  char host[128];
  int port;
  struct sockaddr_in addr;
} NetAddress;

static inline NetAddress NetAddress_ctor_impl(const char *host, int port) {
  NetAddress na;
  memset(&na, 0, sizeof(na));
  if (host) snprintf(na.host, sizeof(na.host), "%s", host);
  na.port = port;
  na.addr.sin_family = AF_INET;
  na.addr.sin_port = htons(port);
  if (host) inet_pton(AF_INET, host, &na.addr.sin_addr);
  return na;
}
#define NetAddress_ctor(...) NetAddress_ctor_impl(__VA_ARGS__)

typedef struct {
  int intVal;
  float floatVal;
  char strVal[128];
} OscArgument;

static inline float OscArgument_floatValue(OscArgument *a) { return a ? a->floatVal : 0.0f; }
static inline int OscArgument_intValue(OscArgument *a) { return a ? a->intVal : 0; }
static inline const char *OscArgument_stringValue(OscArgument *a) { return a ? a->strVal : ""; }

typedef struct {
  char address[128];
  char typetagStr[32];
  OscArgument args[16];
  int nargs;
} OscMessage;

static inline OscMessage OscMessage_ctor_impl(const char *addr) {
  OscMessage m;
  memset(&m, 0, sizeof(m));
  if (addr) snprintf(m.address, sizeof(m.address), "%s", addr);
  return m;
}
#define OscMessage_ctor(...) OscMessage_ctor_impl(__VA_ARGS__)

static inline const char *OscMessage_addrPattern(OscMessage *m) { return m ? m->address : ""; }
static inline const char *OscMessage_typetag(OscMessage *m) { return m ? m->typetagStr : ""; }
static inline bool OscMessage_checkAddrPattern(OscMessage *m, const char *p) {
  return m && p && strcmp(m->address, p) == 0;
}
static inline bool OscMessage_checkTypetag(OscMessage *m, const char *t) {
  return m && t && strcmp(m->typetagStr, t) == 0;
}

static inline void OscMessage_add_float(OscMessage *m, float v) {
  if (!m || m->nargs >= 16) return;
  m->args[m->nargs].floatVal = v;
  m->args[m->nargs].intVal = (int)v;
  snprintf(m->args[m->nargs].strVal, sizeof(m->args[m->nargs].strVal), "%f", v);
  int len = (int)strlen(m->typetagStr);
  if (len < 31) { m->typetagStr[len] = 'f'; m->typetagStr[len + 1] = '\0'; }
  m->nargs++;
}

static inline void OscMessage_add_int(OscMessage *m, int v) {
  if (!m || m->nargs >= 16) return;
  m->args[m->nargs].intVal = v;
  m->args[m->nargs].floatVal = (float)v;
  snprintf(m->args[m->nargs].strVal, sizeof(m->args[m->nargs].strVal), "%d", v);
  int len = (int)strlen(m->typetagStr);
  if (len < 31) { m->typetagStr[len] = 'i'; m->typetagStr[len + 1] = '\0'; }
  m->nargs++;
}

static inline void OscMessage_add_str(OscMessage *m, const char *v) {
  if (!m || m->nargs >= 16) return;
  if (v) snprintf(m->args[m->nargs].strVal, sizeof(m->args[m->nargs].strVal), "%s", v);
  int len = (int)strlen(m->typetagStr);
  if (len < 31) { m->typetagStr[len] = 's'; m->typetagStr[len + 1] = '\0'; }
  m->nargs++;
}

#define OscMessage_add(m, val) _Generic((val), \
    float: OscMessage_add_float, \
    double: OscMessage_add_float, \
    int: OscMessage_add_int, \
    const char *: OscMessage_add_str, \
    char *: OscMessage_add_str, \
    default: OscMessage_add_float)((m), (val))

static inline OscArgument *OscMessage_get(OscMessage *m, int idx) {
  static OscArgument dummy = {0};
  if (m && idx >= 0 && idx < m->nargs) return &m->args[idx];
  return &dummy;
}

typedef struct {
  int sock;
  int port;
} OscP5;

static inline OscP5 OscP5_ctor_impl(void *parent, int port) {
  (void)parent;
  OscP5 o;
  memset(&o, 0, sizeof(o));
  o.port = port;
  o.sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (o.sock >= 0) {
    int flags = fcntl(o.sock, F_GETFL, 0);
    fcntl(o.sock, F_SETFL, flags | O_NONBLOCK);
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = INADDR_ANY;
    bind(o.sock, (struct sockaddr *)&sin, sizeof(sin));
  }
  return o;
}
static inline OscP5 _pde_oscp5_ctor_1(int port) {
  return OscP5_ctor_impl(NULL, port);
}
static inline OscP5 _pde_oscp5_ctor_2(void *parent, int port) {
  return OscP5_ctor_impl(parent, port);
}
#define _OSCP5_CTOR_CHOOSER(_1, _2, NAME, ...) NAME
#define OscP5_ctor(...) _OSCP5_CTOR_CHOOSER(__VA_ARGS__, _pde_oscp5_ctor_2, _pde_oscp5_ctor_1)(__VA_ARGS__)

static inline void OscP5_send(OscP5 *o, OscMessage *msg, NetAddress *dest) {
  (void)o; (void)msg; (void)dest;
  // Non-blocking OSC packet dispatch can be added here
}

#endif
