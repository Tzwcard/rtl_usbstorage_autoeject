#include <iostream>
#include <Windows.h>
#include <dbt.h>
#include "realtek_usbstorage.h"

#define CLS_NAME L"CLS_REALTEK_CDROM_LISTENER"
#define WND_NAME L"REALTEK_CDROM_LISTENER"

static LRESULT _msg_handler(HWND hwnd, UINT uint, WPARAM wparam, LPARAM lparam);
static void _on_dev_change(WPARAM wparam, LPARAM lparam);

static HWND h_mainwindow = NULL;
static HDEVNOTIFY h_notification = NULL;

static void _on_dev_change(WPARAM wparam, LPARAM lparam) {
    PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lparam;
    PDEV_BROADCAST_DEVICEINTERFACE lpdbv = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;
    if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
    {
        switch (wparam)
        {
        case DBT_DEVICEARRIVAL:
            // printf("Connected: '%ws'\n", lpdbv->dbcc_name);
            check_realtek_cdrom_disk();
            break;

        case DBT_DEVICEREMOVECOMPLETE:
            // printf("Disonnected: '%ws'\n", lpdbv->dbcc_name);
            break;
        }
    }
}

static LRESULT _msg_handler(HWND hwnd, UINT uint, WPARAM wparam, LPARAM lparam)
{
    switch (uint)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_DEVICECHANGE:
        _on_dev_change(wparam, lparam);
        break;

    default:
        break;
    }

    return DefWindowProc(hwnd, uint, wparam, lparam);
}

int notify_window_start(void)
{
    WNDCLASSEX wx;
    memset(&wx, 0, sizeof(WNDCLASSEX));

    wx.cbSize        = sizeof(WNDCLASSEX);
    wx.lpfnWndProc   = _msg_handler;
    wx.hInstance     = GetModuleHandle(NULL);
    wx.style         = CS_HREDRAW | CS_VREDRAW;
    wx.lpszClassName = CLS_NAME;

    if (RegisterClassEx(&wx))
    {
        h_mainwindow = CreateWindow(
            CLS_NAME,                 // Class name
            WND_NAME,                 // Window name
            WS_MINIMIZE,              // Create with minimized
            CW_USEDEFAULT, 0,         // x, y
            CW_USEDEFAULT, 0,         // w, h
            HWND_MESSAGE,             // Message-only window should use HWND_MESSAGE
            NULL,                     // hMenu
            GetModuleHandle(NULL),    // hInstance
            NULL                      // lpParam for WM_CREATE
        );
    }
    else {
        printf("Cannot RegisterClassEx()\n");
    }

    if (!h_mainwindow)
    {
        printf("Cannot create msgwnd\n");
        return 0;
    }
    else {
        // Registser a device notification for CDROM

        DEV_BROADCAST_DEVICEINTERFACE notofy_filter = { 0 };
        notofy_filter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
        notofy_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        memcpy(&notofy_filter.dbcc_classguid, &GUID_DEVINTERFACE_CDROM, sizeof(GUID));

        h_notification = RegisterDeviceNotification(h_mainwindow, &notofy_filter, DEVICE_NOTIFY_WINDOW_HANDLE);

        if (!h_notification)
        {
            printf("Failed to register device notification\n");
            DestroyWindow(h_mainwindow);
            return 0;
        }
    }

    // Run once before message dispatch
    check_realtek_cdrom_disk();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (h_notification) {
        UnregisterDeviceNotification(h_notification);
        h_notification = NULL;
    }

    UnregisterClass(CLS_NAME, NULL);
    return 0;
}

int notify_window_close(void) {
    PostMessage(h_mainwindow, WM_CLOSE, 0, 0);

    return 1;
}