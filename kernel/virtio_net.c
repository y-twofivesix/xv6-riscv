//
// VirtIO Network Device Driver
// For QEMU's virtio-net-device
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "virtio.h"
#include "virtio_net.h"

// Device MMIO base
#define R(r) ((volatile uint32 *)(VIRTIO1 + (r)))

// Device state
static struct {
    // RX and TX virtqueues
    struct virtq_desc *rx_desc;
    struct virtq_avail *rx_avail;
    struct virtq_used *rx_used;
    
    struct virtq_desc *tx_desc;
    struct virtq_avail *tx_avail;
    struct virtq_used *tx_used;
    
    // Free descriptor tracking
    char rx_free[NET_QUEUE_SIZE];
    char tx_free[NET_QUEUE_SIZE];
    uint16 rx_used_idx;
    uint16 tx_used_idx;
    
    // RX buffers (pre-allocated)
    struct net_buf rx_bufs[NET_QUEUE_SIZE];
    
    // TX buffer
    struct net_buf tx_buf;
    
    // MAC address
    uint8 mac[6];
    
    // Lock
    struct spinlock lock;
    
    // Initialized flag
    int initialized;
} net;

// Allocate a free descriptor from a queue
static int alloc_desc(char *free_map) {
    for(int i = 0; i < NET_QUEUE_SIZE; i++) {
        if(free_map[i]) {
            free_map[i] = 0;
            return i;
        }
    }
    return -1;
}

static void free_desc(char *free_map, int i) {
    if(i >= 0 && i < NET_QUEUE_SIZE)
        free_map[i] = 1;
}

// Initialize the network device
void
net_init(void)
{
    uint32 status = 0;
    
    initlock(&net.lock, "virtio_net");
    
    // Check magic value
    if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976) {
        printf("virtio_net: magic value mismatch\n");
        return;
    }
    
    // Check version
    if(*R(VIRTIO_MMIO_VERSION) != 2) {
        printf("virtio_net: unsupported version %d\n", *R(VIRTIO_MMIO_VERSION));
        return;
    }
    
    // Check device ID (1 = network)
    if(*R(VIRTIO_MMIO_DEVICE_ID) != VIRTIO_DEV_NET) {
        printf("virtio_net: not a network device (id=%d)\n", *R(VIRTIO_MMIO_DEVICE_ID));
        return;
    }
    
    // Reset device
    *R(VIRTIO_MMIO_STATUS) = 0;
    
    // Acknowledge
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R(VIRTIO_MMIO_STATUS) = status;
    
    // Driver
    status |= VIRTIO_CONFIG_S_DRIVER;
    *R(VIRTIO_MMIO_STATUS) = status;
    
    // Negotiate features
    uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
    // We want MAC address
    features &= (1 << VIRTIO_NET_F_MAC);
    // Disable features we don't support
    features &= ~(1 << VIRTIO_NET_F_MRG_RXBUF);
    features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
    features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;
    
    // Features OK
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R(VIRTIO_MMIO_STATUS) = status;
    
    // Re-read status to ensure features accepted
    if(!(*R(VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK)) {
        printf("virtio_net: features not accepted\n");
        *R(VIRTIO_MMIO_STATUS) = 0; // Reset
        return;
    }
    
    // Initialize RX queue (queue 0)
    *R(VIRTIO_MMIO_QUEUE_SEL) = NET_QUEUE_RX;
    uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if(max == 0) {
        printf("virtio_net: RX queue not available\n");
        return;
    }
    if(max < NET_QUEUE_SIZE) {
        printf("virtio_net: RX queue too small\n");
        return;
    }
    *R(VIRTIO_MMIO_QUEUE_NUM) = NET_QUEUE_SIZE;
    
    // Allocate queue memory
    net.rx_desc = kalloc();
    net.rx_avail = kalloc();
    net.rx_used = kalloc();
    memset(net.rx_desc, 0, PGSIZE);
    memset(net.rx_avail, 0, PGSIZE);
    memset(net.rx_used, 0, PGSIZE);
    
    // Set queue addresses
    *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)net.rx_desc;
    *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)net.rx_desc >> 32;
    *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)net.rx_avail;
    *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)net.rx_avail >> 32;
    *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)net.rx_used;
    *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)net.rx_used >> 32;
    
    // Enable RX queue
    *R(VIRTIO_MMIO_QUEUE_READY) = 1;
    
    // Initialize TX queue (queue 1)
    *R(VIRTIO_MMIO_QUEUE_SEL) = NET_QUEUE_TX;
    max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if(max == 0 || max < NET_QUEUE_SIZE) {
        printf("virtio_net: TX queue not available\n");
        return;
    }
    *R(VIRTIO_MMIO_QUEUE_NUM) = NET_QUEUE_SIZE;
    
    net.tx_desc = kalloc();
    net.tx_avail = kalloc();
    net.tx_used = kalloc();
    memset(net.tx_desc, 0, PGSIZE);
    memset(net.tx_avail, 0, PGSIZE);
    memset(net.tx_used, 0, PGSIZE);
    
    *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)net.tx_desc;
    *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)net.tx_desc >> 32;
    *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)net.tx_avail;
    *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)net.tx_avail >> 32;
    *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)net.tx_used;
    *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)net.tx_used >> 32;
    
    *R(VIRTIO_MMIO_QUEUE_READY) = 1;
    
    // Initialize free descriptor maps
    for(int i = 0; i < NET_QUEUE_SIZE; i++) {
        net.rx_free[i] = 1;
        net.tx_free[i] = 1;
    }
    
    // Read MAC address from config space (offset 0x100)
    volatile uint8 *cfg = (volatile uint8 *)(VIRTIO1 + 0x100);
    for(int i = 0; i < 6; i++) {
        net.mac[i] = cfg[i];
    }
    
    // Pre-populate RX queue with buffers
    for(int i = 0; i < NET_QUEUE_SIZE; i++) {
        int desc = alloc_desc(net.rx_free);
        if(desc < 0) break;
        
        net.rx_desc[desc].addr = (uint64)&net.rx_bufs[i];
        net.rx_desc[desc].len = sizeof(struct net_buf);
        net.rx_desc[desc].flags = VRING_DESC_F_WRITE;
        net.rx_desc[desc].next = 0;
        
        net.rx_avail->ring[net.rx_avail->idx % NET_QUEUE_SIZE] = desc;
        __sync_synchronize();
        net.rx_avail->idx++;
    }
    
    // Notify device of available RX buffers
    __sync_synchronize();
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = NET_QUEUE_RX;
    
    // Driver OK
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = status;
    
    net.initialized = 1;
    
    printf("virtio_net: MAC=%x:%x:%x:%x:%x:%x\n",
           net.mac[0], net.mac[1], net.mac[2],
           net.mac[3], net.mac[4], net.mac[5]);
}

