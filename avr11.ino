#include <stdint.h>
#include "avr11.h"

// signed integer registers
int32_t R[8];
word	PS; // processor status
word	PC; // address of current instruction
word KSP, USP; // kernel and user stack pointer
word SR0, SR2;
word LKS;
boolean curuser, prevuser;

long clkcounter;

page pages[16];

word bootrom[29] = {
	0042113,        /* "KD" */
	0012706, 02000, /* MOV #boot_start, SP */
	0012700, 0000000, /* MOV #unit, R0        ; unit number */
	0010003,          /* MOV R0, R3 */
	0000303,          /* SWAB R3 */
	0006303,          /* ASL R3 */
	0006303,          /* ASL R3 */
	0006303,          /* ASL R3 */
	0006303,          /* ASL R3 */
	0006303,          /* ASL R3 */
	0012701, 0177412, /* MOV #RKDA, R1        ; csr */
	0010311,          /* MOV R3, (R1)         ; load da */
	0005041,          /* CLR -(R1)            ; clear ba */
	0012741, 0177000, /* MOV #-256.*2, -(R1)  ; load wc */
	0012741, 0000005, /* MOV #READ+GO, -(R1)  ; read & go */
	0005002,        /* CLR R2 */
	0005003,        /* CLR R3 */
	0012704, 02020, /* MOV #START+20, R4 */
	0005005, /* CLR R5 */
	0105711, /* TSTB (R1) */
	0100376, /* BPL .-2 */
	0105011, /* CLRB (R1) */
	0005007, /* CLR PC */
};

word memory[2048];

page createpage(word par, word pdr) {
	return page  { par, pdr, par & 07777, pdr >> 8 & 0x7F, (pdr & 2) == 2, (pdr & 6) == 6, (pdr & 8) == 8 };
}

void setup() {
	word i;
	for (i = 0; i < sizeof(R); i++) {
		R[i] = 0;
	}
	PS = 0;
	PC = 0;
	KSP = 0;
	USP = 0;
	curuser = 0;
	prevuser = 0;
	SR0 = 0;
	LKS = 1 << 7;
	for (i = 0; i < sizeof(memory); i++) {
		memory[i] = 0;
	}
	for (i = 0; i < sizeof(bootrom); i++) {
		memory[01000+i] = bootrom[i];
	}
	for (i = 0; i < sizeof(pages); i++) {
		pages[i] = createpage(0, 0);
	}
	R[7] = 02002;
	//clearterminal();
	//rkreset()
	clkcounter = 0;
}

void loop() {
	cpustep();
}

