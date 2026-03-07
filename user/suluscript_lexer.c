#include "kernel/types.h"
#include "user/user.h"
#include "user/suluscript.h"

void suluscript_lexer_init(struct lexer *l, char *source) {
    l->source = source;
    l->curr = source;
    l->line = 1;
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static void skip_whitespace(struct lexer *l) {
    while (*l->curr) {
        char c = *l->curr;
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                l->curr++;
                break;
            case '\n':
                l->line++;
                l->curr++;
                break;
            case '/':
                if (l->curr[1] == '/') {
                    while (*l->curr && *l->curr != '\n') l->curr++;
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static struct token make_token(struct lexer *l, token_type_t type, int len) {
    struct token t;
    t.type = type;
    t.start = l->curr - len;
    t.len = len;
    t.val_int = 0;
    return t;
}

static token_type_t check_keyword(char *start, int len) {
    if (strncmp(start, "window", len) == 0 && len == 6) return TOKEN_WINDOW;
    if (strncmp(start, "layout", len) == 0 && len == 6) return TOKEN_LAYOUT;
    if (strncmp(start, "fn", len) == 0 && len == 2) return TOKEN_FN;
    if (strncmp(start, "var", len) == 0 && len == 3) return TOKEN_VAR;
    if (strncmp(start, "if", len) == 0 && len == 2) return TOKEN_IF;
    if (strncmp(start, "else", len) == 0 && len == 4) return TOKEN_ELSE;
    if (strncmp(start, "return", len) == 0 && len == 6) return TOKEN_RETURN;
    if (strncmp(start, "for", len) == 0 && len == 3) return TOKEN_FOR;
    
    // UI Elements
    if (strncmp(start, "vbox", len) == 0 && len == 4) return TOKEN_VBOX;
    if (strncmp(start, "hbox", len) == 0 && len == 4) return TOKEN_HBOX;
    if (strncmp(start, "text", len) == 0 && len == 4) return TOKEN_TEXT;
    if (strncmp(start, "button", len) == 0 && len == 6) return TOKEN_BUTTON;
    if (strncmp(start, "progress", len) == 0 && len == 8) return TOKEN_PROGRESS;
    if (strncmp(start, "rect", len) == 0 && len == 4) return TOKEN_RECT;
    
    return TOKEN_IDENT;
}

struct token suluscript_lexer_next(struct lexer *l) {
    skip_whitespace(l);
    
    if (!*l->curr) return make_token(l, TOKEN_EOF, 0);
    
    char c = *l->curr;
    
    if (is_alpha(c)) {
        struct token t;
        t.start = l->curr;
        while (is_alpha(*l->curr) || is_digit(*l->curr)) l->curr++;
        t.len = l->curr - t.start;
        t.type = check_keyword(t.start, t.len);
        return t;
    }
    
    if (is_digit(c)) {
        struct token t;
        t.start = l->curr;
        unsigned int val = 0;
        if (c == '0' && (l->curr[1] == 'x' || l->curr[1] == 'X')) {
            l->curr += 2;
            t.type = TOKEN_HEX;
            while (1) {
                char x = *l->curr;
                if (is_digit(x)) val = val * 16 + (x - '0');
                else if (x >= 'a' && x <= 'f') val = val * 16 + (x - 'a' + 10);
                else if (x >= 'A' && x <= 'F') val = val * 16 + (x - 'A' + 10);
                else break;
                l->curr++;
            }
        } else {
            t.type = TOKEN_INT;
            while (is_digit(*l->curr)) {
                val = val * 10 + (*l->curr - '0');
                l->curr++;
            }
        }
        t.len = l->curr - t.start;
        t.val_int = (int)val;
        return t;
    }
    
    if (c == '"') {
        struct token t;
        l->curr++; // skip "
        t.start = l->curr;
        while (*l->curr && *l->curr != '"') {
            if (*l->curr == '\n') l->line++;
            l->curr++;
        }
        t.len = l->curr - t.start;
        t.type = TOKEN_STRING;
        if (*l->curr == '"') l->curr++;
        return t;
    }
    
    l->curr++;
    switch (c) {
        case '{': return make_token(l, TOKEN_LBRACE, 1);
        case '}': return make_token(l, TOKEN_RBRACE, 1);
        case '(': return make_token(l, TOKEN_LPAREN, 1);
        case ')': return make_token(l, TOKEN_RPAREN, 1);
        case '[': return make_token(l, TOKEN_LBRACKET, 1);
        case ']': return make_token(l, TOKEN_RBRACKET, 1);
        case ':': return make_token(l, TOKEN_COLON, 1);
        case ',': return make_token(l, TOKEN_COMMA, 1);
        case ';': return make_token(l, TOKEN_SEMICOLON, 1);
        case '=': {
            if (*l->curr == '=') {
                l->curr++;
                return make_token(l, TOKEN_DBL_EQUAL, 2);
            }
            return make_token(l, TOKEN_EQUAL, 1);
        }
        case '+': return make_token(l, TOKEN_PLUS, 1);
        case '-': return make_token(l, TOKEN_MINUS, 1);
        case '*': return make_token(l, TOKEN_STAR, 1);
        case '/': return make_token(l, TOKEN_SLASH, 1);
        case '%': return make_token(l, TOKEN_MOD, 1);
        case '<': return make_token(l, TOKEN_LESS, 1);
        case '>': return make_token(l, TOKEN_GREATER, 1);
        case '!': {
            if (*l->curr == '=') {
                l->curr++;
                return make_token(l, TOKEN_BANG_EQUAL, 2);
            }
            return make_token(l, TOKEN_BANG, 1);
        }
    }
    
    return make_token(l, TOKEN_ERROR, 1);
}
