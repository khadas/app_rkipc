// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "common.h"
#include "rkmuxer.h"
#include <sys/time.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "storage.c"

char file_name[128] = {0};
const char *file_path;
const char *file_format;
int file_duration;
static int g_record_run_ = 0;
static void *g_storage_signal;
static pthread_t record_thread_id;
// 起个线程，60秒deinit再init
static VideoParam g_video_param;
static AudioParam g_audio_param;

static void *rk_storage_record(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);

	while (g_record_run_) {
		time_t t = time(NULL);
		struct tm tm = *localtime(&t);
		snprintf(file_name, 128, "%s/%d%02d%02d%02d%02d%02d.%s", file_path, tm.tm_year + 1900,
		         tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, file_format);
		LOG_INFO("file_name is %s\n", file_name);
		rkmuxer_deinit(0);
		rkmuxer_init(0, NULL, file_name, &g_video_param, &g_audio_param);
		rk_signal_wait(g_storage_signal, file_duration * 1000);
	}
	rkmuxer_deinit(0);

	return 0;
}

// TODO, need record plan
int rk_storage_init() {
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
	const char *format = rk_param_get_string("audio.0:format", NULL);
	if (format)
		memcpy(g_audio_param.format, format, strlen(format));
	const char *codec = rk_param_get_string("audio.0:encode_type", NULL);
	if (codec)
		memcpy(g_audio_param.codec, codec, strlen(codec));

	file_path = rk_param_get_string("storage:file_path", "/userdata");
	file_format = rk_param_get_string("storage:file_format", "mp4");
	file_duration = rk_param_get_int("storage:file_duration", 60);

	if (rk_param_get_int("storage:enable", 0) == 0) {
		LOG_INFO("storage:enable is 0\n");
		return 0;
	}

	if (g_storage_signal)
		rk_signal_destroy(g_storage_signal);
	g_storage_signal = rk_signal_create(0, 1);
	if (!g_storage_signal) {
		LOG_ERROR("create signal fail\n");
		return -1;
	}
	g_record_run_ = 1;
	pthread_create(&record_thread_id, NULL, rk_storage_record, NULL);
	LOG_INFO("end\n");

	return 0;
}

int rk_storage_deinit() {
	LOG_INFO("begin\n");
	if (rk_param_get_int("storage:enable", 0) == 0) {
		LOG_INFO("storage:enable is 0\n");
		return 0;
	}
	g_record_run_ = 0;
	if (g_storage_signal) {
		rk_signal_give(g_storage_signal);
		pthread_join(record_thread_id, NULL);
		rk_signal_destroy(g_storage_signal);
		g_storage_signal = NULL;
	}
	LOG_INFO("end\n");

	return 0;
}

int rk_storage_write_video_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                                 int64_t present_time, int key_frame) {
	if (g_record_run_)
		rkmuxer_write_video_frame(id, buffer, buffer_size, present_time, key_frame);
	return 0;
}

int rk_storage_write_audio_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                                 int64_t present_time) {
	if (g_record_run_)
		rkmuxer_write_audio_frame(id, buffer, buffer_size, present_time);
	return 0;
}

int rk_storage_record_start() {
	LOG_INFO("start\n");
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	snprintf(file_name, 128, "%s/%d%02d%02d%02d%02d%02d.%s", file_path, tm.tm_year + 1900,
	         tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, file_format);
	LOG_INFO("file_name is %s\n", file_name);
	rkmuxer_deinit(0);
	rkmuxer_init(0, NULL, file_name, &g_video_param, &g_audio_param);
	g_record_run_ = 1;
	LOG_INFO("end\n");
}

int rk_storage_record_stop() {
	LOG_INFO("start\n");
	rkmuxer_deinit(0);
	g_record_run_ = 0;
	LOG_INFO("end\n");
}

int rk_stoarge_record_statue_get(int *value) {
	*value = g_record_run_;
	return 0;
}