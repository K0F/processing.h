#ifndef _PDE_LIB_IO_H
#define _PDE_LIB_IO_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define _PDE_FILEREADER_MAGIC 0xF17E8EA0

typedef struct FileReader {
  uint32_t magic;
  FILE *fp;
  char path[512];
} FileReader;

static inline FileReader *FileReader_ctor(const char *path) {
  FileReader *fr = (FileReader *)calloc(1, sizeof(FileReader));
  if (!fr) return NULL;
  fr->magic = _PDE_FILEREADER_MAGIC;
  if (path) {
    snprintf(fr->path, sizeof(fr->path), "%s", path);
    fr->fp = fopen(path, "r");
    if (!fr->fp) {
      char dataPath[512];
      snprintf(dataPath, sizeof(dataPath), "data/%s", path);
      fr->fp = fopen(dataPath, "r");
    }
  }
  return fr;
}

static inline void FileReader_close(FileReader *fr) {
  if (fr) {
    if (fr->fp) { fclose(fr->fp); fr->fp = NULL; }
    free(fr);
  }
}

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
  if (!r || !r->fp || feof(r->fp)) return NULL;
  ssize_t n = getline(&r->buf, &r->cap, r->fp);
  if (n <= 0) return NULL;
  while (n > 0 && (r->buf[n - 1] == '\r' || r->buf[n - 1] == '\n')) {
    r->buf[--n] = '\0';
  }
  return r->buf;
}

static inline void BufferedReader_close(BufferedReader *r) {
  if (!r) return;
  if (r->fp) { fclose(r->fp); r->fp = NULL; }
  if (r->buf) { free(r->buf); r->buf = NULL; r->cap = 0; }
}

static inline BufferedReader _BufferedReader_ctor_fr(FileReader *fr) {
  BufferedReader r;
  memset(&r, 0, sizeof(r));
  if (!fr) return r;
  r.fp = fr->fp;
  fr->fp = NULL;
  free(fr);
  return r;
}

static inline BufferedReader _BufferedReader_ctor_path(const char *path) {
  return createReader(path);
}

static inline BufferedReader _BufferedReader_ctor_fr_val(FileReader fr) {
  BufferedReader r;
  memset(&r, 0, sizeof(r));
  r.fp = fr.fp;
  return r;
}

static inline BufferedReader (BufferedReader_ctor)(void *src) {
  if (!src) {
    BufferedReader r;
    memset(&r, 0, sizeof(r));
    return r;
  }
  const uint32_t *magic = (const uint32_t *)src;
  if (*magic == _PDE_FILEREADER_MAGIC) {
    return _BufferedReader_ctor_fr((FileReader *)src);
  }
  return _BufferedReader_ctor_path((const char *)src);
}

#define BufferedReader_ctor(src) _Generic((src), \
  FileReader *: _BufferedReader_ctor_fr, \
  const FileReader *: (BufferedReader (*)(FileReader *))_BufferedReader_ctor_fr, \
  FileReader: _BufferedReader_ctor_fr_val, \
  char *: _BufferedReader_ctor_path, \
  const char *: _BufferedReader_ctor_path, \
  default: (BufferedReader_ctor) \
)((src))

typedef struct {
  FILE *fp;
} PrintWriter;

static inline PrintWriter createWriter(const char *path) {
  PrintWriter w;
  memset(&w, 0, sizeof(w));
  if (path) w.fp = fopen(path, "w");
  return w;
}

static inline PrintWriter PrintWriter_ctor(const char *path) {
  return createWriter(path);
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

#ifdef __cplusplus
}
#endif

#endif /* _PDE_LIB_IO_H */
