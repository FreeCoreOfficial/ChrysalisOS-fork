#include "../../drivers/rtc.h"
#include "../i386/io.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg) {
  outb(CMOS_ADDR, reg);
  return inb(CMOS_DATA);
}

static int rtc_updating(void) {
  outb(CMOS_ADDR, 0x0A);
  return inb(CMOS_DATA) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t v) {
  return (v & 0x0F) + ((v / 16) * 10);
}

void rtc_read(struct rtc_time *t) {
  uint8_t sec, min, hour, day, mon, year;
  uint8_t regB;

  while (rtc_updating())
    ;

  sec = cmos_read(0x00);
  min = cmos_read(0x02);
  hour = cmos_read(0x04);
  day = cmos_read(0x07);
  mon = cmos_read(0x08);
  year = cmos_read(0x09);

  regB = cmos_read(0x0B);

  if (!(regB & 0x04)) {
    sec = bcd_to_bin(sec);
    min = bcd_to_bin(min);
    hour = bcd_to_bin(hour);
    day = bcd_to_bin(day);
    mon = bcd_to_bin(mon);
    year = bcd_to_bin(year);
  }

  t->second = sec;
  t->minute = min;
  t->hour = hour;
  t->day = day;
  t->month = mon;
  t->year = 2000 + year;
}
