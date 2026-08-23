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
    "if", "else", "for", "while", "return", "true", "false", "setup", "draw"
  };
  int num_keywords = sizeof(keywords) / sizeof(keywords[0]);
  for (int i = 0; i < num_keywords; i++) {
    if (strcmp(str, keywords[i]) == 0) return true;
  }
  return false;
}

// Helper to identify data types for array conversions
bool is_type_token(Token t) {
  return (t.type == TOKEN_KEYWORD &&
          (strcmp(t.text, "int") == 0 || strcmp(t.text, "float") == 0 ||
           strcmp(t.text, "char") == 0 || strcmp(t.text, "double") == 0 ||
           strcmp(t.text, "boolean") == 0 || strcmp(t.text, "color") == 0));
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
  static const char *names[] = { "keyPressed", "keyReleased", "mousePressed", "mouseReleased" };
  for (int i = 0; i < 4; i++) {
    if (strcmp(name, names[i]) == 0) return "_event";
  }
  return NULL;
}

// Map Java-ish param/return types to C inside prototypes and signatures
void emit_c_type(const char *javaType) {
  if (strcmp(javaType, "boolean") == 0)      printf("bool");
  else if (strcmp(javaType, "color") == 0)   printf("uint32_t");
  else if (strcmp(javaType, "String") == 0)  printf("const char *");
  else                                       printf("%s", javaType);
}

