#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/lexer.h"


// Peek the next character without consuming it
static int peek(FILE *fp) {
    int c = fgetc(fp);
    if (c != EOF) ungetc(c, fp);
    return c;
}
static TokenType lookup_keyword(const char *word) {
    for (int i = 0; keywords[i].word; i++) {
        if (strcmp(word, keywords[i].word) == 0) {
            return keywords[i].token;
        }
    }
    return TOK_ID;
}

/* ===== helpers for dynamic buffer ===== */
#define OOM() do { fprintf(stderr, "out of memory\n"); exit(1); } while (0)
#define GBUF_INIT() \
    size_t cap = 32, n = 0; \
    char *buf = (char*)malloc(cap); \
    if (!buf) OOM();
#define GBUF_PUSH(CH) do { \
    if (n >= cap - 1) { \
        cap *= 2; \
        char *tmp = (char*)realloc(buf, cap); \
        if (!tmp) { free(buf); OOM(); } \
        buf = tmp; \
    } \
    buf[n++] = (char)(CH); \
} while (0)

/* ===== token construction ===== */
#define MAKE_TOK(TTYPE, BUF, N) do { \
    Token _t; \
    _t.type = (TTYPE); \
    _t.len  = (N); \
    _t.line = token_line; \
    _t.lexeme = NULL; \
    if ((BUF) && (N)) { \
        _t.lexeme = (char *)malloc((N) + 1); \
        if (!_t.lexeme) OOM(); \
        memcpy(_t.lexeme, (BUF), (N)); \
        _t.lexeme[(N)] = '\0'; \
    } \
    return _t; \
} while (0)

/* ===== long bracket utilities (Lua style) ===== */
static int read_long_open(FILE *fp, int *eq) {
    int c;
    long pos = ftell(fp);
    int equals = 0;
    while ((c = fgetc(fp)) == '=') equals++;
    if (c == '[') { *eq = equals; return 1; }
    if (c != EOF) fseek(fp, pos, SEEK_SET);
    return 0;
}

static int matches_long_close(FILE *fp, int eq) {
    long pos = ftell(fp);
    int c, k = 0;
    while (k < eq) {
        c = fgetc(fp);
        if (c != '=') { fseek(fp, pos, SEEK_SET); return 0; }
        k++;
    }
    c = fgetc(fp);
    if (c == ']') return 1;
    fseek(fp, pos, SEEK_SET);
    return 0;
}

static void skip_long_comment(FILE *fp, int eq) {
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n') { line_no++; continue; }
        if (c == ']' && matches_long_close(fp, eq)) return;
    }
}

/* return string in out_buf/out_len; set *closed=1 if found matching ]=...=], else 0 */
static void read_long_string(FILE *fp, int eq, char **out_buf, size_t *out_len, int *closed) {
    GBUF_INIT();
    int c;
    *closed = 0;

    /* Skip optional first newline as in Lua */
    long pos_after_open = ftell(fp);
    c = fgetc(fp);
    if (c != '\n' && c != '\r') {
        if (c != EOF) fseek(fp, pos_after_open, SEEK_SET);
    } else {
        if (c == '\r') {
            int d = fgetc(fp);
            if (d != '\n' && d != EOF) ungetc(d, fp);
        }
        line_no++;
    }

    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n') { GBUF_PUSH('\n'); line_no++; continue; }
        if (c == ']' && matches_long_close(fp, eq)) { *closed = 1; break; }
        GBUF_PUSH(c);
    }

    buf[n] = '\0';
    *out_buf = buf;
    *out_len = n;
}

