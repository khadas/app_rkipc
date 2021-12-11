cmake_minimum_required(VERSION 3.8)

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
aux_source_directory(${PROJECT_SOURCE_DIR}/common/isp SRCS)
aux_source_directory(server SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/ SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/param SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/system SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/rtmp SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/network SRCS)
aux_source_directory(${PROJECT_SOURCE_DIR}/common/storage SRCS)

include_directories(audio
					${PROJECT_SOURCE_DIR}/common/isp
					video
					server
					${PROJECT_SOURCE_DIR}/common
					${PROJECT_SOURCE_DIR}/common/rtsp
					${PROJECT_SOURCE_DIR}/common/rtmp
					${PROJECT_SOURCE_DIR}/common/param
					${PROJECT_SOURCE_DIR}/common/system
					${PROJECT_SOURCE_DIR}/common/network
					${PROJECT_SOURCE_DIR}/common/storage)

link_directories(${PROJECT_SOURCE_DIR}/lib/arm-rockchip830-linux-gnueabihf)

add_definitions(-DISP_HW_V20)
add_definitions(-g -ggdb)

add_executable(${PROJECT_NAME} ${SRCS})

target_link_libraries(${PROJECT_NAME} pthread rockit rkaiq rtsp rtmp wpa_client m rkmuxer)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(FILES rkipc.ini DESTINATION share)
