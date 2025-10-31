// User entry point - test RNG and Block devices

#include "user.h"
#include "platform.h"
#include "printk.h"

// Helper to get current timestamp
static uint64_t get_timestamp_ms(kuser_t *user) {
  return user->kernel->current_time_ms;
}

// Helper to write uint64_t to buffer (little-endian)
static void write_u64(uint8_t *buf, uint64_t val) {
  // Use volatile to prevent compiler from optimizing to unaligned store
  volatile uint8_t *vbuf = buf;
  vbuf[0] = (uint8_t)(val);
  vbuf[1] = (uint8_t)(val >> 8);
  vbuf[2] = (uint8_t)(val >> 16);
  vbuf[3] = (uint8_t)(val >> 24);
  vbuf[4] = (uint8_t)(val >> 32);
  vbuf[5] = (uint8_t)(val >> 40);
  vbuf[6] = (uint8_t)(val >> 48);
  vbuf[7] = (uint8_t)(val >> 56);
}

// Helper to read uint64_t from buffer (little-endian)
static uint64_t read_u64(const uint8_t *buf) {
  return (uint64_t)buf[0] | ((uint64_t)buf[1] << 8) | ((uint64_t)buf[2] << 16) |
         ((uint64_t)buf[3] << 24) | ((uint64_t)buf[4] << 32) |
         ((uint64_t)buf[5] << 40) | ((uint64_t)buf[6] << 48) |
         ((uint64_t)buf[7] << 56);
}

// Read 16-bit value from buffer (big-endian/network byte order)
// Avoids unaligned access issues
static uint16_t read_be16(const uint8_t *buf) {
  return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

// Write 16-bit value to buffer (big-endian/network byte order)
// Avoids unaligned access issues
static void write_be16(uint8_t *buf, uint16_t val) {
  volatile uint8_t *vbuf = buf;
  vbuf[0] = (uint8_t)(val >> 8);
  vbuf[1] = (uint8_t)(val);
}

// Calculate IP header checksum (RFC 791)
static uint16_t ip_checksum(const uint8_t *header, size_t len) {
  uint32_t sum = 0;

  // Sum all 16-bit words
  for (size_t i = 0; i < len; i += 2) {
    uint16_t word = ((uint16_t)header[i] << 8) | (uint16_t)header[i + 1];
    sum += word;
  }

  // Fold 32-bit sum to 16 bits
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }

  // Return one's complement
  return (uint16_t)~sum;
}

// Forward declarations
static void on_packet_sent(kwork_t *work);
static void send_arp_reply(kuser_t *user, const uint8_t *target_mac,
                           const uint8_t *target_ip);
static void handle_arp_packet(kuser_t *user, const uint8_t *pkt,
                               size_t pkt_len);
static void handle_icmp_packet(kuser_t *user, const uint8_t *pkt,
                                size_t pkt_len, const uint8_t *ip_hdr);
static void handle_udp_packet(kuser_t *user, const uint8_t *pkt,
                               size_t pkt_len, const uint8_t *ip_hdr);

// Network configuration (QEMU user networking)
static const uint8_t DEVICE_IP[4] = {10, 0, 2, 15};
static const uint8_t GATEWAY_IP[4] = {10, 0, 2, 2};
static const uint8_t GATEWAY_MAC[6] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02};
static const uint16_t UDP_ECHO_PORT = 8080;

// Protocol constants
#define ETHERTYPE_ARP 0x0806
#define ETHERTYPE_IPV4 0x0800
#define IP_PROTOCOL_ICMP 0x01
#define IP_PROTOCOL_UDP 0x11

// ARP constants
#define ARP_HTYPE_ETHERNET 0x0001
#define ARP_PTYPE_IPV4 0x0800
#define ARP_OPER_REQUEST 0x0001
#define ARP_OPER_REPLY 0x0002

// ICMP constants
#define ICMP_TYPE_ECHO_REPLY 0
#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_CODE_ECHO 0

