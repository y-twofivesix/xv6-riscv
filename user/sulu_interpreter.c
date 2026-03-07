#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/sulu_client.h"
#include "user/suluscript.h"
#include "user/font.h"

// --- Global State ---
struct sulu_window win;
struct sulu_node *root_ast = 0;
int did_return = 0;
char *arg1_val = "";
char *arg2_val = "";

// Simple variable scope
struct sulu_var {
    char name[32];
    int val_int;
    char *val_str;
    int *array;
    int array_len;
    struct sulu_var *next;
};
struct sulu_var *variables = 0;

// Forward declarations
void exec_node(struct sulu_node *n);
int eval_expr(struct sulu_node *n);
void clear_arr_internal(struct sulu_var *v);
int scancode_to_ascii(int scancode, int shift);
void pop_internal(struct sulu_var *v);
void push_internal(struct sulu_var *v, int val);
void insert_at_internal(struct sulu_var *v, int idx, int val);
void delete_at_internal(struct sulu_var *v, int idx);
struct sulu_var* get_var_ptr(char *name);

// Helper for strings
char* eval_expr_str(struct sulu_node *n) {
    if(!n) return 0;
    if(n->type == NODE_LIT_STR) return n->val_str;
    if(n->type == NODE_IDENT) {
        struct sulu_var *v = get_var_ptr(n->ident);
        if(v) return v->val_str;
    }
    return 0;
}

// Symbol Table
int get_var(char *name) {
    struct sulu_var *v = variables;
    while(v) {
        if(strcmp(v->name, name) == 0) {
            // printf("sulu: get_var %s = %d\n", name, v->val_int);
            return v->val_int;
        }
        v = v->next;
    }
    return 0;
}

void set_var(char *name, int val) {
    struct sulu_var *v = variables;
    while(v) {
        if(strcmp(v->name, name) == 0) {
            v->val_int = val;
            return;
        }
        v = v->next;
    }
    struct sulu_var *new_v = malloc(sizeof(struct sulu_var));
    strncpy(new_v->name, name, 31);
    new_v->val_int = val;
    new_v->val_str = 0;
    new_v->array = 0;
    new_v->array_len = 0;
    new_v->next = variables;
    variables = new_v;
}

void set_var_str(char *name, char *val) {
    struct sulu_var *v = variables;
    while(v) {
        if(strcmp(v->name, name) == 0) {
            v->val_str = val;
            return;
        }
        v = v->next;
    }
    struct sulu_var *new_v = malloc(sizeof(struct sulu_var));
    strncpy(new_v->name, name, 31);
    new_v->val_int = 0;
    new_v->val_str = val;
    new_v->array = 0;
    new_v->array_len = 0;
    new_v->next = variables;
    variables = new_v;
}
struct sulu_var* get_var_ptr(char *name) {
    struct sulu_var *v = variables;
    while(v) {
        if(strcmp(v->name, name) == 0) return v;
        v = v->next;
    }
    return 0;
}

// --- Native Functions ---

static unsigned int rand_seed = 123;
void native_srand(unsigned int s) {
    rand_seed = s;
}

int native_rand() {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (unsigned int)(rand_seed / 65536) % 32768;
}

