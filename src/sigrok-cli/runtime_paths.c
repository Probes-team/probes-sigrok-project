/*
 * This file is part of the sigrok-cli project.
 *
 * Copyright (C) 2026 Probes Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <config.h>
#include <glib.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif
#include "runtime_paths.h"

int runtime_paths_init(void)
{
#ifdef G_OS_WIN32
	WCHAR executable_path[32768];
	DWORD path_length;
	char *utf8_path, *package_root, *python_home;

	path_length = GetModuleFileNameW(NULL, executable_path,
			G_N_ELEMENTS(executable_path));
	if (!path_length || path_length == G_N_ELEMENTS(executable_path))
		return 0;

	utf8_path = g_utf16_to_utf8((const gunichar2 *)executable_path,
			path_length, NULL, NULL, NULL);
	if (!utf8_path)
		return -1;
	package_root = g_path_get_dirname(utf8_path);
	python_home = g_build_filename(package_root, "python", NULL);
	g_free(package_root);
	g_free(utf8_path);

	if (g_file_test(python_home, G_FILE_TEST_IS_DIR))
		g_setenv("PYTHONHOME", python_home, TRUE);
	/* A portable package must remain immutable while loading decoders. */
	g_setenv("PYTHONDONTWRITEBYTECODE", "1", TRUE);
	g_free(python_home);
#endif

	return 0;
}