// RNG callback
static void on_random_ready(kwork_t *work) {
  kuser_t *user = work->ctx;
  (void)user;

  if (work->result != KERR_OK) {
    printk("RNG failed: error ");
    printk_dec(work->result);
    printk("\n");
    return;
  }

  printk("Random bytes (");
  krng_req_t *req = CONTAINER_OF(work, krng_req_t, work);
  printk_dec(req->completed);
  printk("): ");

  for (size_t i = 0; i < req->completed && i < 32; i++) {
    printk_hex8(req->buffer[i]);
    if (i < req->completed - 1)
      printk(" ");
  }
  printk("\n");
}

// Send ARP reply packet
static void send_arp_reply(kuser_t *user, const uint8_t *target_mac,
                           const uint8_t *target_ip) {
  if (user->arp_send_req.work.state != KWORK_STATE_DEAD) {
    printk("ARP reply already in flight, ignoring\n");
    return;
  }

  uint8_t *tx_pkt = user->arp_tx_buf;
  size_t tx_len = 0;
  const uint8_t *device_mac = user->kernel->platform.net_mac_address;

  // Ethernet header (14 bytes)
  memcpy(&tx_pkt[0], target_mac, 6);     // dst_mac
  memcpy(&tx_pkt[6], device_mac, 6);     // src_mac
  write_be16(&tx_pkt[12], ETHERTYPE_ARP);
  tx_len += 14;

  // ARP packet (28 bytes)
  write_be16(&tx_pkt[14], ARP_HTYPE_ETHERNET);  // Hardware type
  write_be16(&tx_pkt[16], ARP_PTYPE_IPV4);      // Protocol type
  tx_pkt[18] = 6;                               // Hardware address length
  tx_pkt[19] = 4;                               // Protocol address length
  write_be16(&tx_pkt[20], ARP_OPER_REPLY);      // Operation (reply)
  memcpy(&tx_pkt[22], device_mac, 6);           // Sender MAC
  memcpy(&tx_pkt[28], DEVICE_IP, 4);            // Sender IP
  memcpy(&tx_pkt[32], target_mac, 6);           // Target MAC
  memcpy(&tx_pkt[36], target_ip, 4);            // Target IP
  tx_len += 28;

  // Setup send request
  user->arp_tx_packet.buffer = tx_pkt;
  user->arp_tx_packet.buffer_size = tx_len;

  user->arp_send_req.work.op = KWORK_OP_NET_SEND;
  user->arp_send_req.work.callback = on_packet_sent;
  user->arp_send_req.work.ctx = user;
  user->arp_send_req.work.state = KWORK_STATE_DEAD;
  user->arp_send_req.work.flags = 0;
  user->arp_send_req.packets = &user->arp_tx_packet;
  user->arp_send_req.num_packets = 1;
  user->arp_send_req.packets_sent = 0;

  kerr_t err = ksubmit(user->kernel, &user->arp_send_req.work);
  if (err != KERR_OK) {
    printk("ARP reply send failed: error ");
    printk_dec(err);
    printk("\n");
  } else {
    printk("Sent ARP reply to ");
    printk_ip(target_ip);
    printk(" (");
    printk_mac(target_mac);
    printk(")\n");
  }
}

// Handle ARP packet
static void handle_arp_packet(kuser_t *user, const uint8_t *pkt,
                               size_t pkt_len) {
  // Minimum ARP packet: 14 (Ethernet) + 28 (ARP) = 42 bytes
  if (pkt_len < 42) {
    printk("ARP packet too small (");
    printk_dec(pkt_len);
    printk(" bytes)\n");
    return;
  }

  const uint8_t *arp = &pkt[14];
  uint16_t htype = read_be16(&arp[0]);
  uint16_t ptype = read_be16(&arp[2]);
  uint8_t hlen = arp[4];
  uint8_t plen = arp[5];
  uint16_t oper = read_be16(&arp[6]);

  // Validate ARP packet
  if (htype != ARP_HTYPE_ETHERNET || ptype != ARP_PTYPE_IPV4 || hlen != 6 ||
      plen != 4) {
    printk("Invalid ARP packet format\n");
    return;
  }

  const uint8_t *sender_mac = &arp[8];
  const uint8_t *sender_ip = &arp[14];
  const uint8_t *target_ip = &arp[24];

  printk("ARP ");
  if (oper == ARP_OPER_REQUEST) {
    printk("request: Who has ");
    printk_ip(target_ip);
    printk("? Tell ");
    printk_ip(sender_ip);
    printk("\n");

    // Check if the request is for our IP
    if (target_ip[0] == DEVICE_IP[0] && target_ip[1] == DEVICE_IP[1] &&
        target_ip[2] == DEVICE_IP[2] && target_ip[3] == DEVICE_IP[3]) {
      printk("ARP request is for us, sending reply\n");
      send_arp_reply(user, sender_mac, sender_ip);
    }
  } else if (oper == ARP_OPER_REPLY) {
    printk("reply: ");
    printk_ip(sender_ip);
    printk(" is at ");
    printk_mac(sender_mac);
    printk("\n");
  } else {
    printk("unknown opcode ");
    printk_hex16(oper);
    printk("\n");
  }
}

