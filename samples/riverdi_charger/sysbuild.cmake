# SPDX-License-Identifier: Apache-2.0

# Sysbuild configures each image before the sample CMakeLists can see BOARD,
# so image devicetree overlays have to be selected here.
if(BOARD STREQUAL "stm32f769i_disco")
  set(mcuboot_EXTRA_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/sysbuild/mcuboot_f769.overlay"
      CACHE STRING "")
  set(riverdi_charger_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/boards/stm32f769i_disco.overlay"
      CACHE STRING "")
elseif(BOARD STREQUAL "stm32h7b3i_eval")
  set(mcuboot_EXTRA_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/sysbuild/mcuboot_h7.overlay"
      CACHE STRING "")
  set(riverdi_charger_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/boards/stm32h7b3i_eval.overlay"
      CACHE STRING "")
elseif(BOARD MATCHES "stm32h7|riverdi")
  set(mcuboot_EXTRA_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/sysbuild/mcuboot_h7.overlay"
      CACHE STRING "")
  set(riverdi_charger_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/app_h7.overlay"
      CACHE STRING "")
endif()
