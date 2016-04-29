# serial_bootloader

(C) 2016 Paul Qureshi
Licence: GPL 3.0

N.B. This is untested alpha code.

Serial (RS232/UART) bootloader for Atmel XMEGA devices. Fits in 2k bootloader sections so is compatible with the entire XMEGA range. Note that EEPROM access and possibly some other bits might need to change for different devices.

Also included is a demonstration firmware (test_image) for bootloading, which includes an embedded FW_INFO_t struct. This struct includes some basic information about the firmware, such as the target MCU, which is checked by the host software.

Note that the XMEGA NVM controller's CRC function uses an odd variant of the more common CRC32. An implementation is included in the host software.
