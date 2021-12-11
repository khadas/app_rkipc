// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "video.h"
#include "common.h"
#include "event.h"
#include "isp.h"
#include "rkmuxer.h"
#include "rtsp_demo.h"
#include "xiecc_rtmp.h"

#include <rk_debug.h>
#include <rk_mpi_mb.h>
#include <rk_mpi_rgn.h>
#include <rk_mpi_sys.h>
#include <rk_mpi_venc.h>
#include <rk_mpi_vi.h>
#include <rk_mpi_vo.h>
#include <rk_mpi_vpss.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "video.c"

// mainpath 1920*1080 vi→venc→rtmp/rtsp
#define VIDEO_PIPE_0 0
// selfpath 720*1280 vi→vo
#define VIDEO_PIPE_1 1

#define RV1126_VO_DEV_MIPI 0
#define RV1126_VOP_LAYER_CLUSTER0 0

#define RTSP_URL_0 "/live/0"
#define RTSP_URL_1 "/live/1"
#define RTMP_URL "rtmp://127.0.0.1:1935/live/substream"

static int g_video_run_ = 1;
static int pipe_id_ = 0;
static int dev_id_ = 0;
static int g_rtmp_start = 0;
static rtsp_demo_handle g_rtsplive = NULL;
static rtsp_session_handle g_rtsp_session_0;
static rtsp_session_handle g_rtsp_session_1;
static const char *g_output_data_type;
static const char *g_rc_mode;
static const char *g_h264_profile;
static const char *g_smart;
static pthread_t venc_thread_id;
static void *g_rtmp_handle;

MPP_CHN_S pipe_0_vi_chn;
MPP_CHN_S pipe_0_venc_chn;

MPP_CHN_S pipe_1_vi_chn;
MPP_CHN_S pipe_1_venc_chn;
MPP_CHN_S pipe_1_vo_chn;

static VO_DEV VoLayer = RV1126_VOP_LAYER_CLUSTER0;

static void *test_get_vi(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VI_FRAME_S vi_frame;
	VI_CHN_STATUS_S stChnStatus;
	VIDEO_FRAME_INFO_S venc_nv12_frame;
	int loopCount = 0;
	int ret = 0;

	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VI_GetChnFrame(pipe_id_, VIDEO_PIPE_0, &vi_frame, -1);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(vi_frame.pMbBlk);
			// LOG_DEBUG("RK_MPI_VI_GetChnFrame ok:data %p loop:%d seq:%d pts:%lld ms\n", data,
			//           loopCount, vi_frame.s32Seq, vi_frame.s64PTS / 1000);
			// // 6.get the channel status
			// ret = RK_MPI_VI_QueryChnStatus(pipe_id_, VIDEO_PIPE_0, &stChnStatus);
			// LOG_DEBUG("RK_MPI_VI_QueryChnStatus ret %x, "
			//           "w:%d,h:%d,enable:%d,lost:%d,framerate:%d,vbfail:%d\n",
			//           ret, stChnStatus.stSize.u32Width, stChnStatus.stSize.u32Height,
			//           stChnStatus.bEnable, stChnStatus.u32LostFrame, stChnStatus.u32FrameRate,
			//           stChnStatus.u32VbFail);

			memset(&venc_nv12_frame, 0, sizeof(venc_nv12_frame));
			venc_nv12_frame.stVFrame.pMbBlk = vi_frame.pMbBlk;
			venc_nv12_frame.stVFrame.u32Width = rk_param_get_int("video.0:width", 0);
			venc_nv12_frame.stVFrame.u32Height = rk_param_get_int("video.0:height", 0);
			venc_nv12_frame.stVFrame.u32VirWidth = rk_param_get_int("video.0:width", 0);
			venc_nv12_frame.stVFrame.u32VirHeight = rk_param_get_int("video.0:height", 0);
			venc_nv12_frame.stVFrame.enPixelFormat = vi_frame.enPixelFormat;
			venc_nv12_frame.stVFrame.u64PTS = vi_frame.s64PTS;
			ret = RK_MPI_VENC_SendFrame(0, &venc_nv12_frame, -1);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VENC_SendFrame fail %x\n", ret);
			}
			// 7.release the frame
			ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, VIDEO_PIPE_0, &vi_frame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VI_ReleaseChnFrame fail %x\n", ret);
			}
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VI_GetChnFrame timeout %x\n", ret);
		}
	}

	return 0;
}

