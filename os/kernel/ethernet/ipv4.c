#include "ipv4.h"
#include "../firewall/firewall.h"
#include "../mm/kmalloc.h"
#include "../string.h"
#include "arp.h"
#include "tcp.h"
#include "udp.h"

extern void serial(const char *fmt, ...);

static icmp_callback_t icmp_cb = 0;

void ipv4_set_icmp_callback(icmp_callback_t callback) { icmp_cb = callback; }

static uint16_t ip_checksum(void *vdata, size_t length) {
  char *data = (char *)vdata;
  uint32_t acc = 0;
  for (size_t i = 0; i + 1 < length; i += 2) {
    uint16_t word;
    memcpy(&word, data + i, 2);
    acc += ntohs(word);
  }
  if (length & 1) {
    uint16_t word = 0;
    memcpy(&word, data + length - 1, 1);
    acc += ntohs(word);
  }
  while (acc >> 16)
    acc = (acc & 0xFFFF) + (acc >> 16);
  return htons(~acc);
}

void ipv4_handle_packet(net_device_t *dev, const void *data, size_t len) {
  if (len < sizeof(ipv4_header_t))
    return;

  ipv4_header_t *hdr = (ipv4_header_t *)data;
  if (hdr->version != 4)
    return;

  uint32_t src = hdr->src;
  /* serial("[IP] Packet from %d.%d.%d.%d proto=%d\n",
         src & 0xFF, (src>>8)&0xFF, (src>>16)&0xFF, (src>>24)&0xFF, hdr->proto);
   */

  size_t header_len = hdr->ihl * 4;
  void *payload = (uint8_t *)data + header_len;
  size_t payload_len = ntohs(hdr->len) - header_len;

  if (firewall_check_packet(hdr->src, hdr->dst, hdr->proto, payload,
                            payload_len) == FIREWALL_ACTION_DROP) {
    return; /* Packet dropped by firewall */
  }

  if (hdr->proto == IP_PROTO_UDP) {
    udp_handle_packet(dev, src, payload, payload_len);
  } else if (hdr->proto == IP_PROTO_TCP) {
    tcp_handle_packet(dev, src, payload, payload_len);
  } else if (hdr->proto == IP_PROTO_ICMP) {
    if (icmp_cb) {
      icmp_cb(src, (const uint8_t *)payload, payload_len);
    }

    /* Log ICMP packets (e.g. Ping Reply) */
    serial("[IP] ICMP Packet from %d.%d.%d.%d len=%d\n", src & 0xFF,
           (src >> 8) & 0xFF, (src >> 16) & 0xFF, (src >> 24) & 0xFF,
           payload_len);
    /* TODO: Handle Echo Request here if we want to reply to pings */
  }
}

int ipv4_send(net_device_t *dev, uint32_t dst_ip, uint8_t proto,
              const void *data, size_t len) {
  uint8_t dst_mac[6];

  uint32_t next_hop = dst_ip;

  /* Check if destination is in local subnet */
  if ((dst_ip & dev->subnet) != (dev->ip & dev->subnet)) {
    next_hop = dev->gateway;
  }

  /* Handle Broadcast */
  if (dst_ip == 0xFFFFFFFF || dst_ip == 0) {
    memset(dst_mac, 0xFF, 6);
  } else {
    if (!arp_lookup(next_hop, dst_mac)) {
      serial("[IP] ARP miss for %x (next hop), sending request...\n", next_hop);
      arp_send_request(dev, next_hop);
      return -1; /* Packet dropped, retry later */
    }
  }

  size_t total_len = sizeof(ipv4_header_t) + len;
  uint8_t *packet = (uint8_t *)kmalloc(total_len);
  if (!packet)
    return -1;

  ipv4_header_t *hdr = (ipv4_header_t *)packet;
  hdr->version = 4;
  hdr->ihl = 5;
  hdr->tos = 0;
  hdr->len = htons(total_len);
  hdr->id = htons(0x1234);
  hdr->frag_offset = 0;
  hdr->ttl = 64;
  hdr->proto = proto;
  hdr->src = dev->ip;
  hdr->dst = dst_ip;
  hdr->checksum = 0;
  hdr->checksum = ip_checksum(hdr, sizeof(ipv4_header_t));

  memcpy(packet + sizeof(ipv4_header_t), data, len);

  eth_send(dev, dst_mac, ETH_TYPE_IP, packet, total_len);
  kfree(packet);
  return 0;
}