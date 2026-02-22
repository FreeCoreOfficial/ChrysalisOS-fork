#include "tcp.h"
#include "../mm/kmalloc.h"
#include "../string.h"
#include "eth.h"
#include "ipv4.h"

extern void serial(const char *fmt, ...);

static tcp_callback_t tcp_cb = 0;

void tcp_set_callback(tcp_callback_t callback) { tcp_cb = callback; }

typedef struct {
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t seq;
  uint32_t ack;
  uint8_t offset_reserved;
  uint8_t flags;
  uint16_t window;
  uint16_t checksum;
  uint16_t urgent_ptr;
} __attribute__((packed)) tcp_header_t;

static uint16_t tcp_checksum(const void *data, size_t len, uint32_t src_ip,
                             uint32_t dst_ip) {
  uint32_t sum = 0;
  const uint16_t *ptr = (const uint16_t *)data;

  /* Pseudo Header - Treat IPs as 16-bit words to match data summation */
  const uint16_t *src = (const uint16_t *)&src_ip;
  const uint16_t *dst = (const uint16_t *)&dst_ip;

  sum += src[0];
  sum += src[1];
  sum += dst[0];
  sum += dst[1];

  sum += htons(IP_PROTO_TCP);
  sum += htons(len);

  /* TCP Header + Data */
  for (size_t i = 0; i < len / 2; i++) {
    sum += ptr[i];
  }

  if (len & 1) {
    uint16_t word = 0;
    memcpy(&word, (const uint8_t *)data + len - 1, 1);
    sum += word;
  }

  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }

  return ~sum;
}

void tcp_handle_packet(net_device_t *dev, uint32_t src_ip, const void *data,
                       size_t len) {
  (void)dev;
  if (len < sizeof(tcp_header_t))
    return;

  const tcp_header_t *hdr = (const tcp_header_t *)data;
  serial("[TCP] packet from %x, src_port=%d dst_port=%d len=%d\n", src_ip,
         ntohs(hdr->src_port), ntohs(hdr->dst_port), len);

  if (tcp_cb) {
    size_t header_len = ((hdr->offset_reserved >> 4) * 4);
    if (len < header_len)
      return;

    tcp_cb(src_ip, ntohs(hdr->src_port), ntohs(hdr->dst_port), hdr->flags,
           ntohl(hdr->seq), ntohl(hdr->ack), (const uint8_t *)data + header_len,
           len - header_len);
  }
}

int tcp_send_packet(net_device_t *dev, uint32_t dst_ip, uint16_t src_port,
                    uint16_t dst_port, uint32_t seq, uint32_t ack,
                    uint8_t flags, const void *data, size_t len) {
  size_t total_len = sizeof(tcp_header_t) + len;
  uint8_t *packet = (uint8_t *)kmalloc(total_len);
  if (!packet)
    return -1;

  memset(packet, 0, total_len);
  tcp_header_t *hdr = (tcp_header_t *)packet;

  hdr->src_port = htons(src_port);
  hdr->dst_port = htons(dst_port);
  hdr->seq = htonl(seq);
  hdr->ack = htonl(ack);
  hdr->offset_reserved = (sizeof(tcp_header_t) / 4) << 4;
  hdr->flags = flags;
  hdr->window = htons(8192);
  hdr->checksum = 0;
  hdr->urgent_ptr = 0;

  if (data && len > 0) {
    memcpy(packet + sizeof(tcp_header_t), data, len);
  }

  /* Checksum calculation */
  hdr->checksum = tcp_checksum(packet, total_len, dev->ip, dst_ip);

  int ret = ipv4_send(dev, dst_ip, IP_PROTO_TCP, packet, total_len);
  kfree(packet);
  return ret;
}