static void *rkipc_get_venc_0(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	// FILE *fp = fopen("/tmp/venc.h264", "wb");
	stFrame.pstPack = malloc(sizeof(VENC_PACK_S));

	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(VIDEO_PIPE_0, &stFrame, -1);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			// fwrite(data, 1, stFrame.pstPack->u32Len, fp);
			// fflush(fp);
			// LOG_INFO("Count:%d, Len:%d, PTS is %lld, enH264EType is %d\n",
			// loopCount,
			// stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			// stFrame.pstPack->DataType.enH264EType);

			if (g_rtsplive && g_rtsp_session_0) {
				rtsp_tx_video(g_rtsp_session_0, data, stFrame.pstPack->u32Len,
				              stFrame.pstPack->u64PTS);
				rtsp_do_event(g_rtsplive);
			}
			if (g_rtmp_handle && g_rtmp_start) {
				rtmp_sender_write_video_frame(g_rtmp_handle, data, stFrame.pstPack->u32Len,
				                              stFrame.pstPack->u64PTS, 0, 0);
			}
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE))
				rkmuxer_write_video_frame(data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          1);
			else
				rkmuxer_write_video_frame(data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          0);

			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(VIDEO_PIPE_0, &stFrame);
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
	// if (fp)
	//  fclose(fp);

	return 0;
}

int rkipc_rtsp_init() {
	LOG_INFO("start\n");
	g_rtsplive = create_rtsp_demo(554);
	g_rtsp_session_0 = rtsp_new_session(g_rtsplive, RTSP_URL_0);
	if (!strcmp(g_output_data_type, "H.264")) {
		rtsp_set_video(g_rtsp_session_0, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
	} else if (!strcmp(g_output_data_type, "H.265")) {
		rtsp_set_video(g_rtsp_session_0, RTSP_CODEC_ID_VIDEO_H265, NULL, 0);
	} else {
		LOG_ERROR("g_output_data_type is %s, not support\n", g_output_data_type);
		return -1;
	}
	rtsp_sync_video_ts(g_rtsp_session_0, rtsp_get_reltime(), rtsp_get_ntptime());
	LOG_INFO("end\n");

	return 0;
}

int rkipc_rtsp_deinit() {
	LOG_INFO("%s\n", __func__);
	if (g_rtsplive)
		rtsp_del_demo(g_rtsplive);
	LOG_INFO("%s over\n", __func__);

	return 0;
}

int rkipc_rtmp_init() {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	g_rtmp_handle = rtmp_sender_alloc(RTMP_URL);
	LOG_INFO("g_rtmp_handle is %p\n", g_rtmp_handle);
	ret = rtmp_sender_start_publish(g_rtmp_handle, 0, 0);
	if (ret) {
		if (g_rtmp_handle)
			rtmp_sender_free(g_rtmp_handle);
		g_rtmp_handle = NULL;
		LOG_ERROR("rtmp_sender_start_publish fail\n");
	} else {
		g_rtmp_start = 1;
	}

	return ret;
}

int rkipc_rtmp_deinit() {
	LOG_INFO("%s\n", __func__);
	if (g_rtmp_handle) {
		rtmp_sender_stop_publish(g_rtmp_handle);
		rtmp_sender_free(g_rtmp_handle);
	}

	return 0;
}

int rkipc_vi_dev_init() {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	VI_DEV_ATTR_S stDevAttr;
	VI_DEV_BIND_PIPE_S stBindPipe;
	memset(&stDevAttr, 0, sizeof(stDevAttr));
	memset(&stBindPipe, 0, sizeof(stBindPipe));
	// 0. get dev config status
	ret = RK_MPI_VI_GetDevAttr(dev_id_, &stDevAttr);
	if (ret == RK_ERR_VI_NOT_CONFIG) {
		// 0-1.config dev
		ret = RK_MPI_VI_SetDevAttr(dev_id_, &stDevAttr);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("RK_MPI_VI_SetDevAttr %x\n", ret);
			return -1;
		}
	} else {
		LOG_ERROR("RK_MPI_VI_SetDevAttr already\n");
	}
	// 1.get dev enable status
	ret = RK_MPI_VI_GetDevIsEnable(dev_id_);
	if (ret != RK_SUCCESS) {
		// 1-2.enable dev
		ret = RK_MPI_VI_EnableDev(dev_id_);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("RK_MPI_VI_EnableDev %x\n", ret);
			return -1;
		}
		// 1-3.bind dev/pipe
		stBindPipe.u32Num = pipe_id_;
		stBindPipe.PipeId[0] = pipe_id_;
		ret = RK_MPI_VI_SetDevBindPipe(dev_id_, &stBindPipe);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("RK_MPI_VI_SetDevBindPipe %x\n", ret);
			return -1;
		}
	} else {
		LOG_ERROR("RK_MPI_VI_EnableDev already\n");
	}

	return 0;
}

