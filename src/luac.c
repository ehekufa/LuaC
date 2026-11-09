#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#if !defined(LUAC_LIBRARY_ONLY) && !defined(_WIN32)
#include <dlfcn.h>
#endif
#if !defined(LUAC_LIBRARY_ONLY) && defined(_WIN32)
#include <windows.h>
#endif
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef LUAC_RUNTIME_PATH
#define LUAC_RUNTIME_PATH "src/runtime.h"
#endif

#define LUAC_VERSION "0.2.0"
#include "luac.h"

typedef struct { char *data; size_t len, cap; } Str;
static void die_oom(void) { fputs("luac: out of memory\n", stderr); exit(1); }
static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) die_oom(); return p; }
static void *xrealloc(void *p, size_t n) { p = realloc(p, n ? n : 1); if (!p) die_oom(); return p; }
static char *xstrdup(const char *s) { size_t n = strlen(s); char *p = xmalloc(n + 1); memcpy(p, s, n + 1); return p; }
static char *xstrndup(const char *s, size_t n) { char *p = xmalloc(n + 1); memcpy(p, s, n); p[n] = 0; return p; }
static void sb_init(Str *s) { memset(s, 0, sizeof(*s)); }
static void sb_need(Str *s, size_t n) { if (s->len + n + 1 > s->cap) { s->cap = (s->len + n + 1) * 2 + 128; s->data = xrealloc(s->data, s->cap); } }
static void sb_n(Str *s, const char *p, size_t n) { sb_need(s, n); memcpy(s->data + s->len, p, n); s->len += n; s->data[s->len] = 0; }
static void sb_add(Str *s, const char *p) { sb_n(s, p, strlen(p)); }
static void sb_printf(Str *s, const char *fmt, ...) {
    va_list ap, copy; int n;
    va_start(ap, fmt); va_copy(copy, ap); n = vsnprintf(NULL, 0, fmt, copy); va_end(copy);
    if (n < 0) { va_end(ap); return; } sb_need(s, (size_t)n); vsnprintf(s->data + s->len, s->cap - s->len, fmt, ap); s->len += (size_t)n; va_end(ap);
}
static void sb_free(Str *s) { free(s->data); memset(s, 0, sizeof(*s)); }

/* ------------------------------- Lexer ---------------------------------- */
typedef enum {
    T_EOF=0, T_NAME, T_NUMBER, T_STRING,
    T_NIL, T_TRUE, T_FALSE, T_FUNCTION, T_LOCAL, T_END, T_IF, T_THEN,
    T_ELSEIF, T_ELSE, T_WHILE, T_DO, T_FOR, T_IN, T_REPEAT, T_UNTIL,
    T_RETURN, T_BREAK, T_AND, T_OR, T_NOT,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_DSLASH, T_PERCENT, T_CARET,
    T_HASH, T_EQ, T_NE, T_LE, T_GE, T_LT, T_GT, T_ASSIGN,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_SEMI, T_COLON, T_COMMA, T_DOT, T_CONCAT, T_DOTS
} TokenKind;

typedef struct {
    TokenKind kind;
    int line, column;
    double number;
    char *text;
} Token;

typedef struct {
    const char *source, *filename;
    size_t pos, length;
    int line, column;
    Token token;
    char error[512];
    int failed;
} Parser;