// Handle ICMP packet (ping)
static void handle_icmp_packet(kuser_t *user, const uint8_t *pkt,
                                size_t pkt_len, const uint8_t *ip_hdr) {
  // Minimum ICMP packet: 14 (Ethernet) + 20 (IP) + 8 (ICMP header) = 42 bytes
  if (pkt_len < 42) {
    printk("ICMP packet too small\n");
    return;
  }

  const uint8_t *icmp_hdr = &pkt[34]; // 14 + 20
  uint8_t icmp_type = icmp_hdr[0];
  uint8_t icmp_code = icmp_hdr[1];
  uint16_t icmp_checksum = read_be16(&icmp_hdr[2]);
  KUNUSED(icmp_checksum);

  uint16_t ip_total_len = read_be16(&ip_hdr[2]);
  size_t icmp_len = ip_total_len - 20; // Subtract IP header

  const uint8_t *ip_src = &ip_hdr[12];
  const uint8_t *ip_dst = &ip_hdr[16];
  const uint8_t *eth_src_mac = &pkt[6];
  const uint8_t *eth_dst_mac = &pkt[0];

  if (icmp_type == ICMP_TYPE_ECHO_REQUEST && icmp_code == ICMP_CODE_ECHO) {
    printk("ICMP echo request from ");
    printk_ip(ip_src);
    printk(", sending reply\n");

    // Check if send request is available
    if (user->icmp_send_req.work.state != KWORK_STATE_DEAD) {
      printk("ICMP reply already in flight, ignoring\n");
      return;
    }

    uint8_t *tx_pkt = user->icmp_tx_buf;
    size_t tx_len = 0;

    // Ethernet header (14 bytes) - swap src/dst
    memcpy(&tx_pkt[0], eth_src_mac, 6);
    memcpy(&tx_pkt[6], eth_dst_mac, 6);
    write_be16(&tx_pkt[12], ETHERTYPE_IPV4);
    tx_len += 14;

    // IPv4 header (20 bytes) - swap src/dst IPs
    memcpy(&tx_pkt[14], ip_hdr, 20);
    memcpy(&tx_pkt[14 + 12], ip_dst, 4); // src = original dst
    memcpy(&tx_pkt[14 + 16], ip_src, 4); // dst = original src
    // Zero checksum for recalculation
    tx_pkt[14 + 10] = 0;
    tx_pkt[14 + 11] = 0;
    uint16_t ip_csum = ip_checksum(&tx_pkt[14], 20);
    write_be16(&tx_pkt[14 + 10], ip_csum);
    tx_len += 20;

    // ICMP header + data - change type to reply, recalculate checksum
    memcpy(&tx_pkt[34], icmp_hdr, icmp_len);
    tx_pkt[34] = ICMP_TYPE_ECHO_REPLY; // Change type to reply
    tx_pkt[34 + 1] = ICMP_CODE_ECHO;
    // Zero checksum field for recalculation
    tx_pkt[34 + 2] = 0;
    tx_pkt[34 + 3] = 0;
    uint16_t icmp_csum = ip_checksum(&tx_pkt[34], icmp_len);
    write_be16(&tx_pkt[34 + 2], icmp_csum);
    tx_len += icmp_len;

    // Setup send request
    user->icmp_tx_packet.buffer = tx_pkt;
    user->icmp_tx_packet.buffer_size = tx_len;

    user->icmp_send_req.work.op = KWORK_OP_NET_SEND;
    user->icmp_send_req.work.callback = on_packet_sent;
    user->icmp_send_req.work.ctx = user;
    user->icmp_send_req.work.state = KWORK_STATE_DEAD;
    user->icmp_send_req.work.flags = 0;
    user->icmp_send_req.packets = &user->icmp_tx_packet;
    user->icmp_send_req.num_packets = 1;
    user->icmp_send_req.packets_sent = 0;

    kerr_t err = ksubmit(user->kernel, &user->icmp_send_req.work);
    if (err != KERR_OK) {
      printk("ICMP reply send failed: error ");
      printk_dec(err);
      printk("\n");
    } else {
      printk("Sent ICMP echo reply to ");
      printk_ip(ip_src);
      printk("\n");
    }
  } else {
    printk("ICMP type=");
    printk_dec(icmp_type);
    printk(" code=");
    printk_dec(icmp_code);
    printk(" (not echo request)\n");
  }
}