int rkipc_vi_dev_deinit() {
	RK_MPI_VI_DisableDev(pipe_id_);

	return 0;
}

int rkipc_pipe_0_init() {
	int video_width = rk_param_get_int("video.0:width", -1);
	int video_height = rk_param_get_int("video.0:height", -1);
	const char *video_device_name = rk_param_get_string("video.0:src_node", "rkispp_scale0");
	int buf_cnt = 2;
	int ret = 0;

	// VI init
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	LOG_INFO("video_device_name = %s\n", video_device_name);
	memcpy(vi_chn_attr.stIspOpt.aEntityName, video_device_name, strlen(video_device_name));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
	vi_chn_attr.stSize.u32Width = video_width;
	vi_chn_attr.stSize.u32Height = video_height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.u32Depth = 1; // not bind, can get buffer
	ret = RK_MPI_VI_SetChnAttr(pipe_id_, VIDEO_PIPE_0, &vi_chn_attr);
	ret |= RK_MPI_VI_EnableChn(pipe_id_, VIDEO_PIPE_0);
	if (ret) {
		LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
		return -1;
	}
	pthread_t thread_id;
	pthread_create(&thread_id, NULL, test_get_vi, NULL);
	// VENC init
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	g_output_data_type = rk_param_get_string("video.0:output_data_type", NULL);
	g_rc_mode = rk_param_get_string("video.0:rc_mode", NULL);
	g_h264_profile = rk_param_get_string("video.0:h264_profile", NULL);
	if ((g_output_data_type == NULL) || (g_rc_mode == NULL)) {
		LOG_ERROR("g_output_data_type or g_rc_mode is NULL\n");
		return -1;
	}
	LOG_INFO("g_output_data_type is %s, g_rc_mode is %s, g_h264_profile is %s\n",
	         g_output_data_type, g_rc_mode, g_h264_profile);
	if (!strcmp(g_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;

		if (!strcmp(g_h264_profile, "high"))
			venc_chn_attr.stVencAttr.u32Profile = 100;
		else if (!strcmp(g_h264_profile, "main"))
			venc_chn_attr.stVencAttr.u32Profile = 77;
		else if (!strcmp(g_h264_profile, "baseline"))
			venc_chn_attr.stVencAttr.u32Profile = 66;
		else
			LOG_ERROR("g_h264_profile is %s\n", g_h264_profile);

		if (!strcmp(g_rc_mode, "CBR")) {
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
	} else if (!strcmp(g_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(g_rc_mode, "CBR")) {
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
		LOG_ERROR("g_output_data_type is %s, not support\n", g_output_data_type);
		return -1;
	}
	g_smart = rk_param_get_string("video.0:smart", NULL);
	if (!strcmp(g_rc_mode, "open")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
	} else {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
	}
	// venc_chn_attr.stGopAttr.u32GopSize = rk_param_get_int("video.0:gop", -1);

	venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	venc_chn_attr.stVencAttr.u32PicWidth = video_width;
	venc_chn_attr.stVencAttr.u32PicHeight = video_height;
	venc_chn_attr.stVencAttr.u32VirWidth = video_width;
	venc_chn_attr.stVencAttr.u32VirHeight = video_height;
	venc_chn_attr.stVencAttr.u32StreamBufCnt = buf_cnt;
	venc_chn_attr.stVencAttr.u32BufSize = video_width * video_height * 3 / 2;
	// venc_chn_attr.stVencAttr.u32Depth = 1;
	ret = RK_MPI_VENC_CreateChn(VIDEO_PIPE_0, &venc_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%d\n", ret);
		return -1;
	}
	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	RK_MPI_VENC_StartRecvFrame(VIDEO_PIPE_0, &stRecvParam);

	pthread_create(&venc_thread_id, NULL, rkipc_get_venc_0, NULL);

	// pipe_0_venc_chn.enModId = RK_ID_VENC;
	// pipe_0_venc_chn.s32DevId = pipe_id_;
	// pipe_0_venc_chn.s32ChnId = VIDEO_PIPE_0;

	// pipe_0_vi_chn.enModId = RK_ID_VI;
	// pipe_0_vi_chn.s32DevId = pipe_id_;
	// pipe_0_vi_chn.s32ChnId = VIDEO_PIPE_0;
	// ret = RK_MPI_SYS_Bind(&pipe_0_vi_chn, &pipe_0_venc_chn);
	// if (ret) {
	// 	LOG_ERROR("ERROR: Bind VI and VENC error! ret=%d\n", ret);
	// 	return -1;
	// }

	return 0;
}

int rkipc_pipe_0_deinit() {
	int ret = 0;
	// unbind first
	ret = RK_MPI_SYS_UnBind(&pipe_0_vi_chn, &pipe_0_venc_chn);
	if (ret) {
		LOG_ERROR("ERROR: UnBind VI and VENC error! ret=%d\n", ret);
		return -1;
	}
	LOG_INFO("UnBind VI and VENC success\n");
	// destroy venc before vi
	ret = RK_MPI_VENC_StopRecvFrame(VIDEO_PIPE_0);
	ret |= RK_MPI_VENC_DestroyChn(VIDEO_PIPE_0);
	if (ret) {
		LOG_ERROR("ERROR: Destroy VENC error! ret=%d\n", ret);
		return -1;
	}
	LOG_INFO("Destroy VENC success\n");
	// destroy vi
	ret = RK_MPI_VI_DisableChn(pipe_id_, VIDEO_PIPE_0);
	if (ret) {
		LOG_ERROR("ERROR: Destroy VI error! ret=%d\n", ret);
		return -1;
	}
	LOG_INFO("Destroy VI success\n");

	return 0;
}

int rkipc_pipe_1_init() {
	int video_width = 1280;
	int video_height = 720;
	const char *video_device_name = "rkispp_scale0";
	int buf_cnt = 2;
	int ret = 0;

	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	// VI init
	// 2.config channel
	LOG_INFO("video_device_name = %s\n", video_device_name);
	memcpy(vi_chn_attr.stIspOpt.aEntityName, video_device_name, strlen(video_device_name));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
	vi_chn_attr.stSize.u32Width = video_width;
	vi_chn_attr.stSize.u32Height = video_height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	ret = RK_MPI_VI_SetChnAttr(pipe_id_, VIDEO_PIPE_1, &vi_chn_attr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VI_SetChnAttr %x\n", ret);
		return -1;
	}
	LOG_INFO("RK_MPI_VI_SetChnAttr success\n");
	// 3.enable channel
	ret = RK_MPI_VI_EnableChn(pipe_id_, VIDEO_PIPE_1);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VI_EnableChn %x\n", ret);
		return -1;
	}
	LOG_INFO("RK_MPI_VI_EnableChn success\n");

	// pthread_t thread_id;
	// pthread_create(&thread_id, NULL, test_get_vi, NULL);

	VO_PUB_ATTR_S VoPubAttr;
	VO_VIDEO_LAYER_ATTR_S stLayerAttr;
	VO_CSC_S VideoCSC;
	VO_CHN_ATTR_S VoChnAttr;
	RK_U32 u32DispBufLen;
	VO_PUB_ATTR_S pstA;

	memset(&VoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
	memset(&VoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
	memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
	memset(&VideoCSC, 0, sizeof(VO_CSC_S));
	memset(&VoChnAttr, 0, sizeof(VoChnAttr));

	VoPubAttr.enIntfType = VO_INTF_MIPI;
	VoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;
	ret = RK_MPI_VO_SetPubAttr(RV1126_VO_DEV_MIPI, &VoPubAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetPubAttr %x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_SetPubAttr success\n");

	ret = RK_MPI_VO_Enable(RV1126_VO_DEV_MIPI);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_Enable %x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_Enable success\n");

	ret = RK_MPI_VO_GetLayerDispBufLen(VoLayer, &u32DispBufLen);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("Get display buf len failed with error code %d!\n", ret);
		return ret;
	}
	LOG_INFO("Get VoLayer %d disp buf len is %d.\n", VoLayer, u32DispBufLen);
	u32DispBufLen = 15;
	ret = RK_MPI_VO_SetLayerDispBufLen(VoLayer, u32DispBufLen);
	if (ret != RK_SUCCESS) {
		return ret;
	}
	LOG_INFO("Agin Get VoLayer %d disp buf len is %d.\n", VoLayer, u32DispBufLen);

	/* get vo attribute*/
	ret = RK_MPI_VO_GetPubAttr(RV1126_VO_DEV_MIPI, &pstA);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_GetPubAttr fail!\n");
		return ret;
	}
	LOG_INFO("RK_MPI_VO_GetPubAttr success\n");

	stLayerAttr.stDispRect.s32X = 0;
	stLayerAttr.stDispRect.s32Y = 0;
	stLayerAttr.stDispRect.u32Width = pstA.stSyncInfo.u16Hact;
	stLayerAttr.stDispRect.u32Height = pstA.stSyncInfo.u16Vact;
	stLayerAttr.stImageSize.u32Width = pstA.stSyncInfo.u16Hact;
	stLayerAttr.stImageSize.u32Height = pstA.stSyncInfo.u16Vact;
	LOG_INFO("stLayerAttr W=%d, H=%d\n", stLayerAttr.stDispRect.u32Width,
	         stLayerAttr.stDispRect.u32Height);

	// stLayerAttr.u32DispFrmRt = 30;
	stLayerAttr.enPixFormat = RK_FMT_YUV420SP;
	// stLayerAttr.bDoubleFrame = RK_TRUE;
	// VideoCSC.enCscMatrix = VO_CSC_MATRIX_IDENTITY;
	// VideoCSC.u32Contrast = 50;
	// VideoCSC.u32Hue = 50;
	// VideoCSC.u32Luma = 50;
	// VideoCSC.u32Satuature = 50;
	RK_S32 u32VoChn = 0;

	/*bind layer0 to device hd0*/
	ret = RK_MPI_VO_BindLayer(VoLayer, RV1126_VO_DEV_MIPI, VO_LAYER_MODE_GRAPHIC);
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

	ret = RK_MPI_VO_EnableLayer(VoLayer);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_EnableLayer VoLayer = %d error\n", VoLayer);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_EnableLayer success\n");

	// ret = RK_MPI_VO_SetLayerCSC(VoLayer, &VideoCSC);
	// if (ret != RK_SUCCESS) {
	// 	LOG_ERROR("RK_MPI_VO_SetLayerCSC error\n");
	// 	return ret;
	// }
	// LOG_INFO("RK_MPI_VO_SetLayerCSC success\n");

	ret = RK_MPI_VO_EnableChn(RV1126_VOP_LAYER_CLUSTER0, u32VoChn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("create RV1126_VOP_LAYER_CLUSTER0 layer %d ch vo failed!\n", u32VoChn);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_EnableChn success\n");

	VoChnAttr.bDeflicker = RK_FALSE;
	VoChnAttr.u32Priority = 1;
	VoChnAttr.stRect.s32X = 0;
	VoChnAttr.stRect.s32Y = 0;
	VoChnAttr.stRect.u32Width = stLayerAttr.stDispRect.u32Width;
	VoChnAttr.stRect.u32Height = stLayerAttr.stDispRect.u32Height;

	ret = RK_MPI_VO_SetChnAttr(VoLayer, 0, &VoChnAttr);

	// RK_MPI_VO_SetChnFrameRate(VoLayer, 0, 30);

	pipe_1_vi_chn.enModId = RK_ID_VI;
	pipe_1_vi_chn.s32DevId = 0;
	pipe_1_vi_chn.s32ChnId = VIDEO_PIPE_1;
	pipe_1_vo_chn.enModId = RK_ID_VO;
	pipe_1_vo_chn.s32DevId = RV1126_VOP_LAYER_CLUSTER0;
	pipe_1_vo_chn.s32ChnId = 0;
	ret = RK_MPI_SYS_Bind(&pipe_1_vi_chn, &pipe_1_vo_chn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("vi and vo bind error!\n");
		return ret;
	}
	LOG_INFO("vi and vo bind success!\n");

	return 0;
}

int rkipc_pipe_1_deinit() {
	int ret;
	ret = RK_MPI_SYS_UnBind(&pipe_1_vi_chn, &pipe_1_vo_chn);
	if (ret) {
		LOG_ERROR("UnBind vi to vo failed! ret=%d\n", ret);
		return -1;
	}

	// disable vo layer
	ret = RK_MPI_VO_DisableLayer(VoLayer);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_DisableLayer failed\n");
		return -1;
	}
	// disable vo dev
	ret = RK_MPI_VO_Disable(RV1126_VO_DEV_MIPI);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_Disable failed\n");
		return -1;
	}
	RK_MPI_VI_DisableChn(pipe_id_, VIDEO_PIPE_1);

	return 0;
}

int rk_video_get_gop(int stream_id, int *value) {
	*value = rk_param_get_int("video.0:gop", -1);

	return 0;
}

int rk_video_set_gop(int stream_id, int value) {
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	if (!strcmp(g_output_data_type, "H.264")) {
		if (!strcmp(g_rc_mode, "CBR"))
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = value;
		else
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = value;
	} else if (!strcmp(g_output_data_type, "H.265")) {
		if (!strcmp(g_rc_mode, "CBR"))
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = value;
		else
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = value;
	} else {
		LOG_ERROR("g_output_data_type is %s, not support\n", g_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
	rk_param_set_int("video.0:gop", value);

	return 0;
}

int rk_video_get_max_rate(int stream_id, int *value) {
	*value = rk_param_get_int("video.0:max_rate", -1);

	return 0;
}

int rk_video_set_max_rate(int stream_id, int value) {
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	if (!strcmp(g_output_data_type, "H.264")) {
		if (!strcmp(g_rc_mode, "CBR"))
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = value;
		else
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = value;
	} else if (!strcmp(g_output_data_type, "H.265")) {
		if (!strcmp(g_rc_mode, "CBR"))
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = value;
		else
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = value;
	} else {
		LOG_ERROR("g_output_data_type is %s, not support\n", g_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
	rk_param_set_int("video.0:max_rate", value);

	return 0;
}

int rk_video_get_RC_mode(int stream_id, const char **value) {
	*value = rk_param_get_string("video.0:rc_mode", NULL);

	return 0;
}

int rk_video_set_RC_mode(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	if (!strcmp(g_output_data_type, "H.264")) {
		if (!strcmp(value, "CBR")) {
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
	} else if (!strcmp(g_output_data_type, "H.265")) {
		if (!strcmp(value, "CBR")) {
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
		LOG_ERROR("g_output_data_type is %s, not support\n", g_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);

	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
	rk_param_set_string(entry, value);
	g_rc_mode = rk_param_get_string(entry, NULL); // update global variables
	LOG_INFO("g_rc_mode is %s\n", g_rc_mode);

	return 0;
}

int rk_video_get_output_data_type(int stream_id, const char **value) {
	*value = rk_param_get_string("video.0:output_data_type", NULL);

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
	*value = rk_param_get_string("video.0:rc_quality", NULL);

	return 0;
}

int rk_video_set_rc_quality(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:rc_quality", stream_id);
	rk_param_set_string(entry, value);

	return 0;
}

int rk_video_get_smart(int stream_id, const char **value) {
	*value = rk_param_get_string("video.0:smart", NULL);

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
	*value = rk_param_get_string("video.0:stream_type", NULL);

	return 0;
}

int rk_video_set_stream_type(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:stream_type", stream_id);
	rk_param_set_string(entry, value);

	return 0;
}

int rk_video_get_h264_profile(int stream_id, const char **value) {
	*value = rk_param_get_string("video.0:h264_profile", NULL);

	return 0;
}

int rk_video_set_h264_profile(int stream_id, const char *value) {
	char entry[128] = {'\0'};

	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	if (!strcmp(g_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;

		if (!strcmp(value, "high"))
			venc_chn_attr.stVencAttr.u32Profile = 100;
		else if (!strcmp(value, "main"))
			venc_chn_attr.stVencAttr.u32Profile = 77;
		else if (!strcmp(value, "baseline"))
			venc_chn_attr.stVencAttr.u32Profile = 66;
		else
			LOG_ERROR("value is %s\n", value);
		RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
	} else {
		LOG_INFO("g_output_data_type not H.264\n");
	}
	snprintf(entry, 127, "video.%d:h264_profile", stream_id);
	rk_param_set_string(entry, value);
	g_h264_profile = rk_param_get_string("video.0:h264_profile", NULL); // update global variables
	LOG_INFO("g_h264_profile is %s\n", g_h264_profile);

	return 0;
}

int rk_video_get_resolution(int stream_id, char **value) {
	int width = rk_param_get_int("video.0:width", -1);
	int height = rk_param_get_int("video.0:height", -1);
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
	int den = rk_param_get_int("video.0:dst_frame_rate_den", -1);
	int num = rk_param_get_int("video.0:dst_frame_rate_num", -1);
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
	if (!strcmp(g_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
		if (!strcmp(g_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = rk_param_get_int("video.0:max_rate", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = num;
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = rk_param_get_int("video.0:max_rate", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = num;
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		}
	} else if (!strcmp(g_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(g_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = rk_param_get_int("video.0:max_rate", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = num;
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = rk_param_get_int("video.0:max_rate", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = num;
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		}
	} else {
		LOG_ERROR("g_output_data_type is %s, not support\n", g_output_data_type);
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
	int den = rk_param_get_int("video.0:src_frame_rate_den", -1);
	int num = rk_param_get_int("video.0:src_frame_rate_num", -1);
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

int rk_video_init() {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	g_video_run_ = 1;
	ret = RK_MPI_SYS_Init();
	ret |= rkipc_vi_dev_init();
	ret |= rkipc_pipe_0_init(); // for vi-venc-rtsp
	// ret |= rkipc_pipe_1_init(); // for vi-vo
	ret |= rkipc_rtsp_init();
	ret |= rkipc_rtmp_init();

	return ret;
}

int rk_video_deinit() {
	LOG_INFO("%s\n", __func__);
	g_video_run_ = 0;
	pthread_join(venc_thread_id, NULL);
	int ret = 0;
	ret |= rkipc_rtmp_deinit();
	ret |= rkipc_rtsp_deinit();
	ret |= rkipc_pipe_0_deinit();
	// ret |= rkipc_pipe_1_deinit();
	ret |= rkipc_vi_dev_deinit();
	ret |= RK_MPI_SYS_Exit();

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
