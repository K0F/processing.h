/* Pure editor engine (selection / movement / indent) for pdeide.
 * Deliberately free of raylib so it can be unit-tested in isolation. */

#include "editor.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Editor ed;

void (*ed_on_change)(void) = NULL;

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

void ed_init(void) {
  ed.cap = ED_BUF_INIT; ed.len = 0; ed.buf = (char*)calloc(ed.cap, 1);
  ed.cur = 0; ed.sel = SIZE_MAX; ed.view_line = 0; ed.view_col = 0;
  ed.path[0] = 0; ed.dirty = 0; ed.line_count = 0; ed.need_rebuild = 0;
  ed.goal_col = -1;
  ed.err = NULL; ed.err_count = 0; ed.err_cap = 0; ed.first_err = 0;
  ed.char_w = 0; ed.line_h = 0;
}

static void ed_grow(size_t needlen) {
  if (needlen + 1 > ed.cap) {
    while (ed.cap < needlen + 1) ed.cap *= 2;
    ed.buf = (char*)realloc(ed.buf, ed.cap);
  }
}

static void ed_touch(void) {
  ed.dirty = 1; ed.need_rebuild = 1;
  if (ed_on_change) ed_on_change();
}

void ed_set_metrics(float cw, float lh) { ed.char_w = cw; ed.line_h = lh; }

void ed_clear_errors(void) { ed.err_count = 0; ed.first_err = 0; }

void ed_mark_error(int line) {
  for (int i = 0; i < ed.err_count; i++) if (ed.err[i] == line) return;
  if (ed.err_count == ed.err_cap) { ed.err_cap = ed.err_cap ? ed.err_cap*2 : 32; ed.err = (int*)realloc(ed.err, sizeof(int)*ed.err_cap); }
  ed.err[ed.err_count++] = line;
  if (ed.first_err == 0 || line < ed.first_err) ed.first_err = line;
}

/* rebuild line-start index */
static void ed_rebuild_lines(void) {
  if (ed.line_cap == 0) { ed.line_cap = 64; ed.line_off = (size_t*)malloc(sizeof(size_t)*ed.line_cap); }
  ed.line_count = 0; ed.line_off[ed.line_count++] = 0;
  for (size_t i = 0; i < ed.len; i++) {
    if (ed.buf[i] == '\n') {
      if (ed.line_count == ed.line_cap) { ed.line_cap *= 2; ed.line_off = (size_t*)realloc(ed.line_off, sizeof(size_t)*ed.line_cap); }
      ed.line_off[ed.line_count++] = i + 1;
    }
  }
  ed.need_rebuild = 0;
}

static void ed_ensure_lines(void) { if (ed.need_rebuild || ed.line_count == 0) ed_rebuild_lines(); }

/* offset -> line index (0-based) */
int ed_line_of(size_t off) {
  ed_ensure_lines();
  int lo = 0, hi = ed.line_count - 1, ans = 0;
  while (lo <= hi) {
    int mid = (lo+hi)/2;
    if (ed.line_off[mid] <= off) { ans = mid; lo = mid+1; } else hi = mid-1;
  }
  return ans;
}
/* line index (0-based) -> start offset */
size_t ed_line_start(int li) { ed_ensure_lines(); if (li<0) return 0; if (li>=ed.line_count) li=ed.line_count-1; return ed.line_off[li]; }
/* line index -> line length in bytes (excluding trailing \n) */
size_t ed_line_len(int li) {
  ed_ensure_lines();
  size_t s = ed_line_start(li);
  size_t e = (li+1 < ed.line_count) ? ed.line_off[li+1] : ed.len;
  if (e > s && ed.buf[e-1] == '\n') e--;
  return e - s;
}
int ed_lines(void) { ed_ensure_lines(); return ed.line_count; }

int ed_col_of(size_t off) { return (int)(off - ed_line_start(ed_line_of(off))); }

void ed_insert_at(size_t off, const char *text, size_t n) {
  ed_grow(ed.len + n);
  memmove(ed.buf + off + n, ed.buf + off, ed.len - off);
  memcpy(ed.buf + off, text, n);
  ed.len += n;
  ed_touch();
}

void ed_delete_range(size_t a, size_t b) {
  if (b <= a) return;
  memmove(ed.buf + a, ed.buf + b, ed.len - b);
  ed.len -= (b - a);
  ed_touch();
}

/* Replace whole buffer contents with external text (file load). */
void ed_set_text(const char *text) {
  size_t n = strlen(text);
  ed_grow(n);
  memcpy(ed.buf, text, n);
  ed.len = n;
  ed.cur = 0; ed.sel = SIZE_MAX; ed.view_line = 0; ed.view_col = 0;
  ed_touch();
}

