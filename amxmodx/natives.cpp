// vim: set ts=4 sw=4 tw=99 noet:
//
// AMX Mod X, based on AMX Mod by Aleksander Naszko ("OLO").
// Copyright (C) The AMX Mod X Development Team.
//
// This software is licensed under the GNU General Public License, version 3 or higher.
// Additional exceptions apply. For full license details, see LICENSE.txt or visit:
//     https://alliedmods.net/amxmodx-license

#include "amxmodx.h"
#include "sh_stack.h"
#include "natives.h"
#include "debugger.h"
#include "libraries.h"
#include "format.h"

#if defined(__linux__) || defined(__APPLE__)
#if defined(__linux__)
#include <malloc.h>
#endif

#include <stdlib.h>
#include <sys/mman.h>
#include "sclinux.h"
#endif

//Written by David "BAILOPAN" Anderson
//With the exception for param_convert, which was written by
// Julien "dJeyL" Laurent

ke::Vector<regnative *> g_RegNatives;
static char g_errorStr[512] = {0};
bool g_Initialized = false;

/* Stack stuff */
regnative *g_pCurNative = NULL;
AMX *g_pCaller = NULL;
cell g_Params[CALLFUNC_MAXPARAMS + 1];
int g_CurError = AMX_ERR_NONE;

int amxx_DynaCallback(int idx, AMX *amx, cell *params)
{
	if (idx < 0 || idx >= (int)g_RegNatives.length())
	{
		LogError(amx, AMX_ERR_NATIVE, "Invalid dynamic native called");
		return 0;
	}

	regnative *pNative = g_RegNatives[idx];
	int numParams = params[0] / sizeof(cell);

	if (numParams > CALLFUNC_MAXPARAMS)
	{
		LogError(amx, AMX_ERR_NATIVE, "Called dynanative with too many parameters (%d)", CALLFUNC_MAXPARAMS);
		return 0;
	}

	CPluginMngr::CPlugin *pPlugin = g_plugins.findPluginFast(amx);
	CPluginMngr::CPlugin *pNativePlugin = g_plugins.findPluginFast(pNative->amx);

	if (!pNativePlugin->isExecutable(pNative->func))
	{
		LogError(amx, AMX_ERR_NATIVE, "Called dynanative into a paused plugin.");
		pPlugin->setStatus(ps_paused);
		return 0;
	}

	/* Save old values on ZE STACK */
	AMX *pSaveCaller = g_pCaller;
	cell saveParams[CALLFUNC_MAXPARAMS];
	regnative *pSaveNative = g_pCurNative;
	int saveError = g_CurError;

	if (pSaveNative)
	{
		for (ucell i = 0; i <= g_Params[0] / sizeof(cell); i++)
		{
			saveParams[i] = g_Params[i];
		}
	}

	/* Save current info */
	g_CurError = AMX_ERR_NONE;
	g_pCaller = amx;
	g_pCurNative = pNative;

	int err = 0;
	cell ret = 0;
	if (pNative->style == 0)
	{
		amx_Push(pNative->amx, numParams);
		amx_Push(pNative->amx, pPlugin->getId());
		for (int i=numParams; i>=0; i--)
		{
			g_Params[i] = params[i];
		}
	} else if (pNative->style == 1) {
		/**
		 * use dJeyL's system .. very clever!
		 * NOTE: clever, but doesn't work at all since the JIT does bounds checking
		 * this should REALLY be deprecated
		 */
		for (int i=numParams; i>=1; i--)
		{
			amx_Push(pNative->amx, params[i]);
		}
	}

	Debugger *pDebugger = (Debugger *)pNative->amx->userdata[UD_DEBUGGER];
	if (pDebugger)
	{
		pDebugger->BeginExec();
	}

	err = amx_ExecPerf(pNative->amx, &ret, pNative->func);
	if (err != AMX_ERR_NONE)
	{
		if (pDebugger && pDebugger->ErrorExists())
		{
			//don't care
		} else if (err != -1) {
			//nothing logged the error
			LogError(pNative->amx, err, NULL);
		}
		pNative->amx->error = AMX_ERR_NONE;
		//furthermore, log an error in the parent plugin.
		LogError(amx, AMX_ERR_NATIVE, "Unhandled dynamic native error");
	} else if (g_CurError != AMX_ERR_NONE) {
		LogError(amx, g_CurError, g_errorStr);
	}

	if (pDebugger)
	{
		pDebugger->EndExec();
	}

	/* Restore everything */
	g_pCurNative = pSaveNative;
	g_CurError = saveError;
	g_pCaller = pSaveCaller;
	if (pSaveNative)
	{
		for (ucell i = 0; i <= saveParams[0] / sizeof(cell); i++)
		{
			g_Params[i] = saveParams[i];
		}
	}

	return ret;
}

