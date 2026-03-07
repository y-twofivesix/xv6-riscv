# xv6-riscv Project: Full Context Memory Document

**Project**: Custom operating system built on top of the MIT xv6-riscv teaching kernel.
**Repository**: `/Users/takudzwamakoni/dev/xv6-riscv`
**Branch**: `research-iart`
**Status as of 2026-03-07**: Fully working GUI desktop OS with a scripting language.

---

## PART 1: KERNEL — FOUNDATION

### 1.1 Boot & Entry

- **`kernel/entry.S`**: First code that runs. Sets up machine-mode stack per hart, calls `start()`.
- **`kernel/start.c`**: Runs in M-mode. Configures `mstatus`, `mepc`, `medeleg`, `mideleg`, `mie`. Enables timer interrupts. Jumps to `main()` in S-mode via `mret`.
- **`kernel/main.c`**: Orchestrates all subsystem initialization (kinit, kvminit, kvminithart, procinit, trapinit, trapinithart, plicinit, plicinithart, consoleinit, uart, disk, net, gpu, input, rtc). Starts `userinit()` then `scheduler()`.

### 1.2 Memory Layout (`kernel/memlayout.h`)

All physical addresses are fixed QEMU mappings:
- `0x10000000` — UART0
- `0x10001000` — VirtIO disk (VIRTIO0, IRQ 1)
- `0x10005000` — VirtIO Net (VIRTIO1, IRQ 5)
- `0x100000` — QEMU Test Device (Shutdown/Reboot)
- `0x101000` — Goldfish RTC (real-time clock)
- `0x0c000000` — PLIC (interrupt controller)
- `0x80000000` — Kernel base (KERNBASE), 128MB RAM up to PHYSTOP
- `TRAMPOLINE` = `MAXVA - PGSIZE` — top of VM, both user and kernel
- `TRAPFRAME` = `TRAMPOLINE - PGSIZE` — per-process trap frame

### 1.3 Virtual Memory (`kernel/vm.c`)

- Sv39 page table: 3-level, 512 entries each.
- `kvminit()`: Sets up the kernel page table mapping all devices+RAM.
- `uvmcreate()`, `uvmalloc()`, `uvmfree()`: User process memory management.
- `walkaddr()`: Walk page table to get physical address.
- Trampoline is **mapped at the same VA in kernel AND user** space — key to the trap mechanism.

### 1.4 Trap Handling — The Full Path

**User trap entry:**
1. **`kernel/trampoline.S` (`uservec`)**: Triggered on any user-mode exception/interrupt. Saves all 32 user registers into `TRAPFRAME`. Loads kernel stack pointer, kernel page table, and jumps to `usertrap()`.
2. **`kernel/trap.c` (`usertrap`)**: Runs in kernel mode. Dispatches based on `scause`:
   - System call (`scause == 8`): calls `syscall()`.
   - Virtio interrupt (`devintr()`).
   - Timer interrupt (`yield()`).
3. **`usertrapret()`**: Prepares to return to user mode. Sets up SSTATUS, SEPC, SATP for the return, then calls `userret` in trampoline.
4. **`kernel/trampoline.S` (`userret`)**: Restores user registers from TRAPFRAME, switches to user page table, executes `sret`.

**Kernel trap:**
- **`kernel/kernelvec.S`**: Saves/restores kernel registers.
- **`kernel/trap.c` (`kerneltrap`)**: Handles device interrupts in kernel mode.

**Critical design**: Both user and kernel page tables map `TRAMPOLINE` at the same VA so `uservec` remains accessible after switching page tables (no page fault on the instruction boundary).

### 1.5 System Calls (`kernel/syscall.c`, `kernel/syscall.h`)

