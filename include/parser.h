#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include "lexer.h"   /* Token, TokenType */

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================
 * AST Core
 * ========================== */

typedef struct AST AST;

typedef enum {
    AST_INVALID = 0,

    /* Expressions */
    AST_NIL,
    AST_BOOL,
    AST_NUMBER,
    AST_STRING,
    AST_IDENT,
    AST_UNARY,           /* op expr */
    AST_BINARY,          /* lhs op rhs */
    AST_ASSIGN,          /* simple: ident = expr (kept for convenience) */
    AST_ASSIGN_LIST,     /* lvals... = rvals... */
    AST_CALL,            /* callee(args...) */
    AST_INDEX,           /* target[expr] */
    AST_FIELD,           /* target.name */
    AST_TABLE,           /* { ... } */
    AST_FUNCTION,        /* function (...) body end (anonymous literal) */

    /* Statements */
    AST_STMT_EXPR,       /* expr; (semicolon optional in Lua) */
    AST_VAR,             /* local name (= init)? (single for simplicity) */
    AST_BLOCK,           /* sequence of statements */
    AST_IF,              /* if cond then then_blk [else_blk] end (elseif folded) */
    AST_WHILE,           /* while cond do body end */
    AST_REPEAT,          /* repeat body until cond */
    AST_FOR_NUM,         /* for name = a,b[,c] do body end */
    AST_FOR_IN,          /* for n1[,n2]* in explist do body end */
    AST_RETURN,          /* return explist? (stores list) */
    AST_BREAK,           /* break */
    AST_GOTO,            /* goto label */
    AST_LABEL,           /* ::label:: */
    AST_FUNC_STMT,       /* function namechain(params) body end */
    AST_LOCAL_FUNC,
    AST_TRY,
    AST_COMPOUND_ASSIGN
} ASTKind;

typedef enum {
    OP_NONE = 0,
    /* Unary */
    OP_NEG,    /* - */
    OP_NOT,    /* not */
    OP_LEN,    /* # */

    /* Binary */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    OP_CONCAT,             /* .. */
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_AND, OP_OR,
    OP_IDIV
} OpKind;

typedef struct {
  AST *target;   /* Name | Field | Index */
  OpKind op;     /* OP_ADD or OP_SUB (later more) */
  AST *value;    /* rhs expression */
} ASTCompoundAssign;
typedef struct {
    AST   **items;
    size_t  count, cap;
} ASTVec;

/* ==========================
 * AST Node Definition
 * ========================== */

struct AST {
    ASTKind kind;
    int     line;

    union {
        /* Literals & identifiers */
        struct { bool        v; }              bval;
        struct { double      v; }              nval;
        struct { const char *s; }              sval;
        struct { const char *name; }           ident;

        /* Unary / Binary */
        struct { OpKind op; AST *expr; }       unary;
        struct { OpKind op; AST *lhs; AST *rhs; } binary;

        /* Simple assignment (identifier = expr) */
        struct { AST *lhs_ident; AST *rhs; }   assign;

        /* Multi assignment */
        struct { ASTVec lvals; ASTVec rvals; } massign;

        /* Calls & selectors */
        struct { AST *callee; ASTVec args; }   call;
        struct { AST *target; AST *index; }    index;   /* t[expr] */
        struct { AST *target; const char *field; } field; /* t.name */

        /* Table constructor */
        struct { ASTVec keys; ASTVec values; } table;  /* key NULL => array-style */

        /* Function literal */
        struct {
            ASTVec params;   /* identifiers as AST* (AST_IDENT) */
            bool   vararg;   /* has ... */
            AST   *body;     /* AST_BLOCK */
        } fn;

        /* Function statements (named/local) */
        struct {
            bool   is_local;
            AST   *name;     /* name chain as FIELDs (e.g., a.b.c or a:b) or IDENT */
            ASTVec params;   /* AST_IDENT nodes */
            bool   vararg;
            AST   *body;     /* AST_BLOCK */
        } fnstmt;

        /* Statements */
        struct { AST *expr; }                  stmt_expr;

        /* NOTE: added is_close for Lua 5.4 'to-be-closed' locals (local <close> x = ...) */
        struct { bool is_local; bool is_close; const char *name; AST *init; } var;

