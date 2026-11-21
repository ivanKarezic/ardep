# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(riverdi_charger)

target_sources(app PRIVATE src/main.c src/panel.c src/charger_uds.c)
generate_inc_file_for_target(app src/jungheinrich.rgb565
  ${ZEPHYR_BINARY_DIR}/include/generated/jungheinrich.rgb565.inc)
