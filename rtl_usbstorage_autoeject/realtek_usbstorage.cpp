#include <iostream>
#include <Windows.h>
#include <SetupAPI.h>
#pragma comment(lib, "SetupAPI.lib")

// VID_0BDA&PID_1A2B

#define _MAX_SZ_DEVICE_PATH 0x100
#define _MAX_SZ_DEV_IF_DETAILED_DATA _MAX_SZ_DEVICE_PATH + sizeof(PSP_DEVICE_INTERFACE_DETAIL_DATA_A)

static bool _check_realtek_usbstorage_device(const char* str);
static bool _try_eject_cdrom(const char* path);

int check_realtek_cdrom_disk(void) {
	bool ret = 0;

	HDEVINFO dev = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_CDROM, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (dev != INVALID_HANDLE_VALUE) {
		SP_DEVINFO_DATA dev_data;
		int dev_idx = 0;

		memset(&dev_data, 0, sizeof(SP_DEVINFO_DATA));
		dev_data.cbSize = sizeof(SP_DEVINFO_DATA);

		while (SetupDiEnumDeviceInfo(dev, dev_idx++, &dev_data)) {
			char dev_instance_id[_MAX_SZ_DEVICE_PATH] = { 0 };
			bool matched = false;

			if (SetupDiGetDeviceInstanceIdA(dev, &dev_data, dev_instance_id, 0x100, NULL)) {
				matched = _check_realtek_usbstorage_device(dev_instance_id);
			}

			if (matched) {
				SP_DEVICE_INTERFACE_DATA dev_if_data;
				memset(&dev_if_data, 0, sizeof(SP_DEVICE_INTERFACE_DATA));
				dev_if_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
				if (SetupDiEnumDeviceInterfaces(dev, &dev_data, &GUID_DEVINTERFACE_CDROM, 0, &dev_if_data)) {
					unsigned char bufxx[_MAX_SZ_DEV_IF_DETAILED_DATA] = { 0 };
					PSP_DEVICE_INTERFACE_DETAIL_DATA_A ptrxx = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)bufxx;
					ptrxx->cbSize = 8;
					if (SetupDiGetDeviceInterfaceDetailA(dev, &dev_if_data, ptrxx, _MAX_SZ_DEV_IF_DETAILED_DATA, 0, NULL)) {
						// printf("DevPath: %s\n", ptrxx->DevicePath);
						if (_try_eject_cdrom(ptrxx->DevicePath)) {
							ret++;
						}
					}
				}
			}

			memset(&dev_data, 0, sizeof(SP_DEVINFO_DATA));
			dev_data.cbSize = sizeof(SP_DEVINFO_DATA);
		}

		SetupDiDestroyDeviceInfoList(dev);
	}

	return ret;
}

static bool _check_realtek_usbstorage_device(const char* str) {
	return strstr(str, "VEN_REALTEK&PROD_DRIVER_STORAGE") != NULL;
}

static bool _try_eject_cdrom(const char* path) {
	DWORD dwBytes;
	bool ret = false;

	HANDLE hCDROM = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hCDROM == INVALID_HANDLE_VALUE)
	{
		printf("Failed to open CDROM '%s'\n", path);
		return ret;
	}

	if (DeviceIoControl(hCDROM, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &dwBytes, NULL)) {
		ret = true;
	}
	else {
		printf("Failed to DeviceIoControl(%p, IOCTL_STORAGE_EJECT_MEDIA, ...): %08x\n", hCDROM, GetLastError());
	}

	CloseHandle(hCDROM);

	return ret;
}
