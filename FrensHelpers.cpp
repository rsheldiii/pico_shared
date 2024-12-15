
#include <stdio.h>
#include <cstring>
#include "pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "util/exclusive_proc.h"
#include "tusb.h"
#include "dvi/dvi.h"
#include "ff.h"
#include "nespad.h"
#include "wiipad.h"
#include "settings.h"
#include "FrensHelpers.h"

// Pico W devices use a GPIO on the WIFI chip for the LED,
// so when building for Pico W, CYW43_WL_GPIO_LED_PIN will be defined
// NOTE: Building for Pico2 W makes the emulator not work: ioctl timeouts and red flicker
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

std::unique_ptr<dvi::DVI> dvi_;
util::ExclusiveProc exclProc_;
char ErrorMessage[ERRORMESSAGESIZE];
bool scaleMode8_7_ = true;
uintptr_t ROM_FILE_ADDR = 0;
int maxRomSize = 0;
namespace Frens
{
    static FATFS fs;
    // various helper functions
    //
    // test if string ends with suffix
    //
    bool endsWith(std::string const &str, std::string const &suffix)
    {
        if (str.length() < suffix.length())
        {
            return false;
        }
        return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
    }
    //
    // returns lowercase of string s
    //
    std::string str_tolower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c)
                       { return std::tolower(c); } // correct
        );
        return s;
    }

    // Check whether a string ends with a given suffix
    bool cstr_endswith(const char *string, const char *width)
    {
        int lstring = strlen(string);
        int wlen = strlen(width);
        if (wlen >= lstring)
        {
            return false;
        }
        int pos = lstring - wlen;
        return (strcmp(string + pos, width) == 0);
    }

    uint32_t time_us()
    {
        absolute_time_t t = get_absolute_time();
        return to_us_since_boot(t);
    }

