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
#define Client_ctor(...) Client_ctor_impl(0, ##__VA_ARGS__)

static inline int Client_available(Client *c) {
  if (!c || c->sock < 0) return 0;
  char peek;
  ssize_t n = recv(c->sock, &peek, 1, MSG_PEEK | MSG_DONTWAIT);
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

#endif
