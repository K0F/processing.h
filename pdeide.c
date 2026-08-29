/*
 * pdeide.c — a tiny Processing -> raylib IDE.
 *
 * A pure-raylib immediate-mode editor for .pde sketches wired into the
 * existing pde2c -> gcc pipeline. It provides:
 *
 *   - a monospace text editor (open / save / edit / syntax-less)
 *   - a Run button that transpiles + compiles the buffer with the exact
 *     flags pdecc uses, then launches the resulting binary as a child
 *     process on a separate raylib window
 *   - a console panel that streams the child's stdout/stderr live and
 *     reports transpile / compile errors mapped back to editor lines
 *   - a Stop button (SIGTERM then SIGKILL) to halt a running sketch
 *   - native Open / Save dialogs via the vendored tinyfiledialogs
 *
 * Error model (this pipeline compiles to native C, so there are three
 * distinct error sources, all funneled into the console + editor markers):
 *   1. transpile errors from pde2c  -> "name.pde:LINE: error: msg"
 *   2. gcc compile errors, carried through #line directives so they also
 *      reference "name.pde:LINE"
 *   3. runtime: the child sketch process; fatal crashes (signals) are
 *      reported as "sketch crashed (signal N)".
 */

#include "raylib.h"
#include "tinyfiledialogs.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <math.h>
#include <stdint.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ------------------------------------------------------------------ */
/* small utilities                                                     */
/* ------------------------------------------------------------------ */

static char script_dir[PATH_MAX];
static char* str_dup(const char *s)    { size_t n = strlen(s)+1; char *d = malloc(n); memcpy(d, s, n); return d; }

/* encode a Unicode codepoint into UTF-8; returns byte length */
static int utf8_encode(unsigned cp, char out[4]) {
  if (cp < 0x80)     { out[0]=(char)cp; return 1; }
  if (cp < 0x800)    { out[0]=(char)(0xC0|(cp>>6)); out[1]=(char)(0x80|(cp&0x3F)); return 2; }
  if (cp < 0x10000)  { out[0]=(char)(0xE0|(cp>>12)); out[1]=(char)(0x80|((cp>>6)&0x3F)); out[2]=(char)(0x80|(cp&0x3F)); return 3; }
  out[0]=(char)(0xF0|(cp>>18)); out[1]=(char)(0x80|((cp>>12)&0x3F));
  out[2]=(char)(0x80|((cp>>6)&0x3F)); out[3]=(char)(0x80|(cp&0x3F)); return 4;
}

