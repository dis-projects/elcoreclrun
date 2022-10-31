find_package(elcorecl REQUIRED)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(elcorecl-run elcorecl-run.cc)
target_link_libraries(elcorecl-run PRIVATE elcorecl)

install(TARGETS elcorecl-run
        RUNTIME DESTINATION bin)