The full current syscall table (50 syscalls):
```
fork(1), exit(2), wait(3), pipe(4), read(5), kill(6), exec(7), fstat(8),
chdir(9), dup(10), getpid(11), sbrk(12), sleep(13), uptime(14), open(15),
write(16), mknod(17), unlink(18), link(19), mkdir(20), close(21),
pwd(22), send(23), recv(24),
shmget(25), shmat(26),   -- Shared Memory
readavail(27),             -- Non-blocking read check
gpu_flush(28), gpu_flush_rect(29),  -- GPU commands
yield(30), shmdt(31),
flush_console(32),
procinfo(33),              -- Process list for GUI process viewer
stat(34),                  -- File stat without open (for fileman)
time(35),                  -- Unix timestamp from RTC
shutdown(36), reboot(37),  -- Power control via QEMU Test Device
kill_child(38),            -- Shell Ctrl+C support
netping(39), netpoll(40),  -- ICMP ping
socket(41), sockbind(42), sendto(43), recvfrom(44), sockclose(45), -- UDP
tcpsocket(46), tcpconnect(47), tcpsend(48), tcprecv(49), tcpclose(50)  -- TCP
```

Key implementation notes:
- `sys_shmget` / `sys_shmat`: Kernel-managed shared memory. Used extensively by Sulu. Fixed a major **aliasing bug** where multiple `shmget` calls with the same key could return the same physical memory mapped with conflicting permissions.
- `sys_stat`: Implemented WITHOUT open/close (reads inode directly) to safely probe `/dev` entries without triggering driver connections.
- `sys_gpu_flush_rect`: Fixed a critical bug — the GPU was not receiving a `VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D` command before the flush, so the new pixel data in guest RAM was never sent to the host. Added `virtio_gpu_transfer()` call before flush.
- `sys_shutdown`/`sys_reboot`: Writes magic values to `VIRT_TEST` (the QEMU test device at `0x100000`), which signals QEMU to exit or reset.
- `sys_kill_child`: Saves the child PID in the parent's `struct proc` and sends SIGKILL. Enables `Ctrl+C` in the terminal app.

### 1.6 Process Management (`kernel/proc.c`, `kernel/proc.h`)

Extended `struct proc` to include:
- `shm_ids[MAX_SHM]`: Tracks all SHM segments attached to the process for auto-cleanup on `exit()`.
- `child_pid`: For `kill_child` syscall.

Fixed **SHM leak bug**: `exit()` did not detach SHM segments. When apps crashed, their SHM entries filled the global `MAX_SHM` (16) limit, causing all subsequent `shmget()` calls to fail with "sulu_init failed". Fixed by iterating `proc->shm_ids` in `exit()` and calling `shmdt`.

### 1.7 File System (`kernel/fs.c`, `kernel/fs.h`)

Standard xv6 file system:
- **Inode-based**, with direct blocks (12), single indirect, double indirect planned but not yet implemented (currently limits files to ~268KB).
- **Log-based transactions**: All writes wrapped in `begin_op()`/`end_op()` for crash recovery. Missing these caused kernel panics in `sys_stat`.
- **`/dev` entries**: Created via `mknod`. `/dev/console`, `/dev/sulu`, `/dev/input` are device files used for character I/O.

### 1.8 Drivers

- **`kernel/uart.c`**: NS16550A UART driver. Used for the serial console. Interrupt-driven.
- **`kernel/console.c`**: Bridges UART and the kernel console. Handles backspace, echoing, line buffering. Added `flush_console` to clear terminal output for GUI mode where serial is noisy.
- **`kernel/virtio_disk.c`**: VirtIO block device. Standard xv6.
- **`kernel/virtio_gpu.c`**: Custom VirtIO GPU driver. Exposes a 1280×800 framebuffer. Manages GPU resources and render targets. Extended with `sys_gpu_flush_rect` for partial-screen updates.
- **`kernel/virtio_input.c`**: Custom VirtIO input driver. Reads keyboard/mouse events from two VirtIO input devices. Fixed critical **`volatile`** bug — shared memory access from the IRQ handler was being optimized away by the compiler.
- **`kernel/virtio_net.c`**: VirtIO network driver. Handles TX/RX ring buffers, DMA. Supports Ethernet frame send/receive.
- **`kernel/sulu_dev.c`**: Custom `char device` at `/dev/sulu`. Client apps write registration commands (type, SHM key, dimensions) to this fd to register windows with the Sulu Window Manager.
- **`kernel/rtc.c`**: Reads the Goldfish RTC device at `0x101000` to get nanosecond timestamps, converted to Unix seconds for `sys_time`.
- **`kernel/net.c`**: Full network stack (Ethernet, ARP, IP, ICMP, UDP, TCP). 
  - ARP caching.
  - ICMP echo reply (respond to pings from host).
  - UDP socket API.
  - TCP: 3-way handshake, data transfer, FIN teardown, retransmit.

