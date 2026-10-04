// vim: set ts=4 sw=4 tw=99 noet:
//
// AMX Mod X, based on AMX Mod by Aleksander Naszko ("OLO").
// Copyright (C) The AMX Mod X Development Team.
//
// This software is licensed under the GNU General Public License, version 3 or higher.
// Additional exceptions apply. For full license details, see LICENSE.txt or visit:
//     https://alliedmods.net/amxmodx-license

//
// Ham Sandwich Module
//

#ifndef TRAMPOLINES_H
#define TRAMPOLINES_H

#ifndef NDEBUG
#define TPRINT(msg)  printf msg
#else
#define TPRINT(msg) /* nothing */
#endif

#if defined _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#if _MSC_VER >= 1400
#ifdef offsetof
#undef offsetof
#endif // offsetof
#endif // _MSC_VER >= 1400
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#if defined (__linux__)
#include <malloc.h>
#endif
#endif
#include <stddef.h> // size_t
#include <string.h> // memcpy
#include <stdint.h> // uint32_t (ARM64 trampolines)
#include <stdlib.h> // memalign
#include <stdio.h>

#include <amtl/am-bits.h>

namespace Trampolines
{

	/**
	 * List of x86 bytecodes for creating
	 * basic trampolines at runtime.
	 * -
	 * These are defined here so that, should
	 * the need ever arise, this can be ported
	 * to other architectures fairly painlessly
	 */
	namespace Bytecode
	{
		/**
		 * Prologue for a function
		 */
		const unsigned char codePrologue[] = {
			0x55,						// push ebp
			0x89, 0xE5,					// mov ebp, esp
		};

		/**
		 * Align stack on 16 byte boundary
		 */
		const unsigned char codeAlignStack16[] = {
			0x83, 0xE4, 0xF0,				// and esp, 0xFFFFFFF0
		};

		/**
		 * Allocate stack space (8-bit) by adding to ESP
		 */
		const unsigned char codeAllocStack[] = {
			0x83, 0xEC, 0xFF,				// sub esp, 0xFF
		};

		/**
		 * Offset of codeAllocStack to modify at runtime
		 * to contain amount of stack space to allocate. 
		 */
		const unsigned int codeAllocStackReplace = 2;

		/**
		 * Takes a paramter from the trampoline's stack
		 * and pushes it onto the target's stack.
		 */
		const unsigned char codePushParam[] = {
			0xFF, 0x75, 0xFF			// pushl [ebp+0xFF]
		};

		/**
		 * Offset of codePushParam to modify at runtime
		 * that contains the stack offset
		 */
		const unsigned int codePushParamReplace = 2;


		/**
		 * Takes the "this" pointer from the trampoline and
		 * pushes it onto the target's stack.
		 */
		const unsigned char codePushThis[] = {
		#if defined(_WIN32)
			0x51						// push ecx
		#elif defined(__linux__) || defined(__APPLE__)
			0xFF, 0x75, 0x04			// pushl [ebp+0x08h]
		#endif
		};

#if defined(__linux__) || defined(__APPLE__)
		const int codePushThisReplace = 2;
#endif

		/**
		 * Pushes a raw number onto the target's stack
		 */
		const unsigned char codePushID[] = {
			0x68, 0xDE, 0xFA, 0xAD, 0xDE	// push	DEADFADEh
		};

		/**
		 * Offset of codePushID to modify at runtime
		 * to contain the number to push
		 */
		const unsigned int codePushIDReplace = 1;

		/**
		 * Call our procedure
		 */
		const unsigned char codeCall[] = {
			0xB8, 0xDE, 0xFA, 0xAD, 0xDE,// mov eax, DEADFADEh
			0xFF, 0xD0					// call eax
		};

		/**
		 * Offset of codeCall to modify at runtime
		 * to contain the pointer to the function
		 */
		const unsigned int codeCallReplace = 1;

