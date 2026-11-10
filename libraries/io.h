#ifndef _PDE_LIB_IO_H
#define _PDE_LIB_IO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef struct {
  FILE *fp;
  char *buf;
  size_t cap;
} BufferedReader;

static inline BufferedReader createReader(const char *path) {
  BufferedReader r;
  memset(&r, 0, sizeof(r));
  if (!path) return r;
  r.fp = fopen(path, "r");
  if (!r.fp) {
    char dataPath[512];
    snprintf(dataPath, sizeof(dataPath), "data/%s", path);
    r.fp = fopen(dataPath, "r");
  }
  return r;
}

static inline const char *BufferedReader_readLine(BufferedReader *r) {
  if (!r || !r.fp || feof(r.fp)) return NULL;
  ssize_t n = getline(&r->buf, &r->cap, r->fp);
  if (n <= 0) return NULL;
  while (n > 0 && (r->buf[n - 1] == '\r' || r->buf[n - 1] == '\n')) {
    r->buf[--n] = '\0';
  }
  return r->buf;
}

static inline void BufferedReader_close(BufferedReader *r) {
  if (r && r->fp) { fclose(r->fp); r->fp = NULL; }
  if (r && r->buf) { free(r->buf); r->buf = NULL; r->cap = 0; }
}

typedef struct {
  FILE *fp;
} PrintWriter;

static inline PrintWriter createWriter(const char *path) {
  PrintWriter w;
  memset(&w, 0, sizeof(w));
  if (path) w.fp = fopen(path, "w");
  return w;
}

static inline void PrintWriter_println(PrintWriter *w, const char *str) {
  if (w && w->fp) { fputs(str ? str : "", w->fp); fputc('\n', w->fp); }
}

static inline void PrintWriter_print(PrintWriter *w, const char *str) {
  if (w && w->fp) { fputs(str ? str : "", w->fp); }
}

static inline void PrintWriter_flush(PrintWriter *w) {
  if (w && w->fp) fflush(w->fp);
}

static inline void PrintWriter_close(PrintWriter *w) {
  if (w && w->fp) { fclose(w->fp); w->fp = NULL; }
}

#endif