AMX_NATIVE_INFO *BuildNativeTable()
{
	if (g_RegNatives.length() < 1)
	{
		return NULL;
	}

	AMX_NATIVE_INFO *pNatives = new AMX_NATIVE_INFO[g_RegNatives.length() + 1];

	AMX_NATIVE_INFO info;
	regnative *pNative;
	for (size_t i=0; i<g_RegNatives.length(); i++)
	{
		pNative = g_RegNatives[i];
		info.name = pNative->name.chars();
		info.func = (AMX_NATIVE)((void *)(pNative->pfn));
		pNatives[i] = info;
	}
	pNatives[g_RegNatives.length()].name = NULL;
	pNatives[g_RegNatives.length()].func = NULL;

	//this needs to be deleted
	return pNatives;
}

static cell AMX_NATIVE_CALL log_error(AMX *amx, cell *params)
{
	int len;
	char *err = format_amxstring(amx, params, 2, len);

	ke::SafeSprintf(g_errorStr, sizeof(g_errorStr), "%s", err);
	g_CurError = params[1];

	return 1;
}

//get_string(param, dest[], len)
static cell AMX_NATIVE_CALL get_string(AMX *amx, cell *params)
{
	if (!g_pCurNative || (g_pCurNative->amx != amx))
	{
		LogError(amx, AMX_ERR_NATIVE, "Not currently in a dynamic native");
		return 0;
	}
	if (g_pCurNative->style)
	{
		LogError(amx, AMX_ERR_NATIVE, "Wrong style of dynamic native");
		return 0;
	}
	int p = params[1];

	int len;
	char *str = get_amxstring(g_pCaller, g_Params[p], 0, len);
	return set_amxstring(amx, params[2], str, params[3]);
}

//set_string(param, source[], maxlen)
static cell AMX_NATIVE_CALL set_string(AMX *amx, cell *params)
{
	if (!g_pCurNative || (g_pCurNative->amx != amx))
	{
		LogError(amx, AMX_ERR_NATIVE, "Not currently in a dynamic native");
		return 0;
	}
	if (g_pCurNative->style)
	{
		LogError(amx, AMX_ERR_NATIVE, "Wrong style of dynamic native");
		return 0;
	}
	int p = params[1];

	int len;
	char *str = get_amxstring(amx, params[2], 0, len);

	return set_amxstring(g_pCaller, g_Params[p], str, params[3]);
}

//get a byvalue parameter
//get_param(num)
static cell AMX_NATIVE_CALL get_param(AMX *amx, cell *params)
{
	if (!g_pCurNative || (g_pCurNative->amx != amx))
	{
		LogError(amx, AMX_ERR_NATIVE, "Not currently in a dynamic native");
		return 0;
	}
	if (g_pCurNative->style)
	{
		LogError(amx, AMX_ERR_NATIVE, "Wrong style of dynamic native");
		return 0;
	}
	int p = params[1];

	return g_Params[p];
}

//get_param_byref(num)
static cell AMX_NATIVE_CALL get_param_byref(AMX *amx, cell *params)
{
	if (!g_pCurNative || (g_pCurNative->amx != amx))
	{
		LogError(amx, AMX_ERR_NATIVE, "Not currently in a dynamic native");
		return 0;
	}
	if (g_pCurNative->style)
	{
		LogError(amx, AMX_ERR_NATIVE, "Wrong style of dynamic native");
		return 0;
	}
	int p = params[1];

	cell *addr = get_amxaddr(g_pCaller, g_Params[p]);

	return addr[0];
}

//set_param_byref(num, val)
static cell AMX_NATIVE_CALL set_param_byref(AMX *amx, cell *params)
{
	if (!g_pCurNative || (g_pCurNative->amx != amx))
	{
		LogError(amx, AMX_ERR_NATIVE, "Not currently in a dynamic native");
		return 0;
	}
	if (g_pCurNative->style)
	{
		LogError(amx, AMX_ERR_NATIVE, "Wrong style of dynamic native");
		return 0;
	}
	int p = params[1];

	cell *addr = get_amxaddr(g_pCaller, g_Params[p]);

	addr[0] = params[2];

	return 1;
}

//get_array(param, dest[], size)
static cell AMX_NATIVE_CALL get_array(AMX *amx, cell *params)
{
	if (!g_pCurNative || (g_pCurNative->amx != amx))
	{
		LogError(amx, AMX_ERR_NATIVE, "Not currently in a dynamic native");
		return 0;
	}
	if (g_pCurNative->style)
	{
		LogError(amx, AMX_ERR_NATIVE, "Wrong style of dynamic native");
		return 0;
	}
	int p = params[1];

	cell *source = get_amxaddr(g_pCaller, g_Params[p]);
	cell *dest = get_amxaddr(amx, params[2]);

	int size = params[3];

	memcpy(dest, source, size * sizeof(cell));

	return 1;
}

