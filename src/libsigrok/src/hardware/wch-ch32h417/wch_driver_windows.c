#include <config.h>
#include "wch_driver_windows.h"

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "wch-ch32h417"

#if defined(_WIN32)

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <shellapi.h>
#include <wchar.h>

#define WCH_HARDWARE_ID L"USB\\VID_1A86&PID_5537"
#define WCH_SERVICE L"CH375_A64"
#define WCH_INSTALL_TIMEOUT_MS 120000

static int install_attempted;

static int hardware_id_matches(const wchar_t *hardware_id)
{
	size_t prefix_length = wcslen(WCH_HARDWARE_ID);

	return _wcsnicmp(hardware_id, WCH_HARDWARE_ID, prefix_length) == 0 &&
		(hardware_id[prefix_length] == L'\0' ||
		 hardware_id[prefix_length] == L'&' ||
		 hardware_id[prefix_length] == L'\\');
}

static int get_property(HDEVINFO info, SP_DEVINFO_DATA *device,
	DWORD property, wchar_t *buffer, DWORD buffer_size)
{
	DWORD data_type;
	DWORD required;

	return SetupDiGetDeviceRegistryPropertyW(info, device, property,
		&data_type, (BYTE *)buffer, buffer_size * sizeof(*buffer), &required);
}

enum wch_driver_state wch_driver_probe(void)
{
	HDEVINFO info;
	SP_DEVINFO_DATA device;
	DWORD index;
	int found = 0;
	enum wch_driver_state state = WCH_DRIVER_NO_DEVICE;

	info = SetupDiGetClassDevsW(NULL, L"USB", NULL,
		DIGCF_PRESENT | DIGCF_ALLCLASSES);
	if (info == INVALID_HANDLE_VALUE) {
		sr_warn("WCH PnP enumeration failed (%lu).", GetLastError());
		return WCH_DRIVER_ERROR;
	}

	for (index = 0; ; index++) {
		wchar_t hardware_ids[4096];
		wchar_t service[256];
		DEVINST devnode;
		ULONG status = 0;
		ULONG problem = 0;
		CONFIGRET cr;
		wchar_t *id;

		ZeroMemory(&device, sizeof(device));
		device.cbSize = sizeof(device);
		if (!SetupDiEnumDeviceInfo(info, index, &device)) {
			if (GetLastError() == ERROR_NO_MORE_ITEMS)
				break;
			sr_warn("WCH PnP device enumeration failed (%lu).", GetLastError());
			state = WCH_DRIVER_ERROR;
			break;
		}
		ZeroMemory(hardware_ids, sizeof(hardware_ids));
		if (!get_property(info, &device, SPDRP_HARDWAREID, hardware_ids,
			ARRAY_SIZE(hardware_ids)))
			continue;
		for (id = hardware_ids; *id; id += wcslen(id) + 1) {
			if (hardware_id_matches(id))
				break;
		}
		if (!*id)
			continue;

		found = 1;
		service[0] = L'\0';
		(void)get_property(info, &device, SPDRP_SERVICE, service,
			ARRAY_SIZE(service));
		if (!service[0]) {
			state = WCH_DRIVER_MISSING;
			sr_info("WCH CH375 device is present without a function driver.");
			break;
		}
		devnode = device.DevInst;
		cr = CM_Get_DevNode_Status(&status, &problem, devnode, 0);
		if (cr != CR_SUCCESS) {
			sr_warn("WCH PnP status query failed (0x%lx).", (unsigned long)cr);
			state = WCH_DRIVER_ERROR;
			break;
		}
		if (problem != 0) {
			sr_warn("WCH device has ConfigMgr problem code %lu.", problem);
			state = WCH_DRIVER_ERROR;
			break;
		}
		if (_wcsicmp(service, WCH_SERVICE) == 0) {
			state = WCH_DRIVER_READY;
			sr_dbg("WCH CH375 device is bound to %ls.", WCH_SERVICE);
		} else {
			sr_warn("WCH device is bound to another service; refusing replacement.");
			state = WCH_DRIVER_OTHER;
		}
		break;
	}

	SetupDiDestroyDeviceInfoList(info);
	return found ? state : WCH_DRIVER_NO_DEVICE;
}

int wch_driver_install(void)
{
	SHELLEXECUTEINFOW execute_info;
	wchar_t executable[MAX_PATH];
	wchar_t *separator;
	DWORD length;
	DWORD exit_code;
	BOOL result;

	if (install_attempted) {
		sr_warn("WCH driver installation was already attempted in this process.");
		return SR_ERR;
	}
	install_attempted = 1;
	length = GetModuleFileNameW(NULL, executable, ARRAY_SIZE(executable));
	if (length == 0 || length >= ARRAY_SIZE(executable)) {
		sr_err("Unable to resolve Sigrok executable path for WCH driver install.");
		return SR_ERR;
	}
	separator = wcsrchr(executable, L'\\');
	if (!separator) {
		sr_err("Sigrok executable path has no directory.");
		return SR_ERR;
	}
	separator[1] = L'\0';
	if (wcslen(executable) + wcslen(L"wch-driver-installer.exe") >=
		ARRAY_SIZE(executable)) {
		sr_err("WCH driver installer path is too long.");
		return SR_ERR;
	}
	wcscat(executable, L"wch-driver-installer.exe");
	ZeroMemory(&execute_info, sizeof(execute_info));
	execute_info.cbSize = sizeof(execute_info);
	execute_info.fMask = SEE_MASK_NOCLOSEPROCESS;
	execute_info.lpVerb = L"runas";
	execute_info.lpFile = executable;
	execute_info.lpParameters = L"wch-ch375-x64";
	separator = wcsrchr(executable, L'\\');
	*separator = L'\0';
	execute_info.lpDirectory = executable;
	result = ShellExecuteExW(&execute_info);
	*separator = L'\\';
	if (!result) {
		DWORD error = GetLastError();
		if (error == ERROR_CANCELLED)
			sr_warn("WCH driver installation was cancelled by the user.");
		else
			sr_err("Failed to start WCH driver installer (%lu).", error);
		return SR_ERR;
	}
	if (!execute_info.hProcess) {
		sr_err("WCH driver installer returned no process handle.");
		return SR_ERR;
	}
	if (WaitForSingleObject(execute_info.hProcess, WCH_INSTALL_TIMEOUT_MS) != WAIT_OBJECT_0) {
		sr_err("WCH driver installer timed out.");
		CloseHandle(execute_info.hProcess);
		return SR_ERR;
	}
	if (!GetExitCodeProcess(execute_info.hProcess, &exit_code)) {
		sr_err("Unable to read WCH driver installer exit code (%lu).", GetLastError());
		CloseHandle(execute_info.hProcess);
		return SR_ERR;
	}
	CloseHandle(execute_info.hProcess);
	if (exit_code != 0) {
		sr_err("WCH driver installer helper exited with code %lu.", exit_code);
		return SR_ERR;
	}
	return SR_OK;
}

#else

enum wch_driver_state wch_driver_probe(void)
{
	return WCH_DRIVER_READY;
}

int wch_driver_install(void)
{
	return SR_ERR_NA;
}

#endif
