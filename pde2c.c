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

int tokenize(const char *source, Token *tokens) {
  int t_count = 0;
  int i = 0;
  int current_line = 1;

  // whitespaces 
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
      i++; // header
      while (source[i] != '"' && source[i] != '\0') i++;
      int len = i - start + 1;

      tokens[t_count].type = TOKEN_STRING;
      strncpy(tokens[t_count].text, &source[start], len);
      tokens[t_count].text[len] = '\0';
      tokens[t_count].line = current_line;
      t_count++;
      i++; // footer
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
      // Pokud za tečkou není číslo, je to metoda (např. 10.add) - stop číslu
      break;
    }
    tokens[t_count].text[len++] = source[i++];
  }
  tokens[t_count].text[len] = '\0';
  tokens[t_count].line = current_line;
  t_count++;
  continue;
}

// keywords
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

// weird symbols
i++;
}

tokens[t_count].type = TOKEN_EOF;
strcpy(tokens[t_count].text, "EOF");
tokens[t_count].line = current_line;
return t_count;
}

void replace_token(char *line, const char *old_tok, const char *new_tok) {
  char buffer[MAX_LINE_LENGTH];
  char *pos;

  while ((pos = strstr(line, old_tok)) != NULL) {
    int len_before = pos - line;
    strncpy(buffer, line, len_before);
    buffer[len_before] = '\0';

    strcat(buffer, new_tok);

    strcat(buffer, pos + strlen(old_tok));

    strcpy(line, buffer);
  }
}

void parse_vector_methods(char *line) {
  char buffer[MAX_LINE_LENGTH];
  char *dot_pos;

  while ((dot_pos = strstr(line, ".add(")) != NULL) {
    char *obj_start = dot_pos;
    while (obj_start > line && *(obj_start - 1) != ' ' && *(obj_start - 1) != '\t' && *(obj_start - 1) != '(' && *(obj_start - 1) != ',') {
      obj_start--;
    }

    int obj_len = dot_pos - obj_start;
    char obj_name[64];
    strncpy(obj_name, obj_start, obj_len);
    obj_name[obj_len] = '\0';

    char *arg_start = dot_pos + 5; // ".add(" len
    char *arg_end = strchr(arg_start, ')');
    if (!arg_end) break;

    int arg_len = arg_end - arg_start;
    char arg_name[64];
    strncpy(arg_name, arg_start, arg_len);
    arg_name[arg_len] = '\0';

    int len_before = obj_start - line;
    strncpy(buffer, line, len_before);
    buffer[len_before] = '\0';

    char replacement[256];
    sprintf(replacement, "%s = pvector_add(%s, %s)", obj_name, obj_name, arg_name);
    strcat(buffer, replacement);
    strcat(buffer, arg_end + 1);

    strcpy(line, buffer);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Použití: %s <soubor.pde>\n", argv[0]);
    return 1;
  }

  // 1. OTEVŘENÍ A NAČTENÍ SOUBORU
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

  // 2. ALOKACE A SPUŠTĚNÍ TOKENIZERU
  Token *tokens = malloc(sizeof(Token) * MAX_TOKENS);
  if (!tokens) {
    free(source_buffer);
    return 1;
  }

  int num_tokens = tokenize(source_buffer, tokens);

  printf("#include \"processing.h\"\n\n");

  int i = 0;
  while (i < num_tokens) {
    Token current = tokens[i];

    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "size") == 0) {
      printf("size_converted");
      i++;
      continue;
    }

    // Transformace "new PVector(x, y)" -> "pvector(x, y)"
    // Hledáme pattern: IDENTIFIER ("new") -> IDENTIFIER ("PVector") -> SYMBOL ("(")
    if (current.type == TOKEN_IDENTIFIER && strcmp(current.text, "new") == 0 &&
        (i + 2 < num_tokens) &&
        tokens[i+1].type == TOKEN_IDENTIFIER && strcmp(tokens[i+1].text, "PVector") == 0 &&
        tokens[i+2].type == TOKEN_SYMBOL && strcmp(tokens[i+2].text, "(") == 0)
    {
      printf("pvector");
      i += 2; // Přeskočíme "new" a "PVector", v dalším kroku loopu se zpracuje "("
      continue;
    }

    if (current.type == TOKEN_KEYWORD && strcmp(current.text, "boolean") == 0) {
      printf("bool ");
      i++;
      continue;
    }

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
