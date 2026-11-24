target_include_directories(app PRIVATE .)
target_sources(app PRIVATE charger_uds.c main.c truck_art.c)

add_subdirectory(panels)
add_subdirectory(gui)
add_subdirectory(pm_control)