int exec_native(char *name, struct sulu_node *args) {
    if(strcmp(name, "rand") == 0) return native_rand();
    if(strcmp(name, "srand") == 0) {
        if(args) native_srand(eval_expr(args));
        return 0;
    }
    if(strcmp(name, "at") == 0) {
        // at(arr, idx)
        if(!args || !args->next) return 0;
        struct sulu_var *v = get_var_ptr(args->ident);
        int idx = eval_expr(args->next);
        if(v && v->array && idx >= 0 && idx < v->array_len) return v->array[idx];
    }
    if(strcmp(name, "push") == 0) {
        // push(arr, val)
        if(!args || !args->next) return 0;
        struct sulu_var *v = get_var_ptr(args->ident);
        int val = eval_expr(args->next);
        if(v) {
            if(!v->array) {
                v->array = malloc(sizeof(int) * 100);
                v->array_len = 0;
            }
            if(v->array_len < 100) {
                v->array[v->array_len++] = val;
                // printf("sulu: push %s val=%d len=%d\n", v->name, val, v->array_len);
            }
        }
    }
    if(strcmp(name, "len") == 0) {
        if(!args) return 0;
        struct sulu_var *v = get_var_ptr(args->ident);
        if(v) {
            // printf("sulu: len(%s) = %d\n", args->ident, v->array_len);
            return v->array_len;
        }
        return 0;
    }
    if(strcmp(name, "clear_arr") == 0) {
        if(!args) return 0;
        struct sulu_var *v = get_var_ptr(args->ident);
        if(v) v->array_len = 0;
    }
    if(strcmp(name, "print") == 0) {
        struct sulu_node *curr = args;
        while(curr) {
            if(curr->type == NODE_LIT_INT) printf("%d ", curr->val_int);
            else if(curr->type == NODE_LIT_STR) printf("%s ", curr->val_str);
            else printf("%d ", eval_expr(curr));
            curr = curr->next;
        }
        printf("\n");
        return 0;
    }
    if(strcmp(name, "update_at") == 0) {
        // update_at(arr, idx, val)
        if(!args || !args->next || !args->next->next) return 0;
        struct sulu_var *v = get_var_ptr(args->ident);
        int idx = eval_expr(args->next);
        int val = eval_expr(args->next->next);
        if(v && v->array && idx >= 0 && idx < v->array_len) {
            v->array[idx] = val;
            // printf("sulu: update_at %s[%d]=%d\n", v->name, idx, val);
        }
    }
    if(strcmp(name, "usleep") == 0) {
        if(args) usleep(eval_expr(args));
        return 0;
    }
    if(strcmp(name, "push") == 0) {
        if(args && args->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            if(v) push_internal(v, eval_expr(args->next));
        }
        return 0;
    }
    if(strcmp(name, "pop") == 0) {
        if(args) {
            struct sulu_var *v = get_var_ptr(args->ident);
            if(v) pop_internal(v);
        }
        return 0;
    }
    if(strcmp(name, "resize") == 0) {
        if(args && args->next) {
            int nw = eval_expr(args);
            int nh = eval_expr(args->next);
            sulu_resize(&win, nw, nh);
            set_var("windowW", win.width);
            set_var("windowH", win.height);
        }
        return 0;
    }
    if(strcmp(name, "read_file") == 0) {
        if(args && args->next) {
            char *path = eval_expr_str(args);
            struct sulu_var *v = get_var_ptr(args->next->ident);
            if(path && v) {
                int fd = open(path, O_RDONLY);
                if(fd >= 0) {
                    char c;
                    clear_arr_internal(v);
                    while(read(fd, &c, 1) > 0) {
                        push_internal(v, (int)c);
                    }
                    close(fd);
                }
            }
        }
        return 0;
    }
    if(strcmp(name, "write_file") == 0) {
        if(args && args->next) {
            char *path = eval_expr_str(args);
            struct sulu_var *v = get_var_ptr(args->next->ident);
            if(path && v) {
                int fd = open(path, O_WRONLY|O_CREATE|O_TRUNC);
                if(fd >= 0) {
                    for(int i = 0; i < v->array_len; i++) {
                        char c = (char)v->array[i];
                        write(fd, &c, 1);
                    }
                    close(fd);
                }
            }
        }
        return 0;
    }
    if(strcmp(name, "to_char") == 0) {
        if(args) {
            int code = eval_expr(args);
            int shift = (args->next) ? eval_expr(args->next) : 0;
            return scancode_to_ascii(code, shift);
        }
        return 0;
    }
    if(strcmp(name, "insert_at") == 0) {
        // insert_at(arr, idx, val)
        if(args && args->next && args->next->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            int idx = eval_expr(args->next);
            int val = eval_expr(args->next->next);
            if(v) insert_at_internal(v, idx, val);
        }
        return 0;
    }
    if(strcmp(name, "delete_at") == 0) {
        // delete_at(arr, idx)
        if(args && args->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            int idx = eval_expr(args->next);
            if(v) delete_at_internal(v, idx);
        }
        return 0;
    }
    if(strcmp(name, "cursor_line") == 0) {
        // cursor_line(arr, cursor_idx) -> line number (0-based)
        if(args && args->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            int cidx = eval_expr(args->next);
            if(v && v->array) {
                int line = 0;
                int lim = cidx < v->array_len ? cidx : v->array_len;
                for(int i = 0; i < lim; i++)
                    if(v->array[i] == '\n') line++;
                return line;
            }
        }
        return 0;
    }
    if(strcmp(name, "cursor_col") == 0) {
        // cursor_col(arr, cursor_idx) -> column (0-based)
        if(args && args->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            int cidx = eval_expr(args->next);
            if(v && v->array) {
                int col = 0;
                int lim = cidx < v->array_len ? cidx : v->array_len;
                for(int i = 0; i < lim; i++) {
                    if(v->array[i] == '\n') col = 0;
                    else col++;
                }
                return col;
            }
        }
        return 0;
    }
    if(strcmp(name, "line_start") == 0) {
        // line_start(arr, line_num) -> array index of first char on that line
        if(args && args->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            int line_num = eval_expr(args->next);
            if(v && v->array) {
                if(line_num == 0) return 0;
                int line = 0;
                for(int i = 0; i < v->array_len; i++) {
                    if(v->array[i] == '\n') {
                        line++;
                        if(line == line_num) return i + 1;
                    }
                }
                return v->array_len;
            }
        }
        return 0;
    }
    if(strcmp(name, "line_count") == 0) {
        // line_count(arr) -> total number of lines
        if(args) {
            struct sulu_var *v = get_var_ptr(args->ident);
            if(v && v->array) {
                int count = 1;
                for(int i = 0; i < v->array_len; i++)
                    if(v->array[i] == '\n') count++;
                return count;
            }
        }
        return 0;
    }
    if(strcmp(name, "xy_to_cursor") == 0) {
        // xy_to_cursor(arr, rx, ry, tb_x, tb_y, char_w, char_h, tb_w, scroll)
        // Returns cursor index corresponding to a mouse click at (rx, ry)
        if(!args) return 0;
        struct sulu_node *a = args;
        struct sulu_var *v = get_var_ptr(a->ident); a = a->next;
        if(!v || !v->array || !a) return 0;
        int rx = eval_expr(a); a = a->next;
        if(!a) return 0;
        int ry = eval_expr(a); a = a->next;
        if(!a) return 0;
        int tb_x = eval_expr(a); a = a->next;
        if(!a) return 0;
        int tb_y = eval_expr(a); a = a->next;
        if(!a) return 0;
        int cw = eval_expr(a); a = a->next;
        if(!a) return 0;
        int ch = eval_expr(a); a = a->next;
        if(!a) return 0;
        int tb_w = eval_expr(a); a = a->next; (void)tb_w;
        int scroll = a ? eval_expr(a) : 0;
        int pad = 4;
        int start_x = tb_x + pad;
        int start_y = tb_y + pad;
        int target_line = (ry - start_y) / ch + scroll;
        int target_col  = (rx - start_x) / cw;
        if(target_col < 0) target_col = 0;
        if(target_line < 0) target_line = 0;
        int line = 0, col = 0;
        for(int i = 0; i < v->array_len; i++) {
            if(line == target_line) {
                if(col >= target_col || v->array[i] == '\n') return i;
                col++;
            } else if(line > target_line) {
                return i;
            } else {
                if(v->array[i] == '\n') { line++; col = 0; }
            }
        }
        return v->array_len;
    }
    return 0;
}

