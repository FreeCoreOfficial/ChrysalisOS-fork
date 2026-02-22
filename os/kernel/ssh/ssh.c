#include "ssh.h"
#include "../cmds/registry.h"
#include "../ethernet/eth.h"
#include "../ethernet/ipv4.h"
#include "../ethernet/net.h"
#include "../ethernet/tcp.h"
#include "../string.h"
#include "../terminal.h"

extern void serial(const char *fmt, ...);

typedef enum { SSH_LISTEN, SSH_SYN_RCVD, SSH_ESTABLISHED } ssh_state_t;

static ssh_state_t state = SSH_LISTEN;
static uint32_t active_ip = 0;
static uint16_t active_port = 0;
static uint32_t current_seq = 0;
static uint32_t current_ack = 0;
static net_device_t *current_dev = 0;

static char line_buf[256];
static int line_len = 0;

static char capture_buf[4096];
static size_t capture_len = 0;

static void ssh_send(uint8_t flags, const void *data, size_t len) {
  if (!current_dev)
    return;
  tcp_send_packet(current_dev, active_ip, 22, active_port, current_seq,
                  current_ack, flags, data, len);
  if (len > 0) {
    current_seq += len;
  }
  // For SYN or FIN, seq is incremented, but we handle it manually
}

static void execute_command(char *cmd_str) {
  char *argv[32];
  int argc = 0;
  char *p = cmd_str;

  while (*p && argc < 32) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (!*p)
      break;

    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t')
      p++;
    if (*p)
      *p++ = 0;
  }

  if (argc == 0)
    return;

  if (strcmp(argv[0], "exit") == 0) {
    // Send FIN
    ssh_send(TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    state = SSH_LISTEN;
    return;
  }

  int found = 0;
  terminal_start_capture(capture_buf, sizeof(capture_buf) - 1, &capture_len);
  for (int i = 0; i < command_count; i++) {
    if (strcmp(command_table[i].name, argv[0]) == 0) {
      command_table[i].func(argc, argv);
      found = 1;
      break;
    }
  }
  terminal_end_capture();

  if (!found) {
    const char *msg = "Unknown command\n";
    ssh_send(TCP_FLAG_PSH | TCP_FLAG_ACK, msg, strlen(msg));
  } else if (capture_len > 0) {
    ssh_send(TCP_FLAG_PSH | TCP_FLAG_ACK, capture_buf, capture_len);
  }
}

#include "../ethernet/net_device.h"

static void ssh_tcp_callback(uint32_t src_ip, uint16_t src_port,
                             uint16_t dst_port, uint8_t flags, uint32_t seq,
                             uint32_t ack, const uint8_t *data, size_t len) {
  if (dst_port != 22)
    return;

  current_dev = net_get_primary_device();
  if (!current_dev)
    return;

  if (state == SSH_LISTEN) {
    if (flags & TCP_FLAG_SYN) {
      active_ip = src_ip;
      active_port = src_port;
      current_ack = seq + 1;
      current_seq = 1000;
      ssh_send(TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0);
      current_seq++;
      state = SSH_SYN_RCVD;
      serial("[SSH] Connection from %d.%d.%d.%d:%d. Sent SYN-ACK\n",
             src_ip & 0xFF, (src_ip >> 8) & 0xFF, (src_ip >> 16) & 0xFF,
             (src_ip >> 24) & 0xFF, src_port);
    }
  } else if (state == SSH_SYN_RCVD && src_ip == active_ip &&
             src_port == active_port) {
    if (flags & TCP_FLAG_ACK) {
      state = SSH_ESTABLISHED;
      serial("[SSH] Connection ESTABLISHED\n");
      const char *welcome = "Welcome to Chrysalis OS Remote Shell!\n# ";
      ssh_send(TCP_FLAG_PSH | TCP_FLAG_ACK, welcome, strlen(welcome));
    }
  } else if (state == SSH_ESTABLISHED && src_ip == active_ip &&
             src_port == active_port) {
    if (len > 0) {
      current_ack = seq + len;
      int cmd_executed = 0;
      for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n' || c == '\r') {
          line_buf[line_len] = 0;
          if (line_len > 0) {
            execute_command(line_buf);
            const char *prompt = "\n# ";
            ssh_send(TCP_FLAG_PSH | TCP_FLAG_ACK, prompt, strlen(prompt));
            cmd_executed = 1;
          }
          line_len = 0;
        } else if (c == '\b' || c == 0x7F) {
          if (line_len > 0)
            line_len--;
        } else if (c >= 32 && c <= 126 &&
                   line_len < (int)sizeof(line_buf) - 1) {
          line_buf[line_len++] = c;
        }
      }
      if (!cmd_executed) {
        ssh_send(TCP_FLAG_ACK, 0, 0);
      }
    }
    if (flags & TCP_FLAG_FIN) {
      current_ack = seq + 1;
      ssh_send(TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
      state = SSH_LISTEN;
      serial("[SSH] Connection CLOSED by client.\n");
    }
  }
}

void ssh_init() {
  state = SSH_LISTEN;
  tcp_set_callback(ssh_tcp_callback);
  serial("[SSH] Initialized, listening on port 22\n");
}