void cpustep() {
	uint16_t max, maxp, msb;
        int32_t ia, instr;
	PC = (uint16_t)R[7];
	ia = decode(PC, false, curuser);
	R[7] += 2;

        printstate();

	instr = (int32_t)physread16(ia);
/**
	d := instr & 077
	s := (instr & 07700) >> 6
	l := 2 - (instr >> 15)
	o := instr & 0xFF
	if l == 2 {
		max = 0xFFFF
		maxp = 0x7FFF
		msb = 0x8000
	} else {
		max = 0xFF
		maxp = 0x7F
		msb = 0x80
	}
	switch instr & 0070000 {
	case 0010000: // MOV
		// k.printstate()
		sa := k.aget(s, l)
		val := k.memread(sa, l)
		da := k.aget(d, l)
		k.PS &= 0xFFF1
		if val&msb == msb {
			k.PS |= FLAGN
		}
		if val == 0 {
			k.PS |= FLAGZ
		}
		if da < 0 && l == 1 {
			l = 2
			if val&msb == msb {
				val |= 0xFF00
			}
		}
		k.memwrite(da, l, val)
		return
	case 0020000: // CMP
		sa := k.aget(s, l)
		val1 := k.memread(sa, l)
		da := k.aget(d, l)
		val2 := k.memread(da, l)
		val := (val1 - val2) & max
		k.PS &= 0xFFF0
		if val == 0 {
			k.PS |= FLAGZ
		}
		if val&msb == msb {
			k.PS |= FLAGN
		}
		if (val1^val2)&msb == msb && !((val2^val)&msb == msb) {
			k.PS |= FLAGV
		}
		if val1 < val2 {
			k.PS |= FLAGC
		}
		return
	case 0030000: // BIT
		sa := k.aget(s, l)
		val1 := k.memread(sa, l)
		da := k.aget(d, l)
		val2 := k.memread(da, l)
		val := val1 & val2
		k.PS &= 0xFFF1
		if val == 0 {
			k.PS |= FLAGZ
		}
		if val&msb == msb {
			k.PS |= FLAGN
		}
		return
	case 0040000: // BIC
		sa := k.aget(s, l)
		val1 := k.memread(sa, l)
		da := k.aget(d, l)
		val2 := k.memread(da, l)
		val := (max ^ val1) & val2
		k.PS &= 0xFFF1
		if val == 0 {
			k.PS |= FLAGZ
		}
		if val&msb != 0 {
			k.PS |= FLAGN
		}
		k.memwrite(da, l, val)
		return
	case 0050000: // BIS
		sa := k.aget(s, l)
		val1 := k.memread(sa, l)
		da := k.aget(d, l)
		val2 := k.memread(da, l)
		val := val1 | val2
		k.PS &= 0xFFF1
		if val == 0 {
			k.PS |= FLAGZ
		}
		if val&msb == msb {
			k.PS |= FLAGN
		}
		k.memwrite(da, l, val)
		return
	}
	switch instr & 0170000 {
	case 0060000: // ADD
		sa := k.aget(s, 2)
		val1 := k.memread(sa, 2)
		da := k.aget(d, 2)
		val2 := k.memread(da, 2)
		val := (val1 + val2) & 0xFFFF
		k.PS &= 0xFFF0
		if val == 0 {
			k.PS |= FLAGZ
		}
		if val&0x8000 == 0x8000 {
			k.PS |= FLAGN
		}
		if !((val1^val2)&0x8000 == 0x8000) && ((val2^val)&0x8000 == 0x8000) {
			k.PS |= FLAGV
		}
		if val1+val2 >= 0xFFFF {
			k.PS |= FLAGC
		}
		k.memwrite(da, 2, val)
		return
	case 0160000: // SUB
		sa := k.aget(s, 2)
		val1 := k.memread(sa, 2)
		da := k.aget(d, 2)
		val2 := k.memread(da, 2)
		val := (val2 - val1) & 0xFFFF
		k.PS &= 0xFFF0
		if val == 0 {
			k.PS |= FLAGZ
		}
		if val&0x8000 == 0x8000 {
			k.PS |= FLAGN
		}
		if ((val1^val2)&0x8000 == 0x8000) && !((val2^val)&0x8000 == 0x8000) {
			k.PS |= FLAGV
		}
		if val1 > val2 {
			k.PS |= FLAGC
		}
		k.memwrite(da, 2, val)
		return
	}
	switch instr & 0177000 {
	case 0004000: // JSR
		val := k.aget(d, l)
		if val < 0 {
			break
		}
		k.push(uint16(k.R[s&7]))
		k.R[s&7] = k.R[7]
		k.R[7] = val
		return
	case 0070000: // MUL
		val1 := k.R[s&7]
		if val1&0x8000 == 0x8000 {
			val1 = -((0xFFFF ^ val1) + 1)
		}
		da := k.aget(d, l)
		val2 := int(k.memread(da, 2))
		if val2&0x8000 == 0x8000 {
			val2 = -((0xFFFF ^ val2) + 1)
		}
		val := val1 * val2
		k.R[s&7] = (val & 0xFFFF0000) >> 16
		k.R[(s&7)|1] = val & 0xFFFF
		k.PS &= 0xFFF0
		if val&0x80000000 == 0x80000000 {
			k.PS |= FLAGN
		}
		if val&0xFFFFFFFF == 0 {
			k.PS |= FLAGZ
		}
		if val < (1<<15) || val >= ((1<<15)-1) {
			k.PS |= FLAGC
		}
		return
	case 0071000: // DIV
		val1 := (k.R[s&7] << 16) | k.R[(s&7)|1]
		da := k.aget(d, l)
		val2 := int(k.memread(da, 2))
		k.PS &= 0xFFF0
		if val2 == 0 {
			k.PS |= FLAGC
			return
		}
		if val1/val2 >= 0x10000 {
			k.PS |= FLAGV
			return
		}
		k.R[s&7] = (val1 / val2) & 0xFFFF
		k.R[(s&7)|1] = (val1 % val2) & 0xFFFF
		if k.R[s&7] == 0 {
			k.PS |= FLAGZ
		}
		if k.R[s&7]&0100000 == 0100000 {
			k.PS |= FLAGN
		}
		if val1 == 0 {
			k.PS |= FLAGV
		}
		return
	case 0072000: // ASH
		val1 := k.R[s&7]
		da := k.aget(d, 2)
		val2 := uint(k.memread(da, 2) & 077)
		k.PS &= 0xFFF0
		var val int
		if val2&040 != 0 {
			val2 = (077 ^ val2) + 1
			if val1&0100000 == 0100000 {
				val = 0xFFFF ^ (0xFFFF >> val2)
				val |= val1 >> val2
			} else {
				val = val1 >> val2
			}
			shift := 1 << (val2 - 1)
			if val1&shift == shift {
				k.PS |= FLAGC
			}
		} else {
			val = (val1 << val2) & 0xFFFF
			shift := 1 << (16 - val2)
			if val1&shift == shift {
				k.PS |= FLAGC
			}
		}
		k.R[s&7] = val
		if val == 0 {
			k.PS |= FLAGZ
		}
		if val&0100000 == 0100000 {
			k.PS |= FLAGN
		}
		if xor(val&0100000, val1&0100000) != 0 {
			k.PS |= FLAGV
		}
		return
	case 0073000: // ASHC
		val1 := k.R[s&7]<<16 | k.R[(s&7)|1]
		da := k.aget(d, 2)
		val2 := uint(k.memread(da, 2) & 077)
		k.PS &= 0xFFF0
		var val int
		if val2&040 != 0 {
			val2 = (077 ^ val2) + 1
			if val1&0x80000000 == 0x8000000 {
				val = 0xFFFFFFFF ^ (0xFFFFFFFF >> val2)
				val |= val1 >> val2
			} else {
				val = val1 >> val2
			}
			if val1&(1<<(val2-1)) != 0 {
				k.PS |= FLAGC
			}
		} else {
			val = (val1 << val2) & 0xFFFFFFFF
			if val1&(1<<(32-val2)) != 0 {
				k.PS |= FLAGC
			}
		}
		k.R[s&7] = (val >> 16) & 0xFFFF
		k.R[(s&7)|1] = val & 0xFFFF
		if val == 0 {
			k.PS |= FLAGZ
		}
		if val&0x80000000 != 0 {
			k.PS |= FLAGN
		}
		if xor(val&0x80000000, val1&0x80000000) != 0 {
			k.PS |= FLAGV
		}
		return
	case 0074000: // XOR
		val1 := uint16(k.R[s&7])
		da := k.aget(d, 2)
		val2 := k.memread(da, 2)
		val := val1 ^ val2
		k.PS &= 0xFFF1
		if val == 0 {
			k.PS |= FLAGZ
		}
		if val&0x8000 == 0x8000 {
			k.PS |= FLAGZ
		}
		k.memwrite(da, 2, val)
		return
	case 0077000: // SOB
		k.R[s&7]--
		if k.R[s&7] != 0 {
			o &= 077
			o <<= 1
			k.R[7] -= o
		}
		return
	}
	switch instr & 0077700 {
	case 0005000: // CLR
		k.PS &= 0xFFF0
		k.PS |= FLAGZ
		da := k.aget(d, l)
		k.memwrite(da, l, 0)
		return
	case 0005100: // COM
		da := k.aget(d, l)
		val := k.memread(da, l) ^ max
		k.PS &= 0xFFF0
		k.PS |= FLAGC
		if val&msb == msb {
			k.PS |= FLAGN
		}
		if val == 0 {
			k.PS |= FLAGZ
		}
		k.memwrite(da, l, val)
		return
	case 0005200: // INC
		da := k.aget(d, l)
		val := (k.memread(da, l) + 1) & max
		k.PS &= 0xFFF1
		if val&msb == msb {
			k.PS |= FLAGN | FLAGV
		}
		if val == 0 {
			k.PS |= FLAGZ
		}
		k.memwrite(da, l, val)
		return
	case 0005300: // DEC
		da := k.aget(d, l)
		val := (k.memread(da, l) - 1) & max
		k.PS &= 0xFFF1
		if val&msb == msb {
			k.PS |= FLAGN
		}
		if val == maxp {
			k.PS |= FLAGV
		}
		if val == 0 {
			k.PS |= FLAGZ
		}
		k.memwrite(da, l, val)
		return
	case 0005400: // NEG
		da := k.aget(d, l)
		val := (-k.memread(da, l)) & max
		k.PS &= 0xFFF0
		if val&msb == msb {
			k.PS |= FLAGN
		}
		if val == 0 {
			k.PS |= FLAGZ
		} else {
			k.PS |= FLAGC
		}
		if val == 0x8000 {
			k.PS |= FLAGV
		}
		k.memwrite(da, l, val)
		return
	case 0005500: // ADC
		da := k.aget(d, l)
		val := k.memread(da, l)
		if k.PS&FLAGC == FLAGC {
			k.PS &= 0xFFF0
			if (val+1)&msb == msb {
				k.PS |= FLAGN
			}
			if val == max {
				k.PS |= FLAGZ
			}
			if val == 0077777 {
				k.PS |= FLAGV
			}
			if val == 0177777 {
				k.PS |= FLAGC
			}
			k.memwrite(da, l, (val+1)&max)
		} else {
			k.PS &= 0xFFF0
			if val&msb == msb {
				k.PS |= FLAGN
			}
			if val == 0 {
				k.PS |= FLAGZ
			}
		}
		return
	case 0005600: // SBC
		da := k.aget(d, l)
		val := k.memread(da, l)
		if k.PS&FLAGC == FLAGC {
			k.PS &= 0xFFF0
			if (val-1)&msb == msb {
				k.PS |= FLAGN
			}
			if val == 1 {
				k.PS |= FLAGZ
			}
			if val != 0 {
				k.PS |= FLAGC
			}
			if val == 0100000 {
				k.PS |= FLAGV
			}
			k.memwrite(da, l, (val-1)&max)
		} else {
			k.PS &= 0xFFF0
			if val&msb == msb {
				k.PS |= FLAGN
			}
			if val == 0 {
				k.PS |= FLAGZ
			}
			if val == 0100000 {
				k.PS |= FLAGV
			}
			k.PS |= FLAGC
		}
		return
	case 0005700: // TST
		da := k.aget(d, l)
		val := k.memread(da, l)
		k.PS &= 0xFFF0
		if val&msb == msb {
			k.PS |= FLAGN
		}
		if val == 0 {
			k.PS |= FLAGZ
		}
		return
	case 0006000: // ROR
		da := k.aget(d, l)
		val := k.memread(da, l)
		if k.PS&FLAGC == FLAGC {
			val |= max + 1
		}
		k.PS &= 0xFFF0
		if val&1 == 1 {
			k.PS |= FLAGC
		}
		if val&(max+1) != 0 {
			k.PS |= FLAGN
		}
		if !(val&max != 0) {
			k.PS |= FLAGZ
		}
		if xor16(val&1, val&(max+1)) != 0 {
			k.PS |= FLAGV
		}
		val >>= 1
		k.memwrite(da, l, val)
		return
	case 0006100: // ROL
		da := k.aget(d, l)
		val := k.memread(da, l) << 1
		if k.PS&FLAGC == FLAGC {
			val |= 1
		}
		k.PS &= 0xFFF0
		if val&(max+1) != 0 {
			k.PS |= FLAGC
		}
		if val&msb == msb {
			k.PS |= FLAGN
		}
		if !(val&max != 0) {
			k.PS |= FLAGZ
		}
		if (val^(val>>1))&msb != 0 {
			k.PS |= FLAGV
		}
		val &= max
		k.memwrite(da, l, val)
		return
	case 0006200: // ASR
		da := k.aget(d, l)
		val := k.memread(da, l)
		k.PS &= 0xFFF0
		if val&1 == 1 {
			k.PS |= FLAGC
		}
		if val&msb == msb {
			k.PS |= FLAGN
		}
		if xor16(val&msb, val&1) != 0 {
			k.PS |= FLAGV
		}
		val = (val & msb) | (val >> 1)
		if val == 0 {
			k.PS |= FLAGZ
		}
		k.memwrite(da, l, val)
		return
	case 0006300: // ASL
		da := k.aget(d, l)
		val := k.memread(da, l)
		k.PS &= 0xFFF0
		if val&msb == msb {
			k.PS |= FLAGC
		}
		if val&(msb>>1) != 0 {
			k.PS |= FLAGN
		}
		if (val^(val<<1))&msb != 0 {
			k.PS |= FLAGV
		}
		val = (val << 1) & max
		if val == 0 {
			k.PS |= FLAGZ
		}
		k.memwrite(da, l, val)
		return
	case 0006700: // SXT
		da := k.aget(d, l)
		if k.PS&FLAGN == FLAGN {
			k.memwrite(da, l, max)
		} else {
			k.PS |= FLAGZ
			k.memwrite(da, l, 0)
		}
		return
	}
	switch instr & 0177700 {
	case 0000100: // JMP
		val := k.aget(d, 2)
		if val < 0 {
			panic("whoa!")
			break
		}
		k.R[7] = val
		return
	case 0000300: // SWAB
		da := k.aget(d, l)
		val := k.memread(da, l)
		val = ((val >> 8) | (val << 8)) & 0xFFFF
		k.PS &= 0xFFF0
		if (val & 0xFF) == 0 {
			k.PS |= FLAGZ
		}
		if val&0x80 == 0x80 {
			k.PS |= FLAGN
		}
		k.memwrite(da, l, val)
		return
	case 0006400: // MARK
		k.R[6] = k.R[7] + (instr&077)<<1
		k.R[7] = k.R[5]
		k.R[5] = int(k.pop())
		break
	case 0006500: // MFPI
		var val uint16
		da := k.aget(d, 2)
		switch {
		case da == -7:
			// val = (curuser == prevuser) ? R[6] : (prevuser ? k.USP : KSP);
			if curuser == prevuser {
				val = uint16(k.R[6])
			} else {
				if prevuser {
					val = k.USP
				} else {
					val = k.KSP
				}
			}
		case da < 0:
			panic("invalid MFPI instruction")
		default:
			val = k.unibus.physread16(k.decode(uint16(da), false, prevuser))
		}
		k.push(val)
		k.PS &= 0xFFF0
		k.PS |= FLAGC
		if val == 0 {
			k.PS |= FLAGZ
		}
		if val&0x8000 == 0x800 {
			k.PS |= FLAGN
		}
		return
	case 0006600: // MTPI
		da := k.aget(d, 2)
		val := uint16(k.pop())
		switch {
		case da == -7:
			if curuser == prevuser {
				k.R[6] = int(val)
			} else {
				if prevuser {
					k.USP = val
				} else {
					k.KSP = val
				}
			}
		case da < 0:
			panic("invalid MTPI instrution")
		default:
			sa := k.decode(uint16(da), true, prevuser)
			k.unibus.physwrite16(sa, val)
		}
		k.PS &= 0xFFF0
		k.PS |= FLAGC
		if val == 0 {
			k.PS |= FLAGZ
		}
		if val&0x8000 == 0x8000 {
			k.PS |= FLAGN
		}
		return
	}
	if (instr & 0177770) == 0000200 { // RTS
		k.R[7] = k.R[d&7]
		k.R[d&7] = int(k.pop())
		return
	}
	switch instr & 0177400 {
	case 0000400:
		k.branch(o)
		return
	case 0001000:
		if !(k.PS&FLAGZ == FLAGZ) {
			k.branch(o)
		}
		return
	case 0001400:
		if k.PS&FLAGZ == FLAGZ {
			k.branch(o)
		}
		return
	case 0002000:
		if !(xor16(k.PS&FLAGN, k.PS&FLAGV) != 0) {
			k.branch(o)
		}
		return
	case 0002400:
		if xor16(k.PS&FLAGN, k.PS&FLAGV) != 0 {
			k.branch(o)
		}
		return
	case 0003000:
		if !(xor16(k.PS&FLAGN, k.PS&FLAGV) != 0) && !(k.PS&FLAGZ == FLAGZ) {
			k.branch(o)
		}
		return
	case 0003400:
		if xor16(k.PS&FLAGN, k.PS&FLAGV) != 0 || (k.PS&FLAGZ == FLAGZ) {
			k.branch(o)
		}
		return
	case 0100000:
		if k.PS&FLAGN == 0 {
			k.branch(o)
		}
		return
	case 0100400:
		if k.PS&FLAGN == FLAGN {
			k.branch(o)
		}
		return
	case 0101000:
		if !(k.PS&FLAGC == FLAGC) && !(k.PS&FLAGZ == FLAGZ) {
			k.branch(o)
		}
		return
	case 0101400:
		if (k.PS&FLAGC == FLAGC) || (k.PS&FLAGZ == FLAGZ) {
			k.branch(o)
		}
		return
	case 0102000:
		if !(k.PS&FLAGV == FLAGV) {
			k.branch(o)
		}
		return
	case 0102400:
		if k.PS&FLAGV == FLAGV {
			k.branch(o)
		}
		return
	case 0103000:
		if !(k.PS&FLAGC == FLAGC) {
			k.branch(o)
		}
		return
	case 0103400:
		if k.PS&FLAGC == FLAGC {
			k.branch(o)
		}
		return
	}
	if (instr&0177000) == 0104000 || instr == 3 || instr == 4 { // EMT TRAP IOT BPT
		var vec int
		switch {
		case (instr & 0177400) == 0104000:
			vec = 030
		case (instr & 0177400) == 0104400:
			vec = 034
		case instr == 3:
			vec = 014
		default:
			vec = 020
		}
		prev := k.PS
		k.switchmode(false)
		k.push(prev)
		k.push(uint16(k.R[7]))
		k.R[7] = int(memory[vec>>1])
		k.PS = memory[(vec>>1)+1]
		if prevuser {
			k.PS |= (1 << 13) | (1 << 12)
		}
		return
	}
	if (instr & 0177740) == 0240 { // CL?, SE?
		if instr&020 == 020 {
			k.PS |= uint16(instr) & 017
		} else {
			k.PS &= ^(uint16(instr) & 017)
		}
		return
	}
	switch instr {
	case 0000000: // HALT
		if curuser {
			break
		}
		writedebug("HALT\n")
		k.printstate()
		panic("HALT")
		return
	case 0000001: // WAIT
		if curuser {
			break
		}
		//println("WAIT")
		waiting = true
		return
	case 0000002: // RTI
		fallthrough
	case 0000006: // RTT
		k.R[7] = int(k.pop())
		val := k.pop()
		if curuser {
			val &= 047
			val |= k.PS & 0177730
		}
		k.unibus.physwrite16(0777776, val)
		return
	case 0000005: // RESET
		if curuser {
			return
                }
*/
}

void printstate() {
  // TODO
}

uint32_t decode(uint16_t a, bool w, bool user) {
	page p;
        uint32_t aa, block, disp;
	if (!(SR0&1)) {
                aa = (uint32_t)a;
		if (aa >= 0170000) {
			aa += 0600000;
		} 
		return aa;
	}
	if (user) {
		p = pages[(a>>13)+8];
	} else {
		p = pages[(a >> 13)];
	}

	if (w && !p.write) {
		SR0 = (1 << 13) | 1;
		SR0 |= a >> 12 & ~1;
		if (user) {
			SR0 |= (1 << 5) | (1 << 6);
		}
		SR2 = PC;
		abort(); //panic(trap{INTFAULT, "write to read-only page " + ostr(a, 6)})
	}
	if (!p.read) {
		SR0 = (1 << 15) | 1;
		SR0 |= (a >> 12) & ~1;
		if (user) {
			SR0 |= (1 << 5) | (1 << 6);
		}
		SR2 = PC;
		abort(); //panic(trap{INTFAULT, "read from no-access page " + ostr(a, 6)})
	}
	block = a >> 6 & 0177;
	disp = a & 077;
	if (((p.ed && (block < p.len)) || !(p.ed && (block > p.len)))) {
		//if(p.ed ? (block < p.len) : (block > p.len)) {
		SR0 = (1 << 14) | 1;
		SR0 |= (a >> 12) & ~1;
		if (user) {
			SR0 |= (1 << 5) | (1 << 6);
		}
		SR2 = PC;
		abort(); // panic(trap{INTFAULT, "page length exceeded, address " + ostr(a, 6) + " (block " + ostr(block, 3) + ") is beyond length " + ostr(p.len, 3)})
	}
	if (w) {
		p.pdr |= 1 << 6;
	}
	return ((block+p.addr) << 6) + disp;
}

uint16_t read8(uint16_t a) {
	return physread8(decode(a, false, curuser));
}

uint16_t read16(uint16_t a) {
	return physread16(decode(a, false, curuser));
}

void write8(uint16_t a, uint16_t v) {
	physwrite8(decode(a, true, curuser), v);
}

void write16(uint16_t a, uint16_t v) {
	physwrite16(decode(a, true, curuser), v);
}

uint16_t fetch16() {
  uint16_t val;
  val = read16((uint16_t)R[7]);
  R[7] += 2;
  return val;
}

void push(uint16_t v) {
  R[6] -= 2;
  write16((uint16_t)R[6], v);
}

uint16_t pop() {
  uint16_t val;
  val = read16((uint16_t)R[6]);
  R[6] += 2;
  return val;
}

uint16_t physread16(uint32_t a) {
  if (a & 1) {
	abort(); // panic(trap{INTBUS, "read from odd address " + ostr(a, 6)})
  } else if (a < 0760000 ) {
    return memory[a>>1];
  } else if (a == 0777546) {
    return LKS;
  } else if (a == 0777570) {
    return 0173030;
  } else if (a == 0777572) {
    return SR0;
  } else if (a == 0777576) {
    return SR2;
  } else if (a == 0777776) {
    return PS;
  } else if ((a&0777770) == 0777560) {
    abort();
    //return uint16(u.cons.consread16(a))
  } else if ((a&0777760) == 0777400) {
    // return uint16(u.rk.rkread16(a))
    abort();
  } else if (((a&0777600) == 0772200) || ((a&0777600) == 0777600)) {
    // return mmuread16(a)
    abort();
  } 
  abort(); 
   //panic(trap{INTBUS, "read from invalid address " + ostr(a, 6)})
}

uint16_t physread8(uint32_t a) {
  uint16_t val;
  val = physread16(a & ~1);
  if (a&1) {
	return val >> 8;
  }
  return val & 0xFF;
}

void physwrite8(uint32_t a, uint16_t v) {
	if (a < 0760000) {
		if (a&1) {
			memory[a>>1] &= 0xFF;
			memory[a>>1] |= v & 0xFF << 8;
		} else {
			memory[a>>1] &= 0xFF00;
			memory[a>>1] |= v & 0xFF;
		}
	} else {
		if (a&1) {
			physwrite16(a&~1, (physread16(a)&0xFF)|(v&0xFF)<<8);
		} else {
			physwrite16(a&~1, (physread16(a)&0xFF00)|(v&0xFF));
		}
	}
}

void physwrite16(uint32_t a, uint16_t v) {
	if (a%1) {
		abort(); //panic(trap{INTBUS, "write to odd address " + ostr(a, 6)})
	}
	if (a < 0760000) {
		memory[a>>1] = v;
	} else if (a == 0777776) {
		switch (v >> 14) {
		case 0:
			switchmode(false);
			break;
		case 3:
			switchmode(true);
			break;
		default:
			abort(); //panic("invalid mode")
		}
		switch ((v >> 12) & 3) {
		case 0:
			prevuser = false;
			break;
		case 3:
			prevuser = true;
			break;
		default:
			abort(); //panic("invalid mode")
		}
		PS = v;
	} else if (a == 0777546) {
		LKS = v;
	} else if (a == 0777572) {
		SR0 = v;
	} else if ((a & 0777770) == 0777560) {
		abort(); // conswrite16(a, int(v))
	} else if ((a & 0777700) == 0777400) {
		abort(); //u.rk.rkwrite16(a, int(v))
	} else if (((a&0777600) == 0772200) || ((a&0777600) == 0777600)) {
		abort(); //mmuwrite16(a, v)
	} else {
		abort(); //panic(trap{INTBUS, "write to invalid address " + ostr(a, 6)})
	}
}

void switchmode(bool newm) {
	prevuser = curuser;
	curuser = newm;
	if (prevuser) {
		USP = (uint16_t)R[6];
	} else {
		KSP = (uint16_t)R[6];
	}
	if (curuser) {
		R[6] = (int32_t)USP;
	} else {
		R[6] = (int32_t)KSP;
	}
	PS &= 0007777;
	if (curuser) {
		PS |= (1 << 15) | (1 << 14);
	}
	if (prevuser) {
		PS |= (1 << 13) | (1 << 12);
	}
}

