# SuluScript Documentation

SuluScript is a lightweight, declarative scripting and layout language designed specifically for the XV6 Sulu Window System. It allows developers to define window properties, application state, logic, and UI layout in a single, human-readable file.

## 1. Syntax Overview

SuluScript uses a C-like syntax for logic and a block-based syntax for UI definitions.

### Window Definition
Defines the metadata for the application window.
```javascript
window {
    title: "My Application",
    bg: 0x123456,        // ARGB Color
    width: 400,
    height: 300
}
```

### Variables
SuluScript supports global and local integer/string variables.
```javascript
var count = 0;
var status = "Ready";
```

### Functions
Functions handle interactivity and logic.
```javascript
fn handle_click(id: int) {
    count = count + 1;
    return count;
}
```

### Layout
The `layout` block defines the visual hierarchy. It uses two primary containers:
- `vbox`: Vertically stacks children.
- `hbox`: Horizontally stacks children.

UI Elements:
- `text(content: "...")`: Renders a string.
- `button(label: "...", onClick: fn_name)`: An interactive button.
- `rect(width: X, height: Y, color: Z)`: A solid color rectangle.

```javascript
layout {
    vbox(padding: 10, gap: 5) {
        text("Hello Sulu!");
        button(label: "Click Me", onClick: handle_click);
    }
}
```

## 2. Theoretical Architecture

SuluScript is built using a classic compiler pipeline designed for low memory usage on XV6.

### The Lexer (`user/suluscript_lexer.c`)
The lexer (tokenizer) performs a single pass over the source text, converting characters into meaningful tokens. It handles:
- Keywords (`window`, `fn`, `layout`, etc.)
- Identifiers and Numbers (Decimal and Hexadecimal)
- String Literals
- Operators and Punctuation

### The Parser (`user/suluscript_parser.c`)
The parser is a **Recursive Descent Parser**. It consumes tokens from the lexer and builds an **Abstract Syntax Tree (AST)**.
- Each node in the tree is a `struct sulu_node`.
- The parser maintains a `next` token for 1-token lookahead.
- It validates the structure of the program and produces errors if the syntax is malformed.

### The Layout Engine (`user/sulu_interpreter.c`)
Before rendering, the interpreter performs a **Layout Pass**:
1. It traverses the AST starting from the `layout` node.
2. For each container (`vbox`/`hbox`), it calculates the absolute `(x, y)` coordinates and dimensions `(w, h)` for every child node.
3. This pass accounts for `padding` and `gap` properties.

### The Painter (`user/sulu_interpreter.c`)
The painter traverses the tree and executes drawing commands:
- It maps `sulu_node` types to `sulu_client.h` drawing functions (`sulu_draw_text`, `sulu_fill_rect`, etc.).
- It uses the coordinates calculated during the Layout Pass.

### The Virtual Machine (Coming Soon)
A tiny tree-walking interpreter that executes the logic inside `fn` blocks when events (like `MOUSE_BTN`) are triggered.

## 3. Building and Running

SuluScript is integrated into the XV6 `Makefile`.

To compile:
```bash
make
```

To run an app:
```bash
sulu_run my_app.sul
```

To test the parser output:
```bash
sulutest my_app.sul
```
