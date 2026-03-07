#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/sulu_client.h"
#include "user/suluscript.h"
#include "user/font.h"

struct sulu_window win;
struct sulu_node *root_ast = 0;
static int did_return = 0;

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
    return 0;
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
            case '<': res = l < r; break;
            case '>': res = l > r; break;
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

void draw_node(struct sulu_node *n) {
    if(!n) return;
    
    switch(n->type) {
        case NODE_TEXT:
            draw_local_text(get_prop_int(n, "x", n->x), get_prop_int(n, "y", n->y), get_prop_str(n, "content", ""), (uint32)get_prop_int(n, "color", 0xFF000000));
            break;
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
    
    struct sulu_event ev;
    char *frame_func = 0;
    char *keydown_func = 0;
    char *keyup_func = 0;
    if(layout_node_ref) {
        frame_func = get_prop_str(layout_node_ref, "onFrame", 0);
        keydown_func = get_prop_str(layout_node_ref, "onKeyDown", 0);
        keyup_func = get_prop_str(layout_node_ref, "onKeyUp", 0);
    }

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

            if(ev.type == SULU_EV_MOUSE_BTN && ev.value == 1) { // L-Down
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
