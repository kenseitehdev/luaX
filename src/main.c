// src/main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../include/lexer.h"
#include "../include/parser.h"

Token *tokens = NULL;
int tokenCount = 0;
int tokenCap = 0;

static const char *TokenTypeNames[] = {
    [TOK_NUMBER]    = "NUMBER",
    [TOK_STR]       = "STR",
    [TOK_ID]        = "ID",
    [TOK_LPAREN]    = "LPAREN",
    [TOK_RPAREN]    = "RPAREN",
    [TOK_LBRACK]    = "LBRACK",
    [TOK_RBRACK]    = "RBRACK",
    [TOK_LBRACE]    = "LBRACE",
    [TOK_RBRACE]    = "RBRACE",
    [TOK_COMMA]     = "COMMA",
    [TOK_COLON]     = "COLON",
    [TOK_SEMICOLON] = "SEMICOLON",
    [TOK_DOT]       = "DOT",
    [TOK_CONCAT]    = "CONCAT",
    [TOK_VARARG]    = "VARARG",
    [TOK_PLUS]      = "PLUS",
    [TOK_MINUS]     = "MINUS",
    [TOK_STAR]      = "STAR",
    [TOK_SLASH]     = "SLASH",
    [TOK_IDIV]      = "IDIV",
    [TOK_ASSIGN]    = "ASSIGN",
    [TOK_MOD]       = "MOD",
    [TOK_POW]       = "POW",
    [TOK_LEN]       = "LEN",
    [TOK_EQ]        = "EQ",
    [TOK_NE]        = "NE",
    [TOK_LT]        = "LT",
    [TOK_GT]        = "GT",
    [TOK_LE]        = "LE",
    [TOK_GE]        = "GE",
    [TOK_KW_AND]    = "AND",
    [TOK_KW_BREAK]  = "BREAK",
    [TOK_KW_DO]     = "DO",
    [TOK_KW_ELSE]   = "ELSE",
    [TOK_KW_ELSEIF] = "ELSEIF",
    [TOK_KW_END]    = "END",
    [TOK_KW_FALSE]  = "FALSE",
    [TOK_KW_FOR]    = "FOR",
    [TOK_KW_FUNCTION] = "FUNCTION",
    [TOK_KW_GOTO]   = "GOTO",
    [TOK_KW_IF]     = "IF",
    [TOK_KW_IN]     = "IN",
    [TOK_KW_LOCAL]  = "LOCAL",
    [TOK_KW_NIL]    = "NIL",
    [TOK_KW_NOT]    = "NOT",
    [TOK_KW_OR]     = "OR",
    [TOK_KW_REPEAT] = "REPEAT",
    [TOK_KW_RETURN] = "RETURN",
    [TOK_KW_THEN]   = "THEN",
    [TOK_KW_TRUE]   = "TRUE",
    [TOK_KW_UNTIL]  = "UNTIL",
    [TOK_KW_WHILE]  = "WHILE",
    [TOK_KW_TRY]    = "TRY",
    [TOK_KW_CATCH]  = "CATCH",
    [TOK_KW_FINALLY]= "FINALLY",
    [TOK_ERROR]     = "ERROR",
    [TOK_UNKNOWN]   = "UNKNOWN",
    [TOK_EOF]       = "EOF",
};

static void free_tokens(void){
    for (int i = 0; i < tokenCount; i++) free(tokens[i].lexeme);
    free(tokens);
    tokens = NULL; tokenCount = tokenCap = 0;
}

static int has_ext(const char *path, const char *ext) {
    size_t n = strlen(path), m = strlen(ext);
    return n >= m && strcmp(path + (n - m), ext) == 0;
}
static int allowed_ext(const char *path) {
    return has_ext(path, ".lua") || has_ext(path, ".lx");
}

/* Turn a string into a seekable FILE* (tmpfile fallback if fmemopen not available) */
static FILE* open_string_as_FILE(const char *code) {
    if (!code) code = "";
#if defined(_GNU_SOURCE) || defined(__GLIBC__)
    FILE *f = fmemopen((void*)code, strlen(code), "r");
    if (f) return f;
#endif
    FILE *f = tmpfile();
    if (!f) return NULL;
    size_t len = strlen(code);
    if (len && fwrite(code, 1, len, f) != len) { fclose(f); return NULL; }
    rewind(f);
    return f;
}

/* Read all of stdin into a single malloc'd string */
static char* read_all_stdin(void){
    size_t cap = 8192, n = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) { perror("malloc"); return NULL; }

    for (;;) {
        if (n + 4096 + 1 > cap) {
            size_t newCap = cap * 2;
            char *tmp = (char*)realloc(buf, newCap);
            if (!tmp) { free(buf); perror("realloc"); return NULL; }
            cap = newCap; buf = tmp;
        }
        size_t got = fread(buf + n, 1, 4096, stdin);
        n += got;
        if (got < 4096) {
            if (ferror(stdin)) { free(buf); perror("fread"); return NULL; }
            break; /* EOF */
        }
    }
    buf[n] = '\0';
    return buf;
}

int main(int argc, char **argv) {
    FILE *fp = NULL;
    char *stdin_buf = NULL;

    if (argc >= 2) {
        const char *arg = argv[1];
        if (allowed_ext(arg)) {
            fp = fopen(arg, "r");
            if (!fp) {
                fprintf(stderr, "failed to open input '%s': %s\n", arg, strerror(errno));
                return 1;
            }
        } else {
            /* not a .lua/.lx file -> treat as a literal source string */
            fp = open_string_as_FILE(arg);
            if (!fp) {
                fprintf(stderr, "failed to open input: %s\n", strerror(errno));
                return 1;
            }
        }
    } else {
        /* no args -> read stdin */
        stdin_buf = read_all_stdin();
        if (!stdin_buf) return 1;
        fp = open_string_as_FILE(stdin_buf);
        if (!fp) { fprintf(stderr, "failed to wrap stdin\n"); free(stdin_buf); return 1; }
    }

    /* ===== LEX ===== */
    for (;;) {
        Token t = next(fp);
        if (t.type == TOK_EOF) break;

        if (tokenCount >= tokenCap) {
            int newCap = tokenCap ? tokenCap * 2 : 8;
            Token *tmp = (Token *)realloc(tokens, newCap * sizeof(Token));
            if (!tmp) {
                perror("realloc");
                free_tokens();
                if (fp) fclose(fp);
                free(stdin_buf);
                return 1;
            }
            tokens = tmp;
            tokenCap = newCap;
        }
        tokens[tokenCount++] = t;
    }
    if (fp) fclose(fp);

    /* ===== PARSE ===== */
    Parser *p = parser_create(tokens, tokenCount);
    ASTVec stmts = (ASTVec){0};
    while (parser_curr(p).type != TOK_EOF) {
        AST *s = statement(p);
        if (!s) break;
        if (parser_curr(p).type == TOK_EOF && p->had_error) break;
        astvec_push(&stmts, s);
    }
    AST *program = ast_make_block(stmts, tokenCount ? tokens[tokenCount-1].line : 1);

    /* ===== RUN ===== */
    (void)interpret(program);

    /* ===== CLEANUP ===== */
    ast_free(program);
    parser_destroy(p);
    free_tokens();
    free(stdin_buf);
    return 0;
}
