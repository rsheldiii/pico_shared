#include <stdio.h>
#include <cstring>
#include "pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "util/exclusive_proc.h"
#include "tusb.h"
#include "ff.h"
#include "ffwrappers.h"
#include "tf_card.h"

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

#ifdef SPI_SCREEN
#include "spi/spi_buffer.h"
#else
#include "dvi/dvi.h"
#endif // SPI_SCREEN

#include "screen_output.h" // Include the base class header

#ifndef DVIAUDIOFREQ
#define DVIAUDIOFREQ 44100
#endif

// Change pointer type to base class
std::unique_ptr<ScreenOutput> dvi_;

util::ExclusiveProc exclProc_;
char ErrorMessage[ERRORMESSAGESIZE];
bool scaleMode8_7_ = true;
uintptr_t ROM_FILE_ADDR = 0;
int maxRomSize = 0;

namespace Frens
{
    static FATFS fs;

    uint8_t *framebuffer1; // [320 * 240];
    uint8_t *framebuffer2; // [320 * 240];
    uint8_t *framebufferCore0;

    // Shared state
    volatile bool framebuffer1_ready = false;
    volatile bool framebuffer2_ready = false;
    volatile bool use_framebuffer1 = true; // Toggle flag
    volatile bool framebuffer1_rendering = false;
    volatile bool framebuffer2_rendering = false;
    volatile ProcessScanLineFunction processScanLineFunction;
    // Mutex for synchronization
    mutex_t framebuffer_mutex;
    static bool usingFramebuffer = false;

    bool isFrameBufferUsed()
    {
        return usingFramebuffer;
    }
    //
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

    uint64_t time_us()
    {
        absolute_time_t t = get_absolute_time();
        return to_us_since_boot(t);
    }

    uint32_t time_ms()
    {
        absolute_time_t t = get_absolute_time();
        return to_ms_since_boot(t);
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

    // print an int16 as binary
    void printbin16(int16_t v)
    {
        for (int i = 15; i >= 0; i--)
        {
            printf("%d", (v >> i) & 1);
        }
    }

    // End of variuos helper functions

    // Initialize the SD card
    bool initSDCard()
    {
        FRESULT fr;
        TCHAR str[40];
        sleep_ms(1000);

        printf("Mounting SDcard ");

        static pico_fatfs_spi_config_t config = {
            SDCARD_SPI,
            CLK_SLOW_DEFAULT,
            CLK_FAST_DEFAULT_PIO,
            SDCARD_PIN_MISO,
            SDCARD_PIN_CS,
            SDCARD_PIN_SCK,
            SDCARD_PIN_MOSI,
            true // use internal pullup
        };
        bool spi_configured = pico_fatfs_set_config(&config);
        // Try first using SPI
        if (spi_configured) {
            printf("using SPI...");
        } else {
            // fall back to PIO SPI
            pico_fatfs_config_spi_pio(SDCARD_PIO, pio_claim_unused_sm(SDCARD_PIO, true));
            printf("using SPI PIO...");
        }

        fr = f_mount(&fs, "", 1);
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "SD card mount error: %d", fr);
            printf(" %s\n", ErrorMessage);
            return false;
        }
        printf("\n");
        switch (fs.fs_type)
        {
        case FS_FAT12:
            printf("Type is FAT12\n");
            break;
        case FS_FAT16:
            printf("Type is FAT16\n");
            break;
        case FS_FAT32:
            printf("Type is FAT32\n");
            break;
        case FS_EXFAT:
            printf("Type is EXFAT\n");
            break;
        default:
            printf("Type is unknown\n");
            break;
        }
        DWORD fre_clust, fre_sect, tot_sect;
        FATFS *fstemp;
        f_getfree("", &fre_clust, &fstemp);
        /* Get total sectors and free sectors */
        tot_sect = (fstemp->n_fatent - 2) * fstemp->csize;
        fre_sect = fre_clust * fstemp->csize;