static int is_name_start(int c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_name_char(int c) { return isalnum((unsigned char)c) || c == '_'; }
static char peek(Parser *p) { return p->pos < p->length ? p->source[p->pos] : 0; }
static char peek_n(Parser *p, size_t n) { return p->pos + n < p->length ? p->source[p->pos + n] : 0; }
static char take(Parser *p) {
    char c = peek(p); if (!c) return 0; ++p->pos;
    if (c == '\n') { ++p->line; p->column = 1; } else ++p->column;
    return c;
}
static void parse_error(Parser *p, const char *fmt, ...) {
    va_list ap;
    if (p->failed) return;
    p->failed = 1;
    snprintf(p->error, sizeof(p->error), "%s:%d:%d: ", p->filename, p->token.line, p->token.column);
    { size_t n = strlen(p->error); va_start(ap, fmt); vsnprintf(p->error + n, sizeof(p->error) - n, fmt, ap); va_end(ap); }
}
static void token_clear(Token *t) { free(t->text); t->text = NULL; }

static const char *token_name(TokenKind k) {
    static const char *names[] = {
        "eof","name","number","string","nil","true","false","function","local","end","if","then",
        "elseif","else","while","do","for","in","repeat","until","return","break","and","or","not",
        "+","-","*","/","//","%","^","#","==","~=","<=",">=","<",">","=","(",")","{","}","[","]",
        ";",":",",",".","..","..."
    };
    return names[(int)k];
}

static int long_bracket_level(Parser *p, size_t at) {
    int n = 0;
    if (at >= p->length || p->source[at] != '[') return -1;
    ++at; while (at < p->length && p->source[at] == '=') { ++n; ++at; }
    return at < p->length && p->source[at] == '[' ? n : -1;
}
static void skip_long(Parser *p, int level, int keep, Str *out) {
    int l = p->token.line, c = p->token.column;
    take(p); while (peek(p) == '=') take(p); take(p);
    if (peek(p) == '\n') take(p);
    while (peek(p)) {
        if (peek(p) == ']') {
            size_t i = p->pos + 1; int n = 0;
            while (i < p->length && p->source[i] == '=') { ++n; ++i; }
            if (n == level && i < p->length && p->source[i] == ']') {
                take(p); while (peek(p) == '=') take(p); take(p); return;
            }
        }
        { char ch = take(p); if (keep) sb_n(out, &ch, 1); }
    }
    p->token.line = l; p->token.column = c; parse_error(p, "unfinished long string/comment");
}

static void next_token(Parser *p) {
    int start_line, start_col;
    token_clear(&p->token);
again:
    while (peek(p) && isspace((unsigned char)peek(p))) take(p);
    if (peek(p) == '-' && peek_n(p, 1) == '-') {
        take(p); take(p);
        { int level = long_bracket_level(p, p->pos);
          if (level >= 0) { Str dummy; sb_init(&dummy); skip_long(p, level, 0, &dummy); sb_free(&dummy); }
          else while (peek(p) && peek(p) != '\n') take(p); }
        goto again;
    }
    start_line = p->line; start_col = p->column;
    p->token.line = start_line; p->token.column = start_col;
    if (!peek(p)) { p->token.kind = T_EOF; return; }
    if (is_name_start(peek(p))) {
        size_t start = p->pos;
        static const struct { const char *s; TokenKind k; } kw[] = {
            {"nil",T_NIL},{"true",T_TRUE},{"false",T_FALSE},{"function",T_FUNCTION},{"local",T_LOCAL},
            {"end",T_END},{"if",T_IF},{"then",T_THEN},{"elseif",T_ELSEIF},{"else",T_ELSE},{"while",T_WHILE},
            {"do",T_DO},{"for",T_FOR},{"in",T_IN},{"repeat",T_REPEAT},{"until",T_UNTIL},{"return",T_RETURN},
            {"break",T_BREAK},{"and",T_AND},{"or",T_OR},{"not",T_NOT},{NULL,T_EOF}
        };
        int i;
        while (is_name_char(peek(p))) take(p);
        p->token.text = xstrndup(p->source + start, p->pos - start);
        p->token.kind = T_NAME;
        for (i = 0; kw[i].s; ++i) if (!strcmp(kw[i].s, p->token.text)) { p->token.kind = kw[i].k; break; }
        return;
    }
    if (isdigit((unsigned char)peek(p)) || (peek(p) == '.' && isdigit((unsigned char)peek_n(p,1)))) {
        char *end;
        p->token.number = strtod(p->source + p->pos, &end);
        if (end == p->source + p->pos || end > p->source + p->length) {
            parse_error(p, "invalid number"); return;
        }
        while (p->source + p->pos < end) take(p);
        p->token.kind = T_NUMBER; return;
    }
    if (peek(p) == '[') {
        int level = long_bracket_level(p, p->pos);
        if (level >= 0) {
            Str s; sb_init(&s); skip_long(p, level, 1, &s);
            p->token.kind = T_STRING; p->token.text = s.data ? s.data : xstrdup(""); return;
        }
    }
    if (peek(p) == '\'' || peek(p) == '"') {
        char quote = take(p); Str s; sb_init(&s);
        while (peek(p) && peek(p) != quote) {
            char c = take(p);
            if (c == '\n') { parse_error(p, "unfinished string"); break; }
            if (c == '\\') {
                c = take(p);
                switch (c) {
                    case 'n': c = '\n'; break; case 'r': c = '\r'; break; case 't': c = '\t'; break;
                    case 'a': c = '\a'; break; case 'b': c = '\b'; break; case 'f': c = '\f'; break;
                    case 'v': c = '\v'; break; case '\\': break; case '\'': break; case '"': break;
                    case '\n': break;
                    case 'x': {
                        int v = 0, i; for (i = 0; i < 2; ++i) { char h = take(p); if (!isxdigit((unsigned char)h)) { parse_error(p,"invalid hex escape"); break; } v = v*16 + (isdigit((unsigned char)h)?h-'0':tolower((unsigned char)h)-'a'+10); } c=(char)v; break;
                    }
                    default:
                        if (isdigit((unsigned char)c)) { int v = c-'0', i; for(i=0;i<2 && isdigit((unsigned char)peek(p));++i) v=v*10+(take(p)-'0'); c=(char)v; }
                        else parse_error(p, "invalid escape sequence");
                }
            }
            sb_n(&s, &c, 1);
        }
        if (peek(p) == quote) take(p); else parse_error(p, "unfinished string");
        p->token.kind = T_STRING; p->token.text = s.data ? s.data : xstrdup(""); return;
    }
#define OP1(ch, k) case ch: take(p); p->token.kind = (k); return
    switch (peek(p)) {
        OP1('+',T_PLUS); OP1('-',T_MINUS); OP1('*',T_STAR); OP1('%',T_PERCENT); OP1('^',T_CARET);
        OP1('#',T_HASH); OP1('(',T_LPAREN); OP1(')',T_RPAREN);
        OP1('{',T_LBRACE); OP1('}',T_RBRACE); OP1('[',T_LBRACKET); OP1(']',T_RBRACKET);
        OP1(';',T_SEMI); OP1(':',T_COLON); OP1(',',T_COMMA);
        case '/': take(p); if (peek(p)=='/') { take(p); p->token.kind=T_DSLASH; } else p->token.kind=T_SLASH; return;
        case '=': take(p); if (peek(p)=='=') { take(p); p->token.kind=T_EQ; } else p->token.kind=T_ASSIGN; return;
        case '~': take(p); if (peek(p)=='=') { take(p); p->token.kind=T_NE; } else parse_error(p,"unexpected '~'"); return;
        case '<': take(p); if (peek(p)=='=') { take(p); p->token.kind=T_LE; } else p->token.kind=T_LT; return;
        case '>': take(p); if (peek(p)=='=') { take(p); p->token.kind=T_GE; } else p->token.kind=T_GT; return;
        case '.': take(p); if (peek(p)=='.') { take(p); if (peek(p)=='.') { take(p); p->token.kind=T_DOTS; } else p->token.kind=T_CONCAT; } else p->token.kind=T_DOT; return;
        default: parse_error(p, "unexpected character '%c'", peek(p)); take(p); return;
    }
#undef OP1
}

static int accept_token(Parser *p, TokenKind k) { if (p->token.kind == k) { next_token(p); return 1; } return 0; }
static int expect(Parser *p, TokenKind k) { if (accept_token(p,k)) return 1; parse_error(p,"expected '%s', got '%s'",token_name(k),token_name(p->token.kind)); return 0; }
static char *expect_name(Parser *p) { char *s; if (p->token.kind != T_NAME) { parse_error(p,"expected name"); return xstrdup("error"); } s=xstrdup(p->token.text); next_token(p); return s; }

/* -------------------------------- AST ----------------------------------- */
typedef enum { E_NIL,E_BOOL,E_NUMBER,E_STRING,E_VAR,E_VARARG,E_BINARY,E_UNARY,E_TABLE,E_INDEX,E_CALL,E_FUNCTION } ExprKind;
typedef struct Expr Expr;
typedef struct Stmt Stmt;
struct Expr {
    ExprKind kind; int line, op, flag;
    double number; char *name;
    Expr *a,*b;
    Expr **items, **keys; int count;
    char **params; int nparams;
    Stmt *body;
};
typedef enum { S_LOCAL,S_ASSIGN,S_EXPR,S_IF,S_WHILE,S_REPEAT,S_FORNUM,S_FORIN,S_RETURN,S_BREAK,S_DO,S_LOCALFUNC } StmtKind;
struct Stmt {
    StmtKind kind; int line; Stmt *next;
    char **names; int nnames;
    Expr **exprs; int nexprs;
    Expr *expr,*expr2,*expr3;
    Stmt *body;
    Stmt **blocks; Expr **conds; int nblocks;
    int flag;
};

static Expr *new_expr(Parser *p, ExprKind k) { Expr *e=xmalloc(sizeof(*e)); memset(e,0,sizeof(*e)); e->kind=k; e->line=p->token.line; return e; }
static Stmt *new_stmt(Parser *p, StmtKind k) { Stmt *s=xmalloc(sizeof(*s)); memset(s,0,sizeof(*s)); s->kind=k; s->line=p->token.line; return s; }
static void expr_list_add(Expr ***list,int *n,Expr *e) { *list=xrealloc(*list,sizeof(Expr*)*(size_t)(*n+1)); (*list)[(*n)++]=e; }
static void name_list_add(char ***list,int *n,char *s) { *list=xrealloc(*list,sizeof(char*)*(size_t)(*n+1)); (*list)[(*n)++]=s; }
static Expr *parse_expr(Parser *p);
static Expr *parse_suffix(Parser *p, Expr *e);
static Expr *parse_subexpr(Parser *p, int limit);
static Expr *parse_subexpr_tail(Parser *p, Expr *left, int limit);
static Stmt *parse_block(Parser *p);

static Expr *parse_function_body(Parser *p, int line, int method) {
    Expr *e=new_expr(p,E_FUNCTION); e->line=line;
    expect(p,T_LPAREN);
    if (method) name_list_add(&e->params,&e->nparams,xstrdup("self"));
    if (p->token.kind!=T_RPAREN) {
        for (;;) {
            if (accept_token(p,T_DOTS)) { e->flag=1; break; }
            name_list_add(&e->params,&e->nparams,expect_name(p));
            if (!accept_token(p,T_COMMA)) break;
        }
    }
    expect(p,T_RPAREN); e->body=parse_block(p); expect(p,T_END); return e;
}

static Expr *parse_table(Parser *p) {
    Expr *e=new_expr(p,E_TABLE); expect(p,T_LBRACE);
    while (p->token.kind!=T_RBRACE && !p->failed) {
        Expr *key=NULL,*value;
        if (accept_token(p,T_LBRACKET)) { key=parse_expr(p); expect(p,T_RBRACKET); expect(p,T_ASSIGN); value=parse_expr(p); }
        else if (p->token.kind==T_NAME) {
            char *name=xstrdup(p->token.text); next_token(p);
            if (accept_token(p,T_ASSIGN)) { key=new_expr(p,E_STRING); key->name=name; value=parse_expr(p); }
            else {
                Expr *v=new_expr(p,E_VAR); v->name=name; v->line=key?key->line:p->token.line;
                value=parse_subexpr_tail(p,parse_suffix(p,v),0);
            }
        } else value=parse_expr(p);
        expr_list_add(&e->items,&e->count,value); e->keys=xrealloc(e->keys,sizeof(Expr*)*(size_t)e->count); e->keys[e->count-1]=key;
        if (!accept_token(p,T_COMMA) && !accept_token(p,T_SEMI)) break;
    }
    expect(p,T_RBRACE); return e;
}

static Expr *parse_suffix(Parser *p, Expr *e) {
    for (;;) {
        if (accept_token(p,T_DOT)) { Expr *ix=new_expr(p,E_INDEX),*k=new_expr(p,E_STRING); ix->a=e; k->name=expect_name(p); ix->b=k; e=ix; }
        else if (accept_token(p,T_LBRACKET)) { Expr *ix=new_expr(p,E_INDEX); ix->a=e; ix->b=parse_expr(p); expect(p,T_RBRACKET); e=ix; }
        else if (p->token.kind==T_COLON || p->token.kind==T_LPAREN || p->token.kind==T_STRING || p->token.kind==T_LBRACE) {
            Expr *call=new_expr(p,E_CALL); call->a=e;
            if (accept_token(p,T_COLON)) { call->flag=1; call->name=expect_name(p); }
            if (accept_token(p,T_LPAREN)) {
                if (p->token.kind!=T_RPAREN) for (;;) { expr_list_add(&call->items,&call->count,parse_expr(p)); if(!accept_token(p,T_COMMA))break; }
                expect(p,T_RPAREN);
            } else if (p->token.kind==T_STRING) { Expr *a=new_expr(p,E_STRING); a->name=xstrdup(p->token.text); next_token(p); expr_list_add(&call->items,&call->count,a); }
            else if (p->token.kind==T_LBRACE) expr_list_add(&call->items,&call->count,parse_table(p));
            else { parse_error(p,"expected call arguments"); break; }
            e=call;
        } else break;
    }
    return e;
}

static Expr *parse_primary(Parser *p) {
    Expr *e;
    if (p->token.kind==T_NIL) { e=new_expr(p,E_NIL); next_token(p); }
    else if (p->token.kind==T_TRUE || p->token.kind==T_FALSE) { e=new_expr(p,E_BOOL); e->flag=p->token.kind==T_TRUE; next_token(p); }
    else if (p->token.kind==T_NUMBER) { e=new_expr(p,E_NUMBER); e->number=p->token.number; next_token(p); }
    else if (p->token.kind==T_STRING) { e=new_expr(p,E_STRING); e->name=xstrdup(p->token.text?p->token.text:""); next_token(p); }
    else if (p->token.kind==T_DOTS) { e=new_expr(p,E_VARARG); next_token(p); }
    else if (p->token.kind==T_NAME) { e=new_expr(p,E_VAR); e->name=xstrdup(p->token.text); next_token(p); }
    else if (p->token.kind==T_FUNCTION) { int line=p->token.line; next_token(p); return parse_function_body(p,line,0); }
    else if (p->token.kind==T_LBRACE) return parse_table(p);
    else if (accept_token(p,T_LPAREN)) { e=parse_expr(p); expect(p,T_RPAREN); /* Parentheses force one result. */ { Expr *u=new_expr(p,E_UNARY); u->op=3; u->a=e; e=u; } }
    else { parse_error(p,"expected expression, got '%s'",token_name(p->token.kind)); return new_expr(p,E_NIL); }
    return parse_suffix(p,e);
}

static Expr *parse_subexpr_tail(Parser *p, Expr *left, int limit) {
    for (;;) {
        TokenKind k=p->token.kind; int lp=0,rp=0,op=-1;
        switch(k) {
            case T_OR:lp=1;rp=1;op=14;break; case T_AND:lp=2;rp=2;op=15;break;
            case T_LT:lp=3;rp=3;op=9;break; case T_LE:lp=3;rp=3;op=10;break; case T_GT:lp=3;rp=3;op=11;break; case T_GE:lp=3;rp=3;op=12;break;
            case T_EQ:lp=3;rp=3;op=7;break; case T_NE:lp=3;rp=3;op=8;break;
            case T_CONCAT:lp=4;rp=3;op=13;break;
            case T_PLUS:lp=5;rp=5;op=0;break; case T_MINUS:lp=5;rp=5;op=1;break;
            case T_STAR:lp=6;rp=6;op=2;break; case T_SLASH:lp=6;rp=6;op=3;break; case T_DSLASH:lp=6;rp=6;op=4;break; case T_PERCENT:lp=6;rp=6;op=5;break;
            case T_CARET:lp=8;rp=7;op=6;break; default:break;
        }
        if (op<0 || lp<=limit) break;
        next_token(p); { Expr *b=new_expr(p,E_BINARY); b->op=op; b->a=left; b->b=parse_subexpr(p,rp); left=b; }
    }
    return left;
}
static Expr *parse_subexpr(Parser *p,int limit) {
    Expr *left;
    if (p->token.kind==T_NOT || p->token.kind==T_MINUS || p->token.kind==T_HASH) {
        int op=p->token.kind==T_NOT?0:p->token.kind==T_MINUS?1:2;
        next_token(p); left=new_expr(p,E_UNARY); left->op=op; left->a=parse_subexpr(p,7);
    } else left=parse_primary(p);
    return parse_subexpr_tail(p,left,limit);
}
static Expr *parse_expr(Parser *p) { return parse_subexpr(p,0); }
static void parse_expr_list(Parser *p,Expr ***list,int *n) { do { expr_list_add(list,n,parse_expr(p)); } while(accept_token(p,T_COMMA)); }

static int block_end(TokenKind k) { return k==T_EOF||k==T_END||k==T_ELSE||k==T_ELSEIF||k==T_UNTIL; }
static Stmt *stmt_append(Stmt *head,Stmt *s) { Stmt *p=head; if(!head)return s; while(p->next)p=p->next; p->next=s; return head; }

static Stmt *parse_statement(Parser *p) {
    int line=p->token.line;
    if (accept_token(p,T_SEMI)) return NULL;
    if (accept_token(p,T_LOCAL)) {
        if (accept_token(p,T_FUNCTION)) {
            Stmt *s=new_stmt(p,S_LOCALFUNC); s->line=line; name_list_add(&s->names,&s->nnames,expect_name(p)); s->expr=parse_function_body(p,line,0); return s;
        }
        { Stmt *s=new_stmt(p,S_LOCAL); s->line=line;
          do { name_list_add(&s->names,&s->nnames,expect_name(p)); } while(accept_token(p,T_COMMA));
          if(accept_token(p,T_ASSIGN)) parse_expr_list(p,&s->exprs,&s->nexprs);
          return s; }
    }
    if (accept_token(p,T_FUNCTION)) {
        Stmt *s=new_stmt(p,S_ASSIGN); Expr *var=new_expr(p,E_VAR); var->name=expect_name(p); s->line=line;
        while(accept_token(p,T_DOT)){Expr *ix=new_expr(p,E_INDEX),*k=new_expr(p,E_STRING);ix->a=var;k->name=expect_name(p);ix->b=k;var=ix;}
        { int method=accept_token(p,T_COLON); char *method_name=NULL; if(method)method_name=expect_name(p);
          if(method){Expr *ix=new_expr(p,E_INDEX),*k=new_expr(p,E_STRING);ix->a=var;k->name=method_name;ix->b=k;var=ix;}
          expr_list_add(&s->exprs,&s->nexprs,var); expr_list_add(&s->exprs,&s->nexprs,parse_function_body(p,line,method));
          /* Assignment uses first half as targets and second as values; mark one target. */
          s->nexprs=1; s->expr=var; s->expr2=s->exprs[1]; free(s->exprs);s->exprs=NULL;s->nexprs=0; return s; }
    }
    if (accept_token(p,T_IF)) {
        Stmt *s=new_stmt(p,S_IF); s->line=line;
        for(;;){
            Expr *cond=parse_expr(p); Stmt *body; expect(p,T_THEN); body=parse_block(p);
            s->conds=xrealloc(s->conds,sizeof(Expr*)*(size_t)(s->nblocks+1));s->blocks=xrealloc(s->blocks,sizeof(Stmt*)*(size_t)(s->nblocks+1));
            s->conds[s->nblocks]=cond;s->blocks[s->nblocks]=body;++s->nblocks;
            if(!accept_token(p,T_ELSEIF))break;
        }
        if(accept_token(p,T_ELSE)){s->blocks=xrealloc(s->blocks,sizeof(Stmt*)*(size_t)(s->nblocks+1));s->conds=xrealloc(s->conds,sizeof(Expr*)*(size_t)(s->nblocks+1));s->conds[s->nblocks]=NULL;s->blocks[s->nblocks]=parse_block(p);++s->nblocks;}
        expect(p,T_END);return s;
    }
    if (accept_token(p,T_WHILE)) { Stmt *s=new_stmt(p,S_WHILE);s->line=line;s->expr=parse_expr(p);expect(p,T_DO);s->body=parse_block(p);expect(p,T_END);return s; }
    if (accept_token(p,T_REPEAT)) { Stmt *s=new_stmt(p,S_REPEAT);s->line=line;s->body=parse_block(p);expect(p,T_UNTIL);s->expr=parse_expr(p);return s; }
    if (accept_token(p,T_DO)) { Stmt *s=new_stmt(p,S_DO);s->line=line;s->body=parse_block(p);expect(p,T_END);return s; }
    if (accept_token(p,T_FOR)) {
        char *first=expect_name(p);
        if(accept_token(p,T_ASSIGN)){Stmt*s=new_stmt(p,S_FORNUM);s->line=line;name_list_add(&s->names,&s->nnames,first);s->expr=parse_expr(p);expect(p,T_COMMA);s->expr2=parse_expr(p);if(accept_token(p,T_COMMA))s->expr3=parse_expr(p);expect(p,T_DO);s->body=parse_block(p);expect(p,T_END);return s;}
        {Stmt*s=new_stmt(p,S_FORIN);s->line=line;name_list_add(&s->names,&s->nnames,first);while(accept_token(p,T_COMMA))name_list_add(&s->names,&s->nnames,expect_name(p));expect(p,T_IN);parse_expr_list(p,&s->exprs,&s->nexprs);expect(p,T_DO);s->body=parse_block(p);expect(p,T_END);return s;}
    }
    if (accept_token(p,T_RETURN)) { Stmt*s=new_stmt(p,S_RETURN);s->line=line;if(!block_end(p->token.kind)&&p->token.kind!=T_SEMI)parse_expr_list(p,&s->exprs,&s->nexprs);accept_token(p,T_SEMI);return s; }
    if (accept_token(p,T_BREAK)) { Stmt*s=new_stmt(p,S_BREAK);s->line=line;return s; }
    {
        Expr *first=parse_primary(p);
        if(p->token.kind==T_ASSIGN||p->token.kind==T_COMMA){
            Stmt*s=new_stmt(p,S_ASSIGN);s->line=line;expr_list_add(&s->exprs,&s->nexprs,first);while(accept_token(p,T_COMMA))expr_list_add(&s->exprs,&s->nexprs,parse_primary(p));expect(p,T_ASSIGN);
            s->flag=s->nexprs;parse_expr_list(p,&s->exprs,&s->nexprs);return s;
        }
        if(first->kind!=E_CALL)parse_error(p,"only calls can be expression statements");
        {Stmt*s=new_stmt(p,S_EXPR);s->line=line;s->expr=first;return s;}
    }
}
static Stmt *parse_block(Parser *p) {
    Stmt *head=NULL;
    while(!block_end(p->token.kind)&&!p->failed){Stmt*s=parse_statement(p);if(s)head=stmt_append(head,s);if(s&&s->kind==S_RETURN)break;}
    return head;
}

static void free_stmt(Stmt *s);
static void free_expr(Expr *e){int i;if(!e)return;free_expr(e->a);free_expr(e->b);for(i=0;i<e->count;++i){free_expr(e->items[i]);if(e->keys)free_expr(e->keys[i]);}free(e->items);free(e->keys);for(i=0;i<e->nparams;++i)free(e->params[i]);free(e->params);free(e->name);free_stmt(e->body);free(e);}
static void free_stmt(Stmt *s){int i;if(!s)return;free_stmt(s->next);free_stmt(s->body);for(i=0;i<s->nnames;++i)free(s->names[i]);free(s->names);for(i=0;i<s->nexprs;++i)free_expr(s->exprs[i]);free(s->exprs);free_expr(s->expr);free_expr(s->expr2);free_expr(s->expr3);for(i=0;i<s->nblocks;++i){free_expr(s->conds[i]);free_stmt(s->blocks[i]);}free(s->conds);free(s->blocks);free(s);}

/* ---------------------------- C code generator -------------------------- */
typedef enum { SYM_LOCAL,SYM_UPVALUE,SYM_GLOBAL } SymKind;
typedef struct Symbol { char *name,*cname;SymKind kind; } Symbol;
typedef struct Capture { Symbol *source;Symbol *symbol; } Capture;
typedef struct Cg Cg;
typedef struct Cf {
    Cg *cg;struct Cf *parent;Str decl,body,cleanup;Symbol **locals,**all;int nlocals,nall,cap_locals,cap_all;
    Capture *caps;int ncaps;int temp_id,local_id;char *retname;int vararg;
    char **temps;int ntemps;int loop_depth;
} Cf;
struct Cg { Parser *parser;Str functions;int function_id;int failed; };

static Symbol global_symbol={"","",SYM_GLOBAL};
static void cf_temp(Cf*f){f->temp_id++;}
static char *cf_new_temp(Cf*f){
    char buf[32],*name;
    snprintf(buf,sizeof(buf),"t%d",f->temp_id++);
    sb_printf(&f->decl,"    LcValue %s = {0};\n",buf);
    sb_printf(&f->cleanup,"    LC_RELEASE(ctx, %s);\n",buf);
    name=xstrdup(buf);
    f->temps=xrealloc(f->temps,sizeof(char*)*(size_t)(f->ntemps+1));
    f->temps[f->ntemps++]=name;
    return name;
}
static void cf_dispose(Cf*f){
    int i;
    for(i=0;i<f->nall;++i){free(f->all[i]->name);free(f->all[i]->cname);free(f->all[i]);}
    for(i=0;i<f->ncaps;++i){free(f->caps[i].symbol->name);free(f->caps[i].symbol->cname);free(f->caps[i].symbol);}
    for(i=0;i<f->ntemps;++i)free(f->temps[i]);
    free(f->temps);free(f->all);free(f->locals);free(f->caps);
    sb_free(&f->decl);sb_free(&f->body);sb_free(&f->cleanup);
}
static Symbol *cf_local(Cf*f,const char*name,int declare){
    Symbol*s=xmalloc(sizeof(*s));char buf[32];s->name=xstrdup(name);s->kind=SYM_LOCAL;snprintf(buf,sizeof(buf),"l%d",f->local_id++);s->cname=xstrdup(buf);
    f->all=xrealloc(f->all,sizeof(Symbol*)*(size_t)(f->nall+1));f->all[f->nall++]=s;
    sb_printf(&f->decl,"    LcCell *%s = NULL;\n",s->cname);
    sb_printf(&f->cleanup,"    if (%s) { lc_cell_release(ctx, %s); %s = NULL; }\n",s->cname,s->cname,s->cname);
    if(declare){f->locals=xrealloc(f->locals,sizeof(Symbol*)*(size_t)(f->nlocals+1));f->locals[f->nlocals++]=s;}
    return s;
}
static Symbol *cf_resolve(Cf*f,const char*name){
    int i;if(!f)return &global_symbol;
    for(i=f->nlocals-1;i>=0;--i)if(!strcmp(f->locals[i]->name,name))return f->locals[i];
    if(f->parent){Symbol*p=cf_resolve(f->parent,name);if(p->kind!=SYM_GLOBAL){for(i=0;i<f->ncaps;++i)if(f->caps[i].source==p)return f->caps[i].symbol;f->caps=xrealloc(f->caps,sizeof(Capture)*(size_t)(f->ncaps+1));{Symbol*s=xmalloc(sizeof(*s));char buf[64];s->name=xstrdup(name);s->kind=SYM_UPVALUE;snprintf(buf,sizeof(buf),"self->upvalues[%d]",f->ncaps);s->cname=xstrdup(buf);f->caps[f->ncaps].source=p;f->caps[f->ncaps].symbol=s;++f->ncaps;return s;}}}
    return &global_symbol;
}
static void cg_release(Cf*f,const char*t){sb_printf(&f->body,"    LC_RELEASE(ctx, %s);\n",t);}
static void cg_check(Cf*f){sb_add(&f->body,"    if (ctx->error) goto lc_done;\n");}
static char *cg_expr(Cf*f,Expr*e);
static char *cg_function(Cf*parent,Expr*e,const char*name);

static char *cg_var(Cf*f,Expr*e){Symbol*s=cf_resolve(f,e->name);char*t=cf_new_temp(f);if(s->kind==SYM_GLOBAL){char*k=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_string(ctx, \"%s\");\n",k,e->name);sb_printf(&f->body,"    %s = lc_table_get(ctx, ctx->globals, %s);\n",t,k);cg_release(f,k);}else sb_printf(&f->body,"    %s = lc_cell_get(%s);\n",t,s->cname);return t;}

static char *cg_expr(Cf*f,Expr*e){
    char*t,*a,*b;int i;
    if(!e)return NULL;
    switch(e->kind){
        case E_NIL:t=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_nil();\n",t);return t;
        case E_BOOL:t=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_bool(%d);\n",t,e->flag);return t;
        case E_NUMBER:t=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_number(%.17g);\n",t,e->number);return t;
        case E_STRING:t=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_string_n(ctx, \"",t);{const unsigned char*p=(unsigned char*)e->name;for(;p&&*p;++p){if(*p=='\\'||*p=='\"')sb_printf(&f->body,"\\%c",*p);else if(*p=='\n')sb_add(&f->body,"\\n");else if(*p=='\r')sb_add(&f->body,"\\r");else if(*p=='\t')sb_add(&f->body,"\\t");else if(*p<32||*p>126)sb_printf(&f->body,"\\%03o",*p);else sb_n(&f->body,(const char*)p,1);}}sb_printf(&f->body,"\", %zu);\n",strlen(e->name));return t;
        case E_VAR:return cg_var(f,e);
        case E_VARARG:t=cf_new_temp(f);if(!f->vararg){f->cg->failed=1;fprintf(stderr,"%s:%d: cannot use '...' outside a vararg function\n",f->cg->parser->filename,e->line);sb_printf(&f->body,"    %s = lc_nil();\n",t);}else sb_printf(&f->body,"    %s = lc_retain(lc_varargs);\n",t);return t;
        case E_UNARY:
            a=cg_expr(f,e->a);t=cf_new_temp(f);if(e->op==3)sb_printf(&f->body,"    %s = lc_first(%s);\n",t,a);else sb_printf(&f->body,"    %s = lc_unary(ctx, %d, %s);\n",t,e->op,a);cg_release(f,a);return t;
        case E_BINARY:
            if(e->op==14||e->op==15){a=cg_expr(f,e->a);t=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_first(%s);\n",t,a);cg_release(f,a);sb_printf(&f->body,"    if (%slc_truthy(%s)) {\n",e->op==14?"!":"",t);b=cg_expr(f,e->b);cg_release(f,t);sb_printf(&f->body,"        %s = lc_first(%s);\n",t,b);cg_release(f,b);sb_add(&f->body,"    }\n");return t;}
            a=cg_expr(f,e->a);b=cg_expr(f,e->b);t=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_binary(ctx, %d, %s, %s);\n",t,e->op,a,b);cg_release(f,a);cg_release(f,b);return t;
        case E_INDEX:a=cg_expr(f,e->a);b=cg_expr(f,e->b);t=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_table_get(ctx, %s, %s);\n",t,a,b);cg_release(f,a);cg_release(f,b);return t;
        case E_TABLE:
            t=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_table(ctx);\n",t);
            {int array_index=1;for(i=0;i<e->count;++i){Expr*ke=e->keys?e->keys[i]:NULL;char*k=NULL,*v;int expand=!ke&&i==e->count-1;
                if(ke)k=cg_expr(f,ke);else if(!expand){k=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_number(%d);\n",k,array_index);}
                v=cg_expr(f,e->items[i]);
                if(expand)sb_printf(&f->body,"    lc_table_add(ctx, %s, %d, %s);\n",t,array_index,v);
                else sb_printf(&f->body,"    lc_table_set(ctx, %s, %s, %s);\n",t,k,v);
                if(k){cg_release(f,k);}cg_release(f,v);if(!ke)array_index++;}}
            return t;
        case E_CALL:{
            char*fn,*self=NULL;int base=0;
            if(e->flag){self=cg_expr(f,e->a);{char*k=cf_new_temp(f);fn=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_string(ctx, \"%s\");\n",k,e->name);sb_printf(&f->body,"    %s = lc_table_get(ctx, %s, %s);\n",fn,self,k);cg_release(f,k);}base=1;}
            else fn=cg_expr(f,e->a);
            {int n=e->count+base;char**args=xmalloc(sizeof(char*)*(size_t)(n?n:1));if(base)args[0]=self;for(i=0;i<e->count;++i)args[i+base]=cg_expr(f,e->items[i]);t=cf_new_temp(f);sb_printf(&f->body,"    { LcValue args%d[] = { ",f->temp_id);for(i=0;i<n;++i)sb_printf(&f->body,"%s%s",i?", ":"",args[i]);sb_printf(&f->body," };\n      %s = lc_call_list(ctx, %s, %d, args%d); }\n",t,fn,n,f->temp_id);cg_release(f,fn);for(i=0;i<n;++i){cg_release(f,args[i]);}free(args);return t;}}
        case E_FUNCTION:return cg_function(f,e,"anonymous");
        default:t=cf_new_temp(f);return t;
    }
}

static void cg_stmts(Cf*f,Stmt*s);
static int cg_mark(Cf*f){return f->nlocals;}
static void cg_if_branch(Cf*f,Stmt*s,int i){
    char*t;
    if(i>=s->nblocks)return;
    if(!s->conds[i]){cg_stmts(f,s->blocks[i]);return;}
    t=cg_expr(f,s->conds[i]);
    sb_printf(&f->body,"    if (lc_truthy(%s)) {\n",t);
    cg_release(f,t);
    cg_stmts(f,s->blocks[i]);
    sb_add(&f->body,"    } else {\n");
    cg_if_branch(f,s,i+1);
    sb_add(&f->body,"    }\n");
}

static void cg_assign_values(Cf*f,Stmt*s,Symbol**targets,int declare){
    char**rhs=xmalloc(sizeof(char*)*(size_t)(s->nexprs?s->nexprs:1));int i,n=s->nnames;
    for(i=0;i<s->nexprs;++i)rhs[i]=cg_expr(f,s->exprs[i]);
    if(declare==2)for(i=0;i<n;++i){f->locals=xrealloc(f->locals,sizeof(Symbol*)*(size_t)(f->nlocals+1));f->locals[f->nlocals++]=targets[i];}
    for(i=0;i<n;++i){int ri=i<s->nexprs-1?i:s->nexprs-1;int index=i<s->nexprs-1?0:i-(s->nexprs-1);char*v=cf_new_temp(f);if(ri<0)sb_printf(&f->body,"    %s = lc_nil();\n",v);else sb_printf(&f->body,"    %s = lc_nth(%s, %d);\n",v,rhs[ri],index);if(declare)sb_printf(&f->body,"    lc_cell_replace(ctx, &%s, %s); %s = lc_nil();\n",targets[i]->cname,v,v);else sb_printf(&f->body,"    lc_cell_set(ctx, %s, %s); %s = lc_nil();\n",targets[i]->cname,v,v);}
    for(i=0;i<s->nexprs;++i){cg_release(f,rhs[i]);}free(rhs);
}

static void cg_stmt(Cf*f,Stmt*s){
    int i;char*t;
    sb_printf(&f->body,"    ctx->line = %d;\n",s->line);
    switch(s->kind){
        case S_LOCAL:{Symbol**targets=xmalloc(sizeof(Symbol*)*(size_t)s->nnames);for(i=0;i<s->nnames;++i)targets[i]=cf_local(f,s->names[i],0);cg_assign_values(f,s,targets,2);free(targets);break;}
        case S_LOCALFUNC:{Symbol*sym=cf_local(f,s->names[0],1);sb_printf(&f->body,"    lc_cell_replace(ctx, &%s, lc_nil());\n",sym->cname);t=cg_function(f,s->expr,s->names[0]);sb_printf(&f->body,"    lc_cell_set(ctx, %s, %s); %s = lc_nil();\n",sym->cname,t,t);break;}
        case S_ASSIGN:{
            int nt=s->flag,nr=s->nexprs-nt;char**rhs=xmalloc(sizeof(char*)*(size_t)(nr?nr:1));
            /* Function declarations use expr/expr2 to keep target and value separate. */
            if(nt==0&&s->expr&&s->expr2){Stmt tmp;memset(&tmp,0,sizeof(tmp));tmp.nexprs=1;tmp.exprs=&s->expr2;tmp.nnames=1;
                if(s->expr->kind==E_VAR){Symbol*sym=cf_resolve(f,s->expr->name);if(sym->kind==SYM_GLOBAL){char*k=cf_new_temp(f),*v=cg_expr(f,s->expr2);sb_printf(&f->body,"    %s = lc_string(ctx, \"%s\");\n",k,s->expr->name);sb_printf(&f->body,"    lc_table_set(ctx, ctx->globals, %s, %s);\n",k,v);cg_release(f,k);cg_release(f,v);}else{t=cg_expr(f,s->expr2);sb_printf(&f->body,"    lc_cell_set(ctx, %s, %s); %s = lc_nil();\n",sym->cname,t,t);}}
                else{char*obj=cg_expr(f,s->expr->a),*key=cg_expr(f,s->expr->b),*v=cg_expr(f,s->expr2);sb_printf(&f->body,"    lc_table_set(ctx, %s, %s, %s);\n",obj,key,v);cg_release(f,obj);cg_release(f,key);cg_release(f,v);}free(rhs);break;}
            for(i=0;i<nr;++i)rhs[i]=cg_expr(f,s->exprs[nt+i]);
            for(i=0;i<nt;++i){Expr*target=s->exprs[i];int ri=i<nr-1?i:nr-1,idx=i<nr-1?0:i-(nr-1);char*v=cf_new_temp(f);if(ri<0)sb_printf(&f->body,"    %s = lc_nil();\n",v);else sb_printf(&f->body,"    %s = lc_nth(%s, %d);\n",v,rhs[ri],idx);
                if(target->kind==E_VAR){Symbol*sym=cf_resolve(f,target->name);if(sym->kind==SYM_GLOBAL){char*k=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_string(ctx, \"%s\");\n",k,target->name);sb_printf(&f->body,"    lc_table_set(ctx, ctx->globals, %s, %s);\n",k,v);cg_release(f,k);}else sb_printf(&f->body,"    lc_cell_set(ctx, %s, %s);\n",sym->cname,v);cg_release(f,v);}
                else if(target->kind==E_INDEX){char*obj=cg_expr(f,target->a),*key=cg_expr(f,target->b);sb_printf(&f->body,"    lc_table_set(ctx, %s, %s, %s);\n",obj,key,v);cg_release(f,obj);cg_release(f,key);cg_release(f,v);}else{f->cg->failed=1;fprintf(stderr,"%s:%d: invalid assignment target\n",f->cg->parser->filename,s->line);cg_release(f,v);}}
            for(i=0;i<nr;++i){cg_release(f,rhs[i]);}free(rhs);break;}
        case S_EXPR:t=cg_expr(f,s->expr);cg_release(f,t);break;
        case S_IF:
            cg_if_branch(f,s,0);
            break;
        case S_WHILE:sb_add(&f->body,"    for (;;) {\n");t=cg_expr(f,s->expr);sb_printf(&f->body,"    if (!lc_truthy(%s)) { LC_RELEASE(ctx, %s); break; }\n",t,t);cg_release(f,t);++f->loop_depth;cg_stmts(f,s->body);--f->loop_depth;sb_add(&f->body,"    }\n");break;
        case S_REPEAT:{int mark=cg_mark(f);Stmt*it;sb_add(&f->body,"    for (;;) {\n");++f->loop_depth;for(it=s->body;it;it=it->next)cg_stmt(f,it);t=cg_expr(f,s->expr);--f->loop_depth;sb_printf(&f->body,"    if (lc_truthy(%s)) { LC_RELEASE(ctx, %s); break; }\n",t,t);cg_release(f,t);sb_add(&f->body,"    }\n");f->nlocals=mark;break;}
        case S_DO:sb_add(&f->body,"    {\n");cg_stmts(f,s->body);sb_add(&f->body,"    }\n");break;
        case S_FORNUM:{
            int mark=cg_mark(f);
            char*start=cg_expr(f,s->expr),*limit=cg_expr(f,s->expr2),*step=s->expr3?cg_expr(f,s->expr3):NULL;Symbol*var=cf_local(f,s->names[0],1);char*id=xstrdup(var->cname);
            if(!step){step=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_number(1);\n",step);}
            sb_printf(&f->body,"    { double start_%s = lc_to_number(ctx, %s, &(int){0});\n      double limit_%s = lc_to_number(ctx, %s, &(int){0});\n      double step_%s = lc_to_number(ctx, %s, &(int){0});\n      double i_%s;\n",id,start,id,limit,id,step,id);
            sb_printf(&f->body,"      for (i_%s = start_%s; (step_%s >= 0 ? i_%s <= limit_%s : i_%s >= limit_%s); i_%s += step_%s) {\n",id,id,id,id,id,id,id,id,id);
            sb_printf(&f->body,"        lc_cell_replace(ctx, &%s, lc_number(i_%s));\n",id,id);++f->loop_depth;cg_stmts(f,s->body);--f->loop_depth;sb_add(&f->body,"    } }\n");cg_release(f,start);cg_release(f,limit);cg_release(f,step);free(id);f->nlocals=mark;break;}
        case S_FORIN:{
            int mark=cg_mark(f);
            Stmt tmp;memset(&tmp,0,sizeof(tmp));tmp.exprs=s->exprs;tmp.nexprs=s->nexprs;tmp.nnames=3;
            {char**vals=xmalloc(sizeof(char*)*3);char**raw=xmalloc(sizeof(char*)*(size_t)(s->nexprs?s->nexprs:1));
             for(i=0;i<s->nexprs;++i)raw[i]=cg_expr(f,s->exprs[i]);
             for(i=0;i<3;++i){int ri=i<s->nexprs-1?i:s->nexprs-1,idx=i<s->nexprs-1?0:i-(s->nexprs-1);vals[i]=cf_new_temp(f);if(ri<0)sb_printf(&f->body,"    %s = lc_nil();\n",vals[i]);else sb_printf(&f->body,"    %s = lc_nth(%s, %d);\n",vals[i],raw[ri],idx);}
             for(i=0;i<s->nexprs;++i){cg_release(f,raw[i]);}free(raw);
             /* Normalize iterator/state/control from the expression list. */
             sb_add(&f->body,"    for (;;) {\n");t=cf_new_temp(f);sb_printf(&f->body,"    { LcValue args[] = { %s, %s }; %s = lc_call_list(ctx, %s, 2, args); }\n",vals[1],vals[2],t,vals[0]);
             {char*first=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_nth(%s, 0);\n",first,t);sb_printf(&f->body,"    if (%s.type == LC_NIL) { LC_RELEASE(ctx, %s); break; }\n",first,first);cg_release(f,vals[2]);sb_printf(&f->body,"    %s = lc_retain(%s);\n",vals[2],first);cg_release(f,first);}
             for(i=0;i<s->nnames;++i){Symbol*var=cf_local(f,s->names[i],1);char*v=cf_new_temp(f);sb_printf(&f->body,"    %s = lc_nth(%s, %d);\n",v,t,i);sb_printf(&f->body,"    lc_cell_replace(ctx, &%s, %s); %s = lc_nil();\n",var->cname,v,v);}
             cg_release(f,t);++f->loop_depth;cg_stmts(f,s->body);--f->loop_depth;sb_add(&f->body,"    }\n");for(i=0;i<3;++i){cg_release(f,vals[i]);}free(vals);}
            f->nlocals=mark;break;}
        case S_RETURN:
            if(!s->nexprs){sb_add(&f->body,"    lc_return_value = lc_nil();\n");}
            else if(s->nexprs==1){t=cg_expr(f,s->exprs[0]);sb_printf(&f->body,"    lc_return_value = %s; %s = lc_nil();\n",t,t);}
            else{char**vals=xmalloc(sizeof(char*)*(size_t)s->nexprs);for(i=0;i<s->nexprs;++i)vals[i]=cg_expr(f,s->exprs[i]);sb_printf(&f->body,"    { LcValue values[] = { ");for(i=0;i<s->nexprs;++i)sb_printf(&f->body,"%s%s",i?", ":"",vals[i]);sb_printf(&f->body," }; lc_return_value = lc_tuple_list(ctx, %d, values); }\n",s->nexprs);for(i=0;i<s->nexprs;++i){cg_release(f,vals[i]);}free(vals);}sb_add(&f->body,"    goto lc_done;\n");break;
        case S_BREAK:
            if(!f->loop_depth){f->cg->failed=1;fprintf(stderr,"%s:%d: 'break' outside a loop\n",f->cg->parser->filename,s->line);}
            else sb_add(&f->body,"    break;\n");
            break;
        default:break;
    }
    cg_check(f);
}
static void cg_stmts(Cf*f,Stmt*s){int mark=cg_mark(f);for(;s;s=s->next)cg_stmt(f,s);f->nlocals=mark;}

static char *cg_function(Cf*parent,Expr*e,const char*name){
    Cg*cg=parent->cg;Cf f;int i,id=++cg->function_id;char cname[64],*result;Str out;memset(&f,0,sizeof(f));f.cg=cg;f.parent=parent;f.vararg=e->flag;sb_init(&f.decl);sb_init(&f.body);sb_init(&f.cleanup);snprintf(cname,sizeof(cname),"lc_fn_%d",id);
    for(i=0;i<e->nparams;++i){Symbol*s=cf_local(&f,e->params[i],1);sb_printf(&f.body,"    lc_cell_replace(ctx, &%s, %d < argc ? lc_first(argv[%d]) : lc_nil());\n",s->cname,i,i);}
    if(e->flag)sb_add(&f.body,"    { int n = argc > LC_PARAM_COUNT ? argc - LC_PARAM_COUNT : 0; lc_varargs = lc_tuple_new(ctx, n, argv + LC_PARAM_COUNT, 0); }\n");
    cg_stmts(&f,e->body);
    sb_init(&out);sb_printf(&out,"static LcValue %s(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {\n",cname);
    sb_add(&out,f.decl.data?f.decl.data:"");
    if(e->flag)sb_add(&out,"    LcValue lc_varargs = {0};\n");
    sb_add(&out,"    LcValue lc_return_value = {0};\n    (void)self;\n");
    if(f.body.data&&e->flag){const char*src=f.body.data;while(*src){const char*hit=strstr(src,"LC_PARAM_COUNT");if(!hit){sb_add(&out,src);break;}sb_n(&out,src,(size_t)(hit-src));sb_printf(&out,"%d",e->nparams);src=hit+strlen("LC_PARAM_COUNT");}}else sb_add(&out,f.body.data?f.body.data:"");
    sb_add(&out,"lc_done:\n");if(e->flag)sb_add(&out,"    LC_RELEASE(ctx, lc_varargs);\n");sb_add(&out,f.cleanup.data?f.cleanup.data:"");sb_add(&out,"    return lc_return_value;\n}\n\n");
    sb_add(&cg->functions,out.data);sb_free(&out);
    result=cf_new_temp(parent);if(f.ncaps){sb_printf(&parent->body,"    { LcCell *caps[] = { ");for(i=0;i<f.ncaps;++i)sb_printf(&parent->body,"%s%s",i?", ":"",f.caps[i].source->cname);sb_printf(&parent->body," };\n      %s = lc_function_new(ctx, %s, %d, caps, \"%s\"); }\n",result,cname,f.ncaps,name);}else sb_printf(&parent->body,"    %s = lc_function_new(ctx, %s, 0, NULL, \"%s\");\n",result,cname,name);
    cf_dispose(&f);
    return result;
}

static const char *luac_executable_path;
static const char *runtime_header_path(void) {
    static char resolved[PATH_MAX];
    const char *environment = getenv("LUAC_RUNTIME");
    char *slash;
    if (environment && *environment) return environment;
    if (access(LUAC_RUNTIME_PATH, R_OK) == 0) return LUAC_RUNTIME_PATH;
    if (luac_executable_path) {
        snprintf(resolved, sizeof(resolved), "%s", luac_executable_path);
        slash = strrchr(resolved, '/');
#ifdef _WIN32
        { char *backslash = strrchr(resolved, '\\'); if (backslash && (!slash || backslash > slash)) slash = backslash; }
#endif
        if (slash) {
            *slash = 0;
            { size_t used = strlen(resolved); snprintf(resolved + used, sizeof(resolved) - used, "/runtime.h"); }
            if (access(resolved, R_OK) == 0) {
#ifdef _WIN32
                char *p; for (p = resolved; *p; ++p) if (*p == '\\') *p = '/';
#endif
                return resolved;
            }
        }
    }
    if (access("runtime.h", R_OK) == 0) return "runtime.h";
    return LUAC_RUNTIME_PATH;
}

static char *generate_c(Parser*p,Stmt*program,const char*source){
    Cg cg;Cf root;Expr mainexpr;char*entry;Str out;memset(&cg,0,sizeof(cg));cg.parser=p;sb_init(&cg.functions);memset(&root,0,sizeof(root));root.cg=&cg;sb_init(&root.decl);sb_init(&root.body);sb_init(&root.cleanup);memset(&mainexpr,0,sizeof(mainexpr));mainexpr.kind=E_FUNCTION;mainexpr.body=program;mainexpr.line=1;
    /* Root has no parent and acts as the main native function. */
    {int i;Cf*f=&root;f->vararg=0;(void)i;cg_stmts(f,program);sb_init(&out);sb_add(&out,"/* Generated by LuaC. Ownership cleanup is emitted automatically. */\n#include \"");
    sb_add(&out,runtime_header_path());
    sb_add(&out,"\"\n\n#ifndef LC_RELEASE\n#define LC_RELEASE(ctx, v) do { lc_release((ctx), (v)); (v) = lc_nil(); } while (0)\n#endif\n\n");sb_add(&out,cg.functions.data?cg.functions.data:"");sb_add(&out,"static LcValue lc_user_main(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {\n");sb_add(&out,f->decl.data?f->decl.data:"");sb_add(&out,"    LcValue lc_return_value = {0};\n    (void)self; (void)argc; (void)argv;\n");sb_add(&out,f->body.data?f->body.data:"");sb_add(&out,"lc_done:\n");sb_add(&out,f->cleanup.data?f->cleanup.data:"");sb_add(&out,"    return lc_return_value;\n}\n\n");sb_printf(&out,"int lc_program_main(LcContext *ctx) {\n    LcValue r = lc_user_main(ctx, NULL, 0, NULL);\n    lc_release(ctx, r);\n    return lc_report_error(ctx);\n}\n\n#if defined(_WIN32)\n__declspec(dllexport)\n#else\n__attribute__((visibility(\"default\")))\n#endif\nint lc_jit_main(void) {\n    LcContext *ctx = lc_context_new(\"%s\");\n    int status;\n    if (!ctx) return 1;\n    status = lc_program_main(ctx);\n    lc_context_destroy(ctx);\n    return status;\n}\n\n#ifdef LUAC_STANDALONE\nint main(void) { return lc_jit_main(); }\n#endif\n",source);}
    entry=out.data;cf_dispose(&root);sb_free(&cg.functions);(void)entry;
    if(cg.failed){sb_free(&out);return NULL;}
    return out.data;
}

/* ----------------------------- Public C API ----------------------------- */
static void api_parse(Parser *p, const char *source, const char *filename, Stmt **program) {
    memset(p, 0, sizeof(*p));
    p->source = source ? source : "";
    p->filename = filename ? filename : "input.lua";
    p->length = strlen(p->source);
    p->line = p->column = 1;
    next_token(p);
    *program = parse_block(p);
    if (p->token.kind != T_EOF) parse_error(p, "unexpected '%s'", token_name(p->token.kind));
}

LUAC_API char *luac_compile_c(const char *source, const char *filename, char **error_message) {
    Parser p;
    Stmt *program;
    char *result;
    if (error_message) *error_message = NULL;
    if (!source) {
        if (error_message) *error_message = xstrdup("source must not be null");
        return NULL;
    }
    api_parse(&p, source, filename, &program);
    if (p.failed) {
        if (error_message) *error_message = xstrdup(p.error);
        token_clear(&p.token); free_stmt(program); return NULL;
    }
    result = generate_c(&p, program, p.filename);
    if (!result && error_message) *error_message = xstrdup("code generation failed");
    token_clear(&p.token); free_stmt(program);
    return result;
}

LUAC_API int luac_check(const char *source, const char *filename, char **error_message) {
    Parser p;
    Stmt *program;
    int ok;
    if (error_message) *error_message = NULL;
    if (!source) {
        if (error_message) *error_message = xstrdup("source must not be null");
        return 0;
    }
    api_parse(&p, source, filename, &program);
    ok = !p.failed;
    if (!ok && error_message) *error_message = xstrdup(p.error);
    token_clear(&p.token); free_stmt(program);
    return ok;
}

LUAC_API const char *luac_version(void) { return LUAC_VERSION; }
LUAC_API void luac_free(void *memory) { free(memory); }

/* -------------------------------- Driver -------------------------------- */
#ifndef LUAC_LIBRARY_ONLY
#ifndef _WIN32
/* mkdtemp is a BSD extension on macOS and is hidden by POSIX feature macros. */
extern char *mkdtemp(char *template_name);
#endif
#ifdef _WIN32
#include <direct.h>
#define RTLD_NOW 0
#define RTLD_LOCAL 0
static HMODULE luac_library;
static char luac_dl_error[256];
static void *dlopen(const char *path, int mode) {
    (void)mode; luac_library = LoadLibraryA(path);
    if (!luac_library) snprintf(luac_dl_error, sizeof(luac_dl_error), "LoadLibrary error %lu", GetLastError());
    return (void *)luac_library;
}
static void *dlsym(void *handle, const char *name) {
    return (void *)GetProcAddress((HMODULE)handle, name);
}
static int dlclose(void *handle) { return FreeLibrary((HMODULE)handle) ? 0 : -1; }
static char *dlerror(void) { return luac_dl_error; }
static char *luac_mkdtemp(char *template_name) {
    char *where = strstr(template_name, "XXXXXX");
    unsigned int attempt;
    if (!where) return NULL;
    for (attempt = 0; attempt < 100; ++attempt) {
        unsigned int value = (unsigned int)GetTickCount() + attempt * 7919u;
        int i;
        for (i = 0; i < 6; ++i) { where[i] = (char)('A' + value % 26); value /= 26; }
        if (_mkdir(template_name) == 0) return template_name;
        if (errno != EEXIST) return NULL;
    }
    return NULL;
}
#define mkdtemp luac_mkdtemp
#endif
static char *read_file(const char*path,size_t*n){FILE*f=fopen(path,"rb");char*s;long size;if(!f){fprintf(stderr,"luac: cannot open %s: %s\n",path,strerror(errno));return NULL;}fseek(f,0,SEEK_END);size=ftell(f);fseek(f,0,SEEK_SET);if(size<0){fclose(f);return NULL;}s=xmalloc((size_t)size+1);if(fread(s,1,(size_t)size,f)!=(size_t)size){fclose(f);free(s);return NULL;}s[size]=0;fclose(f);*n=(size_t)size;return s;}
static int write_file(const char*path,const char*data,size_t n){FILE*f=fopen(path,"wb");if(!f)return 0;if(fwrite(data,1,n,f)!=n){fclose(f);return 0;}return fclose(f)==0;}
static char *shell_quote(const char*s){
#ifdef _WIN32
    Str out;const char*p;sb_init(&out);sb_add(&out,"\"");
    for(p=s;*p;++p){if(*p=='\"'||*p=='\\')sb_add(&out,"\\");sb_n(&out,p,1);}sb_add(&out,"\"");return out.data;
#else
    Str out;const char*p;sb_init(&out);sb_add(&out,"'");
    for(p=s;*p;++p){if(*p=='\'')sb_add(&out,"'\\''");else sb_n(&out,p,1);}sb_add(&out,"'");return out.data;
#endif
}
static int run_command(const char*cmd){int r=system(cmd);if(r==-1)return -1;return r;}
static void usage(void){puts("LuaC "LUAC_VERSION" — Lua-syntax native compiler without a tracing GC\n\nUsage:\n  luac file.lua              JIT compile and run\n  luac --check file.lua      Parse and validate\n  luac --emit-c file.lua [-o out.c]\n  luac --aot file.lua -o program\n\nOptions: --cc COMPILER  --keep-c PATH  --version");}

int main(int argc,char**argv){
    const char*input=NULL,*output=NULL,*keep=NULL,*cc=getenv("CC");enum{MODE_JIT,MODE_CHECK,MODE_C,MODE_AOT}mode=MODE_JIT;int i,status=1;size_t length;char*source,*generated;Parser p;Stmt*program;
    if(!cc||!*cc)cc="cc";
    luac_executable_path = argc > 0 ? argv[0] : NULL;
    for(i=1;i<argc;++i){if(!strcmp(argv[i],"--check"))mode=MODE_CHECK;else if(!strcmp(argv[i],"--emit-c"))mode=MODE_C;else if(!strcmp(argv[i],"--aot"))mode=MODE_AOT;else if(!strcmp(argv[i],"--jit"))mode=MODE_JIT;else if(!strcmp(argv[i],"-o")&&i+1<argc)output=argv[++i];else if(!strcmp(argv[i],"--keep-c")&&i+1<argc)keep=argv[++i];else if(!strcmp(argv[i],"--cc")&&i+1<argc)cc=argv[++i];else if(!strcmp(argv[i],"--version")){puts("LuaC "LUAC_VERSION);return 0;}else if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){usage();return 0;}else if(argv[i][0]=='-'){fprintf(stderr,"luac: unknown option %s\n",argv[i]);return 2;}else if(!input)input=argv[i];else{fprintf(stderr,"luac: too many input files\n");return 2;}}
    if(!input){usage();return 2;}source=read_file(input,&length);if(!source)return 1;
    memset(&p,0,sizeof(p));p.source=source;p.filename=input;p.length=length;p.line=1;p.column=1;next_token(&p);program=parse_block(&p);if(p.token.kind!=T_EOF)parse_error(&p,"unexpected '%s'",token_name(p.token.kind));
    if(p.failed){fprintf(stderr,"%s\n",p.error);token_clear(&p.token);free_stmt(program);free(source);return 1;}
    if(mode==MODE_CHECK){printf("%s: syntax OK\n",input);token_clear(&p.token);free_stmt(program);free(source);return 0;}
    generated=generate_c(&p,program,input);
    if(!generated){status=1;goto done;}
    if(mode==MODE_C){const char*out=output?output:"a.luac.c";if(!write_file(out,generated,strlen(generated))){fprintf(stderr,"luac: cannot write %s\n",out);goto done;}printf("generated %s\n",out);status=0;goto done;}
    {
#ifdef _WIN32
     char tmp[MAX_PATH];const char*temp_dir=getenv("TEMP");if(!temp_dir)temp_dir=".";snprintf(tmp,sizeof(tmp),"%s/luac-XXXXXX",temp_dir);
#else
     char tmp[]="/tmp/luac-XXXXXX";
#endif
     char*cfile,*sofile=NULL,*qcc,*qc,*qso,*qout;Str cmd;int cr;
     if(keep)cfile=xstrdup(keep);else{if(!mkdtemp(tmp)){fprintf(stderr,"luac: mkdtemp: %s\n",strerror(errno));goto done;}cfile=xmalloc(strlen(tmp)+8);sprintf(cfile,"%s/main.c",tmp);}
     if(!write_file(cfile,generated,strlen(generated))){fprintf(stderr,"luac: cannot write %s\n",cfile);free(cfile);goto done;}
     qcc=shell_quote(cc);qc=shell_quote(cfile);sb_init(&cmd);
     if(mode==MODE_AOT){if(!output){fprintf(stderr,"luac: --aot requires -o OUTPUT\n");free(cfile);free(qcc);free(qc);sb_free(&cmd);goto done;}qout=shell_quote(output);sb_printf(&cmd,"%s -std=gnu99 -O2 -DLUAC_STANDALONE %s -o %s -lm",qcc,qc,qout);free(qout);}
     else{sofile=xmalloc(strlen(cfile)+5);
#ifdef _WIN32
      sprintf(sofile,"%s.dll",cfile);
#else
      sprintf(sofile,"%s.so",cfile);
#endif
      qso=shell_quote(sofile);sb_printf(&cmd,"%s -std=gnu99 -O2 -fPIC -shared %s -o %s -lm",qcc,qc,qso);free(qso);}
     cr=run_command(cmd.data);sb_free(&cmd);free(qcc);free(qc);
     if(cr!=0){fprintf(stderr,"luac: native compilation failed\n");if(!keep){unlink(cfile);if(sofile)unlink(sofile);rmdir(tmp);}free(cfile);free(sofile);goto done;}
     if(mode==MODE_AOT){printf("built %s\n",output);status=0;if(!keep){unlink(cfile);rmdir(tmp);}free(cfile);goto done;}
     {void*handle=dlopen(sofile,RTLD_NOW|RTLD_LOCAL);int(*entry)(void);
      if(!handle){fprintf(stderr,"luac: dlopen: %s\n",dlerror());free(cfile);free(sofile);goto done;}
      *(void**)(&entry)=dlsym(handle,"lc_jit_main");
      if(!entry){fprintf(stderr,"luac: JIT entry point is missing\n");dlclose(handle);free(cfile);free(sofile);goto done;}
      status=entry();
      dlclose(handle);
      if(!keep){unlink(cfile);if(*tmp)rmdir(tmp);}unlink(sofile);
      free(cfile);free(sofile);goto done;}
    }
done:
    free(generated);token_clear(&p.token);free_stmt(program);free(source);return status;
}
#endif /* LUAC_LIBRARY_ONLY */
