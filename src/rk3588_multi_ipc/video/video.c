// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video.h"
#include "common.h"
#include "isp.h"
#include "osd.h"
#include "rtmp.h"
#include "rtsp_demo.h"
#include "storage.h"

#include "rk_mpi_mmz.h"
#include <rk_debug.h>
#include <rk_mpi_avs.h>
#include <rk_mpi_mb.h>
#include <rk_mpi_rgn.h>
#include <rk_mpi_sys.h>
#include <rk_mpi_venc.h>
#include <rk_mpi_vi.h>
#include <rk_mpi_vo.h>
#include <rk_mpi_vpss.h>

#include <fcntl.h>
#include <inttypes.h> // PRId64
#include <linux/input.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <rga/im2d.h>
#include <rga/rga.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "video.c"

#define MAX_RKIPC_SENSOR_NUM 6
// venc 0 8192*2700
#define VIDEO_PIPE_0 0
// venc 1 3840*1280
#define VIDEO_PIPE_1 1
// venc 2 2048*680
#define VIDEO_PIPE_2 2
// venc 3 8192*2700
#define JPEG_VENC_CHN 3
#define VPSS_AVS_TO_VENC_ID 0
#define RKISP_MAINPATH 0
#define RKISP_SELFPATH 1
#define RKISP_FBCPATH 2

#define RK3588_VO_DEV_HDMI 0
#define RK3588_VO_DEV_MIPI 3
#define RK3588_VOP_LAYER_CLUSTER0 0

#define RTSP_URL_0 "/live/0"
#define RTSP_URL_1 "/live/1"
#define RTSP_URL_2 "/live/2"
#define RTMP_URL_0 "rtmp://127.0.0.1:1935/live/mainstream"
#define RTMP_URL_1 "rtmp://127.0.0.1:1935/live/substream"
#define RTMP_URL_2 "rtmp://127.0.0.1:1935/live/thirdstream"

static pthread_mutex_t g_rtsp_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_sensor_num = 6;
static int g_video_run_ = 1;
static int g_format, g_enable_vo, g_vo_dev_id;
static int pipe_id_ = 0;
static int pipe_id_1_ = 1;
static rtsp_demo_handle g_rtsplive = NULL;
static rtsp_session_handle g_rtsp_session_0, g_rtsp_session_1, g_rtsp_session_2;
static const char *tmp_output_data_type = "H.264";
static const char *tmp_rc_mode;
static const char *tmp_h264_profile;
static const char *tmp_smart;
static pthread_t venc_thread_0, venc_thread_1, venc_thread_2, jpeg_venc_thread_id;
static int capture_one = 0;
static int take_photo_one = 0;
static int enable_jpeg, enable_venc_0, enable_venc_1, enable_venc_2;

MPP_CHN_S vo_chn, avs_out_chn, vpss_in_chn;
MPP_CHN_S vi_chn[MAX_RKIPC_SENSOR_NUM], vpss_out_chn[MAX_RKIPC_SENSOR_NUM],
    venc_chn[MAX_RKIPC_SENSOR_NUM], avs_in_chn[MAX_RKIPC_SENSOR_NUM];

static VO_DEV VoLayer = RK3588_VOP_LAYER_CLUSTER0;

// static void *test_get_vi(void *arg) {
// 	printf("#Start %s thread, arg:%p\n", __func__, arg);
// 	VI_FRAME_S stViFrame;
// 	VI_CHN_STATUS_S stChnStatus;
// 	int loopCount = 0;
// 	int ret = 0;

// 	while (g_video_run_) {
// 		// 5.get the frame
// 		ret = RK_MPI_VI_GetChnFrame(pipe_id_, VIDEO_PIPE_0, &stViFrame, 1000);
// 		if (ret == RK_SUCCESS) {
// 			void *data = RK_MPI_MB_Handle2VirAddr(stViFrame.pMbBlk);
// 			LOG_ERROR("RK_MPI_VI_GetChnFrame ok:data %p loop:%d seq:%d pts:%" PRId64 " ms\n", data,
// 			          loopCount, stViFrame.s32Seq, stViFrame.s64PTS / 1000);
// 			// 6.get the channel status
// 			ret = RK_MPI_VI_QueryChnStatus(pipe_id_, VIDEO_PIPE_0, &stChnStatus);
// 			LOG_ERROR("RK_MPI_VI_QueryChnStatus ret %#x, "
// 			          "w:%d,h:%d,enable:%d,lost:%d,framerate:%d,vbfail:%d\n",
// 			          ret, stChnStatus.stSize.u32Width, stChnStatus.stSize.u32Height,
// 			          stChnStatus.bEnable, stChnStatus.u32LostFrame, stChnStatus.u32FrameRate,
// 			          stChnStatus.u32VbFail);
// 			// 7.release the frame
// 			ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, VIDEO_PIPE_0, &stViFrame);
// 			if (ret != RK_SUCCESS) {
// 				LOG_ERROR("RK_MPI_VI_ReleaseChnFrame fail %#x\n", ret);
// 			}
// 			loopCount++;
// 		} else {
// 			LOG_ERROR("RK_MPI_VI_GetChnFrame timeout %#x\n", ret);
// 		}
// 		usleep(10 * 1000);
// 	}

// 	return 0;
// }

static void *rkipc_get_venc_0(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	// FILE *fp = fopen("/data/venc.h265", "wb");
	stFrame.pstPack = malloc(sizeof(VENC_PACK_S));

	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(VIDEO_PIPE_0, &stFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			// fwrite(data, 1, stFrame.pstPack->u32Len, fp);
			// fflush(fp);
			// LOG_INFO("Count:%d, Len:%d, PTS is %" PRId64 ", enH264EType is %d\n", loopCount,
			//          stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			//          stFrame.pstPack->DataType.enH264EType);

			if (g_rtsplive && g_rtsp_session_0) {
				pthread_mutex_lock(&g_rtsp_mutex);
				rtsp_tx_video(g_rtsp_session_0, data, stFrame.pstPack->u32Len,
				              stFrame.pstPack->u64PTS);
				rtsp_do_event(g_rtsplive);
				pthread_mutex_unlock(&g_rtsp_mutex);
			}
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
				rk_storage_write_video_frame(0, data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 1);
				rk_rtmp_write_video_frame(0, data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          1);
			} else {
				rk_storage_write_video_frame(0, data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 0);
				rk_rtmp_write_video_frame(0, data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          0);
			}

			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(VIDEO_PIPE_0, &stFrame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %#x\n", ret);
			}
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %#x\n", ret);
		}
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);
	// if (fp)
	// 	fclose(fp);

	return 0;
}

static void *rkipc_get_venc_1(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	stFrame.pstPack = malloc(sizeof(VENC_PACK_S));

	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(VIDEO_PIPE_1, &stFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			// LOG_INFO("Count:%d, Len:%d, PTS is %" PRId64", enH264EType is %d\n", loopCount,
			// stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			// stFrame.pstPack->DataType.enH264EType);
			if (g_rtsplive && g_rtsp_session_1) {
				pthread_mutex_lock(&g_rtsp_mutex);
				rtsp_tx_video(g_rtsp_session_1, data, stFrame.pstPack->u32Len,
				              stFrame.pstPack->u64PTS);
				rtsp_do_event(g_rtsplive);
				pthread_mutex_unlock(&g_rtsp_mutex);
			}
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
				rk_storage_write_video_frame(1, data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 1);
				rk_rtmp_write_video_frame(1, data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          1);
			} else {
				rk_storage_write_video_frame(1, data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 0);
				rk_rtmp_write_video_frame(1, data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          0);
			}
			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(VIDEO_PIPE_1, &stFrame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %x\n", ret);
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
		}
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);

	return 0;
}

static void *rkipc_get_venc_2(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	stFrame.pstPack = malloc(sizeof(VENC_PACK_S));

	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(VIDEO_PIPE_2, &stFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			// LOG_INFO("Count:%d, Len:%d, PTS is %" PRId64", enH264EType is %d\n", loopCount,
			// stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			// stFrame.pstPack->DataType.enH264EType);
			if (g_rtsplive && g_rtsp_session_2) {
				pthread_mutex_lock(&g_rtsp_mutex);
				rtsp_tx_video(g_rtsp_session_2, data, stFrame.pstPack->u32Len,
				              stFrame.pstPack->u64PTS);
				rtsp_do_event(g_rtsplive);
				pthread_mutex_unlock(&g_rtsp_mutex);
			}
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
				rk_storage_write_video_frame(2, data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 1);
				rk_rtmp_write_video_frame(2, data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          1);
			} else {
				rk_storage_write_video_frame(2, data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 0);
				rk_rtmp_write_video_frame(2, data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          0);
			}
			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(VIDEO_PIPE_2, &stFrame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %x\n", ret);
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
		}
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);

	return 0;
}

