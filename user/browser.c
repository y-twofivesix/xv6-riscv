#include "kernel/types.h"
#include "user/user.h"
#include "user/sulu_client.h"
#include "user/font.h"

// Networking constants
#define SOCK_DGRAM 2
#define HTTP_PORT 80
#define RECV_BUF_SIZE 1024
#define MAX_HTML_SIZE 65536

// Browser State
struct sulu_window win;
char *html_buffer;
int html_len = 0;
int scroll_y = 0;
int content_height = 0;
char page_title[64] = "Browser";

// Colors
#define BG_COLOR 0xFFFFFFFF
#define TEXT_COLOR 0xFF000000
#define H1_COLOR 0xFF000088

// --- Helper String Functions ---

// --- Networking Helper Functions ---

uint16 my_htons(uint16 x) {
    return ((x >> 8) & 0xFF) | ((x & 0xFF) << 8);
}

uint16 my_ntohs(uint16 x) {
    return my_htons(x);
}

// DNS structures
struct dns_header {
    uint16 id;
    uint16 flags;
    uint16 qdcount;
    uint16 ancount;
    uint16 nscount;
    uint16 arcount;
};

int build_dns_query(char *hostname, uint8 *buf) {
    struct dns_header *hdr = (struct dns_header*)buf;
    hdr->id = my_htons(0x1234);
    hdr->flags = my_htons(0x0100);
    hdr->qdcount = my_htons(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    
    uint8 *p = buf + sizeof(struct dns_header);
    char *src = hostname;
    while(*src) {
        char *dot = src;
        while(*dot && *dot != '.') dot++;
        int len = dot - src;
        *p++ = len;
        while(src < dot) *p++ = *src++;
        if(*src == '.') src++;
    }
    *p++ = 0;
    *p++ = 0; *p++ = 1;  // Type A
    *p++ = 0; *p++ = 1;  // Class IN
    return p - buf;
}

int parse_dns_response(uint8 *buf, int len, uint8 *ip_out) {
    if(len < sizeof(struct dns_header)) return -1;
    struct dns_header *hdr = (struct dns_header*)buf;
    if((my_ntohs(hdr->flags) & 0x8000) == 0) return -1;
    if((my_ntohs(hdr->ancount)) == 0) return -1;
    
    uint8 *p = buf + sizeof(struct dns_header);
    while(*p != 0) {
        if((*p & 0xC0) == 0xC0) { p += 2; break; }
        else p += *p + 1;
    }
    if(*p == 0) p++;
    p += 4;
    if((*p & 0xC0) == 0xC0) p += 2;
    else { while(*p != 0) p += *p + 1; p++; }
    
    uint16 type = (p[0] << 8) | p[1]; p += 2;
    p += 2;  // CLASS
    p += 4;  // TTL
    uint16 rdlen = (p[0] << 8) | p[1]; p += 2;
    
    if(type == 1 && rdlen == 4) {
        memmove(ip_out, p, 4);
        return 0;
    }
    return -1;
}

int dns_lookup(char *hostname, uint8 *ip) {
    uint8 dns_server[4] = {10, 0, 2, 3};
    int fd = socket(SOCK_DGRAM);
    if(fd < 0) return -1;
    sockbind(fd, 12347); // Helper port
    
    uint8 query[512];
    int qlen = build_dns_query(hostname, query);
    sendto(fd, query, qlen, dns_server, 53);
    
    uint8 response[512];
    for(int i = 0; i < 50; i++) {
        netpoll();
        int n = recvfrom(fd, response, sizeof(response), 0, 0);
        if(n > 0) {
            sockclose(fd);
            return parse_dns_response(response, n, ip);
        }
        sleep(5);
    }
    sockclose(fd);
    return -1;
}

// Draw a single character
void draw_char_clip(int x, int y, char c, uint color) {
    if(y + 8 < 0 || y >= 600) return; // Simple clipping
    
    int idx = c - 32;
    if(idx < 0 || idx >= 96) idx = 0; // Invalid char -> Space
    
    uint *pixels = win.pixels;
    int w = win.width;
    
    for(int row = 0; row < 8; row++) {
        int py = y + row;
        if(py < 0 || py >= 600) continue;
        
        uint8 bits = font_8x8[idx][row];
        for(int col = 0; col < 8; col++) {
            if((bits >> (7 - col)) & 1) {
                int px = x + col;
                if(px >= 0 && px < w) {
                    pixels[py * w + px] = color;
                }
            }
        }
    }
}

// Render HTML content to window
void render_page() {
    // Clear screen
    int w = win.width;
    int h = win.height;
    for(int i = 0; i < w * h; i++) win.pixels[i] = BG_COLOR;
    
    int x = 10;
    int y = 10 - scroll_y;
    int max_width = w - 20;
    
    // Parser State
    int in_tag = 0;
    int is_h1 = 0;
    int is_title = 0;
    
    char *p = html_buffer;
    
    // Skip headers only if it looks like HTTP response
    if(strncmp(p, "HTTP/", 5) == 0) {
        char *body_start = strstr(p, "\r\n\r\n");
        if(!body_start) body_start = strstr(p, "\n\n");
        if(body_start) p = body_start + 4;
    }
    
    printf("browser: rendering loop start, p=%p\n", p);
    
    while(p && *p) {
        if(*p == '<') {
            in_tag = 1;
            
            // Check specific tags
            if(strncmp(p, "<h1", 3) == 0) { is_h1 = 1; y += 12; x = 10; }
            if(strncmp(p, "</h1", 4) == 0) { is_h1 = 0; y += 16; x = 10; }
            if(strncmp(p, "<p", 2) == 0) { y += 6; x = 10; }
            if(strncmp(p, "</p", 3) == 0) { y += 12; x = 10; }
            if(strncmp(p, "<title", 6) == 0) is_title = 1;
            if(strncmp(p, "</title", 7) == 0) is_title = 0;
            
            p++;
            continue;
        }
        
        if(*p == '>') {
            in_tag = 0;
            p++;
            continue;
        }
        
        if(!in_tag && *p != '\r' && *p != '\n') {
            if(is_title) {
                // Capture title - simplistic
                int tlen = strlen(page_title);
                if(tlen < 63 && strncmp(page_title, "Browser", 7) != 0) {
                     // already set?
                } else if (strncmp(page_title, "Browser", 7) == 0) {
                    // Start clearing it
                    page_title[0] = 0;
                }
                
                int len = strlen(page_title);
                if(len < 60) {
                    page_title[len] = *p;
                    page_title[len+1] = 0;
                }
            } else {
                // Render text
                uint color = is_h1 ? H1_COLOR : TEXT_COLOR;
                
                // Scale H1? Just duplicate pixels? Or just use same font + bold logic?
                // For now, same font.
                
                // Wrap
                if(x + 9 > max_width) {
                    x = 10;
                    y += 10;
                }
                
                draw_char_clip(x, y, *p, color);
                x += 9;
                
                // If bold/h1, draw again slightly offset?
                if(is_h1) {
                    draw_char_clip(x-8, y, *p, color); // Embolden
                }
            }
        }
        p++;
    }
    
    content_height = y + scroll_y + 50;
    
    // Render scrollbar handle if needed
    if(content_height > h) {
        int bar_h = (h * h) / content_height;
        if(bar_h < 20) bar_h = 20;
        int bar_y = (scroll_y * h) / content_height;
        
        // Draw scrollbar on right
        for(int by = bar_y; by < bar_y + bar_h; by++) {
            for(int bx = w - 8; bx < w - 2; bx++) {
                if(by >= 0 && by < h) win.pixels[by*w + bx] = 0xFF888888;
            }
        }
    }
    
    sulu_blit(win.shm, 0, 0, 800, 600);
}

void fetch_url(char *hostname) {
    html_buffer = malloc(MAX_HTML_SIZE);
    if(!html_buffer) {
        printf("browser: malloc failed\n");
        return;
    }
    memset(html_buffer, 0, MAX_HTML_SIZE);
    
    // Trigger ARP resolution
    netping();
    for(int i = 0; i < 10; i++) {
        netpoll();
        sleep(5);
    }

    // DNS
    uint8 ip[4];
    sulu_set_title(win.shm, "Resolving...");
    printf("browser: resolving %s...\n", hostname);
    
    if(dns_lookup(hostname, ip) < 0) {
        printf("browser: dns failed\n");
        strcpy(html_buffer, "<h1>Error</h1><p>DNS Lookup Failed</p>");
        return;
    }
    printf("browser: resolved to %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
    
    // TCP Connect
    int fd = tcpsocket();
    if(fd < 0) return;
    
    sulu_set_title(win.shm, "Connecting...");
    if(tcpconnect(fd, ip, 80) < 0) {
        printf("browser: connect failed\n");
        strcpy(html_buffer, "<h1>Error</h1><p>Connection Failed</p>");
        tcpclose(fd);
        return;
    }
    printf("browser: connected (async), waiting for handshake...\n");
    sulu_set_title(win.shm, "Handshaking...");
    
    // Wait for connection to establish (naive)
    for(int i = 0; i < 50; i++) {
        netpoll();
        sleep(5);
    }
    
    sulu_set_title(win.shm, "Sending Request...");
    printf("browser: sending http request...\n");
    
    // HTTP Request
    char req[512];
    strcpy(req, "GET / HTTP/1.0\r\nHost: ");
    int hlen = strlen(hostname);
    memmove(req + strlen(req), hostname, hlen);
    strcpy(req + strlen(req), "\r\nConnection: close\r\n\r\n");
    
    if(tcpsend(fd, req, strlen(req)) < 0) {
        printf("browser: send failed\n");
    } else {
        printf("browser: request sent\n");
    }
    
    sulu_set_title(win.shm, "Downloading...");
    
    // Receive Loop
    int total = 0;
    int idle_count = 0;
    char buf[RECV_BUF_SIZE];
    
    while(total < MAX_HTML_SIZE - 1024) {
        netpoll();
        int n = tcprecv(fd, buf, RECV_BUF_SIZE);
        if(n > 0) {
            memmove(html_buffer + total, buf, n);
            total += n;
            idle_count = 0;
            printf("."); // Progressive dot
        } else {
             idle_count++;
        }
        
        // Stop if we see ending html tag
        if(strstr(html_buffer, "</html>")) break;
        
        // Stop if idle for too long after getting data
        if(total > 0 && idle_count > 20) {
            printf("\nbrowser: timeout/eof assumed\n");
            break;
        }
        
        sleep(2);
    }
    
    html_len = total;
    tcpclose(fd);
    
    // Update Title
    sulu_set_title(win.shm, page_title);
}

// Load local file
void load_file(char *path) {
    int fd = open(path, O_RDONLY);
    if(fd < 0) return;
    
    html_buffer = malloc(MAX_HTML_SIZE);
    if(!html_buffer) {
        printf("browser: malloc failed\n");
        close(fd);
        return;
    }
    memset(html_buffer, 0, MAX_HTML_SIZE);
    
    int n = read(fd, html_buffer, MAX_HTML_SIZE - 1);
    if(n < 0) n = 0;
    html_buffer[n] = 0;
    html_len = n;
    
    close(fd);
    printf("browser: loaded file %s (%d bytes)\n", path, n);
    if(n > 0) {
        printf("browser: content preview: head '%.10s' ... tail '%.10s'\n", html_buffer, html_buffer + (n > 10 ? n - 10 : 0));
    }
}

int main(int argc, char *argv[]) {
    char *url = "example.com";
    if(argc > 1) url = argv[1];
    
    if(sulu_init(&win, 800, 600, BG_COLOR, 0) < 0) {
        printf("browser: sulu_init failed\n");
        exit(1);
    }
    
    sulu_set_title(win.shm, "Browser");
    
    // Render "Loading"
    draw_char_clip(10, 10, 'L', 0xFF000000);
    sulu_blit(win.shm, 0, 0, 800, 600);
    
    // Try opening as file first
    int fd = open(url, O_RDONLY);
    if(fd >= 0) {
        close(fd);
        load_file(url);
    } else {
        // Assume URL
        fetch_url(url);
    }
    
    // Initial Render
    render_page();
    sulu_set_title(win.shm, page_title);
    
    while(1) {
        if(sulu_event_available(win.shm)) {
            struct sulu_event ev;
            sulu_event_pop(win.shm, &ev);
            
            if(ev.type == SULU_EV_CLOSE) break;
            
            if(ev.type == SULU_EV_MOUSE_WHEEL) {
                scroll_y -= ev.value * 20;
                if(scroll_y < 0) scroll_y = 0;
                if(scroll_y > content_height - 600) scroll_y = content_height - 600;
                if(scroll_y < 0) scroll_y = 0;
                render_page();
            }
            if(ev.type == SULU_EV_KEY && ev.value == 1) { // Key Press
                 if(ev.code == 108) { // Down
                     scroll_y += 20;
                     render_page();
                 }
                 if(ev.code == 103) { // Up
                     scroll_y -= 20;
                     if(scroll_y < 0) scroll_y = 0;
                     render_page();
                 }
            }
        }
        sleep(5);
    }
    
    exit(0);
}
