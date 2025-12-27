# Sulu Window System Documentation

Sulu is a high-performance, **zero-syscall** windowing system for xv6-riscv, designed around **Shared Memory (SHM)** and lock-free ring buffers.

## 1. Architecture

Sulu decouples the Window Manager (Server) from applications (Clients) using a Shared Memory region per window.

*   **Server (`user/sulu`)**: Manages the framebuffer, input devices, and window composition.
*   **Client (`user/editor`, `user/terminal`)**: Writes directly to its window's pixel buffer and communicates via ring buffers.
*   **Kernel**: Only involved in setup (`shmget`, `shmat`) and initial connection (`/dev/sulu`). After setup, communication is purely userspace-to-userspace memory access.

### Shared Memory Layout (`struct sulu_window_shm`)
The SHM region is the heart of the protocol. It contains:

1.  **Header**: Metadata (width, height, window ID).
2.  **Cmd Ring**: Circular buffer for commands sent **Client -> Server** (e.g., "I drew something", "Resize me").
3.  **Event Ring**: Circular buffer for events sent **Server -> Client** (e.g., "Key pressed", "Mouse moved").
4.  **Pixel Buffer**: Raw `uint32` array for ARGB pixels, located immediately after the header.

## 2. Client API (`sulu_client.h`)

### Connection Lifecycle
1.  **`sulu_connect(key, w, h)`**:
    *   Opens `/dev/sulu`.
    *   Sends a registration command.
    *   Returns a file descriptor (used for lifecycle tracking).
2.  **`sulu_attach(key, w, h, ...)`**:
    *   Calls `shmget` and `shmat` to map the shared region into the client's address space.
3.  **Loop**:
    *   Draw to pixels.
    *   Process events from `shm->event_ring`.
    *   Push commands to `shm->cmd_ring`.

### Graphics
*   **Direct Access**: `uint *pixels = sulu_pixels(shm);`
*   **Drawing**: Simple helper functions exist (`sulu_fill_rect`, `sulu_draw_char`, etc.), or clients can write raw pixels.
*   **Blitting**: After drawing, the client **must** notify Sulu to redraw the dirty region:
    ```c
    sulu_blit_rect(win, x, y, w, h); // Pushes SULU_CMD_BLIT
    ```

## 3. Event System

Events are pushed by Sulu into the client's `event_ring`. The client consumes them using `sulu_event_pop()`.

### `struct sulu_event`
| Field | Description |
| :--- | :--- |
| `type` | `SULU_EV_KEY`, `SULU_EV_MOUSE_BTN`, `SULU_EV_MOUSE_MOVE`, `SULU_EV_PASTE`, `SULU_EV_CLOSE` |
| `code` | Scancode (for Key) or Button ID (`BTN_LEFT`, `BTN_RIGHT`) |
| `value` | `1` (Press/Down), `0` (Release/Up), or scroll delta |
| `x, y` | Absolute **Screen** Coordinates |
| `rx, ry` | **Relative** Window Coordinates (Client-space) `[New]` |

### Mouse Handling
*   **Clicks**: `SULU_EV_MOUSE_BTN`. `value=1` is Mouse Down.
*   **Movement**: `SULU_EV_MOUSE_MOVE`.
*   **Coordinates**: Use `rx` and `ry` for interactions relative to your window (e.g., UI buttons). `ry` includes the title bar offset, so content often starts at `ry - TITLE_BAR_HEIGHT`.

### Keyboard
*   **Keys**: `SULU_EV_KEY`.
*   **Scancodes**: Raw PC-AT Set 1 scancodes. Clients must implement their own mapping (or use `scancode_map` in `editor.c`).
*   **Modifiers**: Sulu tracks Shift/Ctrl internally but sends raw key events. Clients should track modifier state if needed.

## 4. Clipboard & Commands

### Copy / Paste
Sulu provides a global shared clipboard mechanism.
*   **Protocol**:
    *   **Data**: `shm->clipboard` (2KB buffer in SHM header).
    *   **Copy**: Write data to `shm->clipboard`, then push `SULU_CMD_CLIP_SET`.
    *   **Paste Request**: Push `SULU_CMD_CLIP_GET`.
    *   **Paste Event**: Receive `SULU_EV_PASTE` when data is ready in `shm->clipboard`.

### Commands (`struct sulu_cmd`)
| Type | Description |
| :--- | :--- |
| `SULU_CMD_BLIT` | "I updated my pixels, please composite me." |
| `SULU_CMD_RESIZE` | Request window resize. |
| `SULU_CMD_CLOSE` | Close the window programmatically. |
| `SULU_CMD_CLIP_SET` | Publish local clipboard buffer to global clipboard. |
| `SULU_CMD_CLIP_GET` | Request global clipboard contents. |
