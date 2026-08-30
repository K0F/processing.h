/* Unit tests for the pure editor engine (editor.c). No raylib dependency.
 * NOTE: the engine's buffer is deliberately NOT NUL-terminated, so all
 * comparisons are length-aware (ed.len / memcmp). */

#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, ...) do { \
  checks++; \
  if (!(cond)) { \
    failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
  } \
} while (0)

/* reset editor and load "text" */
static void load(const char *text) {
  ed_init();
  ed_set_text(text);
}

/* length-aware equality of whole buffer */
static int is_text(const char *expected) {
  size_t n = strlen(expected);
  return ed.len == n && memcmp(ed.buf, expected, n) == 0;
}

/* set cursor to byte offset of first occurrence of `target` (used before edits)
 * and clear any selection */
static int seek(const char *target) {
  char *p = strstr(ed.buf, target);
  if (!p) { CHECK(0, "seek target '%s' not found in buffer", target); return 0; }
  ed.cur = (size_t)(p - ed.buf);
  ed.sel = SIZE_MAX;
  return 1;
}

/* ---------------- tests ---------------- */

static void test_sel_range_ordering(void) {
  load("hello world\nfoo\n");
  size_t h = strstr(ed.buf, "hello") - ed.buf;   /* 0 */
  ed.sel = h; ed.cur = h + 3;
  { size_t a, b; ed_sel_range(&a, &b); CHECK(a == h && b == h + 3, "fwd [%zu,%zu) want [%zu,%zu)", a,b,h,h+3); }
  ed.cur = h + 1; ed.sel = h + 4;                /* anchor after cursor */
  { size_t a, b; ed_sel_range(&a, &b); CHECK(a == h + 1 && b == h + 4, "rev [%zu,%zu) want [%zu,%zu)", a,b,h+1,h+4); }
}

static void test_get_selected_both_directions(void) {
  load("abcdefgh");
  size_t p = 0, q = 3;                            /* select [0,6) = "abcdef" */
  ed.cur = q + 3; ed.sel = p;
  const char *s = ed_get_selected();
  CHECK(s && strcmp(s, "abcdef") == 0, "fwd selection text '%s'", s ? s : "(null)");
  free((void*)s);
  ed.cur = p; ed.sel = q + 3;
  s = ed_get_selected();
  CHECK(s && strcmp(s, "abcdef") == 0, "rev selection text '%s'", s ? s : "(null)");
  free((void*)s);
  /* empty selection returns NULL */
  ed.cur = p; ed.sel = p;
  CHECK(ed_get_selected() == NULL, "empty selection -> NULL");
}

static void test_shift_move_anchor(void) {
  load("hello world");
  size_t h = strstr(ed.buf, "hello") - ed.buf;   /* 0 */
  ed.cur = h; ed.sel = SIZE_MAX;
  ed_move_right(true);
  CHECK(ed.sel == h, "shift+right latches anchor: sel=%zu want %zu", ed.sel, h);
  ed_move_right(true);
  { size_t a,b; ed_sel_range(&a,&b); CHECK(a==h && b==h+2, "shift+right range [%zu,%zu) want [%zu,%zu)", a,b,h,h+2); }
  /* dragging back left keeps [min,max) ordered and shrinks to [0,1) */
  ed_move_left(true);
  { size_t a,b; ed_sel_range(&a,&b); CHECK(a==h && b==h+1, "drag-left ordered [%zu,%zu) want [%zu,%zu)", a,b,h,h+1); }
  ed_move_left(true);                            /* back to anchor -> collapse */
  { size_t a,b; ed_sel_range(&a,&b); CHECK(a==b && a==h, "at anchor collapses [%zu,%zu)", a,b); }
  /* releasing shift clears selection */
  ed_move_right(false);
  CHECK(ed.sel == SIZE_MAX, "non-shift move clears selection: sel=%zu", ed.sel);
}

static void test_shift_up_down_selection(void) {
  load("aaaa\nbb\nccc\n");
  /* at col2 of line0, shift+down -> line1 col2 (clamped). Anchor stays at line0 col2. */
  seek("aaaa"); ed.cur += 2;                   /* line0 col2 = offset 2 */
  ed_move_up_down(1, true);
  CHECK(ed.sel != SIZE_MAX, "shift+down latched selection");
  { size_t a,b; ed_sel_range(&a,&b);
    size_t anchor = strstr(ed.buf,"aaaa") - ed.buf + 2;  /* 2 */
    size_t target = strstr(ed.buf,"bb") - ed.buf + 2;    /* line1 col2 */
    CHECK(a == anchor && b == target, "span [%zu,%zu) want [%zu,%zu)", a,b,anchor,target); }
}

