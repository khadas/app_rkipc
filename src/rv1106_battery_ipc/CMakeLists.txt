cmake_minimum_required(VERSION 3.5)

# SET(CMAKE_C_COMPILER "${PROJECT_SOURCE_DIR}/${PROJECT_SOURCE_DIR}/${PROJECT_SOURCE_DIR}/prebuilts/gcc/linux-x86/arm/gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc")

include_directories(${PROJECT_SOURCE_DIR}/./include
					${CMAKE_SYSROOT}/usr/include/rkaiq/uAPI
					${CMAKE_SYSROOT}/usr/include/rkaiq/algos
					${CMAKE_SYSROOT}/usr/include/rkaiq/common
					${CMAKE_SYSROOT}/usr/include/rkaiq/xcore
					${CMAKE_SYSROOT}/usr/include/rkaiq/iq_parser
					${WPA_SUPPLICANT_INCLUDE_DIRS}
					)

aux_source_directory(./ SRCS)
aux_source_directory(audio SRCS)
aux_source_directory(video SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/isp/rv1106 SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/ SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/param SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/vendor_storage SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/rockiva SRCS)

include_directories(audio
					${PROJECT_SOURCE_DIR}/common/isp/rv1106
					video
					${PROJECT_SOURCE_DIR}/common
					${PROJECT_SOURCE_DIR}/common/rtsp
					${PROJECT_SOURCE_DIR}/common/param
					${PROJECT_SOURCE_DIR}/common/rockiva
					${PROJECT_SOURCE_DIR}/common/rkbar
					${PROJECT_SOURCE_DIR}/common/tuya_ipc/include
					${PROJECT_SOURCE_DIR}/common/tuya_ipc
					${PROJECT_SOURCE_DIR}/common/vendor_storage)

link_directories(${PROJECT_SOURCE_DIR}/lib/arm-rockchip830-linux-uclibcgnueabihf/)

add_definitions(-DISP_HW_V32)
add_definitions(-g -ggdb)

add_executable(${PROJECT_NAME} ${SRCS})
target_link_libraries(${PROJECT_NAME} libpthread.a librockit.a stdc++ librockchip_mpp.a librkaiq.a
				librtsp.a librkaudio_detect.a libaec_bf_process.a libm.a librockiva.a librknnmrt.a librga.a)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION root/usr/bin)
install(FILES rkipc.ini DESTINATION root/usr/share)
