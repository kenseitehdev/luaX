#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../include/parser.h"

AST* ast_make_try_catch_finally(AST *tryBlock, AST *catchBlock, AST *finallyBlock, char *exceptionVar, int line) {
    AST *node = malloc(sizeof(AST));
    node->kind = AST_TRY;
    node->line = line;
node->as.trycatch.try_block = tryBlock;
node->as.trycatch.catch_block = catchBlock;
node->as.trycatch.finally_block = finallyBlock;
node->as.trycatch.catch_var = exceptionVar ? strdup(exceptionVar) : NULL;
    return node;
}
static void *xmalloc(size_t n){ void *p=malloc(n); if(!p){fprintf(stderr,"OOM\n"); exit(1);} return p; }
static char *xstrdup(const char*s){ size_t n=strlen(s)+1; char *p=xmalloc(n); memcpy(p,s,n); return p; }

void astvec_push(ASTVec *v, AST *node){
  if(!v->cap){ v->cap=8; v->items=xmalloc(v->cap*sizeof(AST*)); }
  else if(v->count==v->cap){ v->cap<<=1; v->items=realloc(v->items,v->cap*sizeof(AST*)); }
  v->items[v->count++]=node;
}

static AST *node_new(ASTKind k,int line){ AST*n=xmalloc(sizeof(*n)); memset(n,0,sizeof(*n)); n->kind=k; n->line=line; return n; }
AST *ast_make_nil(int l){return node_new(AST_NIL,l);}
AST *ast_make_bool(bool v,int l){AST*n=node_new(AST_BOOL,l); n->as.bval.v=v; return n;}
AST *ast_make_number(double v,int l){AST*n=node_new(AST_NUMBER,l); n->as.nval.v=v; return n;}
AST *ast_make_string(const char*s,int l){AST*n=node_new(AST_STRING,l); n->as.sval.s=xstrdup(s?s:""); return n;}
AST *ast_make_ident(const char*name,int l){AST*n=node_new(AST_IDENT,l); n->as.ident.name=xstrdup(name?name:""); return n;}
AST *ast_make_unary(OpKind op,AST*e,int l){AST*n=node_new(AST_UNARY,l); n->as.unary.op=op; n->as.unary.expr=e; return n;}
AST *ast_make_binary(OpKind op,AST*l,AST*r,int ln){AST*n=node_new(AST_BINARY,ln); n->as.binary.lhs=l; n->as.binary.rhs=r; n->as.binary.op=op; return n;}
AST *ast_make_assign(AST*lhs,AST*rhs,int l){AST*n=node_new(AST_ASSIGN,l); n->as.assign.lhs_ident=lhs; n->as.assign.rhs=rhs; return n;}
AST *ast_make_assign_list(ASTVec L, ASTVec R, int l){AST*n=node_new(AST_ASSIGN_LIST,l); n->as.massign.lvals=L; n->as.massign.rvals=R; return n;}
AST *ast_make_call(AST*callee,ASTVec args,int l){AST*n=node_new(AST_CALL,l); n->as.call.callee=callee; n->as.call.args=args; return n;}
AST *ast_make_index(AST*t,AST*i,int l){AST*n=node_new(AST_INDEX,l); n->as.index.target=t; n->as.index.index=i; return n;}
AST *ast_make_field(AST*t,const char*name,int l){AST*n=node_new(AST_FIELD,l); n->as.field.target=t; n->as.field.field=xstrdup(name); return n;}
AST *ast_make_table(ASTVec K,ASTVec V,int l){AST*n=node_new(AST_TABLE,l); n->as.table.keys=K; n->as.table.values=V; return n;}
AST *ast_make_function(ASTVec ps,bool vararg,AST*body,int l){AST*n=node_new(AST_FUNCTION,l); n->as.fn.params=ps; n->as.fn.vararg=vararg; n->as.fn.body=body; return n;}
AST *ast_make_func_stmt(bool is_local, AST *name, ASTVec ps, bool vararg, AST *body, int l){AST*n=node_new(AST_FUNC_STMT,l); n->as.fnstmt.is_local=is_local; n->as.fnstmt.name=name; n->as.fnstmt.params=ps; n->as.fnstmt.vararg=vararg; n->as.fnstmt.body=body; return n;}
AST *ast_make_stmt_expr(AST*e,int l){AST*n=node_new(AST_STMT_EXPR,l); n->as.stmt_expr.expr=e; return n;}
AST *ast_make_var(bool is_local,const char*name,AST*init,int l){AST*n=node_new(AST_VAR,l); n->as.var.is_local=is_local; n->as.var.name=xstrdup(name?name:""); n->as.var.init=init; return n;}
AST *ast_make_block(ASTVec s,int l){AST*n=node_new(AST_BLOCK,l); n->as.block.stmts=s; return n;}
AST *ast_make_if(AST*cond,AST*thenb,AST*elseb,int l){AST*n=node_new(AST_IF,l); n->as.ifs.cond=cond; n->as.ifs.then_blk=thenb; n->as.ifs.else_blk=elseb; return n;}
AST *ast_make_while(AST*cond,AST*body,int l){AST*n=node_new(AST_WHILE,l); n->as.whiles.cond=cond; n->as.whiles.body=body; return n;}
AST *ast_make_repeat(AST*body,AST*cond,int l){AST*n=node_new(AST_REPEAT,l); n->as.repeatstmt.body=body; n->as.repeatstmt.cond=cond; return n;}
AST *ast_make_for_num(const char*var,AST*a,AST*b,AST*c,AST*body,int l){AST*n=node_new(AST_FOR_NUM,l); n->as.fornum.var=xstrdup(var); n->as.fornum.start=a; n->as.fornum.end=b; n->as.fornum.step=c; n->as.fornum.body=body; return n;}
AST *ast_make_for_in(ASTVec names,ASTVec iters,AST*body,int l){AST*n=node_new(AST_FOR_IN,l); n->as.forin.names=names; n->as.forin.iters=iters; n->as.forin.body=body; return n;}
AST *ast_make_break(int l){return node_new(AST_BREAK,l);}
AST *ast_make_label(const char*lab,int l){AST*n=node_new(AST_LABEL,l); n->as.label.label=xstrdup(lab); return n;}
AST *ast_make_goto(const char*lab,int l){AST*n=node_new(AST_GOTO,l); n->as.go.label=xstrdup(lab); return n;}
AST *ast_make_return_list(ASTVec vals,int l){AST*n=node_new(AST_RETURN,l); n->as.ret.values=vals; return n;}

