cmake_minimum_required(VERSION 3.10)
project(ZyroBrowser)

set(CMAKE_CXX_STANDARD 17)

set(ULTRALIGHT_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ultralight-sdk")

include_directories("${ULTRALIGHT_SDK_ROOT}/include")
link_directories("${ULTRALIGHT_SDK_ROOT}/bin")

add_executable(ZyroBrowser main.cpp)

target_link_libraries(ZyroBrowser 
    Ultralight 
    UltralightCore 
    WebCore 
    AppCore
)

# FORCE copy assets folder to build directory on every build
add_custom_command(TARGET ZyroBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:ZyroBrowser>/assets
)

# Copy the SDK binaries (libAppCore.so, etc) to the build folder so you don't need LD_LIBRARY_PATH
add_custom_command(TARGET ZyroBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${ULTRALIGHT_SDK_ROOT}/bin
    $<TARGET_FILE_DIR:ZyroBrowser>
)