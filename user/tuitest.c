#include "kernel/types.h"
#include "user/user.h"
#include "user/tui.h"

int
main(int argc, char *argv[])
{
  tui_init();

  // Draw a main background box
  tui_color(TUI_WHITE, TUI_BG_BLUE);
  tui_box(2, 2, 76, 20, "xv6 TUI Demo");

  // Draw a red sub-window
  tui_color(TUI_WHITE, TUI_BG_RED);
  tui_box(5, 5, 30, 10, "Control Panel");
  tui_printat(7, 7, "CPU: 8 Harts");
  tui_printat(7, 8, "RAM: 128 MB");
  tui_printat(7, 9, "Disk: 2048 Blocks");

  // Draw a green sub-window
  tui_color(TUI_BLACK, TUI_BG_GREEN);
  tui_box(40, 5, 30, 10, "Log Viewer");
  tui_printat(42, 7, "Starting Rio...");
  tui_printat(42, 8, "Loading drivers...");
  tui_printat(42, 9, "Shell ready.");

  // Draw some bold text in the middle
  tui_reset();
  tui_color(TUI_YELLOW, TUI_BG_BLACK);
  tui_bold(1);
  tui_printat(30, 18, " PRESS ANY KEY TO EXIT ");
  tui_bold(0);

  // Position cursor at a logical place
  tui_gotoxy(2, 22);
  tui_reset();
  
  // Exit message
  printf("\nTUI session finished.\n");
  
  exit(0);
}