---

## PART 2: SULU WINDOW MANAGER

### 2.1 Architecture

Sulu (`user/sulu.c`) is a **zero-syscall** userspace windowing system.
- **Server**: The `sulu` process. Opens `/dev/sulu` and `/dev/input`. Owns the GPU framebuffer.
- **Client**: Any process that calls `sulu_connect()` + `sulu_attach()`.
- **Communication Protocol**: Pure MMIO via Shared Memory (SHM). After handshake, NO syscalls are needed for drawing or input.

### 2.2 Shared Memory Layout (`user/sulu_client.h`)

```c
struct sulu_window_shm {
    int win_id, width, height, x, y;
    int flags;          // SULU_FLAG_DOUBLE_BUFFER
    int cursor_type;
    int front_buf;      // Active buffer for double-buf
    uint bgcolor;
    char title[64];
    char clipboard[2048];
    int clipboard_len;
    struct sulu_ring cmd_ring;
    struct sulu_cmd  cmd_buf[16];  // Client -> Server
    struct sulu_ring event_ring;
    struct sulu_event event_buf[32]; // Server -> Client
    // Pixel buffer follows (ARGB uint32 array)
};
```

### 2.3 Command Types (Client → Server)
- `SULU_CMD_BLIT(x,y,w,h)`: "I drew in this region, composite me."
- `SULU_CMD_RESIZE(w,h)`: Request resize.
- `SULU_CMD_CLOSE`: Close window.
- `SULU_CMD_SWAP`: Double buffer swap.
- `SULU_CMD_CLIP_SET` / `SULU_CMD_CLIP_GET`: Clipboard.

### 2.4 Event Types (Server → Client)
- `SULU_EV_KEY(code, value)`: Keyboard. `value=1` press, `0` release.
- `SULU_EV_MOUSE_BTN(code, value, x, y, rx, ry)`: Mouse click. `rx,ry` = relative to window content area.
- `SULU_EV_MOUSE_MOVE(x, y, rx, ry)`.
- `SULU_EV_MOUSE_WHEEL(value)`: Scroll delta.
- `SULU_EV_MOUSE_DBLCLICK`: Double click (250ms window).
- `SULU_EV_FOCUS`, `SULU_EV_CLOSE`, `SULU_EV_MAXIMIZE`, `SULU_EV_PASTE`.
- `SULU_EV_RESIZE(x=w, y=h)`: Window resized by WM. **New addition**.

### 2.5 Window Manager Features
- **Multi-window**: Manages a linked list of `struct Window`.
- **Z-ordering**: Windows are composited in order. Focus control via `window_raise()`.
- **Title Bar**: Per-window with: [Minimize (yellow)] [Maximize (green)] [Close (red)] buttons + title text.
- **Drag**: Click-drag title bar to move windows.
- **Occlusion Culling**: Skips drawing regions hidden behind higher-z windows. Improved rendering performance.
- **Dirty Rect Compositing**: `composite_region(x,y,w,h)` only redraws the affected region per BLIT command.
- **System Bar**: 30px bar at bottom. Shows clock (HH:MM:SS from RTC), window task list, and Start Menu button.
- **Start Menu**: Popup listing all launchable apps.
- **Minimize**: Hides window. Restore via taskbar click.
- **Maximize**: Expands to full screen above system bar (or fully over it with `FULLSCREEN_OVER_SYS_BAR`).
- **Background**: Procedural deep purple-to-blue gradient wallpaper.
- **Double Buffering**: Clients use `SULU_FLAG_DOUBLE_BUFFER`. Server reads from `front_buf`, client draws to back buffer, then swaps.
- **Clipboard**: Global shared clipboard. `CLIP_SET` from one window is readable via `CLIP_GET` by another.
- **Shadow Effect**: Skewed floor shadow below windows (cosmetic).

