#ifndef _PDE_LIB_NET_H
#define _PDE_LIB_NET_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int sock;
  char host[128];
  int port;
  char readBuf[4096];
} Client;

static inline Client Client_ctor_impl(void *parent, const char *host, int port) {
  (void)parent;
  Client c;
  memset(&c, 0, sizeof(c));
  c.sock = -1;
  c.port = port;
  if (host) snprintf(c.host, sizeof(c.host), "%s", host);
  c.sock = socket(AF_INET, SOCK_STREAM, 0);
  if (c.sock >= 0 && host) {
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    inet_pton(AF_INET, host, &sin.sin_addr);
    connect(c.sock, (struct sockaddr *)&sin, sizeof(sin));
    int flags = fcntl(c.sock, F_GETFL, 0);
    fcntl(c.sock, F_SETFL, flags | O_NONBLOCK);
  }
  return c;
}

static inline Client Client_ctor3(void *parent, const char *host, int port) {
  return Client_ctor_impl(parent, host, port);
}
static inline Client Client_ctor2(const char *host, int port) {
  return Client_ctor_impl(NULL, host, port);
}
#define _PDE_CLIENT_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define Client_ctor(...) _PDE_CLIENT_CHOOSER(__VA_ARGS__, Client_ctor3, Client_ctor2)(__VA_ARGS__)

static inline int Client_available(Client *c) {
  if (!c || c->sock < 0) return 0;
  char peek;
  ssize_t n = recv(c->sock, &peek, 1, MSG_PEEK | MSG_DONTWAIT);
  if (n == 0) {
    close(c->sock);
    c->sock = -1;
    return 0;
  }
  return (n > 0) ? 1 : 0;
}

static inline const char *Client_readString(Client *c) {
  if (!c || c->sock < 0) return "";
  ssize_t n = recv(c->sock, c->readBuf, sizeof(c->readBuf) - 1, MSG_DONTWAIT);
  if (n > 0) {
    c->readBuf[n] = '\0';
    return c->readBuf;
  }
  return "";
}

static inline void Client_write(Client *c, const char *str) {
  if (!c || c->sock < 0 || !str) return;
  (void)send(c->sock, str, strlen(str), 0);
}

static inline void Client_clear(Client *c) {
  if (c) c->readBuf[0] = '\0';
}

static inline void Client_stop(Client *c) {
  if (c && c->sock >= 0) { close(c->sock); c->sock = -1; }
}

#define _PDE_MAX_SERVER_CLIENTS 64

typedef struct {
  int sock;
  int port;
  Client clients[_PDE_MAX_SERVER_CLIENTS];
  int clientCount;
} Server;

static inline Server Server_ctor_impl(void *parent, int port) {
  (void)parent;
  Server s;
  memset(&s, 0, sizeof(s));
  s.sock = -1;
  s.port = port;
  for (int i = 0; i < _PDE_MAX_SERVER_CLIENTS; i++) {
    s.clients[i].sock = -1;
  }
  s.sock = socket(AF_INET, SOCK_STREAM, 0);
  if (s.sock >= 0) {
    int opt = 1;
    setsockopt(s.sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = INADDR_ANY;
    if (bind(s.sock, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
      listen(s.sock, 8);
      int flags = fcntl(s.sock, F_GETFL, 0);
      fcntl(s.sock, F_SETFL, flags | O_NONBLOCK);
    } else {
      close(s.sock);
      s.sock = -1;
    }
  }
  return s;
}

static inline Server _Server_ctor1(int port) {
  return Server_ctor_impl(NULL, port);
}
static inline Server _Server_ctor2(void *parent, int port) {
  return Server_ctor_impl(parent, port);
}

#define _PDE_SRV_CHOOSER(_1, _2, NAME, ...) NAME
#define Server_ctor(...) _PDE_SRV_CHOOSER(__VA_ARGS__, _Server_ctor2, _Server_ctor1)(__VA_ARGS__)

static inline Client *Server_available(Server *s) {
  if (!s || s->sock < 0) return NULL;

  /* 1. Non-blocking accept of new pending connection */
  struct sockaddr_in cin;
  socklen_t len = sizeof(cin);
  int cfd = accept(s->sock, (struct sockaddr *)&cin, &len);
  if (cfd >= 0) {
    int flags = fcntl(cfd, F_GETFL, 0);
    fcntl(cfd, F_SETFL, flags | O_NONBLOCK);
    int slot = -1;
    for (int i = 0; i < s->clientCount; i++) {
      if (s->clients[i].sock < 0) {
        slot = i;
        break;
      }
    }
    if (slot < 0 && s->clientCount < _PDE_MAX_SERVER_CLIENTS) {
      slot = s->clientCount++;
    }
    if (slot >= 0) {
      memset(&s->clients[slot], 0, sizeof(Client));
      s->clients[slot].sock = cfd;
      s->clients[slot].port = ntohs(cin.sin_port);
      inet_ntop(AF_INET, &cin.sin_addr, s->clients[slot].host, sizeof(s->clients[slot].host));
      return &s->clients[slot];
    } else {
      close(cfd);
    }
  }

  /* 2. Check existing connected clients for incoming data */
  for (int i = 0; i < s->clientCount; i++) {
    if (s->clients[i].sock >= 0) {
      if (Client_available(&s->clients[i])) {
        return &s->clients[i];
      }
    }
  }
  return NULL;
}

static inline void Server_write(Server *s, const char *str) {
  if (!s || !str) return;
  for (int i = 0; i < s->clientCount; i++) {
    if (s->clients[i].sock >= 0) {
      Client_write(&s->clients[i], str);
    }
  }
}

static inline void Server_stop(Server *s) {
  if (!s) return;
  for (int i = 0; i < s->clientCount; i++) {
    Client_stop(&s->clients[i]);
  }
  s->clientCount = 0;
  if (s->sock >= 0) {
    close(s->sock);
    s->sock = -1;
  }
}

typedef struct URL {
  char url[256];
  void *stream;
  void *(*openStream)(void);
} URL;

static inline void *_url_open_stream_dummy(void) { return NULL; }

typedef struct {
  void *stream;
} InputStreamReader;

static inline URL URL_ctor(const char *u) {
  URL url;
  memset(&url, 0, sizeof(url));
  if (u) snprintf(url.url, sizeof(url.url), "%s", u);
  url.openStream = _url_open_stream_dummy;
  return url;
}

static inline void *URL_openStream(URL *u) { (void)u; return NULL; }
static inline void *URL_openStream_val(URL u) { (void)u; return NULL; }

static inline InputStreamReader *InputStreamReader_ctor(void *s) {
  InputStreamReader *isr = (InputStreamReader *)calloc(1, sizeof(InputStreamReader));
  if (isr) isr->stream = s;
  return isr;
}

#ifdef __cplusplus
}
#endif

#endif /* _PDE_LIB_NET_H */