#define INITIAL_CAPACITY 10
    // Split a string into tokens using the specified delimiters
    // The result is an array of dynamically allocated strings
    char **cstr_split(const char *str, const char *delimiters, int *count)
    {
        if (str == NULL || delimiters == NULL)
        {
            *count = 0;
            return NULL;
        }

        // Create a modifiable copy of the input string
        char *str_copy = strdup(str);
        if (str_copy == NULL)
        {
            *count = 0;
            return NULL;
        }

        // Initial memory allocation for the result array
        int capacity = INITIAL_CAPACITY;
        char **result = (char **)malloc(capacity * sizeof(char *));
        if (result == NULL)
        {
            free(str_copy);
            *count = 0;
            return NULL;
        }

        *count = 0;
        char *token = strtok(str_copy, delimiters);
        while (token != NULL)
        {
            // Skip empty tokens
            if (*token != '\0')
            {
                // Reallocate if necessary
                if (*count >= capacity)
                {
                    capacity *= 2;
                    char **temp = (char **)realloc(result, capacity * sizeof(char *));
                    if (temp == NULL)
                    {
                        // Memory allocation failed, clean up
                        for (int i = 0; i < *count; ++i)
                        {
                            free(result[i]);
                        }
                        free(result);
                        free(str_copy);
                        *count = 0;
                        return NULL;
                    }
                    result = temp;
                }

                // Allocate memory for the token and copy it
                result[*count] = strdup(token);
                if (result[*count] == NULL)
                {
                    // Memory allocation failed, clean up
                    for (int i = 0; i < *count; ++i)
                    {
                        free(result[i]);
                    }
                    free(result);
                    free(str_copy);
                    *count = 0;
                    return NULL;
                }
                (*count)++;
            }
            token = strtok(NULL, delimiters);
        }

        free(str_copy);
        return result;
    }

    // Get the file name from a full path
    char *GetfileNameFromFullPath(char *fullPath)
    {
        char *fileName = fullPath;
        char *ptr = fullPath;
        while (*ptr)
        {
            if (*ptr == '/')
            {
                fileName = ptr + 1;
            }
            ptr++;
        }
        return fileName;
    }

    // Strip the extension from a file name
    void stripextensionfromfilename(char *filename)
    {
        char *ptr = filename;
        char *lastdot = filename;
        while (*ptr)
        {
            if (*ptr == '.')
            {
                lastdot = ptr;
            }
            ptr++;
        }
        *lastdot = 0;
    }
    // Initialize the SD card
    bool initSDCard()
    {
        FRESULT fr;
        TCHAR str[40];
        sleep_ms(1000);

        printf("Mounting SDcard");
        fr = f_mount(&fs, "", 1);
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "SD card mount error: %d", fr);
            printf("%s\n", ErrorMessage);
            return false;
        }
        printf("\n");

        fr = f_chdir("/");
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot change dir to / : %d", fr);
            printf("%s\n", ErrorMessage);
            return false;
        }
        // for f_getcwd to work, set
        //   #define FF_FS_RPATH		2
        // in drivers/fatfs/ffconf.h
        fr = f_getcwd(str, sizeof(str));
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot get current dir: %d", fr);
            printf("%s\n", ErrorMessage);
            return false;
        }
        printf("Current directory: %s\n", str);
        printf("Creating directory %s\n", GAMESAVEDIR);
        fr = f_mkdir(GAMESAVEDIR);
        if (fr != FR_OK)
        {
            if (fr == FR_EXIST)
            {
                printf("Directory already exists.\n");
            }
            else
            {
                snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot create dir %s: %d", GAMESAVEDIR, fr);
                printf("%s\n", ErrorMessage);
                return false;
            }
        }
        return true;
    }

    bool applyScreenMode(ScreenMode screenMode_)
    {
        bool scanLine = false;
        bool scaleMode8_7_ = false;
        switch (screenMode_)
        {
        case ScreenMode::SCANLINE_1_1:
            scaleMode8_7_ = false;
            scanLine = true;
            printf("ScreenMode::SCANLINE_1_1\n");
            break;

        case ScreenMode::SCANLINE_8_7:
            scaleMode8_7_ = true;
            scanLine = true;
            printf("ScreenMode::SCANLINE_8_7\n");
            break;

        case ScreenMode::NOSCANLINE_1_1:
            scaleMode8_7_ = false;
            scanLine = false;
            printf("ScreenMode::NOSCANLINE_1_1\n");
            break;

        case ScreenMode::NOSCANLINE_8_7:
            scaleMode8_7_ = true;
            scanLine = false;
            printf("ScreenMode::NOSCANLINE_8_7\n");
            break;
        }
        dvi_->setScanLine(scanLine);
        return scaleMode8_7_;
    }

    bool screenMode(int incr)
    {
        bool scaleMode8_7_;
        settings.screenMode = static_cast<ScreenMode>((static_cast<int>(settings.screenMode) + incr) & 3);
        scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
        savesettings();
        return scaleMode8_7_;
    }

    void flashrom(char *selectedRom)
    {
        // Determine loaded rom
        printf("Rebooted by menu\n");
        FIL fil;
        FRESULT fr;
        size_t tmpSize;
        printf("Reading current game from %s and starting emulator\n", ROMINFOFILE);
        fr = f_open(&fil, ROMINFOFILE, FA_READ);
        if (fr == FR_OK)
        {
            size_t r;
            fr = f_read(&fil, selectedRom, FF_MAX_LFN, &r);

            if (fr != FR_OK)
            {
                snprintf(ErrorMessage, 40, "Cannot read %s:%d\n", ROMINFOFILE, fr);
                selectedRom[0] = 0;
                printf(ErrorMessage);
            }
            else
            {
                selectedRom[r] = 0;
            }
        }
        else
        {
            snprintf(ErrorMessage, 40, "Cannot open %s:%d\n", ROMINFOFILE, fr);
            printf(ErrorMessage);
        }
        f_close(&fil);
        if (selectedRom[0] != 0)
        {
            printf("Starting (%d) %s\n", strlen(selectedRom), selectedRom);
            printf("Checking for /START file. (Is start pressed in Menu?)\n");
            fr = f_unlink("/START");
            if (fr == FR_NO_FILE)
            {
                printf("Start not pressed, flashing rom.\n");
                size_t bufsize = 0x1000;                // Write 4k at a time, larger sizes will increases the risk of making XInput unresponsive. (Still happens sometimes)
                BYTE *buffer = (BYTE *)malloc(bufsize); // (BYTE *)InfoNes_GetPPURAM(&bufsize);
                auto ofs = ROM_FILE_ADDR - XIP_BASE;
                printf("Writing rom %s to flash %x\n", selectedRom, ofs);
                UINT totalBytes = 0;
                fr = f_open(&fil, selectedRom, FA_READ);
                bool onOff = true;
                UINT bytesRead;
                if (fr == FR_OK)
                {
                    UINT filesize = f_size(&fil);
                    printf("Filesize: %d bytes (%dKB)\n", filesize, filesize / 1024);
                    if (filesize < maxRomSize)
                    {
                        bool readError = false;
                        for (;;)
                        {
                            fr = f_read(&fil, buffer, bufsize, &bytesRead);
                            if (fr == FR_OK)
                            {
                                if (bytesRead == 0)
                                {
                                    break;
                                }
                                blinkLed(onOff);
                                onOff = !onOff;
                                // Disable interupts, erase, flash and enable interrupts
                                uint32_t ints = save_and_disable_interrupts();
                                flash_range_erase(ofs, bufsize);
                                flash_range_program(ofs, buffer, bufsize);
                                restore_interrupts(ints);
                                ofs += bufsize;
                                totalBytes += bytesRead;
                                // keep the usb stack running
                                tuh_task();
                            }
                            else
                            {
                                readError = true;
                                snprintf(ErrorMessage, 40, "Error reading rom: %d", fr);
                                printf("Error reading rom: %d: %d/%d bytes read\n", fr, totalBytes, filesize);
                                selectedRom[0] = 0;
                                break;
                            }
                        }
                        if (!readError)
                        {
                            printf("Wrote %d bytes to flash\n", totalBytes);
                            if (totalBytes != filesize)
                            {
                                snprintf(ErrorMessage, 40, "Size mismatch: %d != %d\n", totalBytes, filesize);
                                printf("%s\n", ErrorMessage);
                                selectedRom[0] = 0;
                            }
                        }
                    }
                    else
                    {
                        snprintf(ErrorMessage, 40, "ROM too large: %d > %d\n", filesize, maxRomSize);
                        printf("%s\n", ErrorMessage);
                        selectedRom[0] = 0;
                    }
                    f_close(&fil);
                }
                else
                {
                    snprintf(ErrorMessage, 40, "Cannot open rom %d", fr);
                    printf("%s\n", ErrorMessage);
                    selectedRom[0] = 0;
                }
                free(buffer);
                printf("Flashing done\n");
            }
            else
            {
                if (fr != FR_OK)
                {
                    snprintf(ErrorMessage, 40, "Cannot delete /START file %d", fr);
                    printf("%s\n", ErrorMessage);
                    selectedRom[0] = 0;
                }
                else
                {
                    printf("Start pressed in menu, not flashing rom.\n");
                }
            }
        }
    }
    void __not_in_flash_func(core1_main)()
    {
        while (true)
        {
            dvi_->registerIRQThisCore();
            dvi_->waitForValidLine();

            dvi_->start();
            while (!exclProc_.isExist())
            {
                if (scaleMode8_7_)
                {
                    // Default
                    dvi_->convertScanBuffer12bppScaled16_7(34, 32, 288 * 2);

                    // 34 + 252 + 34
                    // 32 + 576 + 32
                }
                else
                {
                    //
                    dvi_->convertScanBuffer12bpp();
                }
            }

            dvi_->unregisterIRQThisCore();
            dvi_->stop();

            exclProc_.processOrWaitIfExist();
        }
    }
    void blinkLed(bool on)
    {
#if LED_GPIO_PIN > -1
#if LED_GPIO_PIN > 0
        gpio_put(LED_GPIO_PIN, on);
#elif defined(PICO_DEFAULT_LED_PIN)
        gpio_put(PICO_DEFAULT_LED_PIN, on);
#elif defined(CYW43_WL_GPIO_LED_PIN)
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
#else
        // No LED pin defined
        (void)on; // Suppress unused parameter warning
#endif
#else
        (void)on; // Suppress unused parameter warning
#endif
    }

    // Initialize the LED
    // Note that activationg the LED on the PICO W makes the board unstable and
    // completely unresponsive. This is why building for PICO W is not recommended. Use Pico build instead.
    // LED_GPIO_PIN -1 : No Onboard LED
    // LED_GPIO_PIN 0  : Onboard LED
    // LED_GPIO_PIN > 0: Onboard LED on GPIO pin LED_GPIO_PIN. (Feather DVI as a different onboard led pin)
    int initLed()
    {
#if LED_GPIO_PIN > -1
#if LED_GPIO_PIN > 0
        gpio_init(LED_GPIO_PIN);
        gpio_set_dir(LED_GPIO_PIN, GPIO_OUT);
        gpio_put(LED_GPIO_PIN, 1);
#elif defined(PICO_DEFAULT_LED_PIN)
        gpio_init(PICO_DEFAULT_LED_PIN);
        gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
#elif defined(CYW43_WL_GPIO_LED_PIN)
        // For Pico W devices we need to initialize the driver
        return cyw43_arch_init();
#endif
#endif
        return PICO_OK;
    }

    void initVintageControllers(uint32_t CPUFreqKHz)
    {
#if NES_PIN_CLK != -1
        nespad_begin(0, CPUFreqKHz, NES_PIN_CLK, NES_PIN_DATA, NES_PIN_LAT, NES_PIO);
#endif
#if NES_PIN_CLK_1 != -1
        nespad_begin(1, CPUFreqKHz, NES_PIN_CLK_1, NES_PIN_DATA_1, NES_PIN_LAT_1, NES_PIO_1);
#endif
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
        wiipad_begin();
#endif
    }
 
    void initDVandAudio(int marginTop, int marginBottom, size_t audioBufferSize)
    {
        //
        dvi_ = std::make_unique<dvi::DVI>(pio0, &DVICONFIG,
                                          dvi::getTiming640x480p60Hz());
        //    dvi_->setAudioFreq(48000, 25200, 6144);
        dvi_->setAudioFreq(44100, 28000, 6272);

        dvi_->allocateAudioBuffer(audioBufferSize);
        //    dvi_->setExclusiveProc(&exclProc_);

        dvi_->getBlankSettings().top = marginTop * 2;
        dvi_->getBlankSettings().bottom = marginBottom * 2;
        // dvi_->setScanLine(true);
        // 空サンプル詰めとく
        dvi_->getAudioRingBuffer().advanceWritePointer(255);
    }
    
    /// @brief Init dv and audio with default audio buffer size of 256
    /// @param marginTop 
    /// @param marginBottom 
    void initDVandAudio(int marginTop, int marginBottom) {
        initDVandAudio(marginTop, marginBottom, 256);
    }
    bool initAll(char *selectedRom, uint32_t CPUFreqKHz, int marginTop, int marginBottom, size_t audiobufferSize)
    {
        bool ok = false;
        int rc = initLed();
        if (rc != PICO_OK)
        {
            printf("Error initializing LED: %d\n", rc);
        }
        // Calculate the address in flash where roms will be stored
        printf("Flash binary start: 0x%08x\n", &__flash_binary_start);
        printf("Flash binary end  : 0x%08x\n", &__flash_binary_end);
        printf("Flash byte size   :   %08d\n", PICO_FLASH_SIZE_BYTES);
        uint8_t *flash_end = (uint8_t *)&__flash_binary_start + PICO_FLASH_SIZE_BYTES - 1;
        printf("Flash end         : 0x%08x\n", flash_end);
        // round ROM_FILE_ADDRESS address up to 4k boundary of flash_binary_end
        ROM_FILE_ADDR = ((uintptr_t)&__flash_binary_end + 0xFFF) & ~0xFFF;
        // calculate max rom size
        maxRomSize = flash_end - (uint8_t *)ROM_FILE_ADDR;
        printf("ROM_FILE_ADDR     : 0x%08x\n", ROM_FILE_ADDR);
        printf("Max ROM size      :   %08d bytes\n", maxRomSize);

        // reset settings to default in case SD card could not be mounted
        resetsettings();
        if (initSDCard())
        {
            ok = true;
            loadsettings();
            // When a game is started from the menu, the menu will reboot the device.
            // After reboot the emulator will start the selected game.
            // The watchdog timer is used to detect if the reboot was caused by the menu.
            // Use watchdog_enable_caused_reboot in stead of watchdog_caused_reboot because
            // when reset is pressed while in game, the watchdog will also be triggered.
            if (true || watchdog_enable_caused_reboot())
            {
                flashrom(selectedRom);
            }
        }
        initDVandAudio(marginTop, marginBottom, audiobufferSize);
        multicore_launch_core1(core1_main);
        initVintageControllers(CPUFreqKHz);
        return ok;
    }

    /// @brief initAll with default audio buffer size of 512
    /// @param selectedRom 
    /// @param CPUFreqKHz 
    /// @param marginTop 
    /// @param marginBottom 
    /// @return 
    bool initAll(char *selectedRom, uint32_t CPUFreqKHz, int marginTop, int marginBottom) {
        return initAll(selectedRom, CPUFreqKHz, marginTop, marginBottom, 512);
    }
    void resetWifi()
    {
#if defined(CYW43_WL_GPIO_LED_PIN)
        printf("Deinitializing CYW43\n");
        cyw43_arch_deinit();
#endif
    }
}