        /* Print the free space (assuming 512 bytes/sector) */
        printf("%10lu KiB (%7.2f GB) total drive space.\n%10lu KiB available.\n", tot_sect / 2, fstemp->csize * fstemp->n_fatent * 512E-9, fre_sect / 2);
        fr = my_chdir("/"); // f_chdir("/");
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot change dir to / : %d", fr);
            printf("%s\n", ErrorMessage);
            return false;
        }
        // for f_getcwd to work, set
        //   #define FF_FS_RPATH		2
        // in drivers/fatfs/ffconf.h
        fr = my_getcwd(str, sizeof(str));
        ; // f_getcwd(str, sizeof(str));
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
            // case ScreenMode::MAX:
            //     scaleMode8_7_ = false;
            //     scanLine = false;
            //     printf("ScreenMode::MAX\n");
            //     break;
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

    void flashrom(char *selectedRom, bool swapbytes)
    {
        // Determine loaded rom
        printf("Rebooted by menu\n");
        FIL fil;
        FRESULT fr;
        size_t tmpSize;
        printf("Reading current game from %s and starting emulator\n", ROMINFOFILE);
        if (swapbytes)
        {
            printf("Rom will be byteswapped.\n");
        }
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
#if PICO_RP2040
                size_t bufsize = 64 * 1024;
#else
                size_t bufsize = 128 * 1024;
#endif
                BYTE *buffer = (BYTE *)malloc(bufsize); // (BYTE *)InfoNes_GetPPURAM(&bufsize);
                auto ofs = ROM_FILE_ADDR - XIP_BASE;
                printf("Writing rom %s to flash %x\n", selectedRom, ofs);
                UINT totalBytes = 0;
#if 0
                int blockCount=0;
#endif
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
                                if (swapbytes)
                                {
                                    for (int i = 0; i < bytesRead; i += 2)
                                    {
                                        const unsigned char temp = buffer[i];
                                        buffer[i] = buffer[i + 1];
                                        buffer[i + 1] = temp;
                                    }
                                }
                                blinkLed(onOff);
                                onOff = !onOff;
#if 0
                                printf("Writing block %d (%d bytes) to flash at %x\n", blockCount++, bytesRead, ofs);
#endif
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

    /// @brief Render function in core1 to render line by line
    /// @param
    /// @return
    void __not_in_flash_func(core1_main)()
    {
        while (true)
        {
#ifdef SPI_SCREEN
            // SPI version using Timer IRQ - Core 1 mainly waits/yields
            // The timer IRQ drives the line processing
            // We might still need waitForValidLine if core 0 is faster than the display rate
            // Alternatively, just sleep/yield to let other tasks run.
            // Let's use waitForValidLine for now to ensure sync

            //  dvi_->registerIRQThisCore();
             dvi_->start();
             while (!exclProc_.isExist()) // Check if core 0 needs exclusive access
             {
                // commenting all this out while I try synchronous rendering for now
                // dvi_->waitForValidLine(); // Wait until core 0 has provided a line
                // IRQ handler processes the line
                tight_loop_contents(); // Or maybe sleep/yield?
             }
             dvi_->stop();
            //  dvi_->unregisterIRQThisCore();
#else
            // Original DVI version
             dvi_->registerIRQThisCore();
             dvi_->waitForValidLine();
 
             dvi_->start();
             while (!exclProc_.isExist())
             {
                 if (scaleMode8_7_)
                 {
                     // Default
                     dvi_->convertScanBuffer12bppScaled16_7(34, 32, 288 * 2);
                     // dvi_->convertScanBuffer12bppScaled16_7(0,0 , 320 * 2);
                     //  34 + 252 + 34
                     //  32 + 576 + 32
                 }
                 else
                 {
                     //
                     dvi_->convertScanBuffer12bpp();
                 }
             }
 
             dvi_->unregisterIRQThisCore();
             dvi_->stop();
#endif // SPI_SCREEN
             exclProc_.processOrWaitIfExist();
        }
    }

    static WORD buffer[320];
    /// @brief Render function in core1 to render the framebuffers
    /// @param
    /// @return
    void __not_in_flash_func(coreFB_main)()
    {
        uint8_t *framebufferCore1 = framebuffer1;
        dvi_->registerIRQThisCore();
        dvi_->start();
        int fb1 = 0;
        int fb2 = 0;
        int frame = 0;

        while (true)
        {
            bool may_render = false;
            // Try to acquire the mutex to check for new frames
            if (mutex_try_enter(&framebuffer_mutex, NULL))
            {
                // Check if the other framebuffer is ready
                if (framebuffer1_ready && !framebuffer1_rendering)
                {
                    // printf("Core 1: Switching to framebuffer1\n");
                    framebuffer1_rendering = true;
                    may_render = true;
                    framebufferCore1 = framebuffer1;
                    fb1 = 0;
                }
                else if (framebuffer2_ready && !framebuffer2_rendering)
                {
                    // printf("Core 1: Switching to framebuffer2\n");
                    framebuffer2_rendering = true;
                    may_render = true;
                    framebufferCore1 = framebuffer2;
                    fb2 = 0;
                }
                mutex_exit(&framebuffer_mutex);
            }
            if (may_render)
            {
                auto startLine = dvi_->getBlankSettings().top / 2;
                auto endLine = dvi_->getBlankSettings().bottom / 2;
                // printf("Core 1: Rendering frame %s %d\n", current_framebuffer == framebuffer1 ? "framebuffer1" : "framebuffer2", frame++);
                for (int line = startLine; line < SCREENHEIGHT - endLine; ++line)
                {
                    processScanLineFunction(line - startLine, framebufferCore1, buffer);
                    if (scaleMode8_7_)
                    {
                        dvi_->convertScanBuffer12bppScaled16_7(34, 32, 288 * 2, line, buffer, 640);
                        // 34 + 252 + 34
                        // 32 + 576 + 32
                    }
                    else
                    {
                        // printf("line: %d\n", line);
                        dvi_->convertScanBuffer12bpp(line, buffer, 640);
                    }
                }
                // Mark the framebuffer as no longer being rendered
                mutex_enter_blocking(&framebuffer_mutex);
                if (framebufferCore1 == framebuffer1)
                {
                    framebuffer1_rendering = false;

                    fb1++;
                    fb2 = 0;
                }
                else
                {
                    framebuffer2_rendering = false;

                    fb2++;
                    fb1 = 0;
                }
                mutex_exit(&framebuffer_mutex);
                if (fb1 > 1)
                {
                    printf("fb1: %d\n", fb1);
                    // printf("Framebuffer 1 ready: %d\n", framebuffer1_ready);
                }
                if (fb2 > 1)
                {
                    printf("fb2: %d\n", fb2);
                    // printf("Framebuffer 2 ready: %d\n", framebuffer2_ready);
                }
            }
        }
    }

    void SetFrameBufferProcessScanLineFunction(ProcessScanLineFunction processScanLineFunction)
    {
        if (isFrameBufferUsed())
        {
            mutex_enter_blocking(&framebuffer_mutex);
            Frens::processScanLineFunction = processScanLineFunction;
            memset(framebuffer1, 255, SCREENWIDTH * SCREENHEIGHT);
            memset(framebuffer2, 255, SCREENWIDTH * SCREENHEIGHT);
            mutex_exit(&framebuffer_mutex);
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
#ifdef SPI_SCREEN
        // Instantiate SPI derived class
        dvi_ = std::make_unique<spi_buffer::SPI_Buffer>();
#else
        // Instantiate DVI derived class
        dvi_ = std::make_unique<dvi::DVI>(pio0, &DVICONFIG,
                                          dvi::getTiming640x480p60Hz());
        // DVI-specific audio setup (already guarded by #else)
        dvi_->setAudioFreq(DVIAUDIOFREQ, 28000, 6272);
        dvi_->allocateAudioBuffer(audioBufferSize);
#endif // SPI_SCREEN

        // Common setup using base class methods
        dvi_->getBlankSettings().top = marginTop * 2;
        dvi_->getBlankSettings().bottom = marginBottom * 2;
        // Audio ring buffer access might need care if SPI version's is just a dummy
        dvi_->getAudioRingBuffer().advanceWritePointer(255); // Should be safe even on dummy
    }

    /// @brief Init dv and audio with default audio buffer size of 256
    /// @param marginTop
    /// @param marginBottom
    void initDVandAudio(int marginTop, int marginBottom)
    {
        initDVandAudio(marginTop, marginBottom, 256);
    }
    bool initAll(char *selectedRom, uint32_t CPUFreqKHz, int marginTop, int marginBottom, size_t audiobufferSize, bool swapbytes, bool useFrameBuffer)

    {
        bool ok = false;
        int rc = initLed();
        if (rc != PICO_OK)
        {
            printf("Error initializing LED: %d\n", rc);
        }
        // Calculate the address in flash where roms will be stored
        printf("Flash binary start    : 0x%08x\n", &__flash_binary_start);
        printf("Flash binary end      : 0x%08x\n", &__flash_binary_end);
        printf("Flash size in bytes   :   %8d (%d)Kbytes\n", PICO_FLASH_SIZE_BYTES, PICO_FLASH_SIZE_BYTES / 1024);
        uint8_t *flash_end = (uint8_t *)&__flash_binary_start + PICO_FLASH_SIZE_BYTES - 1;
        printf("Flash end             : 0x%08x\n", flash_end);
        printf("Size program in flash :   %8d bytes (%d) Kbytes\n", &__flash_binary_end - &__flash_binary_start, (&__flash_binary_end - &__flash_binary_start) / 1024);
        // round ROM_FILE_ADDRESS address up to 4k boundary of flash_binary_end
        ROM_FILE_ADDR = ((uintptr_t)&__flash_binary_end + 0xFFF) & ~0xFFF;
        // calculate max rom size
        maxRomSize = flash_end - (uint8_t *)ROM_FILE_ADDR;
        printf("ROM_FILE_ADDR         : 0x%08x\n", ROM_FILE_ADDR);
        printf("Max ROM size          :   %8d bytes (%d) KBytes\n", maxRomSize, maxRomSize / 1024);

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
            if (watchdog_enable_caused_reboot())
            {
                flashrom(selectedRom, swapbytes);
            }
        }
        usingFramebuffer = useFrameBuffer;
        if (usingFramebuffer)
        {
            framebuffer1 = (uint8_t *)malloc(SCREENWIDTH * SCREENHEIGHT);
            framebuffer2 = (uint8_t *)malloc(SCREENWIDTH * SCREENHEIGHT);
            framebufferCore0 = framebuffer1;
            if (framebuffer1 == NULL || framebuffer2 == NULL)
            {
                printf("Error allocating framebuffers\n");
                ok = false;
            }
            mutex_init(&framebuffer_mutex);
        }
        initDVandAudio(marginTop, marginBottom, audiobufferSize);
        if (usingFramebuffer)
        {
            multicore_launch_core1(coreFB_main);
        }
        else
        {
            multicore_launch_core1(core1_main);
        }
        initVintageControllers(CPUFreqKHz);
        return ok;
    }

    void markFrameReadyForReendering(bool waitForFrameReady)
    {
        // switch framebuffers
        // Lock the mutex only to update shared state
        mutex_enter_blocking(&framebuffer_mutex);
        if (use_framebuffer1)
        {
            framebuffer1_ready = true;
            framebuffer2_ready = false;
        }
        else
        {
            framebuffer1_ready = false;
            framebuffer2_ready = true;
        }
        use_framebuffer1 = !use_framebuffer1; // Toggle the framebuffer
        framebufferCore0 = use_framebuffer1 ? framebuffer1 : framebuffer2;
        mutex_exit(&framebuffer_mutex);
        // Wait if core1 is still rendering the framebuffer whe just switched to
#if 0
        int start = time_us_64();
#endif
        if (waitForFrameReady)
        {
            while ((use_framebuffer1 && framebuffer1_rendering) || (!use_framebuffer1 && framebuffer2_rendering))
            {
                tight_loop_contents();
            }
        }
#if 0
        int end = time_us_64();
        printf("Core 0: Switching framebuffers took %ld us\n", end - start);
#endif
        // continue processing next frame while the other core renders the framebuffer
    }

    void resetWifi()
    {
#if defined(CYW43_WL_GPIO_LED_PIN)
        printf("Deinitializing CYW43\n");
        cyw43_arch_deinit();
#endif
    }
}