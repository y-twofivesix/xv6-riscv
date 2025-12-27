//
// nc - Simple netcat for UDP
// Usage: nc <ip> <port>   (send mode)
//    or: nc -l <port>     (listen mode)
//

#include "kernel/types.h"
#include "user/user.h"

#define SOCK_DGRAM 2

// Parse IP address like "10.0.2.2"
int parse_ip(char *s, uint8 *ip) {
    int i = 0, val = 0;
    for(char *p = s; ; p++) {
        if(*p == '.' || *p == 0) {
            ip[i++] = val;
            val = 0;
            if(*p == 0 || i >= 4) break;
        } else if(*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
        } else {
            return -1;
        }
    }
    return (i == 4) ? 0 : -1;
}

int
main(int argc, char *argv[])
{
    if(argc < 3) {
        printf("Usage: nc <ip> <port>   (send)\n");
        printf("       nc -l <port>     (listen)\n");
        exit(1);
    }
    
    int listen_mode = (strcmp(argv[1], "-l") == 0);
    
    // Trigger ARP resolution first
    printf("nc: Resolving gateway...\n");
    netping();
    for(int i = 0; i < 10; i++) {
        sleep(10);
        netpoll();
    }
    
    int fd = socket(SOCK_DGRAM);
    if(fd < 0) {
        printf("nc: socket failed\n");
        exit(1);
    }
    
    if(listen_mode) {
        int port = atoi(argv[2]);
        if(sockbind(fd, port) < 0) {
            printf("nc: bind failed\n");
            exit(1);
        }
        printf("nc: Listening on UDP port %d\n", port);
        
        uint8 buf[256];
        uint8 src_ip[4];
        uint16 src_port;
        
        while(1) {
            netpoll();
            int n = recvfrom(fd, buf, sizeof(buf)-1, src_ip, &src_port);
            if(n > 0) {
                buf[n] = 0;
                printf("[%d.%d.%d.%d:%d] %s\n", 
                       src_ip[0], src_ip[1], src_ip[2], src_ip[3],
                       src_port, buf);
            }
            sleep(10);
        }
    } else {
        uint8 dst_ip[4];
        if(parse_ip(argv[1], dst_ip) < 0) {
            printf("nc: Invalid IP\n");
            exit(1);
        }
        int port = atoi(argv[2]);
        
        sockbind(fd, 8888);  // Local port
        
        printf("nc: Sending to %d.%d.%d.%d:%d\n",
               dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3], port);
        
        char *msg = "Hello from xv6!";
        int ret = sendto(fd, msg, strlen(msg), dst_ip, port);
        if(ret < 0) {
            printf("nc: send failed (ARP not ready?)\n");
            // Try again
            sleep(100);
            ret = sendto(fd, msg, strlen(msg), dst_ip, port);
        }
        
        if(ret > 0) {
            printf("nc: Sent %d bytes\n", ret);
        }
        
        // Wait for any reply
        printf("nc: Waiting for reply...\n");
        uint8 buf[256];
        for(int i = 0; i < 50; i++) {
            netpoll();
            int n = recvfrom(fd, buf, sizeof(buf)-1, 0, 0);
            if(n > 0) {
                buf[n] = 0;
                printf("nc: Reply: %s\n", buf);
                break;
            }
            sleep(10);
        }
    }
    
    sockclose(fd);
    exit(0);
}
