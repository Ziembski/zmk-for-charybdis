# CMakeLists.txt — user-config module root
#
# West/Zephyr discovers this file because zephyr/module.yml sets
# cmake-ext: True.  Everything added here is part of the ZMK build.

# mem_monitor.c is only meaningful when the USB log backend is active,
# i.e. when the right-half (central) firmware is being built.
if(CONFIG_ZMK_USB_LOGGING)
    zephyr_library()
    zephyr_library_sources(config/mem_monitor.c)
endif()
