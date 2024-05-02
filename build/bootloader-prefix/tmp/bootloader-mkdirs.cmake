# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "B:/esp/v5.2.1/esp-idf/components/bootloader/subproject"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/bootloader"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/bootloader-prefix"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/bootloader-prefix/tmp"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/bootloader-prefix/src/bootloader-stamp"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/bootloader-prefix/src"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
