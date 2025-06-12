# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/t/Desktop/DO_AN/esp/esp-idf/components/bootloader/subproject"
  "/home/t/Desktop/DO_AN/esp/free_RTOS/build/bootloader"
  "/home/t/Desktop/DO_AN/esp/free_RTOS/build/bootloader-prefix"
  "/home/t/Desktop/DO_AN/esp/free_RTOS/build/bootloader-prefix/tmp"
  "/home/t/Desktop/DO_AN/esp/free_RTOS/build/bootloader-prefix/src/bootloader-stamp"
  "/home/t/Desktop/DO_AN/esp/free_RTOS/build/bootloader-prefix/src"
  "/home/t/Desktop/DO_AN/esp/free_RTOS/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/t/Desktop/DO_AN/esp/free_RTOS/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/t/Desktop/DO_AN/esp/free_RTOS/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