// Internal help for reuse
void clear_arr_internal(struct sulu_var *v) {
    if(v->array) {
        free(v->array);
        v->array = 0;
        v->array_len = 0;
    }
}

void push_internal(struct sulu_var *v, int val) {
    if(!v->array) {
        v->array = malloc(sizeof(int) * 4096);
        v->array_len = 0;
    }
    if(v->array_len < 4096) {
        v->array[v->array_len++] = val;
    }
}

void pop_internal(struct sulu_var *v) {
    if(v->array && v->array_len > 0) {
        v->array_len--;
        if(v->array_len == 0) {
            free(v->array);
            v->array = 0;
        }
    }
}

void insert_at_internal(struct sulu_var *v, int idx, int val) {
    if(!v->array) {
        v->array = malloc(sizeof(int) * 4096);
        v->array_len = 0;
    }
    if(v->array_len >= 4096) return;
    if(idx < 0) idx = 0;
    if(idx > v->array_len) idx = v->array_len;
    for(int i = v->array_len; i > idx; i--)
        v->array[i] = v->array[i-1];
    v->array[idx] = val;
    v->array_len++;
}

void delete_at_internal(struct sulu_var *v, int idx) {
    if(!v->array || idx < 0 || idx >= v->array_len) return;
    for(int i = idx; i < v->array_len - 1; i++)
        v->array[i] = v->array[i+1];
    v->array_len--;
}