/* scan one (trimmed) line: count brace opens/closes outside of string
 * literals and comments, and whether the first non-space char is '}'. */
static void scan_braces(const char *s, size_t len, int *open, int *close, int *starts_close) {
  *open = *close = 0; *starts_close = 0;
  int first_seen = 0;
  size_t i = 0;
  while (i < len) {
    unsigned char c = (unsigned char)s[i];
    if (c == ' ' || c == '\t') { i++; continue; }
    if (!first_seen && c == '}') *starts_close = 1;
    first_seen = 1;
    if (c == '"') { i++; while (i < len) { if (s[i]=='\\') { i+=2; continue; } if (s[i]=='"') { i++; break; } i++; } continue; }
    if (c == '\'') { i++; while (i < len) { if (s[i]=='\\') { i+=2; continue; } if (s[i]=='\'') { i++; break; } i++; } continue; }
    if (c == '/' && i+1 < len && s[i+1] == '/') break;
    if (c == '/' && i+1 < len && s[i+1] == '*') { i += 2; while (i+1 < len && !(s[i]=='*' && s[i+1]=='/')) i++; i = (i+1<len) ? i+2 : len; continue; }
    if (c == '{') (*open)++;
    else if (c == '}') (*close)++;
    i++;
  }
}

/* Re-indent the whole buffer by brace depth (2 spaces per level).
 * String literals, char literals and comments are ignored for brace
 * counting so their contents are not mangled. The cursor is left at the
 * start of whatever line it was on. */
void ed_pretty_format(void) {
  int cur_line = ed_line_of(ed.cur);
  int old_view = ed.view_line;

  size_t cap = ed.len * 2 + 64; if (cap < 256) cap = 256;
  char *out = (char*)malloc(cap);
  size_t olen = 0;
  int depth = 0;
  size_t cur_line_out = 0;

  size_t i = 0;
  int line_idx = 0;
  while (i < ed.len) {
    size_t cs = i, ce = i;
    while (ce < ed.len && ed.buf[ce] != '\n') ce++;
    int has_nl = (ce < ed.len);
    size_t line_next = ce + (has_nl ? 1 : 0);

    while (cs < ce && isspace((unsigned char)ed.buf[cs])) cs++;
    while (ce > cs && isspace((unsigned char)ed.buf[ce-1])) ce--;
    int empty = (cs == ce);

    if (line_idx == cur_line) cur_line_out = olen;

    if (!empty) {
      int op = 0, cl = 0, sc = 0;
      scan_braces(ed.buf + cs, ce - cs, &op, &cl, &sc);
      int pad = depth;
      if (sc) pad = depth - 1;
      if (pad < 0) pad = 0;
      int nsp = pad * 2;
      if (olen + (size_t)nsp + (ce - cs) + 1 > cap) {
        cap *= 2; out = (char*)realloc(out, cap);
      }
      for (int p = 0; p < nsp; p++) out[olen++] = ' ';
      for (size_t x = cs; x < ce; x++) out[olen++] = ed.buf[x];
      depth += op - cl;
      if (depth < 0) depth = 0;
    }
    if (has_nl) {
      if (olen + 1 > cap) { cap *= 2; out = (char*)realloc(out, cap); }
      out[olen++] = '\n';
    }
    i = line_next;
    line_idx++;
  }

  ed_grow(olen);
  memcpy(ed.buf, out, olen);
  ed.len = olen;
  free(out);
  ed.cur = cur_line_out < ed.len ? cur_line_out : ed.len;
  ed.sel = SIZE_MAX; ed.goal_col = -1;
  ed.view_line = old_view < ed_lines() ? old_view : 0;
  ed_touch();
}

/* current line index for cursor */
int ed_cur_line(void) { return ed_line_of(ed.cur); }

/* re-render a selection range from (sel, cur) inclusive ordering */
void ed_sel_range(size_t *a, size_t *b) {
  if (ed.sel == SIZE_MAX) { *a = ed.cur; *b = ed.cur; return; }
  *a = ed.sel < ed.cur ? ed.sel : ed.cur;
  *b = ed.sel < ed.cur ? ed.cur : ed.sel;
}

const char *ed_get_selected(void) {
  size_t a, b; ed_sel_range(&a, &b);
  if (a == b) return NULL;
  char *out = (char*)malloc(b - a + 1);
  memcpy(out, ed.buf + a, b - a); out[b-a] = 0;
  return out;
}

