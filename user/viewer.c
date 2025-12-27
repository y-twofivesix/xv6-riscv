#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "user/sulu_client.h"

#define MAX_PATH 512

// BMP Header Structs (Packed)
#pragma pack(push, 1)
struct BMPHeader {
    ushort type;             // Magic identifier: "BM" (0x4D42)
    uint size;               // File size in bytes
    ushort reserved1;        // Reserved
    ushort reserved2;        // Reserved
    uint offset;             // Offset to image data in bytes
};

struct BMPInfoHeader {
    uint size;               // Header size in bytes
    int width;
    int height;
    ushort planes;           // Number of color planes
    ushort bit_count;        // Bits per pixel
    uint compression;        // Compression type
    uint size_image;         // Image size in bytes
    int x_pels_per_meter;
    int y_pels_per_meter;
    uint clr_used;           // Number of colors
    uint clr_important;      // Important colors
};
#pragma pack(pop)

struct sulu_window win;

void render_bmp(char *filename) {
    int fd = open(filename, O_RDONLY);
    if(fd < 0) {
        printf("viewer: cannot open %s\n", filename);
        return;
    }

    struct BMPHeader header;
    struct BMPInfoHeader info;

    if(read(fd, &header, sizeof(header)) != sizeof(header)) {
        printf("viewer: invalid bmp header\n");
        close(fd);
        return;
    }

    if(header.type != 0x4D42) { // 'BM'
        printf("viewer: not a bmp file (magic %x)\n", header.type);
        close(fd);
        return;
    }

    if(read(fd, &info, sizeof(info)) != sizeof(info)) {
        printf("viewer: invalid bmp info header\n");
        close(fd);
        return;
    }

    printf("viewer: image %dx%d bpp=%d\n", info.width, info.height, info.bit_count);

    if(info.bit_count != 24 && info.bit_count != 32) {
        printf("viewer: only 24/32-bit bmp supported\n");
        close(fd);
        return;
    }

    if(info.compression != 0) {
        printf("viewer: compressed bmp not supported\n");
        close(fd);
        return;
    }

    // Init Sulu Window (with padding for resize)
    int w = info.width;
    int h = (info.height > 0) ? info.height : -info.height; // Handle top-down memory
    
    // Check reasonable size
    if(w > 2000 || h > 2000) {
         printf("viewer: image too large\n");
         close(fd);
         return;
    }
    
    // Use semi-transparent background while loading
    if(sulu_init(&win, w, h, 0xFF333333, 0) < 0) {
        printf("viewer: sulu_init failed\n");
        close(fd);
        return;
    }
    sulu_set_title(win.shm, filename);

    // Seek/Skip
    int current_pos = sizeof(header) + sizeof(info);
    int skip = header.offset - current_pos;
    if(skip > 0) {
        char dump[128];
        while(skip > 0) {
            int n = (skip > 128) ? 128 : skip;
            read(fd, dump, n);
            skip -= n;
        }
    }

    // Allocate row buffer
    int bytes_per_pixel = info.bit_count / 8;
    int row_stride = (w * bytes_per_pixel + 3) & ~3; // 4-byte aligned
    uchar *row_buf = malloc(row_stride);
    if(!row_buf) {
        printf("viewer: malloc failed\n");
        close(fd);
        return;
    }

    // BMP is stored Bottom-Up usually (Height > 0)
    // If Height < 0, it is Top-Down.
    int bottom_up = (info.height > 0);
    
    // Sulu buffer
    uint *pixels = win.pixels; // Correct access
    
    for(int i = 0; i < h; i++) {
        // Read logical row i
        // If bottom-up, row 0 in file is bottom of image (y = h-1)
        
        if(read(fd, row_buf, row_stride) != row_stride) {
             printf("viewer: read error at row %d\n", i);
             break;
        }
        
        int target_y = bottom_up ? (h - 1 - i) : i;
        uint *dest_row = &pixels[target_y * w];
        
        for(int x = 0; x < w; x++) {
            uchar *src = &row_buf[x * bytes_per_pixel];
            uint color = 0;
            // BMP is BGR(A)
            uchar b = src[0];
            uchar g = src[1];
            uchar r = src[2];
            uchar a = (bytes_per_pixel == 4) ? src[3] : 0xFF;
            
            // Sulu expects ARGB (0xAARRGGBB)
            color = ((uint)a << 24) | ((uint)r << 16) | ((uint)g << 8) | b;
            dest_row[x] = color;
        }
    }
    
    free(row_buf);
    close(fd);
    
    // Commit to screen
    sulu_blit(win.shm, 0, 0, w, h);
}

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: viewer <filename.bmp>\n");
        exit(1);
    }
    
    render_bmp(argv[1]);

    // Event Loop
    while(1) {
        if(sulu_event_available(win.shm)) {
            struct sulu_event ev;
            sulu_event_pop(win.shm, &ev);
            
            if(ev.type == SULU_EV_CLOSE) {
                break;
            }
            // Maximize handling?
            // If maximized, we might want to center the image or scale it?
            // For now, just let it do default (white space or stretch?)
            // Editor handles maximize by resizing buffer.
            // Viewer creates fixed buffer. 
            // If maximized, shm size changes?
            // Yes, sulu updates shm.
            // We should redraw/recenter image if resized.
            // But we don't keep the source pixels in separate memory, only in SHM.
            // If SHM is reallocated, we lose the image!
            // Wait. Sulu `resize` event detaches old SHM and attaches new one.
            // We need to reload the image or keep a local copy.
            // To be robust, we should keep a local copy? Or just prevent resize?
            // For MVP, just don't handle resize specially (might clear image).
            // Actually, `spawn_client_window` re-uses buffer? No, it allocates new SHM.
            // Client must redraw.
            // Since we closed the FD, we can't easily redraw without reopening.
            // Let's just exit on close for now.
        }
        sleep(5);
    }
    
    exit(0);
}
