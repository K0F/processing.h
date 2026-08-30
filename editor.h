/* Pure editor engine for pdeide — no raylib / no windowing dependency,
 * so the selection / movement / indent logic can be unit-tested standalone.
 * The host app sets ed.metrics and optionally ed_on_change. */

#ifndef PDEIDE_EDITOR_H
#define PDEIDE_EDITOR_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define ED_BUF_INIT 65536

typedef struct Editor {
  char  *buf; size_t cap, len;
  size_t cur;            /* cursor byte offset */
  size_t sel;            /* selection anchor, or SIZE_MAX for none */
  int    goal_col;       /* desired column kept across up/down movement */
  int    view_line;      /* first visible source line */
  int    view_col;       /* leftmost visible column (bytes) */
  char   path[256];      /* loaded/saved file path ("" = unnamed) */
  int    dirty;
  /* cached line index */
  size_t *line_off; int line_count, line_cap;
  int    need_rebuild;
  /* error markers: line numbers (1-based) to highlight */
  int *err; int err_count, err_cap; int first_err;
  /* rendering metrics set by the host (integer grid) */
  float char_w, line_h;
} Editor;

extern Editor ed;

/* set by host to reset the caret blink phase on any edit/move */
extern void (*ed_on_change)(void);

void ed_init(void);
void ed_set_text(const char *text);
void ed_set_metrics(float char_w, float line_h);

void ed_clear_errors(void);
void ed_mark_error(int line);

int  ed_lines(void);
int  ed_line_of(size_t off);
size_t ed_line_start(int li);
size_t ed_line_len(int li);
int  ed_col_of(size_t off);
int  ed_cur_line(void);

void ed_sel_range(size_t *a, size_t *b);
const char *ed_get_selected(void);

void ed_insert_text(const char *text, size_t n);
void ed_insert_cp(unsigned cp);
void ed_insert_newline(void);
void ed_backspace(void);
void ed_delete(void);
void ed_delete_range(size_t a, size_t b);
void ed_toggle_line_comment(void);
void ed_select_all(void);
void ed_pretty_format(void);

void ed_move_left(bool shift);
void ed_move_right(bool shift);
void ed_move_up_down(int dir, bool shift);
void ed_move_home(bool shift);
void ed_move_end(bool shift);
void ed_move_page(int dir, bool shift);
void ed_move_line(int dir);

size_t ed_click_to_off(float mx, float my, int area_left, int area_top);

#endif
