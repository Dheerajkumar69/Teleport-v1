# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/dheeraj/Code/actual project /teleport/Teleport-v1/build/_deps/json-src"
  "/home/dheeraj/Code/actual project /teleport/Teleport-v1/build/_deps/json-build"
  "/home/dheeraj/Code/actual project /teleport/Teleport-v1/build/_deps/json-subbuild/json-populate-prefix"
  "/home/dheeraj/Code/actual project /teleport/Teleport-v1/build/_deps/json-subbuild/json-populate-prefix/tmp"
  "/home/dheeraj/Code/actual project /teleport/Teleport-v1/build/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp"
  "/home/dheeraj/Code/actual project /teleport/Teleport-v1/build/_deps/json-subbuild/json-populate-prefix/src"
  "/home/dheeraj/Code/actual project /teleport/Teleport-v1/build/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/dheeraj/Code/actual project /teleport/Teleport-v1/build/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/dheeraj/Code/actual project /teleport/Teleport-v1/build/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
