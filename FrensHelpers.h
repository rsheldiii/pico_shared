#ifndef FRENSHELPERS
#define FRENSHELPERS
#include <string>
#include <algorithm>
#include <memory>

#include "dvi/dvi.h"
#include "dvi_configs.h"
enum class ScreenMode
    {
        SCANLINE_8_7,
        NOSCANLINE_8_7,
        SCANLINE_1_1,
        NOSCANLINE_1_1,
        MAX,
    };
#define CBLACK 15
#define CWHITE 48
#define CRED 6
#define CGREEN 0x2A
#define CBLUE 2
#define CLIGHTBLUE 0x11
#define DEFAULT_FGCOLOR CBLACK // 60
#define DEFAULT_BGCOLOR CWHITE

#define ERRORMESSAGESIZE 40
#define GAMESAVEDIR "/SAVES"
#define ROMINFOFILE "/currentloadedrom.txt"
#define ROM_FILE_ADDR 0x10090000

extern char ErrorMessage[];
extern std::unique_ptr<dvi::DVI> dvi_;
extern bool scaleMode8_7_;
namespace Frens
{
    bool endsWith(std::string const &str, std::string const &suffix);
    std::string str_tolower(std::string s);

    bool cstr_endswith(const char *string, const char *width);
    char **cstr_split(const char *str, const char *delimiters, int *count);
    void stripextensionfromfilename(char *filename);
    char *GetfileNameFromFullPath(char *fullPath);
    bool initSDCard();
    bool applyScreenMode(ScreenMode screenMode_);
    bool screenMode(int incr);
    void flashrom(char *selectedRom);
    void __not_in_flash_func(core1_main)();
    int initLed();
    void initVintageControllers(uint32_t CPUFreqKHz);
    void initDVandAudio();
    bool initAll(char *selectedRom, uint32_t CPUFreqKHz);
    void blinkLed(bool on);
    void resetWifi();
} // namespace Frens


#endif