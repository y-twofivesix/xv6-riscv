#include "kernel/types.h"
#include "user/user.h"
#include "user/suluscript.h"

struct parser {
    struct lexer *lexer;
    struct token curr;
    struct token next;
};

static void advance(struct parser *p) {
    p->curr = p->next;
    p->next = suluscript_lexer_next(p->lexer);
}

static int match(struct parser *p, token_type_t type) {
    if (p->curr.type == type) {
        advance(p);
        return 1;
    }
    return 0;
}

static struct sulu_node* alloc_node(node_type_t type) {
    struct sulu_node *n = malloc(sizeof(struct sulu_node));
    memset(n, 0, sizeof(struct sulu_node));
    n->type = type;
    return n;
}

static void add_child(struct sulu_node *parent, struct sulu_node *child) {
    if (!parent->children) {
        parent->children = child;
    } else {
        struct sulu_node *prev = parent->children;
        while (prev->next) prev = prev->next;
        prev->next = child;
    }
}

static void add_prop(struct sulu_node *node, char *name, int vi, char *vs, struct sulu_node *ve) {
    struct sulu_prop *p = malloc(sizeof(struct sulu_prop));
    memset(p, 0, sizeof(struct sulu_prop));
    strncpy(p->name, name, 31);
    p->val_int = vi;
    p->val_expr = ve;
    if (vs) {
        p->val_str = malloc(strlen(vs) + 1);
        strcpy(p->val_str, vs);
    }
    p->next = node->props;
    node->props = p;
}

// Forward declarations
static struct sulu_node* parse_block(struct parser *p);
static struct sulu_node* parse_expression(struct parser *p);
static struct sulu_node* parse_statement(struct parser *p);
static struct sulu_node* parse_for(struct parser *p);
static struct sulu_node* parse_if(struct parser *p);
static struct sulu_node* parse_binary(struct parser *p, int min_prec);
static struct sulu_node* parse_primary(struct parser *p);

static void parse_one_prop(struct parser *p, struct sulu_node *n) {
    if (p->curr.type == TOKEN_IDENT) {
        char key[32];
        int klen = p->curr.len > 31 ? 31 : p->curr.len;
        memmove(key, p->curr.start, klen);
        key[klen] = 0;
        advance(p);
        if (match(p, TOKEN_COLON)) {
            if (p->curr.type == TOKEN_STRING) {
                char *v = malloc(p->curr.len + 1);
                memmove(v, p->curr.start, p->curr.len);
                v[p->curr.len] = 0;
                add_prop(n, key, 0, v, 0);
                free(v);
                advance(p);
            } else if (p->curr.type == TOKEN_INT || p->curr.type == TOKEN_HEX) {
                add_prop(n, key, p->curr.val_int, 0, 0);
                advance(p);
            } else {
                // Parse as expression
                struct sulu_node *expr = parse_expression(p);
                add_prop(n, key, 0, 0, expr);
            }
        }
    }
}

static struct sulu_node* parse_ui_element(struct parser *p) {
    node_type_t type;
    switch(p->curr.type) {
        case TOKEN_VBOX: type = NODE_VBOX; break;
        case TOKEN_HBOX: type = NODE_HBOX; break;
        case TOKEN_TEXT: type = NODE_TEXT; break;
        case TOKEN_BUTTON: type = NODE_BUTTON; break;
        case TOKEN_PROGRESS: type = NODE_PROGRESS; break;
        case TOKEN_RECT: type = NODE_RECT; break;
        case TOKEN_TEXTBOX: type = NODE_TEXTBOX; break;
        case TOKEN_FOR: return parse_for(p);
        case TOKEN_IF: return parse_if(p);
        default: return 0;
    }
    advance(p);
    struct sulu_node *n = alloc_node(type);

    // Optional properties in parens: text("hello") or button(label: "txt")
    if (match(p, TOKEN_LPAREN)) {
        while (p->curr.type != TOKEN_RPAREN && p->curr.type != TOKEN_EOF) {
            if (p->curr.type == TOKEN_STRING) {
                // Positional string (e.g. text("hello"))
                char *s = malloc(p->curr.len + 1);
                memmove(s, p->curr.start, p->curr.len);
                s[p->curr.len] = 0;
                add_prop(n, "content", 0, s, 0);
                free(s);
                advance(p);
            } else {
                parse_one_prop(p, n);
            }
            if (p->curr.type == TOKEN_COMMA) advance(p);
            else break;
        }
        match(p, TOKEN_RPAREN);
    }

