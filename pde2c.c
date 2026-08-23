#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "pde2c.h"

#define MAX_LINE_LENGTH 1024

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
      else if (t.type == TOKEN_KEYWORD || t.type == TOKEN_STRING || t.type == TOKEN_EOF) break;
      /* IDENTIFIER / NUMBER: part of the operand, keep scanning */
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
        snprintf(tokens[t_count].text, MAX_TOKEN_TEXT, "0x%02X%02X%02XFF",
                 (unsigned)((255u << 24) | (b << 16) | (g << 8) | r));
        tokens[t_count].line = current_line;
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
      t_count++;
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
      t_count++;
      continue;
    }

    i++;
  }

  tokens[t_count].type = TOKEN_EOF;
  strcpy(tokens[t_count].text, "EOF");
  tokens[t_count].line = current_line;
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

  // Java-style % (float-capable) -> pmod(a,b)
  {
    Token *rewritten;
    int new_count = rewrite_modulo(tokens, num_tokens, &rewritten);
    free(tokens);
    tokens = rewritten;
    num_tokens = new_count;
  }

  // Print framework header and native little-endian A-B-G-R color bit-packer
  printf("#include \"processing.h\"\n");
  printf("#define pack_color(r, g, b) (((uint32_t)255 << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))\n\n");

  // ---- forward declaration pre-scan -------------------------------------
  // Scan for "TYPE NAME(params) {" definitions and emit C prototypes so
  // sketches can call helper functions defined after their call sites.
  {
    for (int p = 1; p < num_tokens; p++) {
      // "TYPE NAME(" or array-typed "TYPE[] NAME(" (p stays on NAME)
      bool arrayReturn = false;
      if (!is_function_return_type(tokens[p - 1]) || tokens[p].type != TOKEN_IDENTIFIER) {
        if (!(p >= 3 &&
              tokens[p].type == TOKEN_IDENTIFIER &&
              tokens[p - 1].type == TOKEN_SYMBOL && strcmp(tokens[p - 1].text, "]") == 0 &&
              tokens[p - 2].type == TOKEN_SYMBOL && strcmp(tokens[p - 2].text, "[") == 0 &&
              is_function_return_type(tokens[p - 3]))) {
          continue;
        }
        arrayReturn = true;
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
      if (!(tokens[close + 1].type == TOKEN_SYMBOL && strcmp(tokens[close + 1].text, "{") == 0)) continue;

      {
        char retType[64];
        snprintf(retType, sizeof(retType), "%s", tokens[arrayReturn ? p - 3 : p - 1].text);
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

  int i = 0;
  int pendingBracketClose = 0;
  bool pendingCallocType = false;
  char lastArrayType[32] = "";
  while (i < num_tokens) {
    Token current = tokens[i];

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

    // 3d. Common math aliases
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "abs") == 0 &&
        (i + 1 < num_tokens) && tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "(") == 0)
    {
      printf("fabsf(");
      i += 2;
      continue;
    }
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

    // 5b. Transform "new TYPE[n]" -> "(TYPE *)calloc(n, sizeof(TYPE))"
    // (PVector keeps its zeroing helper; primitives get calloc)
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "new") == 0 &&
        (i + 3 < num_tokens) &&
        (tokens[i+1].type == TOKEN_IDENTIFIER || tokens[i+1].type == TOKEN_KEYWORD) &&
        strcmp(tokens[i+1].text, "PVector") != 0 &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "[") == 0 &&
        !(tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "]") == 0))
    {
      snprintf(lastArrayType, sizeof(lastArrayType), "%s", tokens[i+1].text);
      printf("(%s *)_pde_array_new(", tokens[i+1].text);
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

    // 9. Standardize environment entry point formatting
    if (current.type == TOKEN_KEYWORD && (strcmp(current.text, "setup") == 0 || strcmp(current.text, "draw") == 0)) {
      if (i + 2 < num_tokens && strcmp(tokens[i+1].text, "(") == 0 && strcmp(tokens[i+2].text, ")") == 0) {
        printf("%s(void)", current.text);
        i += 3;
        continue;
      }
    }

    if (current.type == TOKEN_EOF) {
      break;
    }

    printf("%s", current.text);

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