# Release notes

## 25/4/2025

- removed Old fatfs driver. Now all configs can use https://github.com/elehobica/pico_fatfs (https://github.com/elehobica/pico_fatfs/issues/8)
- Board configs are moved to pico_shared.

## Fixes
- none

## 23/4/2025

- Releases now built with SDK 2.1.1
- Support added for Adafruit Metro RP2350 board. See README for more info. No RISCV support yet.
- Switched to SD card driver pico_fatfs from https://github.com/elehobica/pico_fatfs. This is required for the Adafruit Metro RP2350. The Pimoroni Pico DV does not work with this updated version and still needs the old version. (see [https://github.com/elehobica/pico_fatfs/issues/7#issuecomment-2817953143](https://github.com/elehobica/pico_fatfs/issues/7#issuecomment-2817953143) ) Therefore, the old version is still included in the repository. (pico_shared/drivers/pio_fatfs) 
    This is configured in CMakeLists.txt file by setting USE_OLD_SDDRIVER to 1.
- Besides FAT32, SD cards can now also be formatted as exFAT.
- Nes controller PIO code updated by [@ManCloud](https://github.com/ManCloud). This fixes the NES controller issues on the Waveshare RP2040 - PiZero board. [#8](https://github.com/fhoedemakers/pico_shared/issues/8)

## Fixes
- Fixed Pico 2 W: Led blinking causes screen flicker and ioctl timeouts [#2](https://github.com/fhoedemakers/pico_shared/issues/2). Solved with in SDK 2.1.1
- WII classic controller: i2c bus instance (i2c0 / i2c1) not hardcoded anymore but configurable via CMakeLists.txt. 

## 19/01/2025

- To properly use the AliExpress SNES controller you need to press Y to enable the X-button. This is now documented in the README.md.
- Added support for additional controllers. See README for details.

## 01/01/2025

- Enabe fastscrolling in the menu, by holding up/down/left/right for 500 milliseconds, repeat delay is 40 milliseconds.
- bld.sh mow uses the amount of cores available on the system to speed up the build process.
- Temporary Rollback NesPad code for the WaveShare RP2040-PiZero only. Other configurations are not affected.
- Update time functions to return milliseconds and use uint64_t to return microseconds.

## 22/12/2024

- The menu now uses the entire screen resolution of 320x240 pixels. This makes a 40x30 char screen with 8x8 font possible instead of 32x29. This also fixes the menu not displaying correctly on Risc-v builds because of a not implemented assembly rendering routine in Risc-v.
- Updated NESPAD to have CLK idle HIGH instead of idle LOW. Thanks to [ManCloud](https://github.com/ManCloud). 
- Other minor changes.
