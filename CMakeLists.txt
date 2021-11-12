cmake_minimum_required(VERSION 3.5)

project(rkipc)

option(COMPILE_FOR_LOW_MEMORY "compile for low memory ipc" OFF)
option(COMPILE_FOR_RV1126 "compile for rv1126 ipc" OFF)
option(COMPILE_FOR_RV1126_BATTERY_IPC "compile for rv1126 battery ipc" OFF)
option(COMPILE_FOR_RV1126_SNAPSHOT "compile for rv1126 snapshot" OFF)
option(COMPILE_FOR_RK3588 "compile for rk3588 ipc" OFF)
option(COMPILE_FOR_RK3588_MULTI_IPC "compile for rk3588 multi-ipc" OFF)

if(RKIPC_CROSS_COMPILE)
message(STATUS "CROSS COMPILE TOOL: ${RKIPC_CROSS_COMPILE}")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-rpath-link,${PROJECT_SOURCE_DIR}/lib/${RKIPC_CROSS_COMPILE}")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-rpath-link,${PROJECT_SOURCE_DIR}/lib/${RKIPC_CROSS_COMPILE}")
endif()

if(COMPILE_FOR_LOW_MEMORY)
  add_subdirectory(src/low_memory_ipc)
endif()

if(COMPILE_FOR_RV1126)
  add_subdirectory(src/rv1126_ipc)
endif()

if(COMPILE_FOR_RV1126_BATTERY_IPC)
  add_subdirectory(src/rv1126_battery_ipc)
endif()

if(COMPILE_FOR_RV1126_SNAPSHOT)
  add_subdirectory(src/rv1126_snapshot)
endif()

if(COMPILE_FOR_RK3588)
  add_subdirectory(src/rk3588_ipc)
endif()

if(COMPILE_FOR_RK3588_MULTI_IPC)
  add_subdirectory(src/rk3588_multi_ipc)
endif()
