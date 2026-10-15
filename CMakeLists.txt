cmake_minimum_required(VERSION 3.14)

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Define VITASDK (ej. export VITASDK=$HOME/vitasdk).")
endif ()
set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")

project(inotia2 C ASM)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Inotia 2")
set(VITA_TITLEID "INOTIA2PV")
set(VITA_VPKNAME "inotia2")
set(VITA_VERSION "01.00")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

set(DATA_PATH "ux0:data/inotia2/" CACHE STRING "Datos del juego (con / final)")
add_definitions(-DDATA_PATH="${DATA_PATH}")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG_LOADER)
endif()

# -ffixed-r9: el código iOS 3 (ARMv6) usa r9 como temporal sin guardarlo. Si el loader usara r9,
# cualquier llamada al juego lo podría corromper.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -O2 -g -ffixed-r9 -Wall -Wno-unused-function -D_GNU_SOURCE")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -ffixed-r9")

add_executable(${CMAKE_PROJECT_NAME}
  source/main.c
  source/log.c
  source/macho.c
  source/asm.S
  source/imports.c
  source/darwin.c
  source/hle_cs.c
)

target_link_libraries(${CMAKE_PROJECT_NAME}
  -Wl,--whole-archive pthread -Wl,--no-whole-archive
  m
  kubridge_stub
  SceAppMgr_stub
  SceCtrl_stub
  SceTouch_stub
  SceDisplay_stub
  SceSysmodule_stub
  ScePower_stub
  SceKernelDmacMgr_stub
)

vita_create_self(eboot.bin ${CMAKE_PROJECT_NAME} UNSAFE NOASLR)
vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)
