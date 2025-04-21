
cmake_minimum_required(VERSION 3.13)


add_library( pico_shared INTERFACE)
target_sources(pico_shared INTERFACE
menu.cpp
RomLister.cpp
settings.cpp
FrensHelpers.cpp
gamepad.cpp
hid_app.cpp
nespad.cpp
wiipad.cpp
FrensFonts.cpp
ffwrappers.c
)

# All configurations work with the pico_fatfs driver except for the Pimoroni Pico DV. Using pico_fatfs, mounting
# the SDcard on this board always fails with error 3. For this board the older pio_fatfs driver is used.
if(USE_OLD_SDDRIVER EQUAL 0)
        add_subdirectory(drivers/pico_fatfs)      
        target_include_directories(pico_shared INTERFACE
        .
        )
        target_link_libraries(pico_shared INTERFACE pico_fatfs )
else ()
        add_subdirectory(drivers/pio_fatfs/fatfs)
        add_subdirectory(drivers/pio_fatfs/sdcard)
        target_include_directories(pico_shared INTERFACE
        .
        drivers/fatfs
        drivers/sdcard
        # tusb_xinput
        )
        target_link_libraries(pico_shared INTERFACE 
            sdcard
            fatfs
        #   xinput_host
        )
endif()
