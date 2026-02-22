#pragma once
#include "../ethernet/ipv4.h"
#include "../ethernet/tcp.h"
#include "../ethernet/udp.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Action to take on rule match
typedef enum {
  FIREWALL_ACTION_ALLOW = 0,
  FIREWALL_ACTION_DROP = 1
} firewall_action_t;

// Structure for a firewall rule
typedef struct {
  uint32_t id;              // Unique rule ID
  firewall_action_t action; // Allow or Drop

  uint8_t proto; // IP protocol (IP_PROTO_TCP, IP_PROTO_UDP, IP_PROTO_ICMP, or 0
                 // for any)

  uint32_t src_ip;   // Source IP (0 for any)
  uint32_t src_mask; // Subnet mask for src_ip (e.g. 0xFFFFFFFF for single host,
                     // 0 for any)

  uint32_t dst_ip;   // Dest IP (0 for any)
  uint32_t dst_mask; // Subnet mask for dst_ip

  uint16_t src_port; // Source port (0 for any) (TCP/UDP only)
  uint16_t dst_port; // Dest port (0 for any) (TCP/UDP only)

  int active; // 1 if rule is active, 0 if empty/deleted
} firewall_rule_t;

// Process an incoming IPv4 packet against firewall rules
// Returns FIREWALL_ACTION_ALLOW to let packet pass, or FIREWALL_ACTION_DROP to
// drop it
firewall_action_t firewall_check_packet(uint32_t src_ip, uint32_t dst_ip,
                                        uint8_t proto, const void *payload,
                                        size_t payload_len);

// Manage rules from user-space / command line
int firewall_add_rule(const firewall_rule_t *rule);
int firewall_delete_rule(uint32_t id);
int firewall_get_rules(firewall_rule_t *out_rules, int max_rules);
void firewall_clear_all();

#ifdef __cplusplus
}
#endif
