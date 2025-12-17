cmake_minimum_required(VERSION 3.10)
project(ZyroBrowser)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.0)
pkg_check_modules(SOUP REQUIRED libsoup-2.4)
pkg_check_modules(OPENSSL REQUIRED openssl)

if(UNIX AND NOT APPLE)
    pkg_check_modules(LIBSECRET REQUIRED libsecret-1)
endif()


include_directories(
    ${CMAKE_SOURCE_DIR}/Browser
    ${CMAKE_SOURCE_DIR}/Downloads
    ${CMAKE_SOURCE_DIR}/Globals
    ${CMAKE_SOURCE_DIR}/Security
    ${CMAKE_SOURCE_DIR}/Storage
    ${CMAKE_SOURCE_DIR}/Utils
    ${GTK3_INCLUDE_DIRS} 
    ${WEBKIT_INCLUDE_DIRS} 
    ${SOUP_INCLUDE_DIRS} 
    ${OPENSSL_INCLUDE_DIRS}
)

if(UNIX AND NOT APPLE)
    include_directories(${LIBSECRET_INCLUDE_DIRS})
endif()

link_directories(${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS} ${SOUP_LIBRARY_DIRS} ${OPENSSL_LIBRARY_DIRS})

add_executable(ZyroBrowser 
    main.cpp 
    Globals/Globals.cpp 
    Utils/Utils.cpp 
    Security/Security.cpp 
    Storage/Storage.cpp 
    Downloads/Downloads.cpp 
    Browser/Browser.cpp
)

if(UNIX AND NOT APPLE)
    target_link_libraries(ZyroBrowser ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} ${SOUP_LIBRARIES} ${OPENSSL_LIBRARIES} ${LIBSECRET_LIBRARIES} crypto)
elseif(WIN32)
    target_link_libraries(ZyroBrowser ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} ${SOUP_LIBRARIES} ${OPENSSL_LIBRARIES} crypto Crypt32)
endif()