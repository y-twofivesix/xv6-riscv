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
*   **Operators**: `+`, `-`, `*`, `/`, `%`, `<`, `>`, `==`.
*   **Grouping**: Supports parentheses [( ... )](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulu.c#1171-1900).
*   **Precedence**: Multiplication, division, and modulo reflect standard C precedence.

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
*   **[text(content, x, y, color)](file:///Users/takudzwamakoni/dev/xv6-riscv/user/sulu_client.h#557-573)**: Draws text on the screen.
*   **`button(label, onClick, x, y)`**: Interactive button.
*   **`vbox { ... }`, `hbox { ... }`**: Layout containers for vertical/horizontal stacking.

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

---

## 4. Built-in Variables
The following variables are automatically populated by the system during event callbacks:
*   [key](file:///Users/takudzwamakoni/dev/xv6-riscv/user/suluscript_lexer.c#54-74): The scancode of the key being pressed/released.
*   `key_val`: `1` for key down, `0` for key up.
