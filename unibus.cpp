#include <Arduino.h>
#include <SpiRAM.h>
#include "avr11.h"
#include "cpu.h"
#include "unibus.h"
#include "rk05.h"


//uint16_t memory[MEMSIZE];

SpiRAM bank0(6);
SpiRAM bank1(7);

const uint32_t ramSize = 0x1FFFF;           // 128K x 8 bit

void dumpsRam(SpiRAM& sRam, uint32_t addr, uint32_t length)
{
  // block to 10
  addr = addr / 10 * 10;
  length = (length + 9) / 10 * 10;

  byte b = sRam.readByte(addr);
  for (int i = 0; i < length; i++)
  {
    if (addr % 10 == 0)
    {
      Serial.println();
      Serial.print(addr, HEX);
      Serial.print(":\t");
    }
    Serial.print(b, HEX);
    b = sRam.readByte(++addr);
    Serial.print("\t");
  }
  Serial.println();
}

void pdp11::unibus::init() {
  Serial.print("zeroing SRAM, bank0");
  bank0.fillBytes(0, 0x0, ramSize);
  Serial.print(", bank1");
  bank1.fillBytes(0, 0x0, ramSize);
  Serial.println(", done.");
  //  dumpsRam(bank0, 0,100);
  //dumpsRam(bank0, ramSize - 100, 100);
  //  dumpsRam(bank1, 0,100);
  //dumpsRam(bank1, ramSize - 100, 100);
}

uint16_t pdp11::unibus::read8(uint32_t a) {
  if (a & 1) {
    return read16(a & ~1) >> 8;
  }
  return read16(a & ~1) & 0xFF;
}

void pdp11::unibus::write8(uint32_t a, uint16_t v) {
  if (a < 0760000) {
    if (a < 0x20000) {
      bank0.writeByte(a, v & 0xff);
    } else {
      bank1.writeByte(a, v & 0xff);
    }
    return;
  }

  if (a & 1) {
    write16(a&~1, (read16(a) & 0xFF) | (v & 0xFF) << 8);
  }
  else {
    write16(a&~1, (read16(a) & 0xFF00) | (v & 0xFF));
  }

}

void pdp11::unibus::write16(uint32_t a, uint16_t v) {
  if (a % 1) {
    Serial.print(F("unibus: write16 to odd address ")); Serial.println(a, OCT);
    trap(INTBUS);
  }
  if (a < 0760000) {
    if (a < 0x20000) {
      bank0.writeBuffer(a, (char*)&v, 2);
    } else {
      bank1.writeBuffer(a, (char*)&v, 2);
    }
    return;
  }
  if (a == 0777776) {
    switch (v >> 14) {
      case 0:
        switchmode(false);
        break;
      case 3:
        switchmode(true);
        break;
      default:
        Serial.println(F("invalid mode"));
        panic();
    }
    switch ((v >> 12) & 3) {
      case 0:
        prevuser = false;
        break;
      case 3:
        prevuser = true;
        break;
      default:
        Serial.println(F("invalid mode"));
        panic();
    }
    PS = v;
    return;
  }
  if (a == 0777546) {
    LKS = v;
    return;
  }
  if (a == 0777572) {
    SR0 = v;
    return;
  }
  if ((a & 0777770) == 0777560) {
    cons.write16(a, v);
    return;
  }
  if ((a & 0777700) == 0777400) {
    rkwrite16(a, v);
    return;
  }
  if (((a & 0777600) == 0772200) || ((a & 0777600) == 0777600)) {
    mmu.write16(a, v);
    return;
  }
  Serial.print("unibus: write to invalid address "); Serial.println(a, OCT);
  trap(INTBUS);
}

uint16_t pdp11::unibus::read16(uint32_t a) {
  if (a & 1) {
    Serial.print("unibus: read16 from odd address "); Serial.println(a, OCT);
    trap(INTBUS);
  }

  if (a < 0760000 ) {
    uint16_t v;
    if (a < 0x20000) {
      bank0.readBuffer(a, (char*)&v, 2);
    } else {
      bank1.readBuffer(a, (char*)&v, 2);
    }
    return v;
  }

  if (a == 0777546) {
    return LKS;
  }

  if (a == 0777570) {
    return 0173030;
  }

  if (a == 0777572) {
    return SR0;
  }

  if (a == 0777576) {
    return SR2;
  }

  if (a == 0777776) {
    return PS;
  }

  if ((a & 0777770) == 0777560) {
    return cons.read16(a);
  }

  if ((a & 0777760) == 0777400) {
    return rkread16(a);
  }

  if (((a & 0777600) == 0772200) || ((a & 0777600) == 0777600)) {
    return mmu.read16(a);
  }
  
  Serial.print("unibus: read from invalid address "); Serial.println(a, OCT);
  trap(INTBUS);
}
