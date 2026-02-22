#include "../string.h"
#include "../terminal.h"
#include "firewall.h"
extern "C" int atoi(const char *str);
extern "C" uint32_t
parse_ipv4(const char *
               ip_str); // assuming a helper exists or we implement a simple one

// Simple IP parser since we're in kernel space
static uint32_t parse_ip(const char *str) {
  uint32_t ip = 0;
  int octets = 0;
  while (*str) {
    int val = 0;
    while (*str >= '0' && *str <= '9') {
      val = val * 10 + (*str - '0');
      str++;
    }
    ip |= (val & 0xFF) << (octets * 8);
    octets++;
    if (*str == '.')
      str++;
    else
      break;
  }
  return ip;
}

extern "C" int cmd_bridge(int argc, char **argv) {
  if (argc < 2) {
    terminal_printf("Usage: bridge [add|del|list|clear]\n");
    terminal_printf(
        "  bridge add <allow|drop> <tcp|udp|icmp|any> [src_ip] [dst_ip] "
        "[dst_port]\n");
    terminal_printf("  bridge del <id>\n");
    terminal_printf("  bridge clear\n");
    return 0;
  }

  if (strcmp(argv[1], "list") == 0) {
    firewall_rule_t rules[128];
    int count = firewall_get_rules(rules, 128);
    terminal_printf("Firewall rules (%d):\n", count);
    for (int i = 0; i < count; i++) {
      firewall_rule_t *r = &rules[i];
      const char *act = (r->action == FIREWALL_ACTION_ALLOW) ? "ALLOW" : "DROP";
      const char *proto = "ANY";
      if (r->proto == 6)
        proto = "TCP";
      else if (r->proto == 17)
        proto = "UDP";
      else if (r->proto == 1)
        proto = "ICMP";

      terminal_printf(
          "  [%d] %s %s src=%d.%d.%d.%d dst=%d.%d.%d.%d dst_port=%d\n", r->id,
          act, proto, r->src_ip & 0xFF, (r->src_ip >> 8) & 0xFF,
          (r->src_ip >> 16) & 0xFF, (r->src_ip >> 24) & 0xFF, r->dst_ip & 0xFF,
          (r->dst_ip >> 8) & 0xFF, (r->dst_ip >> 16) & 0xFF,
          (r->dst_ip >> 24) & 0xFF, r->dst_port);
    }
    return 0;
  }

  if (strcmp(argv[1], "clear") == 0) {
    firewall_clear_all();
    terminal_printf("Firewall rules cleared.\n");
    return 0;
  }

  if (strcmp(argv[1], "del") == 0 && argc == 3) {
    int id = atoi(argv[2]);
    if (firewall_delete_rule(id) == 0) {
      terminal_printf("Rule %d deleted.\n", id);
    } else {
      terminal_printf("Rule %d not found.\n", id);
    }
    return 0;
  }

  if (strcmp(argv[1], "add") == 0 && argc >= 4) {
    firewall_rule_t r;
    memset(&r, 0, sizeof(r));

    if (strcmp(argv[2], "allow") == 0)
      r.action = FIREWALL_ACTION_ALLOW;
    else if (strcmp(argv[2], "drop") == 0)
      r.action = FIREWALL_ACTION_DROP;
    else {
      terminal_printf("Invalid action. Must be 'allow' or 'drop'.\n");
      return 1;
    }

    if (strcmp(argv[3], "tcp") == 0)
      r.proto = 6;
    else if (strcmp(argv[3], "udp") == 0)
      r.proto = 17;
    else if (strcmp(argv[3], "icmp") == 0)
      r.proto = 1;
    else if (strcmp(argv[3], "any") == 0)
      r.proto = 0;
    else {
      terminal_printf(
          "Invalid protocol. Must be 'tcp', 'udp', 'icmp', or 'any'.\n");
      return 1;
    }

    if (argc >= 5 && strcmp(argv[4], "any") != 0 && strcmp(argv[4], "0") != 0) {
      r.src_ip = parse_ip(argv[4]);
      r.src_mask = 0xFFFFFFFF;
    }

    if (argc >= 6 && strcmp(argv[5], "any") != 0 && strcmp(argv[5], "0") != 0) {
      r.dst_ip = parse_ip(argv[5]);
      r.dst_mask = 0xFFFFFFFF;
    }

    if (argc >= 7) {
      r.dst_port = atoi(argv[6]);
    }

    int id = firewall_add_rule(&r);
    if (id > 0)
      terminal_printf("Rule added with ID %d.\n", id);
    else
      terminal_printf("Error adding rule. Limit reached?\n");
    return 0;
  }

  terminal_printf("Invalid usage.\n");
  return 1;
}