// Handle UDP packet (echo server)
static void handle_udp_packet(kuser_t *user, const uint8_t *pkt,
                               size_t pkt_len, const uint8_t *ip_hdr) {
  // Minimum UDP packet: 14 (Ethernet) + 20 (IP) + 8 (UDP) = 42 bytes
  if (pkt_len < 42) {
    printk("UDP packet too small\n");
    return;
  }

  const uint8_t *eth_dst_mac = &pkt[0];
  const uint8_t *eth_src_mac = &pkt[6];
  const uint8_t *ip_src = &ip_hdr[12];
  const uint8_t *ip_dst = &ip_hdr[16];

  // Parse UDP header (starts at offset 34 = 14 + 20)
  const uint8_t *udp_hdr = &pkt[34];
  uint16_t udp_src_port = read_be16(&udp_hdr[0]);
  uint16_t udp_dst_port = read_be16(&udp_hdr[2]);
  uint16_t udp_length = read_be16(&udp_hdr[4]);

  // Check if it's for our echo port
  if (udp_dst_port != UDP_ECHO_PORT) {
    printk("UDP packet not for echo port (dst port ");
    printk_dec(udp_dst_port);
    printk("), dropping\n");
    return;
  }

  // Calculate payload length (UDP data)
  size_t udp_data_len = udp_length - 8; // UDP header is 8 bytes
  const uint8_t *udp_data = &pkt[42];   // 14 + 20 + 8

  // Log received packet with parsed information
  printk("Received UDP packet from ");
  printk_ip(ip_src);
  printk(":");
  printk_dec(udp_src_port);
  printk(" len=");
  printk_dec(udp_data_len);
  printk("\n");

  // Show data (first 32 bytes)
  if (udp_data_len > 0) {
    size_t show_len = udp_data_len;
    if (show_len > 32)
      show_len = 32;
    printk("Data: ");
    for (size_t i = 0; i < show_len; i++) {
      printk_hex8(udp_data[i]);
      if (i < show_len - 1)
        printk(" ");
    }
    if (udp_data_len > 32)
      printk(" ...");
    printk("\n");
  }

  // Construct UDP echo response
  // Check if send request is available (not already in use)
  if (user->udp_send_req.work.state != KWORK_STATE_DEAD) {
    printk("UDP send already in flight, dropping\n");
    return;
  }

  uint8_t *tx_pkt = user->udp_tx_buf;
  size_t tx_len = 0;

  // Ethernet header (14 bytes) - swap src/dst MACs
  memcpy(&tx_pkt[0], eth_src_mac, 6); // dst_mac = incoming src_mac
  memcpy(&tx_pkt[6], eth_dst_mac, 6); // src_mac = incoming dst_mac
  write_be16(&tx_pkt[12], ETHERTYPE_IPV4);
  tx_len += 14;

  // IPv4 header (20 bytes) - swap src/dst IPs
  memcpy(&tx_pkt[14], ip_hdr, 20); // Copy entire IP header
  // Swap source and destination IPs
  memcpy(&tx_pkt[14 + 12], ip_dst, 4); // src_ip = incoming dst_ip
  memcpy(&tx_pkt[14 + 16], ip_src, 4); // dst_ip = incoming src_ip
  // Zero out checksum field before recalculating
  tx_pkt[14 + 10] = 0;
  tx_pkt[14 + 11] = 0;
  // Calculate IP header checksum
  uint16_t ip_csum = ip_checksum(&tx_pkt[14], 20);
  write_be16(&tx_pkt[14 + 10], ip_csum);
  tx_len += 20;

  // UDP header (8 bytes) - swap src/dst ports
  write_be16(&tx_pkt[34], udp_dst_port); // src_port = incoming dst_port
  write_be16(&tx_pkt[36], udp_src_port); // dst_port = incoming src_port
  write_be16(&tx_pkt[38], udp_length);   // length stays the same
  write_be16(&tx_pkt[40], 0);            // checksum = 0 (optional for IPv4)
  tx_len += 8;

  // Copy UDP payload (echo the data back)
  memcpy(&tx_pkt[42], udp_data, udp_data_len);
  tx_len += udp_data_len;

  // Setup send request
  user->udp_tx_packet.buffer = tx_pkt;
  user->udp_tx_packet.buffer_size = tx_len;

  user->udp_send_req.work.op = KWORK_OP_NET_SEND;
  user->udp_send_req.work.callback = on_packet_sent;
  user->udp_send_req.work.ctx = user;
  user->udp_send_req.work.state = KWORK_STATE_DEAD;
  user->udp_send_req.work.flags = 0;
  user->udp_send_req.packets = &user->udp_tx_packet;
  user->udp_send_req.num_packets = 1;
  user->udp_send_req.packets_sent = 0;

  // Submit send request
  kerr_t err = ksubmit(user->kernel, &user->udp_send_req.work);
  if (err != KERR_OK) {
    printk("Network send submit failed: error ");
    printk_dec(err);
    printk("\n");
  } else {
    printk("Sent UDP response to ");
    printk_ip(ip_src);
    printk(":");
    printk_dec(udp_src_port);
    printk(" len=");
    printk_dec(udp_data_len);
    printk("\n");
  }
}

