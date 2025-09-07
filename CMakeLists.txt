cmake_minimum_required(VERSION 3.12)
project(RemoteDesktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server server.cpp)
add_executable(client client.cpp)

if (WIN32)
    target_link_libraries(server ws2_32)
    target_link_libraries(client winhttp shlwapi ws2_32 advapi32 crypt32 gdiplus gdi32 user32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(server Threads::Threads)
    target_link_libraries(client Threads::Threads X11 Xext)
endif()
