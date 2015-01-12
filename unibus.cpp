#include <stdio.h>
#include <SdFat.h>
#include <setjmp.h>
#include "avr11.h"
namespace unibus {

#ifdef __ATMEGA2560__
#include "xmem.h"

// memory as words
int *intptr = reinterpret_cast<int *>(0x2200);
// memory as bytes
char *charptr = reinterpret_cast<char *>(0x2200);
#endif

// memory as words
uint16_t intptr[MEMSIZE >> 1];

char *charptr = reinterpret_cast<char *>(&intptr);

uint16_t read8(const uint32_t a) {
  if (a & 1) {
    return read16(a & ~1) >> 8;
  }
  return read16(a & ~1) & 0xFF;
}

void write8(const uint32_t a, const uint16_t v) {
  if (a < MEMSIZE) {
    //xmem::setMemoryBank((a >> 15) & 0xf, false);
    //charptr[(a & 0x7fff)] = v & 0xff;
    charptr[a] = v;
    return;
  }
  if (a & 1) {
    write16(a & ~1, (read16(a) & 0xFF) | (v & 0xFF) << 8);
  } else {
    write16(a & ~1, (read16(a) & 0xFF00) | (v & 0xFF));
  }
}

void write16(const uint32_t a, const uint16_t v) {
  if (a & 1) {
    printf("unibus: write16 to odd address %06o\n", a);
    trap(INTBUS);
  }
  if (a < MEMSIZE) {
    //xmem::setMemoryBank((a >> 15) & 0xf, false);
    //intptr[(a & 0x7fff) >> 1] = v;
    intptr[a >> 1] = v;
    return;
  }
  switch (a) {
    case 0777776:
      switch (v >> 14) {
        case 0:
          cpu::switchmode(false);
          break;
        case 3:
          cpu::switchmode(true);
          break;
        default:
          printf("invalid mode\n");
          panic();
      }
      switch ((v >> 12) & 3) {
        case 0:
          cpu::prevuser = false;
          break;
        case 3:
          cpu::prevuser = true;
          break;
        default:
          printf("invalid mode\n");
          panic();
      }
      cpu::PS = v;
      return;
    case 0777546:
      cpu::LKS = v;
      return;
    case 0777572:
      mmu::SR0 = v;
      return;
  }
  if ((a & 0777770) == 0777560) {
    cons::write16(a, v);
    return;
  }
  if ((a & 0777700) == 0777400) {
    rk11::write16(a, v);
    return;
  }
  if (((a & 0777600) == 0772200) || ((a & 0777600) == 0777600)) {
    mmu::write16(a, v);
    return;
  }
  printf("unibus: write to invalid address %06o\n", a);
  trap(INTBUS);
}

uint16_t read16(const uint32_t a) {
  if (a & 1) {
    printf("unibus: read16 from odd address %06o\n", a);
    trap(INTBUS);
  }
  if (a < MEMSIZE) {
    //xmem::setMemoryBank((a >> 15) & 0xf, false);
    return intptr[a >> 1]; // intptr[(a & 0x7fff) >> 1];
  }

  if (a == 0777546) {
    return cpu::LKS;
  }

  if (a == 0777570) {
    return 0173030;
  }

  if (a == 0777572) {
    return mmu::SR0;
  }

  if (a == 0777576) {
    return mmu::SR2;
  }

  if (a == 0777776) {
    return cpu::PS;
  }

  if ((a & 0777770) == 0777560) {
    return cons::read16(a);
  }

  if ((a & 0777760) == 0777400) {
    return rk11::read16(a);
  }

  if (((a & 0777600) == 0772200) || ((a & 0777600) == 0777600)) {
    return mmu::read16(a);
  }

  printf("unibus: read from invalid address %06o\n", a);
  return trap(INTBUS);
}
};
