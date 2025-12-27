
// Draw a line using Bresenham's algorithm
static inline void sulu_draw_line(struct sulu_window_shm *shm, int x0, int y0, int x1, int y1, uint color) {
    uint *pixels = sulu_pixels(shm);
    int stride = shm->width;
    int win_w = shm->width;
    int win_h = shm->height;

    int dx = (x1 - x0) > 0 ? (x1 - x0) : -(x1 - x0);
    int dy = (y1 - y0) > 0 ? (y1 - y0) : -(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;
    int e2;

    while (1) {
        if (x0 >= 0 && x0 < win_w && y0 >= 0 && y0 < win_h) {
            pixels[y0 * stride + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

// Draw a single character using the 8x8 kernel font
static inline void sulu_draw_char(struct sulu_window_shm *shm, int x, int y, char c, uint color) {
    uint *pixels = sulu_pixels(shm);
    int stride = shm->width;
    int win_w = shm->width;
    int win_h = shm->height;
    int glyph_idx = c - 32;

    if (glyph_idx < 0 || glyph_idx >= 96) return; // Basic ASCII only

    for(int row = 0; row < 8; row++) {
        for(int col = 0; col < 8; col++) {
            if (font_8x8[glyph_idx][row] & (0x80 >> col)) {
                int px = x + col;
                int py = y + row;
                if(px >= 0 && px < win_w && py >= 0 && py < win_h) {
                    pixels[py * stride + px] = color;
                }
            }
        }
    }
}

// Draw a string
static inline void sulu_draw_string(struct sulu_window_shm *shm, int x, int y, const char *str, uint color) {
    while(*str) {
        sulu_draw_char(shm, x, y, *str, color);
        x += 8;
        str++;
    }
}