// Send a packet
int
net_send(void *data, int len)
{
    if(!net.initialized) return -1;
    if(len > NET_PKT_SIZE) return -1;
    
    acquire(&net.lock);
    
    int desc = alloc_desc(net.tx_free);
    if(desc < 0) {
        release(&net.lock);
        return -1;
    }
    
    // Prepare virtio-net header
    memset(&net.tx_buf.hdr, 0, sizeof(struct virtio_net_hdr));
    net.tx_buf.hdr.gso_type = VIRTIO_NET_HDR_GSO_NONE;
    
    // Copy packet data
    memmove(net.tx_buf.data, data, len);
    
    // Setup descriptor
    net.tx_desc[desc].addr = (uint64)&net.tx_buf;
    net.tx_desc[desc].len = VIRTIO_NET_HDR_SIZE + len;
    net.tx_desc[desc].flags = 0; // Device reads
    net.tx_desc[desc].next = 0;
    
    // Add to available ring
    net.tx_avail->ring[net.tx_avail->idx % NET_QUEUE_SIZE] = desc;
    __sync_synchronize();
    net.tx_avail->idx++;
    
    // Notify device
    __sync_synchronize();
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = NET_QUEUE_TX;
    
    release(&net.lock);
    return len;
}

// Receive a packet (non-blocking)
int
net_recv(void *buf, int maxlen)
{
    if(!net.initialized) return -1;
    
    acquire(&net.lock);
    
    // Check if any packets available
    __sync_synchronize();
    if(net.rx_used_idx == net.rx_used->idx) {
        release(&net.lock);
        return 0; // No packet
    }
    
    
    // Get used descriptor
    struct virtq_used_elem *elem = &net.rx_used->ring[net.rx_used_idx % NET_QUEUE_SIZE];
    int desc_idx = elem->id;
    int pkt_len = elem->len - VIRTIO_NET_HDR_SIZE;
    
    
    if(pkt_len > maxlen) pkt_len = maxlen;
    if(pkt_len > 0) {
        memmove(buf, net.rx_bufs[desc_idx].data, pkt_len);
    }
    
    // Recycle buffer
    net.rx_avail->ring[net.rx_avail->idx % NET_QUEUE_SIZE] = desc_idx;
    __sync_synchronize();
    net.rx_avail->idx++;
    
    net.rx_used_idx++;
    
    // Notify device of new RX buffer
    __sync_synchronize();
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = NET_QUEUE_RX;
    
    release(&net.lock);
    return pkt_len;
}

// Interrupt handler
void
net_intr(void)
{
    // Acknowledge interrupt
    *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
    
    // TX completion: free descriptors
    acquire(&net.lock);
    while(net.tx_used_idx != net.tx_used->idx) {
        struct virtq_used_elem *elem = &net.tx_used->ring[net.tx_used_idx % NET_QUEUE_SIZE];
        free_desc(net.tx_free, elem->id);
        net.tx_used_idx++;
    }
    release(&net.lock);
    
    // RX is handled by net_recv() polling
    // Future: wakeup sleeping processes waiting for packets
}

// Get MAC address
void
net_get_mac(uint8 *mac)
{
    for(int i = 0; i < 6; i++)
        mac[i] = net.mac[i];
}
