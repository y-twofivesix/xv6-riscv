//
// VirtIO Network Device Definitions
// Based on VirtIO 1.1 Spec Section 5.1
//

#ifndef _VIRTIO_NET_H_
#define _VIRTIO_NET_H_

#include "types.h"

// VirtIO device ID for network
#define VIRTIO_DEV_NET 1

// VirtIO-Net feature bits
#define VIRTIO_NET_F_CSUM           0   // Host handles partial checksum
#define VIRTIO_NET_F_GUEST_CSUM     1   // Guest handles partial checksum
#define VIRTIO_NET_F_MAC            5   // Device has given MAC address
#define VIRTIO_NET_F_MRG_RXBUF      15  // Merge RX buffers
#define VIRTIO_NET_F_STATUS         16  // Link status available
#define VIRTIO_NET_F_CTRL_VQ        17  // Control channel available

// Virtqueue indices
#define NET_QUEUE_RX 0
#define NET_QUEUE_TX 1

// Number of descriptors per queue
#define NET_QUEUE_SIZE 16

// Maximum packet size (Ethernet MTU + headers)
#define NET_PKT_SIZE 1518

// Virtio-net packet header (prepended to every packet)
struct virtio_net_hdr {
    uint8 flags;
    uint8 gso_type;
    uint16 hdr_len;
    uint16 gso_size;
    uint16 csum_start;
    uint16 csum_offset;
    // uint16 num_buffers; // Only with VIRTIO_NET_F_MRG_RXBUF
};

#define VIRTIO_NET_HDR_SIZE sizeof(struct virtio_net_hdr)

// GSO types
#define VIRTIO_NET_HDR_GSO_NONE 0

// Flags
#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1

// Network packet buffer (header + frame)
struct net_buf {
    struct virtio_net_hdr hdr;
    uint8 data[NET_PKT_SIZE];
};

// Driver functions
void net_init(void);
int net_send(void *data, int len);
int net_recv(void *buf, int maxlen);
void net_intr(void);

// Get MAC address (6 bytes)
void net_get_mac(uint8 *mac);

#endif // _VIRTIO_NET_H_