    // Optional block for nesting: vbox { ... }
    if (match(p, TOKEN_LBRACE)) {
        while (p->curr.type != TOKEN_RBRACE && p->curr.type != TOKEN_EOF) {
            struct sulu_node *child = parse_statement(p);
            if (child) {
                add_child(n, child);
                match(p, TOKEN_SEMICOLON); // UI elements might have optional semicolons
            } else {
                break;
            }
        }
        match(p, TOKEN_RBRACE);
    }
    
    return n;
}

static struct sulu_node* parse_layout(struct parser *p) {
    if (!match(p, TOKEN_LAYOUT)) return 0;
    struct sulu_node *n = alloc_node(NODE_LAYOUT_DEF);

    // Optional properties: layout(padding: 10)
    if (match(p, TOKEN_LPAREN)) {
        while (p->curr.type != TOKEN_RPAREN && p->curr.type != TOKEN_EOF) {
            parse_one_prop(p, n);
            if (!match(p, TOKEN_COMMA)) break;
        }
        match(p, TOKEN_RPAREN);
    }

    if (match(p, TOKEN_LBRACE)) {
        while (p->curr.type != TOKEN_RBRACE && p->curr.type != TOKEN_EOF) {
            // Skip any semicolons between elements
            while (p->curr.type == TOKEN_SEMICOLON) advance(p);
            if (p->curr.type == TOKEN_RBRACE) break;
            
            struct sulu_node *child = parse_statement(p);
            if (child) add_child(n, child);
            else {
                advance(p); // Skip unknown token to avoid infinite loop
            }
            // Also skip trailing semicolons
            while (p->curr.type == TOKEN_SEMICOLON) advance(p);
        }
        match(p, TOKEN_RBRACE);
    }
    return n;
}

static struct sulu_node* parse_window(struct parser *p) {
    if (!match(p, TOKEN_WINDOW)) return 0;
    struct sulu_node *n = alloc_node(NODE_WINDOW_DEF);
    if (match(p, TOKEN_LBRACE)) {
        while (p->curr.type != TOKEN_RBRACE && p->curr.type != TOKEN_EOF) {
            parse_one_prop(p, n);
            if (!match(p, TOKEN_COMMA)) {
                if (p->curr.type == TOKEN_SEMICOLON) advance(p);
                else break;
            }
        }
        match(p, TOKEN_RBRACE);
    }
    return n;
}

static struct sulu_node* parse_var(struct parser *p) {
    if (!match(p, TOKEN_VAR)) return 0;
    struct sulu_node *n = alloc_node(NODE_VAR_DEF);
    if (p->curr.type == TOKEN_IDENT) {
        n->ident = malloc(p->curr.len + 1);
        memmove(n->ident, p->curr.start, p->curr.len);
        n->ident[p->curr.len] = 0;
        advance(p);
        if (match(p, TOKEN_EQUAL)) {
            n->expr_left = parse_expression(p);
        }
        match(p, TOKEN_SEMICOLON);
    }
    return n;
}

static struct sulu_node* parse_fn(struct parser *p) {
    if (!match(p, TOKEN_FN)) return 0;
    struct sulu_node *n = alloc_node(NODE_FN_DEF);
    if (p->curr.type == TOKEN_IDENT) {
        n->ident = malloc(p->curr.len + 1);
        memmove(n->ident, p->curr.start, p->curr.len);
        n->ident[p->curr.len] = 0;
        advance(p);
        
        // Params (a: int, b: str)
        if (match(p, TOKEN_LPAREN)) {
            while (p->curr.type != TOKEN_RPAREN && p->curr.type != TOKEN_EOF) {
                if (p->curr.type == TOKEN_IDENT) {
                    struct sulu_node *param = alloc_node(NODE_IDENT);
                    param->ident = malloc(p->curr.len + 1);
                    memmove(param->ident, p->curr.start, p->curr.len);
                    param->ident[p->curr.len] = 0;
                    add_child(n, param);
                    advance(p);
                    if (match(p, TOKEN_COLON)) {
                        // Type (ignored for now)
                        advance(p);
                    }
                }
                if (!match(p, TOKEN_COMMA)) break;
            }
            match(p, TOKEN_RPAREN);
        }
        
        n->children = parse_block(p);
    }
    return n;
}