		/**
		 * Adds to ESP, freeing up stack space
		 */
		const unsigned char codeFreeStack[] = {
			0x81, 0xC4, 0xFF, 0xFF, 0xFF, 0xFF// add esp REPLACEME
		};

		/**
		 * Offset of codeFreeStack to modify at runtime
		 * to contain how much data to free
		 */
		const unsigned int codeFreeStackReplace = 2;

		/**
		 * Epilogue of a simple function
		 */
		const unsigned char codeEpilogue[] = {
				0x89, 0xEC,					// mov esp, ebp
				0x5D,						// pop ebp
				0xC3						// ret
		};
		const unsigned char codeEpilogueN[] = {
				0x89, 0xEC,					// mov esp, ebp
				0x5D,						// pop ebp
				0xC2, 0xCD, 0xAB				// retn 0xABCD
		};
		const int codeEpilogueNReplace = 4;

		const unsigned char codeBreakpoint[] = {
			0xCC							// int 3
		};

	}

	/**
	 * Our actual maker of the trampolines!!@$
	 * I've no idea why I made this a class and not a namespace
	 * Oh well!
	 */

	class TrampolineMaker
	{
	private:
		unsigned char		*m_buffer;			// the actual buffer containing the code
		int					 m_size;			// size of the buffer
		int					 m_mystack;			// stack for the trampoline itself
		int					 m_calledstack;		// stack for the target function
		int					 m_paramstart;
		int					 m_thiscall;
		int					 m_maxsize;

		/**
		 * Adds data to the buffer
		 * data must be pre-formatted before hand!
		 */
		void Append(const unsigned char *src, size_t size)
		{
			int orig=m_size;
			m_size+=size;

			if (m_buffer==NULL)
			{
				m_maxsize=512;
				m_buffer=(unsigned char *)malloc(m_maxsize);
			}
			else if (m_size > m_maxsize)
			{
				m_maxsize = m_size + 512;
				m_buffer=(unsigned char *)realloc(m_buffer,m_maxsize);
			}

			unsigned char *dat=m_buffer+orig; // point dat to the end of the prewritten 

			while (orig<m_size)
			{
				*dat++=*src++;

				orig++;
			};

		};
	public:
		TrampolineMaker()
		{
			m_buffer=NULL;
			m_size=0;
			m_mystack=0;
			m_calledstack=0;
			m_paramstart=0;
			m_thiscall=0;
			m_maxsize=0;
		};

		/**
		 * Inserts a breakpoint (int 3) into the trampoline.
		 */
		void Breakpoint()
		{
			Append(&::Trampolines::Bytecode::codeBreakpoint[0],sizeof(::Trampolines::Bytecode::codeBreakpoint));
		};

		/**
		 * Adds the prologue, pushes registers, prepares the stack
		 */
		void Prologue()
		{
			Append(&::Trampolines::Bytecode::codePrologue[0],sizeof(::Trampolines::Bytecode::codePrologue));
			m_paramstart=0;
			m_thiscall=0;
		};

		/**
		 * Flags this trampoline as a thiscall trampoline, and prepares the prologue.
		 */
		void ThisPrologue()
		{
			this->Prologue();
			m_thiscall=1;
		};

		/**
		 * Epilogue for a function pops registers but does not free any more of the stack!
		 */
		void Epilogue()
		{
			Append(&::Trampolines::Bytecode::codeEpilogue[0],sizeof(::Trampolines::Bytecode::codeEpilogue));
		};

		/**
		 * Epilogue that also frees it's estimated stack usage.  Useful for stdcall/thiscall/fastcall.
		 */
		void EpilogueAndFree()
		{
			this->Epilogue(m_mystack);
		};