void ast_free(AST *n){ /* TODO: implement deep free later */ (void)n; }

Parser *parser_create(Token*tokens,int count){
  Parser*p=xmalloc(sizeof(*p));
  p->toks=tokens;
  p->count=count;
  p->pos=0;
  p->had_error=false;
  p->err_count=0;   /* NEW */
  p->panic=false;   /* NEW */
  return p;
}
void parser_destroy(Parser*p){ free(p); }

static inline Token curr(Parser*p){ return (p->pos<p->count)?p->toks[p->pos]:(Token){TOK_EOF,NULL,0,p->count?p->toks[p->count-1].line:1}; }
static inline Token advance(Parser*p){ Token t=curr(p); if(p->pos<p->count) p->pos++; return t; }
static inline bool  check(Parser*p,TokenType t){ return curr(p).type==t; }
static inline bool  match(Parser*p,TokenType t){ if(check(p,t)){ advance(p); return true;} return false; }

/* ----- robust error reporting & recovery ----- */

#ifndef PARSER_MAX_ERRORS
#define PARSER_MAX_ERRORS 10
#endif

/* Human-friendly token names for diagnostics */
static const char *tok_name(TokenType t){
  switch (t) {
    case TOK_NUMBER:    return "number";
    case TOK_STR:       return "string";
    case TOK_ID:        return "identifier";
    case TOK_LPAREN:    return "'('";
    case TOK_RPAREN:    return "')'";
    case TOK_LBRACK:    return "'['";
    case TOK_RBRACK:    return "']'";
    case TOK_LBRACE:    return "'{'";
    case TOK_RBRACE:    return "'}'";
    case TOK_COMMA:     return "','";
    case TOK_COLON:     return "':'";
    case TOK_SEMICOLON: return "';'";
    case TOK_DOT:       return "'.'";
    case TOK_CONCAT:    return "'..'";
    case TOK_VARARG:    return "'...'";
    case TOK_PLUS:      return "'+'";
    case TOK_MINUS:     return "'-'";
    case TOK_STAR:      return "'*'";
    case TOK_SLASH:     return "'/'";
    case TOK_ASSIGN:    return "'='";
    case TOK_MOD:       return "'%'";
    case TOK_POW:       return "'^'";
    case TOK_LEN:       return "'#'";
    case TOK_EQ:        return "'=='";
    case TOK_NE:        return "'~='";
    case TOK_LT:        return "'<'";
    case TOK_GT:        return "'>'";
    case TOK_LE:        return "'<='";
    case TOK_GE:        return "'>='";
    case TOK_KW_AND:    return "'and'";
    case TOK_KW_BREAK:  return "'break'";
    case TOK_KW_DO:     return "'do'";
    case TOK_KW_ELSE:   return "'else'";
    case TOK_KW_ELSEIF: return "'elseif'";
    case TOK_KW_END:    return "'end'";
    case TOK_KW_FALSE:  return "'false'";
    case TOK_KW_FOR:    return "'for'";
    case TOK_KW_FUNCTION:return "'function'";
    case TOK_KW_GOTO:   return "'goto'";
    case TOK_KW_IF:     return "'if'";
    case TOK_KW_IN:     return "'in'";
    case TOK_KW_LOCAL:  return "'local'";
    case TOK_KW_NIL:    return "'nil'";
    case TOK_KW_NOT:    return "'not'";
    case TOK_KW_OR:     return "'or'";
    case TOK_KW_REPEAT: return "'repeat'";
    case TOK_KW_RETURN: return "'return'";
    case TOK_KW_THEN:   return "'then'";
    case TOK_KW_TRUE:   return "'true'";
    case TOK_KW_UNTIL:  return "'until'";
    case TOK_KW_WHILE:  return "'while'";
    case TOK_EOF:       return "end of file";
    default:            return "token";
  }
}

