# SuluScript Language Documentation

SuluScript is a lightweight, agentic scripting language designed for building dynamic UI applications and games within the **Sulu Window System** on xv6-riscv.

## 1. Core Syntax

### Variables
Variables are globally scoped and can hold integers or strings.
```javascript
var x = 10;
var name = "Sulu";
var bodyX = 0; // Arrays are initialized as 0 and populated with push()
```

### Functions
Functions are defined with the [fn](file:///Users/takudzwamakoni/dev/xv6-riscv/user/suluscript_parser.c#208-241) keyword.
```javascript
fn update() {
    x = x + 1;
}
```

### Control Flow
*   **[if](file:///Users/takudzwamakoni/dev/xv6-riscv/user/suluscript_parser.c#310-319)**: Supports standard conditional execution.
*   **[for](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sh.c#453-463)**: Standard loop: [for(init; cond; post) { ... }](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sh.c#453-463).
*   **`return`**: Exits the current function or block.

### Expressions
*   **Operators**: `+`, `-`, `*`, `/`, `%`, `<`, `>`, `<=`, `>=`, `==`, `!=`, `&&`, `||`.
*   **Grouping**: Supports parentheses `( ... )`.
*   **Precedence** (lowest to highest): `||` → `&&` → `==`/`!=` → `<`/`>`/`<=`/`>=` → `+`/`-` → `*`/`/`/`%`.

---

## 2. UI Declarations

Applications are defined using two main blocks: [window](file:///Users/takudzwamakoni/dev/xv6-riscv/user/suluscript_parser.c#176-191) and [layout](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulu_interpreter.c#296-401).

### `window { ... }`
Defines the window properties.
*   [title](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulu_client.h#374-382): The window title string.
*   [bg](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulu_client.h#383-387): Background color (hex ARGB, e.g., `0xFF111111`).
*   `width`, `height`: Initial dimensions.

### [layout( ... ) { ... }](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulu_interpreter.c#296-401)
Defines the visual structure and behavior.
*   **Event Callbacks**:
    *   `onFrame`: Function called every frame (~60fps).
    *   `onKeyDown`: Function called when a key is pressed.
    *   `onKeyUp`: Function called when a key is released.

#### UI Elements
Elements can be nested within `vbox` or `hbox`.
*   **[rect(x, y, width, height, color)](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulu_client.h#401-421)**: Draws a solid color rectangle.
*   **[text(content, x, y, color)](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulu_client.h#557-573)**: Draws text on the screen. If `content` is an array variable, it renders as a char array with `\n` line-wrapping.
*   **`button(label, onClick, x, y)`**: Interactive button. Fires `onClick` callback on mouse click.
*   **`vbox { ... }`, `hbox { ... }`**: Layout containers for vertical/horizontal stacking.
*   **`textbox(content, cursor, x, y, width, height, color, bg, scroll)`**: Multi-line text editing area. Renders a char array with a visible cursor block. Clips to `width`/`height`. Scrolls vertically by `scroll` lines.
    *   `content`: array variable holding the text as char codes.
    *   `cursor`: integer variable holding the cursor index into the array.
    *   `scroll`: integer variable — number of lines to skip from the top.
    *   `color`: text color (ARGB). `bg`: background color (ARGB).

#### Layout Event Callbacks
*   `onFrame`: Called every frame (~60 FPS).
*   `onKeyDown` / `onKeyUp`: Called on key press/release. Sets `key` and `key_val`.
*   `onResize`: Called when WM resizes the window. Updates `windowW`/`windowH`.
*   `onMouseDown`: Called on left mouse button press. Sets `mouse_rx` and `mouse_ry` (window-relative coordinates) before the callback fires.

---

## 3. Native Functions

| Function | Description |
| :--- | :--- |
| [rand()](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulu_interpreter.c#76-80) | Returns a pseudo-random integer. |
| [srand(seed)](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulu_interpreter.c#72-75) | Seeds the random number generator. |
| [print(a, b, ...)](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulutest.c#21-56) | Logs values to the terminal console. |
| [usleep(usec)](file:///Users/takudzwamakoni/dev/xv6-riscv/user/ulib.c#324-339) | Pauses execution for N microseconds. |
| [push(arr, val)](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulu_client.h#158-168) | Appends a value to an array. |
| [at(arr, idx)](file:///Users/takudzwamakoni/dev/xv6-riscv/user/ulib.c#273-283) | Returns the value at the given index. |
| `update_at(arr, idx, val)` | Modifies the value at the given index. |
| [len(arr)](file:///Users/takudzwamakoni/dev/xv6-riscv/user/ulib.c#140-149) | Returns the number of elements in an array. |
| `clear_arr(arr)` | Resets an array's length to 0. |
| `insert_at(arr, idx, val)` | Inserts `val` at index `idx`, shifting elements right. Safe for cursor-based editing. |
| `delete_at(arr, idx)` | Deletes the element at index `idx`, shifting elements left. |
| `cursor_line(arr, cursor_idx)` | Returns the 0-based line number of the cursor position. |
| `cursor_col(arr, cursor_idx)` | Returns the 0-based column of the cursor position. |
| `line_start(arr, line_num)` | Returns the array index of the first character on line `line_num`. |
| `line_count(arr)` | Returns the total number of lines (newline-delimited) in the array. |
| `xy_to_cursor(arr, rx, ry, tb_x, tb_y, char_w, char_h, tb_w, scroll)` | Converts a mouse click at `(rx, ry)` into a cursor index for a textbox at `(tb_x, tb_y)` with given char dimensions and scroll offset. |
| `to_char(scancode, shift)` | Converts a raw scancode to an ASCII character code. |
| `read_file(path, arr)` | Loads file bytes into array. |
| `write_file(path, arr)` | Writes array bytes to file. |
| `resize(w, h)` | Resizes the Sulu window. Updates `windowW`/`windowH`. |
| `print(a, b, ...)` | Logs values to the serial console. |
| `usleep(usec)` | Pauses for N microseconds. |
| `rand()` / `srand(seed)` | PRNG. |

---

## 4. Built-in Variables
The following variables are automatically populated by the system during event callbacks:
*   `key`: The scancode of the key being pressed/released.
*   `key_val`: `1` for key down, `0` for key up.
*   `windowW` / `windowH`: Current window dimensions in pixels.
*   `arg1` / `arg2`: Command-line arguments passed to `sulula`.
*   `mouse_rx` / `mouse_ry`: Window-relative mouse coordinates, set before `onMouseDown` fires.

## 5. Key Scancodes (common)
| Scancode | Key |
| :--- | :--- |
| 1 | ESC |
| 14 | Backspace |
| 28 | Enter |
| 29 | Left Ctrl |
| 42 | Left Shift |
| 54 | Right Shift |
| 103 | Up Arrow |
| 105 | Left Arrow |
| 106 | Right Arrow |
| 108 | Down Arrow |
