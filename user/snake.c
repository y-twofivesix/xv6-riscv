
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "user/sulu_client.h"

#define GRID_SIZE 20
#define CELL_SIZE 20
#define WIN_W (GRID_SIZE * CELL_SIZE)
#define WIN_H (GRID_SIZE * CELL_SIZE)

#define SNAKE_COLOR 0xFF00FF00 // Green
#define FOOD_COLOR  0xFFFF0000 // Red
#define BG_COLOR    0xFF000000 // Black

// Game State
int snake_x[GRID_SIZE * GRID_SIZE];
int snake_y[GRID_SIZE * GRID_SIZE];
int snake_len;
int dir_x, dir_y;
int food_x, food_y;
int game_over;
int score;

// Circular buffer / simple array for snake
// Head is at index 0.

void spawn_food() {
    int valid = 0;
    while(!valid) {
        // Simple random generator using uptime
        // Note: uptime() is slow, using random memory might be better, but we don't have rand().
        // We'll use a simple LCG or just pointer address relative.
        static int seed = 12345;
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
        food_x = (seed % GRID_SIZE);
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
        food_y = (seed % GRID_SIZE);
        
        valid = 1;
        for(int i=0; i<snake_len; i++) {
            if(snake_x[i] == food_x && snake_y[i] == food_y) {
                valid = 0;
                break;
            }
        }
    }
}

void init_game() {
    snake_len = 3;
    snake_x[0] = 10; snake_y[0] = 10;
    snake_x[1] = 10; snake_y[1] = 11;
    snake_x[2] = 10; snake_y[2] = 12;
    dir_x = 0;
    dir_y = -1; // Moving Up
    score = 0;
    game_over = 0;
    spawn_food();
}

int main(int argc, char *argv[])
{
    struct sulu_window win;
    
    if(sulu_init(&win, WIN_W, WIN_H, BG_COLOR, SULU_FLAG_DOUBLE_BUFFER) < 0) {
        printf("snake: sulu_init failed\n");
        exit(1);
    }
    
    sulu_set_title(win.shm, "Snake - Arrows to Move, Q to Quit");
    
    // Seed randomish
    init_game();
    
    // Initial Draw
    sulu_clear(win.shm, BG_COLOR);
    sulu_swap(&win);
    sulu_clear(win.shm, BG_COLOR); // Clear both buffers
    
    
    while(1) {
        // 1. Process Input
        while(sulu_event_available(win.shm)) {
            struct sulu_event ev;
            sulu_event_pop(win.shm, &ev);
            
            if(ev.type == SULU_EV_CLOSE) exit(0);
            
            if(ev.type == SULU_EV_KEY && ev.value == 1) {
                if(ev.code == 103 && dir_y == 0) { dir_x = 0; dir_y = -1; } // UP
                if(ev.code == 108 && dir_y == 0) { dir_x = 0; dir_y = 1; }  // DOWN
                if(ev.code == 105 && dir_x == 0) { dir_x = -1; dir_y = 0; } // LEFT
                if(ev.code == 106 && dir_x == 0) { dir_x = 1; dir_y = 0; }  // RIGHT
                if(ev.code == 16) { // 'q'
                     exit(0);
                }
                if(ev.code == 19 && game_over) { // 'r' to restart
                    init_game();
                }
            }
        }
        
        if(!game_over) {
            // Update Positions
            // Move body
            for(int i = snake_len - 1; i > 0; i--) {
                snake_x[i] = snake_x[i-1];
                snake_y[i] = snake_y[i-1];
            }
            
            // Move head
            snake_x[0] += dir_x;
            snake_y[0] += dir_y;
            
            // Wall Collision
            if(snake_x[0] < 0 || snake_x[0] >= GRID_SIZE || 
               snake_y[0] < 0 || snake_y[0] >= GRID_SIZE) {
                game_over = 1;
            }
            
            // Self Collision
            for(int i = 1; i < snake_len; i++) {
                if(snake_x[0] == snake_x[i] && snake_y[0] == snake_y[i]) {
                    game_over = 1;
                }
            }
            
            // Food Collision
            if(snake_x[0] == food_x && snake_y[0] == food_y) {
                score++;
                snake_len++;
                spawn_food();
            }
        }
        
        // Render
        sulu_clear(win.shm, BG_COLOR);
        
        // Draw Food
        sulu_fill_rect(win.shm, food_x * CELL_SIZE, food_y * CELL_SIZE, CELL_SIZE, CELL_SIZE, FOOD_COLOR);
        
        // Draw Snake
        for(int i = 0; i < snake_len; i++) {
             uint color = (i == 0) ? 0xFF00AA00 : SNAKE_COLOR; // Head slightly different?
             if(game_over) color = 0xFF555555; // Grey if dead
             sulu_fill_rect(win.shm, snake_x[i] * CELL_SIZE, snake_y[i] * CELL_SIZE, CELL_SIZE, CELL_SIZE, color);
             
             // Gaps for visibility?
             // sulu_draw_rect(win.shm, snake_x[i] * CELL_SIZE, snake_y[i] * CELL_SIZE, CELL_SIZE, CELL_SIZE, BG_COLOR);
        }
        
        sulu_swap(&win);
        sulu_blit_rect(&win, 0, 0, WIN_W, WIN_H);
        
        // Speed control
        usleep(100000); // 100ms = 10 FPS
    }
    
    exit(0);
}
