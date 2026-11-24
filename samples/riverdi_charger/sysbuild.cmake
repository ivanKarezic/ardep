# SPDX-License-Identifier: Apache-2.0

if(BOARD STREQUAL "stm32f769i_disco")
  set(mcuboot_EXTRA_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/sysbuild/mcuboot_f769.overlay"
      CACHE STRING "")
elseif(BOARD MATCHES "stm32h7|riverdi")
  set(mcuboot_EXTRA_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/sysbuild/mcuboot_h7.overlay"
      CACHE STRING "")
endif()
