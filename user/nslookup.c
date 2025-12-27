//
// nslookup - DNS lookup utility
// Usage: nslookup <hostname>
// Queries QEMU's built-in DNS server at 10.0.2.3
//

#include "kernel/types.h"
#include "user/user.h"

#define SOCK_DGRAM 2
#define DNS_PORT 53

// DNS header structure
struct dns_header {
    uint16 id;       // Transaction ID
    uint16 flags;    // Flags
    uint16 qdcount;  // Number of questions
    uint16 ancount;  // Number of answers
    uint16 nscount;  // Number of authority records
    uint16 arcount;  // Number of additional records
};

// Simple htons for 16-bit
uint16 my_htons(uint16 x) {
    return ((x >> 8) & 0xFF) | ((x & 0xFF) << 8);
}

uint16 my_ntohs(uint16 x) {
    return my_htons(x);
}

// Build DNS query for a hostname
// Returns total packet length
int build_dns_query(char *hostname, uint8 *buf) {
    struct dns_header *hdr = (struct dns_header*)buf;
    
    // Header
    hdr->id = my_htons(0x1234);  // Transaction ID
    hdr->flags = my_htons(0x0100);  // Standard query, recursion desired
    hdr->qdcount = my_htons(1);  // 1 question
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    
    // Question section: encode hostname
    uint8 *p = buf + sizeof(struct dns_header);
    char *src = hostname;
    
    while(*src) {
        // Find next dot or end
        char *dot = src;
        while(*dot && *dot != '.') dot++;
        
        int len = dot - src;
        *p++ = len;  // Length byte
        while(src < dot) {
            *p++ = *src++;
        }
        if(*src == '.') src++;
    }
    *p++ = 0;  // Null terminator for labels
    
    // Query type: A (host address) = 1
    *p++ = 0;
    *p++ = 1;
    
    // Query class: IN (Internet) = 1
    *p++ = 0;
    *p++ = 1;
    
    return p - buf;
}

// Parse DNS response and extract IP address
int parse_dns_response(uint8 *buf, int len, uint8 *ip_out) {
    if(len < sizeof(struct dns_header)) return -1;
    
    struct dns_header *hdr = (struct dns_header*)buf;
    
    // Check response
    uint16 flags = my_ntohs(hdr->flags);
    if((flags & 0x8000) == 0) {
        printf("nslookup: Not a response\n");
        return -1;
    }
    
    // Check for errors (RCODE in lower 4 bits)
    int rcode = flags & 0x000F;
    if(rcode != 0) {
        printf("nslookup: DNS error code %d\n", rcode);
        return -1;
    }
    
    int ancount = my_ntohs(hdr->ancount);
    if(ancount == 0) {
        printf("nslookup: No answers\n");
        return -1;
    }
    
    // Skip question section
    uint8 *p = buf + sizeof(struct dns_header);
    
    // Skip QNAME (labels ending with 0)
    while(*p != 0) {
        if((*p & 0xC0) == 0xC0) {
            // Compression pointer
            p += 2;
            break;
        } else {
            p += *p + 1;
        }
    }
    if(*p == 0) p++;  // Skip null terminator
    p += 4;  // Skip QTYPE and QCLASS
    
    // Parse first answer
    // Skip NAME (may be compressed)
    if((*p & 0xC0) == 0xC0) {
        p += 2;  // Compression pointer
    } else {
        while(*p != 0) p += *p + 1;
        p++;
    }
    
    // TYPE (2 bytes)
    uint16 type = (p[0] << 8) | p[1];
    p += 2;
    
    // CLASS (2 bytes)
    p += 2;
    
    // TTL (4 bytes)
    p += 4;
    
    // RDLENGTH (2 bytes)
    uint16 rdlen = (p[0] << 8) | p[1];
    p += 2;
    
    // RDATA
    if(type == 1 && rdlen == 4) {
        // A record: IPv4 address
        ip_out[0] = p[0];
        ip_out[1] = p[1];
        ip_out[2] = p[2];
        ip_out[3] = p[3];
        return 0;
    }
    
    printf("nslookup: No A record found (type=%d, len=%d)\n", type, rdlen);
    return -1;
}

int
main(int argc, char *argv[])
{
    if(argc < 2) {
        printf("Usage: nslookup <hostname>\n");
        printf("Example: nslookup google.com\n");
        exit(1);
    }
    
    char *hostname = argv[1];
    printf("Looking up %s...\n", hostname);
    
    // QEMU's DNS server
    uint8 dns_server[4] = {10, 0, 2, 3};
    
    // Trigger ARP resolution for gateway first
    printf("Resolving gateway...\n");
    netping();
    for(int i = 0; i < 10; i++) {
        sleep(10);
        netpoll();
    }
    
    // Create socket
    int fd = socket(SOCK_DGRAM);
    if(fd < 0) {
        printf("nslookup: socket failed\n");
        exit(1);
    }
    
    sockbind(fd, 12345);  // Local port
    
    // Build DNS query
    uint8 query[512];
    int qlen = build_dns_query(hostname, query);
    
    // Send query
    printf("Querying DNS server %d.%d.%d.%d...\n",
           dns_server[0], dns_server[1], dns_server[2], dns_server[3]);
    
    int ret = sendto(fd, query, qlen, dns_server, DNS_PORT);
    if(ret < 0) {
        printf("nslookup: send failed, trying again...\n");
        sleep(100);
        ret = sendto(fd, query, qlen, dns_server, DNS_PORT);
    }
    
    if(ret < 0) {
        printf("nslookup: send failed\n");
        sockclose(fd);
        exit(1);
    }
    
    printf("Waiting for response...\n");
    
    // Wait for response
    uint8 response[512];
    uint8 ip[4];
    
    for(int i = 0; i < 100; i++) {
        netpoll();
        int n = recvfrom(fd, response, sizeof(response), 0, 0);
        if(n > 0) {
            if(parse_dns_response(response, n, ip) == 0) {
                printf("\n%s = %d.%d.%d.%d\n", hostname, 
                       ip[0], ip[1], ip[2], ip[3]);
                sockclose(fd);
                exit(0);
            }
        }
        sleep(10);
    }
    
    printf("nslookup: No response\n");
    sockclose(fd);
    exit(1);
}