### 2.6 Critical Bugs Fixed in Sulu
1. **Blocking Read Deadlock**: Main loop used `read()` on cmd pipe. Switched to `readavail()` (non-blocking).
2. **SHM Aliasing Bug**: `sys_shmget` was returning the same physical pages for different clients when PIDs were reused. Fixed with strict key checking.
3. **`drag_win` Use-After-Free**: Dangling pointer to freed window. Protected with safe guard checks.
4. **Linked List Corruption**: `close_window` / `window_raise` did unsafe pointer manipulation. Refactored with `window_detach()` / `window_reinsert()` helpers.
5. **Titlebar Button Vanishing**: `composite()` was called but only cleared the background (no children rendered). Refactored `composite()` to call `composite_region()` for the full screen.
6. **Static Screen Bug**: `sys_gpu_flush_rect` was sending a flush command without a preceding `transfer` command. GPU was redrawing stale texture. Fixed by calling `virtio_gpu_transfer()` before `virtio_gpu_flush()`.

---

## PART 3: USER-SPACE APPLICATIONS

### 3.1 `user/terminal.c` — GUI Terminal Emulator
- Runs in a Sulu window.
- Forks a child `sh` shell. Writes user keystrokes to shell stdin. Reads shell stdout to display.
- **Scrollback buffer**: `SCROLLBACK_LINES = 1000`. Mouse wheel scrolling.
- **Text Selection**: Click-to-move cursor, drag-to-highlight. Blue highlight.
- **Copy/Paste**: `Ctrl+Shift+C` = copy selection to SHM clipboard. `Ctrl+Shift+V` = paste from clipboard.
- **Shell Interrupt**: `Ctrl+C` calls `kill_child()` syscall.
- **Key Forwarding**: Forwards `KEY_UP`/`DOWN` as ANSI escape codes to shell for history. Forwards `TAB` (no echo) for tab completion.
- **Double Buffering**: Uses `SULU_FLAG_DOUBLE_BUFFER` + `SULU_CMD_SWAP` for flicker-free rendering.

### 3.2 `user/sh.c` — Shell
- Interactive command line with:
  - **Line editing**: Insert chars at cursor position, left/right arrow navigation, backspace (always used through `readline()` in `ulib.c`).
  - **Command History**: 16-entry circular buffer. Up/Down arrows navigate history.
  - **Tab Completion**: Prefix-based completion searching `.` and `/bin`.
  - **Piping, Redirection, Background**: Full POSIX-style parser.
  - **Shebang (`#!`) Support**: Added recently. When executing a file, shell reads first bytes. If `#!interpreter` is found, re-execs the file with the interpreter.
- **`cd`**: Handled by the shell parent (not child), calling `chdir()`.

### 3.3 `user/editor.c` — C Text Editor (GUI)
- Line-buffer data structure (`char lines[MAX_LINES][LINE_LEN]`).
- Full cursor movement (arrows), insert/delete, split/merge lines.
- Load/Save files.
- **Selection**: `Shift+Arrow` highlights text. Blue highlight in render.
- **Clipboard**: `Ctrl+C` copy, `Ctrl+V` paste via Sulu SHM clipboard.
- **Ctrl+S**: Save file.
- **Mouse**: Click to place cursor, drag to select.
- **Save Button**: In status bar. Green = unsaved changes, Grey = saved.
- **Resize-aware**: Syncs width/height with SHM on `SULU_EV_MAXIMIZE`.

> **Note**: The old `editor.c` has a known save-button hit-test bug (click registers as miss). Work in progress to replace with `editor.sul` (SuluScript-based editor).

### 3.4 `user/fileman.c` — File Manager
- Graphical grid/list view of directory contents.
- Icons: Folder (yellow), file (grey), device (purple DIP chip).
- Double-click: Navigate into dirs, launch files.
- **Launch logic** (extension-based dispatch):
  - ELF magic (`\x7fELF`): Execute directly.
  - `.bmp`: Open in `viewer`.
  - `.sul`: Open with `sulula editor.sul <path>`.
  - Other: Open in `editor`.
- `sys_stat` used for probing device files safely (no open/close).
- **Scrolling**: Mouse wheel supported.
- **View toggle**: Grid ↔ List.

