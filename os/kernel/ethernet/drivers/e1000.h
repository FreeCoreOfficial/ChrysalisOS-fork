#pragma once
#include <stdint.h>

#define E1000_CTRL     0x0000
#define E1000_STATUS   0x0008
#define E1000_EEPROM   0x0014
#define E1000_ICR      0x00C0
#define E1000_IMS      0x00D0
#define E1000_RCTL     0x0100
#define E1000_TCTL     0x0400
#define E1000_RDBAL    0x2800
#define E1000_RDBAH    0x2804
#define E1000_RDLEN    0x2808
#define E1000_RDH      0x2810
#define E1000_RDT      0x2818
#define E1000_TDBAL    0x3800
#define E1000_TDBAH    0x3804
#define E1000_TDLEN    0x3808
#define E1000_TDH      0x3810
#define E1000_TDT      0x3818
#define E1000_TIPG     0x0410
#define E1000_MTA      0x5200

#define RCTL_EN        (1 << 1)
#define RCTL_SBP       (1 << 2)
#define RCTL_UPE       (1 << 3)
#define RCTL_MPE       (1 << 4)
#define RCTL_LPE       (1 << 5)
#define RCTL_BAM       (1 << 15)

#define TCTL_EN        (1 << 1)
#define TCTL_PSP       (1 << 3)

int e1000_init(void);