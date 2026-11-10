#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "pde2c.h"

#define MAX_LINE_LENGTH 1024

// file registry: tokens reference source files by id so generated C
// can carry #line directives pointing back at the original .pde tabs
static char *_fileNames[MAX_FILES];
static int _numFiles = 0;

int pdeRegisterFile(const char *name) {
  if (_numFiles >= MAX_FILES) return _numFiles - 1;
  _fileNames[_numFiles] = strdup(name);
  return _numFiles++;
}

const char *pdeFileName(int id) {
  if (id < 0 || id >= _numFiles) return "sketch.pde";
  return _fileNames[id];
}

// Tokenizer /////////////////////////
bool is_keyword(const char *str) {
  const char *keywords[] = {
    "void", "int", "float", "boolean", "color", "char", "double",
    "byte", "if", "else", "for", "while", "return", "true", "false", "setup", "draw"
  };
  int num_keywords = sizeof(keywords) / sizeof(keywords[0]);
  for (int i = 0; i < num_keywords; i++) {
    if (strcmp(str, keywords[i]) == 0) return true;
  }
  return false;
}

// Helper to identify data types for array conversions
// Users can call sketch functions before their definitions, so C needs
// prototypes. Later passes (name collision, forward decl) reuse this.
static bool is_class_name(const char *name);
static const char *symbol_type(const char *name);

bool is_type_token(Token t) {
  if (t.type == TOKEN_KEYWORD &&
      (strcmp(t.text, "int") == 0 || strcmp(t.text, "float") == 0 ||
       strcmp(t.text, "char") == 0 || strcmp(t.text, "double") == 0 ||
       strcmp(t.text, "byte") == 0 || strcmp(t.text, "long") == 0 ||
       strcmp(t.text, "short") == 0 ||
       strcmp(t.text, "boolean") == 0 || strcmp(t.text, "color") == 0))
    return true;
  if (t.type == TOKEN_IDENTIFIER) {
    if (strcmp(t.text, "PImage") == 0 || strcmp(t.text, "PVector") == 0 ||
        strcmp(t.text, "String") == 0 || strcmp(t.text, "PGraphics") == 0 ||
        strcmp(t.text, "PFont") == 0 || strcmp(t.text, "Object") == 0 ||
        strcmp(t.text, "ArrayList") == 0 ||
        strcmp(t.text, "AudioStream") == 0 || strcmp(t.text, "SineWave") == 0 ||
        strcmp(t.text, "FadeOut") == 0 || strcmp(t.text, "FadeIn") == 0)
      return true;
    if (is_class_name(t.text)) return true;
  }
  return false;
}

// Types that can start a user-defined function signature
bool is_function_return_type(Token t) {
  if (is_type_token(t)) return true;
  if (t.type != TOKEN_IDENTIFIER && t.type != TOKEN_KEYWORD) return false;
  if (strcmp(t.text, "void") == 0) return true;
  if (strcmp(t.text, "PVector") == 0) return true;
  if (strcmp(t.text, "String") == 0) return true;
  if (strcmp(t.text, "boolean") == 0) return true;
  if (strcmp(t.text, "color") == 0) return true;
  return false;
}

// Detect a Java trailing-array return DEFINITION: "TYPE name(params)[] {"
bool is_trailing_array_def(Token *tokens, int num_tokens, int i) {
  if (!is_function_return_type(tokens[i])) return false;
  if (!(i + 4 < num_tokens)) return false;
  if (tokens[i+1].type != TOKEN_IDENTIFIER) return false;
  if (!(tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "(") == 0)) return false;
  int depth = 0, close = -1;
  for (int q = i + 2; q < num_tokens; q++) {
    if (tokens[q].type == TOKEN_SYMBOL && strcmp(tokens[q].text, "(") == 0) depth++;
    else if (tokens[q].type == TOKEN_SYMBOL && strcmp(tokens[q].text, ")") == 0) {
      depth--;
      if (depth == 0) { close = q; break; }
    }
  }
  if (close < 0 || close + 3 >= num_tokens) return false;
  return tokens[close+1].type == TOKEN_SYMBOL && strcmp(tokens[close+1].text, "[") == 0 &&
         tokens[close+2].type == TOKEN_SYMBOL && strcmp(tokens[close+2].text, "]") == 0 &&
         tokens[close+3].type == TOKEN_SYMBOL && strcmp(tokens[close+3].text, "{") == 0;
}

// Processing event callbacks are renamed so they can coexist with the
// same-named state variables in processing.h (e.g. bool keyPressed).
const char *event_callback_suffix(const char *name) {
  static const char *names[] = { "keyPressed", "keyReleased",
                                 "mousePressed", "mouseReleased",
                                 "mouseMoved", "mouseDragged", "mouseWheel" };
  for (int i = 0; i < 7; i++) {
    if (strcmp(name, names[i]) == 0) return "_event";
  }
  return NULL;
}

// Map Java-ish param/return types to C inside prototypes and signatures
void emit_c_type(const char *javaType) {
  if (strcmp(javaType, "boolean") == 0)      printf("bool");
  else if (strcmp(javaType, "color") == 0)   printf("uint32_t");
  else if (strcmp(javaType, "byte") == 0)    printf("uint8_t");
  else if (strcmp(javaType, "String") == 0)  printf("const char *");
  else                                       printf("%s", javaType);
}

// same mapping as emit_c_type but into a caller buffer
const char *emit_c_type_str(const char *javaType) {
  static const char *boolean_t = "bool";
  static const char *color_t = "uint32_t";
  static const char *string_t = "const char *";
  static const char *byte_t = "uint8_t";
  if (strcmp(javaType, "boolean") == 0)     return boolean_t;
  if (strcmp(javaType, "color") == 0)       return color_t;
  if (strcmp(javaType, "byte") == 0)        return byte_t;
  if (strcmp(javaType, "String") == 0)      return string_t;
  return javaType;
}

static bool is_arith_op_text(const char *s) {
  static const char *ops[] = { "+", "-", "*", "/", "%", "=", "==", "!=", "<", ">",
                               "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=", "&", "|", "^" };
  for (int i = 0; i < 21; i++) {
    if (strcmp(s, ops[i]) == 0) return true;
  }
  return false;
}

// Rewrites Java-style modulo (works on floats) into pmod(a, b) helper calls.
// Returns a newly allocated token array; caller frees both.
int rewrite_modulo(Token *tokens, int num_tokens, Token **out_tokens) {
  Token *out = malloc(sizeof(Token) * (num_tokens * 4 + 16));
  int o = 0;
  for (int i = 0; i < num_tokens; i++) {
    if (!(tokens[i].type == TOKEN_OPERATOR && strcmp(tokens[i].text, "%") == 0)) {
      out[o++] = tokens[i];
      continue;
    }
    // ---- find left operand extent in output tail ----
    int depth = 0, leftStart = o - 1;
    while (leftStart >= 0) {
      Token t = out[leftStart];
      if (t.type == TOKEN_SYMBOL) {
        const char *s = t.text;
        if (strcmp(s, ")") == 0 || strcmp(s, "]") == 0) depth++;
        else if (strcmp(s, "(") == 0 || strcmp(s, "[") == 0) {
          if (depth == 0) break;
          depth--;
        }
        else if (depth == 0 && (is_arith_op_text(s) || strcmp(s, ",") == 0 || strcmp(s, ";") == 0)) break;
      } else if (t.type == TOKEN_OPERATOR) {
        if (depth == 0) break;
      }
      else if (t.type == TOKEN_EOF ||
               ((t.type == TOKEN_KEYWORD || t.type == TOKEN_STRING) && depth == 0)) break;
      /* IDENTIFIER / NUMBER / casts like (int) inside parens: keep scanning */
      leftStart--;
    }
    leftStart++; // inclusive start of left operand in out[]
    int leftLen = o - leftStart;

    // ---- find right operand extent in input ----
    int j = i + 1, rdepth = 0;
    int rightEnd = j; // exclusive
    while (j < num_tokens) {
      Token t = tokens[j];
      if (t.type == TOKEN_SYMBOL) {
        const char *s = t.text;
        if (strcmp(s, "(") == 0 || strcmp(s, "[") == 0) rdepth++;
        else if (strcmp(s, ")") == 0 || strcmp(s, "]") == 0) {
          if (rdepth == 0) break;
          rdepth--;
        }
        else if (rdepth == 0 && (is_arith_op_text(s) || strcmp(s, ",") == 0 || strcmp(s, ";") == 0)) break;
      } else if (t.type == TOKEN_EOF) break;
      j++;
    }
    rightEnd = j;

    // ---- emit pmod(left, right) ----
    Token *leftSeg = malloc(sizeof(Token) * (leftLen > 0 ? leftLen : 1));
    memcpy(leftSeg, &out[leftStart], sizeof(Token) * leftLen);
    o = leftStart; // drop the raw left operand from output
    Token pm = tokens[i];
    strcpy(pm.text, "pmod");
    pm.type = TOKEN_IDENTIFIER;
    out[o++] = pm;
    Token open = tokens[i]; strcpy(open.text, "("); open.type = TOKEN_SYMBOL;
    out[o++] = open;
    memcpy(&out[o], leftSeg, sizeof(Token) * leftLen);
    o += leftLen;
    free(leftSeg);
    Token comma = tokens[i]; strcpy(comma.text, ","); comma.type = TOKEN_SYMBOL;
    out[o++] = comma;
    // recurse into right operand (may contain further '%')
    Token *rightOut;
    int rightLen = rewrite_modulo(&tokens[i + 1], rightEnd - (i + 1), &rightOut);
    memcpy(&out[o], rightOut, sizeof(Token) * rightLen);
    o += rightLen;
    free(rightOut);
    Token close = tokens[i]; strcpy(close.text, ")"); close.type = TOKEN_SYMBOL;
    out[o++] = close;

    i = rightEnd - 1; // continue after right operand
  }
  *out_tokens = out;
  return o;
}

// Pre-pass: v.charAt(i) -> _pde_charat(v, i) so downstream passes see a
// plain function call instead of Java-style method dispatch.
int rewrite_charat(Token *tokens, int num_tokens, Token **out_tokens) {
  Token *out = malloc(sizeof(Token) * (num_tokens * 2 + 16));
  int o = 0;
  for (int i = 0; i < num_tokens; i++) {
    if (!(tokens[i].type == TOKEN_IDENTIFIER &&
          i + 3 < num_tokens &&
          tokens[i+1].type == TOKEN_DOT &&
          tokens[i+2].type == TOKEN_IDENTIFIER &&
          strcmp(tokens[i+2].text, "charAt") == 0 &&
          tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "(") == 0)) {
      out[o++] = tokens[i];
      continue;
    }
    Token fn = tokens[i];
    snprintf(fn.text, MAX_TOKEN_TEXT, "_pde_charat");
    fn.type = TOKEN_IDENTIFIER;
    out[o++] = fn;
    Token open = tokens[i]; strcpy(open.text, "("); open.type = TOKEN_SYMBOL;
    out[o++] = open;
    out[o++] = tokens[i]; // receiver
    Token comma = tokens[i]; strcpy(comma.text, ","); comma.type = TOKEN_SYMBOL;
    out[o++] = comma;
    i += 3; // consumed: receiver . charAt (
  }
  *out_tokens = out;
  return o;
}

// String instance method dispatch. The TODO Phase-10 String refactor routes
// receiver.method(args) -> _pde_str_METHOD(receiver, args) for the common
// String methods (indexOf, substring, toLowerCase/UpperCase, replace, equals,
// trim, length). Must run AFTER rewrite_classes() so the type symbol table is
// populated; rewrites are gated on the receiver actually being a String.
static bool is_str_method_name(const char *name) {
  static const char *m[] = { "indexOf","substring","toLowerCase","toUpperCase",
                             "replace","equals","trim","length", NULL };
  for (int i = 0; m[i]; i++) if (strcmp(name, m[i]) == 0) return true;
  return false;
}

// Is a token a String literal (as opposed to a Java char literal, which also
// tokenizes as TOKEN_STRING but begins with a "'")?
static bool tok_is_str_literal(const Token *t) {
  return t->type == TOKEN_STRING && t->text[0] != '\'';
}

static bool is_pde_builtin_non_string(const char *name) {
  static const char *builtins[] = {
    "mouseButton", "mouseX", "mouseY", "pmouseX", "pmouseY", "mouseWheelDelta",
    "width", "height", "displayWidth", "displayHeight", "frameCount", "frameRate",
    "key", "keyCode", "keyPressed", "mousePressed",
    "LEFT", "RIGHT", "CENTER", "UP", "DOWN",
    "PI", "HALF_PI", "TWO_PI", "QUARTER_PI", "DEG_TO_RAD", "RAD_TO_DEG",
    "RGB", "HSB", "ARGB", "ALPHA", "OPENGL", "P2D", "P3D", "JAVA2D", "PDF",
    "CORNER", "CORNERS", "RADIUS", "CLOSE", "POINTS", "LINES", "TRIANGLES", "QUADS",
    NULL
  };
  for (int i = 0; builtins[i]; i++) {
    if (strcmp(name, builtins[i]) == 0) return true;
  }
  return false;
}

// Is a symbol's recorded type a non-string type (primitive or user class)?
// Used to avoid over-rewriting method calls on int/class receivers.
static bool symbol_type_non_string(const char *name) {
  if (is_pde_builtin_non_string(name)) return true;
  const char *ct = symbol_type(name);
  if (!ct) return false;                  // unknown: be lenient (String-like)
  if (strcmp(ct, "const char *") == 0 || strcmp(ct, "char *") == 0) return false;
  return true;                            // known int/float/bool/class/etc.
}

// Does the receiver subexpression that ENDS at token index `i` (the token just
// before a DOT) evaluate to a String? Handles: a string literal, an identifier
// that is String-typed OR not known to be a non-string, and string-producing
// calls (_pde_cat/_pde_charat/_pde_numf/_pde_str_*/str/trim).
static bool receiver_is_string(Token *t, int i) {
  if (tok_is_str_literal(&t[i])) return true;
  int j = i;
  if (t[j].type == TOKEN_SYMBOL && strcmp(t[j].text, ")") == 0) {
    int depth = 0;
    for (j = i; j >= 0; j--) {
      if (strcmp(t[j].text, ")") == 0) { depth++; continue; }
      if (strcmp(t[j].text, "(") == 0) { if (--depth <= 0) { j--; break; } continue; }
      const char *s = t[j].text;
      if ((t[j].type == TOKEN_SYMBOL &&
           (strcmp(s, ";") == 0 || strcmp(s, ",") == 0 || strcmp(s, "{") == 0)) ||
          (t[j].type == TOKEN_OPERATOR && strcmp(s, "=") == 0))
        break;
    }
    if (j < 0) return false;
  }
  if (t[j].type == TOKEN_STRING) return t[j].text[0] != '\'';
  if (t[j].type != TOKEN_IDENTIFIER) return false;
  if (strcmp(t[j].text, "_pde_cat") == 0 || strcmp(t[j].text, "_pde_charat") == 0 ||
      strcmp(t[j].text, "_pde_numf") == 0 || strcmp(t[j].text, "str") == 0 ||
      strcmp(t[j].text, "trim") == 0 || strncmp(t[j].text, "_pde_str_", 9) == 0 ||
      strcmp(t[j].text, "nf") == 0 || strcmp(t[j].text, "nfc") == 0 ||
      strcmp(t[j].text, "nfp") == 0 || strcmp(t[j].text, "nfs") == 0 ||
      strcmp(t[j].text, "hex") == 0 || strcmp(t[j].text, "binary") == 0)
    return true;
  // String-typed, or unknown (lenient: nothing rules out a String).
  return !symbol_type_non_string(t[j].text);
}

// Leftmost token index of the receiver subexpression ending at `i` (before a
// DOT), so the whole receiver can be copied verbatim.
static int receiver_start(Token *t, int i) {
  int depth = 0;
  int j = i;
  while (j >= 0) {
    const char *s = t[j].text;
    if (t[j].type == TOKEN_SYMBOL) {
      if (strcmp(s, ")") == 0 || strcmp(s, "]") == 0) depth++;
      else if (strcmp(s, "(") == 0 || strcmp(s, "[") == 0) {
        if (depth == 0) break;
        depth--;
      }
      else if (depth == 0 && (strcmp(s, ",") == 0 || strcmp(s, ";") == 0 ||
               strcmp(s, "{") == 0 || strcmp(s, "}") == 0)) break;
    } else if (t[j].type == TOKEN_OPERATOR && depth == 0)
      break;
    j--;
  }
  return j + 1;
}

// Count top-level comma-separated arguments in tokens [a+1 .. b) (the tokens
// between the method's '(' and its matching ')', i.e. excluding the parens).
static int count_args(Token *t, int open, int close) {
  int depth = 0, n = 0;
  bool started = false;
  for (int q = open + 1; q < close; q++) {
    if (!started && t[q].type != TOKEN_SYMBOL) started = true;
    if (t[q].type == TOKEN_SYMBOL) {
      if (strcmp(t[q].text, "(") == 0 || strcmp(t[q].text, "[") == 0) depth++;
      else if (strcmp(t[q].text, ")") == 0 || strcmp(t[q].text, "]") == 0) depth--;
      else if (depth == 0 && strcmp(t[q].text, ",") == 0) n++;
    }
  }
  return started ? n + 1 : 0;
}

// Rewrite receiver.method(args) -> _pde_str_method(receiver, args) for String
// receivers. Arity-specific names for substring/indexOf get a numeric suffix.
int rewrite_str_methods(Token *tokens, int num_tokens, Token **out_tokens) {
  Token *out = malloc(sizeof(Token) * (num_tokens * 5 + 64));
  int o = 0;
  for (int i = 0; i < num_tokens; i++) {
    if (!(i + 3 < num_tokens &&
          tokens[i+1].type == TOKEN_DOT &&
          tokens[i+2].type == TOKEN_IDENTIFIER &&
          is_str_method_name(tokens[i+2].text) &&
          tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "(") == 0 &&
          receiver_is_string(tokens, i))) {
      out[o++] = tokens[i];
      continue;
    }
    int recvStart = receiver_start(tokens, i);
    int open = i + 3, close = open, depth = 0;
    for (int q = open; q < num_tokens; q++) {
      if (strcmp(tokens[q].text, "(") == 0) depth++;
      else if (strcmp(tokens[q].text, ")") == 0) {
        if (--depth == 0) { close = q; break; }
      }
    }
    int nargs = count_args(tokens, open, close);
    Token anchor = tokens[i];
    char fnname[MAX_TOKEN_TEXT];
    const char *meth = tokens[i+2].text;
    if (strcmp(meth, "substring") == 0)
      snprintf(fnname, MAX_TOKEN_TEXT, "_pde_str_substring%s", nargs > 1 ? "2" : "");
    else if (strcmp(meth, "indexOf") == 0)
      snprintf(fnname, MAX_TOKEN_TEXT, "_pde_str_indexOf%s", nargs > 1 ? "_from" : "");
    else if (strcmp(meth, "length") == 0)
      snprintf(fnname, MAX_TOKEN_TEXT, "_pde_str_len");
    else
      snprintf(fnname, MAX_TOKEN_TEXT, "_pde_str_%s", meth);

    #define PDE_EMIT_A(txt, typ) do { Token mk = anchor; \
        snprintf(mk.text, MAX_TOKEN_TEXT, "%s", txt); mk.type = typ; out[o++] = mk; } while (0)

    PDE_EMIT_A(fnname, TOKEN_IDENTIFIER);
    PDE_EMIT_A("(", TOKEN_SYMBOL);
    for (int q = recvStart; q <= i; q++) out[o++] = tokens[q]; // receiver tokens
    if (nargs > 0) PDE_EMIT_A(",", TOKEN_SYMBOL);
    for (int q = open + 1; q < close; q++) {
      Token arg = tokens[q];
      if (arg.type == TOKEN_STRING && arg.text[0] == '\'') {
        // convert a Java char-literal argument to a one-char string literal
        int len = (int)strlen(arg.text);
        if (len >= 2) {
          char tmp[MAX_TOKEN_TEXT];
          int w = 0;
          tmp[w++] = '"';
          for (int c = 1; c < len - 1 && w < MAX_TOKEN_TEXT - 2; c++) tmp[w++] = arg.text[c];
          tmp[w++] = '"';
          tmp[w] = '\0';
          snprintf(arg.text, MAX_TOKEN_TEXT, "%s", tmp);
        }
      }
      out[o++] = arg;
    }
    out[o++] = tokens[close]; // ')'
    #undef PDE_EMIT_A
    i = close;
  }
  *out_tokens = out;
  return o;
}

// Extent of a statement RHS starting at `start`: to end of statement, stopping
// at a top-level ','/';'/'}' or unbalanced closer. (Used for `+=` rewriting.)
static int rhs_end(Token *t, int n, int start) {
  int depth = 0, j = start;
  while (j < n) {
    Token tk = t[j];
    if (tk.type == TOKEN_SYMBOL) {
      const char *s = tk.text;
      if (strcmp(s, "(") == 0 || strcmp(s, "[") == 0) depth++;
      else if (strcmp(s, ")") == 0 || strcmp(s, "]") == 0) {
        if (depth == 0) break;
        depth--;
      }
      else if (depth == 0 && (strcmp(s, ",") == 0 || strcmp(s, ";") == 0 || strcmp(s, "}") == 0)) break;
    } else if (tk.type == TOKEN_EOF) break;
    j++;
  }
  return j;
}

// Does the token range [start, end) evaluate to a String? Returns true for a
// (non-char) string literal, a known String-typed symbol, or a string-producing
// call; an identifier with no recorded symbol type is also treated as a String
// (the top-of-file first global is not in the symbol table), while known
// primitives/classes are rejected.
static bool range_is_string(Token *t, int start, int end) {
  if (end <= start) return false;
  if (end - start >= 2) {
    if (t[start].type == TOKEN_IDENTIFIER) {
      const char *nm = t[start].text;
      if (strcmp(nm, "_pde_cat") == 0 || strcmp(nm, "_pde_charat") == 0 ||
          strcmp(nm, "_pde_numf") == 0 || strcmp(nm, "str") == 0 ||
          strcmp(nm, "trim") == 0 || strncmp(nm, "_pde_str_", 9) == 0 ||
          strcmp(nm, "nf") == 0 || strcmp(nm, "nfc") == 0 ||
          strcmp(nm, "nfp") == 0 || strcmp(nm, "nfs") == 0 ||
          strcmp(nm, "hex") == 0 || strcmp(nm, "binary") == 0) {
        if (strcmp(t[start+1].text, "(") == 0) return true;
      }
    }
    return false;
  }
  if (t[start].type == TOKEN_STRING) return t[start].text[0] != '\'';
  if (t[start].type != TOKEN_IDENTIFIER) return false;
  if (strncmp(t[start].text, "_pde_str_", 9) == 0 ||
      strcmp(t[start].text, "str") == 0 || strcmp(t[start].text, "trim") == 0)
    return true;
  if (symbol_type_non_string(t[start].text)) return false; // primitive/class
  return true; // String-typed or unknown
}

int rewrite_array_length(Token *tokens, int num_tokens, Token **out_tokens) {
  Token *out = malloc(sizeof(Token) * (num_tokens * 2 + 64));
  int o = 0;
  for (int i = 0; i < num_tokens; i++) {
    // Match: [receiver tokens] . length [NOT followed by '(']
    if (i + 1 < num_tokens &&
        tokens[i].type == TOKEN_DOT &&
        tokens[i+1].type == TOKEN_IDENTIFIER &&
        strcmp(tokens[i+1].text, "length") == 0 &&
        !(i + 2 < num_tokens && tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "(") == 0)) {
      Token anchor = tokens[i];
      #define PDE_EMIT_AL(txt, typ) do { Token mk = anchor; \
          snprintf(mk.text, MAX_TOKEN_TEXT, "%s", txt); mk.type = typ; out[o++] = mk; } while (0)

      if (i > 0 && tokens[i-1].type == TOKEN_IDENTIFIER && strcmp(tokens[i-1].text, "pixels") == 0) {
        if (i >= 2 && tokens[i-2].type == TOKEN_DOT) {
          int img_ls = receiver_start(tokens, i - 3);
          o -= (i - img_ls);
          PDE_EMIT_AL("(", TOKEN_SYMBOL);
          PDE_EMIT_AL("(", TOKEN_SYMBOL);
          for (int q = img_ls; q < i - 2; q++) out[o++] = tokens[q];
          PDE_EMIT_AL(")", TOKEN_SYMBOL);
          PDE_EMIT_AL(".", TOKEN_DOT);
          PDE_EMIT_AL("width", TOKEN_IDENTIFIER);
          PDE_EMIT_AL("*", TOKEN_SYMBOL);
          PDE_EMIT_AL("(", TOKEN_SYMBOL);
          for (int q = img_ls; q < i - 2; q++) out[o++] = tokens[q];
          PDE_EMIT_AL(")", TOKEN_SYMBOL);
          PDE_EMIT_AL(".", TOKEN_DOT);
          PDE_EMIT_AL("height", TOKEN_IDENTIFIER);
          PDE_EMIT_AL(")", TOKEN_SYMBOL);
          i += 1;
          continue;
        } else {
          o -= 1;
          PDE_EMIT_AL("(", TOKEN_SYMBOL);
          PDE_EMIT_AL("width", TOKEN_IDENTIFIER);
          PDE_EMIT_AL("*", TOKEN_SYMBOL);
          PDE_EMIT_AL("height", TOKEN_IDENTIFIER);
          PDE_EMIT_AL(")", TOKEN_SYMBOL);
          i += 1;
          continue;
        }
      }

      int ls = receiver_start(tokens, i - 1);
      // Rewind out by the length of the already-emitted receiver: (i - ls)
      o -= (i - ls);
      PDE_EMIT_AL("_pde_len", TOKEN_IDENTIFIER);
      PDE_EMIT_AL("(", TOKEN_SYMBOL);
      for (int q = ls; q < i; q++) out[o++] = tokens[q];
      PDE_EMIT_AL(")", TOKEN_SYMBOL);
      #undef PDE_EMIT_AL
      i += 1; // skip '.' and 'length'
      continue;
    }
    out[o++] = tokens[i];
  }
  *out_tokens = out;
  return o;
}

