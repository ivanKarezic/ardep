# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

if("${BOARD}" STREQUAL "stm32f769i_disco")
  set(SHIELD st_b_lcd40_dsi1_mb1166)
  set(DTC_OVERLAY_FILE boards/stm32f769i_disco.overlay)
elseif("${BOARD}" STREQUAL "stm32h7b3i_eval")
  set(DTC_OVERLAY_FILE boards/stm32h7b3i_eval.overlay)
elseif("${BOARD}" MATCHES "stm32h7|riverdi")
  set(DTC_OVERLAY_FILE app_h7.overlay)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(riverdi_charger)

target_sources(app PRIVATE src/main.c src/charger_uds.c src/truck_art.c)
if(NOT CONFIG_BOARD_STM32H7B3I_EVAL AND NOT CONFIG_BOARD_STM32F769I_DISCO)
  target_sources(app PRIVATE src/panel.c)
endif()
generate_inc_file_for_target(app src/jungheinrich.rgb565
  ${ZEPHYR_BINARY_DIR}/include/generated/jungheinrich.rgb565.inc)

foreach(truck ejc216 efg216 etv216i efg425)
  generate_inc_file_for_target(app src/trucks/${truck}.pal565
    ${ZEPHYR_BINARY_DIR}/include/generated/${truck}.pal565.inc)
endforeach()