//set_array(param, source[], size)
static cell AMX_NATIVE_CALL set_array(AMX *amx, cell *params)
{
	if (!g_pCurNative || (g_pCurNative->amx != amx))
	{
		LogError(amx, AMX_ERR_NATIVE, "Not currently in a dynamic native");
		return 0;
	}
	if (g_pCurNative->style)
	{
		LogError(amx, AMX_ERR_NATIVE, "Wrong style of dynamic native");
		return 0;
	}
	int p = params[1];

	cell *dest = get_amxaddr(g_pCaller, g_Params[p]);
	cell *source = get_amxaddr(amx, params[2]);

	int size = params[3];

	memcpy(dest, source, size * sizeof(cell));

	return 1;
}

static cell AMX_NATIVE_CALL vdformat(AMX *amx, cell *params)
{
	if (!g_pCurNative || (g_pCurNative->amx != amx))
	{
		LogError(amx, AMX_ERR_NATIVE, "Not currently in a dynamic native");
		return 0;
	}

	if (g_pCurNative->style)
	{
		LogError(amx, AMX_ERR_NATIVE, "Wrong style of dynamic native");
		return 0;
	}

	int vargPos = static_cast<int>(params[4]);
	int fargPos = static_cast<int>(params[3]);

	cell max = g_Params[0] / sizeof(cell);
	if (vargPos > (int)max + 1)
	{
		LogError(amx, AMX_ERR_NATIVE, "Invalid vararg parameter passed: %d", vargPos);
		return 0;
	}
	if (fargPos > (int)max + 1)
	{
		LogError(amx, AMX_ERR_NATIVE, "Invalid fmtarg parameter passed: %d", fargPos);
		return 0;
	}

	/* get destination info */
	cell *fmt;
	if (fargPos == 0)
	{
		if (params[0] / sizeof(cell) != 5)
		{
			LogError(amx, AMX_ERR_NATIVE, "Expected fmtarg as fifth parameter, found none");
			return 0;
		}
		fmt = get_amxaddr(amx, params[5]);
	} else {
		fmt = get_amxaddr(g_pCaller, g_Params[fargPos]);
	}
	cell *realdest = get_amxaddr(amx, params[1]);
	size_t maxlen = static_cast<size_t>(params[2]);
	cell *dest = realdest;

	/* if this is necessary... */
	static cell cpbuf[4096];
	dest = cpbuf;

	/* perform format */
	size_t total = atcprintf(dest, maxlen, fmt, g_pCaller, g_Params, &vargPos);

	/* copy back */
	memcpy(realdest, dest, (total+1) * sizeof(cell));

	return total;
}

//This is basically right from dJeyL's lib_convert function
//This awesome hack modifies the stack frame to have an address offset
// that will align to the other plugin's memory.
//I've no idea how he thought of this, but it's great.  No idea how well it works.
static cell AMX_NATIVE_CALL param_convert(AMX *amx, cell *params)
{
	if (!g_pCurNative || (g_pCurNative->amx != amx))
	{
		LogError(amx, AMX_ERR_NATIVE, "Not currently in a dynamic native");
		return 0;
	}
	if (g_pCurNative->style != 1)
	{
		LogError(amx, AMX_ERR_NATIVE, "Wrong style of dynamic native");
		return 0;
	}
	cell p = params[1];

	AMX *caller = g_pCaller;

	/* Slot on our own stack holding the caller-space string address. */
	unsigned char *data = amx->base + ((AMX_HEADER *)amx->base)->dat;
	cell *slot = (cell *)(data + amx->frm + (p + 2) * (int)sizeof(cell));
	cell caller_addr = *slot;

	/* Park helper: never leave a wild address behind. On any failure the
	 * slot is repointed at an empty string so the caller keeps running
	 * with "" instead of crashing in strncopy(). */
	cell empty_addr = 0;
	cell *empty_phys = NULL;
	bool have_empty = (amx_Allot(amx, 1, &empty_addr, &empty_phys) == AMX_ERR_NONE);
	if (have_empty)
		*empty_phys = 0;

	/* Resolve the string in the CALLER's address space with full 64-bit
	 * precision. The old code truncated both image bases to 32-bit cells
	 * and subtracted them; the result is only valid while both plugins
	 * live inside the same 4GB window (true on x86 desktops, often false
	 * on Android ARM64), otherwise it yields a wild pointer -> SIGSEGV. */
	int caller_dat = ((AMX_HEADER *)caller->base)->dat;

	/* Sanity: AMX addresses (params, frm, stp, ...) are relative to the
	 * DATA section (amx->data), not to the image base -- e.g. 552 is a
	 * perfectly good string 552 bytes into the caller's data. Anything
	 * outside [0, stp) means a corrupt caller stack. (Runtime stp itself
	 * is dat-relative: amx->stp = hdr->stp - hdr->dat - sizeof(cell).) */
	if (caller_addr < 0 || caller_addr >= caller->stp)
	{
		LogError(amx, AMX_ERR_NATIVE, "param_convert: caller string address out of range (%d)", caller_addr);
		if (have_empty)
			*slot = empty_addr;
		return 0;
	}

	cell *src = (cell *)((unsigned char *)caller->base + caller_dat + caller_addr);
	cell *limit = (cell *)((unsigned char *)caller->base + caller_dat + caller->stp);

	/* Bounded length scan: never read past the caller's image. */
	int len = 0;
	while (len < 8192 && &src[len] < limit && src[len] != 0)
		len++;
	if (len >= 8192 || &src[len] >= limit)
	{
		LogError(amx, AMX_ERR_NATIVE, "param_convert: unterminated caller string");
		if (have_empty)
			*slot = empty_addr;
		return 0;
	}

	/* A 32-bit cell can never reference caller memory across images, so
	 * copy the string into our own heap and repoint the slot at the copy.
	 * All in-tree users are input-only, which behaves identically. */
	cell new_addr = 0;
	cell *dst = NULL;
	if (amx_Allot(amx, len + 1, &new_addr, &dst) != AMX_ERR_NONE)
	{
		LogError(amx, AMX_ERR_MEMORY, "param_convert: out of heap space");
		if (have_empty)
			*slot = empty_addr;
		return 0;
	}

	memcpy(dst, src, (len + 1) * sizeof(cell));
	*slot = new_addr;

	return 1;
}

