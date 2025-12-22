#include "kernel/types.h"
#include "user/user.h"
#include "user/tui.h"
#include <stdarg.h>

void
tui_init(void)
{
  tui_clear();
}

void
tui_gotoxy(int x, int y)
{
  // ANSI ESC [ y ; x H
  // ANSI coordinates are 1-based
  printf("\033[%d;%dH", y, x);
}

void
tui_color(int fg, int bg)
{
  printf("\033[%d;%dm", fg, bg);
}

void
tui_bold(int on)
{
  if(on)
    printf("\033[1m");
  else
    printf("\033[22m");
}

void
tui_reset(void)
{
  printf("\033[0m");
}

void
tui_clear(void)
{
  printf("\033[2J\033[H");
}

void
tui_box(int x, int y, int w, int h, char *title)
{
  int i;

  // Top border
  tui_gotoxy(x, y);
  printf("+");
  for(i = 0; i < w - 2; i++) printf("-");
  printf("+");

  // Bottom border
  tui_gotoxy(x, y + h - 1);
  printf("+");
  for(i = 0; i < w - 2; i++) printf("-");
  printf("+");

  // Sides
  for(i = 1; i < h - 1; i++){
    tui_gotoxy(x, y + i);
    printf("|");
    tui_gotoxy(x + w - 1, y + i);
    printf("|");
  }

  // Title
  if(title && strlen(title) > 0){
    int tlen = strlen(title);
    if(tlen > w - 4) tlen = w - 4;
    tui_gotoxy(x + (w - tlen) / 2, y);
    printf(" ");
    for(i = 0; i < tlen; i++) printf("%c", title[i]);
    printf(" ");
  }
}

void
tui_printat(int x, int y, char *fmt, ...)
{
  va_list ap;
  tui_gotoxy(x, y);
  va_start(ap, fmt);
  vprintf(1, fmt, ap);
  va_end(ap);
}