int scancode_to_ascii(int scancode, int shift) {
    static char map[] = {
        0,  0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
    };
    static char shift_map[] = {
        0,  0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~', 0, '|',
        'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
    };
    if(scancode < 0 || scancode >= sizeof(map)) return 0;
    return shift ? shift_map[scancode] : map[scancode];
}

// --- Expression Evaluator ---

int eval_expr(struct sulu_node *n) {
    if(!n) return 0;
    // printf("sulu: eval_expr type=%d\n", n->type);
    if(n->type == NODE_LIT_INT) return n->val_int;
    if(n->type == NODE_IDENT) {
        int v = get_var(n->ident);
        // printf("sulu: eval_ident %s = %d\n", n->ident, v);
        return v;
    }
    if(n->type == NODE_ARRAY_REF) {
        struct sulu_var *v = get_var_ptr(n->ident);
        int idx = eval_expr(n->expr_left);
        // printf("sulu: array_ref %s[%d]\n", n->ident, idx);
        if(v && v->array && idx >= 0 && idx < v->array_len) return v->array[idx];
        return 0;
    }
    if(n->type == NODE_CALL) {
        return exec_native(n->ident, n->children);
    }
    if(n->type == NODE_ASSIGN) {
        int val = eval_expr(n->expr_left);
        set_var(n->ident, val);
        return val;
    }
    if(n->type == NODE_BINOP) {
        int l = eval_expr(n->expr_left);
        int r = eval_expr(n->expr_right);
        int res = 0;
        switch(n->op) {
            case '+': res = l + r; break;
            case '-': res = l - r; break;
            case '*': res = l * r; break;
            case '/': res = r != 0 ? l / r : 0; break;
            case '%': res = r != 0 ? l % r : 0; break;
            case '=': res = (l == r); break;
            case '!': res = (l != r); break;
            case '<': res = l < r; break;
            case '>': res = l > r; break;
            case 'L': res = (l <= r); break;
            case 'G': res = (l >= r); break;
            case '&': res = (l && r) ? 1 : 0; break;
            case '|': res = (l || r) ? 1 : 0; break;
        }
        return res;
    }
    return 0;
}

// --- Statement Executor ---

void exec_node(struct sulu_node *n) {
    if(!n || did_return) return;
    // printf("sulu: exec_node type=%d ident=%s\n", n->type, n->ident ? n->ident : "NULL");
    
    switch(n->type) {
        case NODE_VAR_DEF:
        case NODE_ASSIGN: {
            if(n->type == NODE_ASSIGN && (strstr(n->ident, "[") || strchr(n->ident, '['))) {
                // Future: handle arr[idx] = val here if we want real property assignment
                // For now use native functions for better control
            }
            int val = eval_expr(n->expr_left);
            set_var(n->ident, val);
            break;
        }
        case NODE_FOR: {
            // for(init; cond; post) { body }
            exec_node(n->children); // init
            while(!did_return && eval_expr(n->expr_left)) { // cond
                exec_node(n->children->next); // body
                if(!did_return) eval_expr(n->expr_right); // post
            }
            break;
        }
        case NODE_IF: {
            int cond = eval_expr(n->expr_left);
            if(cond) {
                exec_node(n->children);
            }
            break;
        }
        case NODE_BLOCK: {
            struct sulu_node *curr = n->children;
            while(curr && !did_return) {
                exec_node(curr);
                curr = curr->next;
            }
            break;
        }
        case NODE_RETURN: {
            eval_expr(n->expr_left);
            did_return = 1;
            break;
        }
        case NODE_CALL:
            eval_expr(n);
            break;
        default:
            break;
    }
}

void exec_func(char *name) {
    if(!name || !name[0]) return;
    struct sulu_node *curr = root_ast->children;
    while(curr) {
        if(curr->type == NODE_FN_DEF && strcmp(curr->ident, name) == 0) {
            // printf("sulu: exec_func %s\n", name);
            did_return = 0; // Reset for function call
            exec_node(curr->children); // Executes the block
            did_return = 0; // Reset after finish
            return;
        }
        curr = curr->next;
    }
}

// --- Helper Functions (Updated for Dynamic Lookups) ---

