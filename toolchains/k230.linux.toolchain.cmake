set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

if(DEFINED ENV{RISCV_ROOT_PATH})
    file(TO_CMAKE_PATH $ENV{RISCV_ROOT_PATH} RISCV_ROOT_PATH)
endif()

if(NOT RISCV_ROOT_PATH)
    message(FATAL_ERROR "RISCV_ROOT_PATH env must be defined")
endif()

set(RISCV_ROOT_PATH ${RISCV_ROOT_PATH} CACHE STRING "root path to riscv toolchain")
set(CMAKE_C_COMPILER "${RISCV_ROOT_PATH}/bin/riscv64-unknown-linux-musl-gcc")
set(CMAKE_CXX_COMPILER "${RISCV_ROOT_PATH}/bin/riscv64-unknown-linux-musl-g++")
set(CMAKE_FIND_ROOT_PATH "${RISCV_ROOT_PATH}/riscv64-unknown-linux-musl")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=rv64imafdcv -mabi=lp64d -mcmodel=medany")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=rv64imafdcv -mabi=lp64d -mcmodel=medany")
