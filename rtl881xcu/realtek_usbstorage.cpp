#include <iostream>
#include <Windows.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>

#pragma comment(lib, "SetupAPI.lib")

#include <mutex>

#define _MAX_SZ_DEVICE_PATH 0x100
#define _MAX_SZ_DEV_IF_DETAILED_DATA _MAX_SZ_DEVICE_PATH + sizeof(PSP_DEVICE_INTERFACE_DETAIL_DATA)

static bool _check_realtek_usbstorage_device(const char* str);
static bool _check_realtek_parent_pidvid(DEVINST devinst);

static bool _try_eject_cdrom(LPCWSTR path);
static bool _try_stop_disk_unit(LPCWSTR path);

static std::mutex check_mutex;

int check_realtek_cdrom_disk(int is_disk) {
	bool ret = 0;

	const std::lock_guard<std::mutex> lock(check_mutex);

	HDEVINFO dev = SetupDiGetClassDevs(is_disk ? &GUID_DEVINTERFACE_DISK : &GUID_DEVINTERFACE_CDROM, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (dev != INVALID_HANDLE_VALUE) {
		SP_DEVINFO_DATA dev_data;
		int dev_idx = 0;

		memset(&dev_data, 0, sizeof(SP_DEVINFO_DATA));
		dev_data.cbSize = sizeof(SP_DEVINFO_DATA);
		while (SetupDiEnumDeviceInfo(dev, dev_idx++, &dev_data)) {
			bool matched = false;
			DWORD req_size = 0;

			if (!SetupDiGetDeviceInstanceId(dev, &dev_data, NULL, 0, &req_size)
				&& GetLastError() == ERROR_INSUFFICIENT_BUFFER
				&& req_size > 0
				) {
				TCHAR* dev_instance_id = new TCHAR[req_size + 1];
				memset(dev_instance_id, 0, sizeof(TCHAR) * (req_size + 1));
				if (SetupDiGetDeviceInstanceId(dev, &dev_data, dev_instance_id, req_size + 1, NULL)) {
					matched = _check_realtek_parent_pidvid(dev_data.DevInst);
					if (matched) {
						printf("Matched device instance: '%ws'\n", dev_instance_id);
					}
				}
				delete[]dev_instance_id;
			}

			if (matched) {
				SP_DEVICE_INTERFACE_DATA dev_if_data;
				memset(&dev_if_data, 0, sizeof(SP_DEVICE_INTERFACE_DATA));
				dev_if_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
				if (SetupDiEnumDeviceInterfaces(dev, &dev_data, is_disk ? &GUID_DEVINTERFACE_DISK : &GUID_DEVINTERFACE_CDROM, 0, &dev_if_data)) {
					unsigned char *bufxx = new unsigned char[_MAX_SZ_DEV_IF_DETAILED_DATA];
					memset(bufxx, 0, _MAX_SZ_DEV_IF_DETAILED_DATA);
					PSP_DEVICE_INTERFACE_DETAIL_DATA ptrxx = (PSP_DEVICE_INTERFACE_DETAIL_DATA)bufxx;
					ptrxx->cbSize = 8;
					if (SetupDiGetDeviceInterfaceDetail(dev, &dev_if_data, ptrxx, _MAX_SZ_DEV_IF_DETAILED_DATA, 0, NULL)) {
						// printf("DevPath: %ws\n", ptrxx->DevicePath);
						bool remove_result = is_disk ? _try_stop_disk_unit(ptrxx->DevicePath) : _try_eject_cdrom(ptrxx->DevicePath);
						if (remove_result) {
							ret++;
						}
					}
					delete[]bufxx;
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
		ULONG req_size = 0;
		if (CM_Get_Device_ID_Size(&req_size, parent_devinst, 0) == CR_SUCCESS && req_size > 0) {
			TCHAR* dev_parent_id = new TCHAR[req_size + 1];
			memset(dev_parent_id, 0, sizeof(TCHAR) * (req_size + 1));
			if (CM_Get_Device_ID(parent_devinst, dev_parent_id, req_size + 1, 0) == CR_SUCCESS) {
				match = wcsstr(dev_parent_id, L"VID_0BDA") && wcsstr(dev_parent_id, L"PID_1A2B");
				if (match) {
					printf("Parent matched: '%ws'\n", dev_parent_id);
				}
			}
			delete[]dev_parent_id;
		}
	}

	return match;
}

#define IOCTL_SCSI_PASS_THROUGH_DIRECT  CTL_CODE(FILE_DEVICE_CONTROLLER, 0x0405, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
// #define IOCTL_DISK_EJECT_MEDIA       CTL_CODE(FILE_DEVICE_DISK,       0x0202, METHOD_BUFFERED, FILE_READ_ACCESS)

static bool _try_eject_cdrom(LPCWSTR path) {
	// usbstor#cdrom&ven_realtek&prod_usb_disk_autorun
	// usbstor#cdrom&ven_realtek&prod_driver_storage

	DWORD dwBytes;
	bool ret = false;

	HANDLE hCDROM = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hCDROM == INVALID_HANDLE_VALUE)
	{
		printf("Failed to open CDROM '%ws'\n", path);
		return ret;
	}

	// IOCTL_STORAGE_EJECT_MEDIA?
	if (DeviceIoControl(hCDROM, IOCTL_DISK_EJECT_MEDIA, NULL, 0, NULL, 0, &dwBytes, NULL)) {
		printf("Ejected CDROM: '%ws'\n", path);
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

static bool _try_stop_disk_unit(LPCWSTR path) {
	// usbstor#disk&ven_realtek&prod_driver_storage
	DWORD dwBytes;
	bool ret = false;

	HANDLE hDISK = CreateFile(
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
		printf("Failed to open DISK '%ws'\n", path);
		return ret;
	}

	SCSI_PASS_THROUGH_DIRECT scsi = { 0 };
	scsi.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
	scsi.CdbLength = 6;
	scsi.TimeOutValue = 10;
	scsi.Cdb[0] = 0x1B; // SCSI: START STOP UNIT
	scsi.Cdb[4] = 2;    // LOEJ WITH START BIT=0

	if (DeviceIoControl(hDISK, IOCTL_SCSI_PASS_THROUGH_DIRECT, &scsi, sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytes, NULL)) {
		printf("Stop DISK: '%ws'\n", path);
		ret = true;
	}
	else {
		printf("Failed to DeviceIoControl(%p, IOCTL_SCSI_PASS_THROUGH_DIRECT, ...): %08x\n", hDISK, GetLastError());
	}

	CloseHandle(hDISK);

	return ret;
}