/* ===== main lexer ===== */
Token next(FILE *fp) {
    int c;

    #define IS_IDENT_START(ch) (isalpha((unsigned char)(ch)) || (ch) == '_')
    #define IS_IDENT_CHAR(ch)  (isalnum((unsigned char)(ch)) || (ch) == '_')

    /* Skip whitespace and comments */
    for (;;) {
        c = fgetc(fp);
        if (c == '\n') { line_no++; continue; }
        if (c == EOF) { Token eof = {TOK_EOF, NULL, 0, line_no}; return eof; }

        if (isspace(c)) continue;

        /* Comments */
        if (c == '-') {
            int d = fgetc(fp);
            if (d == '-') {
                int e = fgetc(fp);
                if (e == '[') {
                    int eq;
                    if (read_long_open(fp, &eq)) { skip_long_comment(fp, eq); continue; }
                    else { if (e != EOF) ungetc(e, fp); }
                } else {
                    if (e != EOF) ungetc(e, fp);
                }
                while ((c = fgetc(fp)) != EOF && c != '\n') { /* skip */ }
                if (c == '\n') line_no++;
                continue;
            } else {
                if (d != EOF) ungetc(d, fp);
                break;
            }
        }
        break;
    }

    int token_line = line_no;

    /* Single/compound tokens */
    switch (c) {
        case '(': MAKE_TOK(TOK_LPAREN, "(", 1);
        case ')': MAKE_TOK(TOK_RPAREN, ")", 1);
        case '{': MAKE_TOK(TOK_LBRACE,"{", 1);
        case '}': MAKE_TOK(TOK_RBRACE, "}", 1);

        case '[': {
            int eq;
            if (read_long_open(fp, &eq)) {
                char *s; size_t n; int closed;
                read_long_string(fp, eq, &s, &n, &closed);
                if (!closed) {
                    static const char *msg = "unterminated long string literal";
                    MAKE_TOK(TOK_ERROR, msg, strlen(msg));
                }
                MAKE_TOK(TOK_STR, s, n);
            }
            MAKE_TOK(TOK_LBRACK, "[", 1);
        }

        case ']': MAKE_TOK(TOK_RBRACK, "]", 1);
        case ',': MAKE_TOK(TOK_COMMA,  ",", 1);
        case ':': MAKE_TOK(TOK_COLON,  ":", 1);
        case ';': MAKE_TOK(TOK_SEMICOLON, ";", 1);
        case '+': MAKE_TOK(TOK_PLUS,   "+", 1);
        case '*': MAKE_TOK(TOK_STAR,   "*", 1);
case '/': {
    int d = peek(fp);
    if (d == '/') {
        fgetc(fp); // consume the second '/'
        // skip rest of line (comment)
        while ((c = fgetc(fp)) != EOF && c != '\n');
        if (c == '\n') line_no++;
        return next(fp); // recursively get the next token
    } else {
        Token t;
        t.type = TOK_SLASH;
        t.lexeme = strdup("/");
        t.len = 1;
        t.line = token_line;
        return t;
    }
}
        case '%': MAKE_TOK(TOK_MOD,    "%", 1);
        case '#': MAKE_TOK(TOK_LEN,    "#", 1);
        case '^': MAKE_TOK(TOK_POW,    "^", 1);
        case '-': MAKE_TOK(TOK_MINUS,  "-", 1);

        case '=': {
            int d = fgetc(fp);
            if (d == '=') { MAKE_TOK(TOK_EQ, "==", 2); }
            if (d != EOF) ungetc(d, fp);
            MAKE_TOK(TOK_ASSIGN, "=", 1);
        }

        case '~': {
            int d = fgetc(fp);
            if (d == '=') { MAKE_TOK(TOK_NE, "~=", 2); }
            if (d != EOF) ungetc(d, fp);
            char tmp[2] = {(char)c, '\0'};
            MAKE_TOK(TOK_UNKNOWN, tmp, 1);
        }

        case '<': {
            int d = fgetc(fp);
            if (d == '=') { MAKE_TOK(TOK_LE, "<=", 2); }
            if (d != EOF) ungetc(d, fp);
            MAKE_TOK(TOK_LT, "<", 1);
        }

        case '>': {
            int d = fgetc(fp);
            if (d == '=') { MAKE_TOK(TOK_GE, ">=", 2); }
            if (d != EOF) ungetc(d, fp);
            MAKE_TOK(TOK_GT, ">", 1);
        }

        case '.': {
            int d = fgetc(fp);
            if (d == '.') {
                int d2 = fgetc(fp);
                if (d2 == '.') { MAKE_TOK(TOK_VARARG, "...", 3); }
                if (d2 != EOF) { ungetc(d2, fp); }
                MAKE_TOK(TOK_CONCAT, "..", 2);
            } else if (isdigit(d)) {
                /* .123 -> normalize to 0.xxx */
                GBUF_INIT();
                GBUF_PUSH('0');
                GBUF_PUSH('.');
                GBUF_PUSH(d);
                int ch;
                while ((ch = fgetc(fp)) != EOF && isdigit(ch)) GBUF_PUSH(ch);
                if (ch == 'e' || ch == 'E') {
                    GBUF_PUSH(ch);
                    int s = fgetc(fp);
                    if (s == '+' || s == '-') { GBUF_PUSH(s); s = fgetc(fp); }
                    if (isdigit(s)) {
                        GBUF_PUSH(s);
                        while ((ch = fgetc(fp)) != EOF && isdigit(ch)) GBUF_PUSH(ch);
                    }
                }
                /* optional 'f' suffix: consume it if present */
                if (ch == 'f' || ch == 'F') { /* eat it */ }
                else if (ch != 0 && ch != EOF) ungetc(ch, fp);
                MAKE_TOK(TOK_NUMBER, buf, n);
            } else {
                if (d != EOF) ungetc(d, fp);
                MAKE_TOK(TOK_DOT, ".", 1);
            }
        }

        case '"':
        case '\'': {
            int quote = c;
            GBUF_INIT();
            int ch, escaped = 0;
            int closed = 0;
            while ((ch = fgetc(fp)) != EOF) {
                /* raw newline inside a short-quoted string is an error in Lua */
                if (!escaped && ch == '\n') {
                    line_no++;
                    static const char *msg = "unterminated string literal";
                    MAKE_TOK(TOK_ERROR, msg, strlen(msg));
                }

                if (!escaped && ch == quote) { closed = 1; break; }
                if (!escaped && ch == '\\') { escaped = 1; continue; }

                if (escaped) {
                    switch (ch) {
                        case 'n': ch = '\n'; break;
                        case 't': ch = '\t'; break;
                        case 'r': ch = '\r'; break;
                        case 'b': ch = '\b'; break;
                        case 'f': ch = '\f'; break;
                        case 'a': ch = '\a'; break;
                        case 'v': ch = '\v'; break;
                        case '"': ch = '"';  break;
                        case '\'': ch = '\''; break;
                        case '\\': ch = '\\'; break;
                        case '0': ch = '\0'; break;
                        case 'x': {
                            char h1 = fgetc(fp), h2 = fgetc(fp);
                            if (isxdigit((unsigned char)h1) && isxdigit((unsigned char)h2)) {
                                char hexbuf[3] = {h1, h2, '\0'};
                                ch = (char)strtol(hexbuf, NULL, 16);
                            } else {
                                if (h2 != EOF) ungetc(h2, fp);
                                if (h1 != EOF) ungetc(h1, fp);
                                GBUF_PUSH('\\'); ch = 'x';
                            }
                            break;
                        }
                        case 'u': {
                            int brace = fgetc(fp);
                            if (brace == '{') {
                                unsigned code = 0; int hc;
                                while ((hc = fgetc(fp)) != EOF && hc != '}') {
                                    if (!isxdigit((unsigned char)hc)) break;
                                    code = code * 16 + (isdigit(hc) ? (hc - '0') : (tolower(hc) - 'a' + 10));
                                }
                                ch = (char)(code & 0xFF);
                            } else {
                                if (brace != EOF) ungetc(brace, fp);
                                GBUF_PUSH('\\'); ch = 'u';
                            }
                            break;
                        }
                        default: GBUF_PUSH('\\'); break;
                    }
                    escaped = 0;
                }
                GBUF_PUSH(ch);
            }

            if (!closed) {
                static const char *msg = "unterminated string literal";
                MAKE_TOK(TOK_ERROR, msg, strlen(msg));
            }

            MAKE_TOK(TOK_STR, buf, n);
        }
    }

    /* Backslash alone */
    if (c == '\\') {
        char tmp[2] = {'\\', '\0'};
        MAKE_TOK(TOK_UNKNOWN, tmp, 1);
    }

    /* identifiers */
    if ((isalpha((unsigned char)c)) || c == '_') {
        GBUF_INIT();
        GBUF_PUSH(c);
        int ch;
        while ((ch = fgetc(fp)) != EOF && (isalnum((unsigned char)ch) || ch == '_')) {
            GBUF_PUSH(ch);
        }
        if (ch != EOF) ungetc(ch, fp);
        buf[n] = '\0';
        TokenType token_type = lookup_keyword(buf);
        MAKE_TOK(token_type, buf, n);
    }

    /* numbers (decimal or hex, including hex floats) */
    if (isdigit((unsigned char)c) || (c == '0')) {
        GBUF_INIT();
        GBUF_PUSH(c);

        if (c == '0') {
            int nc = fgetc(fp);
            if (nc == 'x' || nc == 'X') {
                GBUF_PUSH(nc);
                int ch, has_digits = 0;
                while ((ch = fgetc(fp)) != EOF && isxdigit((unsigned char)ch)) { GBUF_PUSH(ch); has_digits = 1; }
                if (!has_digits) { if (ch != EOF) ungetc(ch, fp); MAKE_TOK(TOK_UNKNOWN, buf, n); }

                int is_real = 0;
                if (ch == '.') {
                    int peek = fgetc(fp);
                    if (isxdigit((unsigned char)peek)) {
                        is_real = 1;
                        GBUF_PUSH('.');
                        GBUF_PUSH(peek);
                        while ((ch = fgetc(fp)) != EOF && isxdigit((unsigned char)ch)) GBUF_PUSH(ch);
                    } else {
                        if (peek != EOF) ungetc(peek, fp);
                        ungetc('.', fp);
                        ch = 0;
                    }
                }
                if (ch == 'p' || ch == 'P') {
                    is_real = 1;
                    GBUF_PUSH(ch);
                    int s = fgetc(fp);
                    if (s == '+' || s == '-') { GBUF_PUSH(s); s = fgetc(fp); }
                    if (isdigit((unsigned char)s)) {
                        GBUF_PUSH(s);
                        while ((ch = fgetc(fp)) != EOF && isdigit((unsigned char)ch)) GBUF_PUSH(ch);
                    } else if (s != EOF) {
                        ungetc(s, fp);
                    }
                }
                /* optional 'f' suffix: eat it */
                if (ch == 'f' || ch == 'F') { /* eat */ }
                else if (ch != 0 && ch != EOF) ungetc(ch, fp);
                MAKE_TOK(TOK_NUMBER, buf, n);
            } else {
                if (nc != EOF) ungetc(nc, fp);
            }
        }

        int ch;
        while ((ch = fgetc(fp)) != EOF && isdigit((unsigned char)ch)) GBUF_PUSH(ch);

        int is_real = 0;
        if (ch == '.') {
            int peek = fgetc(fp);
            if (isdigit((unsigned char)peek)) {
                is_real = 1;
                GBUF_PUSH('.');
                GBUF_PUSH(peek);
                while ((ch = fgetc(fp)) != EOF && isdigit((unsigned char)ch)) GBUF_PUSH(ch);
            } else {
                if (peek != EOF) ungetc(peek, fp);
                ungetc('.', fp);
                ch = 0;
            }
        }
        if (ch == 'e' || ch == 'E') {
            is_real = 1;
            GBUF_PUSH(ch);
            int s = fgetc(fp);
            if (s == '+' || s == '-') { GBUF_PUSH(s); s = fgetc(fp); }
            if (isdigit((unsigned char)s)) {
                GBUF_PUSH(s);
                while ((ch = fgetc(fp)) != EOF && isdigit((unsigned char)ch)) GBUF_PUSH(ch);
            } else if (s != EOF) {
                ungetc(s, fp);
            }
        }
        /* optional 'f' suffix: eat it */
        if (ch == 'f' || ch == 'F') { /* eat */ }
        else if (ch != 0 && ch != EOF) ungetc(ch, fp);
        (void)is_real; /* kept for clarity; we always emit TOK_NUMBER */
        MAKE_TOK(TOK_NUMBER, buf, n);
    }

    char tmp[2] = {(char)c, '\0'};
    MAKE_TOK(TOK_UNKNOWN, tmp, 1);

    #undef OOM
    #undef GBUF_INIT
    #undef GBUF_PUSH
    #undef MAKE_TOK
    #undef IS_IDENT_START
    #undef IS_IDENT_CHAR
}
