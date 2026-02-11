cmake_minimum_required(VERSION 3.16)
project(veloce C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(vcs
    main.c
    auth.c
    repos.c
    commits.c
    loading.c
)

if(MSVC)
    target_compile_options(vcs PRIVATE /W4 /permissive-)
else()
    target_compile_options(vcs PRIVATE -Wall -Wextra -Wpedantic)
endif()
