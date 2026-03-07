#ifndef _SULUSCRIPT_H_
#define _SULUSCRIPT_H_

#include "kernel/types.h"

// --- Lexer Section ---

typedef enum {
    TOKEN_EOF = 0,
    TOKEN_IDENT,
    TOKEN_INT,
    TOKEN_HEX,
    TOKEN_STRING,
    
    // Keywords
    TOKEN_WINDOW,
    TOKEN_LAYOUT,
    TOKEN_FN,
    TOKEN_VAR,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_RETURN,
    TOKEN_HOOK,
    
    // UI Elements
    TOKEN_VBOX,
    TOKEN_HBOX,
    TOKEN_TEXT,
    TOKEN_BUTTON,
    TOKEN_PROGRESS,
    TOKEN_RECT,
    TOKEN_TEXTBOX,

    // Punctuation
    TOKEN_LBRACE,   // {
    TOKEN_RBRACE,   // }
    TOKEN_LPAREN,   // (
    TOKEN_RPAREN,   // )
    TOKEN_COLON,    // :
    TOKEN_COMMA,    // ,
    TOKEN_EQUAL,    // =
    TOKEN_PLUS,     // +
    TOKEN_MINUS,    // -
    TOKEN_MOD,      // %
    TOKEN_SEMICOLON, // ;
    TOKEN_LESS,           // <
    TOKEN_GREATER,        // >
    TOKEN_LESS_EQUAL,     // <=
    TOKEN_GREATER_EQUAL,  // >=
    TOKEN_DBL_EQUAL,      // ==
    TOKEN_BANG,      // !
    TOKEN_BANG_EQUAL, // !=
    TOKEN_STAR,      // *
    TOKEN_SLASH,     // /
    TOKEN_LBRACKET,  // [
    TOKEN_RBRACKET,  // ]
    TOKEN_AND,       // &&
    TOKEN_OR,        // ||
    TOKEN_FOR,
    
    TOKEN_ERROR
} token_type_t;

struct token {
    token_type_t type;
    char *start;
    int len;
    int val_int;
};

// --- AST Section ---

typedef enum {
    NODE_PROGRAM,
    NODE_WINDOW_DEF,
    NODE_LAYOUT_DEF,
    NODE_VAR_DEF,
    NODE_FN_DEF,
    NODE_HOOK_DEF,
    
    // UI Nodes
    NODE_VBOX,
    NODE_HBOX,
    NODE_TEXT,
    NODE_BUTTON,
    NODE_PROGRESS,
    NODE_RECT,
    NODE_TEXTBOX,

    // Logic/Expression Nodes
    NODE_BLOCK,
    NODE_CALL,
    NODE_BINOP,
    NODE_IDENT,
    NODE_LIT_INT,
    NODE_LIT_STR,
    NODE_RETURN,
    NODE_IF,
    NODE_ASSIGN,
    NODE_FOR,
    NODE_ARRAY_REF
} node_type_t;

struct sulu_prop {
    char name[32];
    int val_int;
    char *val_str;
    struct sulu_node *val_expr;
    struct sulu_prop *next;
};

struct sulu_node {
    node_type_t type;
    struct sulu_prop *props;
    struct sulu_node *children; // First child
    struct sulu_node *next;     // Sibling
    
    // Content data
    char *ident;
    int val_int;
    char *val_str;
    char op; // for BINOP
    struct sulu_node *expr_left;
    struct sulu_node *expr_right;

    // Layout info (calculated during layout pass)
    int x, y, w, h;
};

struct lexer {
    char *source;
    char *curr;
    int line;
};

void suluscript_lexer_init(struct lexer *l, char *source);
struct token suluscript_lexer_next(struct lexer *l);

struct sulu_node* suluscript_parse(char *source);
void suluscript_free_node(struct sulu_node *node);

#endif
