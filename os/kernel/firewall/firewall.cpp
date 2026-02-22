#include "firewall.h"
#include "../string.h"

extern "C" void serial(const char *fmt, ...);

#define MAX_FIREWALL_RULES 128

static firewall_rule_t rules[MAX_FIREWALL_RULES];
static uint32_t next_rule_id = 1;
static bool firewall_initialized = false;

static void init_firewall() {
  if (firewall_initialized)
    return;
  memset(rules, 0, sizeof(rules));
  firewall_initialized = true;
}

// Check if an IP matches a rule's IP/mask
static inline bool ip_matches(uint32_t ip, uint32_t rule_ip,
                              uint32_t rule_mask) {
  if (rule_mask == 0)
    return true; // Any IP
  return (ip & rule_mask) == (rule_ip & rule_mask);
}

extern "C" firewall_action_t
firewall_check_packet(uint32_t src_ip, uint32_t dst_ip, uint8_t proto,
                      const void *payload, size_t payload_len) {
  if (!firewall_initialized)
    init_firewall();

  uint16_t src_port = 0;
  uint16_t dst_port = 0;

  // Extract ports if TCP or UDP
  if (proto == IP_PROTO_TCP && payload_len >= 4) {
    const uint16_t *ports = (const uint16_t *)payload;
    src_port = ntohs(ports[0]);
    dst_port = ntohs(ports[1]);
  } else if (proto == IP_PROTO_UDP && payload_len >= 4) {
    const uint16_t *ports = (const uint16_t *)payload;
    src_port = ntohs(ports[0]);
    dst_port = ntohs(ports[1]);
  }

  // Default policy: ALLOW
  firewall_action_t final_action = FIREWALL_ACTION_ALLOW;

  // Evaluate rules in order
  for (int i = 0; i < MAX_FIREWALL_RULES; i++) {
    if (!rules[i].active)
      continue;

    firewall_rule_t *r = &rules[i];

    // Protocol check
    if (r->proto != 0 && r->proto != proto)
      continue;

    // IP checks
    if (!ip_matches(src_ip, r->src_ip, r->src_mask))
      continue;
    if (!ip_matches(dst_ip, r->dst_ip, r->dst_mask))
      continue;

    // Port checks (only relevant if it's TCP/UDP, but we enforce if rule
    // specifies it)
    if (r->src_port != 0 && r->src_port != src_port)
      continue;
    if (r->dst_port != 0 && r->dst_port != dst_port)
      continue;

    // Rule matched! Update action
    final_action = r->action;

    // Stop evaluating on first match (typical firewall behavior)
    break;
  }

  if (final_action == FIREWALL_ACTION_DROP) {
    serial(
        "[FIREWALL] Dropped packet src=%d.%d.%d.%d:%d -> dst=*:%d proto=%d\n",
        src_ip & 0xFF, (src_ip >> 8) & 0xFF, (src_ip >> 16) & 0xFF,
        (src_ip >> 24) & 0xFF, src_port, dst_port, proto);
  }

  return final_action;
}

extern "C" int firewall_add_rule(const firewall_rule_t *rule) {
  if (!firewall_initialized)
    init_firewall();
  if (!rule)
    return -1;

  for (int i = 0; i < MAX_FIREWALL_RULES; i++) {
    if (!rules[i].active) {
      rules[i] = *rule;
      rules[i].id = next_rule_id++;
      rules[i].active = 1;
      return rules[i].id;
    }
  }
  return -1; // No space
}

extern "C" int firewall_delete_rule(uint32_t id) {
  if (!firewall_initialized)
    return -1;
  for (int i = 0; i < MAX_FIREWALL_RULES; i++) {
    if (rules[i].active && rules[i].id == id) {
      rules[i].active = 0;
      return 0; // Success
    }
  }
  return -1; // Not found
}

extern "C" int firewall_get_rules(firewall_rule_t *out_rules, int max_rules) {
  if (!firewall_initialized || !out_rules)
    return 0;
  int count = 0;
  for (int i = 0; i < MAX_FIREWALL_RULES && count < max_rules; i++) {
    if (rules[i].active) {
      out_rules[count++] = rules[i];
    }
  }
  return count;
}

extern "C" void firewall_clear_all() {
  if (!firewall_initialized)
    return;
  memset(rules, 0, sizeof(rules));
}
