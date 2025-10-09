#ifndef LEXER_H
#define LEXER_H
#include <stddef.h>
#include <stdio.h>

typedef enum {
    TOK_NUMBER,
    TOK_STR,
    TOK_ID,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACK,
    TOK_RBRACK,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_DOT,
    TOK_CONCAT,
    TOK_VARARG,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_IDIV,
    TOK_ASSIGN,
    TOK_MOD,
    TOK_POW,
    TOK_LEN,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,
    TOK_KW_AND, 
    TOK_KW_BREAK, 
    TOK_KW_DO, 
    TOK_KW_ELSE, 
    TOK_KW_ELSEIF, 
    TOK_KW_END,
    TOK_KW_FALSE, 
    TOK_KW_FOR, 
    TOK_KW_FUNCTION, 
    TOK_KW_GOTO, 
    TOK_KW_IF, 
    TOK_KW_IN,
    TOK_KW_LOCAL, 
    TOK_KW_NIL, 
    TOK_KW_NOT, 
    TOK_KW_OR, 
    TOK_KW_REPEAT, 
    TOK_KW_RETURN,
    TOK_KW_THEN, 
    TOK_KW_TRUE, 
    TOK_KW_UNTIL, 
    TOK_KW_WHILE,
    TOK_KW_TRY,
    TOK_KW_CATCH,
    TOK_KW_FINALLY,
    TOK_ERROR,
    TOK_UNKNOWN,
    TOK_EOF
} TokenType;

typedef struct {
    TokenType type;
    char *lexeme;  
    size_t len;
    size_t line;
} Token;

static int line_no = 1;

typedef struct {
    const char *word;
    TokenType token;
} Keyword;

static const Keyword keywords[] = {
    {"and",      TOK_KW_AND},
    {"break",    TOK_KW_BREAK},
    {"do",       TOK_KW_DO},
    {"else",     TOK_KW_ELSE},
    {"elseif",   TOK_KW_ELSEIF},
    {"end",      TOK_KW_END},
    {"false",    TOK_KW_FALSE},
    {"for",      TOK_KW_FOR},
    {"function", TOK_KW_FUNCTION},
    {"goto",     TOK_KW_GOTO},
    {"if",       TOK_KW_IF},
    {"in",       TOK_KW_IN},
    {"local",    TOK_KW_LOCAL},
    {"nil",      TOK_KW_NIL},
    {"not",      TOK_KW_NOT},
    {"or",       TOK_KW_OR},
    {"repeat",   TOK_KW_REPEAT},
    {"return",   TOK_KW_RETURN},
    {"then",     TOK_KW_THEN},
    {"true",     TOK_KW_TRUE},
    {"until",    TOK_KW_UNTIL},
    {"while",    TOK_KW_WHILE},
    {NULL, TOK_UNKNOWN}
};
extern Token *tokens;
Token next(FILE *f);

#endif