static void verror_at(Parser *p, int line, const char *fmt, va_list ap){
  if (p->panic) return;  /* suppress cascades until we resync */
  p->had_error = true;
  p->err_count++;
  fprintf(stderr, "[LuaX]: syntax error at line %d: ", line);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  if (p->err_count >= PARSER_MAX_ERRORS) {
    fprintf(stderr, "[LuaX]: too many errors (%d). Aborting parse.\n", p->err_count);
    p->pos = p->count; /* force-EOF to unwind gracefully */
  }
}

static void error_at(Parser *p, int line, const char *fmt, ...){
  va_list ap; va_start(ap, fmt); verror_at(p, line, fmt, ap); va_end(ap);
}

/* Skip to a safe boundary so we can keep parsing after an error */
static void synchronize(Parser *p){
  p->panic = false;
  for (;;) {
    Token t = curr(p);
    if (t.type == TOK_EOF) return;

    switch (t.type) {
      /* statement starters */
      case TOK_KW_IF: case TOK_KW_WHILE: case TOK_KW_REPEAT: case TOK_KW_FOR:
      case TOK_KW_FUNCTION: case TOK_KW_LOCAL: case TOK_KW_RETURN:
      case TOK_KW_GOTO: case TOK_KW_BREAK: case TOK_KW_DO:
        return;

      /* block boundaries also good places to resume */
      case TOK_KW_END: case TOK_KW_ELSE: case TOK_KW_ELSEIF: case TOK_KW_UNTIL:
        return;

      default:
        advance(p); /* discard */
        break;
    }
  }
}

/* Expect with detailed message; enter panic+sync on failure.
   Avoid duplicating "expected ..." if the hint already says that. */
static bool expect(Parser *p, TokenType want, const char *hint_msg){
  Token got = curr(p);
  if (got.type == want) { advance(p); return true; }

  /* suppress repetitive hints like "expected ')'" */
  const char *append = hint_msg;
  if (append) {
    if (!strncasecmp(append, "expected", 8)) {
      append = NULL;
    }
  }

  if (!p->panic) {
    const char *got_name = tok_name(got.type);
    const char *got_lex  = (got.lexeme && *got.lexeme) ? got.lexeme : NULL;
    if (got_lex) {
      error_at(p, got.line, "expected %s, got %s \"%s\"%s%s",
               tok_name(want), got_name, got_lex,
               append ? " — " : "", append ? append : "");
    } else {
      error_at(p, got.line, "expected %s, got %s%s%s",
               tok_name(want), got_name,
               append ? " — " : "", append ? append : "");
    }
  }

  p->panic = true;
  synchronize(p);
  return false;
}