### 3.5 `user/procs.c` — Graphical Process Viewer
- Uses `procinfo()` syscall to get process list.
- Renders PID, name, state, memory in a Sulu window.

### 3.6 `user/bouncing_ball.c`
- Demo/screensaver. Bouncing ball with physics.
- Uses **smart erase optimization** (only erases the ball's previous position, not clear full screen).
- Uses dirty-rect BLIT.

### 3.7 `user/snake.c` — C Snake Game (Native/Legacy)
- Classic snake game built as a native C Sulu client.
- (NOTE: This is distinct from `snake.sul` which is the SuluScript version.)

### 3.8 `user/minesweeper.c` — Minesweeper
- Classic minesweeper. Left click = reveal. Right click = flag.
- Face button to reset game.

### 3.9 `user/viewer.c` — Image Viewer
- BMP image loader and viewer. Integrated into File Manager.

### 3.10 `user/browser.c` — Web Browser
- TCP/HTTP GET fetcher.
- Basic HTML parser: handles `<h1>`, `<p>`, `<br>`, `<title>`, `<body>`.
- Word-wrapping text renderer.
- Local file loading support.
- Scrolling.

### 3.11 Networking User Programs
- **`user/ping.c`**: ICMP echo ping using `netping`/`netpoll` syscalls.
- **`user/nc.c`**: UDP netcat. `sendto`/`recvfrom`.
- **`user/nslookup.c`**: DNS A-record lookup via UDP.
- **`user/wget.c`**: HTTP GET via TCP. Downloads file to disk or stdout.

### 3.12 `user/shutdown.c`
- `shutdown` → Powers off QEMU.
- `shutdown -r` → Reboots QEMU.

---

## PART 4: SULUSCRIPT — THE SCRIPTING LANGUAGE

### 4.1 Overview
**SuluScript** (`.sul` files) is a custom interpreted language built entirely from scratch for rapid GUI development in xv6. It replaces the need to write C code for simple apps and games.
- **Interpreter**: `user/sulu_interpreter.c` (the `sulula` binary).
- **Lexer**: `user/suluscript_lexer.c`
- **Parser**: `user/suluscript_parser.c`
- **Header**: `user/suluscript.h` (AST node types, token types)

### 4.2 Language Syntax

```javascript
#!/bin/sulula        // Shebang for direct execution
window {
    title: "My App",
    width: 400, height: 400,
    bg: 0xFF111111
}

var x = 10;
var name = "";
var arr = 0;    // declared as int, used as array via push()

fn update() {
    x = x + 1;
    if(x > 100) { x = 0; }
    for(var i = 0; i < len(arr); i = i + 1) {
        // ...
    }
}

fn on_key() { }

layout(onFrame: update, onKeyDown: on_key, onKeyUp: on_keyup, onResize: handle_resize) {
    rect(x: 10, y: 10, width: 50, height: 50, color: 0xFF00FF00);
    text(content: "Hello", x: 10, y: 70, color: 0xFFFFFFFF);
    text(content: arr, x: 10, y: 90, color: 0xFFCCCCCC);  // renders array as chars
    button(label: "Click Me", x: 10, y: 200, onClick: do_thing);
    for(i = 0; i < len(arr); i = i + 1) {
        rect(x: at(arr, i), y: 0, width: 10, height: 10, color: 0xFFFF0000);
    }
}
```

### 4.3 Native Functions (Built into Interpreter)

| Function | Description |
|---|---|
| `rand()` | PRNG random integer |
| `srand(seed)` | Seed the PRNG (seeded with `uptime()` on startup) |
| `usleep(usec)` | Pause execution for N microseconds (more precise than ms) |
| `resize(w, h)` | Resize the Sulu window |
| `push(arr, val)` | Append integer to dynamic array (4096-int buffer) |
| `pop(arr)` | Remove last element from array (used for backspace) |
| `at(arr, idx)` | Read element at index |
| `update_at(arr, idx, val)` | Write element at index |
| `len(arr)` | Array length |
| `clear_arr(arr)` | Reset array to empty |
| `print(a, b, ...)` | Log values to serial console |
| `read_file(path, arr)` | Load file bytes into array (for editor) |
| `write_file(path, arr)` | Write array bytes back to file (saving) |
| `to_char(scancode, shift)` | Convert raw scancode to ASCII character |
| `insert_at(arr, idx, val)` | Insert `val` at `idx`, shifting right (cursor-based editing) |
| `delete_at(arr, idx)` | Delete element at `idx`, shifting left |
| `cursor_line(arr, cursor_idx)` | Returns 0-based line number of cursor |
| `cursor_col(arr, cursor_idx)` | Returns 0-based column of cursor |
| `line_start(arr, line_num)` | Returns array index of first char on line N |
| `line_count(arr)` | Returns total number of lines in array |
| `xy_to_cursor(arr, rx, ry, tb_x, tb_y, cw, ch, tb_w, scroll)` | Click coords → cursor index for a textbox |

### 4.4 Built-in Global Variables

| Variable | Description |
|---|---|
| `key` | Current key scancode from event |
| `key_val` | `1` = pressed, `0` = released |
| `windowW` | Current window width in pixels |
| `windowH` | Current window height in pixels |
| `arg1` | First command-line argument (e.g., filename) |
| `arg2` | Second command-line argument |
| `mouse_rx` | Window-relative X of last mouse click (set before `onMouseDown`) |
| `mouse_ry` | Window-relative Y of last mouse click (set before `onMouseDown`) |

### 4.5 Layout Callbacks
| Prop | Description |
|---|---|
| `onFrame` | Called every frame (~60 FPS) |
| `onKeyDown` / `onKeyUp` | Called on key events; `key`/`key_val` are set |
| `onResize` | Called when WM resizes window; `windowW`/`windowH` updated |
| `onMouseDown` | Called on left mouse button press; `mouse_rx`/`mouse_ry` are set |

### 4.6 UI Elements
| Element | Description |
|---|---|
| `rect(x, y, width, height, color)` | Solid rectangle |
| `text(content, x, y, color)` | Text label. `content` can be a string or char array variable |
| `button(label, onClick, x, y)` | Clickable button |
| `vbox / hbox` | Layout containers |
| `textbox(content, cursor, x, y, width, height, color, bg, scroll)` | Multi-line editable text area with cursor rendering |

### 4.7 AST Node Types
`NODE_WINDOW_DEF`, `NODE_LAYOUT_DEF`, `NODE_FN_DEF`, `NODE_VAR_DECL`, `NODE_ASSIGN`, `NODE_IF`, `NODE_FOR`, `NODE_RETURN`, `NODE_CALL`, `NODE_BLOCK`, `NODE_RECT`, `NODE_TEXTBOX`, `NODE_TEXT`, `NODE_BUTTON`, `NODE_VBOX`, `NODE_HBOX`, `NODE_LIT_INT`, `NODE_LIT_STR`, `NODE_IDENT`, `NODE_BINOP`, `NODE_ARRAY_REF`

### 4.6 Known Parser Fixes (Critical History)
- **Operator Precedence**: Rewrote expression parsing to use recursive descent with `parse_binary()` and `get_precedence()`. Without this, `(rand() % 19) * 20` was evaluating incorrectly.
- **Parenthesized Expressions**: `parse_primary()` handles `(expr)` correctly.
- **Layout Children**: Fixed `parse_layout()` to handle semicolons between child elements (was skipping everything after first element).
- **Array References**: `TOKEN_LBRACKET` handling added to `parse_primary()`.
- **Missing Tokens**: Added `TOKEN_STAR`, `TOKEN_SLASH`, `TOKEN_BANG`, `TOKEN_BANG_EQUAL`, `TOKEN_LBRACKET`, `TOKEN_RBRACKET` to `suluscript.h`.

### 4.7 SuluScript Applications

**`user/snake.sul`** — Snake game in SuluScript:
- Self-collision, wall collision, food spawning.
- Food uses `rand()` seeded with `uptime()` for unique positions.
- Bounds check uses `windowW`/`windowH` so the game expands when window is resized.
- Speed controlled by tick counter (`tick < 10` = skip frame = ~6-8 moves/sec at 60fps).
- `onResize: handle_resize` registered in layout.
- Shebang: `#!/bin/sulula` so you can run `snake.sul` directly.

**`user/editor.sul`** — Text editor in SuluScript (WIP):
- Reads a file into a character array using `read_file(arg1, content)`.
- Keyboard input: `to_char(key, shift)` converts scancodes to ASCII.
- Backspace: `pop(content)`.
- Enter: `push(content, 10)` (newline).
- Save: `write_file(filename, content)` via clickable SAVE button.
- Displays char array using the enhanced `text(content: arr)` element (renders array as characters with line-wrapping on `\n`).

---

## PART 5: SHELL INTEGRATION

### 5.1 Shebang (`#!`) Support in `sh.c`
Modified `runcmd()` EXEC case to:
1. Open the resolved binary path.
2. Read first 128 bytes.
3. Check for `#!` at offset 0.
4. If found: parse interpreter path (up to first whitespace/newline), rebuild `argv[]` with interpreter as `argv[0]` and script path as `argv[1]`, then `exec(interpreter, new_argv)`.

This means `snake.sul` can be run as: `snake.sul` (if in PATH) or `./snake.sul`.

### 5.2 File Manager `.sul` Dispatch
In `fileman.c` `launch()`: if filename ends with `.sul`, executes `sulula editor.sul <path>` via `exec`.

---

## PART 6: BUILD SYSTEM

### 6.1 Makefile Highlights
- QEMU target: `make qemu-gui` launches with VirtIO GPU, keyboard, tablet, serial stdio.
- GPU driver is compiled in via `kernel/virtio_gpu.c`.
- `sulula` binary links: `sulu_interpreter.o` + `suluscript_lexer.o` + `suluscript_parser.o` + ulib.
- `fs.img` target includes all `.sul` script files explicitly: `test.sul ball.sul key_test.sul arrays.sul snake.sul editor.sul`.
- All userspace programs go in `UPROGS`.

---

## PART 7: CODING PRINCIPLES & USER PREFERENCES

1. **Zero-Syscall Graphics**: All rendering happens through SHM pixel buffers. Never `write()` pixels through the kernel.
2. **Self-Hosting**: Push logic into userspace and SuluScript rather than the kernel where possible.
3. **Native Event Integration**: Instead of simulated in-app buttons for things like resize/expand, hook into actual Sulu WM events (`SULU_EV_RESIZE`, title bar buttons).
4. **Precision over convenience**: Prefer `usleep(usec)` over `sleep(ms)` for timing control.
5. **Document as you go**: Keep `SULUSCRIPT.md` and the artifact docs updated whenever a new native function or feature is added.
6. **No debug noise in release**: Remove or comment out excessive `printf` statements from the interpreter's hot paths (`onFrame`, `draw_node`).
7. **Test early with visible feedback**: Add `print()` calls in SuluScript when debugging is needed, rather than rebuilding the entire C interpreter.

---

## PART 8: CURRENT STATE & KNOWN ISSUES (as of 2026-03-07)

### Working
- Full GUI desktop with wallpaper, taskbar, clock, start menu.
- Terminal, File Manager, Editor (C), Image Viewer, Browser, Process Viewer, Minesweeper.
- Snake game in SuluScript fully working (self-collision, random food, resize-aware).
- Networking: ping, DNS, wget, netcat, TCP.
- Shutdown/Reboot commands.
- Shebang support in shell for `.sul` files.
- SuluScript interpreter with file I/O native functions.

### In Progress / Broken
- `make fs.img` was failing at last session — exact error unknown, needs diagnosis.
- `editor.sul` was written but not yet tested (might have parser issues with string variable handling for `arg1`).
- The `text(content: <variable>)` element was enhanced to handle char arrays, but `val_ident` field may not be set by the parser on props — needs verification.
- `pop_internal()` may have regression from duplicate function declaration during editing session.
- Large SuluScript arrays: `push_internal` allocates fixed 4096-int buffer — realloc-based growth not yet implemented.

### Next Steps
1. Fix `make fs.img` build error (diagnose compiler/linker error from recent interpreter changes).
2. Test `editor.sul` with `sulula editor.sul snake.sul`.
3. Verify `text(content: arr)` renders the char array correctly.
4. Fix the `val_ident` vs `val_expr` distinction for the `content` property in the text node parser.
