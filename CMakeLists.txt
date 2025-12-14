cmake_minimum_required(VERSION 3.10)
project(ZyroBrowser)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.0)

include_directories(${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})

add_executable(ZyroBrowser main.cpp)
target_link_libraries(ZyroBrowser ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES})