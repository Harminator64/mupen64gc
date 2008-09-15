/* Wrappers.c - Dynarec trampoline and other functions to simplify access from dynarec
   by Mike Slegeir for Mupen64-GC
 */

#include <stdlib.h>
#include "../../gui/DEBUG.h"
#include "../Invalid_Code.h"
#include "../../gc_memory/memory.h"
#include "../interupt.h"
#include "../r4300.h"
#include "Wrappers.h"

extern int stop;
extern unsigned long instructionCount;
extern void (*interp_ops[64])(void);
inline unsigned long update_invalid_addr(unsigned long addr);


/* Recompiled code stack frame:
 *  $sp+28  |
 *  $sp+24  | old r16 (new r16 holds instruction count)
 *  $sp+20  | old r15 (new r15 holds decodeNInterpret)
 *  $sp+16  | old r14 (new r14 holds 0)
 * 	$sp+12	| old r13 (new r13 holds reg)
 * 	$sp+8	| old lr
 * 	$sp+4	| old cr
 * 	$sp		| old sp
 */

#define DYNAREC_PRELUDE() \
	__asm__ __volatile__ ( \
		"stwu	1, -32(1) \n" \
		"stw	13, 12(1) \n" \
		"mfcr	13        \n" \
		"stw	13, 8(1)  \n" \
		"mr		13, %0    \n" \
		"stw	14, 16(1) \n" \
		"addi	14, 0, 0  \n" \
		"stw	15, 20(1) \n" \
		"mr		15, %1    \n" \
		"stw	16, 24(1) \n" \
		"addi	16, 0, 0  \n" \
		:: "r" (reg), "r" (decodeNInterpret) )


void dynarec(unsigned int address){
	while(!stop){
		PowerPC_block* dst_block = blocks[address>>12];
		unsigned long paddr = update_invalid_addr(address);
		if(!paddr) return;
		
		// Check for interrupts
		update_count();
		if(next_interupt <= Count) gen_interupt();
		
		sprintf(txtbuffer, "trampolining to 0x%08x\n", address);
		DEBUG_print(txtbuffer, DBG_USBGECKO);
		
		if(!dst_block){
			sprintf(txtbuffer, "block at %08x doesn't exist\n", address&~0xFFF);
			DEBUG_print(txtbuffer, DBG_USBGECKO);
			blocks[address>>12] = malloc(sizeof(PowerPC_block));
			dst_block = blocks[address>>12];
			dst_block->code          = NULL;
			dst_block->code_addr     = NULL;
			dst_block->start_address = address & ~0xFFF;
			dst_block->end_address   = (address & ~0xFFF) + 0x1000;
			init_block(rdram+(((paddr-(address-dst_block->start_address)) & 0x1FFFFFFF)>>2),
				   dst_block);
		}
		
		if(invalid_code_get(address>>12)){
			dst_block->length = 0;
			recompile_block(dst_block);
		}
		
		// Recompute the block offset
		unsigned int (*code)(void);
		code = (unsigned int (*)(void))dst_block->code_addr[(address&0xFFF)>>2];
		
		sprintf(txtbuffer, "Entering dynarec code @ 0x%08x\n", code);
		DEBUG_print(txtbuffer, DBG_USBGECKO);
		
		DYNAREC_PRELUDE();
		address = code();
	}
}

unsigned int decodeNInterpret(MIPS_instr mips, unsigned int PC, unsigned int count){
	// Update the value for the instruction count
	instructionCount = count;
	interp_addr = PC;
	prefetch_opcode(mips);
	interp_ops[MIPS_GET_OPCODE(mips)]();
	return interp_addr != PC + 4 ? interp_addr : 0;
}
