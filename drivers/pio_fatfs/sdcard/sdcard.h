#ifndef _SDCARD_H_
#define _SDCARD_H_

/* SPI pin assignment */

/* Pico Wireless */
#ifndef SDCARD_SPI_BUS
#define SDCARD_SPI_BUS spi0
#endif

#ifndef SDCARD_PIN_CS
#define SDCARD_PIN_CS     22
#endif

#ifndef SDCARD_PIN_SCK
#define SDCARD_PIN_SCK    18
#endif

#ifndef SDCARD_PIN_MOSI
#define SDCARD_PIN_MOSI   19
#endif

#ifndef SDCARD_PIN_MISO 
#define SDCARD_PIN_MISO   16
#endif

#endif // _SDCARD_H_