/* precedence */
static int precedence_of(TokenType t){
  switch(t){
    case TOK_KW_OR: return 1; case TOK_KW_AND: return 2;
    case TOK_EQ: case TOK_NE: case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE: return 3;
    case TOK_CONCAT: return 4; /* right-assoc */
    case TOK_PLUS: case TOK_MINUS: return 5;
    case TOK_STAR: case TOK_SLASH: case TOK_IDIV: case TOK_MOD: return 6;
    case TOK_POW: return 7; /* right-assoc */
    default: return 0;
  }
}
static bool right_assoc(TokenType t){ return (t==TOK_POW || t==TOK_CONCAT); }
static OpKind binop(TokenType t){
  switch(t){
    case TOK_PLUS: return OP_ADD; case TOK_MINUS: return OP_SUB; case TOK_STAR: return OP_MUL;
    case TOK_SLASH: return OP_DIV; case TOK_MOD: return OP_MOD; case TOK_POW: return OP_POW;
    case TOK_CONCAT: return OP_CONCAT;
    case TOK_EQ: return OP_EQ; case TOK_NE: return OP_NE; case TOK_LT: return OP_LT;
    case TOK_LE: return OP_LE; case TOK_GT: return OP_GT; case TOK_GE: return OP_GE;
    case TOK_KW_AND: return OP_AND; case TOK_KW_OR: return OP_OR; case TOK_IDIV: return OP_IDIV; default: return OP_NONE;
  }
}
static OpKind unaop(TokenType t){
  switch(t){ case TOK_MINUS: return OP_NEG; case TOK_KW_NOT: return OP_NOT; case TOK_LEN: return OP_LEN; default: return OP_NONE; }
}

/* fwd */
AST *expression(Parser *p);
AST *statement(Parser *p);
static AST *parse_precedence(Parser *p, int prec_min);
static AST *parse_primary(Parser *p);

/* blocks */
static AST *parse_block(Parser*p){
  int line=curr(p).line; ASTVec stmts={0};
  while(!check(p,TOK_KW_END)&&!check(p,TOK_KW_ELSE)&&!check(p,TOK_KW_ELSEIF)&&!check(p,TOK_KW_UNTIL)&&!check(p,TOK_EOF)){
    astvec_push(&stmts, statement(p));
  }
  return ast_make_block(stmts,line);
}

/* params */
static void parse_paramlist(Parser*p, ASTVec *params, bool *vararg){
  *vararg=false;
  if(check(p,TOK_RPAREN)) return;
  for(;;){
    if(match(p,TOK_VARARG)){ *vararg=true; break; } /* '...' */
    Token id=curr(p);
    if(!match(p,TOK_ID)){ error_at(p,id.line,"expected parameter name or '...'"); break; }
    astvec_push(params, ast_make_ident(id.lexeme?id.lexeme:"", id.line));
    if(!match(p,TOK_COMMA)) break;
  }
}

/* tables: assumes '{' already consumed */
static AST *parse_table(Parser*p){
  int line=curr(p).line; ASTVec keys={0}, values={0};
  while(!check(p,TOK_RBRACE) && !check(p,TOK_EOF)){
    if(match(p,TOK_LBRACK)){ /* [expr] = expr */
      AST *k = expression(p); expect(p,TOK_RBRACK,"expected ']'");
      expect(p,TOK_ASSIGN,"expected '=' after key");
      AST *v = expression(p);
      astvec_push(&keys,k); astvec_push(&values,v);
    } else if(check(p,TOK_ID)){ /* name = expr  OR positional expr */
      Token id = curr(p);
      if(p->pos+1 < p->count && p->toks[p->pos+1].type == TOK_ASSIGN){
        advance(p); advance(p); /* name and '=' */
        AST *v = expression(p);
        astvec_push(&keys, ast_make_string(id.lexeme?id.lexeme:"", id.line));
        astvec_push(&values, v);
      } else {
        AST *v = expression(p);
        astvec_push(&keys, NULL);
        astvec_push(&values, v);
      }
    } else {
      AST *v = expression(p);
      astvec_push(&keys, NULL); astvec_push(&values, v);
    }
    if(!match(p,TOK_COMMA)) { /* only commas supported here */ }
    if(check(p,TOK_RBRACE)) break;
  }
  expect(p,TOK_RBRACE,"expected '}' to close table");
  return ast_make_table(keys,values,line);
}

