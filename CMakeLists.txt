
cmake_minimum_required(VERSION 3.13)

#add_subdirectory(tusb_xinput xinput_host)
add_subdirectory(drivers/fatfs)
add_subdirectory(drivers/sdcard)

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

)
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