		/**
		 * Epilogue.  Pops registers, and frees given amount of data from the stack.
		 *
		 * @param howmuch			How many bytes to free from the stack.
		 */
		void Epilogue(int howmuch)
		{
			unsigned char code[sizeof(::Trampolines::Bytecode::codeEpilogueN)];

			memcpy(&code[0],&::Trampolines::Bytecode::codeEpilogueN[0],sizeof(::Trampolines::Bytecode::codeEpilogueN));


			unsigned char *c=&code[0];

			union 
			{
				int		i;
				unsigned char	b[4];
			} bi;

			bi.i=howmuch;

			c+=::Trampolines::Bytecode::codeEpilogueNReplace;
			*c++=bi.b[0];
			*c++=bi.b[1];

			Append(&code[0],sizeof(::Trampolines::Bytecode::codeEpilogueN));
		};

		/**
		 * Aligns stack on 16 byte boundary for functions that use aligned SSE instructions.
		 * This also allocates extra stack space to allow the specified number of slots to be used
		 * for function paramaters that will be pushed onto the stack.
		 */
		void AlignStack16(int slots)
		{
			const size_t stackNeeded = slots * sizeof(void *);
			const size_t stackReserve = ke::Align(stackNeeded, 16);
			const size_t stackExtra = stackReserve - stackNeeded;

			// Stack space should fit in a byte
			assert(stackExtra <= 0xFF);

			const size_t codeAlignStackSize = sizeof(::Trampolines::Bytecode::codeAlignStack16);
			const size_t codeAllocStackSize = sizeof(::Trampolines::Bytecode::codeAllocStack);
			unsigned char code[codeAlignStackSize + codeAllocStackSize];

			memcpy(&code[0], &::Trampolines::Bytecode::codeAlignStack16[0], codeAlignStackSize);

			if (stackExtra > 0)
			{
				unsigned char *c = &code[codeAlignStackSize];
				memcpy(c, &::Trampolines::Bytecode::codeAllocStack[0], codeAllocStackSize);

				c += ::Trampolines::Bytecode::codeAllocStackReplace;
				*c = (unsigned char)stackExtra;

				Append(&code[0], codeAlignStackSize + codeAllocStackSize);
			}
			else
			{
				Append(&code[0], codeAlignStackSize);
			}
		}

		/**
		 * Pushes the "this" pointer onto the callee stack.  Pushes ECX for MSVC, and param0 on GCC.
		 */
		void PushThis()
		{

			if (!m_thiscall)
			{
				return;
			}

			unsigned char code[sizeof(::Trampolines::Bytecode::codePushThis)];

			memcpy(&code[0],&::Trampolines::Bytecode::codePushThis[0],sizeof(::Trampolines::Bytecode::codePushThis));


#if defined(__linux__) || defined(__APPLE__)
			unsigned char *c=&code[0];

			union 
			{
				int		i;
				unsigned char	b[4];
			} bi;

			bi.i=m_paramstart+8;

			c+=::Trampolines::Bytecode::codePushThisReplace;
			*c++=bi.b[0];
#endif

			Append(&code[0],sizeof(::Trampolines::Bytecode::codePushThis));

#if defined(__linux__) || defined(__APPLE__)
			m_mystack+=4;
#endif
			m_calledstack+=4;
		};

		/**
		 * Frees what is estimated as the stack usage of the trampoline.
		 */
		void FreeMyStack(void)
		{

			this->FreeStack(m_mystack);
		};

		/**
		 * Frees the estimated stack usage of the callee.
		 */
		void FreeTargetStack(void)
		{
			this->FreeStack(m_calledstack);
		};


		/**
		 * Frees the estimated stack usage of the callee and the trampoline.
		 */
		void FreeBothStacks(void)
		{
			this->FreeStack(m_calledstack + m_mystack);
		};