void ed_insert_text(const char *text, size_t n) {
  size_t a, b; ed_sel_range(&a, &b);
  ed_delete_range(a, b);
  ed_insert_at(a, text, n);
  ed.cur = a + n;
  ed.sel = SIZE_MAX;          /* insertion consumes any selection */
  ed.goal_col = -1;
}
void ed_insert_cp(unsigned cp) {
  char u[4]; int n = utf8_encode(cp, u);
  ed_insert_text(u, n);
}
void ed_insert_newline(void) {
  int li = ed_cur_line();
  size_t ls = ed_line_start(li);
  /* indent = leading whitespace of the current line, optionally deepened */
  size_t ws = 0;
  while (ls + ws < ed.cur && (ed.buf[ls+ws]==' ' || ed.buf[ls+ws]=='\t')) ws++;
  /* first non-space char of the line (for dedent on a '}' line) and last
   * non-space char before the cursor (for deepen on a line ending in '{') */
  size_t first = ls + ws;
  char fc = (first < ed.cur && first < ed.len) ? ed.buf[first] : 0;
  char lc = 0;
  size_t p = ed.cur;
  while (p > ls) {
    p = cw_back(ed.buf, p);
    char cc = ed.buf[p];
    if (cc == ' ' || cc == '\t') continue;
    lc = cc; break;
  }
  ed_insert_text("\n", 1);
  if (ws > 0) ed_insert_text(ed.buf + ls, ws);
  /* auto-deepen after an opening brace, and let a closing brace line sit at
   * the parent indent level */
  if (lc == '{') { char t[] = "  "; ed_insert_text(t, 2); }
  else if (fc == '}') {
    /* one level less: strip up to two trailing ws chars of the copied indent */
    size_t q = ed.cur, s = q; int rm = 0;
    while (rm < 2 && s > 0 && (ed.buf[s-1]==' ' || ed.buf[s-1]=='\t')) { s--; rm++; }
    if (rm > 0) { ed_delete_range(s, q); ed.cur = s; }
  }
}
void ed_backspace(void) {
  size_t a, b; ed_sel_range(&a, &b);
  if (a != b) { ed_delete_range(a, b); ed.cur = a; ed.sel = SIZE_MAX; return; }
  if (ed.cur == 0) return;
  size_t prev = cw_back(ed.buf, ed.cur);
  ed_delete_range(prev, ed.cur); ed.cur = prev;
}
void ed_delete(void) {
  size_t a, b; ed_sel_range(&a, &b);
  if (a != b) { ed_delete_range(a, b); ed.cur = a; ed.sel = SIZE_MAX; return; }
  if (ed.cur >= ed.len) return;
  size_t nxt = cw_fwd(ed.buf, ed.len, ed.cur);
  ed_delete_range(ed.cur, nxt);
}

/* Toggle a `// ` line comment on/off for the whole current line. A comment
 * is inserted after the leading whitespace; uncommenting removes the `//`
 * and one following space. The cursor stays on the current line, snapped to
 * the front of any region the toggle removes. */
void ed_toggle_line_comment(void) {
  int li = ed_cur_line();
  size_t ls = ed_line_start(li);
  size_t ll = ed_line_len(li);
  size_t ws = 0;
  while (ws < ll && (ed.buf[ls+ws]==' ' || ed.buf[ls+ws]=='\t')) ws++;
  if (ll - ws >= 2 && ed.buf[ls+ws]=='/' && ed.buf[ls+ws+1]=='/') {
    size_t rm = 2;
    if (ll - ws >= 3 && ed.buf[ls+ws+2]==' ') rm = 3;
    size_t zone = ls + ws + rm;
    if (ed.cur > ls + ws && ed.cur < zone) ed.cur = ls + ws;
    else if (ed.cur >= zone) ed.cur -= rm;
    ed_delete_range(ls + ws, zone);
  } else {
    ed_insert_at(ls + ws, "// ", 3);
    if (ed.cur >= ls + ws) ed.cur += 3;
  }
  ed.sel = SIZE_MAX;
  ed.goal_col = -1;
}

