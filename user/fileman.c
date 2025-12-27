// File Manager for Sulu
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/sulu_client.h"

#define BTN_LEFT 0x110

#define MAX_FILES 128
#define ICON_SIZE 40
#define GRID_COLS 4
#define GRID_SPACING 20
#define PADDING 10
#define TOP_BAR_HEIGHT 24

// Colors
#define BG_COLOR 0xFF202020
#define TOP_BAR_COLOR 0xFF404040
#define TEXT_COLOR 0xFFFFFFFF
#define FOLDER_COLOR 0xFFFFAA00
#define FILE_COLOR 0xFFAAAAAA
#define SEL_COLOR 0xFF0055AA
#define DEVICE_COLOR 0xFFAA00AA

struct FileInfo {
    char name[DIRSIZ+1];
    int type; // T_DIR, T_FILE
    int size;
};

// State
struct sulu_window win;
struct FileInfo files[MAX_FILES];
int file_count = 0;
int selection = -1;
int scroll_offset = 0;
int view_mode = 0; // 0=Grid, 1=List
char current_path[512];

// Helper to format name (from ls.c)
void fmtname(char *path, char *dest) {
  char *p;
  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  memmove(dest, p, strlen(p));
  dest[strlen(p)] = 0;
}

void load_dir(char *path) {
    int fd;
    struct dirent de;
    struct stat st;
    
    file_count = 0;
    selection = -1;
    scroll_offset = 0;
    strcpy(current_path, path);

    if((fd = open(path, 0)) < 0){
        // printf("fileman: cannot open %s\n", path);
        return;
    }
    
    // Add ".." entry manually if not root
    if(strcmp(path, "/") != 0) {
        strcpy(files[file_count].name, "..");
        files[file_count].type = T_DIR;
        files[file_count].size = 0;
        file_count++;
    }

    while(read(fd, &de, sizeof(de)) == sizeof(de)){
        if(de.inum == 0) continue;
        
        // Safe name handling
        char name[DIRSIZ+1];
        memmove(name, de.name, DIRSIZ);
        name[DIRSIZ] = 0;
        
        if(strcmp(name, ".") == 0) continue;
        if(strcmp(name, "..") == 0) continue;
        

        if(file_count >= MAX_FILES) break;

        // Stat to get type
        char buf[512];
        strcpy(buf, path);
        if(path[strlen(path)-1] != '/') strcat(buf, "/");
        strcat(buf, name);
        
        if(stat(buf, &st) >= 0) {
            memmove(files[file_count].name, name, DIRSIZ+1);
            files[file_count].type = st.type;
            files[file_count].size = st.size;
            file_count++;
        }
    }
    close(fd);
}

void draw_icon(int x, int y, int type, int selected) {
    uint color = FILE_COLOR;
    if(type == T_DIR) color = FOLDER_COLOR;
    if(type == T_DEVICE) color = DEVICE_COLOR;
    
    // Selection box
    if(selected) {
        sulu_fill_rect(win.shm, x - 5, y - 5, ICON_SIZE + 10, ICON_SIZE + 20, SEL_COLOR);
    }
    
    // Folder shape or File shape
    if(type == T_DIR) {
        // Folder body
        sulu_fill_rect(win.shm, x, y + 5, ICON_SIZE, ICON_SIZE - 5, color);
        // Folder tab
        sulu_fill_rect(win.shm, x, y, ICON_SIZE / 2, 5, color);
    } else if(type == T_DEVICE) {
        // Chip Body (DIP style)
        int bx = x + 10;
        int by = y + 4;
        int bw = 20;
        int bh = 32;
        
        // Pins (Silver)
        uint pin_col = 0xFFCCCCCC;
        for(int i=0; i<4; i++) {
            // Left pins
            sulu_fill_rect(win.shm, bx - 4, by + 4 + i*7, 4, 3, pin_col);
            // Right pins
            sulu_fill_rect(win.shm, bx + bw, by + 4 + i*7, 4, 3, pin_col);
        }

        // Main Body (Purple)
        sulu_fill_rect(win.shm, bx, by, bw, bh, color);
        
        // Notch indicator
        sulu_fill_rect(win.shm, bx + 6, by, 8, 4, 0xFF550055);
    } else {
        // File body
        sulu_fill_rect(win.shm, x + 5, y, ICON_SIZE - 10, ICON_SIZE, color);
        // Corner fold
        sulu_fill_rect(win.shm, x + ICON_SIZE - 10, y, 5, 5, BG_COLOR); // Cut corner? simplified
    }
}