int rewrite_pvector_accessors(Token *tokens, int num_tokens, Token **out_tokens) {
  Token *out = malloc(sizeof(Token) * (num_tokens * 2 + 64));
  int o = 0;
  for (int i = 0; i < num_tokens; i++) {
    // Match: [receiver] . (mag | magSq | heading | copy) ( )
    if (i + 3 < num_tokens &&
        tokens[i].type == TOKEN_DOT &&
        tokens[i+1].type == TOKEN_IDENTIFIER &&
        (strcmp(tokens[i+1].text, "mag") == 0 ||
         strcmp(tokens[i+1].text, "magSq") == 0 ||
         strcmp(tokens[i+1].text, "heading") == 0 ||
         strcmp(tokens[i+1].text, "copy") == 0) &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "(") == 0 &&
        tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, ")") == 0) {
      int ls = receiver_start(tokens, i - 1);
      // Rewind out by the length of the already-emitted receiver: (i - ls)
      o -= (i - ls);
      Token anchor = tokens[i];
      #define PDE_EMIT_PV(txt, typ) do { Token mk = anchor; \
          snprintf(mk.text, MAX_TOKEN_TEXT, "%s", txt); mk.type = typ; out[o++] = mk; } while (0)
      char fnName[MAX_TOKEN_TEXT];
      snprintf(fnName, sizeof(fnName), "pvector_%s", tokens[i+1].text);
      PDE_EMIT_PV(fnName, TOKEN_IDENTIFIER);
      PDE_EMIT_PV("(", TOKEN_SYMBOL);
      for (int q = ls; q < i; q++) out[o++] = tokens[q];
      PDE_EMIT_PV(")", TOKEN_SYMBOL);
      #undef PDE_EMIT_PV
      i += 3; // skip '.', method, '(', ')'
      continue;
    }
    out[o++] = tokens[i];
  }
  *out_tokens = out;
  return o;
}

int rewrite_gl_calls(Token *tokens, int num_tokens, Token **out_tokens) {
  Token *out = malloc(sizeof(Token) * (num_tokens * 2 + 64));
  int o = 0;
  for (int i = 0; i < num_tokens; i++) {
    // 1. Strip `gl.` or `GL.` before standard OpenGL functions (starts with "gl") or constants ("GL_")
    if (i + 2 < num_tokens &&
        (strcmp(tokens[i].text, "gl") == 0 || strcmp(tokens[i].text, "GL") == 0) &&
        tokens[i+1].type == TOKEN_DOT &&
        tokens[i+2].type == TOKEN_IDENTIFIER &&
        (strncmp(tokens[i+2].text, "gl", 2) == 0 || strncmp(tokens[i+2].text, "GL_", 3) == 0)) {
      i++; // skips tokens[i] (which was gl/GL) and moves to dot
      continue;
    }
    // 2. Rewrite `X.beginGL()` -> `_pde_pgl_beginGL()`
    if (i + 4 < num_tokens &&
        tokens[i+1].type == TOKEN_DOT &&
        strcmp(tokens[i+2].text, "beginGL") == 0 &&
        tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "(") == 0 &&
        tokens[i+4].type == TOKEN_SYMBOL && strcmp(tokens[i+4].text, ")") == 0) {
      Token fn = tokens[i+2];
      snprintf(fn.text, MAX_TOKEN_TEXT, "_pde_pgl_beginGL");
      out[o++] = fn;
      out[o++] = tokens[i+3]; // (
      out[o++] = tokens[i+4]; // )
      i += 4;
      continue;
    }
    // 3. Rewrite `X.endGL()` -> `_pde_pgl_endGL()`
    if (i + 4 < num_tokens &&
        tokens[i+1].type == TOKEN_DOT &&
        strcmp(tokens[i+2].text, "endGL") == 0 &&
        tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "(") == 0 &&
        tokens[i+4].type == TOKEN_SYMBOL && strcmp(tokens[i+4].text, ")") == 0) {
      Token fn = tokens[i+2];
      snprintf(fn.text, MAX_TOKEN_TEXT, "_pde_pgl_endGL");
      out[o++] = fn;
      out[o++] = tokens[i+3]; // (
      out[o++] = tokens[i+4]; // )
      i += 4;
      continue;
    }
    // 4. Rewrite `X.setSwapInterval(...)` -> `_pde_gl_setSwapInterval(...)`
    if (i + 3 < num_tokens &&
        tokens[i+1].type == TOKEN_DOT &&
        strcmp(tokens[i+2].text, "setSwapInterval") == 0 &&
        tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "(") == 0) {
      Token fn = tokens[i+2];
      snprintf(fn.text, MAX_TOKEN_TEXT, "_pde_gl_setSwapInterval");
      out[o++] = fn;
      i += 2; // skip receiver and dot; loop will process `(` and args next
      continue;
    }
    out[o++] = tokens[i];
  }
  *out_tokens = out;
  return o;
}

// Rewrite `s += expr` -> `s = _pde_cat(s, (expr))` for String-typed LHS so the
// compound-assign operator works like Java String concatenation-assign.
int rewrite_str_assign(Token *tokens, int num_tokens, Token **out_tokens) {
  Token *out = malloc(sizeof(Token) * (num_tokens * 4 + 64));
  int o = 0;
  for (int i = 0; i < num_tokens; i++) {
    if (!(tokens[i].type == TOKEN_IDENTIFIER &&
          i + 1 < num_tokens &&
          tokens[i+1].type == TOKEN_OPERATOR &&
          strcmp(tokens[i+1].text, "+=") == 0 &&
          i + 2 < num_tokens)) {
      out[o++] = tokens[i];
      continue;
    }
    // gate: LHS must be a String (or not known to be a non-string)
    if (symbol_type_non_string(tokens[i].text)) {
      out[o++] = tokens[i];
      continue;
    }
    int rhsS = i + 2;
    int rhsE = rhs_end(tokens, num_tokens, rhsS);
    bool rhsStr = range_is_string(tokens, rhsS, rhsE);
    const char *ct = symbol_type(tokens[i].text);
    bool lhsKnownStr = (ct && (strcmp(ct, "const char *") == 0 || strcmp(ct, "char *") == 0 || strcmp(ct, "String") == 0));
    if (!lhsKnownStr && !rhsStr) {
      out[o++] = tokens[i];
      continue;
    }
    Token anchor = tokens[i];
    #define PDE_EMIT_AS(txt, typ) do { Token mk = anchor; \
        snprintf(mk.text, MAX_TOKEN_TEXT, "%s", txt); mk.type = typ; out[o++] = mk; } while (0)
    out[o++] = tokens[i];               // s
    PDE_EMIT_AS("=", TOKEN_OPERATOR);
    PDE_EMIT_AS("_pde_cat", TOKEN_IDENTIFIER);
    PDE_EMIT_AS("(", TOKEN_SYMBOL);
    out[o++] = tokens[i];               // s
    PDE_EMIT_AS(",", TOKEN_SYMBOL);
    if (rhsStr) {
      for (int q = rhsS; q < rhsE; q++) out[o++] = tokens[q];
    } else {
      PDE_EMIT_AS("_pde_numf", TOKEN_IDENTIFIER);
      PDE_EMIT_AS("(", TOKEN_SYMBOL);
      for (int q = rhsS; q < rhsE; q++) out[o++] = tokens[q];
      PDE_EMIT_AS(")", TOKEN_SYMBOL);
    }
    PDE_EMIT_AS(")", TOKEN_SYMBOL);
    #undef PDE_EMIT_AS
    i = rhsE - 1;
  }
  *out_tokens = out;
  return o;
}

static bool range_is_definitely_string(Token *t, int start, int end) {
  if (end <= start) return false;
  if (t[start].type == TOKEN_STRING) return t[start].text[0] != '\'';
  if (t[start].type != TOKEN_IDENTIFIER) return false;
  const char *ct = symbol_type(t[start].text);
  if (ct && (strcmp(ct, "const char *") == 0 || strcmp(ct, "char *") == 0 || strcmp(ct, "String") == 0))
    return true;
  if (strncmp(t[start].text, "_pde_str_", 9) == 0 ||
      strcmp(t[start].text, "str") == 0 || strcmp(t[start].text, "trim") == 0)
    return true;
  return false;
}

// Rewrite `a == b` / `a != b` -> `_pde_str_eq(a, b)` / `!_pde_str_eq(a, b)`
// when both operands are single-token Strings, so Java String equality
// semantics hold (C would otherwise compare pointers).
int rewrite_str_cmp(Token *tokens, int num_tokens, Token **out_tokens) {
  Token *out = malloc(sizeof(Token) * (num_tokens * 2 + 24));
  int o = 0;
  for (int i = 0; i < num_tokens; i++) {
    bool isNeq = tokens[i].type == TOKEN_OPERATOR && strcmp(tokens[i].text, "!=") == 0;
    bool isEq  = tokens[i].type == TOKEN_OPERATOR && strcmp(tokens[i].text, "==") == 0;
    if ((isEq || isNeq) && i - 1 >= 0 && i + 1 < num_tokens) {
      int ls = receiver_start(tokens, i - 1);
      bool lStr = range_is_string(tokens, ls, i);
      bool rStr = range_is_string(tokens, i + 1, i + 2);
      if (lStr && rStr && (range_is_definitely_string(tokens, ls, i) || range_is_definitely_string(tokens, i + 1, i + 2))) {
        o -= (i - ls); // drop the already-emitted left operand from output tail
        Token anchor = tokens[i];
        #define PDE_EMIT_AE(txt, typ) do { Token mk = anchor; \
            snprintf(mk.text, MAX_TOKEN_TEXT, "%s", txt); mk.type = typ; out[o++] = mk; } while (0)
        if (isNeq) PDE_EMIT_AE("!", TOKEN_OPERATOR);
        PDE_EMIT_AE("_pde_str_eq", TOKEN_IDENTIFIER);
        PDE_EMIT_AE("(", TOKEN_SYMBOL);
        for (int q = ls; q < i; q++) out[o++] = tokens[q];
        PDE_EMIT_AE(",", TOKEN_SYMBOL);
        out[o++] = tokens[i + 1];
        PDE_EMIT_AE(")", TOKEN_SYMBOL);
        #undef PDE_EMIT_AE
        i += 1;
        continue;
      }
    }
    out[o++] = tokens[i];
  }
  *out_tokens = out;
  return o;
}

// extent of one concat operand starting at t[start]: stops at a top-level
// '+', ',', ';', '}' or unbalanced closer; returns exclusive end index
static int concat_operand_end(Token *t, int n, int start) {
  int depth = 0, j = start;
  while (j < n) {
    Token tk = t[j];
    if (tk.type == TOKEN_SYMBOL) {
      const char *s = tk.text;
      if (strcmp(s, "(") == 0 || strcmp(s, "[") == 0) depth++;
      else if (strcmp(s, ")") == 0 || strcmp(s, "]") == 0) {
        if (depth == 0) break;
        depth--;
      }
      else if (depth == 0 &&
               (strcmp(s, ",") == 0 || strcmp(s, ";") == 0 || strcmp(s, "}") == 0)) break;
    } else if (tk.type == TOKEN_OPERATOR && depth == 0 &&
               strcmp(tk.text, "+") == 0) break;
    else if (tk.type == TOKEN_EOF) break;
    j++;
  }
  return j;
}

// does the operand evaluate to a string?
static bool concat_operand_stringy(Token *t, int start, int end) {
  for (int j = start; j < end; j++) {
    if (t[j].type == TOKEN_STRING && t[j].text[0] != '\'') return true;
    if (t[j].type == TOKEN_IDENTIFIER) {
      const char *s = t[j].text;
      if (strcmp(s, "_pde_charat") == 0 || strcmp(s, "str") == 0 ||
          strcmp(s, "nf") == 0 || strcmp(s, "nfc") == 0 ||
          strcmp(s, "nfp") == 0 || strcmp(s, "nfs") == 0 ||
          strcmp(s, "hex") == 0 || strcmp(s, "binary") == 0 ||
          strcmp(s, "trim") == 0 || strcmp(s, "sketchPath") == 0 ||
          strcmp(s, "dataPath") == 0)
        return true;
      const char *ct = symbol_type(s);
      if (ct && (strcmp(ct, "const char *") == 0 || strcmp(ct, "char *") == 0 || strcmp(ct, "String") == 0))
        return true;
    }
  }
  return false;
}

// Fold expr + "lit" + expr chains into _pde_cat(...) calls. Right-assoc nesting
// (concatenation is associative). Numeric operands are wrapped in
// _pde_numf(...); string-valued operands pass through untouched.
int rewrite_concat(Token *tokens, int num_tokens, Token **out_tokens) {
  Token *out = malloc(sizeof(Token) * (num_tokens * 8 + 64));
  int o = 0;
  int i = 0;
  while (i < num_tokens) {
    if (i > 0 && tokens[i-1].type == TOKEN_DOT) {
      out[o++] = tokens[i++];
      continue;
    }
    if (tokens[i].type == TOKEN_SYMBOL &&
        (strcmp(tokens[i].text, ";") == 0 || strcmp(tokens[i].text, ",") == 0 ||
         strcmp(tokens[i].text, ")") == 0 || strcmp(tokens[i].text, "]") == 0 ||
         strcmp(tokens[i].text, "}") == 0 || strcmp(tokens[i].text, "{") == 0)) {
      out[o++] = tokens[i++];
      continue;
    }
    if (tokens[i].type == TOKEN_KEYWORD &&
        (strcmp(tokens[i].text, "return") == 0 || strcmp(tokens[i].text, "if") == 0 ||
         strcmp(tokens[i].text, "while") == 0 || strcmp(tokens[i].text, "for") == 0 ||
         strcmp(tokens[i].text, "else") == 0 || strcmp(tokens[i].text, "case") == 0 ||
         strcmp(tokens[i].text, "switch") == 0 || strcmp(tokens[i].text, "break") == 0 ||
         strcmp(tokens[i].text, "continue") == 0)) {
      out[o++] = tokens[i++];
      continue;
    }

    int s0 = i;
    int e0 = concat_operand_end(tokens, num_tokens, s0);
    if (e0 > s0 && e0 < num_tokens && tokens[e0].type == TOKEN_OPERATOR &&
        strcmp(tokens[e0].text, "+") == 0) {
      int segStart[64], segEnd[64];
      bool segStr[64];
      int m = 0;
      segStart[m] = s0; segEnd[m] = e0;
      segStr[m] = concat_operand_stringy(tokens, s0, e0);
      m++;

      int j = e0;
      while (j < num_tokens && tokens[j].type == TOKEN_OPERATOR &&
             strcmp(tokens[j].text, "+") == 0 && m < 64) {
        int s = j + 1;
        int e = concat_operand_end(tokens, num_tokens, s);
        if (e <= s) break;
        segStart[m] = s; segEnd[m] = e;
        segStr[m] = concat_operand_stringy(tokens, s, e);
        m++;
        j = e;
      }

      bool anyStr = false;
      for (int k = 0; k < m; k++) {
        if (segStr[k]) { anyStr = true; break; }
      }

      if (anyStr && m >= 2) {
        Token anchor = tokens[i]; // line/fileId donor
        #define PDE_EMIT(txt, typ) { \
          Token mk = anchor; \
          snprintf(mk.text, MAX_TOKEN_TEXT, "%s", txt); \
          mk.type = typ; \
          out[o++] = mk; \
        }
        #define PDE_COPYRANGE(a, b) { \
          for (int q = (a); q < (b); q++) out[o++] = tokens[q]; \
        }

        PDE_EMIT("_pde_cat", TOKEN_IDENTIFIER);
        PDE_EMIT("(", TOKEN_SYMBOL);
        for (int k = 0; k < m; k++) {
          if (k > 0) {
            PDE_EMIT(",", TOKEN_SYMBOL);
            if (k < m - 1) { PDE_EMIT("_pde_cat", TOKEN_IDENTIFIER); PDE_EMIT("(", TOKEN_SYMBOL); }
          }
          if (segStr[k]) {
            PDE_COPYRANGE(segStart[k], segEnd[k]);
          } else {
            PDE_EMIT("_pde_numf", TOKEN_IDENTIFIER);
            PDE_EMIT("(", TOKEN_SYMBOL);
            PDE_COPYRANGE(segStart[k], segEnd[k]);
            PDE_EMIT(")", TOKEN_SYMBOL);
          }
        }
        for (int k = 0; k < m - 1; k++) PDE_EMIT(")", TOKEN_SYMBOL);
        #undef PDE_EMIT
        #undef PDE_COPYRANGE

        i = j;
        continue;
      }
    }

    out[o++] = tokens[i++];
  }
  *out_tokens = out;
  return o;
}

// ===========================================================================
// User-defined classes (Phase 6)
// ===========================================================================
// Each Processing class becomes a plain-C struct plus receiver-prefixed free
// functions. rewrite_classes() strips `class NAME { ... }` blocks out of the
// main token stream (so the later forward-decl / name-collision passes never
// see method bodies), routes `new Name(args)` -> `Name_ctor(args)`,
// ArrayList member ops, `(Type)list.get(i)` casts and `obj.method(...)` calls
// on class-typed variables, and records everything the two emission phases
// (up-front typedefs + prototypes, post-sketch definitions) need. Class
// method bodies are re-emitted with bare field identifiers rewritten to
// `this->field` (or `this_.field` inside constructors).

#define MAX_CLASSES 64
#define MAX_FIELDS 64
#define MAX_METHODS 64
#define MAX_SYMBOLS 4096
#define MAX_LOCALS 128

typedef struct {
  char name[MAX_TOKEN_TEXT];
  char type[MAX_TOKEN_TEXT];       // mapped C type for the struct member
  bool isPointer;                  // "float w[];" -> member `float *w;`
  bool hasInit;
  Token *init;                     // copy of the field-initializer tokens
  int ninit;
} ClassField;

typedef struct {
  char name[MAX_TOKEN_TEXT];
  bool isCtor;
  char ret[MAX_TOKEN_TEXT];        // mapped C return type
  char params[2048];               // rendered "float a, int b" param list
  int sigOpen, sigClose;           // token indexes of the parameter parens
  int bodyOpen, bodyClose;         // token indexes of the '{ ... }' body
  Token *body;                     // copy of the body tokens (excl. braces)
  int nbody;
  char locals[MAX_LOCALS][MAX_TOKEN_TEXT];
  char local_types[MAX_LOCALS][MAX_TOKEN_TEXT];
  int nlocals;
  int nparams;
} ClassMethod;

typedef struct {
  char name[MAX_TOKEN_TEXT];
  ClassField fields[MAX_FIELDS];
  int nfields;
  ClassMethod methods[MAX_METHODS];
  int nmethods;
  int ctorIdx;                     // -1 when no constructor was written
  int classOpen, classClose;       // token indexes of '{ ... }'
} ClassDef;

static ClassDef _classes[MAX_CLASSES];
static int _nclasses = 0;

typedef struct { char name[MAX_TOKEN_TEXT]; char type[MAX_TOKEN_TEXT]; } Symbol;
static Symbol _symtab[MAX_SYMBOLS];
static int _nsym = 0;

// Track globals with non-constant initializers (= expr containing function calls)
// so they can be deferred to _pde_global_init() instead of file-scope assignment.
#define MAX_GLOBAL_NONCONST 256
static char _global_nonconst_names[MAX_GLOBAL_NONCONST][MAX_TOKEN_TEXT];
static int _nglobal_nonconst = 0;

static bool is_nonconst_name(const char *name) {
  for (int i = 0; i < _nglobal_nonconst; i++)
    if (strcmp(_global_nonconst_names[i], name) == 0) return true;
  return false;
}

static int matching_close(Token *t, int n, int open, const char *os, const char *cs) {
  int depth = 0;
  for (int i = open; i < n; i++) {
    if (t[i].type != TOKEN_SYMBOL) continue;
    if (strcmp(t[i].text, os) == 0) depth++;
    else if (strcmp(t[i].text, cs) == 0) {
      if (--depth == 0) return i;
    }
  }
  return -1;
}

static bool is_class_name(const char *name) {
  for (int i = 0; i < _nclasses; i++)
    if (strcmp(_classes[i].name, name) == 0) return true;
  return false;
}

static bool class_has_method(const char *cls, const char *meth) {
  for (int i = 0; i < _nclasses; i++) {
    if (strcmp(_classes[i].name, cls) != 0) continue;
    for (int m = 0; m < _classes[i].nmethods; m++)
      if (!_classes[i].methods[m].isCtor &&
          strcmp(_classes[i].methods[m].name, meth) == 0) return true;
  }
  return false;
}

static bool is_ag_method(const char *name) {
  return strcmp(name, "add") == 0 || strcmp(name, "get") == 0 ||
         strcmp(name, "size") == 0 || strcmp(name, "set") == 0 ||
         strcmp(name, "remove") == 0 || strcmp(name, "clear") == 0;
}

static bool is_builtin_ctor_class(const char *name) {
  return strcmp(name, "Object") == 0 ||
         strcmp(name, "PeasyCam") == 0 ||
         strcmp(name, "Minim") == 0 ||
         strcmp(name, "AudioPlayer") == 0 ||
         strcmp(name, "AudioInput") == 0 ||
         strcmp(name, "AudioSnippet") == 0 ||
         strcmp(name, "AudioOutput") == 0 ||
         strcmp(name, "AudioStream") == 0 ||
         strcmp(name, "SineWave") == 0 ||
         strcmp(name, "FadeOut") == 0 ||
         strcmp(name, "FadeIn") == 0 ||
         strcmp(name, "FullScreen") == 0 ||
         strcmp(name, "GSMovieMaker") == 0 ||
         strcmp(name, "MovieMaker") == 0 ||
         strcmp(name, "PMatrix3D") == 0;
}

static bool is_plausible_type_token(Token t) {
  if (t.type == TOKEN_KEYWORD) {
    return strcmp(t.text, "int") == 0 || strcmp(t.text, "float") == 0 ||
           strcmp(t.text, "boolean") == 0 || strcmp(t.text, "color") == 0 ||
           strcmp(t.text, "char") == 0 || strcmp(t.text, "double") == 0 ||
           strcmp(t.text, "byte") == 0 || strcmp(t.text, "void") == 0;
  }
  if (t.type != TOKEN_IDENTIFIER) return false;
  if (strcmp(t.text, "PVector") == 0 || strcmp(t.text, "String") == 0 ||
      strcmp(t.text, "ArrayList") == 0 || strcmp(t.text, "PImage") == 0 ||
      strcmp(t.text, "PGraphics") == 0 || strcmp(t.text, "PFont") == 0 ||
      strcmp(t.text, "Object") == 0 || strcmp(t.text, "PeasyCam") == 0 ||
      strcmp(t.text, "Minim") == 0 || strcmp(t.text, "AudioPlayer") == 0 ||
      strcmp(t.text, "AudioInput") == 0 || strcmp(t.text, "AudioSnippet") == 0 ||
      strcmp(t.text, "AudioOutput") == 0 || strcmp(t.text, "SineWave") == 0 ||
      strcmp(t.text, "FullScreen") == 0 || strcmp(t.text, "GSMovieMaker") == 0 ||
      strcmp(t.text, "MovieMaker") == 0 || strcmp(t.text, "PMatrix3D") == 0)
    return true;
  return is_class_name(t.text);
}

// Index of the `>` matching the `<` at t[open] (nesting-aware), or -1.
// Depth starts at 0 because the loop counts the opening `<` itself. `<`/`>`
// tokenize as TOKEN_OPERATOR, so matching is on text alone.
static int skip_angle_close(Token *t, int n, int open) {
  int d = 0;
  for (int j = open; j < n; j++) {
    if (strcmp(t[j].text, "<") == 0) d++;
    else if (strcmp(t[j].text, ">") == 0) {
      if (--d == 0) return j;
    }
  }
  return -1;
}

// If t[i] is `ArrayList` with a `<...>` suffix, return the index of the `>`
// (generic args would clash with C's `<`/`>` operators, so they are dropped;
// the declaration's variable name sits right after that `>`). Otherwise
// return i unchanged, so callers can uniformly do `helper() + 1` to reach
// the name token.
static int skip_al_generics(Token *t, int n, int i) {
  if (t[i].type == TOKEN_IDENTIFIER && strcmp(t[i].text, "ArrayList") == 0 &&
      i + 1 < n && strcmp(t[i + 1].text, "<") == 0) {
    int close = skip_angle_close(t, n, i + 1);
    if (close >= 0) return close;
  }
  return i;
}