static int get_prop_int(struct sulu_node *n, char *name, int def) {
    struct sulu_prop *p = n->props;
    while(p) {
        if(strcmp(p->name, name) == 0) {
            if(p->val_expr) return eval_expr(p->val_expr);
            if(p->val_str) return get_var(p->val_str);
            return p->val_int;
        }
        p = p->next;
    }
    return def;
}

static char* get_prop_str(struct sulu_node *n, char *name, char *def) {
    struct sulu_prop *p = n->props;
    while(p) {
        if(strcmp(p->name, name) == 0) {
            if(p->val_str) return p->val_str;
            if(p->val_expr && p->val_expr->type == NODE_IDENT) return p->val_expr->ident;
        }
        p = p->next;
    }
    return def;
}

// --- Layout Engine ---

void layout_node(struct sulu_node *n, int x, int y, int max_w) {
    if(!n) return;
    
    // Some nodes have explicit x/y overrides
    int rx = get_prop_int(n, "x", -1);
    int ry = get_prop_int(n, "y", -1);
    if(rx == -1) n->x = x; else n->x = rx;
    if(ry == -1) n->y = y; else n->y = ry;
    
    switch(n->type) {
        case NODE_LAYOUT_DEF: {
            struct sulu_node *child = n->children;
            int h = 0;
            while(child) {
                layout_node(child, n->x, n->y, max_w);
                if(child->y + child->h > h) h = child->y + child->h;
                child = child->next;
            }
            n->h = h - n->y;
            break;
        }
        case NODE_VBOX: {
            int padding = get_prop_int(n, "padding", 0);
            int gap = get_prop_int(n, "gap", 0);
            int curr_y = n->y + padding;
            int width = 0;
            struct sulu_node *child = n->children;
            while(child) {
                layout_node(child, n->x + padding, curr_y, max_w - (padding * 2));
                curr_y += child->h + gap;
                if(child->w > width) width = child->w;
                child = child->next;
            }
            n->w = width + (padding * 2);
            n->h = (curr_y - n->y) + padding - (n->children ? gap : 0);
            break;
        }
        case NODE_HBOX: {
            int padding = get_prop_int(n, "padding", 0);
            int gap = get_prop_int(n, "gap", 0);
            int curr_x = n->x + padding;
            int height = 0;
            struct sulu_node *child = n->children;
            while(child) {
                layout_node(child, curr_x, n->y + padding, max_w);
                curr_x += child->w + gap;
                if(child->h > height) height = child->h;
                child = child->next;
            }
            n->w = (curr_x - n->x) + padding - (n->children ? gap : 0);
            n->h = height + (padding * 2);
            break;
        }
        case NODE_TEXT: {
            char *txt = get_prop_str(n, "content", "");
            n->w = strlen(txt) * 8;
            n->h = 10;
            break;
        }
        case NODE_BUTTON: {
            char *label = get_prop_str(n, "label", "Button");
            n->w = (strlen(label) * 8) + 20;
            n->h = 24;
            break;
        }
        case NODE_RECT: {
            n->w = get_prop_int(n, "width", 100);
            n->h = get_prop_int(n, "height", 20);
            break;
        }
        case NODE_TEXTBOX: {
            n->w = get_prop_int(n, "width", 300);
            n->h = get_prop_int(n, "height", 200);
            break;
        }
        case NODE_IF: {
            if(eval_expr(n->expr_left)) {
                struct sulu_node *child = n->children;
                while(child) {
                    layout_node(child, n->x, n->y, max_w);
                    child = child->next;
                }
            }
            break;
        }
        case NODE_BLOCK: {
            struct sulu_node *child = n->children;
            while(child) {
                layout_node(child, n->x, n->y, max_w);
                child = child->next;
            }
            break;
        }
        case NODE_FOR: {
            exec_node(n->children); // init
            while(eval_expr(n->expr_left)) { // cond
                struct sulu_node *body_node = n->children->next;
                while(body_node) {
                    layout_node(body_node, n->x, n->y, max_w);
                    body_node = body_node->next;
                }
                eval_expr(n->expr_right); // post
            }
            break;
        }
        default:
            n->w = 0; n->h = 0;
            break;
    }
}

// --- Drawing Engine ---

