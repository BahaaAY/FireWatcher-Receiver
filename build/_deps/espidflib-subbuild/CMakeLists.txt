# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.24.0)

# We name the project and the target for the ExternalProject_Add() call
# to something that will highlight to the user what we are working on if
# something goes wrong and an error message is produced.

project(espidflib-populate NONE)


# Pass through things we've already detected in the main project to avoid
# paying the cost of redetecting them again in ExternalProject_Add()
set(GIT_EXECUTABLE [==[C:/Users/BahaaAY/.espressif/tools/idf-git/2.39.2/cmd/git.exe]==])
set(GIT_VERSION_STRING [==[2.39.2.windows.1]==])
set_property(GLOBAL PROPERTY _CMAKE_FindGit_GIT_EXECUTABLE_VERSION
  [==[C:/Users/BahaaAY/.espressif/tools/idf-git/2.39.2/cmd/git.exe;2.39.2.windows.1]==]
)


include(ExternalProject)
ExternalProject_Add(espidflib-populate
                     "UPDATE_DISCONNECTED" "False" "GIT_REPOSITORY" "https://github.com/UncleRus/esp-idf-lib.git"
                    SOURCE_DIR          "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/_deps/espidflib-src"
                    BINARY_DIR          "B:/Courses/Thesis/IOT/FireWatcher-Receiver/build/_deps/espidflib-build"
                    CONFIGURE_COMMAND   ""
                    BUILD_COMMAND       ""
                    INSTALL_COMMAND     ""
                    TEST_COMMAND        ""
                    USES_TERMINAL_DOWNLOAD  YES
                    USES_TERMINAL_UPDATE    YES
                    USES_TERMINAL_PATCH     YES
)