static void reg_sym_typed(const char *name, const char *type) {
  if (_nsym >= MAX_SYMBOLS) return;
  for (int i = 0; i < _nsym; i++) {
    if (strcmp(_symtab[i].name, name) == 0) {
      strncpy(_symtab[i].type, type, MAX_TOKEN_TEXT - 1);
      return;
    }
  }
  snprintf(_symtab[_nsym].name, MAX_TOKEN_TEXT, "%s", name);
  snprintf(_symtab[_nsym].type, MAX_TOKEN_TEXT, "%s", type);
  _nsym++;
}

static const char *symbol_type(const char *name) {
  for (int i = 0; i < _nsym; i++)
    if (strcmp(_symtab[i].name, name) == 0) return _symtab[i].type;
  return NULL;
}

// Parse `TYPE [flags] name` declarations inside a method body (params and
// top-level-ish locals/loop vars) and record their names+types.
static void collect_locals(Token *t, int start, int end, ClassMethod *m) {
  for (int i = start; i < end && i + 1 < end; i++) {
    if (!is_plausible_type_token(t[i])) continue;
    if (i + 2 < end && t[i + 1].type == TOKEN_SYMBOL &&
        strcmp(t[i + 1].text, "[") == 0 && t[i + 2].type == TOKEN_SYMBOL &&
        strcmp(t[i + 2].text, "]") == 0) i += 2;
    if (strcmp(t[i].text, "ArrayList") == 0) i = skip_al_generics(t, end, i);
    int j = i + 1;
    if (j >= end || t[j].type != TOKEN_IDENTIFIER) continue;
    if (j + 1 < end && ((t[j + 1].type == TOKEN_SYMBOL &&
                         strcmp(t[j + 1].text, "(") == 0) ||
                        (t[j + 1].type == TOKEN_DOT)))
      continue; // it's a call or member chain, not a declaration
    char mtype[MAX_TOKEN_TEXT];
    snprintf(mtype, sizeof(mtype), "%s", emit_c_type_str(t[i].text));
    if (m->nlocals < MAX_LOCALS) {
      snprintf(m->locals[m->nlocals], MAX_TOKEN_TEXT, "%s", t[j].text);
      snprintf(m->local_types[m->nlocals], MAX_TOKEN_TEXT, "%s", mtype);
      m->nlocals++;
    }
    reg_sym_typed(t[j].text, mtype);
    int cur_j = j;
    while (cur_j < end) {
      while (cur_j < end && !(t[cur_j].type == TOKEN_SYMBOL &&
             (strcmp(t[cur_j].text, ",") == 0 || strcmp(t[cur_j].text, ";") == 0))) {
        if (t[cur_j].type == TOKEN_SYMBOL && (strcmp(t[cur_j].text, "(") == 0 || strcmp(t[cur_j].text, "[") == 0)) {
          int cl = matching_close(t, end, cur_j, t[cur_j].text, strcmp(t[cur_j].text, "(") == 0 ? ")" : "]");
          if (cl > cur_j) { cur_j = cl + 1; continue; }
        }
        cur_j++;
      }
      if (cur_j < end && t[cur_j].type == TOKEN_SYMBOL && strcmp(t[cur_j].text, ",") == 0) {
        cur_j++;
        while (cur_j < end && t[cur_j].type == TOKEN_SYMBOL && strcmp(t[cur_j].text, "*") == 0) cur_j++;
        if (cur_j < end && t[cur_j].type == TOKEN_IDENTIFIER) {
          if (m->nlocals < MAX_LOCALS) {
            snprintf(m->locals[m->nlocals], MAX_TOKEN_TEXT, "%s", t[cur_j].text);
            snprintf(m->local_types[m->nlocals], MAX_TOKEN_TEXT, "%s", mtype);
            m->nlocals++;
          }
          reg_sym_typed(t[cur_j].text, mtype);
        }
      } else {
        break;
      }
    }
    i = j;
  }
}

// Parse the parameter list between sigOpen and sigClose, register the
// PARAMETER NAMES (so the this-> rewrite does not touch them) and render the
// C parameter list into m->params.
static void parse_params(Token *t, int n, int sigOpen, int sigClose, ClassMethod *m) {
  size_t used = 0;
  m->params[0] = '\0';
  m->nparams = 0;
  int i = sigOpen + 1;
  while (i < sigClose) {
    if (i >= n) break;
    char tymap[MAX_TOKEN_TEXT];
    snprintf(tymap, sizeof(tymap), "%s", emit_c_type_str(t[i].text));
    i++;
    if (strcmp(tymap, "ArrayList") == 0) i = skip_al_generics(t, sigClose, i);
    bool arr = false;
    if (i + 1 < sigClose && strcmp(t[i].text, "[") == 0 &&
        strcmp(t[i + 1].text, "]") == 0) { arr = true; i += 2; }
    if (i < sigClose && t[i].type == TOKEN_IDENTIFIER) {
      m->nparams++;
      char ptype[MAX_TOKEN_TEXT];
      snprintf(ptype, sizeof(ptype), "%s%s", tymap, arr ? " *" : "");
      if (m->nlocals < MAX_LOCALS) {
        snprintf(m->locals[m->nlocals], MAX_TOKEN_TEXT, "%s", t[i].text);
        snprintf(m->local_types[m->nlocals], MAX_TOKEN_TEXT, "%s", ptype);
        m->nlocals++;
      }
      reg_sym_typed(t[i].text, ptype);
      size_t wrote = snprintf(m->params + used, sizeof(m->params) - used,
                              "%s%s%s%s", used ? ", " : "",
                              ptype, ptype[strlen(ptype)-1]=='*' ? "" : " ", t[i].text);
      used += (wrote < sizeof(m->params) - used) ? wrote : 0;
      i++;
      if (i < sigClose && strcmp(t[i].text, "[") == 0) {
        if (i + 1 < sigClose && strcmp(t[i + 1].text, "]") == 0) i += 2;
      }
    }
    if (i < sigClose && strcmp(t[i].text, ",") == 0) i++;
  }
}

// Parse one `class NAME { ... }` starting at the "class" keyword token `kw`.
// Called once per class before any token rewriting so _classes is complete.
static void parse_class(Token *t, int n, int kw) {
  ClassDef *c = NULL;
  for (int i = 0; i < _nclasses; i++) {
    if (strcmp(_classes[i].name, t[kw + 1].text) == 0) {
      c = &_classes[i];
      break;
    }
  }
  if (!c) {
    if (_nclasses >= MAX_CLASSES) return;
    c = &_classes[_nclasses++];
    memset(c, 0, sizeof(*c));
    snprintf(c->name, MAX_TOKEN_TEXT, "%s", t[kw + 1].text);
  }
  c->nfields = 0;
  c->nmethods = 0;
  c->ctorIdx = -1;
  snprintf(c->name, MAX_TOKEN_TEXT, "%s", t[kw + 1].text);

  int p = kw + 2;
  while (p < n && !(t[p].type == TOKEN_SYMBOL && strcmp(t[p].text, "{") == 0)) p++;
  c->classOpen = p;
  c->classClose = matching_close(t, n, p, "{", "}");
  if (c->classClose < 0) { c->classClose = p + 1; return; }

  int m = p + 1;
  while (m < c->classClose) {
    Token tk = t[m];
    if (tk.type == TOKEN_SYMBOL && strcmp(tk.text, ";") == 0) { m++; continue; }
    if (tk.type == TOKEN_IDENTIFIER && strcmp(tk.text, "static") == 0) { m++; continue; }

    // constructor: NAME-of-class '(' or method: TYPE NAME '(' / TYPE[] NAME '('
    bool isCtor = (tk.type == TOKEN_IDENTIFIER && strcmp(tk.text, c->name) == 0 &&
                   m + 1 < c->classClose && t[m + 1].type == TOKEN_SYMBOL &&
                   strcmp(t[m + 1].text, "(") == 0);
    int nameTok = -1;
    int typeTok = m;
    bool isArrayRet = false;
    if (!isCtor && is_plausible_type_token(tk) && m + 1 < c->classClose) {
      int j = m + 1;
      if (j + 1 < c->classClose && strcmp(t[j].text, "[") == 0 &&
          strcmp(t[j + 1].text, "]") == 0) { j += 2; isArrayRet = true; }
      if (j < c->classClose && (t[j].type == TOKEN_IDENTIFIER || strcmp(t[j].text, "draw") == 0 || strcmp(t[j].text, "setup") == 0) &&
          j + 1 < c->classClose && t[j + 1].type == TOKEN_SYMBOL &&
          strcmp(t[j + 1].text, "(") == 0) {
        nameTok = j;
      }
    }
    if (isCtor || nameTok >= 0) {
      if (c->nmethods >= MAX_METHODS) break;
      ClassMethod *meth = &c->methods[c->nmethods];
      memset(meth, 0, sizeof(*meth));
      meth->isCtor = isCtor;
      meth->nlocals = 0;
      int openIdx = isCtor ? m + 1 : nameTok + 1;
      if (isCtor) {
        snprintf(meth->name, MAX_TOKEN_TEXT, "%s", c->name);
        snprintf(meth->ret, MAX_TOKEN_TEXT, "%s", c->name);
        c->ctorIdx = c->nmethods;
      } else {
        snprintf(meth->name, MAX_TOKEN_TEXT, "%s", t[nameTok].text);
        if (isArrayRet)
          snprintf(meth->ret, MAX_TOKEN_TEXT, "%s *", emit_c_type_str(t[typeTok].text));
        else
          snprintf(meth->ret, MAX_TOKEN_TEXT, "%s", emit_c_type_str(t[typeTok].text));
      }
      meth->sigOpen = openIdx;
      meth->sigClose = matching_close(t, n, openIdx, "(", ")");
      if (meth->sigClose < 0) { meth->sigClose = openIdx; }
      int bo = meth->sigClose + 1;
      // skip a trailing array return marker "[]" for "T f(...)[] {"
      if (bo + 1 < c->classClose && strcmp(t[bo].text, "[") == 0 &&
          strcmp(t[bo + 1].text, "]") == 0) {
        bo += 2;
        snprintf(meth->ret, MAX_TOKEN_TEXT, "%s *", emit_c_type_str(t[typeTok].text));
      }
      if (bo < c->classClose && t[bo].type == TOKEN_SYMBOL &&
          strcmp(t[bo].text, "{") == 0) {
        meth->bodyOpen = bo;
        meth->bodyClose = matching_close(t, n, bo, "{", "}");
        if (meth->bodyClose < 0) meth->bodyClose = bo;
        parse_params(t, n, meth->sigOpen, meth->sigClose, meth);
        collect_locals(t, meth->bodyOpen + 1, meth->bodyClose, meth);
      } else {
        meth->bodyOpen = bo;
        meth->bodyClose = bo;
      }
      meth->nbody = meth->bodyClose - meth->bodyOpen - 1;
      if (meth->nbody > 0) {
        meth->body = malloc(sizeof(Token) * (size_t)meth->nbody);
        memcpy(meth->body, &t[meth->bodyOpen + 1], sizeof(Token) * (size_t)meth->nbody);
      } else {
        meth->body = NULL;
      }
      c->nmethods++;
      m = meth->bodyClose + 1;
      continue;
    }

    // otherwise: a field declaration TYPE NAME... (only entered when the token
    // actually looks like a type: otherwise skip it as a stray/operator)
    if (!is_plausible_type_token(tk)) { m++; continue; }
    const char *ftype = emit_c_type_str(tk.text);
    int k = m + 1;
    if (strcmp(ftype, "ArrayList") == 0) k = skip_al_generics(t, c->classClose, m) + 1;
    bool any = false;
    for (;;) {
      bool arr = false;
      if (k + 1 < c->classClose && t[k].type == TOKEN_SYMBOL &&
          strcmp(t[k].text, "[") == 0 && t[k + 1].type == TOKEN_SYMBOL &&
          strcmp(t[k + 1].text, "]") == 0) { arr = true; k += 2; }
      if (k >= c->classClose || t[k].type != TOKEN_IDENTIFIER) break;
      if (c->nfields >= MAX_FIELDS) break;
      ClassField *f = &c->fields[c->nfields];
      snprintf(f->name, MAX_TOKEN_TEXT, "%s", t[k].text);
      snprintf(f->type, MAX_TOKEN_TEXT, "%s", ftype);
      f->isPointer = arr;
      f->hasInit = false;
      f->init = NULL;
      f->ninit = 0;
      char mtype[MAX_TOKEN_TEXT];
      snprintf(mtype, sizeof(mtype), "%s%s", ftype, arr ? " *" : "");
      reg_sym_typed(f->name, mtype);
      c->nfields++;
      any = true;
      k++;
      if (k + 1 < c->classClose && t[k].type == TOKEN_SYMBOL &&
          strcmp(t[k].text, "[") == 0 && t[k + 1].type == TOKEN_SYMBOL &&
          strcmp(t[k + 1].text, "]") == 0) { f->isPointer = true; k += 2; }
      if (k < c->classClose && t[k].type == TOKEN_OPERATOR &&
          strcmp(t[k].text, "=") == 0) {
        f->hasInit = true;
        int s = k + 1, e = s, depth = 0;
        while (e < c->classClose) {
          if (t[e].type == TOKEN_SYMBOL) {
            if (strcmp(t[e].text, "(") == 0 || strcmp(t[e].text, "[") == 0) depth++;
            else if (strcmp(t[e].text, ")") == 0 || strcmp(t[e].text, "]") == 0) {
              if (depth == 0) break;
              depth--;
            } else if (depth == 0 &&
                       (strcmp(t[e].text, ";") == 0 || strcmp(t[e].text, ",") == 0)) break;
          }
          e++;
        }
        f->ninit = e - s;
        if (f->ninit > 0) {
          f->init = malloc(sizeof(Token) * (size_t)f->ninit);
          memcpy(f->init, &t[s], sizeof(Token) * (size_t)f->ninit);
        }
        k = e;
      }
      if (k < c->classClose && t[k].type == TOKEN_SYMBOL &&
          strcmp(t[k].text, ",") == 0) { k++; continue; }
      break;
    }
    if (!any) { m++; continue; } // malformed; skip one token
    if (k < c->classClose && strcmp(t[k].text, ";") == 0) k++;
    m = k;
  }

  // default constructor when none written (so `new Foo();` always links)
  if (c->ctorIdx < 0 && c->nmethods < MAX_METHODS) {
    ClassMethod *meth = &c->methods[c->nmethods];
    memset(meth, 0, sizeof(*meth));
    meth->isCtor = true;
    meth->nlocals = 0;
    snprintf(meth->name, MAX_TOKEN_TEXT, "%s", c->name);
    snprintf(meth->ret, MAX_TOKEN_TEXT, "%s", c->name);
    snprintf(meth->params, sizeof(meth->params), "void");
    meth->sigOpen = meth->sigClose = c->classOpen;
    meth->bodyOpen = meth->bodyClose = c->classOpen + 1;
    c->ctorIdx = c->nmethods;
    c->nmethods++;
  }
}

// First pass: parse every class in file order (no token mutation).
static void scan_classes(Token *t, int n) {
  _nclasses = 0;
  for (int i = 0; i < n; i++) {
    if (t[i].type != TOKEN_IDENTIFIER || strcmp(t[i].text, "class") != 0) continue;
    if (i + 1 >= n || t[i + 1].type != TOKEN_IDENTIFIER) continue;
    if (_nclasses < MAX_CLASSES) {
      memset(&_classes[_nclasses], 0, sizeof(_classes[_nclasses]));
      snprintf(_classes[_nclasses].name, MAX_TOKEN_TEXT, "%s", t[i + 1].text);
      _nclasses++;
    }
  }

  for (int i = 0; i < n; i++) {
    if (t[i].type != TOKEN_IDENTIFIER || strcmp(t[i].text, "class") != 0) continue;
    if (i + 1 >= n || t[i + 1].type != TOKEN_IDENTIFIER) continue;
    parse_class(t, n, i);
    int closeIdx = -1;
    for (int c = 0; c < _nclasses; c++) {
      if (strcmp(_classes[c].name, t[i + 1].text) == 0) {
        closeIdx = _classes[c].classClose;
        break;
      }
    }
    if (closeIdx > i) i = closeIdx;
  }
}

// Second pass: top-level (brace-depth 0) variable declarations are globals.
static char _globnames[MAX_SYMBOLS][MAX_TOKEN_TEXT];
static char _globtypes[MAX_SYMBOLS][MAX_TOKEN_TEXT];
static int _nglob = 0;

static bool is_global_name(const char *name) {
  for (int i = 0; i < _nglob; i++)
    if (strcmp(_globnames[i], name) == 0) return true;
  return false;
}

static void scan_globals(Token *t, int n) {
  int depth = 0;
  for (int i = 0; i + 1 < n; i++) {
    Token tk = t[i];
    if (tk.type == TOKEN_KEYWORD && strcmp(tk.text, "void") == 0 &&
        i + 1 < n && (t[i + 1].type == TOKEN_IDENTIFIER || strcmp(t[i + 1].text, "setup") == 0 || strcmp(t[i + 1].text, "draw") == 0) &&
        i + 2 < n && t[i + 2].type == TOKEN_SYMBOL &&
        strcmp(t[i + 2].text, "(") == 0) {
      // a function definition; skip through its matching '{ ... }' body so
      // locals are never mistaken for globals
      int j = i + 2, d = 0, close = -1;
      for (; j < n; j++) {
        if (t[j].text[0] == '(') d++;
        else if (t[j].text[0] == ')') { if (--d == 0) { close = j; break; } }
      }
      int k = close + 1;
      while (k < n && t[k].text[0] != '{') k++;
      if (k < n) {
        int bc = matching_close(t, n, k, "{", "}");
        i = (bc > k) ? bc : k;
      }
      continue;
    }
    if (tk.type == TOKEN_SYMBOL) {
      if (strcmp(tk.text, "{") == 0) depth++;
      else if (strcmp(tk.text, "}") == 0) { if (depth > 0) depth--; }
      else if (depth == 0)
        continue;
      continue;
    }
    if (depth != 0) continue;
    if (i > 0 && t[i - 1].type == TOKEN_IDENTIFIER && strcmp(t[i - 1].text, "class") == 0) continue;
    if (!is_plausible_type_token(tk)) continue;
    int j = i + 1;
    if (strcmp(tk.text, "ArrayList") == 0) j = skip_al_generics(t, n, i) + 1;
    while (j + 1 < n && t[j].type == TOKEN_SYMBOL && strcmp(t[j].text, "[") == 0 &&
           t[j + 1].type == TOKEN_SYMBOL && strcmp(t[j + 1].text, "]") == 0) j += 2;
    if (t[j].type != TOKEN_IDENTIFIER) continue;
    if (j + 1 < n && t[j + 1].type == TOKEN_SYMBOL && strcmp(t[j + 1].text, "(") == 0) continue;
    if (j + 1 < n && t[j + 1].type == TOKEN_SYMBOL && strcmp(t[j + 1].text, "{") == 0) continue;
    bool isArr = (j > i + 1);
    int after_j = j + 1;
    while (after_j + 1 < n && t[after_j].type == TOKEN_SYMBOL && strcmp(t[after_j].text, "[") == 0 &&
           t[after_j + 1].type == TOKEN_SYMBOL && strcmp(t[after_j + 1].text, "]") == 0) {
      isArr = true;
      after_j += 2;
    }
    bool isBraceInit = false;
    int eq = (isArr && after_j > j + 1) ? after_j : (j + 1);
    while (eq < n && eq < j + 10 && !(t[eq].type == TOKEN_SYMBOL && strcmp(t[eq].text, ";") == 0)) {
      if (t[eq].type == TOKEN_OPERATOR && strcmp(t[eq].text, "=") == 0 &&
          eq + 1 < n && t[eq + 1].type == TOKEN_SYMBOL && strcmp(t[eq + 1].text, "{") == 0) {
        isBraceInit = true;
        break;
      }
      eq++;
    }
    char gtype[MAX_TOKEN_TEXT];
    if (isBraceInit)
      snprintf(gtype, sizeof(gtype), "%s []", emit_c_type_str(tk.text));
    else
      snprintf(gtype, sizeof(gtype), "%s%s", emit_c_type_str(tk.text), isArr ? " *" : "");
    reg_sym_typed(t[j].text, gtype);
    if (_nglob < MAX_SYMBOLS) {
      snprintf(_globnames[_nglob], MAX_TOKEN_TEXT, "%s", t[j].text);
      snprintf(_globtypes[_nglob], MAX_TOKEN_TEXT, "%s", gtype);
      _nglob++;
    }
    int cur_j = j;
    while (cur_j < n) {
      while (cur_j < n && !(t[cur_j].type == TOKEN_SYMBOL &&
             (strcmp(t[cur_j].text, ",") == 0 || strcmp(t[cur_j].text, ";") == 0))) {
        if (t[cur_j].type == TOKEN_SYMBOL && (strcmp(t[cur_j].text, "(") == 0 || strcmp(t[cur_j].text, "[") == 0)) {
          int cl = matching_close(t, n, cur_j, t[cur_j].text, strcmp(t[cur_j].text, "(") == 0 ? ")" : "]");
          if (cl > cur_j) { cur_j = cl + 1; continue; }
        }
        cur_j++;
      }
      if (cur_j < n && t[cur_j].type == TOKEN_SYMBOL && strcmp(t[cur_j].text, ",") == 0) {
        cur_j++;
        while (cur_j < n && t[cur_j].type == TOKEN_SYMBOL && strcmp(t[cur_j].text, "*") == 0) cur_j++;
        if (cur_j < n && t[cur_j].type == TOKEN_IDENTIFIER) {
          bool arr2 = isArr;
          int after2 = cur_j + 1;
          while (after2 + 1 < n && t[after2].type == TOKEN_SYMBOL && strcmp(t[after2].text, "[") == 0 &&
                 t[after2 + 1].type == TOKEN_SYMBOL && strcmp(t[after2 + 1].text, "]") == 0) {
            arr2 = true;
            after2 += 2;
          }
          bool isBraceInit2 = false;
          int eq2 = after2;
          while (eq2 < n && eq2 < cur_j + 10 && !(t[eq2].type == TOKEN_SYMBOL && strcmp(t[eq2].text, ";") == 0)) {
            if (t[eq2].type == TOKEN_OPERATOR && strcmp(t[eq2].text, "=") == 0 &&
                eq2 + 1 < n && t[eq2 + 1].type == TOKEN_SYMBOL && strcmp(t[eq2 + 1].text, "{") == 0) {
              isBraceInit2 = true;
              break;
            }
            eq2++;
          }
          char gtype2[MAX_TOKEN_TEXT];
          if (isBraceInit2)
            snprintf(gtype2, sizeof(gtype2), "%s []", emit_c_type_str(tk.text));
          else
            snprintf(gtype2, sizeof(gtype2), "%s%s", emit_c_type_str(tk.text), arr2 ? " *" : "");
          reg_sym_typed(t[cur_j].text, gtype2);
          if (_nglob < MAX_SYMBOLS) {
            snprintf(_globnames[_nglob], MAX_TOKEN_TEXT, "%s", t[cur_j].text);
            snprintf(_globtypes[_nglob], MAX_TOKEN_TEXT, "%s", gtype2);
            _nglob++;
          }
        }
      } else {
        break;
      }
    }
    // Detect non-constant initializer: scan ahead for = expr containing (
    {
      int k = j + 1;
      while (k + 1 < n && t[k].type == TOKEN_SYMBOL &&
             strcmp(t[k].text, "[") == 0 &&
             t[k + 1].type == TOKEN_SYMBOL &&
             strcmp(t[k + 1].text, "]") == 0)
        k += 2;
      while (k < n && t[k].type != TOKEN_SYMBOL &&
             strcmp(t[k].text, "=") != 0) k++;
      if (k < n && strcmp(t[k].text, "=") == 0) {
        bool hasCall = false;
        for (int u = k + 1; u < n && u < k + 120; u++) {
          if (t[u].type == TOKEN_SYMBOL && strcmp(t[u].text, "(") == 0) {
            hasCall = true; break;
          }
          if (t[u].type == TOKEN_IDENTIFIER && strcmp(t[u].text, "new") == 0) {
            hasCall = true; break;
          }
          if (t[u].type == TOKEN_SYMBOL && strcmp(t[u].text, ";") == 0) break;
        }
        if (hasCall && _nglobal_nonconst < MAX_GLOBAL_NONCONST) {
          snprintf(_global_nonconst_names[_nglobal_nonconst],
                   MAX_TOKEN_TEXT, "%s", t[j].text);
          _nglobal_nonconst++;
        }
      }
    }
  }
}