static inline void sulu_draw_pixel_wrap(struct sulu_window_shm *shm, int x, int y, uint32 color) {
    if (x < 0 || y < 0 || x >= shm->width || y >= shm->height) return;
    uint *pixels = sulu_pixels(shm);
    pixels[y * shm->width + x] = color;
}

void draw_local_char(int x, int y, char c, uint32 color) {
    if(c < 32 || c > 127) return;
    sulu_draw_char(win.shm, x, y, c, color);
}

void draw_local_text(int x, int y, char *s, uint32 color) {
    sulu_draw_text(win.shm, x, y, s, color);
}

void draw_local_rect(int x, int y, int w, int h, uint32 color) {
    sulu_fill_rect(win.shm, x, y, w, h, color);
}

// --- Textbox Renderer ---
// Renders a char array with a visible cursor.
// Props: content (arr), cursor (int idx), x, y, width, height, color, bg, scroll (line offset)
void draw_textbox(struct sulu_node *n) {
    int tb_x  = get_prop_int(n, "x", n->x);
    int tb_y  = get_prop_int(n, "y", n->y);
    int tb_w  = get_prop_int(n, "width", n->w);
    int tb_h  = get_prop_int(n, "height", n->h);
    uint32 text_color = (uint32)get_prop_int(n, "color", 0xFFCCCCCC);
    uint32 bg_color   = (uint32)get_prop_int(n, "bg",    0xFF1E1E1E);
    int scroll     = get_prop_int(n, "scroll", 0);
    int cursor_idx = get_prop_int(n, "cursor", -1);

    // Resolve content array variable
    struct sulu_var *v = 0;
    struct sulu_prop *prop = n->props;
    while(prop) {
        if(strcmp(prop->name, "content") == 0) {
            if(prop->val_expr && prop->val_expr->type == NODE_IDENT)
                v = get_var_ptr(prop->val_expr->ident);
            break;
        }
        prop = prop->next;
    }

    draw_local_rect(tb_x, tb_y, tb_w, tb_h, bg_color);
    sulu_draw_rect(win.shm, tb_x, tb_y, tb_w, tb_h, 0xFF444444);

    const int CHAR_W = 8;
    const int CHAR_H = 10;
    const int PAD    = 4;
    int start_x = tb_x + PAD;
    int max_x   = tb_x + tb_w - PAD;
    int max_y   = tb_y + tb_h - PAD;

    int logical_x    = start_x;
    int logical_line = 0;
    int total = v ? v->array_len : 0;

    for(int i = 0; i <= total; i++) {
        int screen_y = tb_y + PAD + (logical_line - scroll) * CHAR_H;

        // Draw cursor before char at position i
        if(i == cursor_idx && screen_y >= tb_y + PAD && screen_y + CHAR_H <= max_y) {
            int cx = logical_x < max_x ? logical_x : max_x;
            draw_local_rect(cx, screen_y, 2, CHAR_H, 0xFFFFFFFF);
        }

        if(i == total) break;

        char c = (char)v->array[i];
        if(c == '\n') {
            logical_line++;
            logical_x = start_x;
        } else {
            if(screen_y >= tb_y + PAD && screen_y + CHAR_H <= max_y && logical_x + CHAR_W <= max_x) {
                sulu_draw_char(win.shm, logical_x, screen_y, c, text_color);
            }
            logical_x += CHAR_W;
        }
    }
}

