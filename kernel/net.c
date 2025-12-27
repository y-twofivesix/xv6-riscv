//
// Network Stack Implementation
// Ethernet, ARP, IP, ICMP
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"
#include "virtio_net.h"

// Our MAC and IP addresses
static uint8 my_mac[6];
static uint8 my_ip[4] = {MY_IP_0, MY_IP_1, MY_IP_2, MY_IP_3};
static uint8 gw_ip[4] = {GW_IP_0, GW_IP_1, GW_IP_2, GW_IP_3};

// ARP cache (simple: just gateway)
static uint8 gw_mac[6];
static int gw_mac_valid = 0;

// Broadcast MAC
static uint8 broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Packet buffer
static uint8 pkt_buf[MAX_PKT_SIZE + 100];

// IP checksum
uint16
ip_checksum(void *data, int len)
{
    uint32 sum = 0;
    uint16 *p = (uint16*)data;
    
    while(len > 1) {
        sum += *p++;
        len -= 2;
    }
    
    if(len == 1) {
        sum += *(uint8*)p;
    }
    
    while(sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

// Initialize network stack
void
net_stack_init(void)
{
    net_get_mac(my_mac);
    printf("net: stack ready, IP=%d.%d.%d.%d\n", 
           my_ip[0], my_ip[1], my_ip[2], my_ip[3]);
}

// Handle incoming packet
void
net_handle_packet(uint8 *pkt, int len)
{
    if(len < sizeof(struct eth_hdr)) return;
    
    struct eth_hdr *eth = (struct eth_hdr*)pkt;
    uint16 type = ntohs(eth->type);
    
    if(type == ETH_TYPE_ARP) {
        net_handle_arp(pkt, len);
    } else if(type == ETH_TYPE_IP) {
        net_handle_ip(pkt, len);
    }
}

// Handle ARP packet
void
net_handle_arp(uint8 *pkt, int len)
{
    if(len < sizeof(struct eth_hdr) + sizeof(struct arp_hdr)) return;
    
    struct arp_hdr *arp = (struct arp_hdr*)(pkt + sizeof(struct eth_hdr));
    
    if(ntohs(arp->oper) == ARP_OP_REQUEST) {
        // Is it asking for our IP?
        if(arp->tpa[0] == my_ip[0] && arp->tpa[1] == my_ip[1] &&
           arp->tpa[2] == my_ip[2] && arp->tpa[3] == my_ip[3]) {
            // Send ARP reply
            uint8 reply[sizeof(struct eth_hdr) + sizeof(struct arp_hdr)];
            struct eth_hdr *re = (struct eth_hdr*)reply;
            struct arp_hdr *ra = (struct arp_hdr*)(reply + sizeof(struct eth_hdr));
            
            // Ethernet header
            memmove(re->dst, arp->sha, 6);
            memmove(re->src, my_mac, 6);
            re->type = htons(ETH_TYPE_ARP);
            
            // ARP reply
            ra->htype = htons(1);
            ra->ptype = htons(0x0800);
            ra->hlen = 6;
            ra->plen = 4;
            ra->oper = htons(ARP_OP_REPLY);
            memmove(ra->sha, my_mac, 6);
            memmove(ra->spa, my_ip, 4);
            memmove(ra->tha, arp->sha, 6);
            memmove(ra->tpa, arp->spa, 4);
            
            net_send(reply, sizeof(reply));
        }
    } else if(ntohs(arp->oper) == ARP_OP_REPLY) {
        // Check if it's from gateway
        if(arp->spa[0] == gw_ip[0] && arp->spa[1] == gw_ip[1] &&
           arp->spa[2] == gw_ip[2] && arp->spa[3] == gw_ip[3]) {
            memmove(gw_mac, arp->sha, 6);
            gw_mac_valid = 1;
            printf("net: gateway MAC=%x:%x:%x:%x:%x:%x\n",
                   gw_mac[0], gw_mac[1], gw_mac[2],
                   gw_mac[3], gw_mac[4], gw_mac[5]);
        }
    }
}

// Handle IP packet
void
net_handle_ip(uint8 *pkt, int len)
{
    if(len < sizeof(struct eth_hdr) + sizeof(struct ip_hdr)) return;
    
    struct ip_hdr *ip = (struct ip_hdr*)(pkt + sizeof(struct eth_hdr));
    
    // Check it's for us
    if(ip->dst[0] != my_ip[0] || ip->dst[1] != my_ip[1] ||
       ip->dst[2] != my_ip[2] || ip->dst[3] != my_ip[3]) {
        return;
    }
    
    if(ip->proto == IP_PROTO_ICMP) {
        net_handle_icmp(pkt, ntohs(ip->len));
    } else if(ip->proto == IP_PROTO_UDP) {
        net_handle_udp(pkt, ntohs(ip->len));
    }
}

// Handle ICMP packet
void
net_handle_icmp(uint8 *pkt, int iplen)
{
    struct eth_hdr *eth = (struct eth_hdr*)pkt;
    struct ip_hdr *ip = (struct ip_hdr*)(pkt + sizeof(struct eth_hdr));
    int ip_hdr_len = (ip->vhl & 0x0F) * 4;
    struct icmp_hdr *icmp = (struct icmp_hdr*)((uint8*)ip + ip_hdr_len);
    
    if(icmp->type == ICMP_ECHO_REQUEST) {
        // Send echo reply
        printf("net: ICMP echo request from %d.%d.%d.%d\n",
               ip->src[0], ip->src[1], ip->src[2], ip->src[3]);
        
        int icmp_len = iplen - ip_hdr_len;
        
        // Build reply packet
        uint8 reply[MAX_PKT_SIZE];
        struct eth_hdr *re = (struct eth_hdr*)reply;
        struct ip_hdr *ri = (struct ip_hdr*)(reply + sizeof(struct eth_hdr));
        struct icmp_hdr *rc = (struct icmp_hdr*)(reply + sizeof(struct eth_hdr) + sizeof(struct ip_hdr));
        
        // Ethernet: swap src/dst
        memmove(re->dst, eth->src, 6);
        memmove(re->src, my_mac, 6);
        re->type = htons(ETH_TYPE_IP);
        
        // IP: swap src/dst
        ri->vhl = 0x45;  // IPv4, 20-byte header
        ri->tos = 0;
        ri->len = htons(sizeof(struct ip_hdr) + icmp_len);
        ri->id = ip->id;
        ri->off = 0;
        ri->ttl = 64;
        ri->proto = IP_PROTO_ICMP;
        ri->csum = 0;
        memmove(ri->src, my_ip, 4);
        memmove(ri->dst, ip->src, 4);
        ri->csum = ip_checksum(ri, sizeof(struct ip_hdr));
        
        // ICMP: copy payload, change type to reply
        memmove(rc, icmp, icmp_len);
        rc->type = ICMP_ECHO_REPLY;
        rc->csum = 0;
        rc->csum = ip_checksum(rc, icmp_len);
        
        int total_len = sizeof(struct eth_hdr) + sizeof(struct ip_hdr) + icmp_len;
        net_send(reply, total_len);
    } else if(icmp->type == ICMP_ECHO_REPLY) {
        printf("net: ICMP echo reply from %d.%d.%d.%d seq=%d\n",
               ip->src[0], ip->src[1], ip->src[2], ip->src[3],
               ntohs(icmp->seq));
    }
}

// Socket table
static struct socket sockets[MAX_SOCKETS];
static struct spinlock sock_lock;
static int sock_init_done = 0;

static void sock_init(void) {
    if(!sock_init_done) {
        initlock(&sock_lock, "sockets");
        memset(sockets, 0, sizeof(sockets));
        sock_init_done = 1;
    }
}

// Handle UDP packet
void
net_handle_udp(uint8 *pkt, int iplen)
{
    sock_init();
    
    struct ip_hdr *ip = (struct ip_hdr*)(pkt + sizeof(struct eth_hdr));
    int ip_hdr_len = (ip->vhl & 0x0F) * 4;
    struct udp_hdr *udp = (struct udp_hdr*)((uint8*)ip + ip_hdr_len);
    
    uint16 dport = ntohs(udp->dport);
    uint16 sport = ntohs(udp->sport);
    int datalen = ntohs(udp->len) - sizeof(struct udp_hdr);
    uint8 *data = (uint8*)udp + sizeof(struct udp_hdr);
    

    
    // Find socket bound to this port
    acquire(&sock_lock);
    for(int i = 0; i < MAX_SOCKETS; i++) {
        if(sockets[i].used && sockets[i].lport == dport) {

            // Copy data to socket buffer
            int tocopy = datalen;
            if(tocopy > SOCK_BUF_SIZE - sockets[i].rxlen) {
                tocopy = SOCK_BUF_SIZE - sockets[i].rxlen;
            }
            for(int j = 0; j < tocopy; j++) {
                sockets[i].rxbuf[sockets[i].rxhead] = data[j];
                sockets[i].rxhead = (sockets[i].rxhead + 1) % SOCK_BUF_SIZE;
            }
            sockets[i].rxlen += tocopy;
            
            // Store sender info
            memmove(sockets[i].last_src_ip, ip->src, 4);
            sockets[i].last_src_port = sport;
            
            release(&sock_lock);
            return;
        }
    }

    release(&sock_lock);
}

// Send ARP request
int
net_send_arp_request(uint8 *target_ip)
{
    uint8 pkt[sizeof(struct eth_hdr) + sizeof(struct arp_hdr)];
    struct eth_hdr *eth = (struct eth_hdr*)pkt;
    struct arp_hdr *arp = (struct arp_hdr*)(pkt + sizeof(struct eth_hdr));
    
    // Ethernet
    memmove(eth->dst, broadcast_mac, 6);
    memmove(eth->src, my_mac, 6);
    eth->type = htons(ETH_TYPE_ARP);
    
    // ARP request
    arp->htype = htons(1);
    arp->ptype = htons(0x0800);
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = htons(ARP_OP_REQUEST);
    memmove(arp->sha, my_mac, 6);
    memmove(arp->spa, my_ip, 4);
    memset(arp->tha, 0, 6);
    memmove(arp->tpa, target_ip, 4);
    
    return net_send(pkt, sizeof(pkt));
}

// Send ICMP echo request (ping)
int
net_send_icmp_echo(uint8 *dst_ip, uint16 id, uint16 seq, uint8 *data, int datalen)
{
    // Need gateway MAC first
    if(!gw_mac_valid) {
        net_send_arp_request(gw_ip);
        return -1;  // Try again later
    }
    
    uint8 pkt[MAX_PKT_SIZE];
    struct eth_hdr *eth = (struct eth_hdr*)pkt;
    struct ip_hdr *ip = (struct ip_hdr*)(pkt + sizeof(struct eth_hdr));
    struct icmp_hdr *icmp = (struct icmp_hdr*)(pkt + sizeof(struct eth_hdr) + sizeof(struct ip_hdr));
    
    int icmp_len = sizeof(struct icmp_hdr) + datalen;
    int ip_len = sizeof(struct ip_hdr) + icmp_len;
    int total_len = sizeof(struct eth_hdr) + ip_len;
    
    // Ethernet
    memmove(eth->dst, gw_mac, 6);
    memmove(eth->src, my_mac, 6);
    eth->type = htons(ETH_TYPE_IP);
    
    // IP
    ip->vhl = 0x45;
    ip->tos = 0;
    ip->len = htons(ip_len);
    ip->id = htons(id);
    ip->off = 0;
    ip->ttl = 64;
    ip->proto = IP_PROTO_ICMP;
    ip->csum = 0;
    memmove(ip->src, my_ip, 4);
    memmove(ip->dst, dst_ip, 4);
    ip->csum = ip_checksum(ip, sizeof(struct ip_hdr));
    
    // ICMP
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = htons(id);
    icmp->seq = htons(seq);
    icmp->csum = 0;
    
    // Copy payload
    if(data && datalen > 0) {
        memmove((uint8*)icmp + sizeof(struct icmp_hdr), data, datalen);
    }
    
    icmp->csum = ip_checksum(icmp, icmp_len);
    
    return net_send(pkt, total_len);
}

// Send UDP packet
int
net_send_udp(uint8 *dst_ip, uint16 sport, uint16 dport, void *data, int len)
{
    if(!gw_mac_valid) {
        net_send_arp_request(gw_ip);
        return -1;
    }
    
    uint8 pkt[MAX_PKT_SIZE];
    struct eth_hdr *eth = (struct eth_hdr*)pkt;
    struct ip_hdr *ip = (struct ip_hdr*)(pkt + sizeof(struct eth_hdr));
    struct udp_hdr *udp = (struct udp_hdr*)(pkt + sizeof(struct eth_hdr) + sizeof(struct ip_hdr));
    
    int udp_len = sizeof(struct udp_hdr) + len;
    int ip_len = sizeof(struct ip_hdr) + udp_len;
    int total_len = sizeof(struct eth_hdr) + ip_len;
    
    // Ethernet
    memmove(eth->dst, gw_mac, 6);
    memmove(eth->src, my_mac, 6);
    eth->type = htons(ETH_TYPE_IP);
    
    // IP
    ip->vhl = 0x45;
    ip->tos = 0;
    ip->len = htons(ip_len);
    ip->id = htons(1234);
    ip->off = 0;
    ip->ttl = 64;
    ip->proto = IP_PROTO_UDP;
    ip->csum = 0;
    memmove(ip->src, my_ip, 4);
    memmove(ip->dst, dst_ip, 4);
    ip->csum = ip_checksum(ip, sizeof(struct ip_hdr));
    
    // UDP
    udp->sport = htons(sport);
    udp->dport = htons(dport);
    udp->len = htons(udp_len);
    udp->csum = 0;  // Optional for IPv4
    
    // Copy data
    if(data && len > 0) {
        memmove((uint8*)udp + sizeof(struct udp_hdr), data, len);
    }
    
    return net_send(pkt, total_len);
}

// Socket API
int
sock_alloc(int type)
{
    sock_init();
    
    acquire(&sock_lock);
    for(int i = 0; i < MAX_SOCKETS; i++) {
        if(!sockets[i].used) {
            memset(&sockets[i], 0, sizeof(struct socket));
            sockets[i].used = 1;
            sockets[i].type = type;
            release(&sock_lock);
            return i;
        }
    }
    release(&sock_lock);
    return -1;
}

void
sock_free(int fd)
{
    if(fd < 0 || fd >= MAX_SOCKETS) return;
    acquire(&sock_lock);
    sockets[fd].used = 0;
    release(&sock_lock);
}

int
sock_bind(int fd, uint16 port)
{
    if(fd < 0 || fd >= MAX_SOCKETS) return -1;
    acquire(&sock_lock);
    if(!sockets[fd].used) {
        release(&sock_lock);
        return -1;
    }
    sockets[fd].lport = port;
    release(&sock_lock);
    return 0;
}

int
sock_sendto(int fd, void *buf, int len, uint8 *dst_ip, uint16 dport)
{
    if(fd < 0 || fd >= MAX_SOCKETS) return -1;
    acquire(&sock_lock);
    if(!sockets[fd].used) {
        release(&sock_lock);
        return -1;
    }
    uint16 sport = sockets[fd].lport;
    if(sport == 0) sport = 10000 + fd;  // Auto-assign
    release(&sock_lock);
    
    return net_send_udp(dst_ip, sport, dport, buf, len);
}

int
sock_recvfrom(int fd, void *buf, int maxlen, uint8 *src_ip, uint16 *src_port)
{
    if(fd < 0 || fd >= MAX_SOCKETS) return -1;
    
    acquire(&sock_lock);
    if(!sockets[fd].used || sockets[fd].rxlen == 0) {
        release(&sock_lock);
        return 0;  // No data
    }
    
    int tocopy = sockets[fd].rxlen;
    if(tocopy > maxlen) tocopy = maxlen;
    
    for(int i = 0; i < tocopy; i++) {
        ((uint8*)buf)[i] = sockets[fd].rxbuf[sockets[fd].rxtail];
        sockets[fd].rxtail = (sockets[fd].rxtail + 1) % SOCK_BUF_SIZE;
    }
    sockets[fd].rxlen -= tocopy;
    
    if(src_ip) memmove(src_ip, sockets[fd].last_src_ip, 4);
    if(src_port) *src_port = sockets[fd].last_src_port;
    
    release(&sock_lock);
    return tocopy;
}

// Poll for incoming packets (called from process context)
void
net_poll(void)
{
    int len = net_recv(pkt_buf, sizeof(pkt_buf));
    if(len > 0) {

        net_handle_packet(pkt_buf, len);
    }
}
