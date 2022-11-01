cmake_minimum_required(VERSION 3.10)

project(elcorecl-run)

set(ELCORE_CMAKE_TOOLCHAIN_FILE "/opt/eltools_4.0_linux/share/cmake/elcore50_toolchain.cmake")

find_package(elcorecl REQUIRED)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(elcorecl-run elcorecl-run.cc)
target_include_directories(elcorecl-run PRIVATE .)
target_link_libraries(elcorecl-run PRIVATE elcorecl)

install(TARGETS elcorecl-run
        RUNTIME DESTINATION bin)