        struct { ASTVec stmts; }               block;
        struct { AST *cond; AST *then_blk; AST *else_blk; } ifs;
        struct { AST *cond; AST *body; }       whiles;
        struct { AST *body; AST *cond; }       repeatstmt;
        struct { const char *var; AST *start; AST *end; AST *step; AST *body; } fornum;
        struct { ASTVec names; ASTVec iters; AST *body; } forin;
        struct { ASTVec values; }              ret;
        struct { const char *label; }          go;      /* goto */
        struct { const char *label; }          label;   /* ::label:: */
   struct {
    AST *try_block;     /* AST_BLOCK */
    AST *catch_block;   /* AST_BLOCK or NULL */
    const char *catch_var; /* variable name for the caught exception */
    AST *finally_block; /* AST_BLOCK or NULL */
} trycatch;

    } as;
};

/* ==========================
 * Parser wrapper
 * ========================== */

typedef struct {
    Token *toks;
    int    count;
    int    pos;
    bool   had_error;

    /* robust error handling */
    int    err_count;   /* number of syntax errors seen */
    bool   panic;       /* in panic mode until we synchronize */
} Parser;

/* ==========================
 * Public API (tiny)
 * ========================== */

/* Construct/destroy a parser around an existing token array */
Parser *parser_create(Token *tokens, int count);
void    parser_destroy(Parser *p);

/* Parse one expression or one statement */
AST *expression(Parser *p);
AST *statement(Parser *p);

/* Your executor/VM entry (stubbed in parser.c) */
int interpret(AST *root);

/* ==========================
 * Optional inline helpers (handy in main.c tests)
 * ========================== */
static inline Token parser_curr(Parser *p) {
    return (p->pos < p->count) ? p->toks[p->pos]
        : (Token){TOK_EOF, NULL, 0, p->count ? p->toks[p->count-1].line : 1};
}
static inline Token parser_peek(Parser *p, int la) {
    int i = p->pos + la;
    Token cur = parser_curr(p);
    return (i < p->count) ? p->toks[i] : (Token){TOK_EOF, NULL, 0, cur.line};
}
static inline bool parser_match(Parser *p, TokenType t) {
    if (parser_curr(p).type == t) { p->pos++; return true; }
    return false;
}

/* ==========================
 * Constructors & utils used by the parser
 * ========================== */

/* Leaf & simple nodes */
AST *ast_make_nil(int line);
AST *ast_make_bool(bool v, int line);
AST *ast_make_number(double v, int line);
AST *ast_make_string(const char *s, int line);
AST *ast_make_ident(const char *name, int line);
AST *ast_make_unary(OpKind op, AST *e, int line);
AST *ast_make_binary(OpKind op, AST *l, AST *r, int line);

/* Assignments */
AST *ast_make_assign(AST *lhs_ident, AST *rhs, int line);         /* ident = expr */
AST *ast_make_assign_list(ASTVec lvals, ASTVec rvals, int line);  /* lvals... = rvals... */

/* Calls & selectors */
AST *ast_make_call(AST *callee, ASTVec args, int line);
AST *ast_make_index(AST *target, AST *index, int line);
AST *ast_make_field(AST *target, const char *field, int line);

/* Table & function literals */
AST *ast_make_table(ASTVec keys, ASTVec values, int line);
AST *ast_make_function(ASTVec params, bool vararg, AST *body, int line);

/* Function statements */
AST *ast_make_func_stmt(bool is_local, AST *name_chain, ASTVec params, bool vararg, AST *body, int line);

/* Statements */

/* Extended constructor with is_close flag */
AST *ast_make_var_ex(bool is_local, bool is_close, const char *name, AST *init, int line);

/* Back-compat: declaration only; implemented in parser.c */
AST *ast_make_var(bool is_local, const char *name, AST *init, int line);

AST *ast_make_block(ASTVec statements, int line);
AST *ast_make_if(AST *cond, AST *then_blk, AST *else_blk, int line);
AST *ast_make_while(AST *cond, AST *body, int line);
AST *ast_make_repeat(AST *body, AST *cond, int line);
AST *ast_make_for_num(const char *var, AST *start, AST *end, AST *step, AST *body, int line);
AST *ast_make_for_in(ASTVec names, ASTVec iters, AST *body, int line);
AST *ast_make_return_list(ASTVec values, int line);
AST *ast_make_break(int line);
AST *ast_make_goto(const char *label, int line);
AST *ast_make_label(const char *label, int line);

/* Vector & memory helpers */
void  astvec_push(ASTVec *v, AST *node);
void  ast_free(AST *n);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PARSER_H */
