cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS components/third_party)

# Fixed placeholder; real version is in components/firmware_version/firmware_version.c.
set(PROJECT_VER "0")
project(tempchart)