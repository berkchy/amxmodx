// vim: set ts=4 sw=4 tw=99 noet:
//
// AMX Mod X, based on AMX Mod by Aleksander Naszko ("OLO").
// Copyright (C) The AMX Mod X Development Team.
//
// This software is licensed under the GNU General Public License, version 3 or higher.
// Additional exceptions apply. For full license details, see LICENSE.txt or visit:
//     https://alliedmods.net/amxmodx-license

#pragma once

#include "platform_helpers.h" 
#include <amtl/os/am-path.h>
#include <amtl/os/am-shared-library.h>

template <typename T>
bool GET_IFACE(const char* library, T*& var, const char* version, bool add_ext = true)
{
	char file[PLATFORM_MAX_PATH];

	if (add_ext)
		ke::path::Format(file, sizeof(file), "%s.%s", library, PLATFORM_LIB_EXT);
	else
		ke::SafeStrcpy(file, sizeof(file), library);

	auto lib = ke::RefPtr<ke::SharedLib>(ke::SharedLib::Open(file));

#if defined(__ANDROID__)
	/* Xash3D FWGS on Android loads the game library (libcs_android_arm64.so)
	 * by an absolute path inside the APK, so an absolute or relative path from
	 * GET_GAME_INFO (e.g. "dlls/libcs_android_arm64.so") commonly cannot be
	 * dlopen()ed on bionic. Retry with just the basename (SONAME); dlopen()
	 * resolves it against the already-loaded mapping instead of loading a new
	 * copy. */
	if (!lib || !lib->valid())
	{
		const char *base = file;
		for (const char *p = file; *p; p++)
		{
			if (*p == '/')
			{
				base = p + 1;
			}
		}
		if (base[0])
		{
			lib = ke::SharedLib::Open(base);
		}
	}
#endif

	if (!lib || !lib->valid())
	{
		return false;
	}

	auto factory = reinterpret_cast<CreateInterfaceFn>(lib->lookup(CREATEINTERFACE_PROCNAME));

	if (factory)
	{
		var = reinterpret_cast<T*>(factory(version, nullptr));
		return true;
	}

	var = nullptr;

	return false;
}
