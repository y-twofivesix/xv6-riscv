//
// Network Protocol Definitions
// Ethernet, ARP, IP, ICMP
//

#ifndef _NET_H_
#define _NET_H_

#include "types.h"

// Byte order macros (network = big endian, host = little endian on RISC-V)
#define htons(x) ((uint16)(((x) >> 8) | ((x) << 8)))
#define ntohs(x) htons(x)
#define htonl(x) ((uint32)(((x) >> 24) | (((x) >> 8) & 0xFF00) | \
                           (((x) << 8) & 0xFF0000) | ((x) << 24)))
#define ntohl(x) htonl(x)

// MAC address size
#define ETH_ADDR_LEN 6

// Ethernet frame header (14 bytes)
struct eth_hdr {
    uint8 dst[ETH_ADDR_LEN];
    uint8 src[ETH_ADDR_LEN];
    uint16 type;  // Network byte order
} __attribute__((packed));

#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806

// ARP packet (28 bytes for IPv4 over Ethernet)
struct arp_hdr {
    uint16 htype;      // Hardware type (1 = Ethernet)
    uint16 ptype;      // Protocol type (0x0800 = IPv4)
    uint8  hlen;       // Hardware address length (6)
    uint8  plen;       // Protocol address length (4)
    uint16 oper;       // Operation (1 = request, 2 = reply)
    uint8  sha[6];     // Sender hardware address
    uint8  spa[4];     // Sender protocol address
    uint8  tha[6];     // Target hardware address
    uint8  tpa[4];     // Target protocol address
} __attribute__((packed));

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

// IPv4 header (20 bytes without options)
struct ip_hdr {
    uint8  vhl;        // Version (4 bits) + Header length (4 bits)
    uint8  tos;        // Type of service
    uint16 len;        // Total length
    uint16 id;         // Identification
    uint16 off;        // Fragment offset + flags
    uint8  ttl;        // Time to live
    uint8  proto;      // Protocol
    uint16 csum;       // Header checksum
    uint8  src[4];     // Source IP
    uint8  dst[4];     // Destination IP
} __attribute__((packed));

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

// ICMP header (8 bytes for echo request/reply)
struct icmp_hdr {
    uint8  type;
    uint8  code;
    uint16 csum;
    uint16 id;
    uint16 seq;
} __attribute__((packed));

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

// UDP header (8 bytes)
struct udp_hdr {
    uint16 sport;      // Source port
    uint16 dport;      // Destination port
    uint16 len;        // Length (header + data)
    uint16 csum;       // Checksum (optional for IPv4)
} __attribute__((packed));

// Socket structure
#define MAX_SOCKETS 8
#define SOCK_BUF_SIZE 2048

struct socket {
    int used;
    int type;          // SOCK_DGRAM for UDP
    uint16 lport;      // Local port
    uint16 rport;      // Remote port (for connected sockets)
    uint8 rip[4];      // Remote IP
    
    // Receive buffer (circular)
    uint8 rxbuf[SOCK_BUF_SIZE];
    int rxhead;
    int rxtail;
    int rxlen;
    
    // Last sender info (for recvfrom)
    uint8 last_src_ip[4];
    uint16 last_src_port;
};

#define SOCK_DGRAM 2

// Our IP configuration (QEMU user-mode defaults)
#define MY_IP_0 10
#define MY_IP_1 0
#define MY_IP_2 2
#define MY_IP_3 15

#define GW_IP_0 10
#define GW_IP_1 0
#define GW_IP_2 2
#define GW_IP_3 2

// Maximum packet size
#define MAX_PKT_SIZE 1500

// Network stack functions
void net_rx_loop(void);
void net_handle_packet(uint8 *pkt, int len);
void net_handle_arp(uint8 *pkt, int len);
void net_handle_ip(uint8 *pkt, int len);
void net_handle_icmp(uint8 *pkt, int iplen);
void net_handle_udp(uint8 *pkt, int iplen);

int net_send_arp_request(uint8 *target_ip);
int net_send_icmp_echo(uint8 *dst_ip, uint16 id, uint16 seq, uint8 *data, int datalen);
int net_send_udp(uint8 *dst_ip, uint16 sport, uint16 dport, void *data, int len);

// Socket API
int sock_alloc(int type);
void sock_free(int fd);
int sock_bind(int fd, uint16 port);
int sock_sendto(int fd, void *buf, int len, uint8 *dst_ip, uint16 dport);
int sock_recvfrom(int fd, void *buf, int maxlen, uint8 *src_ip, uint16 *src_port);

uint16 ip_checksum(void *data, int len);

#endif // _NET_H_