// same mapping as emit_c_type but into a caller buffer
const char *emit_c_type_str(const char *javaType) {
  static const char *boolean_t = "bool";
  static const char *color_t = "uint32_t";
  static const char *string_t = "const char *";
  if (strcmp(javaType, "boolean") == 0)     return boolean_t;
  if (strcmp(javaType, "color") == 0)       return color_t;
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

// does the operand evaluate to a string? (string literal or _pde_charat call)
static bool concat_operand_stringy(Token *t, int start, int end) {
  for (int j = start; j < end; j++) {
    if (t[j].type == TOKEN_STRING) return true;
    if (t[j].type == TOKEN_IDENTIFIER && strcmp(t[j].text, "_pde_charat") == 0) return true;
    if (t[j].type == TOKEN_IDENTIFIER && strcmp(t[j].text, "str") == 0) return true;
  }
  return false;
}

// Fold "lit" + a + "b" chains into _pde_cat(...) calls. Right-assoc nesting
// (concatenation is associative). Numeric operands are wrapped in
// _pde_numf(...); string-valued operands pass through untouched.
int rewrite_concat(Token *tokens, int num_tokens, Token **out_tokens) {
  Token *out = malloc(sizeof(Token) * (num_tokens * 8 + 64));
  int o = 0;
  int i = 0;
  while (i < num_tokens) {
    if (!(tokens[i].type == TOKEN_STRING && i + 1 < num_tokens &&
          tokens[i+1].type == TOKEN_OPERATOR &&
          strcmp(tokens[i+1].text, "+") == 0)) {
      out[o++] = tokens[i++];
      continue;
    }

    // collect chain segments: seg 0 is the leading string literal
    int segStart[64], segEnd[64];
    bool segStr[64];
    int m = 0;
    segStart[m] = i; segEnd[m] = i + 1; segStr[m] = true; m++;
    int j = i + 1;
    while (j < num_tokens && tokens[j].type == TOKEN_OPERATOR &&
           strcmp(tokens[j].text, "+") == 0 && m < 64) {
      int s = j + 1;
      int e = concat_operand_end(tokens, num_tokens, s);
      if (e <= s) break;
      segStart[m] = s; segEnd[m] = e;
      segStr[m] = (e == s + 1 && tokens[s].type == TOKEN_STRING);
      m++;
      j = e;
    }

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
    PDE_COPYRANGE(segStart[0], segEnd[0]);
    for (int k = 1; k < m; k++) {
      PDE_EMIT(",", TOKEN_SYMBOL);
      if (k < m - 1) { PDE_EMIT("_pde_cat", TOKEN_IDENTIFIER); PDE_EMIT("(", TOKEN_SYMBOL); }
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
  }
  *out_tokens = out;
  return o;
}

// PVector methods that reassign the receiver: v.add(w) -> v = pvector_add(v, w)
bool pvector_mutator_method(const char *name) {
  static const char *mutators[] = { "add", "sub", "mult", "div",
                                    "normalize", "limit", "setMag", "rotate" };
  for (int i = 0; i < 8; i++) {
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


int tokenize(const char *source, Token *tokens) {
  int t_count = 0;
  int i = 0;
  int current_line = 1;
  int current_file = pdeRegisterFile("sketch.pde");

  while (source[i] != '\0') {
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

    // numbers
    if (isdigit(source[i])) {
      int len = 0;
      tokens[t_count].type = TOKEN_NUMBER;
      while (isdigit(source[i]) || source[i] == '.' || source[i] == 'f') {
        if (source[i] == '.' && !isdigit(source[i+1])) {
          break;
        }
        tokens[t_count].text[len++] = source[i++];
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
      while (isalnum(source[i]) || source[i] == '_') {
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

  Token *tokens = malloc(sizeof(Token) * MAX_TOKENS);
  if (!tokens) {
    free(source_buffer);
    return 1;
  }

  int num_tokens = tokenize(source_buffer, tokens);

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
          int m = nargs; if (m > 4) m = 4; if (m < 1) m = 1;
          snprintf(buf, sizeof(buf), "colorMode%d", m);
          name = buf;
        }
        else if (strcmp(name, "smooth") == 0 && nargs > 0) name = "smooth1";
      }
      printf("%s(", name);
      i += 2;
      continue;
    }

    // 1. Array declarations "TYPE[] name":
    //    - "= {" literal init stays C-style:  TYPE name[] = {
    //    - otherwise pointer style (heap ref): TYPE *name
    if (is_type_token(current) &&
        (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "[") == 0 &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "]") == 0 &&
        tokens[i+3].type == TOKEN_IDENTIFIER)
    {
      const char *type_out = current.text;
      if (strcmp(type_out, "boolean") == 0) type_out = "bool";
      if (strcmp(type_out, "color") == 0) type_out = "uint32_t";
      bool braceInit = (i + 6 < num_tokens &&
                        tokens[i+4].type == TOKEN_OPERATOR && strcmp(tokens[i+4].text, "=") == 0 &&
                        tokens[i+5].type == TOKEN_SYMBOL && strcmp(tokens[i+5].text, "{") == 0);
      if (braceInit) printf("%s %s[] = {", type_out, tokens[i+3].text);
      else           printf("%s *%s", type_out, tokens[i+3].text);
      i += braceInit ? 6 : 4;
      // additional Java declarators in the same statement: "TYPE a[], b[], c;"
      // array ones become extra pointers; plain scalars fall back to default
      while (!braceInit &&
             (i + 3 < num_tokens) &&
             tokens[i].type == TOKEN_SYMBOL && strcmp(tokens[i].text, ",") == 0 &&
             tokens[i+1].type == TOKEN_IDENTIFIER &&
             tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "[") == 0 &&
             tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "]") == 0)
      {
        printf(", *%s", tokens[i+1].text);
        i += 4;
      }
      continue;
    }

    // 1b. Name-first array declarations: "TYPE name[]" (Java allows both
    //     orders) incl. parameter lists "void f(float arr[], int b[])" and
    //     declarator lists "TYPE a[], b[];" -> pointers / brace init kept
    if ((is_type_token(current) ||
         (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "PVector") == 0)) &&
        (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_IDENTIFIER &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "[") == 0 &&
        tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "]") == 0)
    {
      const char *type_out = emit_c_type_str(current.text);
      bool braceInit = (i + 6 < num_tokens &&
                        tokens[i+4].type == TOKEN_OPERATOR && strcmp(tokens[i+4].text, "=") == 0 &&
                        tokens[i+5].type == TOKEN_SYMBOL && strcmp(tokens[i+5].text, "{") == 0);
      if (braceInit) printf("%s %s[] = {", type_out, tokens[i+1].text);
      else           printf("%s *%s", type_out, tokens[i+1].text);
      i += braceInit ? 6 : 4;
      // further declarators: ", b[]" (same type) or ", float b[]" (retyped,
      // happens in parameter lists)
      while (!braceInit &&
             (i + 3 < num_tokens) &&
             tokens[i].type == TOKEN_SYMBOL && strcmp(tokens[i].text, ",") == 0)
      {
        bool retyped = is_type_token(tokens[i+1]) ||
                       (tokens[i+1].type == TOKEN_IDENTIFIER &&
                        strcmp(tokens[i+1].text, "PVector") == 0);
        int nameIdx = retyped ? i + 2 : i + 1;
        if (tokens[nameIdx].type != TOKEN_IDENTIFIER) break;
        if (!(tokens[nameIdx+1].type == TOKEN_SYMBOL &&
              strcmp(tokens[nameIdx+1].text, "[") == 0 &&
              tokens[nameIdx+2].type == TOKEN_SYMBOL &&
              strcmp(tokens[nameIdx+2].text, "]") == 0)) break;
        if (retyped) printf(", %s *%s", emit_c_type_str(tokens[i+1].text), tokens[nameIdx].text);
        else         printf(", *%s", tokens[nameIdx].text);
        i = nameIdx + 3;
      }
      continue;
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
      i += 5;
      continue;
    }

    // 3. Size function translation (drops the renderer arg: size(w,h,OPENGL))
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "size") == 0 &&
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
        (tokens[i+1].type == TOKEN_IDENTIFIER ||
         (tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "[") == 0)))
    {
      printf("const char *");
      i++;
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

    // 5b. Transform "new TYPE[n]" -> "(TYPE *)_pde_array_new(n, sizeof(TYPE))"
    // (PVector constructor form handled above; this is the array form)
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "new") == 0 &&
        (i + 3 < num_tokens) &&
        (tokens[i+1].type == TOKEN_IDENTIFIER || tokens[i+1].type == TOKEN_KEYWORD) &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "[") == 0 &&
        !(tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "]") == 0))
    {
      const char *elemType = emit_c_type_str(tokens[i+1].text);
      snprintf(lastArrayType, sizeof(lastArrayType), "%s", elemType);
      printf("(%s *)_pde_array_new(", elemType);
      pendingBracketClose++;
      pendingCallocType = true; // closing "]" emits ", sizeof(TYPE))"
      i += 3;
      continue;
    }

    // closing "]" of an array allocation becomes ")"
    if (pendingBracketClose > 0 &&
        current.type == TOKEN_SYMBOL && strcmp(current.text, "]") == 0)
    {
      if (pendingCallocType) {
        printf(", sizeof(%s))", lastArrayType);
        pendingCallocType = false;
      } else {
        printf(")");
      }
      pendingBracketClose--;
      i++;
      continue;
    }

    // 6. Map boolean keyword
    if (current.type == TOKEN_KEYWORD && strcmp(current.text, "boolean") == 0) {
      printf("bool ");
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

    // 8. PVector object method rewriting (table-driven)
    //    mutators: v.add(w)      -> v = pvector_add(v, w)
    //    accessors: v.mag()      -> pvector_mag(v)
    if (current.type == TOKEN_IDENTIFIER &&
        (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_DOT &&
        tokens[i+2].type == TOKEN_IDENTIFIER &&
        tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "(") == 0)
    {
      const char *obj = current.text;
      const char *method = tokens[i+2].text;

      if (strcmp(obj, "PVector") == 0 && strcmp(method, "fromAngle") == 0) {
        printf("pvector_fromAngle(");
        i += 4;
        continue;
      }

      char objName[MAX_TOKEN_TEXT];
      strcpy(objName, obj);

      // generic: only rewrite when the method is part of the PVector API
      if ((pvector_mutator_method(method) || pvector_accessor_method(method))) {
        if (pvector_mutator_method(method)) {
          printf("%s = pvector_%s(%s, ", objName, method, objName);
        } else {
          printf("pvector_%s(%s, ", method, objName);
        }
        i += 4;
        continue;
      }
    }

    // 8b. Java array .length -> C sizeof expression
    //     (true arrays only: brace-initialized decls emit "TYPE name[]";
    //      arrays decayed to pointer params give a wrong count — known limit)
    if (current.type == TOKEN_IDENTIFIER &&
        (i + 2 < num_tokens) &&
        tokens[i+1].type == TOKEN_DOT &&
        tokens[i+2].type == TOKEN_IDENTIFIER &&
        strcmp(tokens[i+2].text, "length") == 0)
    {
      printf("(int)(sizeof(%s) / sizeof(*%s))", current.text, current.text);
      i += 3;
      continue;
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

    if (current.type == TOKEN_EOF) {
      break;
    }

    printf("%s", current.text);

    // brace depth drives labeled-break target placement ("} __lbl_brk: ;")
    if (current.type == TOKEN_SYMBOL && strcmp(current.text, "}") == 0) {
      braceDepth--;
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
  printf("    setup();\n");
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
  free(source_buffer);
  return 0;
}