/* movement; shift selects */
static void ed_move(size_t target, bool shift) {
  if (shift && ed.sel == SIZE_MAX) ed.sel = ed.cur;
  ed.cur = target;
  if (!shift) ed.sel = SIZE_MAX;
  if (ed_on_change) ed_on_change();
}
void ed_move_left(bool shift) {
  size_t t = ed.cur == 0 ? 0 : cw_back(ed.buf, ed.cur);
  ed_move(t, shift);
  ed.goal_col = -1;                       /* horizontal move resets vertical goal */
}
void ed_move_right(bool shift) {
  size_t t = ed.cur >= ed.len ? ed.len : cw_fwd(ed.buf, ed.len, ed.cur);
  ed_move(t, shift);
  ed.goal_col = -1;
}
void ed_move_up_down(int dir, bool shift) {
  int li = ed_cur_line();
  int col = ed_col_of(ed.cur);
  if (ed.goal_col < 0) ed.goal_col = col;   /* latch goal on first vertical move */
  int nli = li + dir;
  if (nli < 0) nli = 0; else if (nli >= ed_lines()) nli = ed_lines()-1;
  size_t ls = ed_line_start(nli);
  size_t ll = ed_line_len(nli);
  int tcol = ed.goal_col < (int)ll ? ed.goal_col : (int)ll;
  size_t t = ls + (size_t)tcol;
  ed_move(t, shift);
}
void ed_move_home(bool shift) {
  int li = ed_cur_line();
  size_t ls = ed_line_start(li);
  size_t p = ls; while (p < ed.cur && (ed.buf[p]==' '||ed.buf[p]=='\t')) p++;
  size_t t = (p < ed.cur) ? p : ls;
  ed_move(t, shift);
  ed.goal_col = -1;
}
void ed_move_end(bool shift) {
  int li = ed_cur_line();
  size_t t = ed_line_start(li) + ed_line_len(li);
  ed_move(t, shift);
  ed.goal_col = -1;
}
void ed_move_page(int dir, bool shift) {
  for (int i = 0; i < 12; i++) ed_move_up_down(dir, shift);
}
void ed_select_all(void) {
  ed.sel = 0; ed.cur = ed.len;
  if (ed_on_change) ed_on_change();
}

/* Move the current line (no selection) or the whole selected block of lines
 * up (dir<0) / down (dir>0) by one. Does nothing at the buffer bounds. The
 * cursor (and selection anchor, if any) move with the block. */
void ed_move_line(int dir) {
  int n = ed_lines();
  if (n == 0) return;

  int fi, la;
  if (ed.sel == SIZE_MAX) {
    fi = la = ed_cur_line();
  } else {
    int a = ed_line_of(ed.sel), b = ed_line_of(ed.cur);
    fi = a < b ? a : b;
    la = a < b ? b : a;
  }
  if (dir < 0 && fi == 0) return;     /* already at the top */
  if (dir > 0 && la == n - 1) return; /* already at the bottom */

  size_t bs = ed_line_start(fi);
  size_t be = (la + 1 < n) ? ed_line_start(la + 1) : ed.len;   /* start of line after block */
  size_t blk_len = be - bs;

  size_t nb_s, nb_e;
  if (dir < 0) {
    nb_s = ed_line_start(fi - 1);
    nb_e = bs;                        /* neighbour is the line above */
  } else {
    nb_s = be;
    nb_e = (la + 2 < n) ? ed_line_start(la + 2) : ed.len;       /* neighbour is the line below */
  }
  size_t nb_len = nb_e - nb_s;
  if (blk_len + nb_len == 0) return;

  /* build the two swapped chunks in their new order:
   *  - up:   block then neighbour
   *  - down: neighbour then block */
  char *tmp = (char*)malloc(blk_len + nb_len);
  if (dir < 0) {
    memcpy(tmp, ed.buf + bs, blk_len);
    memcpy(tmp + blk_len, ed.buf + nb_s, nb_len);
  } else {
    memcpy(tmp, ed.buf + nb_s, nb_len);
    memcpy(tmp + nb_len, ed.buf + bs, blk_len);
  }

  size_t join = dir < 0 ? nb_s : bs;
  /* region covered by both chunks is [min(bs,nb_s), max(be,nb_e)) */
  size_t lo = bs < nb_s ? bs : nb_s;
  size_t hi = be > nb_e ? be : nb_e;

  size_t cur_delta = (dir < 0 ? (int)(nb_s - bs) : (int)(nb_e - be));
  bool had_sel = (ed.sel != SIZE_MAX);
  size_t new_cur = (size_t)((long)ed.cur + cur_delta);
  size_t new_sel = had_sel ? (size_t)((long)ed.sel + cur_delta) : ed.sel;

  ed_delete_range(lo, hi);
  ed_insert_at(join, tmp, blk_len + nb_len);
  free(tmp);

  ed.cur = new_cur < ed.len ? new_cur : ed.len;
  ed.sel = had_sel ? (new_sel < ed.len ? new_sel : ed.len) : SIZE_MAX;
  ed.goal_col = -1;
  ed_touch();
}

/* click position (pixel) -> byte offset. Rounds to the nearest character cell
 * so the caret lands exactly on the column the click actually targets. */
size_t ed_click_to_off(float mx, float my, int area_left, int area_top) {
  int lines = ed_lines();
  int li = ed.view_line + (int)((my - area_top) / ed.line_h);
  if (li < 0) li = 0; else if (li >= lines) li = lines-1;
  int col0 = ed.view_col;
  size_t ls = ed_line_start(li);
  size_t ll = ed_line_len(li);
  float x = mx - area_left;
  long target_col = col0 + (long)(x / ed.char_w + 0.5f);   /* nearest cell */
  if (target_col < 0) target_col = 0;
  if (target_col > (long)ll) target_col = ll;
  return ls + (size_t)target_col;
}