		/**
		 * Frees a given amount of bytes from the stack.
		 *
		 * @param howmuch			How many bytes to free.
		 */
		void FreeStack(int howmuch)
		{
			unsigned char code[sizeof(::Trampolines::Bytecode::codeFreeStack)];

			memcpy(&code[0],&::Trampolines::Bytecode::codeFreeStack[0],sizeof(::Trampolines::Bytecode::codeFreeStack));

			unsigned char *c=&code[0];

			union 
			{
				int		i;
				unsigned char	b[4];
			} bi;

			bi.i=howmuch;

			c+=::Trampolines::Bytecode::codeFreeStackReplace;
			*c++=bi.b[0];
			*c++=bi.b[1];
			*c++=bi.b[2];
			*c++=bi.b[3];

			Append(&code[0],sizeof(::Trampolines::Bytecode::codeFreeStack));

		};

		/**
		 * Pushes a raw number onto the callee stack.
		 *
		 * @param Number			The number to push onto the callee stack.
		 */
		void PushNum(int Number)
		{
			unsigned char code[sizeof(::Trampolines::Bytecode::codePushID)];

			memcpy(&code[0],&::Trampolines::Bytecode::codePushID[0],sizeof(::Trampolines::Bytecode::codePushID));

			unsigned char *c=&code[0];

			union 
			{
				int		i;
				unsigned char	b[4];
			} bi;

			bi.i=Number;

			c+=::Trampolines::Bytecode::codePushIDReplace;
			*c++=bi.b[0];
			*c++=bi.b[1];
			*c++=bi.b[2];
			*c++=bi.b[3];

			Append(&code[0],sizeof(::Trampolines::Bytecode::codePushID));

			m_calledstack+=4; // increase auto detected stack size

		};


		/**
		 * Takes a parameter passed on the trampoline's stack and inserts it into the callee's stack.
		 *
		 * @param which			The parameter number to push. 1-based.  "thiscall" trampolines automatically compensate for the off-number on GCC.
		 */
		void PushParam(int which)
		{
#if defined(__linux__) || defined(__APPLE__)
			if (m_thiscall)
			{
				which++;
			}
#endif
			which=which*4;
			which+=m_paramstart+4;

			unsigned char value=which;

			unsigned char code[sizeof(::Trampolines::Bytecode::codePushParam)];

			memcpy(&code[0],&::Trampolines::Bytecode::codePushParam[0],sizeof(::Trampolines::Bytecode::codePushParam));

			unsigned char *c=&code[0];


			c+=::Trampolines::Bytecode::codePushParamReplace;

			*c=value;

			Append(&code[0],sizeof(::Trampolines::Bytecode::codePushParam));

			m_calledstack+=4; // increase auto detected stack size
			m_mystack+=4;

		};

		/**
		 * Insert a function to call into the trampoline.
		 * 
		 * @param ptr			The function to call, cast to void*.
		 */
		void Call(void *ptr)
		{
			unsigned char code[sizeof(::Trampolines::Bytecode::codeCall)];

			memcpy(&code[0],&::Trampolines::Bytecode::codeCall[0],sizeof(::Trampolines::Bytecode::codeCall));

			unsigned char *c=&code[0];

			union 
			{
				void	*p;
				unsigned char	 b[4];
			} bp;

			bp.p=ptr;

			c+=::Trampolines::Bytecode::codeCallReplace;

			*c++=bp.b[0];
			*c++=bp.b[1];
			*c++=bp.b[2];
			*c++=bp.b[3];
			Append(&code[0],sizeof(::Trampolines::Bytecode::codeCall));


		};