static void test_edit_clears_selection(void) {
  load("hello world");
  size_t h = strstr(ed.buf, "hello") - ed.buf;   /* 0 */
  ed.cur = h + 5; ed.sel = h;                    /* select "hello" */
  ed_insert_text("hi", 2);
  CHECK(ed.sel == SIZE_MAX, "insert clears selection: sel=%zu", ed.sel);
  CHECK(is_text("hi world"), "replace 'hello' with 'hi', got '%.*s'", (int)ed.len, ed.buf);

  load("hello world");
  h = strstr(ed.buf, "hello") - ed.buf;
  ed.cur = h + 2; ed.sel = h;                    /* select "he" */
  ed_backspace();
  CHECK(ed.sel == SIZE_MAX, "backspace clears selection: sel=%zu", ed.sel);
  CHECK(is_text("llo world"), "bs removed 'he', got '%.*s'", (int)ed.len, ed.buf);

  load("hello world");
  h = strstr(ed.buf, "hello") - ed.buf;
  ed.cur = h + 1; ed.sel = h;                    /* select "h" */
  ed_delete();
  CHECK(ed.sel == SIZE_MAX, "delete clears selection: sel=%zu", ed.sel);
  CHECK(is_text("ello world"), "del removed 'h', got '%.*s'", (int)ed.len, ed.buf);
}

static void test_select_all(void) {
  load("alpha\nbeta\n");
  ed_select_all();
  { size_t a,b; ed_sel_range(&a,&b); CHECK(a==0 && b==ed.len, "select-all [%zu,%zu) == [0,%zu)", a,b,ed.len); }
  const char *s = ed_get_selected();
  CHECK(s && strcmp(s, "alpha\nbeta\n") == 0, "select-all text '%s'", s ? s : "(null)");
  free((void*)s);
  ed_insert_text("X", 1);                        /* typing replaces everything */
  CHECK(is_text("X"), "typing over select-all -> 'X', got '%.*s'", (int)ed.len, ed.buf);
}

static void test_newline_indent(void) {
  load("void draw() {");
  seek("{"); ed.cur += 1;                        /* just after '{' (end) */
  ed_insert_newline();
  CHECK(is_text("void draw() {\n  "), "after '{' indent 2, got '%.*s'", (int)ed.len, ed.buf);

  /* Enter mid-line on an indented line splits and copies the indent */
  load("  foobar");
  seek("foo"); ed.cur += 3;                      /* end of "foo" in "  foobar" (col5) */
  ed_insert_newline();
  CHECK(is_text("  foo\n  bar"), "mid-line split preserves indent, got '%.*s'", (int)ed.len, ed.buf);

  /* Enter after a closing brace at line start dedents one level */
  load("    }");
  seek("}"); ed.cur += 1;                        /* end of "    }" */
  ed_insert_newline();
  CHECK(is_text("    }\n  "), "dedent after '}' -> 2 spaces, got '%.*s'", (int)ed.len, ed.buf);
}

static void test_goal_column(void) {
  load("aaaa\nbb\nccccccc\n");
  seek("aaaa"); ed.cur += 3;                     /* col 3 */
  ed_move_up_down(1, false);                     /* line1 len2 -> clamps to col2 */
  CHECK(ed_col_of(ed.cur) == 2 && ed_line_of(ed.cur) == 1, "down clamp, got line=%d col=%d", ed_line_of(ed.cur), ed_col_of(ed.cur));
  ed_move_up_down(1, false);                     /* latched goal 3 -> line2 col3 */
  CHECK(ed_col_of(ed.cur) == 3 && ed_line_of(ed.cur) == 2, "goal latched col3, got line=%d col=%d", ed_line_of(ed.cur), ed_col_of(ed.cur));
  /* horizontal move resets goal: up now latches current col2 */
  ed_move_left(false);
  ed_move_up_down(-1, false);
  CHECK(ed_line_of(ed.cur) == 1 && ed_col_of(ed.cur) == 2, "goal reset; up clamps to line1 len2, got line=%d col=%d", ed_line_of(ed.cur), ed_col_of(ed.cur));
}

