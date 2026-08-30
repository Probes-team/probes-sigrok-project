/*
 * Restricted elevated installer for the packaged WCH CH375 x64 driver.
 *
 * The caller can select only the compiled-in entry ID. The manifest digest and
 * every payload digest are generated at build time from driver-manifest.json.
 */
#include <windows.h>
#include <wincrypt.h>
#include <string.h>
#include <wchar.h>

#include "driver_manifest_data.h"

#define WCH_ENTRY_ID L"wch-ch375-x64"
#define WCH_INSTALL_TIMEOUT_MS 120000
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int append_path(wchar_t *path, size_t capacity, const wchar_t *suffix)
{
	if (wcslen(path) + wcslen(suffix) >= capacity)
		return 0;
	wcscat(path, suffix);
	return 1;
}

static int get_sha256(const wchar_t *path, char output[65])
{
	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	HANDLE file = INVALID_HANDLE_VALUE;
	BYTE buffer[4096];
	BYTE digest[32];
	DWORD read;
	DWORD digest_size = sizeof(digest);
	DWORD index;
	static const char hex[] = "0123456789abcdef";
	int ok = 0;

	file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		goto done;
	if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES,
		CRYPT_VERIFYCONTEXT))
		goto done;
	if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash))
		goto done;
	for (;;) {
		if (!ReadFile(file, buffer, sizeof(buffer), &read, NULL))
			goto done;
		if (!read)
			break;
		if (!CryptHashData(hash, buffer, read, 0))
			goto done;
	}
	if (!CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0) ||
		digest_size != sizeof(digest))
		goto done;
	for (index = 0; index < sizeof(digest); index++) {
		output[index * 2] = hex[digest[index] >> 4];
		output[index * 2 + 1] = hex[digest[index] & 0x0f];
	}
	output[64] = '\0';
	ok = 1;

done:
	if (hash)
		CryptDestroyHash(hash);
	if (provider)
		CryptReleaseContext(provider, 0);
	if (file != INVALID_HANDLE_VALUE)
		CloseHandle(file);
	return ok;
}

static int verify_payload(const wchar_t *driver_root)
{
	wchar_t path[MAX_PATH];
	char digest[65];
	DWORD index;

	wcsncpy(path, driver_root, ARRAY_SIZE(path));
	path[ARRAY_SIZE(path) - 1] = L'\0';
	if (!append_path(path, ARRAY_SIZE(path), L"\\driver-manifest.json") ||
		!get_sha256(path, digest) || strcmp(digest, WCH_MANIFEST_SHA256) != 0)
		return 0;
	for (index = 0; index < WCH_MANIFEST_FILE_COUNT; index++) {
		WIN32_FILE_ATTRIBUTE_DATA attributes;

		wcsncpy(path, driver_root, ARRAY_SIZE(path));
		path[ARRAY_SIZE(path) - 1] = L'\0';
		if (!append_path(path, ARRAY_SIZE(path), L"\\x64\\") ||
			!append_path(path, ARRAY_SIZE(path), wch_manifest_files[index].path) ||
			!GetFileAttributesExW(path, GetFileExInfoStandard, &attributes) ||
			attributes.nFileSizeHigh != 0 ||
			attributes.nFileSizeLow != wch_manifest_files[index].size ||
			!get_sha256(path, digest) ||
			strcmp(digest, wch_manifest_files[index].sha256) != 0)
			return 0;
	}
	return 1;
}

int wmain(int argc, wchar_t **argv)
{
	wchar_t root[MAX_PATH];
	wchar_t setup[MAX_PATH];
	wchar_t command_line[] = L"\"SETUP.EXE\" /S";
	wchar_t *separator;
	STARTUPINFOW startup_info;
	PROCESS_INFORMATION process_info;
	DWORD exit_code;
	DWORD wait_result;

	if (argc != 2 || wcscmp(argv[1], WCH_ENTRY_ID) != 0)
		return 10;
	if (!GetModuleFileNameW(NULL, root, ARRAY_SIZE(root)))
		return 11;
	separator = wcsrchr(root, L'\\');
	if (!separator)
		return 11;
	separator[1] = L'\0';
	if (!append_path(root, ARRAY_SIZE(root), L"driver-install\\wch-ch375"))
		return 11;
	if (!verify_payload(root))
		return 12;
	wcsncpy(setup, root, ARRAY_SIZE(setup));
	setup[ARRAY_SIZE(setup) - 1] = L'\0';
	if (!append_path(setup, ARRAY_SIZE(setup), L"\\x64\\SETUP.EXE"))
		return 11;
	if (!append_path(root, ARRAY_SIZE(root), L"\\x64"))
		return 11;
	ZeroMemory(&startup_info, sizeof(startup_info));
	startup_info.cb = sizeof(startup_info);
	ZeroMemory(&process_info, sizeof(process_info));
	if (!CreateProcessW(setup, command_line, NULL, NULL, FALSE, 0, NULL,
		root, &startup_info, &process_info))
		return 13;
	CloseHandle(process_info.hThread);
	wait_result = WaitForSingleObject(process_info.hProcess, WCH_INSTALL_TIMEOUT_MS);
	if (wait_result != WAIT_OBJECT_0) {
		CloseHandle(process_info.hProcess);
		return wait_result == WAIT_TIMEOUT ? 14 : 15;
	}
	if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
		CloseHandle(process_info.hProcess);
		return 15;
	}
	CloseHandle(process_info.hProcess);
	return exit_code == 0 ? 0 : 16;
}