		/**
		 * Finalizes the trampoline.  Do not try to modify it after this.
		 *
		 * @param size			A pointer to retrieve the size of the trampoline. Ignored if set to NULL.
		 * @return				The trampoline pointer, cast to void*.
		 */
		void *Finish(int *size)
		{
			//void *ret=(void *)m_buffer;

			if (size)
			{
				*size=m_size;
			}

			// Reallocate with proper flags
#if defined(_WIN32)
			void *ret=VirtualAlloc(NULL, m_size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#elif defined(__GNUC__)
# if defined(__APPLE__)
			void *ret = valloc(m_size);
			mprotect(ret,m_size,PROT_READ|PROT_WRITE|PROT_EXEC);
# else
			void *ret=mmap(nullptr, m_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
# endif
#endif
			memcpy(ret, m_buffer, m_size);


			m_size=0;

			free(m_buffer);

			m_buffer=NULL; // so we don't accidentally rewrite!
			m_mystack=0;
			m_calledstack=0;
			m_maxsize=512;

			return ret;
		};
	};
};


/**
 * Utility to make a generic trampoline.
 */
inline void *CreateGenericTrampoline(bool thiscall, bool voidcall, bool retbuf, int paramcount, void *extraptr, void *callee, int *size)
{
#if defined(__aarch64__)
	/* ARM64 (AAPCS64) trampolines. The x86 byte templates above cannot
	 * execute here, so fixed ARM64 code is emitted instead. The game calls
	 * with x0=this, x1-x7=args (every Ham hook fits its integer args in
	 * x0-x6; floats stay in v-regs, which are never touched; x8, the
	 * indirect-result register, is preserved). The gate wants
	 * (Hook*, this, args...), i.e. integer registers shifted by one. The
	 * retbuf variant (Vector returns) additionally takes an out pointer in
	 * x1 (args shifted by two) backed by 16 bytes of scratch, written back
	 * to v0 before returning. sp stays 16-byte aligned; x19-x28 and d8-d15
	 * are untouched, so all return values pass through untouched.
	 * Every encoding below was verified with clang -target aarch64. */
	(void)thiscall;
	(void)voidcall;
	(void)paramcount;

	static const uint32_t tpl[] = {
		0xA9BF7BFD,	/* stp x29, x30, [sp, #-16]! */
		0x910003FD,	/* mov x29, sp */
		0xAA0603E7,	/* mov x7, x6 */
		0xAA0503E6,	/* mov x6, x5 */
		0xAA0403E5,	/* mov x5, x4 */
		0xAA0303E4,	/* mov x4, x3 */
		0xAA0203E3,	/* mov x3, x2 */
		0xAA0103E2,	/* mov x2, x1 */
		0xAA0003E1,	/* mov x1, x0 */
		0xD2800000,	/* movz x0, #hook_lo (patched) */
		0xF2A00000,	/* movk x0, #hook_16, lsl #16 (patched) */
		0xF2C00000,	/* movk x0, #hook_32, lsl #32 (patched) */
		0xF2E00000,	/* movk x0, #hook_48, lsl #48 (patched) */
		0xD2800010,	/* movz x16, #gate_lo (patched) */
		0xF2A00010,	/* movk x16, #gate_16, lsl #16 (patched) */
		0xF2C00010,	/* movk x16, #gate_32, lsl #32 (patched) */
		0xF2E00010,	/* movk x16, #gate_48, lsl #48 (patched) */
		0xD63F0200,	/* blr x16 */
		0xA8C17BFD,	/* ldp x29, x30, [sp], #16 */
		0xD65F03C0,	/* ret */
	};
	static const uint32_t tpl_retbuf[] = {
		0xA9BF7BFD,	/* stp x29, x30, [sp, #-16]! */
		0xD10043FF,	/* sub sp, sp, #16 (out scratch) */
		0x910003FD,	/* mov x29, sp */
		0xAA0503E7,	/* mov x7, x5 */
		0xAA0403E6,	/* mov x6, x4 */
		0xAA0303E5,	/* mov x5, x3 */
		0xAA0203E4,	/* mov x4, x2 */
		0xAA0103E3,	/* mov x3, x1 */
		0xAA0003E2,	/* mov x2, x0 */
		0xAA1D03E1,	/* mov x1, x29 (out = scratch) */
		0xD2800000,	/* movz x0, #hook_lo (patched) */
		0xF2A00000,	/* movk x0, #hook_16, lsl #16 (patched) */
		0xF2C00000,	/* movk x0, #hook_32, lsl #32 (patched) */
		0xF2E00000,	/* movk x0, #hook_48, lsl #48 (patched) */
		0xD2800010,	/* movz x16, #gate_lo (patched) */
		0xF2A00010,	/* movk x16, #gate_16, lsl #16 (patched) */
		0xF2C00010,	/* movk x16, #gate_32, lsl #32 (patched) */
		0xF2E00010,	/* movk x16, #gate_48, lsl #48 (patched) */
		0xD63F0200,	/* blr x16 */
		0xBD4003E0,	/* ldr s0, [sp] (Vector.x writeback to V0) */
		0xBD4007E1,	/* ldr s1, [sp, #4] (Vector.y writeback to V1) */
		0xBD400BE2,	/* ldr s2, [sp, #8] (Vector.z writeback to V2) */
		0x910043FF,	/* add sp, sp, #16 */
		0xA8C17BFD,	/* ldp x29, x30, [sp], #16 */
		0xD65F03C0,	/* ret */
	};

	const uint32_t *tpl_use = retbuf ? tpl_retbuf : tpl;
	int hi = retbuf ? 10 : 9;
	int gi = retbuf ? 14 : 13;
	int bytes = (int)(retbuf ? sizeof(tpl_retbuf) : sizeof(tpl));

	uint32_t buf[25];
	memcpy(buf, tpl_use, bytes);

	uintptr_t hook = (uintptr_t)extraptr;
	uintptr_t gate = (uintptr_t)callee;
	buf[hi + 0] |= ((uint32_t)(hook & 0xFFFF)) << 5;
	buf[hi + 1] |= ((uint32_t)((hook >> 16) & 0xFFFF)) << 5;
	buf[hi + 2] |= ((uint32_t)((hook >> 32) & 0xFFFF)) << 5;
	buf[hi + 3] |= ((uint32_t)((hook >> 48) & 0xFFFF)) << 5;
	buf[gi + 0] |= ((uint32_t)(gate & 0xFFFF)) << 5;
	buf[gi + 1] |= ((uint32_t)((gate >> 16) & 0xFFFF)) << 5;
	buf[gi + 2] |= ((uint32_t)((gate >> 32) & 0xFFFF)) << 5;
	buf[gi + 3] |= ((uint32_t)((gate >> 48) & 0xFFFF)) << 5;

	/* Same lifetime rules as TrampolineMaker::Finish(): RWX mmap, sized
	 * for munmap() in ~Hook. ARM64 needs an instruction-cache flush after
	 * writing code, otherwise the CPU may execute stale cache lines. */
	void *exec = mmap(NULL, bytes, PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	memcpy(exec, buf, bytes);
	__builtin___clear_cache((char *)exec, (char *)exec + bytes);
	if (size)
		*size = bytes;
	return exec;
#else
	Trampolines::TrampolineMaker tramp;

	if (thiscall)
	{
		tramp.ThisPrologue();
		tramp.AlignStack16(paramcount + 2);	// Param count + this ptr + extra ptr
	}
	else
	{
		tramp.Prologue();
		tramp.AlignStack16(paramcount + 1);	// Param count + extra ptr
	}

	while (paramcount)
	{
		tramp.PushParam(paramcount--);
	}
	if (thiscall)
	{
		tramp.PushThis();
	}
	tramp.PushNum(reinterpret_cast<int>(extraptr));
	tramp.Call(callee);
	tramp.FreeTargetStack();

#if defined(_WIN32)
	tramp.EpilogueAndFree();
#elif defined(__linux__) || defined(__APPLE__)
	if (retbuf)
	{
		tramp.Epilogue(4);
	}
	else
	{
		tramp.Epilogue();
	}
#endif

	return tramp.Finish(size);
#endif
};


#endif // TRAMPOLINEMANAGER_H
