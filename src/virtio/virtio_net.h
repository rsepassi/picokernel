// Generic VirtIO Network Device Driver
// Transport-agnostic network driver (works with MMIO or PCI)

#pragma once

#include "kconfig.h"
#include "virtio.h"
#include "virtio_mmio.h"
#include "virtio_pci.h"
#include <stddef.h>
#include <stdint.h>

// Forward declarations
struct kernel;
typedef struct kernel kernel_t;
struct kwork;
typedef struct kwork kwork_t;

// VirtIO Network Device ID
#define VIRTIO_ID_NET 1

// VirtIO Network feature bits
#define VIRTIO_NET_F_CSUM 0              // Device handles packets with partial checksum
#define VIRTIO_NET_F_GUEST_CSUM 1        // Guest handles packets with partial checksum
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS 2 // Control channel offloads
#define VIRTIO_NET_F_MTU 3               // Initial MTU advice
#define VIRTIO_NET_F_MAC 5               // Device has given MAC address
#define VIRTIO_NET_F_GUEST_TSO4 7        // Guest can receive TSOv4
#define VIRTIO_NET_F_GUEST_TSO6 8        // Guest can receive TSOv6
#define VIRTIO_NET_F_GUEST_ECN 9         // Guest can receive TSO with ECN
#define VIRTIO_NET_F_GUEST_UFO 10        // Guest can receive UFO
#define VIRTIO_NET_F_HOST_TSO4 11        // Device can receive TSOv4
#define VIRTIO_NET_F_HOST_TSO6 12        // Device can receive TSOv6
#define VIRTIO_NET_F_HOST_ECN 13         // Device can receive TSO with ECN
#define VIRTIO_NET_F_HOST_UFO 14         // Device can receive UFO
#define VIRTIO_NET_F_MRG_RXBUF 15        // Guest can merge receive buffers
#define VIRTIO_NET_F_STATUS 16           // Configuration status field available
#define VIRTIO_NET_F_CTRL_VQ 17          // Control channel available
#define VIRTIO_NET_F_CTRL_RX 18          // Control channel RX mode support
#define VIRTIO_NET_F_CTRL_VLAN 19        // Control channel VLAN filtering
#define VIRTIO_NET_F_GUEST_ANNOUNCE 21   // Guest can send gratuitous packets
#define VIRTIO_NET_F_MQ 22               // Device supports multiple TX/RX queues

// VirtIO Network device configuration (read from device config space)
typedef struct {
  uint8_t mac[6];        // MAC address (if VIRTIO_NET_F_MAC)
  uint16_t status;       // Device status (if VIRTIO_NET_F_STATUS)
  uint16_t max_virtqueue_pairs; // Maximum number of virtqueue pairs (if VIRTIO_NET_F_MQ)
  uint16_t mtu;          // Maximum MTU (if VIRTIO_NET_F_MTU)
} __attribute__((packed)) virtio_net_config_t;

// VirtIO Network packet header (prepended to all packets)
typedef struct {
  uint8_t flags;         // Flags
  uint8_t gso_type;      // GSO type
  uint16_t hdr_len;      // Header length
  uint16_t gso_size;     // GSO size
  uint16_t csum_start;   // Checksum start
  uint16_t csum_offset;  // Checksum offset
  uint16_t num_buffers;  // Number of buffers (if VIRTIO_NET_F_MRG_RXBUF)
} __attribute__((packed)) virtio_net_hdr_t;

// Network header flags
#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1    // Use csum_start and csum_offset
#define VIRTIO_NET_HDR_F_DATA_VALID 2    // Checksum is valid

// GSO types
#define VIRTIO_NET_HDR_GSO_NONE 0        // No GSO
#define VIRTIO_NET_HDR_GSO_TCPV4 1       // GSO TCP/IPv4
#define VIRTIO_NET_HDR_GSO_UDP 3         // GSO UDP
#define VIRTIO_NET_HDR_GSO_TCPV6 4       // GSO TCP/IPv6
#define VIRTIO_NET_HDR_GSO_ECN 0x80      // TCP has ECN set

// Virtqueue indices
#define VIRTIO_NET_VQ_RX 0               // Receive queue
#define VIRTIO_NET_VQ_TX 1               // Transmit queue

// Maximum number of outstanding requests (per queue)
#define VIRTIO_NET_MAX_REQUESTS 256

// RX request tracking (stores both request and buffer index)
// Note: Uses void* to avoid circular dependency with kapi.h
typedef struct {
  void *req; // knet_recv_req_t pointer
  size_t buffer_index;
} rx_request_tracking_t;

// VirtIO Network device state
typedef struct virtio_net_dev {
  // Device base (MUST be first field for pointer casting!)
  kdevice_base_t base;

  // Transport (either MMIO or PCI)
  void *transport;    // Points to transport-specific structure
  int transport_type; // VIRTIO_TRANSPORT_MMIO or VIRTIO_TRANSPORT_PCI

  // Virtqueues (RX and TX)
  virtqueue_t rx_vq;
  virtqueue_t tx_vq;
  virtqueue_memory_t *rx_vq_memory;
  virtqueue_memory_t *tx_vq_memory;
  uint16_t queue_size;

  // Device configuration
  uint8_t mac_address[6]; // MAC address

  // Request tracking (constant-time lookup)
  rx_request_tracking_t active_rx_requests[VIRTIO_NET_MAX_REQUESTS]; // RX buffers
  void *active_tx_requests[VIRTIO_NET_MAX_REQUESTS]; // TX packets

  // Track number of outstanding requests
  volatile uint16_t outstanding_rx_requests;
  volatile uint16_t outstanding_tx_requests;

  // Receive buffer tracking for standing work
  void *standing_recv_req; // Pointer to the standing recv request (if any)

  // Kernel reference
  kernel_t *kernel;
} virtio_net_dev_t;

// Initialize network device with MMIO transport
int virtio_net_init_mmio(virtio_net_dev_t *net, virtio_mmio_transport_t *mmio,
                         virtqueue_memory_t *rx_queue_memory,
                         virtqueue_memory_t *tx_queue_memory, kernel_t *kernel);

// Initialize network device with PCI transport
int virtio_net_init_pci(virtio_net_dev_t *net, virtio_pci_transport_t *pci,
                        virtqueue_memory_t *rx_queue_memory,
                        virtqueue_memory_t *tx_queue_memory, kernel_t *kernel);

// Process interrupt (transport-agnostic)
void virtio_net_process_irq(virtio_net_dev_t *net, kernel_t *k);

// Submit work (transport-agnostic)
void virtio_net_submit_work(virtio_net_dev_t *net, kwork_t *submissions,
                            kernel_t *k);

// Release a receive buffer back to the ring (for standing work)
// req is knet_recv_req_t* (passed as void* to avoid circular dependency)
void virtio_net_buffer_release(virtio_net_dev_t *net, void *req,
                                size_t buffer_index);

// Cancel work (for standing recv work cleanup)
void virtio_net_cancel_work(virtio_net_dev_t *net, kwork_t *work,
                             kernel_t *k);
