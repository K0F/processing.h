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

#include "editor.h"
#include "platform.h"

#include <errno.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <process.h>
#define read  _read
#define close _close
#ifndef ssize_t
typedef intptr_t ssize_t;
#endif
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ------------------------------------------------------------------ */
/* small utilities                                                     */
/* ------------------------------------------------------------------ */

static char script_dir[PATH_MAX];
static char* str_dup(const char *s)    { size_t n = strlen(s)+1; char *d = malloc(n); memcpy(d, s, n); return d; }

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
static double last_blink = 0;   /* caret blink phase, reset on any input */
static Font font;
static float fs = 12.0f;            /* pixel font size (crisp with POINT filter) */
static float line_h, char_w;        /* integer glyph metrics, set after load */

/* manual key auto-repeat (raylib does not synthesize repeats for GetKeyPressed) */
#define KEYREP_DELAY 0.42
#define KEYREP_RATE  0.035
static int    key_repeat_key  = 0;
static double key_repeat_timer = 0;

/* ------------------------------------------------------------------ */
/* olive dark palette                                                  */
/* ------------------------------------------------------------------ */
#define OL_BG      ((Color){16, 18, 14, 255})   /* window backdrop       */
#define OL_CON     ((Color){13, 15, 11, 255})   /* console / lower pane  */
#define OL_ED      ((Color){24, 28, 21, 255})   /* editor background     */
#define OL_TB      ((Color){33, 38, 29, 255})   /* toolbar               */
#define OL_LINE    ((Color){50, 57, 42, 255})   /* current line          */
#define OL_SEL     ((Color){72, 98, 48, 210})   /* selection             */
#define OL_GUTTER  ((Color){116, 124, 100, 255})
#define OL_TEXT    ((Color){225, 228, 210, 255})
#define OL_DIM     ((Color){150, 156, 132, 255})
#define OL_FAINT   ((Color){92, 98, 80, 255})
#define OL_EDGE    ((Color){56, 62, 46, 255})   /* separating lines      */
#define OL_SHADOW  ((Color){8, 10, 7, 255})     /* 1px drop shadow under lines/btns */
#define OL_BTN     ((Color){61, 74, 47, 255})   /* steady button         */
#define OL_BTNH    ((Color){74, 89, 57, 255})   /* hover button          */
#define OL_RUN     ((Color){44, 88, 40, 255})
#define OL_RUNH    ((Color){58, 112, 50, 255})
#define OL_STOP    ((Color){40, 56, 42, 255})
#define OL_STOPH   ((Color){88, 64, 40, 255})
#define OL_CURSOR  ((Color){196, 202, 180, 255})
#define OL_ERRBAR  ((Color){200, 84, 74, 255})
#define OL_ERRBG   ((Color){56, 34, 30, 200})
#define OL_KW      ((Color){255, 204, 0, 255})     /* keywords: bright yellow #FFCC00 */
#define OL_STR     ((Color){245, 245, 240, 255})   /* strings:  bright white          */
#define OL_NUM     ((Color){235, 235, 230, 255})   /* numbers:  bright white          */
#define OL_COM     ((Color){114, 122, 98, 255})    /* comments: muted grey-olive      */

/* ------------------------------------------------------------------ */
/* editor syntax highlighting                                          */
/* ------------------------------------------------------------------ */
typedef enum { CTXT_NORM, CTXT_STR, CTXT_NUM, CTXT_KW, CTXT_COM } CodeT;

static const char *kws[] = {
  "abstract","boolean","break","byte","case","catch","char","class","color","const",
  "continue","default","do","double","else","enum","extends","final","finally","float",
  "for","if","implements","import","instanceof","int","interface","long","native","new",
  "package","private","protected","public","return","short","static","strictfp","super",
  "switch","synchronized","this","throw","throws","transient","try","void","volatile","while",
  "true","false","null","PVector","String","PImage","PGraphics","ArrayList","M_PI",
  "settings","setup","draw","println","print","size","width","height","frameRate",
  "millis","random","noise","map","constrain","color","stroke","noStroke","fill",
  "noFill","strokeWeight","line","rect","ellipse","circle","square","triangle","quad",
  "arc","point","text","textSize","textAlign","loadImage","image","loadPixels",
  "updatePixels","beginShape","endShape","vertex","translate","rotate","scale",
  "pushMatrix","popMatrix","mouseX","mouseY","pmouseX","pmouseY","key","keyCode",
  "keyPressed","mousePressed","mouseReleased","mouseDragged","mouseMoved","mouseWheel",
  "noLoop","loop","redraw","exit", NULL };

typedef struct { int col; int len; CodeT t; } Run;

/* Classify a line into colored runs. `col` is the running byte column (starts
 * at the byte offset of *str within the line/buffer); *in_comment carries the
 * `/* ... *​/` state across lines. Each run advances `col` by its byte length. */