/* function literal: NOTE KW_FUNCTION already consumed by caller */
static AST *parse_function_literal(Parser*p){
  int line = curr(p).line; /* approx location */
  expect(p,TOK_LPAREN,"expected '(' after 'function'");
  ASTVec params={0}; bool vararg=false; parse_paramlist(p,&params,&vararg);
  expect(p,TOK_RPAREN,"expected ')'");
  AST *body = parse_block(p);
  expect(p,TOK_KW_END,"expected 'end' to close function");
  return ast_make_function(params,vararg,body,line);
}

/* a.b.c or a:b */
static AST *parse_name_chain(Parser*p){
  Token id = curr(p); if(!match(p,TOK_ID)){ error_at(p,id.line,"expected name"); return ast_make_ident("",id.line); }
  AST *base = ast_make_ident(id.lexeme?id.lexeme:"", id.line);
  for(;;){
    if(match(p,TOK_DOT)){
      Token f=curr(p); if(!match(p,TOK_ID)){ error_at(p,f.line,"expected field after '.'"); break; }
      base = ast_make_field(base, f.lexeme?f.lexeme:"", f.line);
    } else if(match(p,TOK_COLON)){
      Token m=curr(p); if(!match(p,TOK_ID)){ error_at(p,m.line,"expected method name after ':'"); break; }
      base = ast_make_field(base, m.lexeme?m.lexeme:"", m.line);
      break; /* only one ':' allowed here */
    } else break;
  }
  return base;
}

static AST *parse_postfix(Parser*p, AST*base){
  for(;;){
    if(match(p,TOK_LPAREN)){
      ASTVec args={0}; int line=curr(p).line;
      if(!check(p,TOK_RPAREN)){ do{ astvec_push(&args, expression(p)); } while(match(p,TOK_COMMA)); }
      expect(p,TOK_RPAREN,"expected ')'");
      base = ast_make_call(base,args,line);
      continue;
    }

    if(match(p,TOK_LBRACK)){
      AST *idx=expression(p);
      expect(p,TOK_RBRACK,"expected ']'");
      base=ast_make_index(base,idx,base->line);
      continue;
    }

    if(match(p,TOK_DOT)){
      Token f=curr(p);
      expect(p,TOK_ID,"expected field name after '.'");
      base=ast_make_field(base,f.lexeme?f.lexeme:"",f.line);
      continue;
    }

    /* method sugar: only if we see  ':' ID '('  ahead */
    if (check(p, TOK_COLON)) {
      size_t pos = p->pos;
      if (pos + 2 < (size_t)p->count &&
          p->toks[pos+1].type == TOK_ID &&
          p->toks[pos+2].type == TOK_LPAREN)
      {
        advance(p);                /* ':' */
        Token m = advance(p);      /* method name (ID) */
        AST *callee = ast_make_field(base, m.lexeme?m.lexeme:"", m.line);
        expect(p, TOK_LPAREN, "expected '(' after method name");

        ASTVec args={0};
        astvec_push(&args, base);  /* implicit self */
        if(!check(p,TOK_RPAREN)){
          do { astvec_push(&args, expression(p)); } while (match(p, TOK_COMMA));
        }
        expect(p, TOK_RPAREN, "expected ')'");
        base = ast_make_call(callee, args, m.line);
        continue;
      } else {
        break;
      }
    }

    /* call sugar: f "str" */
    if(check(p, TOK_STR)){
      Token s = advance(p);
      ASTVec args = (ASTVec){0};
      astvec_push(&args, ast_make_string(s.lexeme ? s.lexeme : "", s.line));
      base = ast_make_call(base, args, s.line);
      continue;
    }

    /* call sugar: f { ... } */
    if(check(p, TOK_LBRACE)){
      int line = curr(p).line;
      advance(p);                  /* consume '{' */
      AST *tbl = parse_table(p);   /* '{' already consumed */
      ASTVec args = (ASTVec){0};
      astvec_push(&args, tbl);
      base = ast_make_call(base, args, line);
      continue;
    }

    break;
  }
  return base;
}