// Network packet received callback - dispatches to protocol handlers
static void on_packet_received(kwork_t *work) {
  KUNUSED(GATEWAY_IP);
  KUNUSED(GATEWAY_MAC);

  kuser_t *user = work->ctx;

  if (work->result != KERR_OK) {
    if (work->result != KERR_CANCELLED) {
      printk("Network recv failed: error ");
      printk_dec(work->result);
      printk("\n");
    }
    return;
  }

  knet_recv_req_t *req = CONTAINER_OF(work, knet_recv_req_t, work);
  knet_buffer_t *rx_buf = &req->buffers[req->buffer_index];

  // The virtio driver uses separate descriptors for header and data,
  // so rx_buf->buffer already points to the Ethernet frame (no header)
  const uint8_t *pkt = rx_buf->buffer;
  size_t pkt_len = rx_buf->packet_length;

  user->packets_received++;

  // Debug: Print first 20 bytes of Ethernet frame
  printk("RX (");
  printk_dec(pkt_len);
  printk("): ");
  for (size_t i = 0; i < pkt_len && i < 20; i++) {
    printk_hex8(pkt[i]);
    printk(" ");
  }
  printk("\n");

  // Minimum Ethernet frame: 14 bytes
  if (pkt_len < 14) {
    printk("Packet too small (");
    printk_dec(pkt_len);
    printk(" bytes), dropping\n");
    goto release;
  }

  // Parse Ethernet header (14 bytes)
  uint16_t ethertype = read_be16(&pkt[12]);

  // Dispatch based on ethertype
  if (ethertype == ETHERTYPE_ARP) {
    handle_arp_packet(user, pkt, pkt_len);
  } else if (ethertype == ETHERTYPE_IPV4) {
    // Minimum IPv4 packet: 14 (Ethernet) + 20 (IP) = 34 bytes
    if (pkt_len < 34) {
      printk("IPv4 packet too small\n");
      goto release;
    }

    // Parse IPv4 header (starts at offset 14)
    const uint8_t *ip_hdr = &pkt[14];
    uint8_t ip_version = (ip_hdr[0] >> 4) & 0x0F;
    uint8_t ip_ihl = ip_hdr[0] & 0x0F;
    uint8_t ip_protocol = ip_hdr[9];
    const uint8_t *ip_dst = &ip_hdr[16];

    // Validate IP version and header length
    if (ip_version != 4) {
      printk("Invalid IP version (");
      printk_dec(ip_version);
      printk("), dropping\n");
      goto release;
    }

    if (ip_ihl != 5) {
      printk("IP options not supported (IHL=");
      printk_dec(ip_ihl);
      printk("), dropping\n");
      goto release;
    }

    // Validate destination IP matches our address
    if (ip_dst[0] != DEVICE_IP[0] || ip_dst[1] != DEVICE_IP[1] ||
        ip_dst[2] != DEVICE_IP[2] || ip_dst[3] != DEVICE_IP[3]) {
      printk("Packet not for us (dst IP ");
      printk_ip(ip_dst);
      printk("), dropping\n");
      goto release;
    }

    // Dispatch based on IP protocol
    if (ip_protocol == IP_PROTOCOL_ICMP) {
      handle_icmp_packet(user, pkt, pkt_len, ip_hdr);
    } else if (ip_protocol == IP_PROTOCOL_UDP) {
      handle_udp_packet(user, pkt, pkt_len, ip_hdr);
    } else {
      printk("Unsupported IP protocol (");
      printk_dec(ip_protocol);
      printk("), dropping\n");
    }
  } else {
    printk("Unknown ethertype ");
    printk_hex16(ethertype);
    printk(", dropping\n");
  }

release:
  // Release buffer back to ring for reuse
  knet_buffer_release(user->kernel, req, req->buffer_index);
}