static cell AMX_NATIVE_CALL register_library(AMX *amx, cell *params)
{
	int len;
	char *lib = get_amxstring(amx, params[1], 0, len);

	AddLibrary(lib, LibType_Library, LibSource_Plugin, g_plugins.findPluginFast(amx));

	return 1;
}

//register_native(const name[], const handler[])
static cell AMX_NATIVE_CALL register_native(AMX *amx, cell *params)
{
	if (!g_Initialized)
		amxx_DynaInit((void *)(amxx_DynaCallback));

	g_Initialized = true;

	int len;
	char *name = get_amxstring(amx, params[1], 0, len);
	char *func = get_amxstring(amx, params[2], 1, len);

	int idx, err;
	if ( (err=amx_FindPublic(amx, func, &idx)) != AMX_ERR_NONE)
	{
		LogError(amx, err, "Function \"%s\" was not found", func);
		return 0;
	}

	regnative *pNative = new regnative;
	pNative->amx = amx;
	pNative->func = idx;
	
	//we'll apply a safety buffer too
	//make our function
	int size = amxx_DynaCodesize();
#if defined(_WIN32)
	DWORD temp;
	pNative->pfn = new char[size + 10];
	VirtualProtect(pNative->pfn, size+10, PAGE_EXECUTE_READWRITE, &temp);
#elif defined(__GNUC__)
# if defined(__APPLE__)
	pNative->pfn = (char *)valloc(size+10);
	mprotect((void *)pNative->pfn, size + 10, PROT_READ | PROT_WRITE | PROT_EXEC);
# else
	pNative->pfn = (char *)mmap(nullptr, size + 10, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
# endif
#endif

	int id = (int)g_RegNatives.length();
	
	amxx_DynaMake(pNative->pfn, id);
	pNative->func = idx;
	pNative->style = params[3];

	g_RegNatives.append(pNative);

	pNative->name = name;

	return 1;
}

int GetRegisteredNativesCount()
{
	return (int)g_RegNatives.length();
}

void ClearPluginLibraries()
{
	ClearLibraries(LibSource_Plugin);
	for (size_t i=0; i<g_RegNatives.length(); i++)
	{
#ifdef __linux__
		munmap(g_RegNatives[i]->pfn, amxx_DynaCodesize() + 10);
#else
		delete [] g_RegNatives[i]->pfn;
#endif
		delete g_RegNatives[i];
	}
	g_RegNatives.clear();
}

AMX_NATIVE_INFO g_NativeNatives[] = {
	{"register_native",	register_native},
	{"log_error",		log_error},
	{"register_library",register_library},
	{"get_string",		get_string},
	{"set_string",		set_string},
	{"get_param",		get_param},
	{"get_param_byref",	get_param_byref},
	{"set_param_byref",	set_param_byref},
	{"get_array",		get_array},
	{"set_array",		set_array},
	//these are dummy functions for floats ;p
	{"get_param_f",		get_param},
	{"get_float_byref",	get_param_byref},
	{"set_float_byref", set_param_byref},
	{"get_array_f",		get_array},
	{"set_array_f",		set_array},
	{"vdformat",		vdformat},
	{"param_convert",	param_convert},
	//////////////////////////
	{NULL,				NULL},
};