static void hl_line(const char *str, int len, int *col, bool *in_comment,
                    Run *runs, int *nruns, int maxruns) {
  int i = 0;
  while (i < len) {
    int s = i;
    CodeT t = CTXT_NORM;

    if (*in_comment) {
      t = CTXT_COM;
      while (i < len) {
        if (str[i]=='*' && i+1<len && str[i+1]=='/') { i += 2; *in_comment = false; break; }
        i++;
      }
    }
    else if (str[i]=='/' && i+1<len && str[i+1]=='/') {
      t = CTXT_COM; i = len;                          /* line comment */
    }
    else if (str[i]=='/' && i+1<len && str[i+1]=='*') {
      t = CTXT_COM; i += 2; *in_comment = true;
      while (i < len) {
        if (str[i]=='*' && i+1<len && str[i+1]=='/') { i += 2; *in_comment = false; break; }
        i++;
      }
    }
    else if (str[i]=='"' || str[i]=='\'') {
      char q = str[i]; t = CTXT_STR; i++;
      while (i < len) {
        if (str[i]=='\\' && i+1<len) { i += 2; continue; }
        if (str[i]==q) { i++; break; }
        i++;
      }
    }
    else if ((str[i]>='0' && str[i]<='9') ||
             (str[i]=='.' && i+1<len && str[i+1]>='0' && str[i+1]<='9')) {
      t = CTXT_NUM;
      unsigned char c;
      if (str[i]=='0' && i+1<len && (str[i+1]=='x'||str[i+1]=='X')) {     /* hex */
        i += 2;
        while (i < len && ((c=str[i], (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))) i++;
      } else {
        while (i < len && (c=str[i], c>='0'&&c<='9')) i++;
        if (i+1<len && str[i]=='.' && str[i+1]>='0' && str[i+1]<='9') {   /* fraction */
          i++;
          while (i < len && (c=str[i], c>='0'&&c<='9')) i++;
        }
        if (i < len && (str[i]=='e'||str[i]=='E')) {                      /* exponent */
          i++;
          if (i < len && (str[i]=='+'||str[i]=='-')) i++;
          while (i < len && (c=str[i], c>='0'&&c<='9')) i++;
        }
      }
    }
    else if ((str[i]>='a'&&str[i]<='z') || (str[i]>='A'&&str[i]<='Z') || str[i]=='_') {
      t = CTXT_KW;
      while (i < len && ((str[i]>='a'&&str[i]<='z')||(str[i]>='A'&&str[i]<='Z')||
                         (str[i]>='0'&&str[i]<='9')||str[i]=='_')) i++;
      if (t == CTXT_KW) {                 /* verify it is a real keyword */
        int wl = i - s; int isk = 0;
        for (int k = 0; kws[k]; k++) if ((int)strlen(kws[k])==wl && strncmp(kws[k], str+s, (size_t)wl)==0) { isk = 1; break; }
        if (!isk) t = CTXT_NORM;
      }
    }
    else { t = CTXT_NORM; i++; }          /* punctuation: single default char */

    if (*nruns < maxruns) { runs[*nruns].col = *col; runs[*nruns].len = i - s; runs[*nruns].t = t; (*nruns)++; }
    *col += (i - s);
  }
}

static Color hl_color(CodeT t) {
  switch (t) {
    case CTXT_KW:  return OL_KW;
    case CTXT_STR: return OL_STR;
    case CTXT_NUM: return OL_NUM;
    case CTXT_COM: return OL_COM;
    default:       return OL_TEXT;
  }
}

/* per-line block-comment-on state (recomputed each frame) so a multi-line
 * block comment keeps its color across the visible window */
static bool *hl_com = NULL;
static int   hl_com_n = 0;

static void hl_sweep(int nlines) {
  if (nlines > hl_com_n) {
    hl_com = realloc(hl_com, (size_t)nlines * sizeof(bool));
    hl_com_n = nlines;
  }
  bool ic = false;
  for (int li = 0; li < nlines; li++) {
    hl_com[li] = ic;
    int col = 0; Run r[8]; int nr = 0;
    hl_line(ed.buf + ed_line_start(li), (int)ed_line_len(li), &col, &ic, r, &nr, 8);
  }
}

/* reset caret blink phase on any editor change/move */
static void reset_blink(void) { last_blink = GetTime(); }

/* highest-priority held key among the auto-repeat set, or 0 */
static int ed_held_repeat_key(void) {
  const int order[] = { KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_BACKSPACE,
                        KEY_DELETE, KEY_HOME, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN };
  for (int i = 0; i < (int)(sizeof order / sizeof order[0]); i++)
    if (IsKeyDown(order[i])) return order[i];
  return 0;
}

/* apply a single editor keystroke (shared by press + auto-repeat) */
static void ed_handle_key(int k, bool shift, bool ctrl) {
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
      case KEY_A: ed_select_all(); break;
      case KEY_X: case KEY_C: {
        const char *s = ed_get_selected();
        if (s) {
          SetClipboardText(s);
          if (k == KEY_X) { size_t a,b; ed_sel_range(&a,&b); ed_delete_range(a,b); ed.cur = a; }
        }
        free((void*)s);
      } break;
      case KEY_V: {
        const char *cb = GetClipboardText();
        if (cb) { size_t n = strlen(cb); if (n) ed_insert_text(cb, n); }
      } break;
      case KEY_Z: break; /* undo: not implemented */
      default: break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* persistent run/build                                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
  plat_pid_t pid;
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
 * Returns the child's exit status, or -1 on exec/spawn failure.
 * On success the captured output is stored (NUL-terminated) in *out. */
static int spawn_capture(char *const argv[], const char *stdout_file, char **base_out) {
  char *buf = NULL;
  int st = plat_spawn_capture(argv, stdout_file, &buf);
  if (st < 0) return -1;
  *base_out = buf ? buf : str_dup("");
  return st;
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
  if (state != ST_RUN || sk.pid == 0) return;
  plat_kill(sk.pid);
  /* wait a moment; escalate */
  for (int i = 0; i < 60; i++) {
    int st = 0, sig = -1;
    int r = plat_wait_nohang(sk.pid, &st, &sig);
    if (r == 0) break;
    plat_sleep_ms(50);
  }
  if (sk.pid != 0 && plat_wait_nohang(sk.pid, &(int){0}, &(int){-1}) != 0) {
    plat_kill_force(sk.pid);
    for (int i = 0; i < 60; i++) {
      int st = 0, sig = -1;
      if (plat_wait_nohang(sk.pid, &st, &sig) == 0) break;
      plat_sleep_ms(50);
    }
  }
  if (sk.out_fd >= 0) { close(sk.out_fd); sk.out_fd = -1; }
  if (sk.err_fd >= 0) { close(sk.err_fd); sk.err_fd = -1; }
  sk.pid = 0;
  state = ST_IDLE;   /* re-enable Run / Ctrl-R / keyboard input */
}

/* run a shell command and split its stdout on whitespace into tok[], returning
 * the token count (used to fetch compiler flags like pkg-config's). */
static int flags_tokens(const char *cmd, char tok[][256]) {
  char *out = plat_shell_read(cmd);
  if (!out) return 0;
  int n = 0;
  char *p = out;
  while (*p && n < 16) {
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    if (!*p) break;
    char *s = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
    int l = (int)(p - s);
    memcpy(tok[n], s, (size_t)l); tok[n][l] = 0; n++;
  }
  free(out);
  return n;
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

    /* Prefer the vendored static raylib+freetype that CMake builds into
     * build/static (what the Export button links against too), so Run needs
     * no system install. Otherwise fall back to system packages. */
    char ray_static[PATH_MAX], ft_static[PATH_MAX], ray_inc[PATH_MAX],
         ft_inc[PATH_MAX], ft_build_inc[PATH_MAX];
    snprintf(ray_static, sizeof ray_static, "%s/build/static/raylib/raylib/libraylib.a", script_dir);
    snprintf(ft_static,  sizeof ft_static,  "%s/build/static/freetype/libfreetype.a", script_dir);
    int vendored = plat_file_access(ray_static, 0) == 0 &&
                   plat_file_access(ft_static, 0) == 0;
    /* fallback cflags/libs: the sketch runtime loads Terminus's embedded
     * bitmap strikes via FreeType */
    static char ft_cf[8][256]; int n_ft_cf = 0;
    static char ft_lib[4][256]; int n_ft_lib = 0;
    if (!vendored) {
      n_ft_cf  = flags_tokens("pkg-config --cflags freetype2 2>/dev/null", ft_cf);
      n_ft_lib = flags_tokens("pkg-config --libs freetype2 2>/dev/null", ft_lib);
    }
    static const char *gl[] = { "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11", NULL };
    int n_gl = (int)(sizeof gl / sizeof gl[0]) - 1;

    int cap = 5 + (vendored ? 3 + 2 + n_gl : n_ft_cf + 1 + n_ft_lib + n_gl) + 1;
    char **argv = calloc((size_t)cap, sizeof(char *));
    int ai = 0;
    argv[ai++] = gcc; argv[ai++] = O; argv[ai++] = Idir_arg; argv[ai++] = sketch_c; argv[ai++] = out;
    if (vendored) {
      snprintf(ray_inc,      sizeof ray_inc,      "-I%s/third_party/raylib/src", script_dir);
      snprintf(ft_inc,       sizeof ft_inc,       "-I%s/third_party/freetype/include", script_dir);
      snprintf(ft_build_inc, sizeof ft_build_inc, "-I%s/build/static/freetype/include", script_dir);
      argv[ai++] = ray_inc; argv[ai++] = ft_inc; argv[ai++] = ft_build_inc;
      argv[ai++] = ray_static; argv[ai++] = ft_static;
    } else {
      for (int i = 0; i < n_ft_cf; i++) argv[ai++] = ft_cf[i];
      argv[ai++] = "-lraylib";
      for (int i = 0; i < n_ft_lib; i++) argv[ai++] = ft_lib[i];
    }
    for (int i = 0; i < n_gl; i++) argv[ai++] = (char*)gl[i];
    argv[ai] = NULL;
    int rc = spawn_capture(argv, NULL, &gerr);
    free(argv);
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

  plat_chmod_exec(sketch_out);
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
  int of = -1, ef = -1;
  plat_pid_t pid = 0;
  char *argv2[] = { sketch_bin, NULL };
  if (plat_spawn_run(argv2, build_dir[0] ? build_dir : NULL, &of, &ef, &pid) != 0) {
    state = ST_IDLE; con_add("run: spawn failed", CON_ERR); return;
  }
  sk.pid = pid; sk.out_fd = of; sk.err_fd = ef;
  sk.out_eof = 0; sk.err_eof = 0;
  sout.n = 0; serr.n = 0;
  plat_set_nonblocking(sk.out_fd);
  plat_set_nonblocking(sk.err_fd);
  state = ST_RUN;
  con_add("== running (pid %ld) ==", CON_OK, (long)pid);
}

/* poll the running sketch each frame */
static void poll_sketch(void) {
  if (state != ST_RUN) return;
  if (!sk.out_eof) stream_drain(sk.out_fd, &sout);
  if (!sk.err_eof) stream_drain(sk.err_fd, &serr);
  int status = 0, sig = -1;
  int r = plat_wait_nohang(sk.pid, &status, &sig);
  if (r == 0) {
    if (sk.out_fd >= 0) close(sk.out_fd); if (sk.err_fd >= 0) close(sk.err_fd);
    sk.pid = 0;
    if (sig >= 0) {
      con_add("== sketch crashed (signal %d) ==", CON_ERR, sig);
    } else if (status == 0) {
      con_add("== sketch exited cleanly ==", CON_OK);
    } else {
      con_add("== sketch exited with code %d ==", CON_ERR, status);
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
static void cmd_export(void);
static void export_static(const char *dest_path);
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
#ifdef _WIN32
  return 0;   /* small C shell only: text input boxes are used instead */
#else
  const char *bins[] = { "zenity", "kdialog", "yad", "qarma", "matedialog", "Xdialog", NULL };
  for (int i = 0; bins[i]; i++) {
    /* search PATH manually */
    char *path = str_dup(getenv("PATH") ? getenv("PATH") : "");
    char *tok = strtok(path, ":");
    while (tok) {
      char f[PATH_MAX]; snprintf(f, sizeof f, "%s/%s", tok, bins[i]);
      if (plat_file_access(f, 1) == 0) { free(path); return 1; }
      tok = strtok(NULL, ":");
    }
    free(path);
  }
  return 0;
#endif
}

static int native_dialogs = 1, dialogs_checked = 0;
static int show_about = 0;

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

/* Compile the current sketch into a single statically-linked, self-contained
 * executable. The sketch runtime links against the vendored static raylib +
 * freetype archives and embeds the terminus font, so the produced binary only
 * depends on the host's base system libraries (libc/libm/libGL/libX11) and can
 * be copied to an identical distro and run without any extra files. */
static void export_static(const char *dest_path) {
  /* 1. write the source .pde with a //@file marker (same as build_pipeline) */
  const char *base = ed.path[0] ? ed.path : "sketch.pde";
  const char *slash = strrchr(base, '/');
  if (slash) base = slash + 1;
  if (strlen(base) < 4) base = "sketch.pde";
  char pde_path[PATH_MAX]; snprintf(pde_path, sizeof pde_path, "%s/%s", build_dir, base);
  FILE *fp = fopen(pde_path, "wb");
  if (!fp) { con_add("export: cannot write %s (%s)", CON_ERR, pde_path, strerror(errno)); return; }
  fprintf(fp, "//@file %s\n", base);
  fwrite(ed.buf, 1, ed.len, fp);
  fclose(fp);

  ed_clear_errors();
  con_clear();
  con_add("== exporting static binary... ==", CON_STATUS);

  /* 2. transpile */
  char pde2c[PATH_MAX]; snprintf(pde2c, sizeof pde2c, "%s/pde2c", script_dir);
  char sketch_c[PATH_MAX]; snprintf(sketch_c, sizeof sketch_c, "%s/%s.c", build_dir, base);
  char *cap = NULL;
  {
    char *argv[] = { pde2c, pde_path, NULL };
    int rc = spawn_capture(argv, sketch_c, &cap);
    if (rc != 0) {
      if (rc == 127) con_add("export: pde2c not found", CON_ERR);
      handle_diagnostics(cap ? cap : "");
      free(cap);
      con_add("EXPORT FAILED (transpile)", CON_ERR);
      return;
    }
  }
  free(cap);

  /* 3. compile sketch to an object, embedding the font + static headers */
  char obj[PATH_MAX]; snprintf(obj, sizeof obj, "%s/%s.o", build_dir, base);
  char *cerr = NULL;
  {
    char Idir[PATH_MAX]; snprintf(Idir, sizeof Idir, "-I%s", script_dir);
    char Iray[PATH_MAX]; snprintf(Iray, sizeof Iray, "-I%s/third_party/raylib/src", script_dir);
    char Ift[PATH_MAX];  snprintf(Ift,  sizeof Ift,  "-I%s/third_party/freetype/include", script_dir);
    char Iftb[PATH_MAX]; snprintf(Iftb, sizeof Iftb, "-I%s/build/static/freetype/include", script_dir);
    char *argv[] = {
      "gcc", "-c", sketch_c, "-o", obj, "-O2", "-DPDEIDE_EMBEDDED_FONT",
      Idir, Iray, Ift, Iftb, NULL
    };
    int rc = spawn_capture(argv, NULL, &cerr);
    if (rc != 0) {
      handle_diagnostics(cerr ? cerr : "");
      free(cerr);
      con_add("EXPORT FAILED (compile)", CON_ERR);
      return;
    }
    free(cerr);
  }

  /* 4. link statically against the vendored archives */
  char *lerr = NULL;
  {
    char Lray[PATH_MAX]; snprintf(Lray, sizeof Lray, "%s/build/static/raylib/raylib/libraylib.a", script_dir);
    char Lft[PATH_MAX];  snprintf(Lft,  sizeof Lft,  "%s/build/static/freetype/libfreetype.a", script_dir);
    char out[PATH_MAX];  snprintf(out,   sizeof out,  "-o%s", dest_path);
    char *argv[] = {
      "gcc", obj, out, Lray, Lft,
      "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11", NULL
    };
    int rc = spawn_capture(argv, NULL, &lerr);
    if (rc != 0) {
      char *dup = str_dup(lerr ? lerr : "");
      char *tok = strtok(dup, "\n");
      int shown = 0;
      while (tok) {
        long ln; const char *kind, *msg;
        if (parse_diag(tok, &ln, &kind, &msg) && strcmp(kind, "error") == 0) {
          if (shown < 20) con_add("L%ld: %s", CON_ERR, ln, msg);
          shown++;
        } else if (tok[0]) {
          if (shown < 20) con_add_full(tok, CON_ERR);
          shown++;
        }
        tok = strtok(NULL, "\n");
      }
      free(dup);
      free(lerr);
      if (shown == 0) con_add("link failed with no diagnostics", CON_ERR);
      con_add("EXPORT FAILED (link)", CON_ERR);
      return;
    }
    free(lerr);
  }

  plat_chmod_exec(dest_path);
  {   /* optional: strip to slim the binary */
    char *argv[] = { "strip", (char*)dest_path, NULL };
    char *serr = NULL;
    spawn_capture(argv, NULL, &serr);
    free(serr);
  }
  con_add("exported %s (static, self-contained)", CON_OK, dest_path);
}

/* Compile the current sketch into a single statically-linked, self-contained
 * executable. The sketch runtime links against the vendored static raylib +
 * freetype archives and embeds the terminus font, so the produced binary only
 * depends on the host's base system libraries (libc/libm/libGL/libX11) and can
 * be copied to an identical distro and run without any extra files. */
static void cmd_export(void) {
  if (ed.len == 0) { con_add("export: nothing to export", CON_ERR); return; }

  char defpath[PATH_MAX];
  {
    const char *base = ed.path[0] ? ed.path : "sketch.pde";
    const char *sl = strrchr(base, '/'); if (sl) base = sl + 1;
    char stem[PATH_MAX]; strncpy(stem, base, sizeof stem - 1); stem[sizeof stem - 1] = 0;
    char *dot = strrchr(stem, '.');
    if (dot && dot[1]) *dot = 0;
    snprintf(defpath, sizeof defpath, "%s/%s.bin", script_dir, stem);
  }

  const char *p;
  if (!native_dialogs) {
    p = tinyfd_inputBox("Export binary", "Enter output path:", defpath);
  } else {
    const char *filt[] = { "*.bin", "*" };
    p = tinyfd_saveFileDialog("Export binary", defpath, 1, filt, "Static self-contained binary");
  }
  if (p && p[0]) export_static(p);
}

static void load_file(const char *path);
static void frame(void) {
  poll_sketch();

  int w = GetScreenWidth(), h = GetScreenHeight();

  /* ---------------- toolbar ---------------- */
  const int tb = 24;
  DrawRectangle(0, 0, w, tb, OL_TB);
  DrawLine(0, tb-1, w, tb-1, OL_EDGE);   /* 1px under-toolbar line */

  /* filename / dirty indicator */
  {
    char title[PATH_MAX + 8];
    const char *base = "unnamed";
    const char *p = ed.path[0] ? ed.path : "unnamed.pde";
    const char *sl = strrchr(p, '/'); if (sl) base = sl+1; else base = p;
    snprintf(title, sizeof title, "%s%s", base, ed.dirty ? " *" : "");
    DrawTextEx(font, title, (Vector2){ 6, 5 }, fs, 0.0f, OL_TEXT);
  }

  /* run / stop buttons */
  Rectangle runb = { 170, 2, 50, 20 };
  Rectangle stopb = { 226, 2, 50, 20 };
  if (state == ST_IDLE || state == ST_BUILD) {
    if (CheckCollisionPointRec(GetMousePosition(), runb)) DrawRectangleRec(runb, OL_RUNH);
    else DrawRectangleRec(runb, OL_RUN);
    DrawLine(runb.x, runb.y+runb.height, runb.x+runb.width, runb.y+runb.height, OL_SHADOW);
    DrawLine(runb.x, runb.height+1, runb.x+runb.width, runb.height+1, OL_SHADOW);
    DrawTextEx(font, "Run", (Vector2){runb.x+18, 4}, fs, 0.0f, (Color){215,232,200,255});
    if (IsMouseButtonPressed(0) && CheckCollisionPointRec(GetMousePosition(), runb) && state == ST_IDLE) {
      start_run();
    }
    DrawRectangleRec(stopb, OL_STOP);
    DrawLine(stopb.x, stopb.y+stopb.height, stopb.x+stopb.width, stopb.y+stopb.height, OL_SHADOW);
    DrawLine(stopb.x, stopb.height+1, stopb.x+stopb.width, stopb.height+1, OL_SHADOW);
    DrawTextEx(font, "Stop", (Vector2){stopb.x+15, 4}, fs, 0.0f, OL_DIM);
  } else { /* running */
    DrawRectangleRec(runb, OL_STOP);
    DrawLine(runb.x, runb.y+runb.height, runb.x+runb.width, runb.y+runb.height, OL_SHADOW);
    DrawLine(runb.x, runb.height+1, runb.x+runb.width, runb.height+1, OL_SHADOW);
    DrawTextEx(font, "Run", (Vector2){runb.x+18, 4}, fs, 0.0f, OL_DIM);
    if (CheckCollisionPointRec(GetMousePosition(), stopb)) DrawRectangleRec(stopb, OL_STOPH);
    else DrawRectangleRec(stopb, (Color){124,54,36,255});
    DrawLine(stopb.x, stopb.y+stopb.height, stopb.x+stopb.width, stopb.y+stopb.height, OL_SHADOW);
    DrawLine(stopb.x, stopb.height+1, stopb.x+stopb.width, stopb.height+1, OL_SHADOW);
    DrawTextEx(font, "Stop", (Vector2){stopb.x+15, 4}, fs, 0.0f, (Color){230,215,200,255});
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
  bx += 64;
  if (ButtonLike("Export", bx, 2, 52, &dummy)) cmd_export();
  bx += 58;
  if (ButtonLike("About", bx, 2, 56, &dummy)) show_about = 1;

  /* status text right-aligned (with a permanent ALPHA release tag) */
  {
    const char *st;
    Color c;
    switch (state) {
      case ST_IDLE:   st = "Idle"; c = (Color){150,190,140,255}; break;
      case ST_BUILD:  st = "Building..."; c = (Color){220,198,112,255}; break;
      default:        { static char s[48]; snprintf(s,48,"Running (pid %d)",(int)sk.pid); st = s; c = (Color){150,205,190,255}; }
    }
    float vtw = MeasureTextEx(font, "0.1.1", fs, 0.0f).x;
    float atw = MeasureTextEx(font, "ALPHA", fs, 0.0f).x;
    DrawTextEx(font, "0.1.1", (Vector2){ w - atw - 10 - vtw, 5 }, fs, 0.0f, OL_FAINT);
    DrawTextEx(font, "ALPHA", (Vector2){ w - atw - 6, 5 }, fs, 0.0f, OL_FAINT);
    float tw = MeasureTextEx(font, st, fs, 0.0f).x;
    DrawTextEx(font, st, (Vector2){ w - tw - atw - vtw - 26, 5 }, fs, 0.0f, c);
  }

  /* ---------------- splitter + console area ---------------- */
  /* keep the console within sane bounds even if the window shrinks while the
   * splitter is not being dragged */
  if (console_frac < 60) console_frac = 60;
  if (console_frac > h - 120) console_frac = (h - 120 > 60) ? h - 120 : 60;
  int split_y = h - console_frac;
  DrawLine(0, split_y, w, split_y, OL_EDGE);
  DrawLine(0, split_y+1, w, split_y+1, OL_SHADOW);   /* 1px shadow under the line */
  /* drag handle */
  Color handle = OL_DIM;
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
  const int con_head = 18;                       /* header strip height */
  int con_top_area = split_y + 3 + con_head;
  int con_h = h - con_top_area;
  /* header strip with a legible label */
  DrawRectangle(0, split_y+3, w, con_head, OL_TB);
  DrawLine(0, con_top_area-1, w, con_top_area-1, OL_EDGE);
  DrawTextEx(font, "console", (Vector2){8, split_y+3+3}, fs, 0.0f, OL_DIM);
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
        case CON_ERR:    col = (Color){235,120,110,255}; break;
        case CON_WARN:   col = (Color){235,214,138,255}; break;
        case CON_STATUS: col = (Color){195,200,180,255}; break;
        case CON_OK:     col = (Color){140,205,150,255}; break;
        default:         col = (Color){205,210,190,255};
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
  /* ---------------- editor ---------------- */
  int ed_top = tb;
  int ed_h = split_y - tb;
  if (ed_h < 10) ed_h = 10;

  /* editor background */
  DrawRectangle(0, ed_top, w, ed_h, OL_ED);

  /* gutter width */
  int lines_n = ed_lines();
  hl_sweep(lines_n);               /* recompute per-line comment state */
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

  /* ---------- keyboard editing (with auto-repeat) ---------- */
  if (!show_about) {
  {
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    /* fresh key presses (single events) */
    int k;
    while ((k = GetKeyPressed()) != 0) {
      if (ctrl) {
        if (ctrl && shift) {
          if (k == KEY_UP)        { ed_move_line(-1); continue; }
          if (k == KEY_DOWN)      { ed_move_line( 1); continue; }
        }
        if (k == KEY_R) { if (state == ST_IDLE) start_run(); continue; }
        if (k == KEY_S) { stop_sketch(); continue; }
        if (k == KEY_T) { ed_pretty_format(); continue; }
        if (k == KEY_SLASH) { ed_toggle_line_comment(); continue; }
      }
      ed_handle_key(k, shift, ctrl);
      /* arm repeat timer for held navigation/editing keys */
      key_repeat_key = ctrl ? 0 : ed_held_repeat_key();
      key_repeat_timer = GetTime() + KEYREP_DELAY;
    }

    /* printable char input */
    int cp;
    while ((cp = GetCharPressed()) != 0) {
      if (cp >= 32 && cp < 127) ed_insert_cp((unsigned)cp);
      else if (cp >= 160) ed_insert_cp((unsigned)cp);
    }

    /* auto-repeat a held navigation/editing key */
    int rep = ctrl ? 0 : ed_held_repeat_key();
    if (rep) {
      if (rep != key_repeat_key) { key_repeat_key = rep; key_repeat_timer = GetTime() + KEYREP_DELAY; }
      else if (GetTime() >= key_repeat_timer) {
        ed_handle_key(rep, shift, false);
        key_repeat_timer = GetTime() + KEYREP_RATE;
      }
    } else key_repeat_key = 0;
  }
  } else if (IsKeyPressed(KEY_ESCAPE)) {
    show_about = 0;
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
    if (li == cli) DrawRectangle(0, y-1, w, line_h, OL_LINE);

    /* error line: red left bar + light red bg */
    bool is_err = false;
    for (int e = 0; e < ed.err_count; e++) if (ed.err[e] == li+1) { is_err = true; break; }
    if (is_err) {
      DrawRectangle(0, y-1, 4, line_h, OL_ERRBAR);
      DrawRectangle(4, y-1, w-4, line_h, OL_ERRBG);
    }

    /* gutter: line number */
    char nl[16]; snprintf(nl, sizeof nl, "%d", li+1);
    Color ncol = (is_err || li == cli) ? OL_TEXT : OL_GUTTER;
    float nw = MeasureTextEx(font, nl, fs, 0.0f).x;
    DrawTextEx(font, nl, (Vector2){gutter_w - nw - 5, y}, fs, 0.0f, ncol);

    /* text, clipped by area */
    int cols = area_w / (int)char_w;
    if (cols < 0) cols = 0;
    int start_col = ed.view_col;
    if (start_col > (int)ll) start_col = ll;
    int drawlen = (int)ll - start_col;
    if (drawlen > cols) drawlen = cols;

    BeginScissorMode(area_l, ed_top, area_w, ed_h);
    if (drawlen > 0) {
      /* lex the whole line for correct classification, draw only the visible slice */
      bool ic = (li < hl_com_n) ? hl_com[li] : false;
      int c0 = 0; Run runs[512]; int nruns = 0;
      hl_line(ed.buf + ls, (int)ll, &c0, &ic, runs, &nruns, 512);
      for (int ri = 0; ri < nruns; ri++) {
        Run *r = &runs[ri];
        long va = (long)r->col, vb = (long)r->col + r->len;
        long vs = va > (long)start_col ? va : (long)start_col;
        long ve = vb < (long)(start_col + drawlen) ? vb : (long)(start_col + drawlen);
        if (ve <= vs) continue;
        int wlen = (int)(ve - vs);
        float gx = area_l + (vs - (long)start_col) * char_w;
        char *sub = malloc((size_t)wlen + 1);
        memcpy(sub, ed.buf + ls + (size_t)vs, (size_t)wlen); sub[wlen] = 0;
        DrawTextEx(font, sub, (Vector2){gx, y}, fs, 0.0f, hl_color(r->t));
        free(sub);
      }
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
          /* first/last selected column visible in this viewport */
          long v1 = ((long)c1 > (long)start_col) ? (long)c1 : (long)start_col;
          long v2 = ((long)c2 > (long)start_col) ? (long)c2 : (long)start_col;
          long selwpx = (v2 - v1) * (long)char_w;
          if (selwpx > 0) {
            float selx = area_l + (v1 - (long)start_col) * char_w;
            DrawRectangle((int)selx, (int)y-1, (int)selwpx, (int)line_h, OL_SEL);
          }
        }
      }
    }
    EndScissorMode();
  }

  /* cursor (always visible so the caret position stays known) */
  {
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
        if (ccol >= ed.view_col) DrawRectangle(x, y-1, (int)(char_w < 2 ? 1 : 1), (int)line_h, OL_CURSOR);
      }
    }
  }

  /* mouse interaction with editor (set cursor / selection) */
  if (!show_about) {
  if (IsMouseButtonPressed(2) && GetMouseY() > ed_top && GetMouseY() < con_top_area && GetMouseX() > area_l) {
    /* Linux/Unix middle-click paste (same content as Ctrl+V) */
    ed.cur = ed_click_to_off(GetMouseX(), GetMouseY(), area_l, ed_top);
    ed.sel = SIZE_MAX; ed.goal_col = -1;
    const char *cb = GetClipboardText();
    if (cb) { size_t n = strlen(cb); if (n) ed_insert_text(cb, n); }
  } else if (IsMouseButtonPressed(0) && GetMouseY() > ed_top && GetMouseY() < con_top_area && GetMouseX() > area_l) {
    ed.cur = ed_click_to_off(GetMouseX(), GetMouseY(), area_l, ed_top);
    ed.sel = SIZE_MAX; ed.goal_col = -1; last_blink = GetTime();
  } else if (IsMouseButtonDown(0) && GetMouseY() > ed_top && GetMouseY() < con_top_area && GetMouseX() > area_l) {
    if (ed.sel == SIZE_MAX) ed.sel = ed.cur;
    ed.cur = ed_click_to_off(GetMouseX(), GetMouseY(), area_l, ed_top);
    ed.goal_col = -1;
  }
  }

  /* ---------------- About modal (drawn last so it overlays everything) ---- */
  if (show_about) {
    DrawRectangle(0, 0, w, h, (Color){0, 0, 0, 140});
    int bw = 440, bh = 168;
    int bx = (w - bw) / 2, by = (h - bh) / 2;
    DrawRectangleRec((Rectangle){ bx, by, bw, bh }, OL_TB);
    DrawRectangleLinesEx((Rectangle){ bx, by, bw, bh }, 1, OL_EDGE);
    DrawRectangleLinesEx((Rectangle){ bx+1, by+1, bw-2, bh-2 }, 1, OL_SHADOW);
    int ty = by + 22;
    DrawTextEx(font, "pdeide — Processing-style IDE", (Vector2){ bx + 18, (float)ty }, fs, 0.0f, OL_TEXT);
    ty += 28;
    DrawTextEx(font, "Author: Kof, 2026", (Vector2){ bx + 18, (float)ty }, fs, 0.0f, OL_DIM);
    ty += 24;
    DrawTextEx(font, "Community software — provided as-is,", (Vector2){ bx + 18, (float)ty }, fs, 0.0f, OL_FAINT);
    ty += 20;
    DrawTextEx(font, "with no warranty. Happy hacking!", (Vector2){ bx + 18, (float)ty }, fs, 0.0f, OL_FAINT);
    static int okdummy;
    if (ButtonLike("OK", bx + bw - 80, by + bh - 30, 60, &okdummy)) show_about = 0;
  }
}

/* Simple immediate-mode button helper */
static int ButtonLike(const char *label, int x, int y, int colw, int *yout) {
  (void)yout;
  Rectangle r = { x, y, colw, 20 };
  Color c = OL_BTN;
  if (CheckCollisionPointRec(GetMousePosition(), r)) c = OL_BTNH;
  DrawRectangleRec(r, c);
  DrawLine(x, y+20, x+colw, y+20, OL_SHADOW);        /* 1px drop shadow */
  DrawLine(x, y+21, x+colw, y+21, OL_SHADOW);
  float tw = MeasureTextEx(font, label, fs, 0.0f).x;
  DrawTextEx(font, label, (Vector2){ x + (colw-tw)/2, y+3 }, fs, 0.0f, OL_TEXT);
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
  plat_exe_dir(script_dir, sizeof script_dir);

  dialogs_checked = 1;
  native_dialogs = have_native_dialogs();

  srand((unsigned)time(NULL));

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  SetWindowMinSize(480, 360);
  InitWindow(814, 576, "pdeide — Processing sketch editor");
  SetExitKey(KEY_NULL);   /* ESC is used to close the About modal, not to exit */
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
  ed_set_metrics(char_w, line_h);
  ed_on_change = reset_blink;

  /* initial sketch: argv[1] or sketch.pde if present in script dir */
  if (argc > 1 && argv[1][0]) {
    load_file(argv[1]);
  } else {
    char def[PATH_MAX]; snprintf(def, sizeof def, "%s/sketch.pde", script_dir);
    if (plat_file_access(def, 4) == 0) load_file(def);
  }

  /* build dir */
  {
    if (plat_mkdtemp_dir("pdeide.XXXXXX", build_dir, sizeof build_dir) != 0) {
#ifdef _WIN32
      snprintf(build_dir, sizeof build_dir, "./pdeide_%ld", plat_getpid());
      _mkdir(build_dir);
#else
      snprintf(build_dir, sizeof build_dir, "/tmp/pdeide.%ld", plat_getpid());
      mkdir(build_dir, 0700);
#endif
    }
    /* copy terminus into build dir so running sketch resolves it */
    char fs[PATH_MAX]; char fs2[PATH_MAX];
    snprintf(fs, sizeof fs, "%s/terminus.ttf", script_dir);
    snprintf(fs2, sizeof fs2, "%s/terminus.ttf", build_dir);
    if (plat_file_access(fs, 4) == 0) {
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
