
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "user/sulu_client.h"

#define GRID_W 10
#define GRID_H 10
#define NUM_MINES 10
#define CELL_SIZE 24
#define HEADER_H 40
#define WIN_W (GRID_W * CELL_SIZE)
#define WIN_H (GRID_H * CELL_SIZE + HEADER_H)

#define BG_COLOR 0xFFC0C0C0 // Classic Grey
#define CELL_COVERED 0xFFD0D0D0
#define CELL_REVEALED 0xFFE0E0E0
#define BORDER_DARK 0xFF808080
#define BORDER_LIGHT 0xFFFFFFFF

#define COLOR_MINE 0xFF000000
#define COLOR_FLAG 0xFFFF0000

// Cell States
// 0-8: Neighbor count
// 9: Mine
#define VAL_MINE 9

// visible_state:
// 0: Covered
// 1: Revealed
// 2: Flagged
#define STATE_COVERED 0
#define STATE_REVEALED 1
#define STATE_FLAGGED 2

int grid[GRID_W][GRID_H];
int state[GRID_W][GRID_H];
int game_over = 0;
int win_state = 0; // 0: Playing, 1: Won, -1: Lost
int flags_left = NUM_MINES;

void draw_rect_3d(struct sulu_window_shm *shm, int x, int y, int w, int h, int items_state) {
    // items_state: 0 = Pop out (Covered), 1 = Sunken (Revealed)
    uint top = items_state ? BORDER_DARK : BORDER_LIGHT;
    uint bot = items_state ? BORDER_LIGHT : BORDER_DARK;
    uint bg  = items_state ? CELL_REVEALED : CELL_COVERED;
    
    sulu_fill_rect(shm, x, y, w, h, bg);
    sulu_draw_line(shm, x, y, x+w-1, y, top); // Top
    sulu_draw_line(shm, x, y, x, y+h-1, top); // Left
    sulu_draw_line(shm, x+w-1, y, x+w-1, y+h-1, bot); // Right
    sulu_draw_line(shm, x, y+h-1, x+w-1, y+h-1, bot); // Bottom
}

void reset_game() {
    game_over = 0;
    win_state = 0;
    flags_left = NUM_MINES;
    
    // Clear grid
    for(int x=0; x<GRID_W; x++)
        for(int y=0; y<GRID_H; y++) {
            grid[x][y] = 0;
            state[x][y] = STATE_COVERED;
        }

    // Place Mines
    int mines_placed = 0;
    
    // Seed LCG
    static int seed = 999;
    
    while(mines_placed < NUM_MINES) {
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
        int rx = seed % GRID_W;
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
        int ry = seed % GRID_H;
        
        if(grid[rx][ry] != VAL_MINE) {
            grid[rx][ry] = VAL_MINE;
            mines_placed++;
            
            // Update neighbors
            for(int dx=-1; dx<=1; dx++) {
                for(int dy=-1; dy<=1; dy++) {
                    int nx = rx+dx, ny = ry+dy;
                    if(nx>=0 && nx<GRID_W && ny>=0 && ny<GRID_H && grid[nx][ny] != VAL_MINE) {
                        grid[nx][ny]++;
                    }
                }
            }
        }
    }
}

void reveal(int x, int y) {
    if(x<0 || x>=GRID_W || y<0 || y>=GRID_H) return;
    if(state[x][y] != STATE_COVERED) return;
    
    state[x][y] = STATE_REVEALED;
    
    if(grid[x][y] == VAL_MINE) {
        game_over = 1;
        win_state = -1; // Lost
        // Reveal all mines
        for(int i=0; i<GRID_W; i++)
            for(int j=0; j<GRID_H; j++)
                if(grid[i][j] == VAL_MINE) state[i][j] = STATE_REVEALED;
    } else if(grid[x][y] == 0) {
        // Flood fill empty
        for(int dx=-1; dx<=1; dx++)
            for(int dy=-1; dy<=1; dy++)
                reveal(x+dx, y+dy);
    }
    
    // Check Win
    if(!game_over) {
        int covered = 0;
        for(int i=0; i<GRID_W; i++)
            for(int j=0; j<GRID_H; j++)
                if(state[i][j] != STATE_REVEALED) covered++;
        
        if(covered == NUM_MINES) {
            game_over = 1;
            win_state = 1; // Won
        }
    }
}

