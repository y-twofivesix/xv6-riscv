//
// wget - Simple HTTP client
// Usage: wget <hostname>
//

#include "kernel/types.h"
#include "user/user.h"

#define SOCK_DGRAM 2

// Simple htons for 16-bit
uint16 my_htons(uint16 x) {
    return ((x >> 8) & 0xFF) | ((x & 0xFF) << 8);
}

uint16 my_ntohs(uint16 x) {
    return my_htons(x);
}

// DNS lookup (copied from nslookup)
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
    uint16 flags = my_ntohs(hdr->flags);
    if((flags & 0x8000) == 0) return -1;
    int rcode = flags & 0x000F;
    if(rcode != 0) return -1;
    int ancount = my_ntohs(hdr->ancount);
    if(ancount == 0) return -1;
    
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
        ip_out[0] = p[0];
        ip_out[1] = p[1];
        ip_out[2] = p[2];
        ip_out[3] = p[3];
        return 0;
    }
    return -1;
}

int dns_lookup(char *hostname, uint8 *ip) {
    uint8 dns_server[4] = {10, 0, 2, 3};
    
    int fd = socket(SOCK_DGRAM);
    if(fd < 0) return -1;
    sockbind(fd, 12346);
    
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
        sleep(10);
    }
    sockclose(fd);
    return -1;
}

int
main(int argc, char *argv[])
{
    if(argc < 2) {
        printf("Usage: wget <hostname>\n");
        printf("Example: wget example.com\n");
        exit(1);
    }
    
    char *hostname = argv[1];
    printf("wget: Resolving %s...\n", hostname);
    
    // Trigger ARP resolution
    netping();
    for(int i = 0; i < 10; i++) {
        sleep(10);
        netpoll();
    }
    
    // DNS lookup
    uint8 ip[4];
    if(dns_lookup(hostname, ip) < 0) {
        printf("wget: DNS lookup failed\n");
        exit(1);
    }
    
    printf("wget: %s = %d.%d.%d.%d\n", hostname, ip[0], ip[1], ip[2], ip[3]);
    
    // Create TCP socket
    int fd = tcpsocket();
    if(fd < 0) {
        printf("wget: socket failed\n");
        exit(1);
    }
    
    printf("wget: Connecting to port 80...\n");
    
    // Connect
    if(tcpconnect(fd, ip, 80) < 0) {
        printf("wget: connect failed (ARP?)\n");
        // Retry after ARP
        sleep(100);
        netpoll();
        if(tcpconnect(fd, ip, 80) < 0) {
            printf("wget: connect failed\n");
            exit(1);
        }
    }
    
    // Wait for connection to establish
    printf("wget: Waiting for connection...\n");
    for(int i = 0; i < 50; i++) {
        netpoll();
        sleep(10);
    }
    
    // Build HTTP request
    char request[256];
    char *p = request;
    p += strlen(strcpy(p, "GET / HTTP/1.0\r\n"));
    p += strlen(strcpy(p, "Host: "));
    p += strlen(strcpy(p, hostname));
    p += strlen(strcpy(p, "\r\n"));
    p += strlen(strcpy(p, "Connection: close\r\n"));
    p += strlen(strcpy(p, "\r\n"));
    int reqlen = p - request;
    
    printf("wget: Sending request...\n");
    
    if(tcpsend(fd, request, reqlen) < 0) {
        printf("wget: send failed\n");
        tcpclose(fd);
        exit(1);
    }
    
    // Receive response
    printf("wget: Receiving response...\n\n");
    
    char buf[512];
    for(int i = 0; i < 200; i++) {
        netpoll();
        int n = tcprecv(fd, buf, sizeof(buf) - 1);
        if(n > 0) {
            buf[n] = 0;
            printf("%s", buf);
        }
        sleep(5);
    }
    
    printf("\n\nwget: Done\n");
    tcpclose(fd);
    exit(0);
}