void draw_node(struct sulu_node *n) {
    if(!n) return;
    
    switch(n->type) {
        case NODE_TEXT: {
            char *msg = 0;
            struct sulu_prop *p = n->props;
            int is_array = 0;
            struct sulu_var *v = 0;
            while(p) {
                if(strcmp(p->name, "content") == 0) {
                    if(p->val_str) msg = p->val_str;
                    else if(p->val_expr && p->val_expr->type == NODE_IDENT) {
                        v = get_var_ptr(p->val_expr->ident);
                        if(v && v->array) is_array = 1;
                        else if(v) msg = v->val_str;
                    }
                    break;
                }
                p = p->next;
            }
            int tx = get_prop_int(n, "x", n->x);
            int ty = get_prop_int(n, "y", n->y);
            uint32 tc = (uint32)get_prop_int(n, "color", 0xFF000000);
            if(is_array && v) {
                int cx = tx;
                int cy = ty;
                for(int i=0; i < v->array_len; i++) {
                    char c = (char)v->array[i];
                    if(c == '\n') {
                        cx = tx;
                        cy += 10;
                    } else {
                        sulu_draw_char(win.shm, cx, cy, c, tc);
                        cx += 8;
                        if(cx > win.width - 10) { cx = tx; cy += 10; }
                    }
                }
            } else {
                sulu_draw_text(win.shm, tx, ty, msg ? msg : "", tc);
            }
            break;
        }
        case NODE_BUTTON: {
            int bx = get_prop_int(n, "x", n->x);
            int by = get_prop_int(n, "y", n->y);
            draw_local_rect(bx, by, n->w, n->h, 0xFFCCCCCC);
            sulu_draw_rect(win.shm, bx, by, n->w, n->h, 0xFFFFFFFF); // White border
            draw_local_text(bx + 10, by + 8, get_prop_str(n, "label", "Button"), 0xFF000000);
            break;
        }
        case NODE_RECT: {
            int rx = get_prop_int(n, "x", n->x);
            int ry = get_prop_int(n, "y", n->y);
            int rw = get_prop_int(n, "width", n->w);
            int rh = get_prop_int(n, "height", n->h);
            uint32 rc = (uint32)get_prop_int(n, "color", 0xFF888888);
            // printf("sulu: draw_rect x=%d y=%d w=%d h=%d color=0x%x\n", rx, ry, rw, rh, rc);
            draw_local_rect(rx, ry, rw, rh, rc);
            break;
        }
        case NODE_TEXTBOX:
            draw_textbox(n);
            break;
        case NODE_BLOCK: {
            struct sulu_node *child = n->children;
            while(child) {
                draw_node(child);
                child = child->next;
            }
            break;
        }
        case NODE_IF: {
            if(eval_expr(n->expr_left)) {
                struct sulu_node *child = n->children;
                while(child) {
                    draw_node(child);
                    child = child->next;
                }
            }
            break;
        }
        case NODE_FOR: {
            // printf("sulu: draw FOR enter\n");
            did_return = 0;
            exec_node(n->children); // init
            int iter = 0;
            while(eval_expr(n->expr_left)) { // cond
                // printf("sulu: draw FOR iter=%d\n", iter);
                struct sulu_node *body_node = n->children->next;
                while(body_node) {
                    draw_node(body_node);
                    body_node = body_node->next;
                }
                eval_expr(n->expr_right); // post
                iter++;
                if(iter > 200) break; // safety
            }
            // printf("sulu: draw FOR done iters=%d\n", iter);
            break;
        }
        case NODE_VBOX:
        case NODE_HBOX:
        case NODE_LAYOUT_DEF:
        case NODE_PROGRAM: {
            struct sulu_node *child = n->children;
            while(child) {
                draw_node(child);
                child = child->next;
            }
            break;
        }
        default:
            break;
    }
}

// --- Hit Detection ---

struct sulu_node* find_clicked_node(struct sulu_node *n, int x, int y) {
    if(!n) return 0;
    
    struct sulu_node *child = n->children;
    while(child) {
        struct sulu_node *found = find_clicked_node(child, x, y);
        if(found) return found;
        child = child->next;
    }
    
    if(x >= n->x && x <= n->x + n->w && y >= n->y && y <= n->y + n->h) {
        if(n->type == NODE_BUTTON) return n;
    }
    
    return 0;
}

// --- Main Loop ---