// Register LOCAL variables of top-level sketch functions (setup, draw, event
// handlers, user helpers) into _symtab, so `list.add(...)`-style member-call
// routing and `(Type) list.get(i)` cast-gets work inside sketch bodies too.
// A "TYPE name ( ... ) { body }" shape (optional "[]" after name or params)
// at brace-depth 0 is treated as a function definition; its params are not
// registered (parse_params covers those for class methods only).
static void scan_fn_locals(Token *t, int n) {
  int depth = 0;
  for (int i = 0; i + 2 < n; i++) {
    Token tk = t[i];
    if (tk.type == TOKEN_SYMBOL) {
      if (strcmp(tk.text, "{") == 0) depth++;
      else if (strcmp(tk.text, "}") == 0) { if (depth > 0) depth--; }
      continue;
    }
    if (depth != 0) continue;
    if (!is_plausible_type_token(tk)) continue;
    int j = i + 1;
    if (j + 1 < n && t[j].type == TOKEN_SYMBOL && strcmp(t[j].text, "[") == 0 &&
        t[j + 1].type == TOKEN_SYMBOL && strcmp(t[j + 1].text, "]") == 0) j += 2;
    if (j >= n || (t[j].type != TOKEN_IDENTIFIER && strcmp(t[j].text, "setup") != 0 && strcmp(t[j].text, "draw") != 0)) continue;
    if (j + 1 >= n || t[j + 1].type != TOKEN_SYMBOL ||
        strcmp(t[j + 1].text, "(") != 0) continue;
    int close = matching_close(t, n, j + 1, "(", ")");
    if (close < 0) continue;
    int k = close + 1;
    if (k + 1 < n && t[k].type == TOKEN_SYMBOL && strcmp(t[k].text, "[") == 0 &&
        t[k + 1].type == TOKEN_SYMBOL && strcmp(t[k + 1].text, "]") == 0) k += 2;
    if (k < n && t[k].type == TOKEN_SYMBOL && strcmp(t[k].text, "{") == 0) {
      ClassMethod scratch;
      memset(&scratch, 0, sizeof(scratch));
      int bc = matching_close(t, n, k, "{", "}");
      if (bc > k) collect_locals(t, k + 1, bc, &scratch);
      i = (bc > k) ? bc : k;
      continue;
    }
    i = j; // consumed up to the name; resume so the "(" isn't re-scanned
  }
}

// Third pass: strip class bodies; route new/casts/member-calls. Runs BEFORE
// the forward-declaration and name-collision passes so method bodies never
// reach them as phase-1 function definitions.
int rewrite_classes(Token *t, int n, Token **out_tokens) {
  _nclasses = 0;
  _nsym = 0;
  _nglob = 0;
  scan_classes(t, n);
  scan_globals(t, n);
  scan_fn_locals(t, n);

  Token *out = malloc(sizeof(Token) * (n * 2 + 4096));
  if (!out) { *out_tokens = t; return n; }
  int o = 0;

  for (int i = 0; i < n; i++) {
    Token cur = t[i];

    // strip a whole class block (do not emit any of its tokens)
    if (cur.type == TOKEN_IDENTIFIER && strcmp(cur.text, "class") == 0) {
      int close = -1;
      for (int c2 = _nclasses - 1; c2 >= 0; c2--) {
        if (strcmp(_classes[c2].name, t[i + 1].text) == 0 && _classes[c2].classOpen > i) {
          close = _classes[c2].classClose;
          break;
        }
      }
      if (close >= 0) { i = close; continue; }
      out[o++] = cur;
      continue;
    }

    // `new ClassName(args)` -> `ClassName_ctor(args)` (Object is a builtin
    // empty value class with an implicit ctor in processing.h)
    if (cur.type == TOKEN_IDENTIFIER && strcmp(cur.text, "new") == 0 &&
        i + 2 < n && t[i + 1].type == TOKEN_IDENTIFIER &&
        (is_class_name(t[i + 1].text) || is_builtin_ctor_class(t[i + 1].text)) &&
        t[i + 2].type == TOKEN_SYMBOL && strcmp(t[i + 2].text, "(") == 0) {
      Token alias = cur;
      snprintf(alias.text, MAX_TOKEN_TEXT, "%s_ctor", t[i + 1].text);
      out[o++] = alias;
      i += 1;
      continue;
    }

    // `new ArrayList([<Gen>])` -> `_pde_ag_new()`
    if (cur.type == TOKEN_IDENTIFIER && strcmp(cur.text, "new") == 0 &&
        i + 2 < n && t[i + 1].type == TOKEN_IDENTIFIER &&
        strcmp(t[i + 1].text, "ArrayList") == 0) {
      int j = i + 2;
      if (j < n && strcmp(t[j].text, "<") == 0)
        j = skip_angle_close(t, n, j) + 1;
      if (j < n && t[j].type == TOKEN_SYMBOL && strcmp(t[j].text, "(") == 0) {
        Token alias = cur;
        snprintf(alias.text, MAX_TOKEN_TEXT, "_pde_ag_new");
        out[o++] = alias;
        i = j - 1;
        continue;
      }
    }

    // `(Type) list.get(i)` -> `*(Type *) _pde_ag_get(list, i)`
    if (cur.type == TOKEN_SYMBOL && strcmp(cur.text, "(") == 0 &&
        i + 6 < n && t[i + 1].type == TOKEN_IDENTIFIER &&
        is_plausible_type_token(t[i + 1]) &&
        t[i + 2].type == TOKEN_SYMBOL && strcmp(t[i + 2].text, ")") == 0 &&
        t[i + 3].type == TOKEN_IDENTIFIER) {
      const char *xty = symbol_type(t[i + 3].text);
      if (xty && strcmp(xty, "ArrayList") == 0 &&
          t[i + 4].type == TOKEN_DOT &&
          t[i + 5].type == TOKEN_IDENTIFIER && strcmp(t[i + 5].text, "get") == 0 &&
          t[i + 6].type == TOKEN_SYMBOL && strcmp(t[i + 6].text, "(") == 0) {
        Token op = cur; strcpy(op.text, "*"); op.type = TOKEN_OPERATOR; out[o++] = op;
        Token lp = cur; strcpy(lp.text, "("); out[o++] = lp;
        Token ty = t[i + 1];
        snprintf(ty.text, MAX_TOKEN_TEXT, "%s *", emit_c_type_str(t[i + 1].text));
        out[o++] = ty;
        Token rp = cur; strcpy(rp.text, ")"); out[o++] = rp;
        Token fn = cur; snprintf(fn.text, MAX_TOKEN_TEXT, "_pde_ag_get"); out[o++] = fn;
        Token ol = cur; strcpy(ol.text, "("); out[o++] = ol;
        out[o++] = t[i + 3];
        Token cm = cur; strcpy(cm.text, ","); out[o++] = cm;
        i = i + 6; // resume at the argument after "get("
        continue;
      }
    }

    // Minim.CONSTANT -> CONSTANT
    if (cur.type == TOKEN_IDENTIFIER && strcmp(cur.text, "Minim") == 0 &&
        i + 1 < n && t[i + 1].type == TOKEN_DOT) {
      i += 1;
      continue;
    }

    // member-call routing on typed receivers: `a.meth(` or `a[i].meth(`
    int dotIdx = i + 1;
    bool isArrElem = false;
    while (dotIdx + 1 < n && t[dotIdx].type == TOKEN_SYMBOL && strcmp(t[dotIdx].text, "[") == 0) {
      int cl = matching_close(t, n, dotIdx, "[", "]");
      if (cl > dotIdx) { dotIdx = cl + 1; isArrElem = true; }
      else break;
    }
    if ((i == 0 || t[i - 1].type != TOKEN_DOT) &&
        cur.type == TOKEN_IDENTIFIER && dotIdx + 2 < n &&
        t[dotIdx].type == TOKEN_DOT &&
        (t[dotIdx + 1].type == TOKEN_IDENTIFIER || strcmp(t[dotIdx + 1].text, "draw") == 0 || strcmp(t[dotIdx + 1].text, "setup") == 0) &&
        t[dotIdx + 2].type == TOKEN_SYMBOL && strcmp(t[dotIdx + 2].text, "(") == 0) {
      const char *ty = symbol_type(cur.text);
      const char *mn = t[dotIdx + 1].text;
      bool noArg = (dotIdx + 3 < n && t[dotIdx + 3].type == TOKEN_SYMBOL &&
                    strcmp(t[dotIdx + 3].text, ")") == 0);
      char elemTy[MAX_TOKEN_TEXT];
      if (ty) {
        snprintf(elemTy, sizeof(elemTy), "%s", ty);
        char *star = strchr(elemTy, '*');
        if (star) { while (star > elemTy && *(star-1) == ' ') star--; *star = '\0'; }
      }
      const char *targetTy = isArrElem ? elemTy : ty;
      if (!isArrElem && strcmp(cur.text, "PVector") == 0) {
        Token fn = cur;
        snprintf(fn.text, MAX_TOKEN_TEXT, "pvector_%s", mn);
        out[o++] = fn;
        Token ol = cur; strcpy(ol.text, "("); out[o++] = ol;
        i = dotIdx + 2;
        continue;
      }
      if (!isArrElem && targetTy && strcmp(targetTy, "ArrayList") == 0 && is_ag_method(mn)) {
        Token fn = cur;
        snprintf(fn.text, MAX_TOKEN_TEXT, "_pde_ag_%s", mn);
        out[o++] = fn;
        Token ol = cur; strcpy(ol.text, "("); out[o++] = ol;
        out[o++] = cur; // receiver
        if (!noArg) { Token cm = cur; strcpy(cm.text, ","); out[o++] = cm; }
        i = dotIdx + 2;
        continue;
      }
      if (targetTy && is_class_name(targetTy) && class_has_method(targetTy, mn)) {
        Token fn = cur;
        snprintf(fn.text, MAX_TOKEN_TEXT, "%s_%s", targetTy, mn);
        out[o++] = fn;
        Token ol = cur; strcpy(ol.text, "("); out[o++] = ol;
        Token amp = cur; strcpy(amp.text, "&"); amp.type = TOKEN_OPERATOR; out[o++] = amp;
        for (int q = i; q < dotIdx; q++) out[o++] = t[q];
        if (!noArg) { Token cm = cur; strcpy(cm.text, ","); out[o++] = cm; }
        i = dotIdx + 2;
        continue;
      }
    }

    out[o++] = cur;
  }
  *out_tokens = out;
  return o;
}

// PVector methods that reassign the receiver: v.add(w) -> v = pvector_add(v, w)
bool pvector_mutator_method(const char *name) {
  static const char *mutators[] = { "add", "sub", "mult", "div",
                                    "normalize", "limit", "setMag", "rotate", "set" };
  for (int i = 0; i < 9; i++) {
    if (strcmp(name, mutators[i]) == 0) return true;
  }
  return false;
}

// PVector methods that only read: float m = v.mag() -> pvector_mag(v)
bool pvector_accessor_method(const char *name) {
  static const char *accessors[] = { "mag", "magSq", "dist", "dot", "cross",
                                     "heading", "lerp", "copy" };
  for (int i = 0; i < 8; i++) {
    if (strcmp(name, accessors[i]) == 0) return true;
  }
  return false;
}

// count top-level args of a call whose "(" is at tokens[open];
// writes the index of the matching ")" into *close_out
int count_call_args(Token *tokens, int num_tokens, int open, int *close_out) {
  int depth = 0, args = 0;
  bool seenOpen = false;
  for (int j = open; j < num_tokens; j++) {
    if (tokens[j].type != TOKEN_SYMBOL) {
      if (seenOpen && depth == 1 && args == 0) args = 1;
      continue;
    }
    const char *s = tokens[j].text;
    if (strcmp(s, "(") == 0 || strcmp(s, "[") == 0) {
      if (!seenOpen) { seenOpen = true; depth = 1; } else { depth++; }
      continue;
    }
    if (strcmp(s, ")") == 0 || strcmp(s, "]") == 0) {
      depth--;
      if (depth == 0 && seenOpen) { *close_out = j; return args; }
      continue;
    }
    if (strcmp(s, ",") == 0 && depth == 1 && seenOpen) args++;
  }
  *close_out = -1;
  return -1;
}

// ===========================================================================
// User-defined class emission (Phase 6)
// ===========================================================================
// Class method bodies are emitted from the token copies recorded during
// parse_class. Bare field identifiers become `self->field`, receiverless own
// method calls become `Class_method(self, ...)`, `this.x` becomes
// `self->x`, and PVector / ArrayList / user-class member calls on fields (or
// typed locals/globals) are routed to the same helpers the sketch-level loop
// uses. Constructors build a stack struct, run field initializers, then the
// body, and return it by value.

static const char *local_type_of(const ClassMethod *m, const char *name) {
  if (!m) return NULL;
  for (int i = 0; i < m->nlocals; i++)
    if (strcmp(m->locals[i], name) == 0) return m->local_types[i];
  return NULL;
}

static const char *field_type_of(const ClassDef *c, const char *name) {
  for (int i = 0; i < c->nfields; i++)
    if (strcmp(c->fields[i].name, name) == 0) return c->fields[i].type;
  return NULL;
}

static bool is_field_of(const ClassDef *c, const char *name) {
  return field_type_of(c, name) != NULL;
}

static bool is_class_method_name(const ClassDef *c, const char *name) {
  for (int i = 0; i < c->nmethods; i++)
    if (!c->methods[i].isCtor && strcmp(c->methods[i].name, name) == 0) return true;
  return false;
}

static void class_recv(const ClassDef *c, const char *subj, char *out, size_t sz) {
  if (is_field_of(c, subj)) snprintf(out, sz, "self->%s", subj);
  else snprintf(out, sz, "%s", subj);
}

// Trailing spacing after a printed raw token (mirrors the sketch-body loop:
// operator/comma get a space, ';' a newline, otherwise a space unless the
// next token is a dot or symbol).
static void class_sp(Token cur, bool hasNext, Token next) {
  if (cur.type == TOKEN_SYMBOL && strcmp(cur.text, ";") == 0) { printf("\n"); return; }
  if (cur.type == TOKEN_OPERATOR ||
      (cur.type == TOKEN_SYMBOL && strcmp(cur.text, ",") == 0)) { printf(" "); return; }
  if (hasNext && next.type != TOKEN_DOT && next.type != TOKEN_SYMBOL && cur.type != TOKEN_DOT)
    printf(" ");
}

// Trailing spacing after a SYNTHESIZED identifier (self->x, pvector, ...).
static void synth_sp(Token *t, int n, int nextIdx) {
  if (nextIdx < n && t[nextIdx].type != TOKEN_DOT && t[nextIdx].type != TOKEN_SYMBOL)
    printf(" ");
}

static void emit_class_body(const ClassDef *c, const ClassMethod *m, Token *t, int n) {
  for (int i = 0; i < n; i++) {
    Token tk = t[i];

    // `(Type[])` array cast -> `(Type *)`
    if (tk.type == TOKEN_SYMBOL && strcmp(tk.text, "(") == 0 &&
        i + 4 < n && is_plausible_type_token(t[i + 1]) &&
        t[i + 2].type == TOKEN_SYMBOL && strcmp(t[i + 2].text, "[") == 0 &&
        t[i + 3].type == TOKEN_SYMBOL && strcmp(t[i + 3].text, "]") == 0 &&
        t[i + 4].type == TOKEN_SYMBOL && strcmp(t[i + 4].text, ")") == 0) {
      const char *cty = emit_c_type_str(t[i + 1].text);
      if (strcmp(cty, "const char *") == 0)
        printf("(const char **)");
      else
        printf("(%s *)", cty);
      i += 4;
      continue;
    }

    // `(PGraphics3D) expr` -> `_pde_as_pgraphics3d(expr)`
    if (tk.type == TOKEN_SYMBOL && strcmp(tk.text, "(") == 0 &&
        i + 3 < n && strcmp(t[i + 1].text, "PGraphics3D") == 0 &&
        t[i + 2].type == TOKEN_SYMBOL && strcmp(t[i + 2].text, ")") == 0 &&
        t[i + 3].type == TOKEN_IDENTIFIER) {
      char recv[MAX_TOKEN_TEXT + 8];
      class_recv(c, t[i + 3].text, recv, sizeof(recv));
      printf("_pde_as_pgraphics3d(%s)", recv);
      i += 3;
      continue;
    }

    // `(PGraphics...)` struct cast stripping -> skip the cast parens & type
    if (tk.type == TOKEN_SYMBOL && strcmp(tk.text, "(") == 0 &&
        i + 2 < n &&
        (strcmp(t[i + 1].text, "PGraphics") == 0 ||
         strcmp(t[i + 1].text, "PGraphicsOpenGL") == 0 ||
         strcmp(t[i + 1].text, "PGraphicsPDF") == 0 ||
         strcmp(t[i + 1].text, "PGraphics2D") == 0) &&
        t[i + 2].type == TOKEN_SYMBOL && strcmp(t[i + 2].text, ")") == 0) {
      i += 2;
      continue;
    }

    // primitive type conversion cast: int(x), float(x), char(x), double(x) -> (int)(x) etc.
    if (tk.type == TOKEN_KEYWORD &&
        (strcmp(tk.text, "int") == 0 || strcmp(tk.text, "float") == 0 ||
         strcmp(tk.text, "char") == 0 || strcmp(tk.text, "double") == 0) &&
        i + 1 < n && t[i + 1].type == TOKEN_SYMBOL && strcmp(t[i + 1].text, "(") == 0) {
      printf("(%s)", emit_c_type_str(tk.text));
      continue;
    }

    // boolean mapping
    if (tk.type == TOKEN_KEYWORD && strcmp(tk.text, "boolean") == 0) {
      if (i + 1 < n && t[i + 1].type == TOKEN_SYMBOL && strcmp(t[i + 1].text, "(") == 0)
        printf("boolean");
      else
        printf("bool ");
      continue;
    }

    // color mapping
    if (tk.type == TOKEN_KEYWORD && strcmp(tk.text, "color") == 0) {
      if (!(i + 1 < n && t[i + 1].type == TOKEN_SYMBOL && strcmp(t[i + 1].text, "(") == 0)) {
        printf("uint32_t ");
        continue;
      }
    }

    // String type mapping inside methods
    if (tk.type == TOKEN_IDENTIFIER && strcmp(tk.text, "String") == 0) {
      printf("const char *");
      continue;
    }

    // Java null literal -> NULL
    if (tk.type == TOKEN_IDENTIFIER && strcmp(tk.text, "null") == 0) {
      printf("NULL");
      continue;
    }

    // super.method(...) -> ((void)0)
    if (tk.type == TOKEN_IDENTIFIER && strcmp(tk.text, "super") == 0 &&
        i + 3 < n && t[i + 1].type == TOKEN_DOT &&
        t[i + 2].type == TOKEN_IDENTIFIER &&
        t[i + 3].type == TOKEN_SYMBOL && strcmp(t[i + 3].text, "(") == 0) {
      int close = matching_close(t, n, i + 3, "(", ")");
      if (close > i + 3) {
        printf("((void)0)");
        i = close;
        continue;
      }
    }

    // color(...) -> pack_color(...)
    if (strcmp(tk.text, "color") == 0 && i + 1 < n &&
        t[i + 1].type == TOKEN_SYMBOL && strcmp(t[i + 1].text, "(") == 0) {
      printf("pack_color");
      continue;
    }

    // this.x -> self->x  (also bare this -> self)
    if (tk.type == TOKEN_IDENTIFIER && strcmp(tk.text, "this") == 0) {
      if (i + 2 < n && t[i + 1].type == TOKEN_DOT && t[i + 2].type == TOKEN_IDENTIFIER) {
        printf("self->%s", t[i + 2].text);
        i += 2;
        continue;
      } else {
        printf("self");
        continue;
      }
    }

    // `new` forms
    if (tk.type == TOKEN_IDENTIFIER && strcmp(tk.text, "new") == 0) {
      if (i + 2 < n && t[i + 1].type == TOKEN_IDENTIFIER &&
          strcmp(t[i + 1].text, "PVector") == 0 &&
          t[i + 2].type == TOKEN_SYMBOL && strcmp(t[i + 2].text, "(") == 0) {
        printf("pvector");
        synth_sp(t, n, i + 2);
        i += 1;
        continue;
      }
      if (i + 2 < n && t[i + 1].type == TOKEN_IDENTIFIER &&
          (is_class_name(t[i + 1].text) || is_builtin_ctor_class(t[i + 1].text)) &&
          t[i + 2].type == TOKEN_SYMBOL && strcmp(t[i + 2].text, "(") == 0) {
        printf("%s_ctor", t[i + 1].text);
        synth_sp(t, n, i + 2);
        i += 1;
        continue;
      }
      if (i + 1 < n && t[i + 1].type == TOKEN_IDENTIFIER &&
          strcmp(t[i + 1].text, "ArrayList") == 0) {
        int lp = i + 2;
        if (lp < n && strcmp(t[lp].text, "<") == 0)
          lp = skip_angle_close(t, n, lp) + 1;
        if (lp < n && t[lp].type == TOKEN_SYMBOL && strcmp(t[lp].text, "(") == 0) {
          printf("_pde_ag_new");
          synth_sp(t, n, lp);
          i = lp - 1;
          continue;
        }
      }
      // new TYPE[d1]... -> (TYPE *)_pde_array*_new(d1, ..., sizeof(TYPE))
      if (i + 3 < n &&
          (t[i + 1].type == TOKEN_IDENTIFIER || t[i + 1].type == TOKEN_KEYWORD) &&
          t[i + 2].type == TOKEN_SYMBOL && strcmp(t[i + 2].text, "[") == 0 &&
          !(t[i + 3].type == TOKEN_SYMBOL && strcmp(t[i + 3].text, "]") == 0)) {
        const char *elemType = emit_c_type_str(t[i + 1].text);
        int dimStarts[4], dimEnds[4];
        int nDims = 0;
        int cur = i + 2;
        while (nDims < 4 && cur < n &&
               t[cur].type == TOKEN_SYMBOL && strcmp(t[cur].text, "[") == 0) {
          int depth = 1, s = cur + 1, e = s;
          while (e < n && depth > 0) {
            if (t[e].type == TOKEN_SYMBOL && strcmp(t[e].text, "[") == 0) depth++;
            else if (t[e].type == TOKEN_SYMBOL && strcmp(t[e].text, "]") == 0) depth--;
            if (depth == 0) break;
            e++;
          }
          if (depth == 0) {
            dimStarts[nDims] = s;
            dimEnds[nDims] = e;
            nDims++;
            cur = e + 1;
          } else break;
        }
        if (nDims > 0) {
          if (nDims == 1)      printf("(%s *)_pde_array_new(", elemType);
          else if (nDims == 2) printf("(%s **)_pde_array2d_new(", elemType);
          else if (nDims == 3) printf("(%s ***)_pde_array3d_new(", elemType);
          else                 printf("(%s *)_pde_array_new(", elemType);
          for (int d = 0; d < nDims; d++) {
            if (d > 0) printf(", ");
            for (int q = dimStarts[d]; q < dimEnds[d]; q++) {
              if (t[q].type == TOKEN_IDENTIFIER &&
                  q + 2 < dimEnds[d] &&
                  t[q + 1].type == TOKEN_DOT &&
                  t[q + 2].type == TOKEN_IDENTIFIER &&
                  strcmp(t[q + 2].text, "length") == 0) {
                char recv[MAX_TOKEN_TEXT + 8];
                class_recv(c, t[q].text, recv, sizeof(recv));
                printf("_pde_len(%s)", recv);
                q += 2;
              } else if (t[q].type == TOKEN_IDENTIFIER && is_field_of(c, t[q].text)) {
                char recv[MAX_TOKEN_TEXT + 8];
                class_recv(c, t[q].text, recv, sizeof(recv));
                printf("%s", recv);
              } else {
                printf("%s", t[q].text);
              }
              if (q + 1 < dimEnds[d]) printf(" ");
            }
          }
          printf(", sizeof(%s))", elemType);
          i = dimEnds[nDims - 1];
          continue;
        }
      }
    }

    // `(Type) name.get(i)` ArrayList cast-deref
    if (tk.type == TOKEN_SYMBOL && strcmp(tk.text, "(") == 0 &&
        i + 6 < n && t[i + 1].type == TOKEN_IDENTIFIER &&
        is_plausible_type_token(t[i + 1]) &&
        t[i + 2].type == TOKEN_SYMBOL && strcmp(t[i + 2].text, ")") == 0 &&
        t[i + 3].type == TOKEN_IDENTIFIER && t[i + 4].type == TOKEN_DOT &&
        t[i + 5].type == TOKEN_IDENTIFIER && strcmp(t[i + 5].text, "get") == 0 &&
        t[i + 6].type == TOKEN_SYMBOL && strcmp(t[i + 6].text, "(") == 0) {
      const char *subj = t[i + 3].text;
      const char *ft = local_type_of(m, subj);
      if (!ft) ft = is_field_of(c, subj) ? field_type_of(c, subj) : symbol_type(subj);
      if (ft && strcmp(ft, "ArrayList") == 0) {
        char recv[MAX_TOKEN_TEXT + 8];
        class_recv(c, subj, recv, sizeof(recv));
        printf("*(%s *) _pde_ag_get(%s, ", emit_c_type_str(t[i + 1].text), recv);
        i += 6;
        continue;
      }
    }

    // Minim.CONSTANT -> CONSTANT
    if (tk.type == TOKEN_IDENTIFIER && strcmp(tk.text, "Minim") == 0 &&
        i + 1 < n && t[i + 1].type == TOKEN_DOT) {
      i += 1;
      continue;
    }

    // EXPR == null / EXPR != null check for user classes
    int eqIdx = i + 1;
    bool isArrNull = false;
    while (eqIdx + 1 < n && t[eqIdx].type == TOKEN_SYMBOL && strcmp(t[eqIdx].text, "[") == 0) {
      int cl = matching_close(t, n, eqIdx, "[", "]");
      if (cl > eqIdx) { eqIdx = cl + 1; isArrNull = true; }
      else break;
    }
    if ((i == 0 || t[i - 1].type != TOKEN_DOT) &&
        tk.type == TOKEN_IDENTIFIER && eqIdx + 1 < n &&
        (strcmp(t[eqIdx].text, "==") == 0 || strcmp(t[eqIdx].text, "!=") == 0) &&
        (strcmp(t[eqIdx + 1].text, "null") == 0 || strcmp(t[eqIdx + 1].text, "NULL") == 0)) {
      bool isNeq = (strcmp(t[eqIdx].text, "!=") == 0);
      const char *subj = tk.text;
      const char *ft = local_type_of(m, subj);
      if (!ft) ft = is_field_of(c, subj) ? field_type_of(c, subj) : symbol_type(subj);
      char elemTy[MAX_TOKEN_TEXT];
      if (ft) {
        snprintf(elemTy, sizeof(elemTy), "%s", ft);
        char *star = strchr(elemTy, '*');
        if (star) { while (star > elemTy && *(star-1) == ' ') star--; *star = '\0'; }
      }
      const char *targetTy = isArrNull ? elemTy : ft;
      if (targetTy && is_class_name(targetTy) && targetTy[strlen(targetTy)-1] != '*') {
        char recv[MAX_TOKEN_TEXT + 8];
        class_recv(c, subj, recv, sizeof(recv));
        if (isArrNull) {
          char fullRecv[MAX_TOKEN_TEXT * 2];
          snprintf(fullRecv, sizeof(fullRecv), "%s", recv);
          int rlen = (int)strlen(fullRecv);
          for (int q = i + 1; q < eqIdx && rlen + 32 < (int)sizeof(fullRecv); q++) {
            rlen += snprintf(fullRecv + rlen, sizeof(fullRecv) - rlen, "%s", t[q].text);
          }
          printf("%s_pde_is_null(%s)", isNeq ? "!" : "", fullRecv);
        } else {
          printf("%s_pde_is_null(%s)", isNeq ? "!" : "", recv);
        }
        i = eqIdx + 1;
        continue;
      }
    }

    // Assignment of null to a user class: vecOne = null -> vecOne = (Vector){0}
    if ((i == 0 || t[i - 1].type != TOKEN_DOT) &&
        tk.type == TOKEN_IDENTIFIER && i + 2 < n &&
        t[i + 1].type == TOKEN_OPERATOR && strcmp(t[i + 1].text, "=") == 0 &&
        (strcmp(t[i + 2].text, "null") == 0 || strcmp(t[i + 2].text, "NULL") == 0)) {
      const char *subj = tk.text;
      const char *ft = local_type_of(m, subj);
      if (!ft) ft = is_field_of(c, subj) ? field_type_of(c, subj) : symbol_type(subj);
      if (ft && is_class_name(ft) && ft[strlen(ft)-1] != '*') {
        char recv[MAX_TOKEN_TEXT + 8];
        class_recv(c, subj, recv, sizeof(recv));
        printf("%s = (%s){0}", recv, ft);
        i += 2;
        continue;
      }
    }

    // member call on a PVector / user-class / ArrayList receiver
    int dotIdx = i + 1;
    bool isArrElem = false;
    while (dotIdx + 1 < n && t[dotIdx].type == TOKEN_SYMBOL && strcmp(t[dotIdx].text, "[") == 0) {
      int cl = matching_close(t, n, dotIdx, "[", "]");
      if (cl > dotIdx) { dotIdx = cl + 1; isArrElem = true; }
      else break;
    }
    if ((i == 0 || t[i - 1].type != TOKEN_DOT) &&
        tk.type == TOKEN_IDENTIFIER && dotIdx + 2 < n &&
        t[dotIdx].type == TOKEN_DOT &&
        (t[dotIdx + 1].type == TOKEN_IDENTIFIER || strcmp(t[dotIdx + 1].text, "draw") == 0 || strcmp(t[dotIdx + 1].text, "setup") == 0) &&
        t[dotIdx + 2].type == TOKEN_SYMBOL && strcmp(t[dotIdx + 2].text, "(") == 0) {
      const char *subj = tk.text;
      const char *meth = t[dotIdx + 1].text;
      if (!isArrElem && strcmp(subj, "PVector") == 0) {
        printf("pvector_%s(", meth);
        i = dotIdx + 2;
        continue;
      }
      bool zeroArg = (dotIdx + 3 < n && t[dotIdx + 3].type == TOKEN_SYMBOL &&
                      strcmp(t[dotIdx + 3].text, ")") == 0);
      const char *ft = local_type_of(m, subj);
      if (!ft) ft = is_field_of(c, subj) ? field_type_of(c, subj) : symbol_type(subj);
      char elemTy[MAX_TOKEN_TEXT];
      if (ft) {
        snprintf(elemTy, sizeof(elemTy), "%s", ft);
        char *star = strchr(elemTy, '*');
        if (star) { while (star > elemTy && *(star-1) == ' ') star--; *star = '\0'; }
      }
      const char *targetTy = isArrElem ? elemTy : ft;
      if (!isArrElem && ft && strcmp(ft, "PVector") == 0 &&
          (pvector_mutator_method(meth) || pvector_accessor_method(meth))) {
        char recv[MAX_TOKEN_TEXT + 8];
        class_recv(c, subj, recv, sizeof(recv));
        if (pvector_mutator_method(meth))
          printf("%s = pvector_%s(%s%s", recv, meth, recv, zeroArg ? "" : ", ");
        else
          printf("pvector_%s(%s%s", meth, recv, zeroArg ? "" : ", ");
        i = dotIdx + 2;
        continue;
      }
      if (!isArrElem && ft && strcmp(ft, "ArrayList") == 0 && is_ag_method(meth)) {
        char recv[MAX_TOKEN_TEXT + 8];
        class_recv(c, subj, recv, sizeof(recv));
        printf("_pde_ag_%s(%s%s", meth, recv, zeroArg ? "" : ", ");
        i = dotIdx + 2;
        continue;
      }
      if (targetTy && is_class_name(targetTy) && class_has_method(targetTy, meth)) {
        char recv[MAX_TOKEN_TEXT + 8];
        class_recv(c, subj, recv, sizeof(recv));
        // Check if single argument is null/NULL and param type is a user class struct
        const ClassMethod *targetMeth = NULL;
        for (int ci = 0; ci < _nclasses; ci++) {
          if (strcmp(_classes[ci].name, targetTy) == 0) {
            for (int mi = 0; mi < _classes[ci].nmethods; mi++) {
              if (!_classes[ci].methods[mi].isCtor && strcmp(_classes[ci].methods[mi].name, meth) == 0) {
                targetMeth = &_classes[ci].methods[mi];
                break;
              }
            }
            break;
          }
        }
        if (targetMeth && targetMeth->nparams == 1 &&
            dotIdx + 4 < n && (strcmp(t[dotIdx + 3].text, "null") == 0 || strcmp(t[dotIdx + 3].text, "NULL") == 0) &&
            t[dotIdx + 4].type == TOKEN_SYMBOL && strcmp(t[dotIdx + 4].text, ")") == 0) {
          const char *p0 = targetMeth->local_types[0];
          if (p0 && is_class_name(p0) && p0[strlen(p0)-1] != '*') {
            if (isArrElem) {
              char fullRecv[MAX_TOKEN_TEXT * 2];
              snprintf(fullRecv, sizeof(fullRecv), "%s", recv);
              int rlen = (int)strlen(fullRecv);
              for (int q = i + 1; q < dotIdx && rlen + 32 < (int)sizeof(fullRecv); q++) {
                rlen += snprintf(fullRecv + rlen, sizeof(fullRecv) - rlen, "%s", t[q].text);
              }
              printf("%s_%s(&%s, (%s){0})", targetTy, meth, fullRecv, p0);
            } else {
              bool isPtr = (strcmp(recv, "self") == 0 || (targetTy && targetTy[strlen(targetTy)-1] == '*'));
              printf("%s_%s(%s%s, (%s){0})", targetTy, meth, isPtr ? "" : "&", recv, p0);
            }
            i = dotIdx + 4;
            continue;
          }
        }
        if (isArrElem) {
          char fullRecv[MAX_TOKEN_TEXT * 2];
          snprintf(fullRecv, sizeof(fullRecv), "%s", recv);
          int rlen = (int)strlen(fullRecv);
          for (int q = i + 1; q < dotIdx && rlen + 32 < (int)sizeof(fullRecv); q++) {
            rlen += snprintf(fullRecv + rlen, sizeof(fullRecv) - rlen, "%s", t[q].text);
          }
          printf("%s_%s(&%s%s", targetTy, meth, fullRecv, zeroArg ? "" : ", ");
        } else {
          bool isPtr = (strcmp(recv, "self") == 0 || (targetTy && targetTy[strlen(targetTy)-1] == '*'));
          printf("%s_%s(%s%s%s", targetTy, meth, isPtr ? "" : "&", recv, zeroArg ? "" : ", ");
        }
        i = dotIdx + 2;
        continue;
      }
    }

    // `name . length` (no parens) -> _pde_len(recv)
    if (tk.type == TOKEN_IDENTIFIER && i + 2 < n &&
        t[i + 1].type == TOKEN_DOT && t[i + 2].type == TOKEN_IDENTIFIER &&
        strcmp(t[i + 2].text, "length") == 0 &&
        !(i + 3 < n && t[i + 3].type == TOKEN_SYMBOL && strcmp(t[i + 3].text, "(") == 0)) {
      char recv[MAX_TOKEN_TEXT + 8];
      class_recv(c, tk.text, recv, sizeof(recv));
      printf("_pde_len(%s)", recv);
      i += 2;
      continue;
    }

    // receiverless call to an own method: meth(args) -> Class_meth(self, args)
    if (tk.type == TOKEN_IDENTIFIER && i + 1 < n &&
        (i == 0 || t[i - 1].type != TOKEN_DOT) &&
        t[i + 1].type == TOKEN_SYMBOL && strcmp(t[i + 1].text, "(") == 0 &&
        is_class_method_name(c, tk.text)) {
      bool zeroArg = (i + 2 < n && t[i + 2].type == TOKEN_SYMBOL &&
                      strcmp(t[i + 2].text, ")") == 0);
      printf("%s_%s(self%s", c->name, tk.text, zeroArg ? "" : ", ");
      i += 1;
      continue;
    }

    // bare field use -> self->field (unless shadowed by a local/param/global
    // or it is actually a call to a same-named method, handled above)
    if ((i == 0 || t[i - 1].type != TOKEN_DOT) && tk.type == TOKEN_IDENTIFIER && is_field_of(c, tk.text)) {
      bool shadowed = false;
      if (m) for (int k = 0; k < m->nlocals; k++)
        if (strcmp(m->locals[k], tk.text) == 0) { shadowed = true; break; }
      if (!shadowed && !is_global_name(tk.text)) {
        bool callAhead = (i + 1 < n && t[i + 1].type == TOKEN_SYMBOL &&
                          strcmp(t[i + 1].text, "(") == 0);
        if (!callAhead) {
          printf("self->%s", tk.text);
          synth_sp(t, n, i + 1);
          continue;
        }
      }
    }

    printf("%s", tk.text);
    class_sp(tk, i + 1 < n, i + 1 < n ? t[i + 1] : tk);
  }
}

