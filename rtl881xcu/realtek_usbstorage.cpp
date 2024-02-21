#include <iostream>
#include <Windows.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>

#pragma comment(lib, "SetupAPI.lib")

#include <mutex>

#define _MAX_SZ_DEVICE_PATH 0x100
#define _MAX_SZ_DEV_IF_DETAILED_DATA _MAX_SZ_DEVICE_PATH + sizeof(PSP_DEVICE_INTERFACE_DETAIL_DATA)

#define IOCTL_SCSI_PASS_THROUGH_DIRECT  CTL_CODE(FILE_DEVICE_CONTROLLER, 0x0405, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

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

static bool _check_realtek_parent_pidvid(DEVINST devinst);
static bool _try_enable_nic(LPCWSTR path, int is_disk = 0);

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
					ptrxx->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
					if (SetupDiGetDeviceInterfaceDetail(dev, &dev_if_data, ptrxx, _MAX_SZ_DEV_IF_DETAILED_DATA, 0, NULL)) {
						// printf("DevPath: %ws\n", ptrxx->DevicePath);
						bool remove_result = _try_enable_nic(ptrxx->DevicePath, is_disk ? 1 : 0);
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

static bool _try_enable_nic(LPCWSTR path, int is_disk) {
	// usbstor#disk&ven_realtek&prod_driver_storage
	// usbstor#cdrom&ven_realtek&prod_usb_disk_autorun
	// usbstor#cdrom&ven_realtek&prod_driver_storage

	DWORD dwBytes;
	bool ret = false;

	HANDLE hDev = CreateFile(
		path,
		GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (hDev == INVALID_HANDLE_VALUE)
	{
		printf("Failed to open Device '%ws'\n", path);
		return ret;
	}

	if (is_disk) {
		SCSI_PASS_THROUGH_DIRECT scsi = { 0 };
		scsi.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
		scsi.CdbLength = 6;
		scsi.TimeOutValue = 10;
		scsi.Cdb[0] = 0x1B; // SCSI: START STOP UNIT
		scsi.Cdb[4] = 2;    // LOEJ WITH START BIT=0

		if (DeviceIoControl(hDev, IOCTL_SCSI_PASS_THROUGH_DIRECT, &scsi, sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytes, NULL)) {
			printf("Stop DISK: '%ws'\n", path);
			ret = true;
		}
		else {
			printf("Failed to DeviceIoControl(%p, %s, ...): %08x\n", hDev, "IOCTL_SCSI_PASS_THROUGH_DIRECT", GetLastError());
		}
	}
	else {
		if (DeviceIoControl(hDev, IOCTL_DISK_EJECT_MEDIA, NULL, 0, NULL, 0, &dwBytes, NULL)) {
			printf("Ejected CDROM: '%ws'\n", path);
			ret = true;
		}
		else {
			printf("Failed to DeviceIoControl(%p, %s, ...): %08x\n", hDev, "IOCTL_STORAGE_EJECT_MEDIA", GetLastError());
		}
	}

	CloseHandle(hDev);

	return ret;
}
