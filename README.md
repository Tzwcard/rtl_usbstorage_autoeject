# rtl_usbstorage_autoeject
 CDROM Auto Eject Service For RTL8811CU/RTL8821CU.

 Some of the RTL8811CU/RTL8821CU chips will presented it as a CDROM when inserted into PC, you need to eject the CDROM in order to let the device switch into NIC mode, and it's annoying if you need to reboot or unplug then replug it in.
 
 Although the file in the CDROM drive is something that can do about the same thing as ejecting the cdrom everytime with driver in it, I still feel uncomfortable when the seller labeled it as "driver free" wifi NIC.
 
 So here's something that will just eject the CDROM drive when Windows sees it.

 Service codes reference: https://learn.microsoft.com/en-us/windows/win32/services/the-complete-service-sample

 ## Environment

 Tested build with VS2022 and works as expect.

 You might need to rebuild the ``.res`` file from ``.mc``, please check [here](https://learn.microsoft.com/en-us/windows/win32/services/sample-mc) for more information.

## Install Service
```rtl_usbstorage_autoeject install```

## Remove Service
```rtl_usbstorage_autoeject uninstall```

## Standalone mode
```rtl_usbstorage_autoeject standalone```