static void get_ctor_name(const ClassDef *c, int mi, char *buf, size_t sz) {
  int nctors = 0;
  for (int j = 0; j < c->nmethods; j++) {
    if (c->methods[j].isCtor) nctors++;
  }
  if (nctors <= 1) {
    snprintf(buf, sz, "%s_ctor", c->name);
    return;
  }
  const ClassMethod *mm = &c->methods[mi];
  int dupCount = 0;
  for (int j = 0; j < mi; j++) {
    if (c->methods[j].isCtor && c->methods[j].nparams == mm->nparams) dupCount++;
  }
  int totalWithArity = 0;
  for (int j = 0; j < c->nmethods; j++) {
    if (c->methods[j].isCtor && c->methods[j].nparams == mm->nparams) totalWithArity++;
  }
  if (dupCount > 0) {
    snprintf(buf, sz, "%s_ctor_%d_dup%d", c->name, mm->nparams, dupCount);
  } else if (totalWithArity > 1) {
    snprintf(buf, sz, "%s_ctor_%d_base", c->name, mm->nparams);
  } else {
    snprintf(buf, sz, "%s_ctor_%d", c->name, mm->nparams);
  }
}

static void get_method_name(const ClassDef *c, int mi, char *buf, size_t sz) {
  const ClassMethod *mm = &c->methods[mi];
  int count = 0;
  for (int j = 0; j < c->nmethods; j++) {
    if (!c->methods[j].isCtor && strcmp(c->methods[j].name, mm->name) == 0) count++;
  }
  if (count <= 1) {
    snprintf(buf, sz, "%s_%s", c->name, mm->name);
    return;
  }
  int dupCount = 0;
  for (int j = 0; j < mi; j++) {
    if (!c->methods[j].isCtor && strcmp(c->methods[j].name, mm->name) == 0 &&
        c->methods[j].nparams == mm->nparams) dupCount++;
  }
  int totalWithArity = 0;
  for (int j = 0; j < c->nmethods; j++) {
    if (!c->methods[j].isCtor && strcmp(c->methods[j].name, mm->name) == 0 &&
        c->methods[j].nparams == mm->nparams) totalWithArity++;
  }
  if (dupCount > 0) {
    snprintf(buf, sz, "%s_%s_%d_dup%d", c->name, mm->name, mm->nparams, dupCount);
  } else if (totalWithArity > 1) {
    snprintf(buf, sz, "%s_%s_%d_base", c->name, mm->name, mm->nparams);
  } else {
    snprintf(buf, sz, "%s_%s_%d", c->name, mm->name, mm->nparams);
  }
}

static void emit_method_dispatch_macro(const ClassDef *c, const char *methName, int arity) {
  int indices[16];
  int n = 0;
  for (int mi = 0; mi < c->nmethods; mi++) {
    if (!c->methods[mi].isCtor && strcmp(c->methods[mi].name, methName) == 0 &&
        c->methods[mi].nparams == arity && n < 16) {
      indices[n++] = mi;
    }
  }
  if (n <= 1) return;

  char params[128] = "self";
  size_t plen = 4;
  for (int a = 0; a < arity; a++) {
    plen += snprintf(params + plen, sizeof(params) - plen, ", _%d", a + 1);
  }

  int defIdx = -1;
  for (int i = 0; i < n; i++) {
    const char *t0 = c->methods[indices[i]].local_types[0];
    if (!t0 || strcmp(t0, "float") == 0 || strcmp(t0, "int") == 0 || strcmp(t0, "double") == 0) {
      defIdx = indices[i];
      break;
    }
  }
  if (defIdx < 0) defIdx = indices[0];

  char defName[MAX_TOKEN_TEXT];
  get_method_name(c, defIdx, defName, sizeof(defName));

  char types[16][MAX_TOKEN_TEXT];
  int ntypes = 0;
  for (int i = 0; i < n; i++) {
    if (indices[i] == defIdx) continue;
    const char *t0 = c->methods[indices[i]].local_types[0];
    if (!t0) continue;
    bool found = false;
    for (int j = 0; j < ntypes; j++) {
      if (strcmp(types[j], t0) == 0) { found = true; break; }
    }
    if (!found && ntypes < 16) {
      snprintf(types[ntypes++], MAX_TOKEN_TEXT, "%s", t0);
    }
  }

  printf("#define %s_%s_%d(%s) ", c->name, methName, arity, params);
  if (ntypes == 0) {
    printf("%s(%s)\n", defName, params);
    return;
  }

  printf("_Generic((_1), ");
  for (int t = 0; t < ntypes; t++) {
    int sub[16];
    int nsub = 0;
    for (int i = 0; i < n; i++) {
      if (indices[i] == defIdx) continue;
      const char *t0 = c->methods[indices[i]].local_types[0];
      if (t0 && strcmp(t0, types[t]) == 0) sub[nsub++] = indices[i];
    }
    printf("%s: ", types[t]);
    if (nsub == 1 || arity < 2) {
      char mname[MAX_TOKEN_TEXT];
      get_method_name(c, sub[0], mname, sizeof(mname));
      printf("%s, ", mname);
    } else {
      char subDef[MAX_TOKEN_TEXT];
      get_method_name(c, sub[0], subDef, sizeof(subDef));
      printf("_Generic((_2), ");
      for (int s = 1; s < nsub; s++) {
        const char *t1 = c->methods[sub[s]].local_types[1];
        char sname[MAX_TOKEN_TEXT];
        get_method_name(c, sub[s], sname, sizeof(sname));
        if (t1) printf("%s: %s, ", t1, sname);
      }
      printf("default: %s), ", subDef);
    }
  }
  printf("default: %s)(%s)\n", defName, params);
}

static void emit_ctor_dispatch_macro(const ClassDef *c, int arity) {
  int indices[16];
  int n = 0;
  for (int mi = 0; mi < c->nmethods; mi++) {
    if (c->methods[mi].isCtor && c->methods[mi].nparams == arity && n < 16) {
      indices[n++] = mi;
    }
  }
  if (n <= 1) return;

  char params[128] = "";
  size_t plen = 0;
  for (int a = 0; a < arity; a++) {
    plen += snprintf(params + plen, sizeof(params) - plen, "%s_%d", a ? ", " : "", a + 1);
  }

  int defIdx = -1;
  for (int i = 0; i < n; i++) {
    const char *t0 = c->methods[indices[i]].local_types[0];
    if (!t0 || strcmp(t0, "float") == 0 || strcmp(t0, "int") == 0 || strcmp(t0, "double") == 0) {
      defIdx = indices[i];
      break;
    }
  }
  if (defIdx < 0) defIdx = indices[0];

  char defName[MAX_TOKEN_TEXT];
  get_ctor_name(c, defIdx, defName, sizeof(defName));

  char types[16][MAX_TOKEN_TEXT];
  int ntypes = 0;
  for (int i = 0; i < n; i++) {
    if (indices[i] == defIdx) continue;
    const char *t0 = c->methods[indices[i]].local_types[0];
    if (!t0) continue;
    bool found = false;
    for (int j = 0; j < ntypes; j++) {
      if (strcmp(types[j], t0) == 0) { found = true; break; }
    }
    if (!found && ntypes < 16) {
      snprintf(types[ntypes++], MAX_TOKEN_TEXT, "%s", t0);
    }
  }

  printf("#define %s_ctor_%d(%s) ", c->name, arity, params);
  if (ntypes == 0) {
    printf("%s(%s)\n", defName, params);
    return;
  }

  printf("_Generic((_1), ");
  for (int t = 0; t < ntypes; t++) {
    int sub[16];
    int nsub = 0;
    for (int i = 0; i < n; i++) {
      if (indices[i] == defIdx) continue;
      const char *t0 = c->methods[indices[i]].local_types[0];
      if (t0 && strcmp(t0, types[t]) == 0) sub[nsub++] = indices[i];
    }
    printf("%s: ", types[t]);
    if (nsub == 1 || arity < 2) {
      char cname[MAX_TOKEN_TEXT];
      get_ctor_name(c, sub[0], cname, sizeof(cname));
      printf("%s, ", cname);
    } else {
      char subDef[MAX_TOKEN_TEXT];
      get_ctor_name(c, sub[0], subDef, sizeof(subDef));
      printf("_Generic((_2), ");
      for (int s = 1; s < nsub; s++) {
        const char *t1 = c->methods[sub[s]].local_types[1];
        char sname[MAX_TOKEN_TEXT];
        get_ctor_name(c, sub[s], sname, sizeof(sname));
        if (t1) printf("%s: %s, ", t1, sname);
      }
      printf("default: %s), ", subDef);
    }
  }
  printf("default: %s)(%s)\n", defName, params);
}

static void emit_class_typedefs_and_protos(void) {
  for (int i = 0; i < _nclasses; i++) {
    printf("typedef struct _Pde_%s %s;\n", _classes[i].name, _classes[i].name);
  }
  printf("\n");
  for (int i = 0; i < _nclasses; i++) {
    const ClassDef *c = &_classes[i];
    printf("struct _Pde_%s {\n", c->name);
    if (c->nfields == 0) {
      printf("  char _pde_unused;\n");
    } else {
      for (int f = 0; f < c->nfields; f++) {
        const ClassField *fl = &c->fields[f];
        printf("  %s%s %s;\n", fl->type, fl->isPointer ? " *" : "", fl->name);
      }
    }
    printf("};\n\n");
    int nctors = 0;
    for (int mi = 0; mi < c->nmethods; mi++) {
      const ClassMethod *mm = &c->methods[mi];
      if (mm->isCtor) {
        nctors++;
        char ctorName[MAX_TOKEN_TEXT];
        get_ctor_name(c, mi, ctorName, sizeof(ctorName));
        printf("%s %s(%s);\n", c->name, ctorName,
               mm->params[0] ? mm->params : "void");
      } else {
        char methName[MAX_TOKEN_TEXT];
        get_method_name(c, mi, methName, sizeof(methName));
        printf("%s %s(%s *self%s%s);\n", mm->ret, methName, c->name,
               mm->params[0] ? ", " : "", mm->params[0] ? mm->params : "");
      }
    }
    // Emit ctor dispatcher macros for overloaded ctors with identical arities
    bool arityEmitted[32] = {false};
    for (int mi = 0; mi < c->nmethods; mi++) {
      if (!c->methods[mi].isCtor) continue;
      int a = c->methods[mi].nparams;
      if (a < 32 && !arityEmitted[a]) {
        arityEmitted[a] = true;
        emit_ctor_dispatch_macro(c, a);
      }
    }
    if (nctors > 1) {
      printf("#define %s_ctor(...) _PDE_CONCAT(%s_ctor_, _PDE_NARGS(__VA_ARGS__))(__VA_ARGS__)\n", c->name, c->name);
    }
    // Emit method dispatcher macros for overloaded methods
    for (int mi = 0; mi < c->nmethods; mi++) {
      const ClassMethod *mm = &c->methods[mi];
      if (mm->isCtor) continue;
      bool first = true;
      for (int j = 0; j < mi; j++) {
        if (!c->methods[j].isCtor && strcmp(c->methods[j].name, mm->name) == 0) {
          first = false;
          break;
        }
      }
      if (!first) continue;
      int count = 0;
      for (int j = 0; j < c->nmethods; j++) {
        if (!c->methods[j].isCtor && strcmp(c->methods[j].name, mm->name) == 0) count++;
      }
      if (count > 1) {
        bool arityDone[32] = {false};
        for (int j = 0; j < c->nmethods; j++) {
          if (!c->methods[j].isCtor && strcmp(c->methods[j].name, mm->name) == 0) {
            int a = c->methods[j].nparams;
            if (a < 32 && !arityDone[a]) {
              arityDone[a] = true;
              emit_method_dispatch_macro(c, mm->name, a);
            }
          }
        }
        printf("#define %s_%s(self, ...) _PDE_CONCAT(%s_%s_, _PDE_NARGS(__VA_ARGS__))(self, ##__VA_ARGS__)\n",
               c->name, mm->name, c->name, mm->name);
      }
    }
    printf("\n");
  }
}

static void emit_class_definitions(void) {
  for (int i = 0; i < _nclasses; i++) {
    const ClassDef *c = &_classes[i];
    for (int mi = 0; mi < c->nmethods; mi++) {
      const ClassMethod *mm = &c->methods[mi];
      if (mm->isCtor) {
        char ctorName[MAX_TOKEN_TEXT];
        get_ctor_name(c, mi, ctorName, sizeof(ctorName));
        printf("\n%s %s(%s) {\n", c->name, ctorName,
               mm->params[0] ? mm->params : "void");
        printf("  %s __self = {0};\n", c->name);
        printf("  %s *self = &__self;\n", c->name);
        for (int f = 0; f < c->nfields; f++) {
          const ClassField *fl = &c->fields[f];
          if (!fl->hasInit || !fl->ninit) continue;
          printf("  self->%s = ", fl->name);
          emit_class_body(c, mm, fl->init, fl->ninit);
          printf(";\n");
        }
        if (mm->nbody > 0) emit_class_body(c, mm, mm->body, mm->nbody);
        printf("  return __self;\n}\n\n");
      } else {
        char methName[MAX_TOKEN_TEXT];
        get_method_name(c, mi, methName, sizeof(methName));
        printf("\n%s %s(%s *self%s%s) {\n", mm->ret, methName, c->name,
               mm->params[0] ? ", " : "", mm->params[0] ? mm->params : "");
        if (mm->nbody > 0) emit_class_body(c, mm, mm->body, mm->nbody);
        printf("}\n\n");
      }
    }
  }
}


// Growable token buffer: sketches from the wild easily exceed any fixed
// limit (a 3k-line multi-tab sketch overflows 10k tokens and segfaults).
// Every loop iteration adds at most one token, so checking for two free
// slots up front keeps the EOF sentinel safe as well.
#define TOKEN_GROW()                                                          \
  if ((size_t)(t_count + 2) > capacity) {                                     \
    capacity *= 2;                                                            \
    tokens = realloc(tokens, sizeof(Token) * capacity);                       \
    if (!tokens) { fprintf(stderr, "pde2c: out of memory\n"); exit(1); }       \
  }