/* primary / unary / precedence */
static AST *parse_primary(Parser*p){
  Token t=advance(p);
  switch(t.type){
    case TOK_KW_NIL:   return ast_make_nil(t.line);
    case TOK_KW_TRUE:  return ast_make_bool(true,t.line);
    case TOK_KW_FALSE: return ast_make_bool(false,t.line);
    case TOK_NUMBER:   return ast_make_number(atof(t.lexeme?t.lexeme:"0"),t.line);
    case TOK_STR:      return ast_make_string(t.lexeme?t.lexeme:"",t.line);
    case TOK_LBRACE:   return parse_table(p);
    case TOK_KW_FUNCTION: return parse_function_literal(p);
    case TOK_VARARG: {return ast_make_ident("...", t.line);}
    case TOK_ID: {
      AST *id=ast_make_ident(t.lexeme?t.lexeme:"",t.line);
      return parse_postfix(p,id);
    }
    case TOK_LPAREN: {
      AST*e=expression(p); expect(p,TOK_RPAREN,"expected ')'");
      return e;
    }
    default: error_at(p,t.line,"unexpected %s%s%s",
                      tok_name(t.type),
                      (t.lexeme && *t.lexeme) ? " \"" : "",
                      (t.lexeme && *t.lexeme) ? t.lexeme : "");
             return ast_make_nil(t.line);
  }
}
static AST *parse_unary(Parser*p){
  Token t=curr(p); OpKind op=unaop(t.type);
  if(op!=OP_NONE){ advance(p); AST*rhs=parse_precedence(p,8); return ast_make_unary(op,rhs,t.line); }
  return parse_primary(p);
}
static AST *parse_precedence(Parser*p,int prec_min){
  AST *left=parse_unary(p);
  for(;;){
    TokenType tt=curr(p).type; int prec=precedence_of(tt);
    if(prec<prec_min||prec==0) break;
    OpKind op=binop(tt); int line=curr(p).line; advance(p);
    int next_min = right_assoc(tt)?prec:(prec+1);
    AST *right=parse_precedence(p,next_min);
    left=ast_make_binary(op,left,right,line);
  }
  return left;
}

/* -------- lvalues (with soft/backtracking mode) -------- */
static AST *parse_lvalue_ex(Parser *p, bool soft){
  int start = p->pos;

  if (check(p, TOK_LPAREN)) {
    advance(p);
    AST *base = expression(p);
    if (!expect(p, TOK_RPAREN, "expected ')'")) {
      if (soft) { p->pos = start; return NULL; }
      return ast_make_ident("", curr(p).line);
    }
    bool had_selector = false;
    for (;;) {
      if (match(p, TOK_DOT)) {
        Token f = curr(p);
        if (!match(p, TOK_ID)) {
          if (soft) { p->pos = start; return NULL; }
          error_at(p, f.line, "expected field");
          return ast_make_ident("", f.line);
        }
        base = ast_make_field(base, f.lexeme ? f.lexeme : "", f.line);
        had_selector = true;
        continue;
      }
      if (match(p, TOK_LBRACK)) {
        AST *idx = expression(p);
        if (!expect(p, TOK_RBRACK, "expected ']'")) {
          if (soft) { p->pos = start; return NULL; }
          return ast_make_ident("", curr(p).line);
        }
        base = ast_make_index(base, idx, base->line);
        had_selector = true;
        continue;
      }
      break;
    }
    if (!had_selector) {
      if (soft) { p->pos = start; return NULL; }
      error_at(p, curr(p).line, "expected lvalue");
    }
    return base;
  }

  if (!check(p, TOK_ID)) {
    if (soft) { p->pos = start; return NULL; }
    error_at(p, curr(p).line, "expected lvalue");
    return ast_make_ident("", curr(p).line);
  }
  Token id = advance(p);
  AST *base = ast_make_ident(id.lexeme ? id.lexeme : "", id.line);

  for (;;) {
    if (match(p, TOK_DOT)) {
      Token f = curr(p);
      if (!match(p, TOK_ID)) {
        if (soft) { p->pos = start; return NULL; }
        error_at(p, f.line, "expected field");
        return base;
      }
      base = ast_make_field(base, f.lexeme ? f.lexeme : "", f.line);
      continue;
    }
    if (match(p, TOK_LBRACK)) {
      AST *idx = expression(p);
      if (!expect(p, TOK_RBRACK, "expected ']'")) {
        if (soft) { p->pos = start; return NULL; }
        return base;
      }
      base = ast_make_index(base, idx, base->line);
      continue;
    }
    break;
  }
  return base;
}

static AST *parse_lvalue(Parser *p){
  return parse_lvalue_ex(p, /*soft=*/false);
}

static ASTVec parse_lval_list(Parser*p){
  ASTVec L={0};
  do{ astvec_push(&L, parse_lvalue(p)); } while(match(p,TOK_COMMA));
  return L;
}

/* explist */
static ASTVec parse_explist(Parser*p){
  ASTVec xs={0};
  do{ astvec_push(&xs, expression(p)); } while(match(p,TOK_COMMA));
  return xs;
}

