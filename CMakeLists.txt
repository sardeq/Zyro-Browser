cmake_minimum_required(VERSION 3.10)
project(ZyroBrowser)

set(CMAKE_CXX_STANDARD 17)

# 1. Define where the SDK is (relative to this file)
set(ULTRALIGHT_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ultralight-sdk")

# 2. Tell CMake where to find the .h files
include_directories("${ULTRALIGHT_SDK_ROOT}/include")

# 3. Tell CMake where to find the .so (library) files
# On Linux, Ultralight keeps its .so files in the 'bin' folder
link_directories("${ULTRALIGHT_SDK_ROOT}/bin")

add_executable(ZyroBrowser main.cpp)

# 4. Link the libraries manually
# We need all four parts of the engine
target_link_libraries(ZyroBrowser 
    Ultralight 
    UltralightCore 
    WebCore 
    AppCore
)

# 5. Copy resources (CSS/Fonts) to the build folder automatically
add_custom_command(TARGET ZyroBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${ULTRALIGHT_SDK_ROOT}/resources
    $<TARGET_FILE_DIR:ZyroBrowser>/resources
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})