static void *rkipc_get_jpeg(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	char file_name[128] = {0};
	const char *file_path = rk_param_get_string("storage:file_path", "/userdata");
	stFrame.pstPack = malloc(sizeof(VENC_PACK_S));

	while (g_video_run_) {
		usleep(300 * 1000);
		if (!take_photo_one)
			continue;
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(JPEG_VENC_CHN, &stFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			LOG_INFO("Count:%d, Len:%d, PTS is %" PRId64 ", enH264EType is %d\n", loopCount,
			         stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			         stFrame.pstPack->DataType.enH264EType);
			// save jpeg file
			time_t t = time(NULL);
			struct tm tm = *localtime(&t);
			snprintf(file_name, 128, "%s/%d%02d%02d%02d%02d%02d.jpeg", file_path, tm.tm_year + 1900,
			         tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
			LOG_INFO("file_name is %s\n", file_name);
			FILE *fp = fopen(file_name, "wb");
			fwrite(data, 1, stFrame.pstPack->u32Len, fp);
			fflush(fp);
			fclose(fp);
			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(JPEG_VENC_CHN, &stFrame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %x\n", ret);
			}
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
		}
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);

	return 0;
}

int rkipc_rtsp_init() {
	LOG_INFO("start\n");
	g_rtsplive = create_rtsp_demo(554);
	g_rtsp_session_0 = rtsp_new_session(g_rtsplive, RTSP_URL_0);
	g_rtsp_session_1 = rtsp_new_session(g_rtsplive, RTSP_URL_1);
	g_rtsp_session_2 = rtsp_new_session(g_rtsplive, RTSP_URL_2);
	tmp_output_data_type = rk_param_get_string("video.0:output_data_type", "H.264");
	if (!strcmp(tmp_output_data_type, "H.264"))
		rtsp_set_video(g_rtsp_session_0, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
	else if (!strcmp(tmp_output_data_type, "H.265"))
		rtsp_set_video(g_rtsp_session_0, RTSP_CODEC_ID_VIDEO_H265, NULL, 0);
	else
		LOG_ERROR("0 tmp_output_data_type is %s, not support\n", tmp_output_data_type);

	tmp_output_data_type = rk_param_get_string("video.1:output_data_type", "H.264");
	if (!strcmp(tmp_output_data_type, "H.264"))
		rtsp_set_video(g_rtsp_session_1, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
	else if (!strcmp(tmp_output_data_type, "H.265"))
		rtsp_set_video(g_rtsp_session_1, RTSP_CODEC_ID_VIDEO_H265, NULL, 0);
	else
		LOG_ERROR("1 tmp_output_data_type is %s, not support\n", tmp_output_data_type);

	tmp_output_data_type = rk_param_get_string("video.2:output_data_type", "H.264");
	if (!strcmp(tmp_output_data_type, "H.264"))
		rtsp_set_video(g_rtsp_session_2, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
	else if (!strcmp(tmp_output_data_type, "H.265"))
		rtsp_set_video(g_rtsp_session_2, RTSP_CODEC_ID_VIDEO_H265, NULL, 0);
	else
		LOG_ERROR("2 tmp_output_data_type is %s, not support\n", tmp_output_data_type);

	rtsp_sync_video_ts(g_rtsp_session_0, rtsp_get_reltime(), rtsp_get_ntptime());
	rtsp_sync_video_ts(g_rtsp_session_1, rtsp_get_reltime(), rtsp_get_ntptime());
	rtsp_sync_video_ts(g_rtsp_session_2, rtsp_get_reltime(), rtsp_get_ntptime());
	LOG_INFO("end\n");

	return 0;
}

int rkipc_rtsp_deinit() {
	LOG_INFO("start\n");
	LOG_INFO("%s\n", __func__);
	if (g_rtsplive)
		rtsp_del_demo(g_rtsplive);
	g_rtsplive = NULL;
	LOG_INFO("end\n");

	return 0;
}

int rkipc_rtmp_init() {
	int ret = 0;
	ret |= rk_rtmp_init(0, RTMP_URL_0);
	ret |= rk_rtmp_init(1, RTMP_URL_1);
	ret |= rk_rtmp_init(2, RTMP_URL_2);

	return ret;
}

int rkipc_rtmp_deinit() {
	int ret = 0;
	ret |= rk_rtmp_deinit(0);
	ret |= rk_rtmp_deinit(1);
	ret |= rk_rtmp_deinit(2);

	return ret;
}

int rkipc_vi_dev_init() {
	LOG_INFO("start\n");
	int ret = 0;
	VI_DEV_ATTR_S stDevAttr;
	VI_DEV_BIND_PIPE_S stBindPipe;
	memset(&stDevAttr, 0, sizeof(stDevAttr));
	memset(&stBindPipe, 0, sizeof(stBindPipe));
	for (int i = 0; i < g_sensor_num; i++) {
		ret = RK_MPI_VI_GetDevAttr(i, &stDevAttr);
		if (ret == RK_ERR_VI_NOT_CONFIG) {
			ret = RK_MPI_VI_SetDevAttr(i, &stDevAttr);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("%d: RK_MPI_VI_SetDevAttr %#x\n", i, ret);
				return -1;
			}
		} else {
			LOG_ERROR("%d: RK_MPI_VI_SetDevAttr already\n", i);
		}
		ret = RK_MPI_VI_GetDevIsEnable(i);
		if (ret != RK_SUCCESS) {
			ret = RK_MPI_VI_EnableDev(i);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("%d: RK_MPI_VI_EnableDev %#x\n", i, ret);
				return -1;
			}
			stBindPipe.u32Num = 1;
			stBindPipe.PipeId[i] = i;
			ret = RK_MPI_VI_SetDevBindPipe(i, &stBindPipe);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("%d: RK_MPI_VI_SetDevBindPipe %#x\n", i, ret);
				return -1;
			}
		} else {
			LOG_ERROR("%d: RK_MPI_VI_EnableDev already\n", i);
		}
	}
	LOG_INFO("end\n");

	return ret;
}

int rkipc_vi_dev_deinit() {
	LOG_INFO("start\n");
	int ret = 0;
	for (int i = 0; i < g_sensor_num; i++) {
		RK_MPI_VI_DisableDev(i);
		if (ret) {
			LOG_ERROR("RK_MPI_VI_DisableDev error! ret=%#x\n", ret);
			return -1;
		}
		LOG_INFO("RK_MPI_VI_DisableDev success\n");
	}
	LOG_INFO("end\n");

	return ret;
}

int rkipc_multi_vi_init() {
	LOG_INFO("start\n");
	int video_width = rk_param_get_int("avs:source_width", -1);
	int video_height = rk_param_get_int("avs:source_height", -1);
	const char *video_device_name;
	char entry[128] = {'\0'};
	int buf_cnt = 10;
	int ret = 0;
	VI_CHN_ATTR_S vi_chn_attr;

	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
	vi_chn_attr.stSize.u32Width = video_width;
	vi_chn_attr.stSize.u32Height = video_height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	// vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
	if (g_format)
		vi_chn_attr.enCompressMode = COMPRESS_AFBC_16x16;
	vi_chn_attr.u32Depth = 2;
	for (int i = 0; i < g_sensor_num; i++) {
		// snprintf(entry, 127, "avs:source_node_%d", i);
		// video_device_name = rk_param_get_string(entry, NULL);
		// LOG_INFO("%d: video_device_name = %s\n", i, video_device_name);
		// memcpy(vi_chn_attr.stIspOpt.aEntityName, video_device_name, strlen(video_device_name));
		if (g_format) {
			ret = RK_MPI_VI_SetChnAttr(i, RKISP_FBCPATH, &vi_chn_attr);
			ret |= RK_MPI_VI_EnableChn(i, RKISP_FBCPATH);
		} else {
			ret = RK_MPI_VI_SetChnAttr(i, RKISP_MAINPATH, &vi_chn_attr);
			ret |= RK_MPI_VI_EnableChn(i, RKISP_MAINPATH);
		}
		if (ret) {
			LOG_ERROR("%d: ERROR: create VI error! ret=%#x\n", i, ret);
			return -1;
		}
		LOG_INFO("%d: RK_MPI_VI_EnableChn success\n", i);
	}
	LOG_INFO("end\n");

	return 0;
}

int rkipc_multi_vi_deinit() {
	LOG_INFO("start\n");
	int ret = 0;
	for (int i = 0; i < g_sensor_num; i++) {
		ret = RK_MPI_VI_DisableChn(i, 0);
		if (ret) {
			LOG_ERROR("%d: RK_MPI_VI_DisableChn error, ret=%#x\n", i, ret);
			return ret;
		}
		LOG_INFO("%d: RK_MPI_VI_DisableChn success\n", i);
	}
	LOG_INFO("end\n");

	return ret;
}

int rkipc_vpss_init() {
	LOG_INFO("start\n");
	int ret;
	// VPSS for 1 → 4
	VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = {VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3};
	VPSS_GRP VpssGrp = VPSS_AVS_TO_VENC_ID;
	VPSS_GRP_ATTR_S stVpssGrpAttr;
	VPSS_CHN_ATTR_S stVpssChnAttr[VPSS_MAX_CHN_NUM];
	memset(&stVpssGrpAttr, 0, sizeof(stVpssGrpAttr));
	memset(&stVpssChnAttr[0], 0, sizeof(stVpssChnAttr[0]));
	memset(&stVpssChnAttr[1], 0, sizeof(stVpssChnAttr[1]));
	memset(&stVpssChnAttr[2], 0, sizeof(stVpssChnAttr[2]));
	memset(&stVpssChnAttr[3], 0, sizeof(stVpssChnAttr[3]));
	stVpssGrpAttr.u32MaxW = 8192;
	stVpssGrpAttr.u32MaxH = 8192;
	stVpssGrpAttr.enPixelFormat = RK_FMT_YUV420SP;
	stVpssGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	stVpssGrpAttr.stFrameRate.s32DstFrameRate = -1;
	if (g_format)
		stVpssGrpAttr.enCompressMode = COMPRESS_AFBC_16x16;
	ret = RK_MPI_VPSS_CreateGrp(VpssGrp, &stVpssGrpAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VPSS_CreateGrp error! ret is %#x\n", ret);
		return ret;
	}
	if (enable_venc_0) {
		// stVpssChnAttr[0].enChnMode = VPSS_CHN_MODE_PASSTHROUGH;
		// stVpssChnAttr[0].u32Depth = 1;
		// stVpssChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
		// stVpssChnAttr[0].enPixelFormat = RK_FMT_YUV420SP;
		// stVpssChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
		// stVpssChnAttr[0].stFrameRate.s32DstFrameRate = -1;
		// stVpssChnAttr[0].u32Width = 8192;
		// stVpssChnAttr[0].u32Height = 2700;
		// if (g_format)
		// 	stVpssChnAttr[0].enCompressMode = COMPRESS_AFBC_16x16;
		// ret = RK_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn[0], &stVpssChnAttr[0]);
		// if (ret != RK_SUCCESS)
		// 	LOG_ERROR("0: RK_MPI_VPSS_SetChnAttr error! ret is %#x\n", ret);
		// ret = RK_MPI_VPSS_EnableChn(VpssGrp, VpssChn[0]);
		// if (ret != RK_SUCCESS)
		// 	LOG_ERROR("0: RK_MPI_VPSS_EnableChn error! ret is %#x\n", ret);
	}
	if (enable_venc_1) {
		stVpssChnAttr[1].enChnMode = VPSS_CHN_MODE_USER;
		stVpssChnAttr[1].enDynamicRange = DYNAMIC_RANGE_SDR8;
		stVpssChnAttr[1].enPixelFormat = RK_FMT_YUV420SP;
		stVpssChnAttr[1].stFrameRate.s32SrcFrameRate = -1;
		stVpssChnAttr[1].stFrameRate.s32DstFrameRate = -1;
		stVpssChnAttr[1].u32Width = rk_param_get_int("video.1:width", 0);
		stVpssChnAttr[1].u32Height = rk_param_get_int("video.1:height", 0);
		if (g_format)
			stVpssChnAttr[1].enCompressMode = COMPRESS_AFBC_16x16;
		ret = RK_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn[1], &stVpssChnAttr[1]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("1: RK_MPI_VPSS_SetChnAttr error! ret is %#x\n", ret);
		ret = RK_MPI_VPSS_EnableChn(VpssGrp, VpssChn[1]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("1: RK_MPI_VPSS_EnableChn error! ret is %#x\n", ret);
	}
	if (enable_venc_2 || g_enable_vo) {
		stVpssChnAttr[2].enChnMode = VPSS_CHN_MODE_USER;
		stVpssChnAttr[2].enDynamicRange = DYNAMIC_RANGE_SDR8;
		stVpssChnAttr[2].enPixelFormat = RK_FMT_YUV420SP;
		stVpssChnAttr[2].stFrameRate.s32SrcFrameRate = -1;
		stVpssChnAttr[2].stFrameRate.s32DstFrameRate = -1;
		stVpssChnAttr[2].u32Width = rk_param_get_int("video.2:width", 0);
		stVpssChnAttr[2].u32Height = rk_param_get_int("video.2:height", 0);
		if (g_format)
			stVpssChnAttr[2].enCompressMode = COMPRESS_AFBC_16x16;
		ret = RK_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn[2], &stVpssChnAttr[2]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("2: RK_MPI_VPSS_SetChnAttr error! ret is %#x\n", ret);
		ret = RK_MPI_VPSS_EnableChn(VpssGrp, VpssChn[2]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("2: RK_MPI_VPSS_EnableChn error! ret is %#x\n", ret);
	}
	if (enable_jpeg) {
		stVpssChnAttr[3].enChnMode = VPSS_CHN_MODE_USER;
		stVpssChnAttr[3].enDynamicRange = DYNAMIC_RANGE_SDR8;
		stVpssChnAttr[3].enPixelFormat = RK_FMT_YUV420SP;
		stVpssChnAttr[3].stFrameRate.s32SrcFrameRate = -1;
		stVpssChnAttr[3].stFrameRate.s32DstFrameRate = -1;
		stVpssChnAttr[3].u32Width = rk_param_get_int("video.0:width", 0);
		stVpssChnAttr[3].u32Height = rk_param_get_int("video.0:height", 0);
		stVpssChnAttr[3].enCompressMode = COMPRESS_MODE_NONE;
		ret = RK_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn[3], &stVpssChnAttr[3]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("3: RK_MPI_VPSS_SetChnAttr error! ret is %#x\n", ret);
		ret = RK_MPI_VPSS_EnableChn(VpssGrp, VpssChn[3]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("3: RK_MPI_VPSS_EnableChn error! ret is %#x\n", ret);
	}
	ret = RK_MPI_VPSS_EnableBackupFrame(VpssGrp);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VPSS_EnableBackupFrame error! ret is %#x\n", ret);
		return ret;
	}
	ret = RK_MPI_VPSS_SetVProcDev(VpssGrp, VIDEO_PROC_DEV_RGA);
	ret = RK_MPI_VPSS_StartGrp(VpssGrp);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VPSS_StartGrp error! ret is %#x\n", ret);
		return ret;
	}
	LOG_INFO("end\n");

	return ret;
}

int rkipc_vpss_deinit() {
	LOG_INFO("start\n");
	int ret = 0;
	VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = {VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3};
	VPSS_GRP VpssGrp = VPSS_AVS_TO_VENC_ID;
	ret |= RK_MPI_VPSS_StopGrp(VpssGrp);
	if (enable_venc_0)
		ret |= RK_MPI_VPSS_DisableChn(VpssGrp, VpssChn[0]);
	if (enable_venc_1)
		ret |= RK_MPI_VPSS_DisableChn(VpssGrp, VpssChn[1]);
	if (enable_venc_2)
		ret |= RK_MPI_VPSS_DisableChn(VpssGrp, VpssChn[2]);
	if (enable_jpeg)
		ret |= RK_MPI_VPSS_DisableChn(VpssGrp, VpssChn[3]);
	ret |= RK_MPI_VPSS_DisableBackupFrame(VpssGrp);
	ret |= RK_MPI_VPSS_DestroyGrp(VpssGrp);
	LOG_INFO("end\n");

	return ret;
}

int rkipc_avs_init() {
	LOG_INFO("start\n");
	int ret;
	AVS_GRP s32GrpId = 0;
	AVS_CHN s32ChnId = 0;
	AVS_MOD_PARAM_S stAvsModParam;
	AVS_GRP_ATTR_S stAvsGrpAttr;
	AVS_OUTPUT_ATTR_S stAvsOutAttr;
	AVS_CHN_ATTR_S stAvsChnAttr[4];

	stAvsModParam.u32WorkingSetSize = 67 * 1024;
	stAvsGrpAttr.enMode = rk_param_get_int("avs:avs_mode", 0);
	const char *lut_file_path = rk_param_get_string("avs:avs_file_path", NULL);
	LOG_INFO("lut_file_path = %s\n", lut_file_path);
	memcpy(stAvsGrpAttr.stLUT.aFilePath, lut_file_path, strlen(lut_file_path) + 1);
	memset(stAvsGrpAttr.stLUT.aFilePath + strlen(lut_file_path) + 1, '\0', sizeof(char));
	LOG_INFO("stAvsGrpAttr.stLUT.aFilePath = %s\n", stAvsGrpAttr.stLUT.aFilePath);
	stAvsGrpAttr.u32PipeNum = g_sensor_num;
	stAvsGrpAttr.stGainAttr.enMode = AVS_GAIN_MODE_AUTO;
	stAvsGrpAttr.stOutAttr.enPrjMode = AVS_PROJECTION_EQUIRECTANGULAR;
	stAvsGrpAttr.stOutAttr.stCenter.s32X = 5088 / 2;
	stAvsGrpAttr.stOutAttr.stCenter.s32Y = 1520 / 2;
	stAvsGrpAttr.stOutAttr.stFOV.u32FOVX = 36000;
	stAvsGrpAttr.stOutAttr.stFOV.u32FOVY = 18000;
	stAvsGrpAttr.stOutAttr.stORIRotation.s32Roll = 9000;
	stAvsGrpAttr.stOutAttr.stORIRotation.s32Pitch = 9000;
	stAvsGrpAttr.stOutAttr.stORIRotation.s32Yaw = 0;
	stAvsGrpAttr.stOutAttr.stRotation.s32Roll = 0;
	stAvsGrpAttr.stOutAttr.stRotation.s32Pitch = 0;
	stAvsGrpAttr.stOutAttr.stRotation.s32Yaw = 0;
	stAvsGrpAttr.stLUT.enAccuracy = AVS_LUT_ACCURACY_HIGH;
	stAvsGrpAttr.bSyncPipe = rk_param_get_int("avs:sync", 1);
	stAvsGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	stAvsGrpAttr.stFrameRate.s32DstFrameRate = -1;

	if (g_format)
		stAvsChnAttr[0].enCompressMode = COMPRESS_AFBC_16x16;
	else
		stAvsChnAttr[0].enCompressMode = COMPRESS_MODE_NONE;
	stAvsChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	stAvsChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	stAvsChnAttr[0].u32Depth = 1;
	stAvsChnAttr[0].u32Width = rk_param_get_int("avs:avs_width", -1);
	stAvsChnAttr[0].u32Height = rk_param_get_int("avs:avs_height", -1);
	stAvsChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
	ret = RK_MPI_AVS_SetModParam(&stAvsModParam);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_AVS_SetModParam failed, ret is %#x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_AVS_SetModParam success\n");

	ret = RK_MPI_AVS_CreateGrp(s32GrpId, &stAvsGrpAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_AVS_CreateGrp failed, ret is %#x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_AVS_CreateGrp success\n");

	ret = RK_MPI_AVS_SetChnAttr(s32GrpId, s32ChnId, &stAvsChnAttr[0]);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_AVS_SetChnAttr failed, ret is %#x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_AVS_SetChnAttr success\n");

	ret = RK_MPI_AVS_EnableChn(s32GrpId, s32ChnId);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_AVS_EnableChn failed, ret is %#x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_AVS_EnableChn success\n");

	ret = RK_MPI_AVS_StartGrp(s32GrpId);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_AVS_StartGrp failed, ret is %#x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_AVS_StartGrp success\n");
	LOG_INFO("end\n");

	return ret;
}

int rkipc_avs_deinit() {
	LOG_INFO("start\n");
	int ret = 0;
	AVS_GRP s32GrpId = 0;
	AVS_CHN s32ChnId = 0;
	ret = RK_MPI_AVS_StopGrp(s32GrpId);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_AVS_StopGrp failed with %#x!\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_AVS_StopGrp success\n");
	ret = RK_MPI_AVS_DisableChn(s32GrpId, s32ChnId);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_AVS_DisableChn failed with %#x!\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_AVS_DisableChn success\n");
	ret = RK_MPI_AVS_DestroyGrp(s32GrpId);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_AVS_DestroyGrp failed with %#x!\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_AVS_DestroyGrp success\n");
	LOG_INFO("end\n");

	return ret;
}

int rkipc_venc_0_init() {
	LOG_INFO("start\n");
	int ret;
	int venc_width = rk_param_get_int("video.0:width", -1);
	int venc_height = rk_param_get_int("video.0:height", -1);
	// VENC[0] init
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	tmp_output_data_type = rk_param_get_string("video.0:output_data_type", NULL);
	tmp_rc_mode = rk_param_get_string("video.0:rc_mode", NULL);
	tmp_h264_profile = rk_param_get_string("video.0:h264_profile", NULL);
	if ((tmp_output_data_type == NULL) || (tmp_rc_mode == NULL)) {
		LOG_ERROR("tmp_output_data_type or tmp_rc_mode is NULL\n");
		return -1;
	}
	LOG_INFO("tmp_output_data_type is %s, tmp_rc_mode is %s, tmp_h264_profile is %s\n",
	         tmp_output_data_type, tmp_rc_mode, tmp_h264_profile);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;

		if (!strcmp(tmp_h264_profile, "high"))
			venc_chn_attr.stVencAttr.u32Profile = 100;
		else if (!strcmp(tmp_h264_profile, "main"))
			venc_chn_attr.stVencAttr.u32Profile = 77;
		else if (!strcmp(tmp_h264_profile, "baseline"))
			venc_chn_attr.stVencAttr.u32Profile = 66;
		else
			LOG_ERROR("tmp_h264_profile is %s\n", tmp_h264_profile);

		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = rk_param_get_int("video.0:max_rate", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.0:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.0:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = rk_param_get_int("video.0:max_rate", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.0:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.0:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = rk_param_get_int("video.0:max_rate", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.0:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.0:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = rk_param_get_int("video.0:max_rate", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.0:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.0:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	tmp_smart = rk_param_get_string("video.0:smart", NULL);
	if (!strcmp(tmp_rc_mode, "open")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
	} else {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
	}
	// venc_chn_attr.stGopAttr.u32GopSize = rk_param_get_int("video.0:gop", -1);

	venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	venc_chn_attr.stVencAttr.u32PicWidth = 8192;
	venc_chn_attr.stVencAttr.u32PicHeight = 2700;
	venc_chn_attr.stVencAttr.u32VirWidth = 8176;
	venc_chn_attr.stVencAttr.u32VirHeight = 2720;
	venc_chn_attr.stVencAttr.u32StreamBufCnt = 3;
	venc_chn_attr.stVencAttr.u32BufSize = 8176 * 2720 * 3 / 2;
	// venc_chn_attr.stVencAttr.u32Depth = 1;
	ret = RK_MPI_VENC_CreateChn(VIDEO_PIPE_0, &venc_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%#x\n", ret);
		return -1;
	}
	// VENC_RC_PARAM_S h265_RcParam;
	// RK_MPI_VENC_GetRcParam(VIDEO_PIPE_0, &h265_RcParam);
	// h265_RcParam.s32FirstFrameStartQp = 26;
	// h265_RcParam.stParamH265.u32StepQp = 8;
	// h265_RcParam.stParamH265.u32MaxQp = 51;
	// h265_RcParam.stParamH265.u32MinQp = 10;
	// h265_RcParam.stParamH265.u32MaxIQp = 46;
	// h265_RcParam.stParamH265.u32MinIQp = 24;
	// h265_RcParam.stParamH265.s32DeltIpQp = rk_param_get_int("avs:deltea_ip_qp", -4);
	// RK_MPI_VENC_SetRcParam(VIDEO_PIPE_0, &h265_RcParam);

	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	RK_MPI_VENC_StartRecvFrame(VIDEO_PIPE_0, &stRecvParam);
	pthread_create(&venc_thread_0, NULL, rkipc_get_venc_0, NULL);
	LOG_INFO("end\n");

	return 0;
}

int rkipc_venc_0_deinit() {
	LOG_INFO("start\n");
	int ret;
	pthread_join(venc_thread_0, NULL);
	ret |= RK_MPI_VENC_StopRecvFrame(VIDEO_PIPE_0);
	ret |= RK_MPI_VENC_DestroyChn(VIDEO_PIPE_0);
	LOG_INFO("end\n");

	return ret;
}

int rkipc_venc_1_init() {
	LOG_INFO("start\n");
	int ret;
	int venc_width = rk_param_get_int("video.1:width", 0);
	int venc_height = rk_param_get_int("video.1:height", 0);
	// VENC[1] init
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	tmp_output_data_type = rk_param_get_string("video.1:output_data_type", NULL);
	tmp_rc_mode = rk_param_get_string("video.1:rc_mode", NULL);
	tmp_h264_profile = rk_param_get_string("video.1:h264_profile", NULL);
	if ((tmp_output_data_type == NULL) || (tmp_rc_mode == NULL)) {
		LOG_ERROR("tmp_output_data_type or tmp_rc_mode is NULL\n");
		return -1;
	}
	LOG_INFO("tmp_output_data_type is %s, tmp_rc_mode is %s, tmp_h264_profile is %s\n",
	         tmp_output_data_type, tmp_rc_mode, tmp_h264_profile);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;

		if (!strcmp(tmp_h264_profile, "high"))
			venc_chn_attr.stVencAttr.u32Profile = 100;
		else if (!strcmp(tmp_h264_profile, "main"))
			venc_chn_attr.stVencAttr.u32Profile = 77;
		else if (!strcmp(tmp_h264_profile, "baseline"))
			venc_chn_attr.stVencAttr.u32Profile = 66;
		else
			LOG_ERROR("tmp_h264_profile is %s\n", tmp_h264_profile);

		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = rk_param_get_int("video.1:gop", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = rk_param_get_int("video.1:max_rate", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.1:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.1:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.1:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.1:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = rk_param_get_int("video.1:gop", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = rk_param_get_int("video.1:max_rate", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.1:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.1:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.1:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.1:src_frame_rate_num", -1);
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = rk_param_get_int("video.1:gop", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = rk_param_get_int("video.1:max_rate", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.1:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.1:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.1:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.1:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = rk_param_get_int("video.1:gop", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = rk_param_get_int("video.1:max_rate", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.1:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.1:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.1:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.1:src_frame_rate_num", -1);
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	tmp_smart = rk_param_get_string("video.1:smart", NULL);
	if (!strcmp(tmp_rc_mode, "open")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
	} else {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
	}
	// venc_chn_attr.stGopAttr.u32GopSize = rk_param_get_int("video.1:gop", -1);

	venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	venc_chn_attr.stVencAttr.u32PicWidth = venc_width;
	venc_chn_attr.stVencAttr.u32PicHeight = venc_height;
	venc_chn_attr.stVencAttr.u32VirWidth = venc_width;
	venc_chn_attr.stVencAttr.u32VirHeight = venc_height;
	venc_chn_attr.stVencAttr.u32StreamBufCnt = 3;
	venc_chn_attr.stVencAttr.u32BufSize = venc_width * venc_height * 3 / 2;
	// venc_chn_attr.stVencAttr.u32Depth = 1;
	ret = RK_MPI_VENC_CreateChn(VIDEO_PIPE_1, &venc_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%#x\n", ret);
		return -1;
	}
	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	RK_MPI_VENC_StartRecvFrame(VIDEO_PIPE_1, &stRecvParam);
	pthread_create(&venc_thread_1, NULL, rkipc_get_venc_1, NULL);
	LOG_INFO("end\n");

	return 0;
}

int rkipc_venc_1_deinit() {
	LOG_INFO("start\n");
	int ret;
	pthread_join(venc_thread_1, NULL);
	ret |= RK_MPI_VENC_StopRecvFrame(VIDEO_PIPE_1);
	ret |= RK_MPI_VENC_DestroyChn(VIDEO_PIPE_1);
	LOG_INFO("end\n");

	return ret;
}

int rkipc_venc_2_init() {
	LOG_INFO("start\n");
	int ret;
	int venc_width = rk_param_get_int("video.2:width", 0);
	int venc_height = rk_param_get_int("video.2:height", 0);
	// VENC[2] init
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	tmp_output_data_type = rk_param_get_string("video.2:output_data_type", NULL);
	tmp_rc_mode = rk_param_get_string("video.2:rc_mode", NULL);
	tmp_h264_profile = rk_param_get_string("video.2:h264_profile", NULL);
	if ((tmp_output_data_type == NULL) || (tmp_rc_mode == NULL)) {
		LOG_ERROR("tmp_output_data_type or tmp_rc_mode is NULL\n");
		return -1;
	}
	LOG_INFO("tmp_output_data_type is %s, tmp_rc_mode is %s, tmp_h264_profile is %s\n",
	         tmp_output_data_type, tmp_rc_mode, tmp_h264_profile);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;

		if (!strcmp(tmp_h264_profile, "high"))
			venc_chn_attr.stVencAttr.u32Profile = 100;
		else if (!strcmp(tmp_h264_profile, "main"))
			venc_chn_attr.stVencAttr.u32Profile = 77;
		else if (!strcmp(tmp_h264_profile, "baseline"))
			venc_chn_attr.stVencAttr.u32Profile = 66;
		else
			LOG_ERROR("tmp_h264_profile is %s\n", tmp_h264_profile);

		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = rk_param_get_int("video.2:gop", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = rk_param_get_int("video.2:max_rate", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.2:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.2:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.2:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.2:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = rk_param_get_int("video.1:gop", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = rk_param_get_int("video.1:max_rate", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.2:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.2:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.2:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.2:src_frame_rate_num", -1);
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = rk_param_get_int("video.2:gop", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = rk_param_get_int("video.2:max_rate", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.2:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.2:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.2:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.2:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = rk_param_get_int("video.2:gop", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = rk_param_get_int("video.2:max_rate", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.2:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.2:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.2:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.2:src_frame_rate_num", -1);
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	tmp_smart = rk_param_get_string("video.2:smart", NULL);
	if (!strcmp(tmp_rc_mode, "open")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
	} else {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
	}
	// venc_chn_attr.stGopAttr.u32GopSize = rk_param_get_int("video.2:gop", -1);

	venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	venc_chn_attr.stVencAttr.u32PicWidth = venc_width;
	venc_chn_attr.stVencAttr.u32PicHeight = venc_height;
	venc_chn_attr.stVencAttr.u32VirWidth = venc_width;
	venc_chn_attr.stVencAttr.u32VirHeight = venc_height;
	venc_chn_attr.stVencAttr.u32StreamBufCnt = 3;
	venc_chn_attr.stVencAttr.u32BufSize = venc_width * venc_height * 3 / 2;
	// venc_chn_attr.stVencAttr.u32Depth = 1;
	ret = RK_MPI_VENC_CreateChn(VIDEO_PIPE_2, &venc_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%#x\n", ret);
		return -1;
	}
	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	RK_MPI_VENC_StartRecvFrame(VIDEO_PIPE_2, &stRecvParam);
	pthread_create(&venc_thread_2, NULL, rkipc_get_venc_2, NULL);
	LOG_INFO("end\n");

	return 0;
}

int rkipc_venc_2_deinit() {
	LOG_INFO("start\n");
	int ret;
	int venc_width = rk_param_get_int("video.2:width", 0);
	int venc_height = rk_param_get_int("video.2:height", 0);
	pthread_join(venc_thread_2, NULL);
	ret |= RK_MPI_VENC_StopRecvFrame(VIDEO_PIPE_2);
	ret |= RK_MPI_VENC_DestroyChn(VIDEO_PIPE_2);
	LOG_INFO("end\n");

	return ret;
}

int rkipc_venc_3_init() {
	// jpeg resolution same to video.0
	int ret;
	int video_width = rk_param_get_int("video.0:width", -1);
	int video_height = rk_param_get_int("video.0:height", -1);
	// VENC[3] init
	VENC_CHN_ATTR_S jpeg_chn_attr;
	memset(&jpeg_chn_attr, 0, sizeof(jpeg_chn_attr));
	jpeg_chn_attr.stVencAttr.enType = RK_VIDEO_ID_MJPEG;
	jpeg_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	jpeg_chn_attr.stVencAttr.u32PicWidth = video_width;
	jpeg_chn_attr.stVencAttr.u32PicHeight = video_height;
	jpeg_chn_attr.stVencAttr.u32VirWidth = video_width;
	jpeg_chn_attr.stVencAttr.u32VirHeight = video_height;
	jpeg_chn_attr.stVencAttr.u32StreamBufCnt = 2;
	jpeg_chn_attr.stVencAttr.u32BufSize = video_width * video_height * 3 / 2;
	// jpeg_chn_attr.stVencAttr.u32Depth = 1;
	ret = RK_MPI_VENC_CreateChn(JPEG_VENC_CHN, &jpeg_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%d\n", ret);
		return -1;
	}
	VENC_JPEG_PARAM_S stJpegParam;
	memset(&stJpegParam, 0, sizeof(stJpegParam));
	stJpegParam.u32Qfactor = 95;
	RK_MPI_VENC_SetJpegParam(JPEG_VENC_CHN, &stJpegParam);
	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = 1;
	RK_MPI_VENC_StartRecvFrame(JPEG_VENC_CHN,
	                           &stRecvParam); // must, for no streams callback running failed
	RK_MPI_VENC_StopRecvFrame(JPEG_VENC_CHN);
	pthread_create(&jpeg_venc_thread_id, NULL, rkipc_get_jpeg, NULL);

	return ret;
}

int rkipc_venc_3_deinit() {
	int ret = 0;
	ret = RK_MPI_VENC_StopRecvFrame(JPEG_VENC_CHN);
	ret |= RK_MPI_VENC_DestroyChn(JPEG_VENC_CHN);
	if (ret)
		LOG_ERROR("ERROR: Destroy VENC error! ret=%#x\n", ret);

	return ret;
}

int rkipc_vo_init() {
	LOG_INFO("start\n");
	int ret;
	VO_PUB_ATTR_S VoPubAttr;
	VO_VIDEO_LAYER_ATTR_S stLayerAttr;
	VO_CSC_S VideoCSC;
	VO_CHN_ATTR_S VoChnAttr;
	RK_U32 u32DispBufLen;
	memset(&VoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
	memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
	memset(&VideoCSC, 0, sizeof(VO_CSC_S));
	memset(&VoChnAttr, 0, sizeof(VoChnAttr));
	if (g_vo_dev_id == RK3588_VO_DEV_HDMI) {
		VoPubAttr.enIntfType = VO_INTF_HDMI;
		VoPubAttr.enIntfSync = VO_OUTPUT_1080P60;
	} else {
		VoPubAttr.enIntfType = VO_INTF_MIPI;
		VoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;
	}
	ret = RK_MPI_VO_SetPubAttr(g_vo_dev_id, &VoPubAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetPubAttr %#x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_SetPubAttr success\n");

	ret = RK_MPI_VO_Enable(g_vo_dev_id);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_Enable %#x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_Enable success\n");

	ret = RK_MPI_VO_GetLayerDispBufLen(VoLayer, &u32DispBufLen);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("Get display buf len failed with error code %d!\n", ret);
		return ret;
	}
	LOG_INFO("Get VoLayer %d disp buf len is %d.\n", VoLayer, u32DispBufLen);
	u32DispBufLen = 3;
	ret = RK_MPI_VO_SetLayerDispBufLen(VoLayer, u32DispBufLen);
	if (ret != RK_SUCCESS) {
		return ret;
	}
	LOG_INFO("Agin Get VoLayer %d disp buf len is %d.\n", VoLayer, u32DispBufLen);

	/* get vo attribute*/
	ret = RK_MPI_VO_GetPubAttr(g_vo_dev_id, &VoPubAttr);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_GetPubAttr fail!\n");
		return ret;
	}
	LOG_INFO("RK_MPI_VO_GetPubAttr success\n");

	stLayerAttr.stDispRect.s32X = 0;
	stLayerAttr.stDispRect.s32Y = 0;
	stLayerAttr.stDispRect.u32Width = VoPubAttr.stSyncInfo.u16Hact;
	stLayerAttr.stDispRect.u32Height = VoPubAttr.stSyncInfo.u16Vact;
	stLayerAttr.stImageSize.u32Width = VoPubAttr.stSyncInfo.u16Hact;
	stLayerAttr.stImageSize.u32Height = VoPubAttr.stSyncInfo.u16Vact;
	LOG_INFO("stLayerAttr W=%d, H=%d\n", stLayerAttr.stDispRect.u32Width,
	         stLayerAttr.stDispRect.u32Height);

	stLayerAttr.u32DispFrmRt = 25;
	stLayerAttr.enPixFormat = RK_FMT_RGB888;
	stLayerAttr.bDoubleFrame = RK_TRUE;
	VideoCSC.enCscMatrix = VO_CSC_MATRIX_IDENTITY;
	VideoCSC.u32Contrast = 50;
	VideoCSC.u32Hue = 50;
	VideoCSC.u32Luma = 50;
	VideoCSC.u32Satuature = 50;
	RK_S32 u32VoChn = 0;

	/*bind layer0 to device hd0*/
	ret = RK_MPI_VO_BindLayer(VoLayer, g_vo_dev_id, VO_LAYER_MODE_GRAPHIC);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_BindLayer VoLayer = %d error\n", VoLayer);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_BindLayer success\n");

	ret = RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetLayerAttr VoLayer = %d error\n", VoLayer);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_SetLayerAttr success\n");

	ret = RK_MPI_VO_SetLayerSpliceMode(VoLayer, VO_SPLICE_MODE_RGA);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetLayerSpliceMode VoLayer = %d error\n", VoLayer);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_SetLayerSpliceMode success\n");

	ret = RK_MPI_VO_EnableLayer(VoLayer);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_EnableLayer VoLayer = %d error\n", VoLayer);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_EnableLayer success\n");

	ret = RK_MPI_VO_SetLayerCSC(VoLayer, &VideoCSC);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetLayerCSC error\n");
		return ret;
	}
	LOG_INFO("RK_MPI_VO_SetLayerCSC success\n");

	ret = RK_MPI_VO_EnableChn(RK3588_VOP_LAYER_CLUSTER0, u32VoChn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("create RK3588_VOP_LAYER_CLUSTER0 layer %d ch vo failed!\n", u32VoChn);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_EnableChn success\n");

	VoChnAttr.bDeflicker = RK_FALSE;
	VoChnAttr.u32Priority = 1;
	VoChnAttr.stRect.s32X = 0;
	VoChnAttr.stRect.s32Y = 0;
	VoChnAttr.stRect.u32Width = stLayerAttr.stDispRect.u32Width;
	VoChnAttr.stRect.u32Height = stLayerAttr.stDispRect.u32Height;
	if (g_vo_dev_id == RK3588_VO_DEV_MIPI)
		VoChnAttr.enRotation = ROTATION_90;
	ret = RK_MPI_VO_SetChnAttr(VoLayer, 0, &VoChnAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetChnAttr failed, ret is %#x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_SetChnAttr success\n");
	LOG_INFO("end\n");

	return ret;
}

int rkipc_vo_deinit() {
	LOG_INFO("start\n");
	int ret = 0;
	// disable vo layer
	ret = RK_MPI_VO_DisableLayer(VoLayer);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_DisableLayer failed\n");
		return -1;
	}
	// disable vo dev
	ret = RK_MPI_VO_Disable(g_vo_dev_id);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_Disable failed\n");
		return -1;
	}
	LOG_INFO("end\n");

	return ret;
}

int rkipc_bind_init() {
	LOG_INFO("start\n");
	int ret;
	for (int i = 0; i < g_sensor_num; i++) {
		LOG_INFO("i is %d\n", i);
		vi_chn[i].enModId = RK_ID_VI;
		vi_chn[i].s32DevId = i;
		if (g_format)
			vi_chn[i].s32ChnId = RKISP_FBCPATH;
		else
			vi_chn[i].s32ChnId = RKISP_MAINPATH;

		avs_in_chn[i].enModId = RK_ID_AVS;
		avs_in_chn[i].s32DevId = 0;
		avs_in_chn[i].s32ChnId = i;

		ret = RK_MPI_SYS_Bind(&vi_chn[i], &avs_in_chn[i]);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("bind error %#x: vi [%d, %d] -> avs [%d, %d]\n", ret, vi_chn[i].s32DevId,
			          vi_chn[i].s32ChnId, avs_in_chn[i].s32DevId, avs_in_chn[i].s32ChnId);
			// return ret;
		}
	}

	avs_out_chn.enModId = RK_ID_AVS;
	avs_out_chn.s32DevId = 0;
	avs_out_chn.s32ChnId = 0;
	vpss_in_chn.enModId = RK_ID_VPSS;
	vpss_in_chn.s32DevId = 0;
	vpss_in_chn.s32ChnId = 0;
	ret = RK_MPI_SYS_Bind(&avs_out_chn, &vpss_in_chn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("bind error %#x: avs_out_chn [%d, %d] -> vpss_in_chn [%d, %d]", ret,
		          avs_out_chn.s32DevId, avs_out_chn.s32ChnId, vpss_in_chn.s32DevId,
		          vpss_in_chn.s32ChnId);
		return ret;
	}

	// bind vpss and venc
	for (int i = 0; i < 4; i++) {
		vpss_out_chn[i].enModId = RK_ID_VPSS;
		vpss_out_chn[i].s32DevId = 0;
		vpss_out_chn[i].s32ChnId = i;
		venc_chn[i].enModId = RK_ID_VENC;
		venc_chn[i].s32DevId = 0;
		venc_chn[i].s32ChnId = i;
	}
	if (enable_venc_0) {
		ret = RK_MPI_SYS_Bind(&avs_out_chn, &venc_chn[0]);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("bind error %#x: vpss_out_chn[i] [%d, %d] -> venc_chn[i] [%d, %d]", ret,
			          avs_out_chn.s32DevId, avs_out_chn.s32ChnId, venc_chn[0].s32DevId,
			          venc_chn[0].s32ChnId);
		}
	}
	if (enable_venc_1) {
		ret = RK_MPI_SYS_Bind(&vpss_out_chn[1], &venc_chn[1]);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("bind error %#x: vpss_out_chn[i] [%d, %d] -> venc_chn[i] [%d, %d]", ret,
			          vpss_out_chn[1].s32DevId, vpss_out_chn[1].s32ChnId, venc_chn[1].s32DevId,
			          venc_chn[1].s32ChnId);
		}
	}
	if (enable_venc_2) {
		ret = RK_MPI_SYS_Bind(&vpss_out_chn[2], &venc_chn[2]);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("bind error %#x: vpss_out_chn[i] [%d, %d] -> venc_chn[i] [%d, %d]", ret,
			          vpss_out_chn[2].s32DevId, vpss_out_chn[2].s32ChnId, venc_chn[2].s32DevId,
			          venc_chn[2].s32ChnId);
		}
	}
	if (enable_jpeg) {
		ret = RK_MPI_SYS_Bind(&vpss_out_chn[3], &venc_chn[3]);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("bind error %#x: vpss_out_chn[i] [%d, %d] -> venc_chn[i] [%d, %d]", ret,
			          vpss_out_chn[3].s32DevId, vpss_out_chn[3].s32ChnId, venc_chn[3].s32DevId,
			          venc_chn[3].s32ChnId);
		}
	}

	if (g_enable_vo) {
		vo_chn.enModId = RK_ID_VO;
		vo_chn.s32DevId = 0;
		vo_chn.s32ChnId = 0;
		ret = RK_MPI_SYS_Bind(&vpss_out_chn[2], &vo_chn);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("bind error %#x: vpss_out_chn[2] [%d, %d] -> vo [%d, %d]", ret,
			          vpss_out_chn[2].s32DevId, vpss_out_chn[2].s32ChnId, vo_chn.s32DevId,
			          vo_chn.s32ChnId);
			// return ret;
		}
	}

	LOG_INFO("end\n");
	return ret;
}

int rkipc_bind_deinit() {
	LOG_INFO("start\n");
	int ret = 0;

	// unbind vpss and venc
	if (enable_venc_0) {
		ret = RK_MPI_SYS_UnBind(&avs_out_chn, &venc_chn[0]);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("bind error %#x: vpss_out_chn[i] [%d, %d] -> venc_chn[i] [%d, %d]", ret,
			          avs_out_chn.s32DevId, avs_out_chn.s32ChnId, venc_chn[0].s32DevId,
			          venc_chn[0].s32ChnId);
			return ret;
		}
	}
	if (enable_venc_1) {
		ret = RK_MPI_SYS_UnBind(&vpss_out_chn[1], &venc_chn[1]);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("bind error %#x: vpss_out_chn[i] [%d, %d] -> venc_chn[i] [%d, %d]", ret,
			          vpss_out_chn[1].s32DevId, vpss_out_chn[1].s32ChnId, venc_chn[1].s32DevId,
			          venc_chn[1].s32ChnId);
			return ret;
		}
	}
	if (enable_venc_2) {
		ret = RK_MPI_SYS_UnBind(&vpss_out_chn[2], &venc_chn[2]);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("bind error %#x: vpss_out_chn[i] [%d, %d] -> venc_chn[i] [%d, %d]", ret,
			          vpss_out_chn[2].s32DevId, vpss_out_chn[2].s32ChnId, venc_chn[2].s32DevId,
			          venc_chn[2].s32ChnId);
			return ret;
		}
	}
	if (enable_jpeg) {
		ret = RK_MPI_SYS_UnBind(&vpss_out_chn[3], &venc_chn[3]);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("bind error %#x: vpss_out_chn[i] [%d, %d] -> venc_chn[i] [%d, %d]", ret,
			          vpss_out_chn[3].s32DevId, vpss_out_chn[3].s32ChnId, venc_chn[3].s32DevId,
			          venc_chn[3].s32ChnId);
			return ret;
		}
	}

	ret = RK_MPI_SYS_UnBind(&avs_out_chn, &vpss_in_chn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("bind error %#x: avs_out_chn [%d, %d] -> vpss_in_chn [%d, %d]", ret,
		          avs_out_chn.s32DevId, avs_out_chn.s32ChnId, vpss_in_chn.s32DevId,
		          vpss_in_chn.s32ChnId);
		return ret;
	}

	if (g_enable_vo) {
		ret = RK_MPI_SYS_UnBind(&vpss_out_chn[2], &vo_chn);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("unbind error %#x: vpss_out_chn[2] [%d, %d] -> vo [%d, %d]", ret,
			          vpss_out_chn[2].s32DevId, vpss_out_chn[2].s32ChnId, vo_chn.s32DevId,
			          vo_chn.s32ChnId);
			return ret;
		}
	}
	for (int i = 0; i < g_sensor_num; i++) {
		LOG_INFO("i is %d\n", i);
		ret = RK_MPI_SYS_UnBind(&vi_chn[i], &avs_in_chn[i]);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("unbind error %#x: vi [%d, %d] -> avs [%d, %d]\n", ret, vi_chn[i].s32DevId,
			          vi_chn[i].s32ChnId, avs_in_chn[i].s32DevId, avs_in_chn[i].s32ChnId);
			// return ret;
		}
	}
	LOG_INFO("end\n");

	return ret;
}

// export API
int rk_video_get_gop(int stream_id, int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:gop", stream_id);
	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_gop(int stream_id, int value) {
	char entry[128] = {'\0'};
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
	tmp_output_data_type = rk_param_get_string(entry, "H.264");
	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
	tmp_rc_mode = rk_param_get_string(entry, "CBR");
	if (!strcmp(tmp_output_data_type, "H.264")) {
		if (!strcmp(tmp_rc_mode, "CBR"))
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = value;
		else
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = value;
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		if (!strcmp(tmp_rc_mode, "CBR"))
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = value;
		else
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = value;
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
	snprintf(entry, 127, "video.%d:gop", stream_id);
	rk_param_set_int(entry, value);

	return 0;
}

int rk_video_get_max_rate(int stream_id, int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:max_rate", stream_id);
	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_max_rate(int stream_id, int value) {
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
	tmp_output_data_type = rk_param_get_string(entry, "H.264");
	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
	tmp_rc_mode = rk_param_get_string(entry, "CBR");
	if (!strcmp(tmp_output_data_type, "H.264")) {
		if (!strcmp(tmp_rc_mode, "CBR"))
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = value;
		else
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = value;
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		if (!strcmp(tmp_rc_mode, "CBR"))
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = value;
		else
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = value;
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
	snprintf(entry, 127, "video.%d:max_rate", stream_id);
	rk_param_set_int(entry, value);

	return 0;
}

int rk_video_get_RC_mode(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
	*value = rk_param_get_string(entry, "CBR");

	return 0;
}

int rk_video_set_RC_mode(int stream_id, const char *value) {
	char entry_output_data_type[128] = {'\0'};
	char entry_gop[128] = {'\0'};
	char entry_max_rate[128] = {'\0'};
	char entry_dst_frame_rate_den[128] = {'\0'};
	char entry_dst_frame_rate_num[128] = {'\0'};
	char entry_src_frame_rate_den[128] = {'\0'};
	char entry_src_frame_rate_num[128] = {'\0'};
	char entry_rc_mode[128] = {'\0'};
	snprintf(entry_output_data_type, 127, "video.%d:output_data_type", stream_id);
	snprintf(entry_gop, 127, "video.%d:gop", stream_id);
	snprintf(entry_max_rate, 127, "video.%d:output_data_type", stream_id);
	snprintf(entry_dst_frame_rate_den, 127, "video.%d:dst_frame_rate_den", stream_id);
	snprintf(entry_dst_frame_rate_num, 127, "video.%d:dst_frame_rate_num", stream_id);
	snprintf(entry_src_frame_rate_den, 127, "video.%d:src_frame_rate_den", stream_id);
	snprintf(entry_src_frame_rate_num, 127, "video.%d:src_frame_rate_num", stream_id);
	snprintf(entry_rc_mode, 127, "video.%d:rc_mode", stream_id);

	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	tmp_output_data_type = rk_param_get_string(entry_output_data_type, "H.264");
	if (!strcmp(tmp_output_data_type, "H.264")) {
		if (!strcmp(value, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = rk_param_get_int(entry_gop, -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = rk_param_get_int(entry_max_rate, -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen =
			    rk_param_get_int(entry_dst_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum =
			    rk_param_get_int(entry_dst_frame_rate_num, -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen =
			    rk_param_get_int(entry_src_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum =
			    rk_param_get_int(entry_src_frame_rate_num, -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = rk_param_get_int(entry_gop, -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = rk_param_get_int(entry_max_rate, -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen =
			    rk_param_get_int(entry_dst_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum =
			    rk_param_get_int(entry_dst_frame_rate_num, -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen =
			    rk_param_get_int(entry_src_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum =
			    rk_param_get_int(entry_src_frame_rate_num, -1);
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		if (!strcmp(value, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = rk_param_get_int(entry_gop, -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = rk_param_get_int(entry_max_rate, -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen =
			    rk_param_get_int(entry_dst_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum =
			    rk_param_get_int(entry_dst_frame_rate_num, -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen =
			    rk_param_get_int(entry_src_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum =
			    rk_param_get_int(entry_src_frame_rate_num, -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = rk_param_get_int(entry_gop, -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = rk_param_get_int(entry_max_rate, -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen =
			    rk_param_get_int(entry_dst_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum =
			    rk_param_get_int(entry_dst_frame_rate_num, -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen =
			    rk_param_get_int(entry_src_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum =
			    rk_param_get_int(entry_src_frame_rate_num, -1);
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
	rk_param_set_string(entry_rc_mode, value);

	return 0;
}

int rk_video_get_output_data_type(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
	*value = rk_param_get_string(entry, "H.265");

	return 0;
}

int rk_video_set_output_data_type(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
	rk_param_set_string(entry, value);
	rk_video_restart();

	return 0;
}

int rk_video_get_rc_quality(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:rc_quality", stream_id);
	*value = rk_param_get_string(entry, "high");

	return 0;
}

int rk_video_set_rc_quality(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:rc_quality", stream_id);
	rk_param_set_string(entry, value);

	return 0;
}

int rk_video_get_smart(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:smart", stream_id);
	*value = rk_param_get_string(entry, "close");

	return 0;
}

int rk_video_set_smart(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:smart", stream_id);
	rk_param_set_string(entry, value);
	rk_video_restart();

	return 0;
}

int rk_video_get_stream_type(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:stream_type", stream_id);
	*value = rk_param_get_string(entry, "mainStream");

	return 0;
}

int rk_video_set_stream_type(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:stream_type", stream_id);
	rk_param_set_string(entry, value);

	return 0;
}

int rk_video_get_h264_profile(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:h264_profile", stream_id);
	*value = rk_param_get_string(entry, "high");

	return 0;
}

int rk_video_set_h264_profile(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:h264_profile", stream_id);
	rk_param_set_string(entry, value);
	rk_video_restart();

	return 0;
}

int rk_video_get_resolution(int stream_id, char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:width", stream_id);
	int width = rk_param_get_int(entry, 0);
	snprintf(entry, 127, "video.%d:height", stream_id);
	int height = rk_param_get_int(entry, 0);
	sprintf(*value, "%d*%d", width, height);

	return 0;
}

int rk_video_set_resolution(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	int width, height;

	sscanf(value, "%d*%d", &width, &height);
	LOG_INFO("value is %s, width is %d, height is %d\n", value, width, height);
	snprintf(entry, 127, "video.%d:width", stream_id);
	rk_param_set_int(entry, width);
	snprintf(entry, 127, "video.%d:height", stream_id);
	rk_param_set_int(entry, height);
	rk_video_restart();

	return 0;
}

int rk_video_get_frame_rate(int stream_id, char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:dst_frame_rate_den", stream_id);
	int den = rk_param_get_int(entry, -1);
	snprintf(entry, 127, "video.%d:dst_frame_rate_num", stream_id);
	int num = rk_param_get_int(entry, -1);
	if (den == 1)
		sprintf(*value, "%d", num);
	else
		sprintf(*value, "%d/%d", num, den);

	return 0;
}

int rk_video_set_frame_rate(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	int den, num;
	if (strchr(value, '/') == NULL) {
		den = 1;
		sscanf(value, "%d", &num);
	} else {
		sscanf(value, "%d/%d", &num, &den);
	}
	LOG_INFO("num is %d, den is %d\n", num, den);

	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
	tmp_output_data_type = rk_param_get_string(entry, "H.264");
	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
	tmp_rc_mode = rk_param_get_string(entry, "CBR");
	if (!strcmp(tmp_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = num;
		} else {
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = num;
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = num;
		} else {
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = num;
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);

	snprintf(entry, 127, "video.%d:dst_frame_rate_den", stream_id);
	rk_param_set_int(entry, den);
	snprintf(entry, 127, "video.%d:dst_frame_rate_num", stream_id);
	rk_param_set_int(entry, num);

	return 0;
}

int rk_video_get_frame_rate_in(int stream_id, char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:src_frame_rate_den", stream_id);
	int den = rk_param_get_int(entry, -1);
	snprintf(entry, 127, "video.%d:src_frame_rate_num", stream_id);
	int num = rk_param_get_int(entry, -1);
	if (den == 1)
		sprintf(*value, "%d", num);
	else
		sprintf(*value, "%d/%d", num, den);

	return 0;
}

int rk_video_set_frame_rate_in(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	int den, num;
	if (strchr(value, '/') == NULL) {
		den = 1;
		sscanf(value, "%d", &num);
	} else {
		sscanf(value, "%d/%d", &num, &den);
	}
	LOG_INFO("num is %d, den is %d\n", num, den);
	snprintf(entry, 127, "video.%d:src_frame_rate_den", stream_id);
	rk_param_set_int(entry, den);
	snprintf(entry, 127, "video.%d:src_frame_rate_num", stream_id);
	rk_param_set_int(entry, num);
	rk_video_restart();

	return 0;
}

int rkipc_osd_cover_create(int id, osd_data_s *osd_data) {
	LOG_INFO("id is %d\n", id);
	int ret = 0;
	RGN_HANDLE coverHandle = id;
	RGN_ATTR_S stCoverAttr;
	MPP_CHN_S stCoverChn;
	RGN_CHN_ATTR_S stCoverChnAttr;

	memset(&stCoverAttr, 0, sizeof(stCoverAttr));
	memset(&stCoverChnAttr, 0, sizeof(stCoverChnAttr));
	// create cover regions
	stCoverAttr.enType = COVER_RGN;
	ret = RK_MPI_RGN_Create(coverHandle, &stCoverAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", coverHandle, ret);
		RK_MPI_RGN_Destroy(coverHandle);
		return RK_FAILURE;
	}
	LOG_INFO("The handle: %d, create success\n", coverHandle);

	// display cover regions to venc groups
	stCoverChn.enModId = RK_ID_VENC;
	stCoverChn.s32DevId = 0;
	stCoverChn.s32ChnId = 0;
	memset(&stCoverChnAttr, 0, sizeof(stCoverChnAttr));
	stCoverChnAttr.bShow = osd_data->enable;
	stCoverChnAttr.enType = COVER_RGN;
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X = osd_data->origin_x;
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y = osd_data->origin_y;
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width = osd_data->width;
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height = osd_data->height;
	stCoverChnAttr.unChnAttr.stCoverChn.u32Color = 0xffffff;
	stCoverChnAttr.unChnAttr.stCoverChn.u32Layer = id;
	ret = RK_MPI_RGN_AttachToChn(coverHandle, &stCoverChn, &stCoverChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) failed with %#x\n", coverHandle, ret);
		return RK_FAILURE;
	}
	LOG_INFO("cover region to chn success\n");

	return ret;
}

int rkipc_osd_cover_destroy(int id) {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	// Detach osd from chn
	MPP_CHN_S stMppChn;
	RGN_HANDLE RgnHandle = id;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = 0;

	ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_DetachFrmChn (%d) failed with %#x\n", RgnHandle, ret);
		return RK_FAILURE;
	}
	LOG_INFO("Detach handle:%d from chn success", RgnHandle);

	// destory region
	ret = RK_MPI_RGN_Destroy(RgnHandle);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Destroy [%d] failed with %#x\n", RgnHandle, ret);
	}
	LOG_INFO("Destory handle:%d success\n", RgnHandle);

	return ret;
}

/* PixFormat: big-edian:BGRA8888 => A:bit31~bit24 R:bit23~bit16 G:bit15~bit8 B:bit7~bit0 */
const RK_U32 u32BGRA8888ColorTblUser[256] = {
    // transparency/gray/red/earthy yellow /blue
    0x00ffffff, 0xff5e6060, 0xffe9491e, 0xfff4bc1f, 0xff1ca2dd, 0xff87bd43, 0xffff1f1f, 0xff0000af,
    0xff0000d7, 0xff0000ff, 0xff005f00, 0xff005f5f, 0xff005f87, 0xff005faf, 0xff005fd7, 0xff005fff,
    0xff008000, 0xff008080, 0xff008700, 0xff00875f, 0xff008787, 0xff0087af, 0xff0087d7, 0xff0087ff,
    0xff00af00, 0xff00af5f, 0xff00af87, 0xff00afaf, 0xff00afd7, 0xff00afff, 0xff00d700, 0xff00d75f,
    0xff00d787, 0xff00d7af, 0xff00d7d7, 0xff00d7ff, 0xff00ff00, 0xff00ff28, 0xff00ff5f, 0xff00ff87,
    0xff00ffaf, 0xff00ffd7, 0xff00ffff, 0xff00ffff, 0xff080808, 0xff121212, 0xff1c1c1c, 0xff262626,
    0xff303030, 0xff3a3a3a, 0xff444444, 0xff4e4e4e, 0xff585858, 0xff5f0000, 0xff5f005f, 0xff5f0087,
    0xff5f00af, 0xff5f00d7, 0xff5f00ff, 0xff5f5f00, 0xff5f5f5f, 0xff5f5f87, 0xff5f5faf, 0xff5f5fd7,
    0xff5f5fff, 0xff5f8700, 0xff5f875f, 0xff5f8787, 0xff5f87af, 0xff5f87d7, 0xff5f87ff, 0xff5faf00,
    0xff5faf5f, 0xff5faf87, 0xff5fafaf, 0xff5fafd7, 0xff5fafff, 0xff5fd700, 0xff5fd75f, 0xff5fd787,
    0xff5fd7af, 0xff5fd7d7, 0xff5fd7ff, 0xff5fff00, 0xff5fff5f, 0xff5fff87, 0xff5fffaf, 0xff5fffd7,
    0xff5fffff, 0xff626262, 0xff6c6c6c, 0xff767676, 0xff800000, 0xff800080, 0xff808000, 0xff808080,
    0xff808080, 0xff870000, 0xff87005f, 0xff870087, 0xff8700af, 0xff8700d7, 0xff8700ff, 0xff875f00,
    0xff875f5f, 0xff875f87, 0xff875faf, 0xff875fd7, 0xff875fff, 0xff878700, 0xff87875f, 0xff878787,
    0xff8787af, 0xff8787d7, 0xff8787ff, 0xff87af00, 0xff87af5f, 0xff87af87, 0xff87afaf, 0xff87afd7,
    0xff87afff, 0xff87d700, 0xff87d75f, 0xff87d787, 0xff87d7af, 0xff87d7d7, 0xff87d7ff, 0xff87ff00,
    0xff87ff5f, 0xff87ff87, 0xff87ffaf, 0xff87ffd7, 0xff87ffff, 0xff8a8a8a, 0xff949494, 0xff9e9e9e,
    0xffa8a8a8, 0xffaf0000, 0xffaf005f, 0xffaf0087, 0xffaf00af, 0xffaf00d7, 0xffaf00ff, 0xffaf5f00,
    0xffaf5f5f, 0xffaf5f87, 0xffaf5faf, 0xffaf5fd7, 0xffaf5fff, 0xffaf8700, 0xffaf875f, 0xffaf8787,
    0xffaf87af, 0xffaf87d7, 0xffaf87ff, 0xffafaf00, 0xffafaf5f, 0xffafaf87, 0xffafafaf, 0xffafafd7,
    0xffafafff, 0xffafd700, 0xffafd75f, 0xffafd787, 0xffafd7af, 0xffafd7d7, 0xffafd7ff, 0xffafff00,
    0xffafff5f, 0xffafff87, 0xffafffaf, 0xffafffd7, 0xffafffff, 0xffb2b2b2, 0xffbcbcbc, 0xffc0c0c0,
    0xffc6c6c6, 0xffd0d0d0, 0xffd70000, 0xffd7005f, 0xffd70087, 0xffd700af, 0xffd700d7, 0xffd700ff,
    0xffd75f00, 0xffd75f5f, 0xffd75f87, 0xffd75faf, 0xffd75fd7, 0xffd75fff, 0xffd78700, 0xffd7875f,
    0xffd78787, 0xffd787af, 0xffd787d7, 0xffd787ff, 0xffd7af00, 0xffd7af5f, 0xffd7af87, 0xffd7afaf,
    0xffd7afd7, 0xffd7afff, 0xffd7d700, 0xffd7d75f, 0xffd7d787, 0xffd7d7af, 0xffd7d7d7, 0xffd7d7ff,
    0xffd7ff00, 0xffd7ff5f, 0xffd7ff87, 0xffd7ffaf, 0xffd7ffd7, 0xffd7ffff, 0xffdadada, 0xffe4e4e4,
    0xffeeeeee, 0xffff0000, 0xffff0028, 0xffff005f, 0xffff0087, 0xffff00af, 0xffff00d7, 0xffff00ff,
    0xffff00ff, 0xffff5f00, 0xffff5f5f, 0xffff5f87, 0xffff5faf, 0xffff5fd7, 0xffff5fff, 0xffff8700,
    0xffff875f, 0xffff8787, 0xffff87af, 0xffff87d7, 0xffff87ff, 0xffffaf00, 0xffffaf5f, 0xffffaf87,
    0xffffafaf, 0xffffafd7, 0xffffafff, 0xffffd700, 0xffffd75f, 0xffffd787, 0xffffd7af, 0xffffd7d7,
    0xffffd7ff, 0xffffff00, 0xffffff28, 0xffffff5f, 0xffffff87, 0xffffffaf, 0xffffffd7, 0xffffffff,
};

int rkipc_osd_bmp_create(int id, osd_data_s *osd_data) {
	LOG_INFO("id is %d\n", id);
	int ret = 0;
	RGN_HANDLE RgnHandle = id;
	RGN_ATTR_S stRgnAttr;
	MPP_CHN_S stMppChn;
	RGN_CHN_ATTR_S stRgnChnAttr;
	BITMAP_S stBitmap;

	// create overlay regions
	memset(&stRgnAttr, 0, sizeof(stRgnAttr));
	stRgnAttr.enType = OVERLAY_RGN;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = RK_FMT_ARGB8888;
	stRgnAttr.unAttr.stOverlay.stSize.u32Width = osd_data->width;
	stRgnAttr.unAttr.stOverlay.stSize.u32Height = osd_data->height;
	stRgnAttr.unAttr.stOverlay.u32ClutNum = 255;
	memcpy(stRgnAttr.unAttr.stOverlay.u32Clut, u32BGRA8888ColorTblUser,
	       sizeof(u32BGRA8888ColorTblUser));
	ret = RK_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", RgnHandle, ret);
		RK_MPI_RGN_Destroy(RgnHandle);
		return RK_FAILURE;
	}
	LOG_INFO("The handle: %d, create success\n", RgnHandle);

	// display overlay regions to venc groups
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = 0;
	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = osd_data->enable;
	stRgnChnAttr.enType = OVERLAY_RGN;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = osd_data->origin_x;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = osd_data->origin_y;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = id;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	if (enable_venc_0) {
		stMppChn.s32ChnId = 0;
		ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_INFO("RK_MPI_RGN_AttachToChn to venc0 success\n");
	}
	if (enable_venc_1) {
		stMppChn.s32ChnId = 1;
		ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc1 failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_INFO("RK_MPI_RGN_AttachToChn to venc1 success\n");
	}
	if (enable_venc_2) {
		stMppChn.s32ChnId = 2;
		ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc2 failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_INFO("RK_MPI_RGN_AttachToChn to venc2 success\n");
	}

	// set bitmap
	stBitmap.enPixelFormat = RK_FMT_ARGB8888;
	stBitmap.u32Width = osd_data->width;
	stBitmap.u32Height = osd_data->height;
	stBitmap.pData = (RK_VOID *)osd_data->buffer;
	ret = RK_MPI_RGN_SetBitMap(RgnHandle, &stBitmap);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_RGN_SetBitMap failed with %#x\n", ret);
		return RK_FAILURE;
	}

	return ret;
}

int rkipc_osd_bmp_destroy(int id) {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	// Detach osd from chn
	MPP_CHN_S stMppChn;
	RGN_HANDLE RgnHandle = id;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	if (enable_venc_0) {
		stMppChn.s32ChnId = 0;
		ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_DetachFrmChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_INFO("RK_MPI_RGN_DetachFromChn to venc0 success\n");
	}
	if (enable_venc_1) {
		stMppChn.s32ChnId = 1;
		ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_DetachFrmChn (%d) to venc1 failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_INFO("RK_MPI_RGN_DetachFromChn to venc1 success\n");
	}
	if (enable_venc_2) {
		stMppChn.s32ChnId = 2;
		ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_DetachFrmChn (%d) to venc2 failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_INFO("RK_MPI_RGN_DetachFromChn to venc2 success\n");
	}

	// destory region
	ret = RK_MPI_RGN_Destroy(RgnHandle);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Destroy [%d] failed with %#x\n", RgnHandle, ret);
	}
	LOG_INFO("Destory handle:%d success\n", RgnHandle);

	return ret;
}

int rkipc_osd_bmp_change(int id, osd_data_s *osd_data) {
	// LOG_INFO("id is %d\n", id);
	int ret = 0;
	RGN_HANDLE RgnHandle = id;
	BITMAP_S stBitmap;

	// set bitmap
	stBitmap.enPixelFormat = RK_FMT_ARGB8888;
	stBitmap.u32Width = osd_data->width;
	stBitmap.u32Height = osd_data->height;
	stBitmap.pData = (RK_VOID *)osd_data->buffer;
	ret = RK_MPI_RGN_SetBitMap(RgnHandle, &stBitmap);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_RGN_SetBitMap failed with %#x\n", ret);
		return RK_FAILURE;
	}

	return ret;
}

int rkipc_osd_init() {
	rk_osd_cover_create_callback_register(rkipc_osd_cover_create);
	rk_osd_cover_destroy_callback_register(rkipc_osd_cover_destroy);
	rk_osd_bmp_create_callback_register(rkipc_osd_bmp_create);
	rk_osd_bmp_destroy_callback_register(rkipc_osd_bmp_destroy);
	rk_osd_bmp_change_callback_register(rkipc_osd_bmp_change);
	rk_osd_init();

	return 0;
}

int rkipc_osd_deinit() {
	rk_osd_deinit();
	rk_osd_cover_create_callback_register(NULL);
	rk_osd_cover_destroy_callback_register(NULL);
	rk_osd_bmp_create_callback_register(NULL);
	rk_osd_bmp_destroy_callback_register(NULL);
	rk_osd_bmp_change_callback_register(NULL);

	return 0;
}

static void *wait_key_event(void *arg) {
	int key_fd;
	key_fd = open("/dev/input/event1", O_RDONLY);
	if (key_fd < 0) {
		LOG_ERROR("can't open /dev/input/event1\n");
		return NULL;
	}
	fd_set rfds;
	int nfds = key_fd + 1;
	struct timeval timeout;
	struct input_event key_event;

	while (g_video_run_) {
		// The rfds collection must be emptied every time,
		// otherwise the descriptor changes cannot be detected
		timeout.tv_sec = 1;
		FD_ZERO(&rfds);
		FD_SET(key_fd, &rfds);
		select(nfds, &rfds, NULL, NULL, &timeout);
		// wait for the key event to occur
		if (FD_ISSET(key_fd, &rfds)) {
			read(key_fd, &key_event, sizeof(key_event));
			LOG_INFO("[timeval:sec:%d,usec:%d,type:%d,code:%d,value:%d]\n", key_event.time.tv_sec,
			         key_event.time.tv_usec, key_event.type, key_event.code, key_event.value);
			if ((key_event.code == 139) && key_event.value) {
				LOG_INFO("start capture\n");
				system("dumpsys avs record 0 1 /userdata/");
				// capture_one = 1;
			}
		}
	}

	if (key_fd) {
		close(key_fd);
		key_fd = 0;
	}
	LOG_DEBUG("wait key event out\n");
	return NULL;
}

static void *save_all_vi_frame(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VI_FRAME_S stViFrame;
	VI_CHN_STATUS_S stChnStatus;
	int ret = 0;
	char file_name[256];
	void *data[6];

	while (g_video_run_) {
		if (capture_one == 0) {
			usleep(300 * 1000);
			continue;
		}
		capture_one = 0;
		for (int i = 0; i < 6; i++) {
			if (g_format)
				ret = RK_MPI_VI_GetChnFrame(i, RKISP_FBCPATH, &stViFrame, -1);
			else
				ret = RK_MPI_VI_GetChnFrame(i, RKISP_MAINPATH, &stViFrame, -1);
			if (ret == RK_SUCCESS) {
				data[i] = RK_MPI_MB_Handle2VirAddr(stViFrame.pMbBlk);
				LOG_ERROR("RK_MPI_VI_GetChnFrame ok:data %p loop:%d seq:%d pts:%" PRId64 " ms\n",
				          data[i], i, stViFrame.s32Seq, stViFrame.s64PTS / 1000);
			} else {
				LOG_ERROR("RK_MPI_VI_GetChnFrame timeout %#x\n", ret);
			}
			time_t t = time(NULL);
			struct tm tm = *localtime(&t);
			snprintf(file_name, 128, "/userdata/nv12_2560x1520_camera%d_%d%02d%02d%02d%02d%02d.yuv",
			         i, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
			         tm.tm_sec);
			LOG_INFO("file_name is %s\n", file_name);
			FILE *fp = fopen(file_name, "wb");
			fwrite(data[i], 1, stViFrame.u32Len, fp);
			fflush(fp);
			fclose(fp);
			if (g_format)
				ret = RK_MPI_VI_ReleaseChnFrame(i, RKISP_FBCPATH, &stViFrame);
			else
				ret = RK_MPI_VI_ReleaseChnFrame(i, RKISP_MAINPATH, &stViFrame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VI_ReleaseChnFrame fail %#x\n", ret);
			}
		}
	}

	return 0;
}

int rk_take_photo() {
	LOG_INFO("start\n");
	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = 1;
	RK_MPI_VENC_StartRecvFrame(JPEG_VENC_CHN, &stRecvParam);
	take_photo_one = 1;

	return 0;
}

int rk_video_init() {
	LOG_INFO("begin\n");
	int ret = 0;
	g_video_run_ = 1;
	g_sensor_num = rk_param_get_int("avs:sensor_num", 6);
	g_format = rk_param_get_int("avs:format", 0);
	g_enable_vo = rk_param_get_int("avs:enable_vo", 0);
	g_vo_dev_id = rk_param_get_int("avs:vo_dev_id", 3);
	LOG_INFO("g_sensor_num is %d, g_format is %d, g_enable_vo is %d, g_vo_dev_id is %d\n",
	         g_sensor_num, g_format, g_enable_vo, g_vo_dev_id);
	enable_jpeg = rk_param_get_int("avs:enable_jpeg", 1);
	enable_venc_0 = rk_param_get_int("avs:enable_venc_0", 1);
	enable_venc_1 = rk_param_get_int("avs:enable_venc_1", 1);
	enable_venc_2 = rk_param_get_int("avs:enable_venc_2", 1);
	LOG_INFO("enable_jpeg is %d, enable_venc_0 is %d, enable_venc_1 is %d, enable_venc_2 is %d\n",
	         enable_jpeg, enable_venc_0, enable_venc_1, enable_venc_2);

	ret = RK_MPI_SYS_Init();
	ret |= rkipc_vi_dev_init();
	ret |= rkipc_multi_vi_init();
	ret |= rkipc_avs_init();
	ret |= rkipc_vpss_init();
	if (enable_venc_0)
		ret |= rkipc_venc_0_init();
	if (enable_venc_1)
		ret |= rkipc_venc_1_init();
	if (enable_venc_2)
		ret |= rkipc_venc_2_init();
	if (enable_jpeg)
		ret |= rkipc_venc_3_init();
	if (g_enable_vo)
		ret |= rkipc_vo_init();
	ret |= rkipc_bind_init();
	ret |= rkipc_rtsp_init();
	ret |= rkipc_rtmp_init();
	//ret |= rkipc_osd_init();
#if 1
	pthread_t key_id;
	// pthread_t get_vi_id;
	pthread_create(&key_id, NULL, wait_key_event, NULL);
	// pthread_create(&get_vi_id, NULL, save_all_vi_frame, NULL);
#endif
	LOG_INFO("over\n");

	return ret;
}

int rk_video_deinit() {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	g_video_run_ = 0;
	//ret |= rkipc_osd_deinit();
	ret |= rkipc_bind_deinit();
	if (enable_venc_0) {
		pthread_join(venc_thread_0, NULL);
		ret |= rkipc_venc_0_deinit();
	}
	if (enable_venc_1) {
		pthread_join(venc_thread_1, NULL);
		ret |= rkipc_venc_1_deinit();
	}
	if (enable_venc_2) {
		pthread_join(venc_thread_2, NULL);
		ret |= rkipc_venc_2_deinit();
	}
	if (enable_jpeg) {
		pthread_join(jpeg_venc_thread_id, NULL);
		ret |= rkipc_venc_3_deinit();
	}
	ret |= rkipc_vpss_deinit();
	if (g_enable_vo)
		ret |= rkipc_vo_deinit();
	ret |= rkipc_avs_deinit();
	ret |= rkipc_multi_vi_deinit();
	ret |= rkipc_vi_dev_deinit();
	ret |= RK_MPI_SYS_Exit();
	ret |= rkipc_rtmp_deinit();
	ret |= rkipc_rtsp_deinit();

	return ret;
}

int rk_video_restart() {
	int ret;
	// ret = rk_video_deinit();
	// rk_isp_deinit(0);
	// rk_isp_deinit(1);
	sleep(1);
	// ret |= rk_video_init();
	// rk_isp_init(0);
	// rk_isp_init(1);

	return ret;
}