/* step one codepoint back from offset i (i>0); returns new offset */
static size_t cw_back(const char *s, size_t i) {
  if (i == 0) return 0;
  i--; while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
  return i;
}
/* step one codepoint forward from offset i (i<len); returns new offset */
static size_t cw_fwd(const char *s, size_t len, size_t i) {
  if (i >= len) return len;
  i++; while (i < len && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
  return i;
}

/* ------------------------------------------------------------------ */
/* console (bounded line log)                                          */
/* ------------------------------------------------------------------ */
enum { CON_OUT = 0, CON_ERR = 1, CON_WARN = 2, CON_STATUS = 3, CON_OK = 4 };

#define CON_MAX 6000
typedef struct { char *text; int kind; } ConLine;

static ConLine *con_lines = NULL; static int con_count = 0, con_cap = 0;
static int con_view_back = 0;      /* lines scrolled back from the bottom */
static int con_follow = 1;         /* auto-scroll to newest */

static void con_add_full(const char *text, int kind) {
  if (con_count == con_cap) {
    con_cap = con_cap ? con_cap*2 : 256;
    con_lines = realloc(con_lines, sizeof(ConLine)*con_cap);
  }
  con_lines[con_count].text = str_dup(text);
  con_lines[con_count].kind  = kind;
  con_count++;
  if (con_count > CON_MAX) {
    ConLine *dst = con_lines;
    for (int i = con_count - CON_MAX; i < con_count; i++) dst[i - (con_count - CON_MAX)] = con_lines[i];
    con_count = CON_MAX;
  }
  if (con_follow) con_view_back = 0;
}

static void con_add(const char *fmt, int kind, ...) {
  char buf[8192];
  va_list ap; va_start(ap, kind); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  con_add_full(buf, kind);
}
static void con_clear(void) {
  for (int i = 0; i < con_count; i++) free(con_lines[i].text);
  con_count = 0; con_view_back = 0;
}

/* ------------------------------------------------------------------ */
/* editor buffer                                                       */
/* ------------------------------------------------------------------ */
#define BUF_INIT 65536

static double last_blink = 0;   /* caret blink phase, reset on any input */
static Font font;
static float fs = 12.0f;            /* pixel font size (crisp with POINT filter) */
static float line_h, char_w;        /* integer glyph metrics, set after load */

typedef struct {
  char  *buf; size_t cap, len;
  size_t cur;            /* cursor byte offset */
  size_t sel;            /* selection anchor, or SIZE_MAX for none */
  int    view_line;      /* first visible source line */
  int    view_col;       /* leftmost visible column (bytes) */
  char   path[PATH_MAX]; /* loaded/saved file path ("" = unnamed) */
  int    dirty;
  /* cached line index */
  size_t *line_off; int line_count, line_cap;
  int    need_rebuild;
  /* error markers: line numbers (1-based) to highlight */
  int *err; int err_count, err_cap; int first_err; /* first_err cached */
} Editor;

static Editor ed;

static void ed_init(void) {
  ed.cap = BUF_INIT; ed.len = 0; ed.buf = calloc(ed.cap, 1);
  ed.cur = 0; ed.sel = SIZE_MAX; ed.view_line = 0; ed.view_col = 0;
  ed.path[0] = 0; ed.dirty = 0; ed.line_count = 0; ed.need_rebuild = 0;
  ed.err = NULL; ed.err_count = 0; ed.err_cap = 0; ed.first_err = 0;
}

static void ed_clear_errors(void) { ed.err_count = 0; ed.first_err = 0; }

static void ed_mark_error(int line) {
  for (int i = 0; i < ed.err_count; i++) if (ed.err[i] == line) return;
  if (ed.err_count == ed.err_cap) { ed.err_cap = ed.err_cap ? ed.err_cap*2 : 32; ed.err = realloc(ed.err, sizeof(int)*ed.err_cap); }
  ed.err[ed.err_count++] = line;
  if (ed.first_err == 0 || line < ed.first_err) ed.first_err = line;
}

static void ed_grow(size_t needlen) {
  if (needlen + 1 > ed.cap) {
    while (ed.cap < needlen + 1) ed.cap *= 2;
    ed.buf = realloc(ed.buf, ed.cap);
  }
}

/* rebuild line-start index */
static void ed_rebuild_lines(void) {
  if (ed.line_cap == 0) { ed.line_cap = 64; ed.line_off = malloc(sizeof(size_t)*ed.line_cap); }
  ed.line_count = 0; ed.line_off[ed.line_count++] = 0;
  for (size_t i = 0; i < ed.len; i++) {
    if (ed.buf[i] == '\n') {
      if (ed.line_count == ed.line_cap) { ed.line_cap *= 2; ed.line_off = realloc(ed.line_off, sizeof(size_t)*ed.line_cap); }
      ed.line_off[ed.line_count++] = i + 1;
    }
  }
  ed.need_rebuild = 0;
}

static void ed_touch(void) { ed.dirty = 1; ed.need_rebuild = 1; last_blink = GetTime(); }

static void ed_ensure_lines(void) { if (ed.need_rebuild || ed.line_count == 0) ed_rebuild_lines(); }

/* offset -> line index (0-based) */
static int ed_line_of(size_t off) {
  ed_ensure_lines();
  int lo = 0, hi = ed.line_count - 1, ans = 0;
  while (lo <= hi) {
    int mid = (lo+hi)/2;
    if (ed.line_off[mid] <= off) { ans = mid; lo = mid+1; } else hi = mid-1;
  }
  return ans;
}
/* line index (0-based) -> start offset */
static size_t ed_line_start(int li) { ed_ensure_lines(); if (li<0) return 0; if (li>=ed.line_count) li=ed.line_count-1; return ed.line_off[li]; }
/* line index -> line length in bytes (excluding trailing \n) */
static size_t ed_line_len(int li) {
  ed_ensure_lines();
  size_t s = ed_line_start(li);
  size_t e = (li+1 < ed.line_count) ? ed.line_off[li+1] : ed.len;
  if (e > s && ed.buf[e-1] == '\n') e--;
  return e - s;
}
static int ed_lines(void) { ed_ensure_lines(); return ed.line_count; }

static int ed_col_of(size_t off) { return (int)(off - ed_line_start(ed_line_of(off))); }

static void ed_insert_at(size_t off, const char *text, size_t n) {
  ed_grow(ed.len + n);
  memmove(ed.buf + off + n, ed.buf + off, ed.len - off);
  memcpy(ed.buf + off, text, n);
  ed.len += n;
  ed_touch();
}
static void ed_delete_range(size_t a, size_t b) {
  if (b <= a) return;
  memmove(ed.buf + a, ed.buf + b, ed.len - b);
  ed.len -= (b - a);
  ed_touch();
}

/* Replace whole buffer contents with external text (file load). */
static void ed_set_text(const char *text) {
  size_t n = strlen(text);
  ed_grow(n);
  memcpy(ed.buf, text, n);
  ed.len = n;
  ed.cur = 0; ed.sel = SIZE_MAX; ed.view_line = 0; ed.view_col = 0;
  ed_touch();
}

/* current line index for cursor */
static int ed_cur_line(void) { return ed_line_of(ed.cur); }

/* re-render a selection range from (sel, cur) inclusive ordering */
static void ed_sel_range(size_t *a, size_t *b) {
  if (ed.sel == SIZE_MAX) { *a = ed.cur; *b = ed.cur; return; }
  *a = ed.sel < ed.cur ? ed.sel : ed.cur;
  *b = ed.sel < ed.cur ? ed.cur : ed.sel;
}

static const char *ed_get_selected(void) {
  size_t a, b; ed_sel_range(&a, &b);
  if (a == b) return NULL;
  char *out = malloc(b - a + 1);
  memcpy(out, ed.buf + a, b - a); out[b-a] = 0;
  return out;
}

/* ---------- editor editing operations ---------- */

static void ed_insert_text(const char *text, size_t n) {
  size_t a, b; ed_sel_range(&a, &b);
  ed_delete_range(a, b);
  ed_insert_at(a, text, n);
  ed.cur = a + n;
}
static void ed_insert_cp(unsigned cp) {
  char u[4]; int n = utf8_encode(cp, u);
  ed_insert_text(u, n);
}
static void ed_insert_newline(void) {
  int li = ed_cur_line();
  size_t ls = ed_line_start(li);
  /* copy the leading whitespace of the current line as auto-indent */
  size_t ws = 0;
  while (ls + ws < ed.cur && (ed.buf[ls+ws]==' ' || ed.buf[ls+ws]=='\t')) ws++;
  ed_insert_text("\n", 1);
  if (ws > 0) ed_insert_text(ed.buf + ls, ws);
  ed.cur += ws;
}
static void ed_backspace(void) {
  size_t a, b; ed_sel_range(&a, &b);
  if (a != b) { ed_delete_range(a, b); ed.cur = a; return; }
  if (ed.cur == 0) return;
  size_t prev = cw_back(ed.buf, ed.cur);
  ed_delete_range(prev, ed.cur); ed.cur = prev;
}
static void ed_delete(void) {
  size_t a, b; ed_sel_range(&a, &b);
  if (a != b) { ed_delete_range(a, b); ed.cur = a; return; }
  if (ed.cur >= ed.len) return;
  size_t nxt = cw_fwd(ed.buf, ed.len, ed.cur);
  ed_delete_range(ed.cur, nxt);
}

/* movement; shift selects */
static void ed_move(size_t target, bool shift) {
  if (shift && ed.sel == SIZE_MAX) ed.sel = ed.cur;
  ed.cur = target;
  if (!shift) ed.sel = SIZE_MAX;
  last_blink = GetTime();
}
static void ed_move_left(bool shift) {
  size_t t = ed.cur == 0 ? 0 : cw_back(ed.buf, ed.cur);
  ed_move(t, shift);
}
static void ed_move_right(bool shift) {
  size_t t = ed.cur >= ed.len ? ed.len : cw_fwd(ed.buf, ed.len, ed.cur);
  ed_move(t, shift);
}
static void ed_move_up_down(int dir, bool shift) {
  int li = ed_cur_line();
  int col = ed_col_of(ed.cur);
  int nli = li + dir;
  if (nli < 0) nli = 0; else if (nli >= ed_lines()) nli = ed_lines()-1;
  size_t ls = ed_line_start(nli);
  size_t ll = ed_line_len(nli);
  size_t t = ls + (size_t)(col < (int)ll ? col : ll);
  ed_move(t, shift);
}
static void ed_move_home(bool shift) {
  int li = ed_cur_line();
  /* home: to start of indentation, else line start */
  size_t ls = ed_line_start(li);
  size_t p = ls; while (p < ed.cur && (ed.buf[p]==' '||ed.buf[p]=='\t')) p++;
  size_t t = (p < ed.cur) ? p : ls;
  ed_move(t, shift);
}
static void ed_move_end(bool shift) {
  int li = ed_cur_line();
  size_t t = ed_line_start(li) + ed_line_len(li);
  ed_move(t, shift);
}
static void ed_move_page(int dir, bool shift) {
  for (int i = 0; i < 12; i++) ed_move_up_down(dir, shift);
}

/* click position (pixel) -> byte offset. Rounds to the nearest character cell
 * so the caret lands exactly on the column the click actually targets. */
static size_t ed_click_to_off(float mx, float my, int area_left, int area_top) {
  int lines = ed_lines();
  int li = ed.view_line + (int)((my - area_top) / line_h);
  if (li < 0) li = 0; else if (li >= lines) li = lines-1;
  int col0 = ed.view_col;
  size_t ls = ed_line_start(li);
  size_t ll = ed_line_len(li);
  float x = mx - area_left;
  long target_col = col0 + (long)(x / char_w + 0.5f);   /* nearest cell */
  if (target_col < 0) target_col = 0;
  if (target_col > (long)ll) target_col = ll;
  return ls + (size_t)target_col;
}

/* ------------------------------------------------------------------ */
/* persistent run/build                                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
  pid_t pid;
  int   out_fd, err_fd;
  int   out_eof, err_eof;
  int   out_dead, err_dead;
} Sketch;

static Sketch sk = {0};

enum { ST_IDLE = 0, ST_BUILD = 1, ST_RUN = 2 };
static int state = ST_IDLE;
static char build_dir[PATH_MAX];
static char sketch_src[PATH_MAX];   /* source .pde written for the build */
static char sketch_bin[PATH_MAX];   /* compiled binary */

/* spawn a command, capturing its output (stdout+stderr combined when
 * stdout_file is NULL; otherwise stdout goes to the file, stderr captured).
 * Returns the child's exit status, or -1 on exec/fork failure.
 * On success the captured output is stored (NUL-terminated) in *out. */
static int spawn_capture(char *const argv[], const char *stdout_file, char **base_out) {
  int p[2];
  if (pipe(p) != 0) return -1;
  pid_t pid = fork();
  if (pid < 0) { close(p[0]); close(p[1]); return -1; }
  if (pid == 0) {
    close(p[0]);
    dup2(p[1], STDERR_FILENO);
    if (stdout_file) {
      int fd = open(stdout_file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
      if (fd >= 0) dup2(fd, STDOUT_FILENO);
      else close(STDOUT_FILENO);
    } else {
      dup2(p[1], STDOUT_FILENO);
    }
    if (p[1] > 2) close(p[1]);
    execvp(argv[0], argv);
    _exit(127);
  }
  close(p[1]);
  size_t cap = 65536, n = 0; char *buf = malloc(cap); buf[0] = 0;
  for (;;) {
    char tmp[4096];
    ssize_t r = read(p[0], tmp, sizeof tmp);
    if (r > 0) {
      if (n + (size_t)r + 1 > cap) { while (n + (size_t)r + 1 > cap) cap *= 2; buf = realloc(buf, cap); }
      memcpy(buf + n, tmp, (size_t)r); n += (size_t)r; buf[n] = 0;
    } else if (r == 0) break;
    else if (errno != EINTR) break;
  }
  close(p[0]);
  int status = 0; waitpid(pid, &status, 0);
  *base_out = buf;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return -1;
}

/* Parse one diagnostic line of the form
 *       file:LINE: error: msg
 *   or  file:LINE:COL: error: msg
 * Returns 1 if a kind was recognised, 0 otherwise. When recognised,
 * *line_num holds the .pde line, *kind one of "error"/"warning"/"note",
 * and *msg points past the "kind:" tag (may point into `line`, NUL-free).
 */
static int parse_diag(const char *line, long *line_num, const char **kind, const char **msg) {
  const char *colon = strchr(line, ':');
  if (!colon) return 0;
  const char *rest = colon + 1;
  char *endp;
  long ln = strtol(rest, &endp, 10);
  if (endp == rest) return 0;      /* no line number after first colon */
  const char *p;
  if (*endp == ':') {              /* next part is a column, then ':' */
    char *e2;
    long c = strtol(endp + 1, &e2, 10);
    if (e2 != endp + 1 && *e2 == ':') p = e2 + 1;  /* has a column */
    else p = endp + 1;
  } else {
    p = endp;                       /* no column; p points at ':' ... */
    if (*p == ':') p++;             /* ... skip it */
  }
  while (*p == ' ' || *p == '\t') p++;
  const char *k = NULL;
  if      (strncmp(p, "error",   5) == 0) k = "error";
  else if (strncmp(p, "warning", 7) == 0) k = "warning";
  else if (strncmp(p, "note",    4) == 0) k = "note";
  if (!k) return 0;
  *line_num = ln;
  *kind = k;
  const char *m = p + strlen(k);
  if (*m == ':') m++;
  while (*m == ' ' || *m == '\t') m++;
  *msg = m;
  return 1;
}

/* Collect diagnostics from a whole captured stream and route them to the
 * console + editor error markers. Each line is parsed; unrecognised lines
 * are printed verbatim as plain output.
 */
static void handle_diagnostics(const char *text) {
  char *copy = str_dup(text);
  char *line = strtok(copy, "\n");
  while (line) {
    long ln; const char *kind, *msg;
    if (parse_diag(line, &ln, &kind, &msg)) {
      int kcon;
      if      (strcmp(kind, "error") == 0)   { kcon = CON_ERR;   ed_mark_error((int)ln); }
      else if (strcmp(kind, "warning") == 0) kcon = CON_WARN;
      else kcon = CON_OUT;
      con_add("L%ld: %s", kcon, ln, msg);
    } else {
      con_add_full(line, CON_OUT);
    }
    line = strtok(NULL, "\n");
  }
  free(copy);
}

/* ---- streaming read of sketch output into the console ---- */
typedef struct { char part[8192]; int n; int kind; } StreamBuf;
static StreamBuf sout, serr;

/* raylib writes its trace log (INFO/WARNING/... ) to stdout/stderr; a child
 * sketch is typically wrapped in dozens of these lines per launch. Filter
 * them out so the user's own println()/stderr output is actually visible. */
static const char *raylib_log_prefixes[] = {
  "TRACE:", "DEBUG:", "INFO:", "WARNING:", "ERROR:", "FATAL:"
};
static int is_raylib_log(const char *line) {
  while (*line==' ' || *line=='\t') line++;
  for (unsigned i = 0; i < sizeof raylib_log_prefixes/sizeof *raylib_log_prefixes; i++) {
    if (strncmp(line, raylib_log_prefixes[i], strlen(raylib_log_prefixes[i])) == 0) return 1;
  }
  return 0;
}

static void stream_drain(int fd, StreamBuf *sb) {
  char tmp[4096];
  for (;;) {
    ssize_t r = read(fd, tmp, sizeof tmp);
    if (r > 0) {
      for (ssize_t i = 0; i < r; i++) {
        if (sb->n == (int)sizeof sb->part) { /* flush overflow without newline */ con_add_full(sb->part, sb->kind); sb->n = 0; }
        sb->part[sb->n++] = tmp[i];
        if (tmp[i] == '\n') {
          sb->part[sb->n-1] = 0;
          if (!is_raylib_log(sb->part)) con_add_full(sb->part, sb->kind);
          sb->n = 0;
        }
      }
    } else if (r == 0) break;
    else if (errno != EINTR) break;
  }
}

/* stop the running sketch */
static void stop_sketch(void) {
  if (state != ST_RUN || sk.pid <= 0) return;
  kill(sk.pid, SIGTERM);
  /* wait a moment; escalate */
  for (int i = 0; i < 60; i++) {
    int st; pid_t r = waitpid(sk.pid, &st, WNOHANG);
    if (r == sk.pid) { sk.pid = 0; return; }
    usleep(50000);
  }
  kill(sk.pid, SIGKILL);
  waitpid(sk.pid, NULL, 0);
  sk.pid = 0;
}

/* read all lines written into *p line-by-line, split by source file marker.
 * This is a helper the caller doesn't strictly need but is kept for clarity. */
static int build_pipeline(void) {
  /* 1. write the source .pde into the build dir with a //@file marker so
   *    pde2c/gcc diagnostics reference the real sketch name & line. */
  const char *base = ed.path[0] ? ed.path : "sketch.pde";
  const char *slash = strrchr(base, '/');
  if (slash) base = slash + 1;
  if (strlen(base) < 4) base = "sketch.pde";

  char pde_path[PATH_MAX]; snprintf(pde_path, sizeof pde_path, "%s/%s", build_dir, base);

  /* the marker occupies its own virtual line 0; pde2c resets line counting
   * right after it, so editor line 1 == pde2c/gcc line 1 == #line 1. */
  FILE *fp = fopen(pde_path, "wb");
  if (!fp) { con_add("run: cannot write %s (%s)", CON_ERR, pde_path, strerror(errno)); return -1; }
  fprintf(fp, "//@file %s\n", base);
  fwrite(ed.buf, 1, ed.len, fp);
  fclose(fp);

  ed_clear_errors();

  /* 2. transpile */
  char pde2c[PATH_MAX]; snprintf(pde2c, sizeof pde2c, "%s/pde2c", script_dir);
  char sketch_c[PATH_MAX]; snprintf(sketch_c, sizeof sketch_c, "%s/%s.c", build_dir, base);
  char *cap = NULL;
  {
    char *argv[] = { pde2c, pde_path, NULL };
    int rc = spawn_capture(argv, sketch_c, &cap);
    if (rc != 0) {
      if (rc == 127) con_add("run: pde2c not found", CON_ERR);
      /* report diagnostics, but cap noise: transpile errors are few */
      handle_diagnostics(cap ? cap : "");
      free(cap);
      con_add("BUILD FAILED (transpile)", CON_ERR);
      return -1;
    }
  }
  /* if there were no Parse errors, still surface any diagnostics (warnings) */
  /* handle_diagnostics(cap ? cap : ""); // pde2c prints only errors on failure */
  free(cap);

  /* 3. compile on stdout-less output; capture stderr */
  char gcc[PATH_MAX]; /* use plain "gcc" from PATH */
  snprintf(gcc, sizeof gcc, "gcc");
  char sketch_out[PATH_MAX]; snprintf(sketch_out, sizeof sketch_out, "%s/%s.bin", build_dir, base);
  char *gerr = NULL;
  {
    char O[] = "-O2"; char Idir[PATH_MAX]; char *Idir_arg = malloc(4 + strlen(script_dir));
    sprintf(Idir_arg, "-I%s", script_dir);
    char out[PATH_MAX]; snprintf(out, sizeof out, "-o%s", sketch_out);
    char *argv[] = { gcc, O, Idir_arg, sketch_c, out,
                     "-lraylib", "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11", NULL };
    int rc = spawn_capture(argv, NULL, &gerr);
    free(Idir_arg);
    if (rc != 0) {
      /* gcc prints a mountain of diagnostics; dedupe to first error per
       * line and cap the count shown. */
      char *dup = str_dup(gerr ? gerr : "");
      char *tok = strtok(dup, "\n");
      int *seen = NULL; int seen_n = 0, seen_cap = 0;
      int shown = 0;
      while (tok) {
        long ln; const char *kind, *msg;
        if (parse_diag(tok, &ln, &kind, &msg) && strcmp(kind, "error") == 0) {
          int dup_line = 0;
          for (int k = 0; k < seen_n; k++) if (seen[k] == ln) { dup_line = 1; break; }
          if (!dup_line) {
            if (seen_n == seen_cap) { seen_cap = seen_cap ? seen_cap*2 : 64; seen = realloc(seen, sizeof(int)*seen_cap); }
            seen[seen_n++] = (int)ln;
            if (shown < 40) { con_add("L%ld: %s", CON_ERR, ln, msg); ed_mark_error((int)ln); }
            shown++;
          }
        }
        tok = strtok(NULL, "\n");
      }
      free(seen);
      if (shown > 40) con_add("... and %d more diagnostics", CON_ERR, shown - 40);
      if (shown == 0) con_add("%s", CON_ERR, gerr ? gerr : "gcc failed with no diagnostics");
      free(dup);
      free(gerr);
      con_add("BUILD FAILED (compile)", CON_ERR);
      return -1;
    }
    free(gerr);
  }

  chmod(sketch_out, 0755);
  strncpy(sketch_bin, sketch_out, sizeof sketch_bin - 1);
  return 0;
}

static void start_run(void) {
  if (state != ST_IDLE) return;
  con_clear();
  con_add("== building... ==", CON_STATUS);
  state = ST_BUILD;
  if (build_pipeline() != 0) { state = ST_IDLE; return; }
  con_add("== build ok, launching ==", CON_OK);
  /* launch the sketch asynchronously */
  int o[2], e[2];
  if (pipe(o) != 0 || pipe(e) != 0) {
    state = ST_IDLE; con_add("run: pipe failed", CON_ERR); return;
  }
  pid_t pid = fork();
  if (pid < 0) { state = ST_IDLE; con_add("run: fork failed", CON_ERR); return; }
  if (pid == 0) {
    close(o[0]); dup2(o[1], STDOUT_FILENO);
    close(e[0]); dup2(e[1], STDERR_FILENO);
    /* stdin from /dev/null */
    int nullfd = open("/dev/null", O_RDONLY); if (nullfd >= 0) dup2(nullfd, STDIN_FILENO);
    /* chdir into build dir so relative assets (terminus.ttf) resolve */
    if (build_dir[0]) chdir(build_dir);
    execl(sketch_bin, sketch_bin, NULL);
    _exit(127);
  }
  close(o[1]); close(e[1]);
  sk.pid = pid; sk.out_fd = o[0]; sk.err_fd = e[0];
  sk.out_eof = 0; sk.err_eof = 0;
  sout.n = 0; serr.n = 0;
  /* make pipes non-blocking? we use non-blocking reads via O_NONBLOCK */
  int fl = fcntl(sk.out_fd, F_GETFL, 0); fcntl(sk.out_fd, F_SETFL, fl | O_NONBLOCK);
  fl = fcntl(sk.err_fd, F_GETFL, 0);     fcntl(sk.err_fd, F_SETFL, fl | O_NONBLOCK);
  state = ST_RUN;
  con_add("== running (pid %d) ==", CON_OK, (int)pid);
}

/* poll the running sketch each frame */
static void poll_sketch(void) {
  if (state != ST_RUN) return;
  if (!sk.out_eof) stream_drain(sk.out_fd, &sout);
  if (!sk.err_eof) stream_drain(sk.err_fd, &serr);
  int status = 0;
  pid_t r = waitpid(sk.pid, &status, WNOHANG);
  if (r == sk.pid) {
    if (sk.out_fd >= 0) close(sk.out_fd); if (sk.err_fd >= 0) close(sk.err_fd);
    sk.pid = 0;
    if (WIFSIGNALED(status)) {
      con_add("== sketch crashed (signal %d) ==", CON_ERR, WTERMSIG(status));
    } else if (WIFEXITED(status)) {
      int c = WEXITSTATUS(status);
      if (c == 0) con_add("== sketch exited cleanly ==", CON_OK);
      else        con_add("== sketch exited with code %d ==", CON_ERR, c);
    } else {
      con_add("== sketch exited ==", CON_OK);
    }
    state = ST_IDLE;
  }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static int console_frac = 130;      /* console panel height in px */
static int dragging_splitter = 0;

static void load_file(const char *path);
static void save_file(const char *path);
static void cmd_open(void);
static void cmd_save(void);
static void cmd_save_as(void);
static int ButtonLike(const char *label, int x, int y, int colw, int *yout);

static void load_file(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) { con_add("open: cannot read %s: %s", CON_ERR, path, strerror(errno)); return; }
  fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
  char *buf = malloc((size_t)sz + 1);
  size_t r = fread(buf, 1, (size_t)sz, fp); fclose(fp);
  buf[r] = 0;
  ed_set_text(buf); free(buf);
  strncpy(ed.path, path, sizeof ed.path - 1);
  ed.dirty = 0; ed_clear_errors();
  con_clear();
  con_add("opened %s", CON_OK, path);
}

static void save_file(const char *path) {
  FILE *fp = fopen(path, "wb");
  if (!fp) { con_add("save: cannot write %s: %s", CON_ERR, path, strerror(errno)); return; }
  fwrite(ed.buf, 1, ed.len, fp); fclose(fp);
  strncpy(ed.path, path, sizeof ed.path - 1);
  ed.dirty = 0;
  con_add("saved %s", CON_OK, path);
}

/* detect whether a native dialog backend is present (zenity/kdialog/...) */
static int have_native_dialogs(void) {
  const char *bins[] = { "zenity", "kdialog", "yad", "qarma", "matedialog", "Xdialog", NULL };
  for (int i = 0; bins[i]; i++) {
    /* search PATH manually */
    char *path = str_dup(getenv("PATH") ? getenv("PATH") : "");
    char *tok = strtok(path, ":");
    while (tok) {
      char f[PATH_MAX]; snprintf(f, sizeof f, "%s/%s", tok, bins[i]);
      if (access(f, X_OK) == 0) { free(path); return 1; }
      tok = strtok(NULL, ":");
    }
    free(path);
  }
  return 0;
}

static int native_dialogs = 1, dialogs_checked = 0;

static void cmd_open(void) {
  if (!native_dialogs) {
    const char *p = tinyfd_inputBox("Open sketch", "Enter .pde path:", ed.path[0]?ed.path:NULL);
    if (p && p[0]) load_file(p);
    return;
  }
  const char *filt[] = { "*.pde" };
  const char *def = ed.path[0] ? ed.path : NULL;
  const char *p = tinyfd_openFileDialog("Open sketch", def, 1, filt, "Processing sketches", 0);
  if (p) load_file(p);
}
static void cmd_save(void) {
  if (ed.path[0]) { save_file(ed.path); return; }
  cmd_save_as();
}
static void cmd_save_as(void) {
  if (!native_dialogs) {
    const char *p = tinyfd_inputBox("Save sketch", "Enter path:", NULL);
    if (p && p[0]) save_file(p);
    return;
  }
  const char *filt[] = { "*.pde" };
  const char *p = tinyfd_saveFileDialog("Save sketch", ed.path[0]?ed.path:NULL, 1, filt, "Processing sketches");
  if (p) save_file(p);
}

static void load_file(const char *path);
static void frame(void) {
  poll_sketch();

  int w = GetScreenWidth(), h = GetScreenHeight();

  /* ---------------- toolbar ---------------- */
  const int tb = 24;
  DrawRectangle(0, 0, w, tb, (Color){ 34, 34, 38, 255 });

  /* filename / dirty indicator */
  {
    char title[PATH_MAX + 8];
    const char *base = "unnamed";
    const char *p = ed.path[0] ? ed.path : "unnamed.pde";
    const char *sl = strrchr(p, '/'); if (sl) base = sl+1; else base = p;
    snprintf(title, sizeof title, "%s%s", base, ed.dirty ? " *" : "");
    DrawTextEx(font, title, (Vector2){ 6, 5 }, fs, 0.0f, (Color){ 220,220,220,255 });
  }

  /* run / stop buttons */
  Rectangle runb = { 170, 2, 50, 20 };
  Rectangle stopb = { 226, 2, 50, 20 };
  if (state == ST_IDLE || state == ST_BUILD) {
    if (CheckCollisionPointRec(GetMousePosition(), runb)) DrawRectangleRec(runb, (Color){28,120,48,255});
    else DrawRectangleRec(runb, (Color){22,90,38,255});
    DrawTextEx(font, "Run", (Vector2){runb.x+18, 4}, fs, 0.0f, WHITE);
    if (IsMouseButtonPressed(0) && CheckCollisionPointRec(GetMousePosition(), runb) && state == ST_IDLE) {
      start_run();
    }
    DrawRectangleRec(stopb, (Color){70,70,74,255});
    DrawTextEx(font, "Stop", (Vector2){stopb.x+15, 4}, fs, 0.0f, (Color){150,150,150,255});
  } else { /* running */
    DrawRectangleRec(runb, (Color){70,70,74,255});
    DrawTextEx(font, "Run", (Vector2){runb.x+18, 4}, fs, 0.0f, (Color){150,150,150,255});
    if (CheckCollisionPointRec(GetMousePosition(), stopb)) DrawRectangleRec(stopb, (Color){140,40,40,255});
    else DrawRectangleRec(stopb, (Color){110,32,32,255});
    DrawTextEx(font, "Stop", (Vector2){stopb.x+15, 4}, fs, 0.0f, WHITE);
    if (IsMouseButtonPressed(0) && CheckCollisionPointRec(GetMousePosition(), stopb)) stop_sketch();
  }

  /* menu-ish buttons: Open, Save */
  static int dummy;
  int bx = 220 + 70;
  if (ButtonLike("Open", bx, 2, 44, &dummy)) cmd_open();
  bx += 50;
  if (ButtonLike("Save", bx, 2, 44, &dummy)) cmd_save();
  bx += 50;
  if (ButtonLike("SaveAs", bx, 2, 58, &dummy)) cmd_save_as();

  /* status text right-aligned */
  {
    const char *st;
    Color c;
    switch (state) {
      case ST_IDLE:   st = "Idle"; c = (Color){160,200,160,255}; break;
      case ST_BUILD:  st = "Building..."; c = (Color){230,200,120,255}; break;
      default:        { static char s[48]; snprintf(s,48,"Running (pid %d)",(int)sk.pid); st = s; c = (Color){160,220,255,255}; }
    }
    float tw = MeasureTextEx(font, st, fs, 0.0f).x;
    DrawTextEx(font, st, (Vector2){ w - tw - 8, 5 }, fs, 0.0f, c);
  }

  /* ---------------- splitter + console area ---------------- */
  int split_y = h - console_frac;
  DrawLine(0, split_y, w, split_y, (Color){90,90,90,255});
  /* drag handle */
  Color handle = (Color){120,120,120,255};
  DrawLine(0, split_y-2, w, split_y-2, handle);
  DrawLine(0, split_y+2, w, split_y+2, handle);
  if (state == ST_IDLE) {
    if (IsMouseButtonPressed(0) && GetMouseY() > split_y-6 && GetMouseY() < split_y+6) dragging_splitter = 1;
  }
  if (dragging_splitter) {
    if (IsMouseButtonDown(0)) { console_frac = h - GetMouseY(); if (console_frac < 60) console_frac = 60; if (console_frac > h - 120) console_frac = h - 120; }
    else dragging_splitter = 0;
    split_y = h - console_frac;
  }

  /* ---------------- console ---------------- */
  DrawRectangle(0, split_y+3, w, h - (split_y+3), (Color){16,16,18,255});
  int con_top_area = split_y + 4;
  int con_h = h - con_top_area;
  if (con_h > 4) {
    /* count lines that fit */
    int vis = con_h / (int)line_h;
    int n = con_count;
    int start = n - vis - con_view_back;
    if (start < 0) start = 0;
    /* draw from start */
    for (int i = start; i < n; i++) {
      ConLine *cl = &con_lines[i];
      Color col;
      switch (cl->kind) {
        case CON_ERR:    col = (Color){255,110,110,255}; break;
        case CON_WARN:   col = (Color){255,220,140,255}; break;
        case CON_STATUS: col = (Color){200,200,220,255}; break;
        case CON_OK:     col = (Color){140,220,160,255}; break;
        default:         col = (Color){210,210,210,255};
      }
      float yy = con_top_area + (i - start) * line_h;
      int maxw = w / (int)char_w;
      int llen = (int)strlen(cl->text);
      if (llen > maxw) llen = maxw;
      DrawTextEx(font, cl->text, (Vector2){4, yy}, fs, 0.0f, col);
    }
    /* scroll wheel on console area */
    if (GetMouseY() > con_top_area) {
      float mw = GetMouseWheelMove();
      if (mw != 0) {
        con_view_back += -(int)(mw * 3);
        if (con_view_back < 0) con_view_back = 0;
        /* only follow-mode across the top of the scrollback is "off" */
        if (con_view_back > 0) con_follow = 0;
        else con_follow = 1;
      }
    }
  }
  DrawTextEx(font, "console", (Vector2){w-70, con_top_area+2}, fs-4, 1.0f, (Color){90,90,90,255});

  /* ---------------- editor ---------------- */
  int ed_top = tb;
  int ed_h = split_y - tb;
  if (ed_h < 10) ed_h = 10;

  /* editor background */
  DrawRectangle(0, ed_top, w, ed_h, (Color){24,24,27,255});

  /* gutter width */
  int lines_n = ed_lines();
  int gutter_w = 28 + (int)((lines_n >= 10000) ? 3 : (lines_n >= 1000 ? 2 : 1)) * 6;

  int area_l = gutter_w;
  int area_w = w - area_l;

  /* viewport line count */
  int vis_lines = ed_h / (int)line_h;
  if (vis_lines < 1) vis_lines = 1;

  /* current source line and col */
  int cli = ed_cur_line();

  /* keep view_line in range */
  if (ed.view_line < 0) ed.view_line = 0;
  if (ed.view_line > lines_n - 1) ed.view_line = lines_n - 1 > 0 ? lines_n - 1 : 0;

  /* horizontal range: compute max line len in viewport */
  int maxlen = 0;
  for (int i = ed.view_line; i < lines_n && i - ed.view_line < vis_lines; i++) { int l = (int)ed_line_len(i); if (l > maxlen) maxlen = l; }

  /* ---------- keyboard editing ---------- */
  {
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    int k = GetKeyPressed();
    /* consume all queued keys */
    while (k) {
      if (!ctrl) {
        switch (k) {
          case KEY_BACKSPACE: ed_backspace(); break;
          case KEY_DELETE:    ed_delete(); break;
          case KEY_LEFT:      ed_move_left(shift); break;
          case KEY_RIGHT:     ed_move_right(shift); break;
          case KEY_UP:        ed_move_up_down(-1, shift); break;
          case KEY_DOWN:      ed_move_up_down(1, shift); break;
          case KEY_HOME:      ed_move_home(shift); break;
          case KEY_END:       ed_move_end(shift); break;
          case KEY_PAGE_UP:   ed_move_page(-1, shift); break;
          case KEY_PAGE_DOWN: ed_move_page(1, shift); break;
          case KEY_ENTER: case KEY_KP_ENTER: ed_insert_newline(); break;
          case KEY_TAB: { char t[] = "  "; ed_insert_text(t, 2); } break;
          default: break;
        }
      } else {
        switch (k) {
          case KEY_A: { /* select all */
            ed.sel = 0; ed.cur = ed.len;
          } break;
          case KEY_X: {
            const char *s = ed_get_selected();
            if (s) { /* copy to clipboard via tinyfd */
#if defined(TINYFD_OSX) || defined(_WIN32)
              /* no portable clipboard in tinyfiledialogs; skip */
#else
              /* use xclip/xsel if present */
              const char *clips[] = { "xclip -selection clipboard", "xsel -b" };
              for (int ci = 0; ci < 2; ci++) {
                if (access("/usr/bin/xclip", X_OK)==0 || access("/bin/xclip", X_OK)==0 || access("/usr/bin/xsel", X_OK)==0) {
                  char cmd[8192]; snprintf(cmd, sizeof cmd, "printf '%%s' %s | %s ", "", clips[ci]);
                  /* not robust for arbitrary content; skip security-wise */
                  break;
                }
              }
#endif
              size_t a,b; ed_sel_range(&a,&b); ed_delete_range(a,b); ed.cur = a;
            }
            free((void*)s);
          } break;
          case KEY_C: break; /* copy: no clipboard util wired; no-op */
          case KEY_V: break; /* paste: no clipboard util wired; no-op */
          case KEY_Z: break; /* undo: not implemented */
          default: break;
        }
      }
      k = GetKeyPressed();
    }

    /* printable char input */
    int cp;
    while ((cp = GetCharPressed()) != 0) {
      if (cp >= 32 && cp < 127) ed_insert_cp((unsigned)cp);
      else if (cp >= 160) ed_insert_cp((unsigned)cp);
    }
  }

  /* horizontal scrolling with shift+wheel? use plain wheel over editor for
   * vertical, and ctrl+wheel for horizontal. Keep it simple: wheel = vertical */
  if (GetMouseY() < con_top_area && !GetMouseY() < 0) { /* hover editor region only */
    /* vertical wheel handled below */
  }

  /* mouse scroll (editor area) */
  if (GetMouseY() > ed_top && GetMouseY() < con_top_area) {
    float mw = GetMouseWheelMove();
    if (mw != 0) {
      if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        ed.view_col += (int)(mw * 4);
        if (ed.view_col < 0) ed.view_col = 0;
      } else {
        ed.view_line -= (int)mw;
        if (ed.view_line < 0) ed.view_line = 0;
        if (ed.view_line > lines_n - 1) ed.view_line = lines_n - 1 > 0 ? lines_n - 1 : 0;
      }
    }
  }

  /* follow cursor into view */
  if (cli < ed.view_line) ed.view_line = cli;
  if (cli > ed.view_line + vis_lines - 1) ed.view_line = cli - vis_lines + 1;

  /* clamp scroll column so cursor / selection is visible (simple) */
  {
    int ccol = ed_col_of(ed.cur);
    int charcols = area_w / (int)char_w;
    if (ccol < ed.view_col) ed.view_col = ccol;
    if (ccol > ed.view_col + charcols - 1) ed.view_col = ccol - charcols + 3;
  }

  /* ---------- render editor lines ---------- */
  for (int i = 0; i < vis_lines; i++) {
    int li = ed.view_line + i;
    if (li >= lines_n) break;
    size_t ls = ed_line_start(li);
    size_t ll = ed_line_len(li);
    float y = ed_top + i * line_h + 1;

    /* current line highlight */
    if (li == cli) DrawRectangle(0, y-1, w, line_h, (Color){38,38,44,255});

    /* error line: red left bar + light red bg */
    bool is_err = false;
    for (int e = 0; e < ed.err_count; e++) if (ed.err[e] == li+1) { is_err = true; break; }
    if (is_err) {
      DrawRectangle(0, y-1, 4, line_h, (Color){230,70,70,255});
      DrawRectangle(4, y-1, w-4, line_h, (Color){52,30,30,200});
    }

    /* gutter: line number */
    char nl[16]; snprintf(nl, sizeof nl, "%d", li+1);
    Color ncol = (is_err || li == cli) ? (Color){220,220,220,255} : (Color){110,110,118,255};
    float nw = MeasureTextEx(font, nl, fs, 0.0f).x;
    DrawTextEx(font, nl, (Vector2){gutter_w - nw - 5, y}, fs, 0.0f, ncol);

    /* text, clipped by area */
    int cols = area_w / (int)char_w;
    if (cols < 0) cols = 0;
    int start_col = ed.view_col;
    if (start_col > (int)ll) start_col = ll;
    size_t trim = (size_t)start_col;
    int drawlen = (int)ll - start_col;
    if (drawlen > cols) drawlen = cols;

    BeginScissorMode(area_l, ed_top, area_w, ed_h);
    if (drawlen > 0) {
      char *lined = malloc((size_t)drawlen + 1);
      memcpy(lined, ed.buf + ls + trim, (size_t)drawlen); lined[drawlen] = 0;
      float gx = area_l + (trim - (size_t)start_col) * char_w; /* equals area_l */
      DrawTextEx(font, lined, (Vector2){gx, y}, fs, 0.0f, (Color){225,225,228,255});
      free(lined);
    }
    /* selection highlight */
    {
      size_t sa, sb; ed_sel_range(&sa, &sb);
      if (sa != sb) {
        int la = ed_line_of(sa), lb = ed_line_of(sb);
        if (li >= la && li <= lb) {
          size_t c1 = (li==la) ? (sa - ls) : 0;
          size_t c2 = (li==lb) ? (sb - ls) : ll;
          if (c1 > ll) c1 = ll; if (c2 > ll) c2 = ll;
          float selx = area_l + ((long)c1 > (long)start_col ? (long)c1 - start_col : 0) * char_w;
          float selw = ((long)c2 - (long)start_col) * char_w;
          if (c1 < (size_t)start_col) { selx = area_l; selw += ((long)start_col - (long)c1)*char_w; }
          if (selw < 0) selw = 0;
          DrawRectangle((int)selx, (int)y-1, (int)selw, (int)line_h, (Color){58,96,140,200});
        }
      }
    }
    EndScissorMode();
  }

  /* cursor */
  if (state == ST_IDLE || state == ST_BUILD) {
    double now = GetTime();
    double dt = now - last_blink;
    /* solid briefly on any input, then blink on a 0.5s cycle */
    int blink = ((int)(dt / 0.5)) & 1;
    int show = (dt < 0.15) || (blink == 0);
    if (show) {
      int cl = ed_cur_line();
      if (cl >= ed.view_line) {
        int ccol = ed_col_of(ed.cur);
        int c = cl - ed.view_line;
        int x = (int)(area_l + (ccol - ed.view_col) * char_w);
        int y = (int)(ed_top + c * line_h + 1);
        if (ccol >= ed.view_col) DrawRectangle(x, y-1, (int)(char_w < 2 ? 1 : 1), (int)line_h, (Color){180,180,190,255});
      }
    }
  }

  /* mouse interaction with editor (set cursor / selection) */
  if (IsMouseButtonPressed(0) && GetMouseY() > ed_top && GetMouseY() < con_top_area && GetMouseX() > area_l) {
    ed.cur = ed_click_to_off(GetMouseX(), GetMouseY(), area_l, ed_top);
    ed.sel = SIZE_MAX; last_blink = GetTime();
  } else if (IsMouseButtonDown(0) && GetMouseY() > ed_top && GetMouseY() < con_top_area && GetMouseX() > area_l) {
    if (ed.sel == SIZE_MAX) ed.sel = ed.cur;
    ed.cur = ed_click_to_off(GetMouseX(), GetMouseY(), area_l, ed_top);
  }
}

/* Simple immediate-mode button helper */
static int ButtonLike(const char *label, int x, int y, int colw, int *yout) {
  (void)yout;
  Rectangle r = { x, y, colw, 20 };
  Color c = (Color){55,58,64,255};
  if (CheckCollisionPointRec(GetMousePosition(), r)) c = (Color){72,76,84,255};
  DrawRectangleRec(r, c);
  float tw = MeasureTextEx(font, label, fs, 0.0f).x;
  DrawTextEx(font, label, (Vector2){ x + (colw-tw)/2, y+3 }, fs, 0.0f, (Color){230,230,235,255});
  return IsMouseButtonPressed(0) && CheckCollisionPointRec(GetMousePosition(), r);
}

/* Build a Font directly from the font's EMBEDDED bitmap strikes (the authentic
 * hand-drawn pixel glyphs that TTF outline rasterization never produces).
 * raylib's own TTF loader uses stb_truetype which can only rasterize outlines,
 * so we bypass it: use FreeType to fetch the embedded monochrome bitmaps at the
 * requested pixel size, then assemble GlyphInfo[] using raylib's conventions
 * (offsetX = bitmap_left, offsetY = ascent - bitmap_top, advanceX = px advance),
 * and pack them into a raylib atlas. Falls back to the default font on failure. */
static Font load_pixel_font(const char *path, int px) {
  Font f = {0};
  FT_Library lib = NULL;
  FT_Face face = NULL;
  FILE *fp = fopen(path, "rb");
  if (!fp) return GetFontDefault();
  fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
  unsigned char *data = malloc((size_t)sz);
  if (!data) { fclose(fp); return GetFontDefault(); }
  size_t got = fread(data, 1, (size_t)sz, fp); fclose(fp);
  if (got != (size_t)sz) { free(data); return GetFontDefault(); }

  if (FT_Init_FreeType(&lib) != 0) { free(data); return GetFontDefault(); }
  if (FT_New_Memory_Face(lib, data, (FT_Long)sz, 0, &face) != 0) {
    FT_Done_FreeType(lib); free(data); return GetFontDefault();
  }

  /* Select the embedded strike whose ppem matches px (12,14,16,...32).
   * If none matches exactly, fall back to the first reaching/exceeding px. */
  int chosen = -1;
  for (int i = 0; i < face->num_fixed_sizes; i++) {
    FT_Bitmap_Size bs = face->available_sizes[i];
    int ppem = (int)(((bs.y_ppem > 0 ? bs.y_ppem : bs.height) + 32) / 64);  /* 26.6 -> px */
    if (ppem == px) { chosen = i; break; }            /* exact match preferred */
    if (chosen < 0 && ppem > px) chosen = i;          /* first reaching px */
  }
  if (chosen < 0) {
    /* no embedded strike at/below requested size; rasterize outlines in mono */
    FT_Done_FreeType(lib); free(data); return GetFontDefault();
  }
  if (FT_Select_Size(face, chosen) != 0) {
    FT_Done_FreeType(lib); free(data); return GetFontDefault();
  }

  int ascent = (int)(face->size->metrics.ascender / 64);
  int cps[] = { 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,
                58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,
                91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,
                117,118,119,120,121,122,123,124,125,126 };
  int ncp = (int)(sizeof(cps)/sizeof(cps[0]));
  GlyphInfo *glyphs = calloc((size_t)ncp, sizeof(GlyphInfo));
  Rectangle *recs = NULL;
  int gcount = 0;

  for (int i = 0; i < ncp; i++) {
    int cp = cps[i];
    FT_UInt gid = FT_Get_Char_Index(face, (FT_ULong)cp);
    if (gid == 0) continue;                     /* glyph not present */
    if (FT_Load_Glyph(face, gid, FT_LOAD_RENDER) != 0) continue;   /* uses embedded bitmap */
    FT_Bitmap *b = &face->glyph->bitmap;
    int bw = (int)b->width, bh = (int)b->rows;
    int stride = (int)b->pitch;
    if (stride < 0) stride = -stride;

    /* Terminus monospace: each glyph is a full cell; tolerate empty space glyph */
    if (cp == 32 && (bw == 0 || bh == 0)) {
      int a = (int)(face->glyph->advance.x/64);
      Image sp = { .data = calloc((size_t)(a>0?a:px)* (size_t)px, 1),
                   .width = a>0?a:px, .height = px, .mipmaps = 1,
                   .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE };
      glyphs[gcount].value = cp; glyphs[gcount].advanceX = a>0?a:px;
      glyphs[gcount].offsetX = 0; glyphs[gcount].offsetY = 0; glyphs[gcount].image = sp;
      gcount++; continue;
    }
    if (bw <= 0 || bh <= 0) continue;

    /* copy embedded monochrome bitmap (1 bpp) -> 8-bit GRAYSCALE (0/255) */
    unsigned char *pxd = calloc((size_t)bw*(size_t)bh, 1);
    const unsigned char *src = b->buffer;
    for (int y = 0; y < bh; y++) {
      const unsigned char *row = src + (long)y*stride;
      for (int x = 0; x < bw; x++) {
        pxd[(size_t)y*bw + (size_t)x] = (row[x>>3] >> (7-(x&7))) & 1 ? 255 : 0;
      }
    }
    Image gi = { .data = pxd, .width = bw, .height = bh, .mipmaps = 1,
                 .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE };
    glyphs[gcount].value = cp;
    glyphs[gcount].offsetX = face->glyph->bitmap_left;
    glyphs[gcount].offsetY = ascent - face->glyph->bitmap_top;
    glyphs[gcount].advanceX = (int)(face->glyph->advance.x/64);
    glyphs[gcount].image = gi;
    gcount++;
  }

  FT_Done_FreeType(lib);
  free(data);

  if (gcount <= 0) { free(glyphs); return GetFontDefault(); }

  Image atlas = GenImageFontAtlas(glyphs, &recs, gcount, px, 0, 0);
  if (atlas.data == NULL) { for (int i=0;i<gcount;i++) UnloadImage(glyphs[i].image); free(glyphs); return GetFontDefault(); }

  f.baseSize = px;
  f.glyphCount = gcount;
  f.glyphPadding = 0;
  f.texture = LoadTextureFromImage(atlas);
  f.recs = recs;
  f.glyphs = glyphs;
  UnloadImage(atlas);
  return f;
}

int main(int argc, char **argv) {
  /* script dir = directory of this binary */
  {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) { strcpy(script_dir, "."); }
    else { buf[n] = 0; char *sl = strrchr(buf, '/'); if (sl) *sl = 0; snprintf(script_dir, sizeof script_dir, "%s", buf); }
  }

  if (!native_dialogs) { (void)0; }
  dialogs_checked = 1;
  native_dialogs = have_native_dialogs();

  srand((unsigned)time(NULL));

  InitWindow(800, 520, "pdeide — Processing sketch editor");
  SetTargetFPS(60);

  char fontpath[PATH_MAX]; snprintf(fontpath, sizeof fontpath, "%s/terminus.ttf", script_dir);
  font = load_pixel_font(fontpath, (int)fs);
  if (font.texture.id == 0) font = GetFontDefault();
  SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);   /* crisp pixel glyphs */
  line_h = (float)font.baseSize + 3.0f;
  /* true per-character advance: measure a long run and divide. (MeasureTextEx
   * of a single char underestimates, causing click/selection/caret drift.) */
  char_w = MeasureTextEx(font, "MMMMMMMMMMMMMMMMMMMMMM", fs, 0.0f).x / 22.0f;
  if (char_w < 1) char_w = fs * 0.6f;
  line_h = (float)(int)line_h;

  ed_init();

  /* initial sketch: argv[1] or sketch.pde if present in script dir */
  if (argc > 1 && argv[1][0]) {
    load_file(argv[1]);
  } else {
    char def[PATH_MAX]; snprintf(def, sizeof def, "%s/sketch.pde", script_dir);
    if (access(def, R_OK) == 0) load_file(def);
  }

  /* build dir */
  {
    char tmpl[] = "/tmp/pdeide.XXXXXX";
    char *d = mkdtemp(tmpl);
    if (d) snprintf(build_dir, sizeof build_dir, "%s", d);
    else  snprintf(build_dir, sizeof build_dir, "/tmp/pdeide.%d", (int)getpid());
    mkdir(build_dir, 0700);
    /* copy terminus into build dir so running sketch resolves it */
    char fs[PATH_MAX]; char fs2[PATH_MAX];
    snprintf(fs, sizeof fs, "%s/terminus.ttf", script_dir);
    snprintf(fs2, sizeof fs2, "%s/terminus.ttf", build_dir);
    if (access(fs, R_OK) == 0) {
      FILE *a = fopen(fs, "rb"); FILE *b = fopen(fs2, "wb");
      if (a && b) { char bf[8192]; size_t r; while ((r=fread(bf,1,sizeof bf,a))>0) fwrite(bf,1,r,b); }
      if (a) fclose(a); if (b) fclose(b);
    }
  }

  con_add("pdeide ready. edit a sketch, press Run.", CON_STATUS);

  while (!WindowShouldClose()) {
    BeginDrawing();
    frame();
    EndDrawing();
  }

  stop_sketch();
  UnloadFont(font);
  CloseWindow();
  return 0;
}
