cmake_minimum_required(VERSION 3.20)

project(wal
    VERSION 0.1.0
    LANGUAGES CXX
)

add_executable(wal
    src/main.cpp
)

target_compile_features(wal PRIVATE cxx_std_20)