// Network packet sent callback
static void on_packet_sent(kwork_t *work) {
  kuser_t *user = work->ctx;

  if (work->result != KERR_OK) {
    printk("Network send failed: error ");
    printk_dec(work->result);
    printk("\n");
    return;
  }

  knet_send_req_t *req = CONTAINER_OF(work, knet_send_req_t, work);

  user->packets_sent++;

  printk("Packet sent (");
  printk_dec(user->packets_sent);
  printk("): ");
  printk_dec(req->packets_sent);
  printk(" packets, ");
  printk_dec(req->packets[0].buffer_size);
  printk(" bytes\n");
}

// Block device test callback
static void on_block_complete(kwork_t *work) {
  kuser_t *user = work->ctx;

  if (work->result != KERR_OK) {
    printk("Block operation failed: error ");
    printk_dec(work->result);
    printk("\n");
    return;
  }

  kblk_req_t *req = CONTAINER_OF(work, kblk_req_t, work);

  if (user->test_stage == 0) {
    // Stage 0: Read complete, check for magic bytes
    printk("Block read complete\n");

    uint32_t magic = (uint32_t)user->sector_buffer[0] |
                     ((uint32_t)user->sector_buffer[1] << 8) |
                     ((uint32_t)user->sector_buffer[2] << 16) |
                     ((uint32_t)user->sector_buffer[3] << 24);

    if (magic == 0x564D4F53) { // "VMOS" magic
      uint64_t timestamp = read_u64(&user->sector_buffer[4]);
      printk("Found existing magic: timestamp=");
      printk_dec(timestamp);
      printk("\n");
    } else {
      printk("No magic found, writing new magic\n");
    }

    // Stage 1: Write magic and new timestamp
    user->test_stage = 1;

    // Write magic bytes (0x564D4F53 = "VMOS")
    user->sector_buffer[0] = 0x53;
    user->sector_buffer[1] = 0x4F;
    user->sector_buffer[2] = 0x4D;
    user->sector_buffer[3] = 0x56;

    // Write current timestamp
    uint64_t now = get_timestamp_ms(user);
    write_u64(&user->sector_buffer[4], now);

    // Submit write
    req->work.op = KWORK_OP_BLOCK_WRITE;
    req->work.state = KWORK_STATE_DEAD;
    kerr_t err = ksubmit(user->kernel, &req->work);
    if (err != KERR_OK) {
      printk("Block write submit failed: error ");
      printk_dec(err);
      printk("\n");
    }
  } else if (user->test_stage == 1) {
    // Stage 1: Write complete, flush
    printk("Block write complete, flushing...\n");

    user->test_stage = 2;

    // Clear segment for flush (flush has no segments)
    req->segments = NULL;
    req->num_segments = 0;
    req->work.op = KWORK_OP_BLOCK_FLUSH;
    req->work.state = KWORK_STATE_DEAD;

    kerr_t err = ksubmit(user->kernel, &req->work);
    if (err != KERR_OK) {
      printk("Block flush submit failed: error ");
      printk_dec(err);
      printk("\n");
    }
  } else if (user->test_stage == 2) {
    // Stage 2: Flush complete, verify
    printk("Block flush complete, reading back...\n");

    user->test_stage = 3;

    // Clear buffer for verify read
    for (size_t i = 0; i < sizeof(user->sector_buffer); i++) {
      user->sector_buffer[i] = 0;
    }

    // Setup segment for read
    user->blk_segment.sector = 0;
    user->blk_segment.buffer = user->sector_buffer;
    user->blk_segment.num_sectors = 1;
    user->blk_segment.completed_sectors = 0;

    req->segments = &user->blk_segment;
    req->num_segments = 1;
    req->work.op = KWORK_OP_BLOCK_READ;
    req->work.state = KWORK_STATE_DEAD;

    kerr_t err = ksubmit(user->kernel, &req->work);
    if (err != KERR_OK) {
      printk("Block verify read submit failed: error ");
      printk_dec(err);
      printk("\n");
    }
  } else if (user->test_stage == 3) {
    // Stage 3: Verify read complete
    printk("Block verify read complete\n");

    uint32_t magic = (uint32_t)user->sector_buffer[0] |
                     ((uint32_t)user->sector_buffer[1] << 8) |
                     ((uint32_t)user->sector_buffer[2] << 16) |
                     ((uint32_t)user->sector_buffer[3] << 24);

    uint64_t timestamp = read_u64(&user->sector_buffer[4]);

    if (magic == 0x564D4F53) {
      printk("Verified magic and timestamp=");
      printk_dec(timestamp);
      printk("\n");
      printk("Block device test PASSED\n");
    } else {
      printk("Verification failed: magic mismatch\n");
      printk("Block device test FAILED\n");
    }
  }
}

