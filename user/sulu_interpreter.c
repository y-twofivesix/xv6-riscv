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

// Call frame for local variable scope
struct sulu_frame {
    struct sulu_var *locals;
    int return_val;
    struct sulu_frame *prev;
};
struct sulu_frame *current_frame = 0;

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
struct sulu_node* find_fn(char *name);
int call_fn(struct sulu_node *fn);

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
    if(current_frame) {
        struct sulu_var *v = current_frame->locals;
        while(v) {
            if(strcmp(v->name, name) == 0) return v->val_int;
            v = v->next;
        }
    }
    struct sulu_var *v = variables;
    while(v) {
        if(strcmp(v->name, name) == 0) return v->val_int;
        v = v->next;
    }
    return 0;
}

void set_var(char *name, int val) {
    if(current_frame) {
        struct sulu_var *v = current_frame->locals;
        while(v) {
            if(strcmp(v->name, name) == 0) { v->val_int = val; return; }
            v = v->next;
        }
    }
    struct sulu_var *v = variables;
    while(v) {
        if(strcmp(v->name, name) == 0) { v->val_int = val; return; }
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

// Create or update a variable in the current local frame (or globals if not in a frame)
void set_local(char *name, int val) {
    if(!current_frame) { set_var(name, val); return; }
    struct sulu_var *v = current_frame->locals;
    while(v) {
        if(strcmp(v->name, name) == 0) { v->val_int = val; return; }
        v = v->next;
    }
    struct sulu_var *new_v = malloc(sizeof(struct sulu_var));
    strncpy(new_v->name, name, 31);
    new_v->val_int = val;
    new_v->val_str = 0;
    new_v->array = 0;
    new_v->array_len = 0;
    new_v->next = current_frame->locals;
    current_frame->locals = new_v;
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
    if(current_frame) {
        struct sulu_var *v = current_frame->locals;
        while(v) {
            if(strcmp(v->name, name) == 0) return v;
            v = v->next;
        }
    }
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
        if(!args || !args->next) return 0;
        struct sulu_var *v = get_var_ptr(args->ident);
        int idx = eval_expr(args->next);
        if(v && v->array && idx >= 0 && idx < v->array_len) return v->array[idx];
        return 0;
    }
    if(strcmp(name, "push") == 0) {
        if(args && args->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            if(v) push_internal(v, eval_expr(args->next));
        }
        return 0;
    }
    if(strcmp(name, "len") == 0) {
        if(!args) return 0;
        struct sulu_var *v = get_var_ptr(args->ident);
        if(v) return v->array_len;
        return 0;
    }
    if(strcmp(name, "clear_arr") == 0) {
        if(!args) return 0;
        struct sulu_var *v = get_var_ptr(args->ident);
        if(v) v->array_len = 0;
        return 0;
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
        if(!args || !args->next || !args->next->next) return 0;
        struct sulu_var *v = get_var_ptr(args->ident);
        int idx = eval_expr(args->next);
        int val = eval_expr(args->next->next);
        if(v && v->array && idx >= 0 && idx < v->array_len) {
            v->array[idx] = val;
        }
        return 0;
    }
    if(strcmp(name, "usleep") == 0) {
        if(args) usleep(eval_expr(args));
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
                    v->array_len = 0; // Don't free, just reset for speed
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
            if(path && v && v->array) {
                int fd = open(path, O_WRONLY|O_CREATE|O_TRUNC);
                if(fd >= 0) {
                    // Use a buffer to avoid thousands of 1-byte syscalls
                    char *tmp = malloc(v->array_len);
                    if(tmp) {
                        for(int i = 0; i < v->array_len; i++) tmp[i] = (char)v->array[i];
                        write(fd, tmp, v->array_len);
                        free(tmp);
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
        if(args && args->next && args->next->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            int idx = eval_expr(args->next);
            int val = eval_expr(args->next->next);
            if(v) insert_at_internal(v, idx, val);
        }
        return 0;
    }
    if(strcmp(name, "delete_at") == 0) {
        if(args && args->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            int idx = eval_expr(args->next);
            if(v) delete_at_internal(v, idx);
        }
        return 0;
    }
    if(strcmp(name, "cursor_line") == 0) {
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
    if(strcmp(name, "clip_set") == 0) {
        if(args) {
            char *s = eval_expr_str(args);
            if(s) {
                strncpy(win.shm->clipboard, s, 2047);
                win.shm->clipboard[2047] = 0;
                win.shm->clipboard_len = strlen(s);
                struct sulu_cmd cmd = { .type = SULU_CMD_CLIP_SET };
                sulu_cmd_push(win.shm, &cmd);
            }
        }
        return 0;
    }
    if(strcmp(name, "clip_copy_range") == 0) {
        if(args && args->next && args->next->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            int start = eval_expr(args->next);
            int end = eval_expr(args->next->next);
            if(v && v->array) {
                if(start > end) { int t = start; start = end; end = t; }
                if(start < 0) start = 0;
                if(end > v->array_len) end = v->array_len;
                int len = end - start;
                if(len > 2047) len = 2047;
                if(len < 0) len = 0;
                for(int i = 0; i < len; i++) win.shm->clipboard[i] = (char)v->array[start + i];
                win.shm->clipboard[len] = 0;
                win.shm->clipboard_len = len;
                struct sulu_cmd cmd = { .type = SULU_CMD_CLIP_SET };
                sulu_cmd_push(win.shm, &cmd);
            }
        }
        return 0;
    }
    if(strcmp(name, "clip_paste_to") == 0) {
        if(args && args->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            int idx = eval_expr(args->next);
            if(v && win.shm->clipboard_len > 0) {
                // Insert clipboard chars one by one at idx
                for(int i = 0; i < win.shm->clipboard_len; i++) {
                    insert_at_internal(v, idx + i, (unsigned char)win.shm->clipboard[i]);
                }
            }
        }
        return 0;
    }
    if(strcmp(name, "clip_request") == 0) {
        struct sulu_cmd cmd = { .type = SULU_CMD_CLIP_GET };
        sulu_cmd_push(win.shm, &cmd);
        return 0;
    }
    if(strcmp(name, "clip_len") == 0) {
        return win.shm->clipboard_len;
    }
    if(strcmp(name, "delete_range") == 0) {
        if(args && args->next && args->next->next) {
            struct sulu_var *v = get_var_ptr(args->ident);
            int start = eval_expr(args->next);
            int end = eval_expr(args->next->next);
            if(v && v->array) {
                if(start > end) { int t = start; start = end; end = t; }
                if(start < 0) start = 0;
                if(end > v->array_len) end = v->array_len;
                int len = end - start;
                if(len > 0) {
                    for(int i = start; i < v->array_len - len; i++)
                        v->array[i] = v->array[i + len];
                    v->array_len -= len;
                }
            }
        }
        return 0;
    }
    // Fallback: script-defined function
    struct sulu_node *fn = find_fn(name);
    if(fn) return call_fn(fn);
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

// --- Call Frame Helpers ---

void push_frame() {
    struct sulu_frame *f = malloc(sizeof(struct sulu_frame));
    f->locals = 0;
    f->return_val = 0;
    f->prev = current_frame;
    current_frame = f;
}

int pop_frame() {
    if(!current_frame) return 0;
    struct sulu_frame *f = current_frame;
    int ret = f->return_val;
    current_frame = f->prev;
    struct sulu_var *v = f->locals;
    while(v) {
        struct sulu_var *next = v->next;
        if(v->array) free(v->array);
        free(v);
        v = next;
    }
    free(f);
    return ret;
}

struct sulu_node* find_fn(char *name) {
    struct sulu_node *n = root_ast->children;
    while(n) {
        if(n->type == NODE_FN_DEF && strcmp(n->ident, name) == 0) return n;
        n = n->next;
    }
    return 0;
}

// --- Statement Executor ---

void exec_node(struct sulu_node *n) {
    if(!n || did_return) return;
    // printf("sulu: exec_node type=%d ident=%s\n", n->type, n->ident ? n->ident : "NULL");
    
    switch(n->type) {
        case NODE_VAR_DEF: {
            int val = eval_expr(n->expr_left);
            set_local(n->ident, val);  // creates local in current frame, or global at top level
            break;
        }
        case NODE_ASSIGN: {
            int val = eval_expr(n->expr_left);
            set_var(n->ident, val);  // updates existing var in frame or globals
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
            int val = eval_expr(n->expr_left);
            if(current_frame) current_frame->return_val = val;
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

int call_fn(struct sulu_node *fn) {
    push_frame();
    int prev_did_return = did_return;
    did_return = 0;
    exec_node(fn->children);
    int ret = current_frame->return_val;
    did_return = prev_did_return;
    pop_frame();
    return ret;
}

void exec_func(char *name) {
    if(!name || !name[0]) return;
    struct sulu_node *fn = find_fn(name);
    if(fn) call_fn(fn);
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
            n->w = get_prop_int(n, "width", (strlen(label) * 8) + 20);
            n->h = get_prop_int(n, "height", 24);
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
        case NODE_PROGRAM:
        case NODE_LAYOUT_DEF:
        case NODE_BLOCK: {
            struct sulu_node *child = n->children;
            while(child) {
                layout_node(child, n->x, n->y, max_w);
                child = child->next;
            }
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
        case NODE_VAR_DEF:
        case NODE_ASSIGN:
        case NODE_CALL:
            exec_node(n);
            break;
        default:
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

// --- Textbox Renderer with Line-Level Dirty Tracking ---
//
// Tracks previous cursor, scroll, and content length to compute minimal
// dirty line range. Only clears+redraws those strips, then blit_rects.

// Per-textbox cached state (single textbox assumption for now)
static int tb_prev_cursor  = -2;  // previous cursor idx
static int tb_prev_scroll  = -1;  // previous scroll offset
static int tb_prev_content_len = -1;  // previous content array length
static int tb_first_paint = 1;
static int tb_full_redraw = 0;

// Compute which logical line a cursor index falls on
static int cursor_to_line(struct sulu_var *v, int idx) {
    int line = 0;
    int lim = idx < v->array_len ? idx : v->array_len;
    for(int i = 0; i < lim; i++)
        if(v->array[i] == '\n') line++;
    return line;
}

static int tb_prev_sel_start = -1;
static int tb_prev_sel_end   = -1;

// Render a single line strip (clear + draw chars on that line)
static void render_line_strip(struct sulu_var *v, int target_line, int scroll,
        int tb_x, int tb_y, int tb_w, int tb_h,
        int cursor_idx, int sel_start, int sel_end, uint32 text_color, uint32 bg_color) {
    const int CHAR_W = 8, CHAR_H = 10, PAD = 4;
    int start_x = tb_x + PAD;
    int max_x   = tb_x + tb_w - PAD;
    int screen_line = target_line - scroll;
    int sy = tb_y + PAD + screen_line * CHAR_H;

    // Clip — if off-screen, skip
    if(sy + CHAR_H < tb_y + PAD || sy >= tb_y + tb_h - PAD) return;

    // Clear this strip
    draw_local_rect(tb_x + 1, sy, tb_w - 2, CHAR_H, bg_color);

    // Find the start index of target_line in the array
    int idx = 0;
    int line = 0;
    int total = v ? v->array_len : 0;
    if(target_line > 0) {
        int found = 0;
        for(int i = 0; i < total; i++) {
            if(v->array[i] == '\n') {
                line++;
                if(line == target_line) { idx = i + 1; found = 1; break; }
            }
        }
        if(!found) return; // target_line doesn't exist in content — strip is already cleared
    }

    // Draw characters on this line
    int lx = start_x;
    for(int i = idx; i < total; i++) {
        // Selection highlight
        if(sel_start != sel_end) {
            int ss = sel_start < sel_end ? sel_start : sel_end;
            int se = sel_start < sel_end ? sel_end : sel_start;
            if(i >= ss && i < se) {
                draw_local_rect(lx, sy, CHAR_W, CHAR_H, 0xFF444488);
            }
        }

        // Draw cursor at this position
        if(i == cursor_idx) {
            int cx = lx < max_x ? lx : max_x;
            draw_local_rect(cx, sy, 2, CHAR_H, 0xFFFFFFFF);
        }
        char c = (char)v->array[i];
        if(c == '\n') break;  // End of this line
        if(lx + CHAR_W <= max_x) {
            sulu_draw_char(win.shm, lx, sy, c, text_color);
        }
        lx += CHAR_W;
    }
    // Cursor at end-of-file: only draw if this strip IS the cursor's line
    if(cursor_idx >= idx && cursor_idx == total) {
        int cursor_line = v ? cursor_to_line(v, total) : 0;
        if(cursor_line == target_line) {
            int cx = lx < max_x ? lx : max_x;
            draw_local_rect(cx, sy, 2, CHAR_H, 0xFFFFFFFF);
        }
    }
}

extern int tb_full_redraw; // Declared in render_app_full

void draw_textbox(struct sulu_node *n) {
    int tb_x  = get_prop_int(n, "x", n->x);
    int tb_y  = get_prop_int(n, "y", n->y);
    int tb_w  = get_prop_int(n, "width", n->w);
    int tb_h  = get_prop_int(n, "height", n->h);
    uint32 text_color = (uint32)get_prop_int(n, "color", 0xFFCCCCCC);
    uint32 bg_color   = (uint32)get_prop_int(n, "bg",    0xFF1E1E1E);
    int scroll     = get_prop_int(n, "scroll", 0);
    int cursor_idx = get_prop_int(n, "cursor", -1);
    int sel_start  = get_prop_int(n, "sel_start", -1);
    int sel_end    = get_prop_int(n, "sel_end", -1);

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

    const int CHAR_H = 10, PAD = 4;
    int vis_lines = (tb_h - PAD * 2) / CHAR_H;
    int total = v ? v->array_len : 0;

    // Determine dirty line range
    int dirty_min = 0;
    int dirty_max = scroll + vis_lines;  // redraw everything by default
    int need_border = 0;

    if(tb_first_paint || tb_full_redraw || scroll != tb_prev_scroll) {
        // Full redraw: scroll changed, first paint, or forced full redraw
        dirty_min = scroll;
        dirty_max = scroll + vis_lines;
        need_border = 1;
        tb_first_paint = 0;
    } else if(sel_start != tb_prev_sel_start || sel_end != tb_prev_sel_end) {
        // Selection changed — for now just redraw everything visible
        // (Improving this to only redraw affected lines is possible but complex)
        dirty_min = scroll;
        dirty_max = scroll + vis_lines;
    } else if(total != tb_prev_content_len) {
        // Content changed (insert/delete)
        // Cursor line and everything below it could have shifted
        int cur_line = v ? cursor_to_line(v, cursor_idx < total ? cursor_idx : total) : 0;
        dirty_min = cur_line;
        dirty_max = scroll + vis_lines;  // everything from cursor line down

        // Also redraw old cursor line if it's different
        if(tb_prev_cursor >= 0 && tb_prev_cursor != cursor_idx) {
            int old_line = v ? cursor_to_line(v, tb_prev_cursor < total ? tb_prev_cursor : total) : 0;
            if(old_line < dirty_min) dirty_min = old_line;
        }
    } else if(cursor_idx != tb_prev_cursor) {
        // Cursor moved only — just redraw the 2 affected lines
        int cur_line = v ? cursor_to_line(v, cursor_idx < total ? cursor_idx : total) : 0;
        int old_line = cur_line;
        if(tb_prev_cursor >= 0) {
            old_line = v ? cursor_to_line(v, tb_prev_cursor < total ? tb_prev_cursor : total) : 0;
        }
        dirty_min = cur_line < old_line ? cur_line : old_line;
        dirty_max = (cur_line > old_line ? cur_line : old_line) + 1;
    } else {
        // Nothing changed — skip drawing entirely but update blit trackers
        return;
    }

    // Clamp to visible range
    if(dirty_min < scroll) dirty_min = scroll;
    if(dirty_max > scroll + vis_lines) dirty_max = scroll + vis_lines;

    // On first paint or scroll change, draw border
    if(need_border) {
        draw_local_rect(tb_x, tb_y, tb_w, tb_h, bg_color);
        sulu_draw_rect(win.shm, tb_x, tb_y, tb_w, tb_h, 0xFF444444);
    }

    // Render only the dirty lines
    for(int line = dirty_min; line < dirty_max; line++) {
        render_line_strip(v, line, scroll, tb_x, tb_y, tb_w, tb_h,
                          cursor_idx, sel_start, sel_end, text_color, bg_color);
    }

    // Partial blit: only the dirty region
    int blit_y = tb_y + PAD + (dirty_min - scroll) * CHAR_H;
    int blit_h = (dirty_max - dirty_min) * CHAR_H;
    if(blit_y < tb_y) { blit_h -= (tb_y - blit_y); blit_y = tb_y; }
    if(blit_y + blit_h > tb_y + tb_h) blit_h = tb_y + tb_h - blit_y;
    if(blit_h > 0) {
        sulu_blit_rect(&win, tb_x, blit_y, tb_w, blit_h);
    }

    // Update tracked state
    tb_prev_cursor = cursor_idx;
    tb_prev_scroll = scroll;
    tb_prev_content_len = total;
    tb_prev_sel_start = sel_start;
    tb_prev_sel_end = sel_end;
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
            uint32 bc = (uint32)get_prop_int(n, "color", 0xFFCCCCCC);
            draw_local_rect(bx, by, n->w, n->h, bc);
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
        case NODE_VAR_DEF:
        case NODE_ASSIGN:
        case NODE_CALL:
            exec_node(n);
            break;
        default:
            break;
    }
}

// --- Hit Detection ---

struct sulu_node* find_clicked_node(struct sulu_node *n, int x, int y) {
    if(!n) return 0;
    
    // Evaluate conditions for visibility-changing nodes
    if(n->type == NODE_IF) {
        if(!eval_expr(n->expr_left)) return 0;
    }
    
    // Z-order: Children are appended, so last child is drawn on top.
    // Search in reverse for hits.
    struct sulu_node *child = n->children;
    struct sulu_node *best_hit = 0;
    while(child) {
        struct sulu_node *found = find_clicked_node(child, x, y);
        if(found) best_hit = found; // Keep the 'latest' one found
        child = child->next;
    }
    if(best_hit) return best_hit;
    
    // Check this node itself
    if(x >= n->x && x <= n->x + n->w && y >= n->y && y <= n->y + n->h) {
        if(n->type == NODE_BUTTON || n->type == NODE_RECT || n->type == NODE_TEXTBOX) 
            return n;
    }
    
    return 0;
}

// --- Main Loop ---

// Full-window render (used for first paint, resize, etc.)
void render_app_full() {
    did_return = 0;
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
    if(win_def) bg = (uint32)get_prop_int(win_def, "bg", 0xFFFFFFFF);
    
    if(!initialized_buffers && (win.shm->flags & SULU_FLAG_DOUBLE_BUFFER)) {
        draw_local_rect(0, 0, win.width, win.height, bg);
        sulu_swap(&win);
        draw_local_rect(0, 0, win.width, win.height, bg);
        sulu_swap(&win);
        initialized_buffers = 1;
    }

    // Force textbox to do a complete repaint
    tb_full_redraw = 1;

    draw_local_rect(0, 0, win.width, win.height, bg);
    
    if(layout_def) {
        layout_node(layout_def, 0, 0, win.width);
        draw_node(layout_def);
    }
    
    sulu_swap(&win);
}

// Incremental render — only redraws textbox dirty lines, skips everything else.
// Non-textbox nodes (header bar, buttons) are only redrawn on full render.
//
// DOUBLE-BUFFER TRICK: All Sulu drawing helpers internally call sulu_pixels(shm)
// which uses shm->front_buf to select the buffer. By temporarily flipping
// front_buf, we redirect ALL pixel writes to the front buffer (what the compositor
// reads). After drawing, we restore it and issue blit_rect for the dirty region.
void render_app_incremental() {
    did_return = 0;
    struct sulu_node *layout_def = 0;
    struct sulu_node *curr = root_ast->children;
    while(curr) {
        if(curr->type == NODE_LAYOUT_DEF) layout_def = curr;
        curr = curr->next;
    }
    
    if(layout_def) {
        layout_node(layout_def, 0, 0, win.width);

        // Flip front_buf so sulu_pixels() returns the front buffer
        int saved_front = win.shm->front_buf;
        if(win.shm->flags & SULU_FLAG_DOUBLE_BUFFER) {
            // sulu_pixels returns the buffer that is NOT front_buf.
            // So to write to the currently-displayed (front) buffer,
            // we flip front_buf temporarily.
            win.shm->front_buf = (saved_front == 0) ? 1 : 0;
        }

        // Only draw textbox nodes — they handle their own partial blit
        struct sulu_node *child = layout_def->children;
        while(child) {
            if(child->type == NODE_TEXTBOX) draw_textbox(child);
            child = child->next;
        }

        // Restore original front_buf
        win.shm->front_buf = saved_front;
    }
    // No sulu_swap here — textbox does its own blit_rect
}

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: sulula <file.sul>\n");
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
    char *init_func    = 0;
    char *frame_func   = 0;
    char *keydown_func = 0;
    char *keyup_func   = 0;
    char *resize_func  = 0;
    char *mousedown_func = 0;
    char *mouseup_func   = 0;
    char *mousemove_func = 0;
    if(layout_node_ref) {
        init_func      = get_prop_str(layout_node_ref, "onInit",    0);
        frame_func     = get_prop_str(layout_node_ref, "onFrame",   0);
        keydown_func   = get_prop_str(layout_node_ref, "onKeyDown", 0);
        keyup_func     = get_prop_str(layout_node_ref, "onKeyUp",   0);
        resize_func    = get_prop_str(layout_node_ref, "onResize",  0);
        mousedown_func = get_prop_str(layout_node_ref, "onMouseDown", 0);
        mouseup_func   = get_prop_str(layout_node_ref, "onMouseUp",   0);
        mousemove_func = get_prop_str(layout_node_ref, "onMouseMove", 0);
        char *wheel_func = get_prop_str(layout_node_ref, "onMouseWheel", 0);
        (void)wheel_func; // Will use in loop
    }

    set_var("windowW", win.width);
    set_var("windowH", win.height);
    set_var("mouse_wheel", 0);
    set_var("mouse_rx", 0);
    set_var("mouse_ry", 0);
    set_var("needs_redraw", 0);

    // One-shot initialisation before the loop
    if(init_func) exec_func(init_func);

    int dirty = 2; // 2 = full redraw, 1 = incremental, 0 = idle

    while(1) {
        // Animation/Frame Logic
        if(frame_func) {
            exec_func(frame_func);
            // Only force dirty if script explicitly requested it
            int nr = get_var("needs_redraw");
            if(nr == 2) { dirty = 2; set_var("needs_redraw", 0); }
            else if(nr == 1) { if(dirty < 1) dirty = 1; set_var("needs_redraw", 0); }
            // If needs_redraw was not set, don't touch dirty
        }

        // Render based on dirty level
        if(dirty == 2) {
            render_app_full();
            dirty = 0;
        } else if(dirty >= 1) {
            render_app_incremental();
            dirty = 0;
        }

        // Process Events
        while (sulu_event_pop(win.shm, &ev) >= 0) {
            if(ev.type == SULU_EV_CLOSE) goto cleanup;

            if(ev.type == SULU_EV_KEY) {
                set_var("key", ev.code);
                set_var("key_val", ev.value);
                if(ev.value == 1 && keydown_func) { exec_func(keydown_func); if(dirty < 1) dirty = 1; }
                if(ev.value == 0 && keyup_func)   { exec_func(keyup_func);   if(dirty < 1) dirty = 1; }
            }

            if(ev.type == SULU_EV_RESIZE) {
                win.width  = ev.x;
                win.height = ev.y;
                set_var("windowW", win.width);
                set_var("windowH", win.height);
                if(resize_func) exec_func(resize_func);
                dirty = 2; // resize always needs full redraw
            }

            if(ev.type == SULU_EV_MOUSE_BTN) {
                set_var("mouse_rx", ev.rx);
                set_var("mouse_ry", ev.ry);
                if(ev.value == 1) { // L-Down
                    struct sulu_node *hit = 0;
                    if(layout_node_ref) hit = find_clicked_node(layout_node_ref, ev.rx, ev.ry);

                    if(hit && hit->type == NODE_BUTTON) {
                        char *callback = get_prop_str(hit, "onClick", 0);
                        if(callback) exec_func(callback);
                    } else {
                        // Propagate to global handler only if it's the textbox or empty space
                        if(!hit || hit->type == NODE_TEXTBOX) {
                            if(mousedown_func) exec_func(mousedown_func);
                        }
                    }
                } else { // L-Up
                    if(mouseup_func) exec_func(mouseup_func);
                }
            }

            if(ev.type == SULU_EV_MOUSE_MOVE) {
                set_var("mouse_rx", ev.rx);
                set_var("mouse_ry", ev.ry);
                
                // Only propagate move if not blocked by UI
                struct sulu_node *move_hit = 0;
                if(layout_node_ref) move_hit = find_clicked_node(layout_node_ref, ev.rx, ev.ry);
                if(!move_hit || move_hit->type == NODE_TEXTBOX) {
                    if(mousemove_func) exec_func(mousemove_func);
                }
                // No forced dirty here to prevent flickering
            }

            if(ev.type == SULU_EV_MOUSE_WHEEL) {
                set_var("mouse_wheel", ev.value);
                if(layout_node_ref) {
                    char *wheel_func = get_prop_str(layout_node_ref, "onMouseWheel", 0);
                    if(wheel_func) exec_func(wheel_func);
                }
                if(dirty < 1) dirty = 1;
            }

            if(ev.type == SULU_EV_PASTE) {
                set_var_str("clipboard", win.shm->clipboard);
                if(layout_node_ref) {
                    char *paste_func = get_prop_str(layout_node_ref, "onPaste", 0);
                    if(paste_func) exec_func(paste_func);
                }
                dirty = 2; // Force full redraw to show pasted content
            }
        }

        usleep(16000); // ~60 FPS
    }
    
cleanup:
    suluscript_free_node(root_ast);
    exit(0);
}