void render() {
    // Clear
    sulu_clear(win.shm, BG_COLOR);
    
    // Top Bar
    sulu_fill_rect(win.shm, 0, 0, win.width, TOP_BAR_HEIGHT, TOP_BAR_COLOR);
    sulu_draw_text(win.shm, 5, 5, current_path, TEXT_COLOR);

    // View Toggle Button: [L] or [G]
    char *btn_text = (view_mode == 0) ? "[L]" : "[G]";
    sulu_draw_text(win.shm, win.width - 30, 5, btn_text, TEXT_COLOR);
    
    int start_y = TOP_BAR_HEIGHT + PADDING;
    int start_x = PADDING;
    
    if(view_mode == 0) { // GRID VIEW
        for(int i=0; i<file_count; i++) {
            int col = i % GRID_COLS;
            int row = i / GRID_COLS;
            
            int x = start_x + col * (ICON_SIZE + GRID_SPACING + PADDING);
            int y = start_y + row * (ICON_SIZE + GRID_SPACING + PADDING + 15);
            
            // Apply scroll
            int screen_y = y - scroll_offset;
            
            // Simple clipping
            if(screen_y + ICON_SIZE + 20 < TOP_BAR_HEIGHT || screen_y > win.height) continue;
            
            draw_icon(x, screen_y, files[i].type, (i == selection));
            
            // Name (truncated)
            char display_name[16];
            int len = strlen(files[i].name);
            if(len > 12) {
                 memmove(display_name, files[i].name, 12);
                 strcpy(display_name+12, "...");
            } else {
                 strcpy(display_name, files[i].name);
            }
            sulu_draw_text(win.shm, x, screen_y + ICON_SIZE + 2, display_name, TEXT_COLOR);
        }
    } else { // LIST VIEW
        int row_h = 24;
        for(int i=0; i<file_count; i++) {
            int x = start_x;
            int y = start_y + i * row_h;
            int screen_y = y - scroll_offset;
            
            if(screen_y + row_h < TOP_BAR_HEIGHT || screen_y > win.height) continue;
            
            // Selection BG
            if(i == selection) {
                sulu_fill_rect(win.shm, 0, screen_y, win.width, row_h, SEL_COLOR);
            }
            
            // Small Icon (12x12)
            uint color = FILE_COLOR;
            if(files[i].type == T_DIR) color = FOLDER_COLOR;
            if(files[i].type == T_DEVICE) color = DEVICE_COLOR;
            sulu_fill_rect(win.shm, x, screen_y + 6, 12, 12, color);
            
            // Name
            sulu_draw_text(win.shm, x + 20, screen_y + 8, files[i].name, TEXT_COLOR);
        }
    }
    
    sulu_blit(win.shm, 0, 0, win.width, win.height);
}