// User entry point
void kmain_usermain(kuser_t *user) {
  printk("kmain_usermain: Requesting 32 random bytes...\n");

  // Setup RNG request
  user->rng_req.work.op = KWORK_OP_RNG_READ;
  user->rng_req.work.callback = on_random_ready;
  user->rng_req.work.ctx = user;
  user->rng_req.work.state = KWORK_STATE_DEAD;
  user->rng_req.work.flags = 0;
  user->rng_req.buffer = user->random_buf;
  user->rng_req.length = 32;
  user->rng_req.completed = 0;

  // Submit work
  kerr_t err = ksubmit(user->kernel, &user->rng_req.work);
  if (err != KERR_OK) {
    printk("ksubmit failed: error ");
    printk_dec(err);
    printk("\n");
  } else {
    printk("RNG request submitted\n");
  }

  // Setup block device test (stage 0: initial read)
  printk("kmain_usermain: Starting block device test...\n");

  user->test_stage = 0;

  user->blk_segment.sector = 0;
  user->blk_segment.buffer = user->sector_buffer;
  user->blk_segment.num_sectors = 1;
  user->blk_segment.completed_sectors = 0;

  user->blk_req.work.op = KWORK_OP_BLOCK_READ;
  user->blk_req.work.callback = on_block_complete;
  user->blk_req.work.ctx = user;
  user->blk_req.work.state = KWORK_STATE_DEAD;
  user->blk_req.work.flags = 0;
  user->blk_req.segments = &user->blk_segment;
  user->blk_req.num_segments = 1;

  err = ksubmit(user->kernel, &user->blk_req.work);
  if (err != KERR_OK) {
    printk("Block request submit failed: error ");
    printk_dec(err);
    printk("\n");
  } else {
    printk("Block request submitted\n");
  }

  // Setup network device test (continuous packet reception)
  printk("kmain_usermain: Starting network packet reception...\n");

  // Initialize receive buffers
  user->net_rx_bufs[0].buffer = user->net_rx_buf0;
  user->net_rx_bufs[0].buffer_size = 1514;
  user->net_rx_bufs[0].packet_length = 0;

  user->net_rx_bufs[1].buffer = user->net_rx_buf1;
  user->net_rx_bufs[1].buffer_size = 1514;
  user->net_rx_bufs[1].packet_length = 0;

  user->net_rx_bufs[2].buffer = user->net_rx_buf2;
  user->net_rx_bufs[2].buffer_size = 1514;
  user->net_rx_bufs[2].packet_length = 0;

  user->net_rx_bufs[3].buffer = user->net_rx_buf3;
  user->net_rx_bufs[3].buffer_size = 1514;
  user->net_rx_bufs[3].packet_length = 0;

  // Memory barrier before setting counters to prevent optimization issues
  KLOG("x user ptr=");
  printk_hex64((uintptr_t)user);
  printk("\n");
  user->packets_received = 0;
  user->packets_sent = 0;
  KLOG("x");

  // Setup standing receive request
  user->net_recv_req.work.op = KWORK_OP_NET_RECV;
  user->net_recv_req.work.callback = on_packet_received;
  user->net_recv_req.work.ctx = user;
  user->net_recv_req.work.state = KWORK_STATE_DEAD;
  user->net_recv_req.work.flags = KWORK_FLAG_STANDING; // Keep LIVE
  user->net_recv_req.buffers = user->net_rx_bufs;
  user->net_recv_req.num_buffers = 4;
  user->net_recv_req.buffer_index = 0;

  err = ksubmit(user->kernel, &user->net_recv_req.work);
  if (err != KERR_OK) {
    printk("Network recv submit failed: error ");
    printk_dec(err);
    printk("\n");
  } else {
    printk("Network recv request submitted (4 buffers)\n");
  }

  // Send gratuitous ARP to announce our presence
  // This pre-populates QEMU's ARP cache, eliminating delay on first packet
  printk("Sending gratuitous ARP...\n");

  uint8_t *tx_pkt = user->arp_tx_buf;
  size_t tx_len = 0;
  const uint8_t *device_mac = user->kernel->platform.net_mac_address;
  const uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

  // Ethernet header (14 bytes) - broadcast
  memcpy(&tx_pkt[0], broadcast_mac, 6);  // dst_mac = broadcast
  memcpy(&tx_pkt[6], device_mac, 6);     // src_mac = our MAC
  write_be16(&tx_pkt[12], ETHERTYPE_ARP);
  tx_len += 14;

  // ARP packet (28 bytes) - gratuitous ARP
  write_be16(&tx_pkt[14], ARP_HTYPE_ETHERNET);  // Hardware type
  write_be16(&tx_pkt[16], ARP_PTYPE_IPV4);      // Protocol type
  tx_pkt[18] = 6;                               // Hardware address length
  tx_pkt[19] = 4;                               // Protocol address length
  write_be16(&tx_pkt[20], ARP_OPER_REQUEST);    // Operation (request)
  memcpy(&tx_pkt[22], device_mac, 6);           // Sender MAC = our MAC
  memcpy(&tx_pkt[28], DEVICE_IP, 4);            // Sender IP = our IP
  memcpy(&tx_pkt[32], broadcast_mac, 6);        // Target MAC = broadcast
  memcpy(&tx_pkt[36], DEVICE_IP, 4);            // Target IP = our IP (gratuitous)
  tx_len += 28;

  // Setup send request for gratuitous ARP
  user->arp_tx_packet.buffer = tx_pkt;
  user->arp_tx_packet.buffer_size = tx_len;

  user->arp_send_req.work.op = KWORK_OP_NET_SEND;
  user->arp_send_req.work.callback = on_packet_sent;
  user->arp_send_req.work.ctx = user;
  user->arp_send_req.work.state = KWORK_STATE_DEAD;
  user->arp_send_req.work.flags = 0;
  user->arp_send_req.packets = &user->arp_tx_packet;
  user->arp_send_req.num_packets = 1;
  user->arp_send_req.packets_sent = 0;

  err = ksubmit(user->kernel, &user->arp_send_req.work);
  if (err != KERR_OK) {
    printk("Gratuitous ARP send failed: error ");
    printk_dec(err);
    printk("\n");
  } else {
    printk("Gratuitous ARP sent (announcing ");
    printk_ip(DEVICE_IP);
    printk(" at ");
    printk_mac(device_mac);
    printk(")\n");
  }
}