/**
package pdp11

import "fmt"

const (
	FLAGN = 8
	FLAGZ = 4
	FLAGV = 2
	FLAGC = 1
)

const pr = false // debug

var (
	curuser, prevuser bool
	clkcounter        int
	waiting           = false
	interrupts        []intr
)

type intr struct{ vec, pri int }

// traps
const (
	INTBUS    = 0004
	INTINVAL  = 0010
	INTDEBUG  = 0014
	INTIOT    = 0020
	INTTTYIN  = 0060
	INTTTYOUT = 0064
	INTFAULT  = 0250
	INTCLOCK  = 0100
	INTRK     = 0220
)

}

func xor(x, y int) int {
	a := x & y
	b := ^x & ^y
	z := ^a & ^b
	return z
}

func xor16(x, y uint16) uint16 {
	a := x & y
	b := ^x & ^y
	z := ^a & ^b
	return z
}


	Input  chan uint8
	unibus *Unibus
}






func ostr(z interface{}, n int) string {
	return fmt.Sprintf("%0"+fmt.Sprintf("%d", n)+"o", z)
}

var writedebug = fmt.Print

func (k *KB11) printstate() {
	writedebug(fmt.Sprintf("R0 %06o R1 %06o R2 %06o R3 %06o R4 %06o R5 %06o R6 %06o R7 %06o\n[", k.R[0], k.R[1], k.R[2], k.R[3], k.R[4], k.R[5], k.R[6], k.R[7]))
	if prevuser {
		writedebug("u")
	} else {
		writedebug("k")
	}
	if curuser {
		writedebug("U")
	} else {
		writedebug("K")
	}
	if k.PS&FLAGN != 0 {
		writedebug("N")
	} else {
		writedebug(" ")
	}
	if k.PS&FLAGZ != 0 {
		writedebug("Z")
	} else {
		writedebug(" ")
	}
	if k.PS&FLAGV != 0 {
		writedebug("V")
	} else {
		writedebug(" ")
	}
	if k.PS&FLAGC != 0 {
		writedebug("C")
	} else {
		writedebug(" ")
	}
	ia := k.decode(k.PC, false, curuser)
	instr := k.unibus.physread16(ia)
	writedebug("]  instr " + ostr(k.PC, 6) + ": " + ostr(instr, 6) + "   " + disasm(ia) + "\n")
}

type trap struct {
	num int
	msg string
}

func (t trap) String() string {
	return fmt.Sprintf("trap %06o occured: %s", t.num, t.msg)
}

func interrupt(vec, pri int) {
	var i int
	if vec&1 == 1 {
		panic("Thou darst calling interrupt() with an odd vector number?")
	}
	for ; i < len(interrupts); i++ {
		if interrupts[i].pri < pri {
			break
		}
	}
	for ; i < len(interrupts); i++ {
		if interrupts[i].vec >= vec {
			break
		}
	}
	// interrupts.splice(i, 0, {vec: vec, pri: pri});
	interrupts = append(interrupts[:i], append([]intr{{vec, pri}}, interrupts[i:]...)...)
}

func (k *KB11) handleinterrupt(vec int) {
	defer func() {
		trap := recover()
		switch trap := trap.(type) {
		case struct {
			num int
			msg string
		}:
			k.trapat(trap.num, trap.msg)
		case nil:
			break
		default:
			panic(trap)
		}
		k.R[7] = int(memory[vec>>1])
		k.PS = memory[(vec>>1)+1]
		if prevuser {
			k.PS |= (1 << 13) | (1 << 12)
		}
		waiting = false
	}()
	prev := k.PS
	k.switchmode(false)
	k.push(prev)
	k.push(uint16(k.R[7]))
}

func (k *KB11) trapat(vec int, msg string) {
	var prev uint16
	defer func() {
		t := recover()
		switch t := t.(type) {
		case trap:
			writedebug("red stack trap!\n")
			memory[0] = uint16(k.R[7])
			memory[1] = prev
			vec = 4
			panic("fatal")
		case nil:
			break
		default:
			panic(t)
		}
		k.R[7] = int(memory[vec>>1])
		k.PS = memory[(vec>>1)+1]
		if prevuser {
			k.PS |= (1 << 13) | (1 << 12)
		}
		waiting = false
	}()
	if vec&1 == 1 {
		panic("Thou darst calling trapat() with an odd vector number?")
	}
	writedebug("trap " + ostr(vec, 6) + " occured: " + msg + "\n")
	k.printstate()

	prev = k.PS
	k.switchmode(false)
	k.push(prev)
	k.push(uint16(k.R[7]))
}

func (k *KB11) aget(v int, l int) int {
	if (v&7) >= 6 || (v&010 != 0) {
		l = 2
	}
	if (v & 070) == 000 {
		return -(v + 1)
	}
	var addr uint16
	switch v & 060 {
	case 000:
		v &= 7
		addr = uint16(k.R[v&7])
	case 020:
		addr = uint16(k.R[v&7])
		k.R[v&7] += l
	case 040:
		k.R[v&7] -= l
		addr = uint16(k.R[v&7])
	case 060:
		addr = k.fetch16()
		addr += uint16(k.R[v&7])
	}
	addr &= 0xFFFF
	if v&010 != 0 {
		addr = k.read16(addr)
	}
	return int(addr)
}

func (k *KB11) memread(a, l int) uint16 {
	if a < 0 {
		r := uint8(-(a + 1))
		if l == 2 {
			return uint16(k.R[r&7])
		} else {
			return uint16(k.R[r&7]) & 0xFF
		}
	}
	if l == 2 {
		return k.read16(uint16(a))
	}
	return k.read8(uint16(a))
}

func (k *KB11) memwrite(a, l int, v uint16) {
	if a < 0 {
		r := uint8(-(a + 1))
		if l == 2 {
			k.R[r&7] = int(v)
		} else {
			k.R[r&7] &= 0xFF00
			k.R[r&7] |= int(v)
		}
	} else if l == 2 {
		k.write16(uint16(a), v)
	} else {
		k.write8(uint16(a), v)
	}
}

func (k *KB11) branch(o int) {
	//printstate()
	if o&0x80 == 0x80 {
		o = -(((^o) + 1) & 0xFF)
	}
	o <<= 1
	k.R[7] += o
}

	k.unibus.rk.Step()
	k.unibus.cons.Step(k)
}

func (k *KB11) onestep() {
	defer func() {
		t := recover()
		switch t := t.(type) {
		case trap:
			k.trapat(t.num, t.msg)
		case nil:
			// ignore
		default:
			panic(t)
		}
	}()

	k.step()
	if len(interrupts) > 0 && interrupts[0].pri >= ((int(k.PS)>>5)&7) {
		//fmt.Printf("IRQ: %06o\n", interrupts[0].vec)
		k.handleinterrupt(interrupts[0].vec)
		interrupts = interrupts[1:]
	}
	clkcounter++
	if clkcounter >= 40000 {
		clkcounter = 0
		k.unibus.LKS |= (1 << 7)
		if k.unibus.LKS&(1<<6) != 0 {
			interrupt(INTCLOCK, 6)
		}
	}
}

func New() *KB11 {
	var cpu KB11
	var unibus Unibus
	var rk RK05
	var cons Console
	cpu.unibus = &unibus
	unibus.cpu = &cpu
	unibus.rk = &rk
	unibus.cons = &cons
	rk.rkinit()
	cpu.Reset()
	return &cpu
}

type Unibus struct {
	cpu  *KB11
	rk   *RK05 // drive 0
	cons *Console
}

// uint18 represents a unibus 18 bit physical address
type uint18 uint32

package pdp11

import "fmt"
import "io/ioutil"

const imglen = 2077696

const (
	RKOVR = (1 << 14)
	RKNXD = (1 << 7)
	RKNXC = (1 << 6)
	RKNXS = (1 << 5)
)

type RK05 struct {
	RKBA, RKDS, RKER, RKCS, RKWC, drive, sector, surface, cylinder, rkimg int
	running                                                               bool
	rkdisk                                                                []byte // rk0 disk image
}

func (r *RK05) rkread16(a uint18) int {
	switch a {
	case 0777400:
		return r.RKDS
	case 0777402:
		return r.RKER
	case 0777404:
		return r.RKCS | (r.RKBA&0x30000)>>12
	case 0777406:
		return r.RKWC
	case 0777410:
		return r.RKBA & 0xFFFF
	case 0777412:
		return (r.sector) | (r.surface << 4) | (r.cylinder << 5) | (r.drive << 13)
	default:
		panic("invalid read")
	}
}

func (r *RK05) rknotready() {
	r.RKDS &= ^(1 << 6)
	r.RKCS &= ^(1 << 7)
}

func (r *RK05) rkready() {
	r.RKDS |= 1 << 6
	r.RKCS |= 1 << 7
}

func (r *RK05) rkerror(code int) {
	var msg string
	r.rkready()
	r.RKER |= code
	r.RKCS |= (1 << 15) | (1 << 14)
	switch code {
	case RKOVR:
		msg = "operation overflowed the disk"
		break
	case RKNXD:
		msg = "invalid disk accessed"
		break
	case RKNXC:
		msg = "invalid cylinder accessed"
		break
	case RKNXS:
		msg = "invalid sector accessed"
		break
	}
	panic(msg)
}

func (r *RK05) Step() {
	if !r.running {
		return
	}
	var w bool
	switch (r.RKCS & 017) >> 1 {
	case 0:
		return
	case 1:
		w = true
	case 2:
		w = false
	default:
		panic(fmt.Sprintf("unimplemented RK05 operation %#o", ((r.RKCS & 017) >> 1)))
	}
	//fmt.Println("rkrwsec: RKBA:", r.RKBA, "RKWC:", r.RKWC, "cylinder:", r.cylinder, "sector:", r.sector)
	if r.drive != 0 {
		r.rkerror(RKNXD)
	}
	if r.cylinder > 0312 {
		r.rkerror(RKNXC)
	}
	if r.sector > 013 {
		r.rkerror(RKNXS)
	}
	pos := (r.cylinder*24 + r.surface*12 + r.sector) * 512
	for i := 0; i < 256 && r.RKWC != 0; i++ {
		if w {
			val := memory[r.RKBA>>1]
			r.rkdisk[pos] = byte(val & 0xFF)
			r.rkdisk[pos+1] = byte((val >> 8) & 0xFF)
		} else {
			memory[r.RKBA>>1] = uint16(r.rkdisk[pos]) | uint16(r.rkdisk[pos+1])<<8
		}
		r.RKBA += 2
		pos += 2
		r.RKWC = (r.RKWC + 1) & 0xFFFF
	}
	r.sector++
	if r.sector > 013 {
		r.sector = 0
		r.surface++
		if r.surface > 1 {
			r.surface = 0
			r.cylinder++
			if r.cylinder > 0312 {
				r.rkerror(RKOVR)
			}
		}
	}
	if r.RKWC == 0 {
		r.running = false
		r.rkready()
		if r.RKCS&(1<<6) != 0 {
			interrupt(INTRK, 5)
		}
	}
}

func (r *RK05) rkgo() {
	switch (r.RKCS & 017) >> 1 {
	case 0:
		r.rkreset()
	case 1, 2:
		r.running = true
		r.rknotready()
	default:
		panic(fmt.Sprintf("unimplemented RK05 operation %#o", ((r.RKCS & 017) >> 1)))
	}
}

func (r *RK05) rkwrite16(a uint18, v int) {
	switch a {
	//	case 0777400:
	//		break
	//	case 0777402:
	//		break
	case 0777404:
		r.RKBA = (r.RKBA & 0xFFFF) | ((v & 060) << 12)
		const BITS = 017517
		v &= BITS // writable bits
		r.RKCS &= ^BITS
		r.RKCS |= v & ^1 // don't set GO bit
		if v&1 == 1 {
			r.rkgo()
		}
	case 0777406:
		r.RKWC = v
	case 0777410:
		r.RKBA = (r.RKBA & 0x30000) | (v)
	case 0777412:
		r.drive = v >> 13
		r.cylinder = (v >> 5) & 0377
		r.surface = (v >> 4) & 1
		r.sector = v & 15
	default:
		panic("invalid write")
	}
}

func (r *RK05) rkreset() {
	r.RKDS = (1 << 11) | (1 << 7) | (1 << 6)
	r.RKER = 0
	r.RKCS = 1 << 7
	r.RKWC = 0
	r.RKBA = 0
}

func (r *RK05) rkinit() {
	var err error
	r.rkdisk, err = ioutil.ReadFile("rk0")
	if err != nil {
		panic(err)
	}
}
package pdp11

import (
	"os"
)

type Console struct {
	TKS, TKB, TPS, TPB int

	Input chan uint8
	count uint8 // step delay
	ready bool
}

func (c *Console) clearterminal() {
	c.TKS = 0
	c.TPS = 1 << 7
	c.TKB = 0
	c.TPB = 0
	c.ready = true
}

func (c *Console) writeterminal(char int) {
	var outb [1]byte
	switch char {
	case 13:
		// skip
	default:
		outb[0] = byte(char)
		os.Stdout.Write(outb[:])
	}
}

func (c *Console) addchar(char int) {
	switch char {
	case 42:
		c.TKB = 4
	case 19:
		c.TKB = 034
	case 46:
		c.TKB = 127
	default:
		c.TKB = char
	}
	c.TKS |= 0x80
	c.ready = false
	if c.TKS&(1<<6) != 0 {
		interrupt(INTTTYIN, 4)
	}
}

func (c *Console) getchar() int {
	if c.TKS&0x80 == 0x80 {
		c.TKS &= 0xff7e
		c.ready = true
		return c.TKB
	}
	return 0
}

func (c *Console) Step(k *KB11) {
	if c.ready {
		select {
		case v, ok := <-c.Input:
			if ok {
				c.addchar(int(v))
			}
		default:
		}
	}
	c.count++
	if c.count%32 != 0 {
		return
	}
	if c.TPS&0x80 == 0 {
		c.writeterminal(c.TPB & 0x7f)
		c.TPS |= 0x80
		if c.TPS&(1<<6) != 0 {
			interrupt(INTTTYOUT, 4)
		}
	}
}

func (c *Console) consread16(a uint18) int {
	switch a {
	case 0777560:
		return c.TKS
	case 0777562:
		return c.getchar()
	case 0777564:
		return c.TPS
	case 0777566:
		return 0
	default:
		panic("read from invalid address " + ostr(a, 6))
	}
}

func (c *Console) conswrite16(a uint18, v int) {
	switch a {
	case 0777560:
		if v&(1<<6) != 0 {
			c.TKS |= 1 << 6
		} else {
			c.TKS &= ^(1 << 6)
		}
	case 0777564:
		if v&(1<<6) != 0 {
			c.TPS |= 1 << 6
		} else {
			c.TPS &= ^(1 << 6)
		}
	case 0777566:
		c.TPB = v & 0xff
		c.TPS &= 0xff7f
	default:
		panic("write to invalid address " + ostr(a, 6))
	}
}
package pdp11

import "fmt"

var rs = [...]string{"R0", "R1", "R2", "R3", "R4", "R5", "SP", "PC"}

type D struct {
	inst, arg uint16
	msg       string
	flag      string
	b         bool
}

var disasmtable = []D{
	{0077700, 0005000, "CLR", "D", true},
	{0077700, 0005100, "COM", "D", true},
	{0077700, 0005200, "INC", "D", true},
	{0077700, 0005300, "DEC", "D", true},
	{0077700, 0005400, "NEG", "D", true},
	{0077700, 0005700, "TST", "D", true},
	{0077700, 0006200, "ASR", "D", true},
	{0077700, 0006300, "ASL", "D", true},
	{0077700, 0006000, "ROR", "D", true},
	{0077700, 0006100, "ROL", "D", true},
	{0177700, 0000300, "SWAB", "D", false},
	{0077700, 0005500, "ADC", "D", true},
	{0077700, 0005600, "SBC", "D", true},
	{0177700, 0006700, "SXT", "D", false},
	{0070000, 0010000, "MOV", "SD", true},
	{0070000, 0020000, "CMP", "SD", true},
	{0170000, 0060000, "ADD", "SD", false},
	{0170000, 0160000, "SUB", "SD", false},
	{0070000, 0030000, "BIT", "SD", true},
	{0070000, 0040000, "BIC", "SD", true},
	{0070000, 0050000, "BIS", "SD", true},
	{0177000, 0070000, "MUL", "RD", false},
	{0177000, 0071000, "DIV", "RD", false},
	{0177000, 0072000, "ASH", "RD", false},
	{0177000, 0073000, "ASHC", "RD", false},
	{0177400, 0000400, "BR", "O", false},
	{0177400, 0001000, "BNE", "O", false},
	{0177400, 0001400, "BEQ", "O", false},
	{0177400, 0100000, "BPL", "O", false},
	{0177400, 0100400, "BMI", "O", false},
	{0177400, 0101000, "BHI", "O", false},
	{0177400, 0101400, "BLOS", "O", false},
	{0177400, 0102000, "BVC", "O", false},
	{0177400, 0102400, "BVS", "O", false},
	{0177400, 0103000, "BCC", "O", false},
	{0177400, 0103400, "BCS", "O", false},
	{0177400, 0002000, "BGE", "O", false},
	{0177400, 0002400, "BLT", "O", false},
	{0177400, 0003000, "BGT", "O", false},
	{0177400, 0003400, "BLE", "O", false},
	{0177700, 0000100, "JMP", "D", false},
	{0177000, 0004000, "JSR", "RD", false},
	{0177770, 0000200, "RTS", "R", false},
	{0177777, 0006400, "MARK", "", false},
	{0177000, 0077000, "SOB", "RO", false},
	{0177777, 0000005, "RESET", "", false},
	{0177700, 0006500, "MFPI", "D", false},
	{0177700, 0006600, "MTPI", "D", false},
	{0177777, 0000001, "WAIT", "", false},
	{0177777, 0000002, "RTI", "", false},
	{0177777, 0000006, "RTT", "", false},
	{0177400, 0104000, "EMT", "N", false},
	{0177400, 0104400, "TRAP", "N", false},
	{0177777, 0000003, "BPT", "", false},
	{0177777, 0000004, "IOT", "", false},
}

func disasmaddr(m uint16, a uint18) string {
	if (m & 7) == 7 {
		switch m {
		case 027:
			a += 2
			return fmt.Sprintf("$%06o", memory[a>>1])
		case 037:
			a += 2
			return fmt.Sprintf("*%06o", memory[a>>1])
		case 067:
			a += 2
			return fmt.Sprintf("*%06o", (a+2+uint18(memory[a>>1]))&0xFFFF)
		case 077:
			return fmt.Sprintf("**%06o", (a+2+uint18(memory[a>>1]))&0xFFFF)
		}
	}
	r := rs[m&7]
	switch m & 070 {
	case 000:
		return r
	case 010:
		return "(" + r + ")"
	case 020:
		return "(" + r + ")+"
	case 030:
		return "*(" + r + ")+"
	case 040:
		return "-(" + r + ")"
	case 050:
		return "*-(" + r + ")"
	case 060:
		a += 2
		return fmt.Sprintf("%06o (%s)", memory[a>>1], r)
	case 070:
		a += 2
		return fmt.Sprintf("*%06o (%s)", memory[a>>1], r)
	}
	panic(fmt.Sprintf("disasmaddr: unknown addressing mode, register %v, mode %o", r, m&070))
}

func disasm(a uint18) string {
	ins := memory[a>>1]
	msg := "???"
	var l D
	for i := 0; i < len(disasmtable); i++ {
		l = disasmtable[i]
		if (ins & l.inst) == l.arg {
			msg = l.msg
			break
		}
	}
	if msg == "???" {
		return msg
	}
	if l.b && (ins&0100000 == 0100000) {
		msg += "B"
	}
	s := (ins & 07700) >> 6
	d := ins & 077
	o := byte(ins & 0377)
	switch l.flag {
	case "SD":
		msg += " " + disasmaddr(s, a) + ","
		fallthrough
	case "D":
		msg += " " + disasmaddr(d, a)
	case "RO":
		msg += " " + rs[(ins&0700)>>6] + ","
		o &= 077
		fallthrough
	case "O":
		if o&0x80 == 0x80 {
			msg += fmt.Sprintf(" -%#o", (2 * ((0xFF ^ o) + 1)))
		} else {
			msg += fmt.Sprintf(" +%#o", (2 * o))
		}
	case "RD":
		msg += " " + rs[(ins&0700)>>6] + ", " + disasmaddr(d, a)
	case "R":
		msg += " " + rs[ins&7]
	case "R3":
		msg += " " + rs[(ins&0700)>>6]
	}
	return msg
}
package pdp11

}


func mmuread16(a uint18) uint16 {
	i := ((a & 017) >> 1)
	if (a >= 0772300) && (a < 0772320) {
		return pages[i].pdr
	}
	if (a >= 0772340) && (a < 0772360) {
		return pages[i].par
	}
	if (a >= 0777600) && (a < 0777620) {
		return pages[i+8].pdr
	}
	if (a >= 0777640) && (a < 0777660) {
		return pages[i+8].par
	}
	panic(trap{INTBUS, "invalid read from " + ostr(a, 6)})
}

func mmuwrite16(a uint18, v uint16) {
	i := ((a & 017) >> 1)
	if (a >= 0772300) && (a < 0772320) {
		pages[i] = createpage(pages[i].par, v)
		return
	}
	if (a >= 0772340) && (a < 0772360) {
		pages[i] = createpage(v, pages[i].pdr)
		return
	}
	if (a >= 0777600) && (a < 0777620) {
		pages[i+8] = createpage(pages[i+8].par, v)
		return
	}
	if (a >= 0777640) && (a < 0777660) {
		pages[i+8] = createpage(v, pages[i+8].pdr)
		return
	}
	panic(trap{INTBUS, "write to invalid address " + ostr(a, 6)})
}
*/
