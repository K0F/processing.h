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
    if (strchr(";{},()[]", source[i])) {
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

    // operators
    if (strchr("+-*/=!><&|", source[i])) {
      int len = 0;
      tokens[t_count].type = TOKEN_OPERATOR;
      tokens[t_count].text[len++] = source[i++];
      if (source[i] == '=' || source[i] == '&' || source[i] == '|') {
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

  // Print framework header and native little-endian A-B-G-R color bit-packer
  printf("#include \"processing.h\"\n");
  printf("#define pack_color(r, g, b) (((uint32_t)255 << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))\n\n");

  int i = 0;
  while (i < num_tokens) {
    Token current = tokens[i];

    // 1. Transform Processing Array declarations: "int[] mm" -> "int mm[]"
    if (is_type_token(current) &&
        (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_SYMBOL && strcmp(tokens[i+1].text, "[") == 0 &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "]") == 0 &&
        tokens[i+3].type == TOKEN_IDENTIFIER)
    {
      const char *type_out = current.text;
      if (strcmp(type_out, "boolean") == 0) type_out = "bool";
      if (strcmp(type_out, "color") == 0) type_out = "uint32_t";
      printf("%s %s[]", type_out, tokens[i+3].text);
      i += 4;
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

    // 3. Size function translation
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "size") == 0) {
      printf("size_converted");
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

    // 8. PVector object method rewriting
    if (current.type == TOKEN_IDENTIFIER &&
        (i + 3 < num_tokens) &&
        tokens[i+1].type == TOKEN_DOT &&
        tokens[i+2].type == TOKEN_IDENTIFIER && strcmp(tokens[i+2].text, "add") == 0 &&
        tokens[i+3].type == TOKEN_SYMBOL && strcmp(tokens[i+3].text, "(") == 0)
    {
      char obj_name[MAX_TOKEN_TEXT];
      strcpy(obj_name, current.text);
      printf("%s = pvector_add(%s, ", obj_name, obj_name);
      i += 4;
      continue;
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
  printf("    setup();\n");
  printf("    while (!WindowShouldClose()) {\n");
  printf("        beginDraw();\n");
  printf("        draw();\n");
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