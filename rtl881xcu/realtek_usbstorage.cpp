#include <iostream>
#include <Windows.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>

#pragma comment(lib, "SetupAPI.lib")

#include <mutex>

#define _MAX_SZ_DEVICE_PATH 0x100
#define _MAX_SZ_DEV_IF_DETAILED_DATA _MAX_SZ_DEVICE_PATH + sizeof(PSP_DEVICE_INTERFACE_DETAIL_DATA_A)

static bool _check_realtek_usbstorage_device(const char* str);
static bool _check_realtek_parent_pidvid(DEVINST devinst);

static bool _try_eject_cdrom(const char* path);
static bool _try_stop_disk_unit(const char* path);

static std::mutex check_mutex;

int check_realtek_cdrom_disk(int is_disk) {
	bool ret = 0;

	const std::lock_guard<std::mutex> lock(check_mutex);

	HDEVINFO dev = SetupDiGetClassDevsA(is_disk ? &GUID_DEVINTERFACE_DISK : &GUID_DEVINTERFACE_CDROM, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (dev != INVALID_HANDLE_VALUE) {
		SP_DEVINFO_DATA dev_data;
		int dev_idx = 0;

		memset(&dev_data, 0, sizeof(SP_DEVINFO_DATA));
		dev_data.cbSize = sizeof(SP_DEVINFO_DATA);

		while (SetupDiEnumDeviceInfo(dev, dev_idx++, &dev_data)) {
			char dev_instance_id[_MAX_SZ_DEVICE_PATH] = { 0 };
			bool matched = false;

			if (SetupDiGetDeviceInstanceIdA(dev, &dev_data, dev_instance_id, 0x100, NULL)) {
				// matched = _check_realtek_usbstorage_device(dev_instance_id);
				matched = _check_realtek_parent_pidvid(dev_data.DevInst);
				if (matched) {
					printf("Matched device instance: '%s'\n", dev_instance_id);
				}
			}

			if (matched) {
				SP_DEVICE_INTERFACE_DATA dev_if_data;
				memset(&dev_if_data, 0, sizeof(SP_DEVICE_INTERFACE_DATA));
				dev_if_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
				if (SetupDiEnumDeviceInterfaces(dev, &dev_data, is_disk ? &GUID_DEVINTERFACE_DISK : &GUID_DEVINTERFACE_CDROM, 0, &dev_if_data)) {
					unsigned char bufxx[_MAX_SZ_DEV_IF_DETAILED_DATA] = { 0 };
					PSP_DEVICE_INTERFACE_DETAIL_DATA_A ptrxx = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)bufxx;
					ptrxx->cbSize = 8;
					if (SetupDiGetDeviceInterfaceDetailA(dev, &dev_if_data, ptrxx, _MAX_SZ_DEV_IF_DETAILED_DATA, 0, NULL)) {
						// printf("DevPath: %s\n", ptrxx->DevicePath);
						bool remove_result = is_disk ? _try_stop_disk_unit(ptrxx->DevicePath) : _try_eject_cdrom(ptrxx->DevicePath);
						if (remove_result) {
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

static bool _check_realtek_parent_pidvid(DEVINST devinst) {
	DEVINST parent_devinst = NULL;
	bool match = false;
	if (CM_Get_Parent(&parent_devinst, devinst, 0) == CR_SUCCESS) {
		char dev_parent_id[MAX_DEVICE_ID_LEN] = { 0 };
		if (CM_Get_Device_IDA(parent_devinst, dev_parent_id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
			match = strstr(dev_parent_id, "VID_0BDA") && strstr(dev_parent_id, "PID_1A2B");
			if (match) {
				printf("Parent matched: '%s'\n", dev_parent_id);
			}
		}
	}

	return match;
}

#define IOCTL_SCSI_PASS_THROUGH_DIRECT  CTL_CODE(FILE_DEVICE_CONTROLLER, 0x0405, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
// #define IOCTL_DISK_EJECT_MEDIA       CTL_CODE(FILE_DEVICE_DISK,       0x0202, METHOD_BUFFERED, FILE_READ_ACCESS)

static bool _try_eject_cdrom(const char* path) {
	// usbstor#cdrom&ven_realtek&prod_usb_disk_autorun
	// usbstor#cdrom&ven_realtek&prod_driver_storage

	DWORD dwBytes;
	bool ret = false;

	HANDLE hCDROM = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hCDROM == INVALID_HANDLE_VALUE)
	{
		printf("Failed to open CDROM '%s'\n", path);
		return ret;
	}

	// IOCTL_STORAGE_EJECT_MEDIA?
	if (DeviceIoControl(hCDROM, IOCTL_DISK_EJECT_MEDIA, NULL, 0, NULL, 0, &dwBytes, NULL)) {
		printf("Ejected CDROM: '%s'\n", path);
		ret = true;
	}
	else {
		printf("Failed to DeviceIoControl(%p, IOCTL_STORAGE_EJECT_MEDIA, ...): %08x\n", hCDROM, GetLastError());
	}

	CloseHandle(hCDROM);

	return ret;
}

struct SCSI_PASS_THROUGH_DIRECT
{
	USHORT Length;
	UCHAR ScsiStatus;
	UCHAR PathId;
	UCHAR TargetId;
	UCHAR Lun;
	UCHAR CdbLength;
	UCHAR SenseInfoLength;
	UCHAR DataIn;
	ULONG DataTransferLength;
	ULONG TimeOutValue;
	VOID* POINTER_32 DataBuffer;
	ULONG SenseInfoOffset;
	UCHAR Cdb[16];
};

static bool _try_stop_disk_unit(const char* path) {
	// usbstor#disk&ven_realtek&prod_driver_storage
	DWORD dwBytes;
	bool ret = false;

	HANDLE hDISK = CreateFileA(
		path,
		GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (hDISK == INVALID_HANDLE_VALUE)
	{
		printf("Failed to open DISK '%s'\n", path);
		return ret;
	}

	SCSI_PASS_THROUGH_DIRECT scsi = { 0 };
	scsi.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
	scsi.CdbLength = 6;
	scsi.TimeOutValue = 10;
	scsi.Cdb[0] = 0x1B; // SCSI: START STOP UNIT
	scsi.Cdb[4] = 2;    // LOEJ WITH START BIT=0

	if (DeviceIoControl(hDISK, IOCTL_SCSI_PASS_THROUGH_DIRECT, &scsi, sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytes, NULL)) {
		printf("Stop DISK: '%s'\n", path);
		ret = true;
	}
	else {
		printf("Failed to DeviceIoControl(%p, IOCTL_SCSI_PASS_THROUGH_DIRECT, ...): %08x\n", hDISK, GetLastError());
	}

	CloseHandle(hDISK);

	return ret;
}