int tokenize(const char *source, Token **tokens_out, size_t capacity_in) {
  int t_count = 0;
  int i = 0;
  int current_line = 1;
  int current_file = pdeRegisterFile("sketch.pde");
  size_t capacity = capacity_in ? capacity_in : 1 << 14;
  Token *tokens = *tokens_out;

  while (source[i] != '\0') {
    TOKEN_GROW();
    if (source[i] == '\n') {
      current_line++;
      i++;
      continue;
    }
    if (isspace(source[i])) {
      i++;
      continue;
    }

    // comments
    if (source[i] == '/' && source[i+1] == '/') {
      // file marker injected by the run wrapper: //@file name.pde
      if (strncmp(&source[i+2], "@file ", 6) == 0) {
        const char *p = &source[i+8];
        while (*p == ' ') p++;
        char name[MAX_TOKEN_TEXT];
        int n = 0;
        while (*p != '\0' && *p != '\n' && *p != '\r' && n < MAX_TOKEN_TEXT-1)
          name[n++] = *p++;
        name[n] = '\0';
        current_file = pdeRegisterFile(name);
        current_line = 0; // ++ on next newline -> 1
        while (source[i] != '\n' && source[i] != '\0') i++;
        continue;
      }
      while (source[i] != '\n' && source[i] != '\0') i++;
      continue;
    }

    // block comments
    if (source[i] == '/' && source[i+1] == '*') {
      i += 2;
      while (source[i] != '\0' && !(source[i] == '*' && source[i+1] == '/')) {
        if (source[i] == '\n') current_line++;
        i++;
      }
      if (source[i] != '\0') i += 2;
      continue;
    }

    // hex color literals: #RRGGBB -> packed ABGR uint32 (opaque)
    if (source[i] == '#' &&
        (i + 6 < (int)strlen(source))) {
      char hex[7];
      strncpy(hex, &source[i+1], 6);
      hex[6] = '\0';
      unsigned int rgb;
      if (sscanf(hex, "%x", &rgb) == 1 && strlen(hex) == 6) {
        unsigned int r = (rgb >> 16) & 0xFF;
        unsigned int g = (rgb >> 8) & 0xFF;
        unsigned int b = rgb & 0xFF;
        tokens[t_count].type = TOKEN_NUMBER;
        // opaque ABGR (raylib-native layout, matches pack_color / red()/green()/blue())
        snprintf(tokens[t_count].text, MAX_TOKEN_TEXT, "0xFF%02X%02X%02X", b, g, r);
        tokens[t_count].line = current_line;
      tokens[t_count].fileId = current_file;
        t_count++;
        i += 7;
        continue;
      }
    }

    // dot parse
    if (source[i] == '.') {
      tokens[t_count].type = TOKEN_DOT;
      strcpy(tokens[t_count].text, ".");
      tokens[t_count].line = current_line;
      tokens[t_count].fileId = current_file;
      t_count++;
      i++;
      continue;
    }

    // symbols
    if (strchr(";{},()[]?:", source[i])) {
      tokens[t_count].type = TOKEN_SYMBOL;
      tokens[t_count].text[0] = source[i];
      tokens[t_count].text[1] = '\0';
      tokens[t_count].line = current_line;
      tokens[t_count].fileId = current_file;
      t_count++;
      i++;
      continue;
    }

    // strings
    if (source[i] == '"') {
      int start = i;
      i++;
      while (source[i] != '"' && source[i] != '\0') i++;
      int len = i - start + 1;
      if (len > MAX_TOKEN_TEXT - 1) len = MAX_TOKEN_TEXT - 1; // clamp: wild sketches embed long data strings

      tokens[t_count].type = TOKEN_STRING;
      strncpy(tokens[t_count].text, &source[start], len);
      tokens[t_count].text[len] = '\0';
      tokens[t_count].line = current_line;
      tokens[t_count].fileId = current_file;
      t_count++;
      i++; 
      continue;
    }

    // char literals ('a', '\n', ' ')
    if (source[i] == '\'') {
      int start = i;
      i++;
      if (source[i] == '\\' && source[i+1] != '\0') i += 2; else if (source[i] != '\0') i++;
      while (source[i] != '\'' && source[i] != '\0') i++;
      int len = (i - start) + 1;
      if (len > MAX_TOKEN_TEXT - 1) len = MAX_TOKEN_TEXT - 1;

      tokens[t_count].type = TOKEN_STRING;
      strncpy(tokens[t_count].text, &source[start], len);
      tokens[t_count].text[len] = '\0';
      tokens[t_count].line = current_line;
      tokens[t_count].fileId = current_file;
      t_count++;
      if (source[i] == '\'') i++;
      continue;
    }

    // operators (incl. compound: ++ -- += -= *= /= %= && || == != <= >=)
    if (strchr("+-*/%=!><&|^", source[i])) {
      int len = 0;
      char c1 = source[i];
      tokens[t_count].type = TOKEN_OPERATOR;
      tokens[t_count].text[len++] = source[i++];
      // second slot: '=' completes == != <= >= += -= *= /= %= &= |= ^=,
      // doubled char completes ++ -- << >> && ||
      if (source[i] != '\0' &&
          (source[i] == '=' || (source[i] == c1 && strchr("+-&|<>", c1)))) {
        tokens[t_count].text[len++] = source[i++];
      }
      tokens[t_count].text[len] = '\0';
      tokens[t_count].line = current_line;
      tokens[t_count].fileId = current_file;
      t_count++;
      continue;
    }

    // numbers (incl. Java hex 0x... and long suffix 1234L)
    if (isdigit(source[i])) {
      int len = 0;
      tokens[t_count].type = TOKEN_NUMBER;
      if (source[i] == '0' && (source[i+1] == 'x' || source[i+1] == 'X')) {
        tokens[t_count].text[len++] = source[i++]; // '0'
        tokens[t_count].text[len++] = source[i++]; // 'x'
        while (isxdigit(source[i]) && len < MAX_TOKEN_TEXT - 1)
          tokens[t_count].text[len++] = source[i++];
        if (source[i] == 'l' || source[i] == 'L')
          tokens[t_count].text[len++] = source[i++];
      } else {
        while ((isdigit(source[i]) || source[i] == '.' || source[i] == 'f') &&
               len < MAX_TOKEN_TEXT - 1) {
          if (source[i] == '.' && !isdigit(source[i+1])) {
            break;
          }
          tokens[t_count].text[len++] = source[i++];
        }
        if (source[i] == 'l' || source[i] == 'L')
          tokens[t_count].text[len++] = source[i++];
        tokens[t_count].text[len] = '\0';
        // Java allows integer-like float literals without dot: e.g. 150f -> 150.0f
        if (len > 1 && (tokens[t_count].text[len-1] == 'f' || tokens[t_count].text[len-1] == 'F') &&
            !strchr(tokens[t_count].text, '.') && len + 2 < MAX_TOKEN_TEXT) {
          tokens[t_count].text[len-1] = '.';
          tokens[t_count].text[len] = '0';
          tokens[t_count].text[len+1] = 'f';
          tokens[t_count].text[len+2] = '\0';
          len += 2;
        }
      }
      tokens[t_count].text[len] = '\0';
      tokens[t_count].line = current_line;
      tokens[t_count].fileId = current_file;
      t_count++;
      continue;
    }

    // import statements reference external Java libraries: drop them
    if (source[i] == 'i' && strncmp(&source[i], "import", 6) == 0 &&
        !isalnum(source[i+6]) && source[i+6] != '_') {
      while (source[i] != '\0' && source[i] != ';' && source[i] != '\n') i++;
      if (source[i] == ';') i++;
      continue;
    }

    // keywords / identifiers
    if (isalpha(source[i]) || source[i] == '_') {
      int len = 0;
      while ((isalnum(source[i]) || source[i] == '_') &&
             len < MAX_TOKEN_TEXT - 1) {
        tokens[t_count].text[len++] = source[i++];
      }
      tokens[t_count].text[len] = '\0';

      if (is_keyword(tokens[t_count].text)) {
        tokens[t_count].type = TOKEN_KEYWORD;
      } else {
        tokens[t_count].type = TOKEN_IDENTIFIER;
      }
      tokens[t_count].line = current_line;
      tokens[t_count].fileId = current_file;
      t_count++;
      continue;
    }

    i++;
  }

  tokens[t_count].type = TOKEN_EOF;
  strcpy(tokens[t_count].text, "EOF");
  tokens[t_count].line = current_line;
      tokens[t_count].fileId = current_file;
  *tokens_out = tokens;
  return t_count;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Použití: %s <soubor.pde>\n", argv[0]);
    return 1;
  }

  FILE *file = fopen(argv[1], "rb");
  if (!file) {
    perror("Chyba při otevírání souboru");
    return 1;
  }

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *source_buffer = malloc(file_size + 1);
  if (!source_buffer) {
    fclose(file);
    return 1;
  }
  size_t read_bytes = fread(source_buffer, 1, file_size, file);
  source_buffer[read_bytes] = '\0';
  fclose(file);

  size_t token_capacity = 1 << 14;
  Token *tokens = malloc(sizeof(Token) * token_capacity);
  if (!tokens) {
    free(source_buffer);
    return 1;
  }

  int num_tokens = tokenize(source_buffer, &tokens, token_capacity);

  // ---- bracket balance validation ----------------------------------------
  // Cheap syntax gate before transpilation: unbalanced or mismatched
  // () [] {} reported with original file/line, so users never see the
  // confusing gcc errors on generated C for this common typo class.
  {
    struct { char open; int line; int fileId; } stack[256];
    int sp = 0;
    int errCount = 0;
    for (int k = 0; k < num_tokens && errCount < 3; k++) {
      Token t = tokens[k];
      if (t.type != TOKEN_SYMBOL) continue;
      const char *s = t.text;
      if (strcmp(s, "(") == 0 || strcmp(s, "[") == 0 || strcmp(s, "{") == 0) {
        if (sp < 256) {
          stack[sp].open = s[0];
          stack[sp].line = t.line;
          stack[sp].fileId = t.fileId;
        }
        sp++;
      } else if (strcmp(s, ")") == 0 || strcmp(s, "]") == 0 || strcmp(s, "}") == 0) {
        char close = s[0];
        char want = (close == ')') ? '(' : (close == ']') ? '[' : '{';
        if (sp == 0) {
          fprintf(stderr, "%s:%d: error: unmatched '%c'\n",
                  pdeFileName(t.fileId), t.line, close);
          errCount++;
        } else {
          sp--;
          if (sp < 256 && stack[sp].open != want) {
            fprintf(stderr,
                    "%s:%d: error: '%c' does not match '%c' opened at %s:%d\n",
                    pdeFileName(t.fileId), t.line, close, stack[sp].open,
                    pdeFileName(stack[sp].fileId), stack[sp].line);
            errCount++;
          }
        }
      }
    }
    if (errCount == 0 && sp > 0) {
      int top = (sp - 1 < 256) ? sp - 1 : 255;
      fprintf(stderr, "%s:%d: error: unclosed '%c' opened here (%d unmatched total)\n",
              pdeFileName(stack[top].fileId), stack[top].line,
              stack[top].open, sp);
      errCount++;
    }
    if (errCount > 0) {
      free(tokens);
      free(source_buffer);
      return 1;
    }
  }

  // Java-style % (float-capable) -> pmod(a,b)
  {
    Token *rewritten;
    int new_count = rewrite_modulo(tokens, num_tokens, &rewritten);
    free(tokens);
    tokens = rewritten;
    num_tokens = new_count;
  }

  // v.charAt(i) -> _pde_charat(v, i), then "a" + x + "b" -> _pde_cat(...)
  {
    Token *r;
    int n = rewrite_charat(tokens, num_tokens, &r);
    free(tokens);
    tokens = r;
    num_tokens = n;    n = rewrite_concat(tokens, num_tokens, &r);
    free(tokens);
    tokens = r;
    num_tokens = n;
    n = rewrite_pvector_accessors(tokens, num_tokens, &r);
    free(tokens);
    tokens = r;
    num_tokens = n;
  }

  // Phase 6: user-defined classes. Must run BEFORE the name-collision rename
  // and the forward-declaration prescan: rewrite_classes() strips `class NAME
  // { ... }` blocks, records member metadata (typedefs/prototypes/definitions
  // are emitted around the sketch body), and routes `new X(...)`,
  // ArrayList member ops, cast-gets and class member calls on typed
  // receivers. Method-body tokens are copied out, so the source array can be
  // freed here as usual.
  {
    Token *rewritten;
    int new_count = rewrite_classes(tokens, num_tokens, &rewritten);
    free(tokens);
    tokens = rewritten;
    num_tokens = new_count;
  }

  // Phase 10 Tier A item 9 / Tier B: real String instance methods + string
  // operators. Symbol table is populated by rewrite_classes above, so these
  // can gate on receiver/operand types. Order: method-call dispatch first,
  // then the `+=` and == / != operator rewrites.
  {
    Token *r;
    int n = rewrite_str_methods(tokens, num_tokens, &r);
    free(tokens); tokens = r; num_tokens = n;
    n = rewrite_str_assign(tokens, num_tokens, &r);
    free(tokens); tokens = r; num_tokens = n;
    n = rewrite_str_cmp(tokens, num_tokens, &r);
    free(tokens); tokens = r; num_tokens = n;
    n = rewrite_gl_calls(tokens, num_tokens, &r);
    free(tokens); tokens = r; num_tokens = n;
    n = rewrite_array_length(tokens, num_tokens, &r);
    free(tokens); tokens = r; num_tokens = n;
  }

  // Java allows a variable and a function to share a name (separate
  // namespaces); C does not. Rename colliding functions NAME -> NAME_fn at
  // definition and call sites (call = IDENT followed by "(").
  {
    char (*funcDefs)[MAX_TOKEN_TEXT] = malloc(MAX_TOKEN_TEXT * MAX_TOKENS);
    int funcCount = 0;
    for (int p = 1; p < num_tokens && funcCount < MAX_TOKENS; p++) {
      if (!is_function_return_type(tokens[p - 1]) || tokens[p].type != TOKEN_IDENTIFIER) continue;
      if (!(p + 1 < num_tokens && tokens[p + 1].type == TOKEN_SYMBOL &&
            strcmp(tokens[p + 1].text, "(") == 0)) continue;
      int depth = 0, close = -1;
      for (int q = p + 1; q < num_tokens; q++) {
        if (tokens[q].type == TOKEN_SYMBOL && strcmp(tokens[q].text, "(") == 0) depth++;
        else if (tokens[q].type == TOKEN_SYMBOL && strcmp(tokens[q].text, ")") == 0) {
          depth--;
          if (depth == 0) { close = q; break; }
        }
      }
      if (close < 0 || close + 1 >= num_tokens) continue;
      bool bodyAfter =
        (tokens[close+1].type == TOKEN_SYMBOL && strcmp(tokens[close+1].text, "{") == 0) ||
        (close + 3 < num_tokens &&
         tokens[close+1].type == TOKEN_SYMBOL && strcmp(tokens[close+1].text, "[") == 0 &&
         tokens[close+2].type == TOKEN_SYMBOL && strcmp(tokens[close+2].text, "]") == 0 &&
         tokens[close+3].type == TOKEN_SYMBOL && strcmp(tokens[close+3].text, "{") == 0);
      if (!bodyAfter) continue;
      snprintf(funcDefs[funcCount++], MAX_TOKEN_TEXT, "%s", tokens[p].text);
    }
    int renames = 0;
    for (int p = 0; p < num_tokens && renames < 64; p++) {
      if (tokens[p].type != TOKEN_IDENTIFIER) continue;
      bool isFunc = false;
      for (int q = 0; q < funcCount; q++) {
        if (strcmp(funcDefs[q], tokens[p].text) == 0) { isFunc = true; break; }
      }
      if (!isFunc) continue;
      // declared as a variable too? "TYPE name" where next isn't "("
      bool alsoVar = false;
      for (int q = 1; q < num_tokens; q++) {
        if (tokens[q].type != TOKEN_IDENTIFIER || strcmp(tokens[q].text, tokens[p].text) != 0)
          continue;
        if (!is_function_return_type(tokens[q - 1])) continue;
        if (q + 1 < num_tokens && tokens[q + 1].type == TOKEN_SYMBOL &&
            strcmp(tokens[q + 1].text, "(") == 0) continue; // the function itself
        alsoVar = true;
        break;
      }
      if (!alsoVar) continue;
      if (p + 1 < num_tokens && tokens[p + 1].type == TOKEN_SYMBOL &&
          strcmp(tokens[p + 1].text, "(") == 0) {
        char buf[MAX_TOKEN_TEXT];
        snprintf(buf, sizeof(buf), "%s_fn", tokens[p].text);
        snprintf(tokens[p].text, MAX_TOKEN_TEXT, "%s", buf);
        renames++;
      }
    }
    free(funcDefs);
  }

  // Print framework header (pack_color lives in processing.h, arity-routed)
  printf("#include \"processing.h\"\n\n");

  // If the sketch declares global variables that clash with standard Processing
  // mode macros (e.g. float RADIUS), undefine them so the sketch variable is valid.
  {
    static const char *const _clashable_constants[] = {
      "RADIUS", "CORNER", "CORNERS", "CENTER", NULL
    };
    for (int c = 0; _clashable_constants[c] != NULL; c++) {
      for (int g = 0; g < _nglob; g++) {
        if (strcmp(_globnames[g], _clashable_constants[c]) == 0) {
          printf("#undef %s\n", _clashable_constants[c]);
          break;
        }
      }
    }
  }

  // Phase 6: user-class structs + method prototypes, emitted up front so the
  // sketch globals/body (printed below) can refer to `new Class(...)` and
  // `obj.method(...)` before the definitions appear in the postlude.
  emit_class_typedefs_and_protos();

  // Forward declarations for multi-tab globals
  for (int g = 0; g < _nglob; g++) {
    if (strstr(_globtypes[g], "[]")) {
      char base[MAX_TOKEN_TEXT];
      snprintf(base, sizeof(base), "%s", _globtypes[g]);
      char *brk = strstr(base, "[]");
      if (brk) *brk = '\0';
      while (strlen(base) > 0 && base[strlen(base)-1] == ' ') base[strlen(base)-1] = '\0';
      printf("extern %s %s[];\n", base, _globnames[g]);
    } else {
      printf("extern %s %s;\n", _globtypes[g], _globnames[g]);
    }
  }
  printf("\n");

  // ---- forward declaration pre-scan -------------------------------------
  // Scan for "TYPE NAME(params) {" definitions and emit C prototypes so
  // sketches can call helper functions defined after their call sites.
  {
    for (int p = 1; p < num_tokens; p++) {
      // "TYPE NAME(" or array-typed "TYPE[] NAME(" (p stays on NAME)
      bool arrayReturn = false;
      int retIdx = p - 1;
      if (!is_function_return_type(tokens[p - 1]) || tokens[p].type != TOKEN_IDENTIFIER) {
        if (!(p >= 3 &&
              tokens[p].type == TOKEN_IDENTIFIER &&
              tokens[p - 1].type == TOKEN_SYMBOL && strcmp(tokens[p - 1].text, "]") == 0 &&
              tokens[p - 2].type == TOKEN_SYMBOL && strcmp(tokens[p - 2].text, "[") == 0 &&
              is_function_return_type(tokens[p - 3]))) {
          continue;
        }
        arrayReturn = true;
        retIdx = p - 3; // type-first form: TYPE sits three back
      }

      const char *fnName = tokens[p].text;
      if (event_callback_suffix(fnName) != NULL) continue; // renamed later

      // constructors inside classes: preceded by their own name
      if (p >= 2 && tokens[p - 2].type == TOKEN_IDENTIFIER &&
          strcmp(tokens[p - 2].text, fnName) == 0) continue;
      // method call or array use: require "(" next and "{" after matching ")"
      if (!(p + 1 < num_tokens && tokens[p + 1].type == TOKEN_SYMBOL &&
            strcmp(tokens[p + 1].text, "(") == 0)) continue;

      int depth = 0;
      int close = -1;
      for (int q = p + 1; q < num_tokens; q++) {
        if (tokens[q].type == TOKEN_SYMBOL && strcmp(tokens[q].text, "(") == 0) depth++;
        else if (tokens[q].type == TOKEN_SYMBOL && strcmp(tokens[q].text, ")") == 0) {
          depth--;
          if (depth == 0) { close = q; break; }
        }
      }
      if (close < 0 || close + 1 >= num_tokens) continue;
      // Java trailing-array return: "TYPE name(...)[] {"
      if (tokens[close + 1].type == TOKEN_SYMBOL && strcmp(tokens[close + 1].text, "[") == 0 &&
          close + 3 < num_tokens &&
          tokens[close + 2].type == TOKEN_SYMBOL && strcmp(tokens[close + 2].text, "]") == 0 &&
          tokens[close + 3].type == TOKEN_SYMBOL && strcmp(tokens[close + 3].text, "{") == 0)
      {
        arrayReturn = true;
      }
      else if (!(tokens[close + 1].type == TOKEN_SYMBOL && strcmp(tokens[close + 1].text, "{") == 0)) continue;

      {
        char retType[64];
        snprintf(retType, sizeof(retType), "%s", tokens[retIdx].text);
        if (strcmp(retType, "boolean") == 0) strcpy(retType, "bool");
        if (strcmp(retType, "color") == 0) strcpy(retType, "uint32_t");
        if (strcmp(retType, "String") == 0) strcpy(retType, "const char*");
        printf("%s%s%s(", retType,
               arrayReturn ? ((retType[strlen(retType)-1] == '*') ? "" : " *") : " ",
               fnName);
      }
      {
        int q = p + 2;
        while (q < close) {
          Token t = tokens[q];
          bool isType = (t.type == TOKEN_KEYWORD || t.type == TOKEN_IDENTIFIER);
          if (isType && q + 2 < close &&
              tokens[q+1].type == TOKEN_SYMBOL && strcmp(tokens[q+1].text, "[") == 0 &&
              tokens[q+2].type == TOKEN_SYMBOL && strcmp(tokens[q+2].text, "]") == 0) {
            // "TYPE[] name" -> "TYPE name[]"
            emit_c_type(t.text);
            printf(" ");
            printf("%s[]", tokens[q+3].text);
            q += 4;
          } else if (isType) {
            emit_c_type(t.text);
            printf(" ");
            q++;
          } else {
            printf("%s", t.text);
            q++;
          }
        }
      }
      printf(");\n");
    }
    printf("\n");
  }

  // strip Java trailing "[]" from function definitions in the token stream
  // ("boolean f(int n)[] {" -> "boolean f(int n) {"); the emitter's rule 5c
  // turns the return type into a pointer
  {
    for (int p = 0; p + 4 < num_tokens; p++) {
      if (!is_trailing_array_def(tokens, num_tokens, p)) continue;
      int depth = 0, close = -1;
      for (int q = p + 2; q < num_tokens; q++) {
        if (tokens[q].type == TOKEN_SYMBOL && strcmp(tokens[q].text, "(") == 0) depth++;
        else if (tokens[q].type == TOKEN_SYMBOL && strcmp(tokens[q].text, ")") == 0) {
          depth--;
          if (depth == 0) { close = q; break; }
        }
      }
      if (close < 0 || close + 3 >= num_tokens) continue;
      memmove(&tokens[close + 1], &tokens[close + 3],
              sizeof(Token) * (num_tokens - close - 3));
      num_tokens -= 2;
      // bake the pointer into the return-type token so the emitter (which
      // maps "boolean"->"bool" etc.) prints e.g. "bool *"
      {
        char mapped[64];
        snprintf(mapped, sizeof(mapped), "%s *", emit_c_type_str(tokens[p].text));
        tokens[p].type = TOKEN_IDENTIFIER;
        snprintf(tokens[p].text, MAX_TOKEN_TEXT, "%s", mapped);
      }
      p = close;
    }
  }

  // Detect non-constant global initializers in the post-rewrite stream
  // and build a skip array + collect init expressions for _pde_global_init().
  // Only variables actually registered as globals (scan_globals runs at
  // brace-depth-0, so locals inside functions are excluded).
  bool *skipInit = calloc((size_t)num_tokens, sizeof(bool));
  char initTexts[MAX_GLOBAL_NONCONST][512];
  int nInitTexts = 0;
  {
    int braceDepth = 0;
    for (int p = 0; p < num_tokens; p++) {
      if (tokens[p].type == TOKEN_SYMBOL && strcmp(tokens[p].text, "{") == 0)
        braceDepth++;
      if (tokens[p].type == TOKEN_SYMBOL &&
          strcmp(tokens[p].text, "}") == 0)
        if (braceDepth > 0) braceDepth--;
      if (braceDepth != 0) continue;
      if (!is_plausible_type_token(tokens[p])) continue;
      int q = p + 1;
      while (q < num_tokens && tokens[q].type != TOKEN_IDENTIFIER) q++;
      if (q >= num_tokens) continue;
      if (!is_global_name(tokens[q].text)) continue;
      // Skip past any plausible type tokens to find the actual variable name
      while (q < num_tokens && is_plausible_type_token(tokens[q])) {
        q++;
        while (q < num_tokens && tokens[q].type != TOKEN_IDENTIFIER) q++;
      }
      if (q >= num_tokens) continue;
      int r = q + 1;
      while (r + 1 < num_tokens && tokens[r].type == TOKEN_SYMBOL &&
             strcmp(tokens[r].text, "[") == 0 &&
             tokens[r + 1].type == TOKEN_SYMBOL &&
             strcmp(tokens[r + 1].text, "]") == 0)
        r += 2;
      while (r < num_tokens && tokens[r].type != TOKEN_SYMBOL &&
             strcmp(tokens[r].text, "=") != 0)
        r++;
      if (r >= num_tokens || strcmp(tokens[r].text, "=") != 0) continue;
      bool hasCall = false;
      int initStart = r + 1;
      int initEnd = initStart;
      for (int u = initStart; u < num_tokens; u++) {
        if (tokens[u].type == TOKEN_SYMBOL &&
            strcmp(tokens[u].text, ";") == 0) {
          initEnd = u;
          break;
        }
        if (tokens[u].type == TOKEN_SYMBOL &&
            strcmp(tokens[u].text, "(") == 0)
          hasCall = true;
        if (tokens[u].type == TOKEN_IDENTIFIER &&
            strcmp(tokens[u].text, "new") == 0)
          hasCall = true;
      }
      if (!hasCall) continue;
      for (int u = r; u < initEnd; u++) skipInit[u] = true;
      if (nInitTexts < MAX_GLOBAL_NONCONST) {
        int pos = 0;
        snprintf(initTexts[nInitTexts],
                 sizeof(initTexts[nInitTexts]), "%s = ",
                 tokens[q].text);
        pos = (int)strlen(initTexts[nInitTexts]);
        for (int u = r + 1; u < initEnd; u++) {
          // new PVector(...) → pvector(...)
          if (tokens[u].type == TOKEN_IDENTIFIER &&
              strcmp(tokens[u].text, "new") == 0 &&
              u + 2 < initEnd &&
              tokens[u+1].type == TOKEN_IDENTIFIER &&
              strcmp(tokens[u+1].text, "PVector") == 0 &&
              tokens[u+2].type == TOKEN_SYMBOL &&
              strcmp(tokens[u+2].text, "(") == 0) {
            const char *s = "pvector(";
            int slen = (int)strlen(s);
            if (pos + slen + 1 < 512) {
              memcpy(initTexts[nInitTexts] + pos, s, slen);
              pos += slen;
              initTexts[nInitTexts][pos++] = ' ';
              initTexts[nInitTexts][pos] = '\0';
            }
            u += 2;
            continue;
          }
          // new PImage(...) → pimage_new(...)
          if (tokens[u].type == TOKEN_IDENTIFIER &&
              strcmp(tokens[u].text, "new") == 0 &&
              u + 2 < initEnd &&
              tokens[u+1].type == TOKEN_IDENTIFIER &&
              strcmp(tokens[u+1].text, "PImage") == 0 &&
              tokens[u+2].type == TOKEN_SYMBOL &&
              strcmp(tokens[u+2].text, "(") == 0) {
            const char *s = "pimage_new(";
            int slen = (int)strlen(s);
            if (pos + slen + 1 < 512) {
              memcpy(initTexts[nInitTexts] + pos, s, slen);
              pos += slen;
              initTexts[nInitTexts][pos++] = ' ';
              initTexts[nInitTexts][pos] = '\0';
            }
            u += 2;
            continue;
          }
          // new TYPE[d1]... -> _pde_array*_new(...)
          if (tokens[u].type == TOKEN_IDENTIFIER &&
              strcmp(tokens[u].text, "new") == 0 &&
              u + 2 < initEnd &&
              tokens[u+2].type == TOKEN_SYMBOL &&
              strcmp(tokens[u+2].text, "[") == 0) {
            const char *elemType = emit_c_type_str(tokens[u+1].text);
            int dimStarts[4], dimEnds[4];
            int nDims = 0;
            int cur = u + 2;
            while (nDims < 4 && cur < initEnd &&
                   tokens[cur].type == TOKEN_SYMBOL && strcmp(tokens[cur].text, "[") == 0) {
              int depth = 1, s = cur + 1, e = s;
              while (e < initEnd && depth > 0) {
                if (tokens[e].type == TOKEN_SYMBOL && strcmp(tokens[e].text, "[") == 0) depth++;
                else if (tokens[e].type == TOKEN_SYMBOL && strcmp(tokens[e].text, "]") == 0) depth--;
                if (depth == 0) break;
                e++;
              }
              if (depth == 0) {
                dimStarts[nDims] = s;
                dimEnds[nDims] = e;
                nDims++;
                cur = e + 1;
              } else break;
            }
            if (nDims > 0) {
              char buf[256];
              int blen = 0;
              if (nDims == 1)      blen = snprintf(buf, sizeof(buf), "(%s *)_pde_array_new(", elemType);
              else if (nDims == 2) blen = snprintf(buf, sizeof(buf), "(%s **)_pde_array2d_new(", elemType);
              else if (nDims == 3) blen = snprintf(buf, sizeof(buf), "(%s ***)_pde_array3d_new(", elemType);
              else                 blen = snprintf(buf, sizeof(buf), "(%s *)_pde_array_new(", elemType);
              if (pos + blen < 512) { memcpy(initTexts[nInitTexts] + pos, buf, blen); pos += blen; }
              for (int d = 0; d < nDims; d++) {
                if (d > 0 && pos + 2 < 512) { memcpy(initTexts[nInitTexts] + pos, ", ", 2); pos += 2; }
                for (int t = dimStarts[d]; t < dimEnds[d]; t++) {
                  int tlen = (int)strlen(tokens[t].text);
                  if (tokens[t].type == TOKEN_IDENTIFIER && t + 2 < dimEnds[d] &&
                      tokens[t+1].type == TOKEN_DOT && strcmp(tokens[t+2].text, "length") == 0) {
                    char lbuf[64];
                    int llen = snprintf(lbuf, sizeof(lbuf), "_pde_len(%s)", tokens[t].text);
                    if (pos + llen < 512) { memcpy(initTexts[nInitTexts] + pos, lbuf, llen); pos += llen; }
                    t += 2;
                  } else {
                    if (pos + tlen < 512) { memcpy(initTexts[nInitTexts] + pos, tokens[t].text, tlen); pos += tlen; }
                  }
                  if (t + 1 < dimEnds[d] && pos + 1 < 512) { initTexts[nInitTexts][pos++] = ' '; }
                }
              }
              char endbuf[64];
              int elen = snprintf(endbuf, sizeof(endbuf), ", sizeof(%s))", elemType);
              if (pos + elen < 512) { memcpy(initTexts[nInitTexts] + pos, endbuf, elen); pos += elen; }
              initTexts[nInitTexts][pos++] = ' ';
              initTexts[nInitTexts][pos] = '\0';
              u = dimEnds[nDims - 1];
              continue;
            }
          }
          // color(...) → pack_color(...)
          if (tokens[u].type == TOKEN_KEYWORD &&
              strcmp(tokens[u].text, "color") == 0 &&
              u + 1 < initEnd &&
              tokens[u+1].type == TOKEN_SYMBOL &&
              strcmp(tokens[u+1].text, "(") == 0) {
            const char *s = "pack_color(";
            int slen = (int)strlen(s);
            if (pos + slen + 1 < 512) {
              memcpy(initTexts[nInitTexts] + pos, s, slen);
              pos += slen;
              initTexts[nInitTexts][pos++] = ' ';
              initTexts[nInitTexts][pos] = '\0';
            }
            u += 1;
            continue;
          }
          int len = (int)strlen(tokens[u].text);
          if (pos + len + 1 < 512) {
            memcpy(initTexts[nInitTexts] + pos, tokens[u].text, len);
            pos += len;
            initTexts[nInitTexts][pos++] = ' ';
            initTexts[nInitTexts][pos] = '\0';
          }
        }
        if (pos > 0 && initTexts[nInitTexts][pos - 1] == ' ')
          initTexts[nInitTexts][pos - 1] = '\0';
        nInitTexts++;
      }
    }
  }

  int i = 0;
  int pendingBracketClose = 0;
  bool pendingCallocType = false;
  char lastArrayType[32] = "";
  int lastLine = -1;
  int lastFile = -1;

  // expand(arr, n) call tracking: closing ")" gets ", sizeof(*arr))"
  bool pendingExpand = false;
  char expandVar[MAX_TOKEN_TEXT];
  int expandDepth = 0;

  // Java labeled breaks: "loop: for(...){ break loop; }"
  // -> goto __loop_brk emitted after the loop's closing brace
  struct { char name[MAX_TOKEN_TEXT]; int depth; } labelStack[16];
  int labelCount = 0;
  int braceDepth = 0;
  while (i < num_tokens) {
    Token current = tokens[i];

    // #line directives: map generated C positions back to the original
    // .pde tabs so gcc diagnostics cite the user's own source lines.
    // A directive must start at column 0: split the line if needed.
    if (current.line != lastLine || current.fileId != lastFile) {
      bool atBOL = (i == 0) ||
        (tokens[i-1].type == TOKEN_SYMBOL && strcmp(tokens[i-1].text, ";") == 0);
      if (!atBOL) printf("\n");
      printf("#line %d \"%s\"\n", current.line, pdeFileName(current.fileId));
      lastLine = current.line;
      lastFile = current.fileId;
    }

    // drop Java "final" modifier before any type keyword or identifier
    if ((current.type == TOKEN_IDENTIFIER) && strcmp(current.text, "final") == 0 &&
        (i + 1 < num_tokens) &&
        (tokens[i+1].type == TOKEN_IDENTIFIER || tokens[i+1].type == TOKEN_KEYWORD))
    {
      i++;
      continue;
    }

    // drop Java/Processing "import x.y.*;" statements (may appear mid-file)
    // and stray Java "package x.y;" declarations; external libraries are not
    // linked, so their imports only produce garbage C if passed through
    if (current.type == TOKEN_IDENTIFIER &&
        (strcmp(current.text, "import") == 0 || strcmp(current.text, "package") == 0))
    {
      while (i < num_tokens &&
             !(tokens[i].type == TOKEN_SYMBOL && strcmp(tokens[i].text, ";") == 0)) i++;
      if (i < num_tokens) i++; // consume ';'
      continue;
    }

    // arity-routed builtins
    if (current.type == TOKEN_IDENTIFIER &&
        (strcmp(current.text, "createFont") == 0 ||
         strcmp(current.text, "bezierVertex") == 0 ||
         strcmp(current.text, "pixelDensity") == 0 ||
         strcmp(current.text, "displayDensity") == 0 ||
         strcmp(current.text, "textAlign") == 0 ||
         strcmp(current.text, "colorMode") == 0 ||
         strcmp(current.text, "smooth") == 0) &&
        (i + 1 < num_tokens) &&
        tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
    {
      int close;
      int nargs = count_call_args(tokens, num_tokens, i + 1, &close);
      const char *name = current.text;
      if (nargs > 0) {
        if (strcmp(name, "createFont") == 0 && nargs >= 3) name = "createFont3";
        else if (strcmp(name, "bezierVertex") == 0 && nargs >= 8) name = "bezierVertex8";
        else if (strcmp(name, "pixelDensity") == 0) name = "pixelDensity1";
        else if (strcmp(name, "displayDensity") == 0) name = "displayDensity1";
        else if (strcmp(name, "textAlign") == 0 && nargs >= 2) name = "textAlign2";
        else if (strcmp(name, "textAlign") == 0) name = "textAlign1";
        else if (strcmp(name, "colorMode") == 0) {
          static char buf[16];
          int m = nargs; if (m > 5) m = 5; if (m < 1) m = 1;
          snprintf(buf, sizeof(buf), "colorMode%d", m);
          name = buf;
        }
        else if (strcmp(name, "smooth") == 0 && nargs > 0) name = "smooth1";
      }
      printf("%s(", name);
      i += 2;
      continue;
    }

    // image(): type-dispatched at runtime via _Generic (PGraphics vs PImage);
    // normalize the 3-arg form to 5 params, 0 0 meaning natural size
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "image") == 0 &&
        !(i > 0 && tokens[i-1].type == TOKEN_DOT) &&
        (i + 1 < num_tokens) &&
        tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
    {
      int close;
      int nargs = count_call_args(tokens, num_tokens, i + 1, &close);
      printf("image(");
      i += 2;
      int depth = 1;
      while (i < num_tokens && depth > 0) {
        Token t = tokens[i];
        if (t.type == TOKEN_SYMBOL && strcmp(t.text, "(") == 0) depth++;
        if (t.type == TOKEN_SYMBOL && strcmp(t.text, ")") == 0) {
          depth--;
          if (depth == 0) {
            if (nargs == 3) printf(", 0, 0");
            printf(")");
            i++;
            break;
          }
        }
        printf("%s", t.text);
        if (t.type != TOKEN_DOT && t.type != TOKEN_OPERATOR &&
            !(t.type == TOKEN_SYMBOL && strchr("([{,", t.text[0]))) printf(" ");
        i++;
      }
      continue;
    }

    // 0b. Drop `ArrayList<...>` generics in declaration position (type args
    //     would clash with C's `<`/`>` operators; the `(Type) list.get(i)`
    //     idiom recovers the element type at each use).
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "ArrayList") == 0 &&
        (i + 1 < num_tokens) && strcmp(tokens[i+1].text, "<") == 0)
    {
      int close = skip_angle_close(tokens, num_tokens, i + 1);
      int j = (close >= 0) ? close + 1 : i + 3;
      printf("ArrayList");
      if (j < num_tokens && tokens[j].type != TOKEN_SYMBOL &&
          tokens[j].type != TOKEN_EOF) printf(" ");
      i = j;
      continue;
    }

    // 1. Array declarations "TYPE[]... name" (multi-dimensional: TYPE[][] name, etc.):
    //    - "= {" literal init stays C-style:  TYPE name[] = {
    //    - otherwise pointer style (heap ref): TYPE *name, TYPE **name, etc.
    {
      int dims = 0;
      int k = i + 1;
      while (k + 1 < num_tokens &&
             tokens[k].type == TOKEN_SYMBOL && strcmp(tokens[k].text, "[") == 0 &&
             tokens[k+1].type == TOKEN_SYMBOL && strcmp(tokens[k+1].text, "]") == 0) {
        dims++;
        k += 2;
      }
      if (dims > 0 && k < num_tokens && tokens[k].type == TOKEN_IDENTIFIER &&
          (is_type_token(current) || current.type == TOKEN_IDENTIFIER)) {
        const char *type_out = emit_c_type_str(current.text);
        bool braceInit = (dims == 1 && k + 2 < num_tokens &&
                          tokens[k+1].type == TOKEN_OPERATOR && strcmp(tokens[k+1].text, "=") == 0 &&
                          tokens[k+2].type == TOKEN_SYMBOL && strcmp(tokens[k+2].text, "{") == 0);
        if (braceInit) {
          printf("%s %s[] = {", type_out, tokens[k].text);
          braceDepth++;
          i = k + 3;
        } else {
          printf("%s ", type_out);
          for (int d = 0; d < dims; d++) printf("*");
          printf("%s", tokens[k].text);
          i = k + 1;
        }
        // additional declarators in the same statement: ", *b, *c"
        while (!braceInit &&
               (i + 1 < num_tokens) &&
               tokens[i].type == TOKEN_SYMBOL && strcmp(tokens[i].text, ",") == 0 &&
               tokens[i+1].type == TOKEN_IDENTIFIER) {
          int extraDims = 0;
          int ek = i + 2;
          while (ek + 1 < num_tokens &&
                 tokens[ek].type == TOKEN_SYMBOL && strcmp(tokens[ek].text, "[") == 0 &&
                 tokens[ek+1].type == TOKEN_SYMBOL && strcmp(tokens[ek+1].text, "]") == 0) {
            extraDims++;
            ek += 2;
          }
          int useDims = (extraDims > 0) ? extraDims : dims;
          printf(", ");
          for (int d = 0; d < useDims; d++) printf("*");
          printf("%s", tokens[i+1].text);
          i = (extraDims > 0) ? ek : i + 2;
        }
        continue;
      }
    }

    // 1b. Name-first array declarations: "TYPE name[]..."
    if ((is_type_token(current) || current.type == TOKEN_IDENTIFIER) &&
        (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_IDENTIFIER &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "[") == 0 &&
        tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "]") == 0)
    {
      const char *type_out = emit_c_type_str(current.text);
      int dims = 0;
      int k = i + 2;
      while (k + 1 < num_tokens &&
             tokens[k].type == TOKEN_SYMBOL && strcmp(tokens[k].text, "[") == 0 &&
             tokens[k+1].type == TOKEN_SYMBOL && strcmp(tokens[k+1].text, "]") == 0) {
        dims++;
        k += 2;
      }
      bool braceInit = (dims == 1 && k + 1 < num_tokens &&
                        tokens[k].type == TOKEN_OPERATOR && strcmp(tokens[k].text, "=") == 0 &&
                        tokens[k+1].type == TOKEN_SYMBOL && strcmp(tokens[k+1].text, "{") == 0);
      if (braceInit) {
        printf("%s %s[] = {", type_out, tokens[i+1].text);
        braceDepth++;
        i = k + 2;
      } else {
        printf("%s ", type_out);
        for (int d = 0; d < dims; d++) printf("*");
        printf("%s", tokens[i+1].text);
        i = k;
      }
      // further declarators: ", b[]" or ", float b[]"
      while (!braceInit &&
             (i + 1 < num_tokens) &&
             tokens[i].type == TOKEN_SYMBOL && strcmp(tokens[i].text, ",") == 0)
      {
        bool retyped = is_type_token(tokens[i+1]);
        int nameIdx = retyped ? i + 2 : i + 1;
        if (nameIdx >= num_tokens || tokens[nameIdx].type != TOKEN_IDENTIFIER) break;
        int d2 = 0;
        int k2 = nameIdx + 1;
        while (k2 + 1 < num_tokens &&
               tokens[k2].type == TOKEN_SYMBOL && strcmp(tokens[k2].text, "[") == 0 &&
               tokens[k2+1].type == TOKEN_SYMBOL && strcmp(tokens[k2+1].text, "]") == 0) {
          d2++;
          k2 += 2;
        }
        if (d2 == 0) d2 = dims;
        if (retyped) {
          printf(", %s ", emit_c_type_str(tokens[i+1].text));
          for (int d = 0; d < d2; d++) printf("*");
          printf("%s", tokens[nameIdx].text);
        } else {
          printf(", ");
          for (int d = 0; d < d2; d++) printf("*");
          printf("%s", tokens[nameIdx].text);
        }
        i = k2;
      }
      continue;
    }

    // super.method(...) -> ((void)0)
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "super") == 0 &&
        i + 3 < num_tokens && tokens[i + 1].type == TOKEN_DOT &&
        tokens[i + 2].type == TOKEN_IDENTIFIER &&
        tokens[i + 3].type == TOKEN_SYMBOL && strcmp(tokens[i + 3].text, "(") == 0) {
      int close = matching_close(tokens, num_tokens, i + 3, "(", ")");
      if (close > i + 3) {
        printf("((void)0)");
        i = close + 1;
        continue;
      }
    }

    // 2. Normalize pointer-to-literal array initialization: "int* mm = {" -> "int mm[] = {"
    if (is_type_token(current) &&
        (i + 4 < num_tokens) &&
        tokens[i+1].type == TOKEN_OPERATOR && strcmp(tokens[i+1].text, "*") == 0 &&
        tokens[i+2].type == TOKEN_IDENTIFIER &&
        tokens[i+3].type == TOKEN_OPERATOR && strcmp(tokens[i+3].text, "=") == 0 &&
        tokens[i+4].type == TOKEN_SYMBOL && strcmp(tokens[i+4].text, "{") == 0)
    {
      const char *type_out = current.text;
      if (strcmp(type_out, "boolean") == 0) type_out = "bool";
      if (strcmp(type_out, "color") == 0) type_out = "uint32_t";
      printf("%s %s[] = {", type_out, tokens[i+2].text);
      braceDepth++;
      i += 5;
      continue;
    }

    // 3. Size function translation (drops the renderer arg: size(w,h,OPENGL))
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "size") == 0 &&
        !(i > 0 && tokens[i-1].type == TOKEN_DOT) &&
        (i + 1 < num_tokens) && tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
    {
      printf("size_converted(");
      i += 2;
      {
        int depth = 1;
        int commas = 0;
        while (i < num_tokens && depth > 0) {
          Token t = tokens[i];
          if (t.type == TOKEN_SYMBOL && strcmp(t.text, "(") == 0) depth++;
          if (t.type == TOKEN_SYMBOL && strcmp(t.text, ")") == 0) {
            depth--;
            if (depth == 0) { printf(")"); i++; break; }
          }
          if (depth == 1 && t.type == TOKEN_SYMBOL && strcmp(t.text, ",") == 0) {
            commas++;
            if (commas < 2) printf(", ");
            i++;
            continue;
          }
          if (commas < 2) {
            printf("%s", t.text);
            if (t.type != TOKEN_DOT && t.type != TOKEN_OPERATOR &&
                !(i + 1 < num_tokens && tokens[i+1].type == TOKEN_SYMBOL)) printf(" ");
          }
          i++;
        }
      }
      continue;
    }

    // 3b. frameRate: setter call vs read-only variable
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "frameRate") == 0) {
      if ((i + 1 < num_tokens) && tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0) {
        printf("set_frame_rate(");
        i += 2;
      } else {
        printf("current_frame_rate");
        i++;
      }
      continue;
    }

    // 3b2. createGraphics(w, h, renderer) drops the renderer arg
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "createGraphics") == 0 &&
        !(i > 0 && tokens[i-1].type == TOKEN_DOT) &&
        (i + 1 < num_tokens) && tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
    {
      int close;
      int nargs = count_call_args(tokens, num_tokens, i + 1, &close);
      printf("createGraphics(");
      i += 2;
      int depth = 1, commas = 0;
      while (i < num_tokens && depth > 0) {
        Token t = tokens[i];
        if (t.type == TOKEN_SYMBOL && strcmp(t.text, "(") == 0) depth++;
        if (t.type == TOKEN_SYMBOL && strcmp(t.text, ")") == 0) {
          depth--;
          if (depth == 0) { printf(")"); i++; break; }
        }
        if (depth == 1 && t.type == TOKEN_SYMBOL && strcmp(t.text, ",") == 0) {
          commas++;
          if (commas < 2) printf(", ");
          i++;
          continue;
        }
        if (commas < 2) {
          printf("%s", t.text);
          if (t.type != TOKEN_DOT && t.type != TOKEN_OPERATOR &&
              !(i + 1 < num_tokens && tokens[i+1].type == TOKEN_SYMBOL)) printf(" ");
        }
        i++;
      }
      (void)nargs;
      continue;
    }

    // 3c. exit() -> cooperative shutdown flag
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "exit") == 0 &&
        (i + 1 < num_tokens) && tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
    {
      printf("exit_sketch(");
      i += 2;
      continue;
    }

    // 3d. Common math aliases (abs handled by a _Generic macro in
    //     processing.h so integer subscripts stay integers)
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "parseInt") == 0 &&
        (i + 1 < num_tokens) && tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
    {
      printf("atoi(");
      i += 2;
      continue;
    }
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "parseFloat") == 0 &&
        (i + 1 < num_tokens) && tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
    {
      printf("atof(");
      i += 2;
      continue;
    }

    // 3e. String type declarations -> const char *
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "String") == 0 &&
        (i + 1 < num_tokens) &&
        tokens[i+1].type == TOKEN_IDENTIFIER)
    {
      printf("const char *");
      i++;
      continue;
    }

    // 3e1. Java null literal -> NULL
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "null") == 0) {
      printf("NULL");
      i++;
      continue;
    }

    // 3e2. Java this literal outside classes -> NULL
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "this") == 0) {
      printf("NULL");
      i++;
      continue;
    }

    // 3e2. String[] array declarations -> const char **name (pointer form,
    //      like the "TYPE[] name" -> "TYPE *name" rewrite for primitives)
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "String") == 0 &&
        (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "[") == 0 &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "]") == 0 &&
        tokens[i+3].type == TOKEN_IDENTIFIER)
    {
      printf("const char **%s", tokens[i+3].text);
      i += 4;
      continue;
    }

    // 3e3. `(Type[])` array cast -> `(Type *)`
    if (current.type == TOKEN_SYMBOL && strcmp(current.text, "(") == 0 &&
        i + 4 < num_tokens && is_plausible_type_token(tokens[i + 1]) &&
        tokens[i + 2].type == TOKEN_SYMBOL && strcmp(tokens[i + 2].text, "[") == 0 &&
        tokens[i + 3].type == TOKEN_SYMBOL && strcmp(tokens[i + 3].text, "]") == 0 &&
        tokens[i + 4].type == TOKEN_SYMBOL && strcmp(tokens[i + 4].text, ")") == 0) {
      const char *cty = emit_c_type_str(tokens[i + 1].text);
      if (strcmp(cty, "const char *") == 0)
        printf("(const char **)");
      else
        printf("(%s *)", cty);
      i += 4;
      continue;
    }

    // 3e4. `(PGraphics3D) expr` -> `_pde_as_pgraphics3d(expr)`
    if (current.type == TOKEN_SYMBOL && strcmp(current.text, "(") == 0 &&
        i + 3 < num_tokens && strcmp(tokens[i + 1].text, "PGraphics3D") == 0 &&
        tokens[i + 2].type == TOKEN_SYMBOL && strcmp(tokens[i + 2].text, ")") == 0 &&
        tokens[i + 3].type == TOKEN_IDENTIFIER) {
      printf("_pde_as_pgraphics3d(%s)", tokens[i + 3].text);
      i += 4;
      continue;
    }

    // 3e5. `(PGraphics...)` struct cast stripping -> skip the cast parens & type
    if (current.type == TOKEN_SYMBOL && strcmp(current.text, "(") == 0 &&
        i + 2 < num_tokens &&
        (strcmp(tokens[i + 1].text, "PGraphics") == 0 ||
         strcmp(tokens[i + 1].text, "PGraphicsOpenGL") == 0 ||
         strcmp(tokens[i + 1].text, "PGraphicsPDF") == 0 ||
         strcmp(tokens[i + 1].text, "PGraphics2D") == 0) &&
        tokens[i + 2].type == TOKEN_SYMBOL && strcmp(tokens[i + 2].text, ")") == 0) {
      i += 3;
      continue;
    }

    // 4. Localized pixel pointer type conversion mapping
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "pixels") == 0) {
      printf("((uint32_t*)pixels)");
      i++;
      continue;
    }

    // 5. Transform "new PVector(x, y)" -> "pvector(x, y)"
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "new") == 0 &&
        (i + 2 < num_tokens) &&
        tokens[i+1].type == TOKEN_IDENTIFIER && strcmp(tokens[i+1].text, "PVector") == 0 &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "(") == 0)
    {
      printf("pvector");
      i += 2;
      continue;
    }

    // 5a. Transform "new PImage(w, h)" -> "pimage_new(w, h)"
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "new") == 0 &&
        (i + 2 < num_tokens) &&
        tokens[i+1].type == TOKEN_IDENTIFIER && strcmp(tokens[i+1].text, "PImage") == 0 &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "(") == 0)
    {
      printf("pimage_new");
      i += 2;
      continue;
    }

    // 5b. Transform "new TYPE[d1]..." -> "(TYPE *)_pde_array*_new(d1, ..., sizeof(TYPE))"
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "new") == 0 &&
        (i + 3 < num_tokens) &&
        (tokens[i+1].type == TOKEN_IDENTIFIER || tokens[i+1].type == TOKEN_KEYWORD) &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "[") == 0 &&
        !(tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "]") == 0))
    {
      const char *elemType = emit_c_type_str(tokens[i+1].text);
      int dimStarts[4], dimEnds[4];
      int nDims = 0;
      int cur = i + 2;
      while (nDims < 4 && cur < num_tokens &&
             tokens[cur].type == TOKEN_SYMBOL && strcmp(tokens[cur].text, "[") == 0) {
        int depth = 1, s = cur + 1, e = s;
        while (e < num_tokens && depth > 0) {
          if (tokens[e].type == TOKEN_SYMBOL && strcmp(tokens[e].text, "[") == 0) depth++;
          else if (tokens[e].type == TOKEN_SYMBOL && strcmp(tokens[e].text, "]") == 0) depth--;
          if (depth == 0) break;
          e++;
        }
        if (depth == 0) {
          dimStarts[nDims] = s;
          dimEnds[nDims] = e;
          nDims++;
          cur = e + 1;
        } else break;
      }
      if (nDims > 0) {
        if (nDims == 1)      printf("(%s *)_pde_array_new(", elemType);
        else if (nDims == 2) printf("(%s **)_pde_array2d_new(", elemType);
        else if (nDims == 3) printf("(%s ***)_pde_array3d_new(", elemType);
        else                 printf("(%s *)_pde_array_new(", elemType);
        for (int d = 0; d < nDims; d++) {
          if (d > 0) printf(", ");
          for (int t = dimStarts[d]; t < dimEnds[d]; t++) {
            if (tokens[t].type == TOKEN_IDENTIFIER &&
                t + 2 < dimEnds[d] &&
                tokens[t+1].type == TOKEN_DOT &&
                tokens[t+2].type == TOKEN_IDENTIFIER &&
                strcmp(tokens[t+2].text, "length") == 0) {
              printf("_pde_len(%s)", tokens[t].text);
              t += 2;
            } else {
              printf("%s", tokens[t].text);
            }
            if (t + 1 < dimEnds[d]) printf(" ");
          }
        }
        printf(", sizeof(%s))", elemType);
        i = dimEnds[nDims - 1] + 1;
        continue;
      }
    }

    // 5c. Java array cast: "(TYPE[])" / "(TYPE[][])" -> "(TYPE *)" / "(TYPE **)"
    if (current.type == TOKEN_SYMBOL && strcmp(current.text, "(") == 0 &&
        i + 3 < num_tokens &&
        (is_type_token(tokens[i+1]) || tokens[i+1].type == TOKEN_IDENTIFIER))
    {
      int dims = 0;
      int k = i + 2;
      while (k + 1 < num_tokens &&
             tokens[k].type == TOKEN_SYMBOL && strcmp(tokens[k].text, "[") == 0 &&
             tokens[k+1].type == TOKEN_SYMBOL && strcmp(tokens[k+1].text, "]") == 0) {
        dims++;
        k += 2;
      }
      if (dims > 0 && k < num_tokens &&
          tokens[k].type == TOKEN_SYMBOL && strcmp(tokens[k].text, ")") == 0) {
        const char *castType = emit_c_type_str(tokens[i+1].text);
        printf("(%s ", castType);
        for (int d = 0; d < dims; d++) printf("*");
        printf(")");
        i = k + 1;
        continue;
      }
    }

    // 6. Map boolean keyword (type OR conversion cast: boolean(x) routes through
    //    the runtime macro so strings and numbers convert like Processing)
    if (current.type == TOKEN_KEYWORD && strcmp(current.text, "boolean") == 0) {
      if (i + 1 < num_tokens && tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
        printf("boolean");
      else
        printf("bool ");
      i++;
      continue;
    }

    // 6b. Primitive type as conversion cast: int(x), float(x), char(x),
    //     double(x) -> (int)(x) etc. (C has no function-style casts). byte(x)
    //     is deliberately excluded: it passes through to the runtime macro so
    //     the sketch gets clamp-to-[0,255] semantics instead of a wrap cast.
    if (current.type == TOKEN_KEYWORD &&
        (strcmp(current.text, "int") == 0 || strcmp(current.text, "float") == 0 ||
         strcmp(current.text, "char") == 0 || strcmp(current.text, "double") == 0) &&
        (i + 1 < num_tokens) &&
        tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
    {
      printf("(%s)", emit_c_type_str(current.text));
      i++;
      continue;
    }

    // 6c. byte as a scalar type: byte b; -> uint8_t b; (the cast form byte(x)
    //     falls through to the header macro, expanded by the C compiler).
    if (current.type == TOKEN_KEYWORD && strcmp(current.text, "byte") == 0) {
      if (i + 1 < num_tokens && tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
        printf("byte");
      else
        printf("uint8_t ");
      i++;
      continue;
    }

    // 7. Map color type vs color(...) constructor function split
    if (current.type == TOKEN_KEYWORD && strcmp(current.text, "color") == 0) {
      if (i + 1 < num_tokens && tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0) {
        printf("pack_color"); // Redirect function call to uint32 bit-packer
      } else {
        printf("uint32_t ");  // Redirect primitive type allocation
      }
      i++;
      continue;
    }

    // 7b. Rename event callback definitions: void keyPressed() -> void keyPressed_event()
    if (current.type == TOKEN_KEYWORD && strcmp(current.text, "void") == 0 &&
        (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_IDENTIFIER &&
        event_callback_suffix(tokens[i+1].text) != NULL &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "(") == 0 &&
        tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, ")") == 0)
    {
      printf("void %s%s(void)", tokens[i+1].text, event_callback_suffix(tokens[i+1].text));
      i += 4;
      continue;
    }

    // 7c. beginShape/endShape arity: zero-arg form gets the _0 variant
    if (current.type == TOKEN_IDENTIFIER &&
        (strcmp(current.text, "beginShape") == 0 || strcmp(current.text, "endShape") == 0) &&
        (i + 2 < num_tokens) &&
        tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
    {
      bool zeroArg = (tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, ")") == 0);
      printf("%s%s(", current.text, zeroArg ? "0" : "1");
      i += 2;
      continue;
    }

    // 7d. camera(): zero-arg form is the reset; 9-arg is left to the header
    //     macro (CAMERA_CHOOSER) which selects camera9 for the full call.
    if (current.type == TOKEN_IDENTIFIER &&
        strcmp(current.text, "camera") == 0 &&
        (i + 2 < num_tokens) &&
        tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
    {
      bool zeroArg = (tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, ")") == 0);
      if (zeroArg) {
        printf("camera0(");
        i += 2;
        continue;
      }
    }

    // 8a. ArrayList member ops on registered receivers (sketch globals +
    //     function locals): list.add(x) -> _pde_ag_add(list, x). Ahead of
    //     both the PVector table (so `list.add(...)` is not pvector_add) and
    //     the PImage block (so list.get(i) is not pimage_get_copy).
    if (current.type == TOKEN_IDENTIFIER &&
        (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_DOT &&
        tokens[i+2].type == TOKEN_IDENTIFIER &&
        tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "(") == 0)
    {
      const char *ty = symbol_type(current.text);
      if (ty && strcmp(ty, "ArrayList") == 0 && is_ag_method(tokens[i+2].text)) {
        bool noArg = (i + 4 < num_tokens && tokens[i+4].type == TOKEN_SYMBOL &&
                      strcmp(tokens[i+4].text, ")") == 0);
        printf("_pde_ag_%s(%s%s", tokens[i+2].text, current.text, noArg ? "" : ", ");
        i += 3;
        continue;
      }
    }

    // Minim.CONSTANT -> CONSTANT
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "Minim") == 0 &&
        i + 1 < num_tokens && tokens[i + 1].type == TOKEN_DOT) {
      i += 1;
      continue;
    }

    // 8. PVector object method rewriting (table-driven)
    //    mutators: v.add(w)      -> v = pvector_add(v, w)
    //    accessors: v.mag()      -> pvector_mag(v)
    //    static: PVector.add(..) -> pvector_add(..)
    if (current.type == TOKEN_IDENTIFIER &&
        (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_DOT &&
        tokens[i+2].type == TOKEN_IDENTIFIER &&
        tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "(") == 0)
    {
      const char *obj = current.text;
      const char *method = tokens[i+2].text;

      if (strcmp(obj, "PVector") == 0) {
        printf("pvector_%s(", method);
        i += 3;
        continue;
      }

      const char *objTy = symbol_type(obj);
      if (objTy && strcmp(objTy, "PVector") != 0) {
        // Not a PVector; do not hijack member calls on other types
      } else {
        char objName[MAX_TOKEN_TEXT];
        strcpy(objName, obj);

        if (pvector_mutator_method(method) || pvector_accessor_method(method)) {
          bool zeroArg = (i + 4 < num_tokens && tokens[i+4].type == TOKEN_SYMBOL &&
                          strcmp(tokens[i+4].text, ")") == 0);
          // PVector add/sub require arguments; zero-arg add() belongs to user classes
          if (zeroArg && (strcmp(method, "add") == 0 || strcmp(method, "sub") == 0)) {
            // not a zero-arg PVector call
          } else if (zeroArg) {
            if (pvector_mutator_method(method))
              printf("%s = pvector_%s(%s)", objName, method, objName);
            else
              printf("pvector_%s(%s)", method, objName);
            i += 4;
            continue;
          } else {
            if (pvector_mutator_method(method))
              printf("%s = pvector_%s(%s, ", objName, method, objName);
            else
              printf("pvector_%s(%s, ", method, objName);
            i += 3;
            continue;
          }
        }
      }
    }

    // 8b. Java array .length -> _pde_len(name): exact count for brace-initialized
    //     true arrays (via _Generic array branch) AND heap arrays allocated by
    //     _pde_array_new/expand (via registry lookup).
    if (current.type == TOKEN_IDENTIFIER &&
        (i + 2 < num_tokens) &&
        tokens[i+1].type == TOKEN_DOT &&
        tokens[i+2].type == TOKEN_IDENTIFIER &&
        strcmp(tokens[i+2].text, "length") == 0)
    {
      printf("_pde_len(%s)", current.text);
      i += 3;
      continue;
    }
    // 8c2. PImage member operations: img.op(...) / img.pixels rewritten to
    //     pimage_* helpers taking &receiver; dot-keyed so bare canvas calls
    //     (loadPixels(), updatePixels()) are untouched
    if (current.type == TOKEN_IDENTIFIER &&
        (i == 0 || tokens[i - 1].type != TOKEN_DOT) &&
        (i + 2 < num_tokens) &&
        tokens[i+1].type == TOKEN_DOT &&
        tokens[i+2].type == TOKEN_IDENTIFIER)
    {
      const char *recvTy = symbol_type(current.text);
      if (recvTy && strcmp(recvTy, "PImage") != 0 && strcmp(recvTy, "PGraphics") != 0 &&
          strcmp(recvTy, "PImage *") != 0 && strcmp(recvTy, "PGraphics *") != 0) {
        // Not a PImage; do not hijack member calls on other types
      } else {
        const char *op = tokens[i+2].text;
      const char *helper = NULL; bool takesArgs = false, mapsFilterConsts = false;
      bool byVal = false; // PGraphics ops take the canvas by value
      if (strcmp(op, "loadPixels") == 0) helper = "pimage_loadPixels";
      else if (strcmp(op, "updatePixels") == 0) helper = "pimage_updatePixels";
      else if (strcmp(op, "pixels") == 0) helper = "pimage_pixels";
      else if (strcmp(op, "resize") == 0) { helper = "pimage_resize"; takesArgs = true; }
      else if (strcmp(op, "filter") == 0) { helper = "pimage_filter"; takesArgs = true; mapsFilterConsts = true; }
      else if (strcmp(op, "mask") == 0) { helper = "pimage_mask"; takesArgs = true; }
      else if (strcmp(op, "save") == 0) { helper = "pimage_save"; takesArgs = true; }
      else if (strcmp(op, "beginDraw") == 0) { helper = "beginGraphics"; byVal = true; }
      else if (strcmp(op, "endDraw") == 0) {
        // endGraphics(void) pops whatever canvas is active
        int j = i + 3;
        if (j + 1 < num_tokens && tokens[j].type == TOKEN_SYMBOL &&
            strcmp(tokens[j].text, "(") == 0 &&
            tokens[j+1].type == TOKEN_SYMBOL && strcmp(tokens[j+1].text, ")") == 0) j += 2;
        printf("endGraphics()");
        i = j;
        continue;
      }
      else if (strcmp(op, "get") == 0) {
        // img.get()                -> pimage_get_copy(&img)
        // img.get(x,y)             -> pimage_get_px(&img, x, y)      [color]
        // img.get(x,y,w,h)         -> pimage_get_region(&img, x..h)  [region]
        int j = i + 3;
        if (j < num_tokens && tokens[j].type == TOKEN_SYMBOL &&
            strcmp(tokens[j].text, "(") == 0) {
          int close = j, depth = 1;
          for (int q = j + 1; q < num_tokens && depth > 0; q++) {
            if (tokens[q].type == TOKEN_SYMBOL && strcmp(tokens[q].text, "(") == 0) depth++;
            else if (tokens[q].type == TOKEN_SYMBOL && strcmp(tokens[q].text, ")") == 0) { depth--; if (depth == 0) close = q; }
          }
          // count top-level commas to pick arity
          int commas = 0, d2 = 0;
          for (int q = j + 1; q < close; q++) {
            Token t = tokens[q];
            if (t.type == TOKEN_SYMBOL && strcmp(t.text, "(") == 0) d2++;
            else if (t.type == TOKEN_SYMBOL && strcmp(t.text, ")") == 0) d2--;
            else if (t.type == TOKEN_SYMBOL && strcmp(t.text, ",") == 0 && d2 == 0) commas++;
          }
          if (commas == 0) {
            printf("pimage_get_copy(&%s)", current.text);
          } else {
            const char *fn = (commas >= 3) ? "pimage_get_region" : "pimage_get_px";
            printf("%s(&%s,", fn, current.text);
            for (int q = j + 1; q < close; q++) {
              Token t = tokens[q];
              printf("%s", t.text);
              if (t.type != TOKEN_DOT && t.type != TOKEN_OPERATOR &&
                  !(t.type == TOKEN_SYMBOL && strchr("([{,", t.text[0]))) printf(" ");
            }
            printf(")");
          }
          i = close + 1;
          continue;
        }
      }
      if (helper != NULL) {
        int j = i + 3;
        bool refArg = false;
        if (takesArgs) {
          if (!(j < num_tokens && tokens[j].type == TOKEN_SYMBOL &&
                strcmp(tokens[j].text, "(") == 0)) goto not_pimage_op;
          j++;
        }
        if (byVal) printf("%s(%s", helper, current.text);
        else       printf("%s(&(%s)", helper, current.text);
        if (takesArgs) {
          // mask(src) passes the source by address when it's a plain variable
          refArg = strcmp(op, "mask") == 0 &&
                   tokens[j].type == TOKEN_IDENTIFIER &&
                   (tokens[j+1].type == TOKEN_SYMBOL &&
                    (strcmp(tokens[j+1].text, ",") == 0 || strcmp(tokens[j+1].text, ")") == 0));
          if (refArg) printf(", &(");
          else printf(", ");
          bool sawComma = false;
          int depth = 1;
          while (j < num_tokens && depth > 0) {
            Token t = tokens[j];
            if (t.type == TOKEN_SYMBOL && strcmp(t.text, "(") == 0) depth++;
            if (t.type == TOKEN_SYMBOL && strcmp(t.text, ")") == 0) {
              depth--;
              if (depth == 0) {
                if (refArg) printf(")");
                if (mapsFilterConsts && !sawComma) printf(", 0"); // filter(GRAY) default arg
                printf(")");
                j++;
                break;
              }
            }
            if (t.type == TOKEN_SYMBOL && strcmp(t.text, ",") == 0 && depth == 1) sawComma = true;
            if (mapsFilterConsts && t.type == TOKEN_IDENTIFIER) {
              const char *names[] = { "BLUR", "GRAY", "INVERT", "THRESHOLD", "POSTERIZE", "OPAQUE" };
              const char *mapped[] = { "_PIMAGE_BLUR", "_PIMAGE_GRAY", "_PIMAGE_INVERT",
                                       "_PIMAGE_THRESHOLD", "_PIMAGE_POSTERIZE", "_PIMAGE_OPAQUE" };
              bool hit = false;
              for (int k = 0; k < 6; k++)
                if (strcmp(t.text, names[k]) == 0) { printf("%s", mapped[k]); hit = true; break; }
              if (!hit) printf("%s", t.text);
            } else {
              printf("%s", t.text);
            }
            if (t.type != TOKEN_DOT && t.type != TOKEN_OPERATOR &&
                !(t.type == TOKEN_SYMBOL && strchr("([{,", t.text[0]))) printf(" ");
            j++;
          }
        } else {
          // pixels/loadPixels/updatePixels may carry an empty call "()"
          if (j + 1 < num_tokens && tokens[j].type == TOKEN_SYMBOL &&
              strcmp(tokens[j].text, "(") == 0 &&
              tokens[j+1].type == TOKEN_SYMBOL && strcmp(tokens[j+1].text, ")") == 0) j += 2;
          printf(")"); // close the helper call (receiver's ")" already emitted)
        }
        i = j;
        continue;
      }
      }
      not_pimage_op:;
    }

    // 8d. expand(arr, n) keeps contents, zero-fills the tail like Java
    if (current.type == TOKEN_IDENTIFIER && !pendingExpand &&
        strcmp(current.text, "expand") == 0 &&
        (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0 &&
        tokens[i+2].type == TOKEN_IDENTIFIER)
    {
      snprintf(expandVar, sizeof(expandVar), "%s", tokens[i+2].text);
      printf("_pde_expand((void **)&%s", expandVar);
      pendingExpand = true;
      expandDepth = 1;
      i += 3;
      continue;
    }

    // track the expand( ... ) call so its closing ")" gains the elem size
    if (pendingExpand) {
      if (current.type == TOKEN_SYMBOL && strcmp(current.text, "(") == 0) {
        expandDepth++;
      } else if (current.type == TOKEN_SYMBOL && strcmp(current.text, ")") == 0) {
        if (--expandDepth == 0) {
          printf(", sizeof(*%s))", expandVar);
          pendingExpand = false;
          i++;
          continue;
        }
      }
    }

    // 9. Standardize environment entry point formatting
    if (current.type == TOKEN_KEYWORD && (strcmp(current.text, "setup") == 0 || strcmp(current.text, "draw") == 0)) {
      if (i + 2 < num_tokens && strcmp(tokens[i+1].text, "(") == 0 && strcmp(tokens[i+2].text, ")") == 0) {
        printf("%s(void)", current.text);
        i += 3;
        continue;
      }
    }

    // 10a. Java labeled loop: "loop: for(...)" -> register goto target,
    //      drop the label (C has no labeled break)
    if (current.type == TOKEN_IDENTIFIER && (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, ":") == 0 &&
        ((tokens[i+2].type == TOKEN_KEYWORD &&
          (strcmp(tokens[i+2].text, "for") == 0 || strcmp(tokens[i+2].text, "while") == 0)) ||
         (tokens[i+2].type == TOKEN_IDENTIFIER && strcmp(tokens[i+2].text, "do") == 0)))
    {
      if (labelCount < 16) {
        snprintf(labelStack[labelCount].name, MAX_TOKEN_TEXT, "%s", current.text);
        labelStack[labelCount].depth = braceDepth;
        labelCount++;
      }
      i += 2;
      continue;
    }

    // 10b. labeled break -> goto the target emitted after the loop body
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "break") == 0 &&
        (i + 2 < num_tokens) && tokens[i+1].type == TOKEN_IDENTIFIER)
    {
      bool known = false;
      for (int q = 0; q < labelCount; q++) {
        if (strcmp(labelStack[q].name, tokens[i+1].text) == 0) { known = true; break; }
      }
      if (known) {
        printf("goto __%s_brk;\n", tokens[i+1].text); // \n keeps #line valid
        i += (tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, ";") == 0) ? 3 : 2;
        continue;
      }
    }

    // Skip non-constant global initializer (= expr) at file scope
    // '=' is tokenized as TOKEN_OPERATOR (see tokenizer), so length-check the
    // text regardless of the token type tag.
    if (current.type == TOKEN_OPERATOR &&
        strcmp(current.text, "=") == 0 &&
        braceDepth == 0 &&
        skipInit && skipInit[i]) {
      while (i < num_tokens &&
             !(tokens[i].type == TOKEN_SYMBOL &&
               strcmp(tokens[i].text, ";") == 0))
        i++;
      // the ';' terminates the (now) declaration-only global; the trailing
      // newline keeps the following #line directive on its own line so gcc
      // never sees '; #line' glued together.
      if (i < num_tokens) { printf(";\n"); i++; }
      continue;
    }

    if (current.type == TOKEN_EOF) {
      break;
    }

    printf("%s", current.text);

    // brace depth drives labeled-break target placement ("} __lbl_brk: ;")
    if (current.type == TOKEN_SYMBOL && strcmp(current.text, "}") == 0) {
      if (braceDepth > 0) braceDepth--;
      while (labelCount > 0 && labelStack[labelCount-1].depth == braceDepth) {
        printf("\n__%s_brk: ;", labelStack[--labelCount].name);
      }
    } else if (current.type == TOKEN_SYMBOL && strcmp(current.text, "{") == 0) {
      braceDepth++;
    }

    // Spacing rules
    if (current.type == TOKEN_SYMBOL && strcmp(current.text, ";") == 0) {
      printf("\n");
    } else if (current.type == TOKEN_OPERATOR || (current.type == TOKEN_SYMBOL && strcmp(current.text, ",") == 0)) {
      printf(" ");
    } else if (i + 1 < num_tokens && tokens[i+1].type != TOKEN_DOT && tokens[i+1].type != TOKEN_SYMBOL && current.type != TOKEN_DOT) {
      printf(" ");
    }

    i++;
  }

  // Phase 6: user-class method/constructor definitions, emitted after the
  // sketch body (so they may call sketch-level helpers) and before main().
  emit_class_definitions();

  // _pde_global_init() assigns non-constant global initializers
  // that cannot be used as C file-scope constant expressions.
  if (nInitTexts > 0) {
    printf("\nstatic void _pde_global_init(void) {\n");
    for (int g = 0; g < nInitTexts; g++)
      printf("    %s;\n", initTexts[g]);
    printf("}\n");
  }

  printf("\n\nint main(void) {\n");
  // Processing's settings() (window size etc.) runs before setup()
  for (int p = 1; p < num_tokens; p++) {
    if (tokens[p].type == TOKEN_IDENTIFIER && strcmp(tokens[p].text, "settings") == 0 &&
        tokens[p-1].type == TOKEN_KEYWORD && strcmp(tokens[p-1].text, "void") == 0 &&
        p + 1 < num_tokens && tokens[p+1].type == TOKEN_SYMBOL && strcmp(tokens[p+1].text, "(") == 0 &&
        p + 2 < num_tokens && tokens[p+2].type == TOKEN_SYMBOL && strcmp(tokens[p+2].text, ")") == 0 &&
        p + 3 < num_tokens && tokens[p+3].type == TOKEN_SYMBOL && strcmp(tokens[p+3].text, "{") == 0) {
      printf("    settings();\n");
      break;
    }
  }
  if (nInitTexts > 0)
    printf("    _pde_global_init();\n");
  printf("    setup();\n");
  // a sketch that never calls size() still needs a window (Processing always
  // shows a canvas, even an empty setup)
  printf("    if (_windowInit == 0) size3(640, 480, \"Processing Ray\");\n");
  printf("    while (!WindowShouldClose() && !_exitRequested) {\n");
  printf("        beginDraw();\n");
  printf("        if (_loopRunning || _redrawPending) {\n");
  printf("            _redrawPending = false;\n");
  printf("            draw();\n");
  printf("        }\n");
  printf("        endDraw();\n");
  printf("    }\n");
  printf("    destroyProcessing();\n");
  printf("    CloseWindow();\n");
  printf("    return 0;\n");
  printf("}\n");

  free(tokens);
  free(skipInit);
  free(source_buffer);
  return 0;
}