static void test_click_to_off(void) {
  load("ab cd\nefg\n");
  ed_set_metrics(8.0f, 16.0f);                   /* set AFTER load (init resets) */
  ed.view_line = 0; ed.view_col = 0;
  /* click left half of cell 1 on line0 -> col1 */
  size_t off = ed_click_to_off(9.0f, 16.0f * 0.5f, 0, 0);
  CHECK(ed_line_of(off) == 0 && ed_col_of(off) == 1, "click col1 line0 -> off=%zu (line %d col %d)", off, ed_line_of(off), ed_col_of(off));
  /* far past end clamps to line length (5) */
  off = ed_click_to_off(1000.0f, 16.0f * 0.5f, 0, 0);
  CHECK(ed_line_of(off) == 0 && ed_col_of(off) == (int)ed_line_len(0), "far-right clamp, off=%zu col=%d", off, ed_col_of(off));
  /* y=24px / 16 = row1 -> line1 */
  off = ed_click_to_off(8.0f, 24.0f, 0, 0);
  CHECK(ed_line_of(off) == 1, "click row1 -> line1, got line=%d", ed_line_of(off));
  /* respect view_col offset: scroll right 3 cols, click left half of cell0 -> col3 */
  ed.view_col = 3;
  off = ed_click_to_off(2.0f, 8.0f, 0, 0);       /* cell 0 + view_col 3 = col3 */
  CHECK(ed_line_of(off) == 0 && ed_col_of(off) == 3, "view_col offset, got col=%d", ed_col_of(off));
}

static void test_move_line(void) {
  /* move a single line down swaps it with the line below */
  load("aaa\nbbb\nccc\n");
  seek("aaa");                                   /* cursor on line 0 */
  ed_move_line(1);
  CHECK(is_text("bbb\naaa\nccc\n"), "line down -> 'bbb\\naaa\\nccc\\n', got '%.*s'", (int)ed.len, ed.buf);
  CHECK(ed_line_of(ed.cur) == 1, "cursor follows line down, line=%d", ed_line_of(ed.cur));

  /* move it back up */
  ed_move_line(-1);
  CHECK(is_text("aaa\nbbb\nccc\n"), "line up back -> 'aaa\\nbbb\\nccc\\n', got '%.*s'", (int)ed.len, ed.buf);
  CHECK(ed_line_of(ed.cur) == 0, "cursor follows line up, line=%d", ed_line_of(ed.cur));

  /* boundary: no move at the top line going up */
  ed_move_line(-1);
  CHECK(is_text("aaa\nbbb\nccc\n"), "top-line up is a no-op, got '%.*s'", (int)ed.len, ed.buf);

  /* boundary: move last line down is a no-op */
  seek("ccc");
  ed_move_line(1);
  CHECK(is_text("aaa\nbbb\nccc\n"), "bottom-line down is a no-op, got '%.*s'", (int)ed.len, ed.buf);

  /* move a selected multi-line block up as a unit */
  load("a\nb\nc\nd\n");
  seek("b"); ed.cur += 1; ed.sel = (size_t)(strstr(ed.buf, "c") - ed.buf) + 1;   /* sel line1..line2 */
  ed_move_line(-1);
  CHECK(is_text("b\nc\na\nd\n"), "block up -> 'b\\nc\\na\\nd\\n', got '%.*s'", (int)ed.len, ed.buf);
  CHECK(ed_line_of(ed.cur) == 0 && ed_line_of(ed.sel) == 1, "block up keeps sel lines 0..1, got cur=%d sel=%d", ed_line_of(ed.cur), ed_line_of(ed.sel));
}

static void test_toggle_line_comment(void) {
  /* comment a line that has leading whitespace; then uncomment it again */
  load("  aa\nbb\n");
  ed.cur = 4;                                   /* just past "aa" */
  ed_toggle_line_comment();
  CHECK(is_text("  // aa\nbb\n"), "comment -> '  // aa', got '%.*s'", (int)ed.len, ed.buf);
  CHECK(ed.cur == 7, "comment shifts cursor +3: cur=%zu", ed.cur);
  ed_toggle_line_comment();
  CHECK(is_text("  aa\nbb\n"), "uncomment -> '  aa', got '%.*s'", (int)ed.len, ed.buf);
  CHECK(ed.cur == 4, "uncomment shifts cursor -3: cur=%zu", ed.cur);

  /* uncommenting an already-commented line also strips one following space */
  load("// hi there\nbb\n");
  seek("hi");
  ed_toggle_line_comment();
  CHECK(is_text("hi there\nbb\n"), "uncomment existing -> 'hi there', got '%.*s'", (int)ed.len, ed.buf);

  /* an empty line becomes a comment */
  load("x\n\ny\n");
  ed.cur = 2;                                    /* middle, empty line */
  ed_toggle_line_comment();
  CHECK(is_text("x\n// \ny\n"), "comment empty line -> '// ', got '%.*s'", (int)ed.len, ed.buf);
}

int main(void) {
  test_sel_range_ordering();
  test_get_selected_both_directions();
  test_shift_move_anchor();
  test_shift_up_down_selection();
  test_edit_clears_selection();
  test_select_all();
  test_newline_indent();
  test_goal_column();
  test_click_to_off();
  test_move_line();
  test_toggle_line_comment();

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