static struct sulu_node* parse_primary(struct parser *p) {
    struct sulu_node *n = 0;
    if (p->curr.type == TOKEN_INT || p->curr.type == TOKEN_HEX) {
        n = alloc_node(NODE_LIT_INT);
        n->val_int = p->curr.val_int;
        advance(p);
    } else if (p->curr.type == TOKEN_STRING) {
        n = alloc_node(NODE_LIT_STR);
        n->val_str = malloc(p->curr.len + 1);
        memmove(n->val_str, p->curr.start, p->curr.len);
        n->val_str[p->curr.len] = 0;
        advance(p);
    } else if (p->curr.type == TOKEN_IDENT) {
        char *name = malloc(p->curr.len + 1);
        memmove(name, p->curr.start, p->curr.len);
        name[p->curr.len] = 0;
        advance(p);

        if (match(p, TOKEN_LPAREN)) {
            // Function call: func(arg1, arg2)
            n = alloc_node(NODE_CALL);
            n->ident = name;
            while (p->curr.type != TOKEN_RPAREN && p->curr.type != TOKEN_EOF) {
                struct sulu_node *arg = parse_expression(p);
                if (arg) add_child(n, arg);
                if (!match(p, TOKEN_COMMA)) break;
            }
            match(p, TOKEN_RPAREN);
        } else if (match(p, TOKEN_LBRACKET)) {
            // Array reference: arr[idx]
            n = alloc_node(NODE_ARRAY_REF);
            n->ident = name;
            n->expr_left = parse_expression(p);
            match(p, TOKEN_RBRACKET);
        } else {
            // Simple identifier
            n = alloc_node(NODE_IDENT);
            n->ident = name;
        }
    } else if (match(p, TOKEN_LPAREN)) {
        n = parse_expression(p);
        match(p, TOKEN_RPAREN);
    }
    return n;
}

static struct sulu_node* parse_for(struct parser *p) {
    if (!match(p, TOKEN_FOR)) return 0;
    struct sulu_node *n = alloc_node(NODE_FOR);
    match(p, TOKEN_LPAREN);
    // for(var i = 0; i < 10; i = i + 1)
    n->children = parse_statement(p); // Init (e.g. var i = 0;)
    n->expr_left = parse_expression(p); // Cond (e.g. i < 10)
    match(p, TOKEN_SEMICOLON);
    n->expr_right = parse_expression(p); // Post (e.g. i = i + 1)
    match(p, TOKEN_RPAREN);
    
    // The block is the next level of children after n->children
    struct sulu_node *block = parse_block(p);
    if(n->children) {
        struct sulu_node *init = n->children;
        init->next = block;
    } else {
        n->children = block;
    }
    return n;
}

static struct sulu_node* parse_if(struct parser *p) {
    if (!match(p, TOKEN_IF)) return 0;
    struct sulu_node *n = alloc_node(NODE_IF);
    match(p, TOKEN_LPAREN);
    n->expr_left = parse_expression(p);
    match(p, TOKEN_RPAREN);
    n->children = parse_block(p);
    return n;
}

static struct sulu_node* parse_statement(struct parser *p) {
    while (match(p, TOKEN_SEMICOLON)); // Skip empty statements
    if (p->curr.type == TOKEN_VAR) return parse_var(p);
    if (p->curr.type == TOKEN_IF) return parse_if(p);
    if (p->curr.type == TOKEN_FOR) return parse_for(p);
    
    // UI Elements as statements (useful in layout loops)
    if (p->curr.type == TOKEN_VBOX || p->curr.type == TOKEN_HBOX ||
        p->curr.type == TOKEN_TEXT || p->curr.type == TOKEN_BUTTON ||
        p->curr.type == TOKEN_RECT || p->curr.type == TOKEN_TEXTBOX) {
        return parse_ui_element(p);
    }

    if (p->curr.type == TOKEN_RETURN) {
        advance(p);
        struct sulu_node *n = alloc_node(NODE_RETURN);
        n->expr_left = parse_expression(p);
        match(p, TOKEN_SEMICOLON);
        return n;
    }
    // Expression statement
    struct sulu_node *n = parse_expression(p);
    match(p, TOKEN_SEMICOLON);
    return n;
}