/* public: expression (with safe varlist '=' lookahead) */
AST *expression(Parser*p){
  int save = p->pos;

  ASTVec L = (ASTVec){0};
  AST *lv = parse_lvalue_ex(p, /*soft=*/true);
  if (lv) {
    astvec_push(&L, lv);

    bool ok_list = true;
    while (match(p, TOK_COMMA)) {
      AST *lv2 = parse_lvalue_ex(p, /*soft=*/true);
      if (!lv2) { ok_list = false; break; }
      astvec_push(&L, lv2);
    }

    if (ok_list && match(p, TOK_ASSIGN)) {
      ASTVec R = parse_explist(p);
      return ast_make_assign_list(L, R, curr(p).line);
    }
    p->pos = save;
  }

  return parse_precedence(p,1);
}

/* public: statement */
AST *statement(Parser*p){
  /* If we arrive in panic mode, resynchronize before parsing a statement */
  if (p->panic) synchronize(p);

  {
    int save = p->pos;
    if (match(p, TOK_COLON) && match(p, TOK_COLON)) {
      Token nameTok = curr(p);
      if (match(p, TOK_ID) && match(p, TOK_COLON) && match(p, TOK_COLON)) {
        return ast_make_label(nameTok.lexeme ? nameTok.lexeme : "", nameTok.line);
      }
      p->pos = save;
    }
  }
  if (match(p, TOK_SEMICOLON)) {
    return ast_make_nil(curr(p).line);  // optional: an empty statement AST node
}

  if(match(p,TOK_KW_GOTO)){ Token name=curr(p);
    expect(p,TOK_ID,"expected label name after 'goto'");
    return ast_make_goto(name.lexeme?name.lexeme:"", name.line);
  }

  if(match(p,TOK_KW_BREAK)) return ast_make_break(curr(p).line);

  if(match(p,TOK_KW_DO)){ AST*body=parse_block(p); expect(p,TOK_KW_END,"expected 'end'"); return body; }

  if(match(p,TOK_KW_IF)){
    int line=curr(p).line;

    AST *cond0 = expression(p);
    expect(p,TOK_KW_THEN,"expected 'then'");
    AST *then0 = parse_block(p);

    AST *root = ast_make_if(cond0, then0, NULL, line);
    AST *tail = root;

    while (match(p, TOK_KW_ELSEIF)) {
      AST *c = expression(p);
      expect(p, TOK_KW_THEN, "expected 'then'");
      AST *tb = parse_block(p);
      AST *node = ast_make_if(c, tb, NULL, line);
      tail->as.ifs.else_blk = node;
      tail = node;
    }

    if (match(p, TOK_KW_ELSE)) {
      AST *eb = parse_block(p);
      tail->as.ifs.else_blk = eb;
    }

    expect(p,TOK_KW_END,"expected 'end'");
    return root;
  }

if (match(p, TOK_KW_TRY)) {
    AST *tryBlock = parse_block(p);   // parse code inside try
    AST *catchBlock = NULL;
    AST *finallyBlock = NULL;
    char *exceptionVar = NULL;

    if (match(p, TOK_KW_CATCH)) {
        if (curr(p).type == TOK_ID) {
            exceptionVar = strdup(curr(p).lexeme);
            advance(p);
        }
        catchBlock = parse_block(p);
    }

    if (match(p, TOK_KW_FINALLY)) {
        finallyBlock = parse_block(p);
    }

    expect(p, TOK_KW_END, "expected 'end' after try/catch/finally");

int line = parser_curr(p).line;
return ast_make_try_catch_finally(tryBlock, catchBlock, finallyBlock, exceptionVar, line);
}
  if(match(p,TOK_KW_WHILE)){
    int line=curr(p).line; AST*cond=expression(p);
    expect(p,TOK_KW_DO,"expected 'do'"); AST *body=parse_block(p); expect(p,TOK_KW_END,"expected 'end'");
    return ast_make_while(cond, body, line);
  }

  if(match(p,TOK_KW_REPEAT)){
    int line=curr(p).line; AST *body=parse_block(p);
    expect(p,TOK_KW_UNTIL,"expected 'until'"); AST *cond=expression(p);
    return ast_make_repeat(body, cond, line);
  }

  if(match(p,TOK_KW_FOR)){
    Token name=curr(p); expect(p,TOK_ID,"expected identifier after 'for'");
    if(match(p,TOK_ASSIGN)){
      AST *start=expression(p); expect(p,TOK_COMMA,"expected ','");
      AST *end=expression(p); AST *step=NULL;
      if(match(p,TOK_COMMA)) step=expression(p);
      expect(p,TOK_KW_DO,"expected 'do'"); AST *body=parse_block(p); expect(p,TOK_KW_END,"expected 'end'");
      return ast_make_for_num(name.lexeme?name.lexeme:"", start, end, step, body, name.line);
    } else if(match(p,TOK_COMMA) || match(p,TOK_KW_IN)){
      ASTVec names={0};
      astvec_push(&names, ast_make_ident(name.lexeme?name.lexeme:"", name.line));
      if(p->toks[p->pos-1].type==TOK_COMMA){
        for(;;){
          Token id=curr(p); expect(p,TOK_ID,"expected identifier");
          astvec_push(&names, ast_make_ident(id.lexeme?id.lexeme:"", id.line));
          if(!match(p,TOK_COMMA)) break;
        }
        expect(p,TOK_KW_IN,"expected 'in'");
      }
      ASTVec iters = parse_explist(p);
      expect(p,TOK_KW_DO,"expected 'do'"); AST *body=parse_block(p); expect(p,TOK_KW_END,"expected 'end'");
      return ast_make_for_in(names, iters, body, name.line);
    } else {
      error_at(p,name.line,"malformed 'for' statement");
      while(!check(p,TOK_KW_END)&&!check(p,TOK_EOF)) advance(p);
      if(check(p,TOK_KW_END)) advance(p);
      return ast_make_nil(name.line);
    }
  }

  if(match(p,TOK_KW_FUNCTION)){
    AST *namechain = parse_name_chain(p);
    expect(p,TOK_LPAREN,"expected '('"); ASTVec params={0}; bool vararg=false; parse_paramlist(p,&params,&vararg);
    expect(p,TOK_RPAREN,"expected ')'");
    AST *body = parse_block(p); expect(p,TOK_KW_END,"expected 'end'");
    return ast_make_func_stmt(false, namechain, params, vararg, body, namechain->line);
  }

  if(match(p,TOK_KW_LOCAL)){
    if(match(p,TOK_KW_FUNCTION)){
      Token nm=curr(p); expect(p,TOK_ID,"expected function name");
      AST *name = ast_make_ident(nm.lexeme?nm.lexeme:"", nm.line);
      expect(p,TOK_LPAREN,"expected '('"); ASTVec params={0}; bool vararg=false; parse_paramlist(p,&params,&vararg);
      expect(p,TOK_RPAREN,"expected ')'");
      AST *body=parse_block(p); expect(p,TOK_KW_END,"expected 'end'");
      return ast_make_func_stmt(true, name, params, vararg, body, nm.line);
    } else {
      Token nm=curr(p); expect(p,TOK_ID,"expected identifier after 'local'");
      ASTVec names={0}; astvec_push(&names, ast_make_ident(nm.lexeme?nm.lexeme:"", nm.line));
      while(match(p,TOK_COMMA)){
        Token nx=curr(p); expect(p,TOK_ID,"expected identifier");
        astvec_push(&names, ast_make_ident(nx.lexeme?nx.lexeme:"", nx.line));
      }
      ASTVec inits={0}; bool has_init=false;
      if(match(p,TOK_ASSIGN)){ has_init=true; inits = parse_explist(p); }
      ASTVec lvals={0}; for(size_t i=0;i<names.count;i++) astvec_push(&lvals, names.items[i]);
      if(has_init){
        return ast_make_assign_list(lvals, inits, nm.line);
      } else {
        ASTVec emptyR = {0};
        return ast_make_assign_list(lvals, emptyR, nm.line);
      }
    }
  }

  if(match(p,TOK_KW_RETURN)){
    int line=curr(p).line; ASTVec xs={0};
    if(!check(p,TOK_KW_END)&&!check(p,TOK_KW_ELSE)&&!check(p,TOK_KW_UNTIL)&&!check(p,TOK_EOF)){
      xs = parse_explist(p);
    }
    return ast_make_return_list(xs,line);
  }

  int line = curr(p).line;
  AST *e = expression(p);
  if (e && (e->kind == AST_ASSIGN || e->kind == AST_ASSIGN_LIST)) {
    return e;
  }
  return ast_make_stmt_expr(e, line);
}
