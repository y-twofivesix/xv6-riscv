#ifndef TUI_H
#define TUI_H

// ANSI Colors
#define TUI_BLACK   30
#define TUI_RED     31
#define TUI_GREEN   32
#define TUI_YELLOW  33
#define TUI_BLUE    34
#define TUI_MAGENTA 35
#define TUI_CYAN    36
#define TUI_WHITE   37

#define TUI_BG_BLACK   40
#define TUI_BG_RED     41
#define TUI_BG_GREEN   42
#define TUI_BG_YELLOW  43
#define TUI_BG_BLUE    44
#define TUI_BG_MAGENTA 45
#define TUI_BG_CYAN    46
#define TUI_BG_WHITE   47

// TUI primitives
void tui_init(void);
void tui_gotoxy(int x, int y);
void tui_color(int fg, int bg);
void tui_bold(int on);
void tui_reset(void);
void tui_clear(void);
void tui_box(int x, int y, int w, int h, char *title);
void tui_printat(int x, int y, char *fmt, ...);

#endif