static struct sulu_node* parse_expression(struct parser *p) {
    if (p->curr.type == TOKEN_IDENT && p->next.type == TOKEN_EQUAL) {
        char *name = malloc(p->curr.len + 1);
        memmove(name, p->curr.start, p->curr.len);
        name[p->curr.len] = 0;
        advance(p); // ident
        advance(p); // =
        struct sulu_node *n = alloc_node(NODE_ASSIGN);
        n->ident = name;
        n->expr_left = parse_expression(p);
        return n;
    }
    return parse_binary(p, 0);
}

static int get_precedence(token_type_t type) {
    switch(type) {
        case TOKEN_OR:        return 1;
        case TOKEN_AND:       return 2;
        case TOKEN_DBL_EQUAL:
        case TOKEN_BANG_EQUAL: return 3;
        case TOKEN_LESS:
        case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER_EQUAL: return 4;
        case TOKEN_PLUS:
        case TOKEN_MINUS:     return 5;
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_MOD:       return 6;
        default: return 0;
    }
}

static struct sulu_node* parse_binary(struct parser *p, int min_prec) {
    struct sulu_node *left = parse_primary(p);

    while (1) {
        int prec = get_precedence(p->curr.type);
        if (prec <= min_prec) break;

        token_type_t type = p->curr.type;
        char op = '+';
        if (type == TOKEN_MINUS)      op = '-';
        else if (type == TOKEN_STAR)  op = '*';
        else if (type == TOKEN_SLASH) op = '/';
        else if (type == TOKEN_MOD)   op = '%';
        else if (type == TOKEN_LESS)           op = '<';
        else if (type == TOKEN_GREATER)        op = '>';
        else if (type == TOKEN_LESS_EQUAL)     op = 'L';
        else if (type == TOKEN_GREATER_EQUAL)  op = 'G';
        else if (type == TOKEN_DBL_EQUAL)  op = '=';
        else if (type == TOKEN_BANG_EQUAL) op = '!';
        else if (type == TOKEN_AND)        op = '&';
        else if (type == TOKEN_OR)         op = '|';

        advance(p);
        struct sulu_node *bin = alloc_node(NODE_BINOP);
        bin->op = op;
        bin->expr_left = left;
        bin->expr_right = parse_binary(p, prec);
        left = bin;
    }
    return left;
}

static struct sulu_node* parse_block(struct parser *p) {
    if (!match(p, TOKEN_LBRACE)) return 0;
    struct sulu_node *n = alloc_node(NODE_BLOCK);
    while (p->curr.type != TOKEN_RBRACE && p->curr.type != TOKEN_EOF) {
        struct sulu_node *stmt = parse_statement(p);
        if (stmt) add_child(n, stmt);
        else break;
    }
    match(p, TOKEN_RBRACE);
    return n;
}

struct sulu_node* suluscript_parse(char *source) {
    struct lexer lex;
    suluscript_lexer_init(&lex, source);
    struct parser p;
    p.lexer = &lex;
    p.next = suluscript_lexer_next(&lex);
    advance(&p); // Initialize curr and next

    struct sulu_node *root = alloc_node(NODE_PROGRAM);
    
    while (p.curr.type != TOKEN_EOF) {
        struct sulu_node *node = 0;
        if (p.curr.type == TOKEN_WINDOW) node = parse_window(&p);
        else if (p.curr.type == TOKEN_VAR) node = parse_var(&p);
        else if (p.curr.type == TOKEN_FN) node = parse_fn(&p);
        else if (p.curr.type == TOKEN_LAYOUT) node = parse_layout(&p);
        else advance(&p); // Error or unknown top-level

        if (node) add_child(root, node);
    }
    
    return root;
}

void suluscript_free_node(struct sulu_node *node) {
    if (!node) return;
    
    struct sulu_node *child = node->children;
    while (child) {
        struct sulu_node *next = child->next;
        suluscript_free_node(child);
        child = next;
    }
    
    struct sulu_prop *prop = node->props;
    while (prop) {
        struct sulu_prop *next = prop->next;
        if (prop->val_str) free(prop->val_str);
        free(prop);
        prop = next;
    }
    
    if (node->ident) free(node->ident);
    if (node->val_str) free(node->val_str);
    if (node->expr_left) suluscript_free_node(node->expr_left);
    if (node->expr_right) suluscript_free_node(node->expr_right);
    
    free(node);
}