void toggle_flag(int x, int y) {
    if(state[x][y] == STATE_COVERED) {
        state[x][y] = STATE_FLAGGED;
        flags_left--;
    } else if(state[x][y] == STATE_FLAGGED) {
        state[x][y] = STATE_COVERED;
        flags_left++;
    }
}

void draw_game(struct sulu_window_shm *shm) {
    sulu_clear(shm, BG_COLOR);
    
    // Header (Reset Face)
    sulu_draw_text(shm, 10, 10, "Minesweeper", 0xFF000000);
    
    // Status
    if(win_state == 1) sulu_draw_text(shm, 160, 10, ":)", 0xFF008800);
    else if(win_state == -1) sulu_draw_text(shm, 160, 10, "X(", 0xFFFF0000);
    else sulu_draw_text(shm, 160, 10, ":|", 0xFF000000);
    
    
    // Grid
    for(int x=0; x<GRID_W; x++) {
        for(int y=0; y<GRID_H; y++) {
            int px = x * CELL_SIZE;
            int py = y * CELL_SIZE + HEADER_H;
            
            if(state[x][y] == STATE_COVERED) {
                draw_rect_3d(shm, px, py, CELL_SIZE, CELL_SIZE, 0);
            } else if(state[x][y] == STATE_FLAGGED) {
                draw_rect_3d(shm, px, py, CELL_SIZE, CELL_SIZE, 0);
                sulu_fill_rect(shm, px+8, py+6, 8, 6, COLOR_FLAG); // Flag
                sulu_draw_line(shm, px+8, py+6, px+8, py+16, 0xFF000000); // Pole
            } else {
                // Revealed
                draw_rect_3d(shm, px, py, CELL_SIZE, CELL_SIZE, 1);
                int val = grid[x][y];
                if(val == VAL_MINE) {
                     sulu_fill_circle(shm, px+12, py+12, 6, COLOR_MINE);
                } else if(val > 0) {
                    uint colors[] = {0, 0xFF0000FF, 0xFF008000, 0xFFFF0000, 0xFF000080, 0xFF800000, 0xFF008080, 0xFF000000, 0xFF808080};
                    sulu_draw_char(shm, px+8, py+8, '0'+val, colors[val]);
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    struct sulu_window win;
    
    if(sulu_init(&win, WIN_W, WIN_H, BG_COLOR, SULU_FLAG_DOUBLE_BUFFER) < 0) {
        exit(1);
    }
    sulu_set_title(win.shm, "Minesweeper");
    
    reset_game();
    draw_game(win.shm);
    sulu_swap(&win);     // Swap to make visible
    draw_game(win.shm);  // Sync back buffer
    sulu_blit_rect(&win, 0, 0, WIN_W, WIN_H);
    
    while(1) {
        while(sulu_event_available(win.shm)) {
            struct sulu_event ev;
            sulu_event_pop(win.shm, &ev);
            
            if(ev.type == SULU_EV_CLOSE) exit(0);
            if(ev.type == SULU_EV_MOUSE_BTN && ev.value == 1) { // Mouse Down
                 // Adjust for Title Bar (20px)
                 int client_y = ev.ry - 20; 
                 int client_x = ev.rx;

                 if(client_y < 0) continue; // Clicked title bar

                 // Check coordinates
                 if(client_y < HEADER_H) {
                     // Clicked header - Reset?
                     reset_game();
                 } else {
                     int gx = client_x / CELL_SIZE;
                     int gy = (client_y - HEADER_H) / CELL_SIZE;
                     
                     if(gx >= 0 && gx < GRID_W && gy >= 0 && gy < GRID_H && !game_over) {
                         if(ev.code == BTN_LEFT) reveal(gx, gy);
                         if(ev.code == BTN_RIGHT) toggle_flag(gx, gy);
                     }
                 }
                 
                 draw_game(win.shm);
                 sulu_swap(&win);
                 draw_game(win.shm); // Sync double buffers
                 sulu_blit_rect(&win, 0, 0, WIN_W, WIN_H);
            }
        }
        sleep(5); // Idle loop
    }
    
    exit(0);
}
