// src/main.c
#define _POSIX_C_SOURCE 200809L 
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/util.h"
#include "../include/interpreter.h"

#define LUAX_VERSION "1.0.2"

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

static void print_help(const char *progname) {
    printf("Usage: %s [options] [file|code]\n\n", progname);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --version  Show version information\n\n");
    printf("Arguments:\n");
    printf("  file           Execute a .lua or .lx file\n");
    printf("  code           Execute code string directly\n");
    printf("  (none)         Start interactive REPL\n\n");
    printf("Examples:\n");
    printf("  %s script.lua          # Run a file\n", progname);
    printf("  %s 'print(\"hi\")'       # Run code string\n", progname);
    printf("  %s                     # Start REPL\n", progname);
}

static void print_version(void) {
    printf("LuaX version %s\n", LUAX_VERSION);
}

static int execute_code(FILE *fp) {
    free_tokens();
    
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
                return 1;
            }
            tokens = tmp;
            tokenCap = newCap;
        }
        tokens[tokenCount++] = t;
    }

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
    int result = interpret(program);

    /* ===== CLEANUP ===== */
    ast_free(program);
    parser_destroy(p);
    
    return result;
}

// Forward declare exec_stmt_repl from interpreter
extern void exec_stmt_repl(VM *vm, AST *n);
extern VM *vm_create_repl(void);

static void run_repl(void) {
    printf("LuaX %s REPL - Press Ctrl+D or type 'exit' to quit\n", LUAX_VERSION);
    
    // Create a persistent VM for the REPL session
    VM *vm = vm_create_repl();
    if (!vm) {
        fprintf(stderr, "Failed to create REPL VM\n");
        return;
    }
    
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    
    while (1) {
        printf("> ");
        fflush(stdout);
        
        nread = getline(&line, &len, stdin);
        if (nread == -1) {
            printf("\n");
            break; /* EOF (Ctrl+D) */
        }
        
        /* Remove trailing newline */
        if (nread > 0 && line[nread-1] == '\n') {
            line[nread-1] = '\0';
            nread--;
        }
        
        /* Skip empty lines */
        if (nread == 0) continue;
        
        /* Check for exit command */
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            break;
        }
        
        /* Parse and execute the line using the persistent VM */
        FILE *fp = open_string_as_FILE(line);
        if (!fp) {
            fprintf(stderr, "Error: failed to process input\n");
            continue;
        }
        
        // Lex the input
        free_tokens();
        for (;;) {
            Token t = next(fp);
            if (t.type == TOK_EOF) break;
            
            if (tokenCount >= tokenCap) {
                int newCap = tokenCap ? tokenCap * 2 : 8;
                Token *tmp = (Token *)realloc(tokens, newCap * sizeof(Token));
                if (!tmp) {
                    perror("realloc");
                    free_tokens();
                    fclose(fp);
                    continue;
                }
                tokens = tmp;
                tokenCap = newCap;
            }
            tokens[tokenCount++] = t;
        }
        fclose(fp);
        
        // Parse the input
        Parser *p = parser_create(tokens, tokenCount);
        ASTVec stmts = (ASTVec){0};
        while (parser_curr(p).type != TOK_EOF) {
            AST *s = statement(p);
            if (!s) break;
            if (parser_curr(p).type == TOK_EOF && p->had_error) break;
            astvec_push(&stmts, s);
        }
        
        if (stmts.count == 0) {
            parser_destroy(p);
            free_tokens();
            continue;
        }
        
        AST *program = ast_make_block(stmts, tokenCount ? tokens[tokenCount-1].line : 1);
        
        // Execute using persistent VM
        exec_stmt_repl(vm, program);
        
        // Cleanup
        ast_free(program);
        parser_destroy(p);
        free_tokens();
    }
    
    free(line);
    // Note: We're not freeing the VM here to avoid complex cleanup
    // In a production system, you'd want proper VM cleanup
}

int main(int argc, char **argv) {
    FILE *fp = NULL;
    char *stdin_buf = NULL;

    /* Handle flags */
    if (argc >= 2) {
        const char *arg = argv[1];
        
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
        
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        
        /* File or code string */
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
        
        int result = execute_code(fp);
        fclose(fp);
        free_tokens();
        return result;
        
    } else {
        /* No args -> start REPL */
        run_repl();
        return 0;
    }
}
