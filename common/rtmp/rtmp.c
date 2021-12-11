// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "common.h"
#include "rkmuxer.h"
#include <sys/time.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "rtmp.c"

static VideoParam g_video_param;
static AudioParam g_audio_param;

int rk_rtmp_init(int id, const char *rtmp_url) {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	LOG_INFO("begin\n");
	// set g_video_param
	g_video_param.level = 52;
	g_video_param.width = rk_param_get_int("video.0:width", -1);
	g_video_param.height = rk_param_get_int("video.0:height", -1);
	g_video_param.bit_rate = rk_param_get_int("video.0:max_rate", -1);
	g_video_param.frame_rate_den = rk_param_get_int("video.0:dst_frame_rate_den", -1);
	g_video_param.frame_rate_num = rk_param_get_int("video.0:dst_frame_rate_num", -1);
	const char *output_data_type = rk_param_get_string("video.0:output_data_type", NULL);
	if (output_data_type)
		memcpy(g_video_param.codec, output_data_type, strlen(output_data_type));
	const char *h264_profile = rk_param_get_string("video.0:h264_profile", NULL);
	if (!strcmp(h264_profile, "high"))
		g_video_param.profile = 100;
	else if (!strcmp(h264_profile, "main"))
		g_video_param.profile = 77;
	else if (!strcmp(h264_profile, "baseline"))
		g_video_param.profile = 66;
	memcpy(g_video_param.format, "NV12", strlen("NV12"));
	// set g_audio_param
	g_audio_param.channels = rk_param_get_int("audio.0:channels", 2);
	g_audio_param.sample_rate = rk_param_get_int("audio.0:sample_rate", 16000);
	g_audio_param.frame_size = rk_param_get_int("audio.0:frame_size", 1024);
	// const char *format = rk_param_get_string("audio.0:format", NULL);
	// if (format)
	// 	memcpy(g_audio_param.format, format, strlen(format));
	// const char *codec = rk_param_get_string("audio.0:encode_type", NULL);
	// if (codec)
	// 	memcpy(g_audio_param.codec, codec, strlen(codec));
	rkmuxer_init(id + 3, "flv", rtmp_url, &g_video_param, &g_audio_param);

	return ret;
}

int rk_rtmp_deinit(int id) {
	LOG_INFO("begin\n");
	rkmuxer_deinit(id + 3);
	LOG_INFO("end\n");

	return 0;
}

int rk_rtmp_write_video_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                              int64_t present_time, int key_frame) {
	rkmuxer_write_video_frame(id + 3, buffer, buffer_size, present_time, key_frame);
	return 0;
}

int rk_rtmp_write_audio_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                              int64_t present_time) {
	rkmuxer_write_audio_frame(id + 3, buffer, buffer_size, present_time);
	return 0;
}
