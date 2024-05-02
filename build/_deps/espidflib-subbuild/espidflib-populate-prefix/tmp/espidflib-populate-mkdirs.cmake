# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/_deps/espidflib-src"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/_deps/espidflib-build"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/_deps/espidflib-subbuild/espidflib-populate-prefix"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/_deps/espidflib-subbuild/espidflib-populate-prefix/tmp"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/_deps/espidflib-subbuild/espidflib-populate-prefix/src/espidflib-populate-stamp"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/_deps/espidflib-subbuild/espidflib-populate-prefix/src"
  "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/_deps/espidflib-subbuild/espidflib-populate-prefix/src/espidflib-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/_deps/espidflib-subbuild/espidflib-populate-prefix/src/espidflib-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/_deps/espidflib-subbuild/espidflib-populate-prefix/src/espidflib-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