void launch(char *name) {
    char path[512];
    strcpy(path, current_path);
    if(path[strlen(path)-1] != '/') strcat(path, "/");
    strcat(path, name);
    
    // Check if file is an ELF executable
    int fd = open(path, O_RDONLY);
    int is_elf = 0;
    if(fd >= 0) {
        uint magic = 0;
        if(read(fd, &magic, 4) == 4) {
            // Check for ELF Magic: 0x7F 'E' 'L' 'F' 
            // In Little Endian this is 0x464C457F
            if(magic == 0x464C457F) {
                is_elf = 1;
            }
        }
        close(fd);
    }
    
    int pid = fork();
    if(pid == 0) {
        if (is_elf) {
            char *argv[] = {path, 0};
            exec(path, argv);
            printf("fileman: exec failed for %s\n", path);
        } else {
             // Check Extension for .bmp
             int len = strlen(path);
             if(len > 4 && strcmp(path + len - 4, ".bmp") == 0) {
                 char *argv[] = {"viewer", path, 0};
                 exec("viewer", argv);
                 printf("fileman: failed to launch viewer for %s\n", path);
             } else {
                // Fallback to editor for non-executable files
                char *argv[] = {"editor", path, 0};
                exec("editor", argv);
                printf("fileman: failed to launch editor for %s\n", path);
             }
        }
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    // Init Sulu
    if(sulu_init(&win, 300, 400, BG_COLOR, 0) < 0) {
        printf("fileman: sulu_init failed\n");
        exit(1);
    }
    sulu_set_title(win.shm, "File Manager");
    
    // Give Sulu time to register window
    sleep(50);
    
    load_dir("/");
    render();
    
    while(1) {
        if(sulu_event_available(win.shm)) {
            struct sulu_event ev;
            sulu_event_pop(win.shm, &ev);
            
            if(ev.type == SULU_EV_CLOSE) exit(0);
            
            if(ev.type == SULU_EV_MOUSE_BTN && ev.code == BTN_LEFT && ev.value == 1) {
                // Click handling
                int mx = ev.x - win.shm->x;
                int my = ev.y - (win.shm->y + SULU_TITLE_BAR_HEIGHT);
                
                int start_y = TOP_BAR_HEIGHT + PADDING;
                int start_x = PADDING;
                int clicked_idx = -1;
                
                // Check Toggle Button (Top Right)
                if(my < 0) { // SULU_TITLE_BAR_HEIGHT offset means top bar is at my < 0? No.
                    // my is relevant to window content area start (0,0).
                    // Top bar is at 0..TOP_BAR_HEIGHT.
                }

                if(my < TOP_BAR_HEIGHT && mx > win.width - 40) {
                     view_mode = !view_mode;
                     render();
                     continue; // handled
                }
                
                
                for(int i=0; i<file_count; i++) {
                    if(view_mode == 0) { // GRID HIT TEST
                        int col = i % GRID_COLS;
                        int row = i / GRID_COLS;
                        
                        int x = start_x + col * (ICON_SIZE + GRID_SPACING + PADDING);
                        int y = start_y + row * (ICON_SIZE + GRID_SPACING + PADDING + 15);
                        
                        // Hit test in virtual coords (add scroll to mouse)
                        int virtual_my = my + scroll_offset;
                        
                        if(mx >= x && mx < x + ICON_SIZE && 
                           virtual_my >= y && virtual_my < y + ICON_SIZE + 15) {
                            clicked_idx = i;
                            break;
                        }
                    } else { // LIST HIT TEST
                        int row_h = 24;
                        int y = start_y + i * row_h;
                        int virtual_my = my + scroll_offset;
                        
                        if(virtual_my >= y && virtual_my < y + row_h) {
                            clicked_idx = i;
                            break;
                        }
                    }
                }
                
                if(clicked_idx != -1) {
                    selection = clicked_idx;
                    render();
                } else {
                    if(selection != -1) {
                        selection = -1;
                        render();
                    }
                }
            } else if (ev.type == SULU_EV_MOUSE_DBLCLICK) {
                // Double Click Handling
                int mx = ev.x - win.shm->x;
                int my = ev.y - (win.shm->y + SULU_TITLE_BAR_HEIGHT);
                int clicked_idx = -1;
                
                int start_y = TOP_BAR_HEIGHT + PADDING;
                int start_x = PADDING;
                
                // Reuse Hit Test Logic
               for(int i=0; i<file_count; i++) {
                    if(view_mode == 0) { // GRID HIT TEST
                        int col = i % GRID_COLS;
                        int row = i / GRID_COLS;
                        int x = start_x + col * (ICON_SIZE + GRID_SPACING + PADDING);
                        int y = start_y + row * (ICON_SIZE + GRID_SPACING + PADDING + 15);
                        int virtual_my = my + scroll_offset;
                        if(mx >= x && mx < x + ICON_SIZE && 
                           virtual_my >= y && virtual_my < y + ICON_SIZE + 15) {
                            clicked_idx = i;
                            break;
                        }
                    } else { // LIST HIT TEST
                        int row_h = 24;
                        int y = start_y + i * row_h;
                        int virtual_my = my + scroll_offset;
                        if(virtual_my >= y && virtual_my < y + row_h) {
                            clicked_idx = i;
                            break;
                        }
                    }
                }
                
                if(clicked_idx != -1) {
                        if(files[clicked_idx].type == T_DIR) {
                            // Change Dir
                            char new_path[512];
                            strcpy(new_path, current_path);
                            // Ensure only one slash
                            int len = strlen(new_path);
                            if(len > 0 && new_path[len-1] != '/') strcat(new_path, "/"); // Safer check
                            strcat(new_path, files[clicked_idx].name);
                            
                            chdir(files[clicked_idx].name);
                            
                            if(strcmp(files[clicked_idx].name, "..") == 0) {
                                // Go up logic simple
                                int l = strlen(current_path);
                                while(l > 0 && current_path[l-1] != '/') l--;
                                if(l > 1) l--; 
                                current_path[l] = 0;
                                if(strlen(current_path) == 0) strcpy(current_path, "/");
                                load_dir(current_path);
                            } else {
                                load_dir(new_path);
                            }
                            render();
                        } else if(files[clicked_idx].type == T_FILE) {
                            launch(files[clicked_idx].name);
                        } else if(files[clicked_idx].type == T_DEVICE) {
                            // Do nothing
                        }
                }
            }
            
            // ... Wheel logic ...
            if(ev.type == SULU_EV_MOUSE_WHEEL) {
                scroll_offset -= ev.value * 20; 
                if(scroll_offset < 0) scroll_offset = 0;
                int total_rows;
                int row_h;
                if(view_mode == 0) {
                    total_rows = (file_count + GRID_COLS - 1) / GRID_COLS;
                    row_h = (ICON_SIZE + GRID_SPACING + PADDING + 15);
                } else {
                    total_rows = file_count;
                    row_h = 24;
                }
                int content_height = TOP_BAR_HEIGHT + PADDING + total_rows * row_h;
                int max_scroll = content_height - win.height + PADDING;
                if(max_scroll < 0) max_scroll = 0;
                if(scroll_offset > max_scroll) scroll_offset = max_scroll;
                render();
            }
        }
        sleep(1);
    }
    return 0;
}
