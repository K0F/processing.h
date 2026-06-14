#define MAX_TOKEN_TEXT 256
#define MAX_TOKENS 10000

typedef enum {
  TOKEN_KEYWORD,    // void, float, boolean, int...
  TOKEN_IDENTIFIER, // názvy proměnných, funkcí (pos, vel, setup...)
  TOKEN_NUMBER,     // 10, 3.14, 0.5f
  TOKEN_OPERATOR,   // +, -, *, /, =, ==
  TOKEN_SYMBOL,     // ;, {, }, (, ), ,
  TOKEN_DOT,        // Tečka pro metody (.)
  TOKEN_STRING,     // "text v uvozovkách"
  TOKEN_EOF         // Konec souboru
} TokenType;

typedef struct {
  TokenType type;
  char text[MAX_TOKEN_TEXT];
  int line;
} Token;


