#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/suluscript.h"

char *token_names[] = {
    "EOF", "IDENT", "INT", "HEX", "STRING",
    "WINDOW", "LAYOUT", "FN", "VAR", "IF", "ELSE", "RETURN",
    "VBOX", "HBOX", "TEXT", "BUTTON", "PROGRESS", "RECT",
    "LBRACE", "RBRACE", "LPAREN", "RPAREN", "COLON", "COMMA", "EQUAL", "PLUS", "MINUS", "SEMICOLON",
    "ERROR"
};

char *node_names[] = {
    "PROGRAM", "WINDOW_DEF", "LAYOUT_DEF", "VAR_DEF", "FN_DEF",
    "VBOX", "HBOX", "TEXT", "BUTTON", "PROGRESS", "RECT",
    "BLOCK", "CALL", "BINOP", "IDENT", "LIT_INT", "LIT_STR", "RETURN"
};

void print_ast(struct sulu_node *n, int indent) {
    if (!n) return;
    for (int i = 0; i < indent; i++) printf("  ");
    printf("[%s]", node_names[n->type]);
    if (n->ident) printf(" ident='%s'", n->ident);
    if (n->val_int) printf(" val=%d", n->val_int);
    if (n->val_str) printf(" str='%s'", n->val_str);
    if (n->op) printf(" op='%c'", n->op);
    
    struct sulu_prop *p = n->props;
    while (p) {
        printf(" {%s=", p->name);
        if (p->val_str) printf("'%s'}", p->val_str);
        else printf("%d}", p->val_int);
        p = p->next;
    }
    printf("\n");
    
    if (n->expr_left) {
        for (int i = 0; i <= indent; i++) printf("  ");
        printf("L: ");
        print_ast(n->expr_left, 0);
    }
    if (n->expr_right) {
        for (int i = 0; i <= indent; i++) printf("  ");
        printf("R: ");
        print_ast(n->expr_right, 0);
    }

    struct sulu_node *curr = n->children;
    while (curr) {
        print_ast(curr, indent + 1);
        curr = curr->next;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: sulutest <file>\n");
        exit(1);
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("cannot open %s\n", argv[1]);
        exit(1);
    }

    struct stat st;
    fstat(fd, &st);
    char *buf = malloc(st.size + 1);
    read(fd, buf, st.size);
    buf[st.size] = 0;
    close(fd);

    printf("--- SuluScript Parsing: %s ---\n", argv[1]);
    struct sulu_node *root = suluscript_parse(buf);
    if (root) {
        print_ast(root, 0);
        suluscript_free_node(root);
    } else {
        printf("Parsing failed.\n");
    }

    free(buf);
    exit(0);
}
