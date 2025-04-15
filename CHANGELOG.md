# Release notes

## 4/2025

 Improved SD card Support:
    - Updated to the latest version of the pico_fatfs library from https://github.com/elehobica/pico_fatfs.
    - Besides FAT32, SD cards can now also be formatted as exFAT.
- Nes controller PIO code updated by [@ManCloud](https://github.com/ManCloud). This fixes the NES controller issues on the Waveshare RP2040 - PiZero board. [#8](https://github.com/fhoedemakers/pico_shared/issues/8)
- Support for Adafruit Metro RP2350 board. You need the following materials:
    - Adafruit Metro RP2350 board. https://www.adafruit.com/product/6267
    - 22-pin 0.5mm pitch FPC flex cable for DSI CSI or HSTX. https://www.adafruit.com/product/6036
    - Adafruit RP2350 22-pin FPC HSTX to DVI Adapter for HDMI Displays. https://www.adafruit.com/product/6055 
    - Usb-c Y-cable. https://a.co/d/9vCzu0h For power and USB-controller. NES controller support is not yet available.
    
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