void render_app() {
    did_return = 0; // CRITICAL: Reset return flag before rendering
    static int initialized_buffers = 0;
    struct sulu_node *win_def = 0;
    struct sulu_node *layout_def = 0;
    
    struct sulu_node *curr = root_ast->children;
    while(curr) {
        if(curr->type == NODE_WINDOW_DEF) win_def = curr;
        if(curr->type == NODE_LAYOUT_DEF) layout_def = curr;
        curr = curr->next;
    }
    
    uint32 bg = 0xFFFFFFFF;
    if(win_def) {
        bg = (uint32)get_prop_int(win_def, "bg", 0xFFFFFFFF);
    }
    
    // Clear both buffers on first render to avoid white frames
    if(!initialized_buffers && (win.shm->flags & SULU_FLAG_DOUBLE_BUFFER)) {
        draw_local_rect(0, 0, win.width, win.height, bg);
        sulu_swap(&win);
        draw_local_rect(0, 0, win.width, win.height, bg);
        sulu_swap(&win);
        initialized_buffers = 1;
    }

    draw_local_rect(0, 0, win.width, win.height, bg);
    
    if(layout_def) {
        layout_node(layout_def, 0, 0, win.width);
        draw_node(layout_def);
    }
    
    sulu_swap(&win);
}

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: sulu_run <file.sul>\n");
        exit(1);
    }
    
    int fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        printf("cannot open %s\n", argv[1]);
        exit(1);
    }
    
    struct stat st;
    fstat(fd, &st);
    char *buf = malloc(st.size + 1);
    read(fd, buf, st.size);
    buf[st.size] = 0;
    close(fd);
    
    root_ast = suluscript_parse(buf);
    if(!root_ast) {
        printf("Parsing failed\n");
        exit(1);
    }
    
    // Seed the RNG
    native_srand(uptime());

    // Initialize Global Variables and Functions
    struct sulu_node *init_node = root_ast->children;
    while(init_node) {
        if(init_node->type == NODE_VAR_DEF) {
            exec_node(init_node);
        }
        init_node = init_node->next;
    }
    
    // Window params
    int w = 800, h = 600;
    char *title = "SuluScript App";
    struct sulu_node *node = root_ast->children;
    struct sulu_node *layout_node_ref = 0;
    while(node) {
        if(node->type == NODE_WINDOW_DEF) {
            w = get_prop_int(node, "width", 800);
            h = get_prop_int(node, "height", 600);
            title = get_prop_str(node, "title", title);
        }
        if(node->type == NODE_LAYOUT_DEF) layout_node_ref = node;
        node = node->next;
    }
    
    if(sulu_init(&win, w, h, 0xFFFFFFFF, SULU_FLAG_DOUBLE_BUFFER) < 0) {
        printf("sulu_init failed\n");
        exit(1);
    }
    sulu_set_title(win.shm, title);

    if(argc > 2) set_var_str("arg1", argv[2]);
    else set_var_str("arg1", "");
    if(argc > 3) set_var_str("arg2", argv[3]);
    else set_var_str("arg2", "");
    
    struct sulu_event ev;
    char *frame_func = 0;
    char *keydown_func = 0;
    char *keyup_func = 0;
    char *resize_func = 0;
    char *mousedown_func = 0;
    if(layout_node_ref) {
        frame_func    = get_prop_str(layout_node_ref, "onFrame", 0);
        keydown_func  = get_prop_str(layout_node_ref, "onKeyDown", 0);
        keyup_func    = get_prop_str(layout_node_ref, "onKeyUp", 0);
        resize_func   = get_prop_str(layout_node_ref, "onResize", 0);
        mousedown_func = get_prop_str(layout_node_ref, "onMouseDown", 0);
    }
    
    set_var("windowW", win.width);
    set_var("windowH", win.height);

    while(1) {
        // Animation/Frame Logic
        if(frame_func) {
            exec_func(frame_func);
        }
        
        // Full frame render
        render_app();

        // Process Events
        while (sulu_event_pop(win.shm, &ev) >= 0) {
            if(ev.type == SULU_EV_CLOSE) goto cleanup;
            
            if(ev.type == SULU_EV_KEY) {
                set_var("key", ev.code);
                set_var("key_val", ev.value);
                if(ev.value == 1 && keydown_func) exec_func(keydown_func);
                if(ev.value == 0 && keyup_func) exec_func(keyup_func);
            }

            if(ev.type == SULU_EV_RESIZE) {
                // Sulu WM resized us
                win.width = ev.x;
                win.height = ev.y;
                set_var("windowW", win.width);
                set_var("windowH", win.height);
                if(resize_func) exec_func(resize_func);
            }

            if(ev.type == SULU_EV_MOUSE_BTN && ev.value == 1) { // L-Down
                set_var("mouse_rx", ev.rx);
                set_var("mouse_ry", ev.ry);
                if(mousedown_func) exec_func(mousedown_func);
                if(layout_node_ref) {
                    struct sulu_node *hit = find_clicked_node(layout_node_ref, ev.rx, ev.ry);
                    if(hit) {
                        char *callback = get_prop_str(hit, "onClick", 0);
                        if(callback) exec_func(callback);
                    }
                }
            }
        }
        
        usleep(16000); // ~60 FPS
    }
    
cleanup:
    suluscript_free_node(root_ast);
    exit(0);
}
