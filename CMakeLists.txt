cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
<<<<<<< HEAD
set(EXTRA_COMPONENT_DIRS components/third_party)
project(tempchart VERSION "1.0.6")
=======
set(EXTRA_COMPONENT_DIRS components/externlib)

# Single source of firmware version (string in esp_app_desc + numeric OTA code).
set(APP_FIRMWARE_VERSION "1.0.11")
set(PROJECT_VER ${APP_FIRMWARE_VERSION})

string(REPLACE "." ";" _ver_parts "${APP_FIRMWARE_VERSION}")
list(GET _ver_parts 0 _ver_major)
list(GET _ver_parts 1 _ver_minor)
list(GET _ver_parts 2 _ver_patch)
math(EXPR APP_VERSION_CODE "${_ver_major} * 10000 + ${_ver_minor} * 100 + ${_ver_patch}")

project(smart_tempchart VERSION ${APP_FIRMWARE_VERSION})

idf_build_set_property(COMPILE_DEFINITIONS "-DAPP_VERSION_CODE=${APP_VERSION_CODE}" APPEND)
>>>>>>> d1bbfd2 (display about on home 2nd page)
