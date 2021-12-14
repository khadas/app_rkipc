// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "common.h"
#include "rkmuxer.h"
#include <sys/time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "storage.c"

#define STORAGE_NUM 3

typedef struct rk_storage_struct_ {
	char file_name[128];
	const char *file_path;
	const char *file_format;
	int file_duration;
	int g_record_run_;
	void *g_storage_signal;
	pthread_t record_thread_id;
	VideoParam g_video_param;
	AudioParam g_audio_param;
} rk_storage_struct;

static rk_storage_struct rk_storage_group[STORAGE_NUM];

static void *rk_storage_record(void *arg) {
	int *id_ptr = arg;
	int id = *id_ptr;
	printf("#Start %s thread, arg:%p\n", __func__, arg);

	while (rk_storage_group[id].g_record_run_) {
		time_t t = time(NULL);
		struct tm tm = *localtime(&t);
		snprintf(rk_storage_group[id].file_name, 128, "%s/%d%02d%02d%02d%02d%02d.%s", rk_storage_group[id].file_path, tm.tm_year + 1900,
		         tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, rk_storage_group[id].file_format);
		LOG_INFO("[%d], file_name is %s\n", id, rk_storage_group[id].file_name);
		rkmuxer_deinit(id);
		rkmuxer_init(id, NULL, rk_storage_group[id].file_name, &rk_storage_group[id].g_video_param, &rk_storage_group[id].g_audio_param);
		rk_signal_wait(rk_storage_group[id].g_storage_signal, rk_storage_group[id].file_duration * 1000);
	}
	rkmuxer_deinit(id);

	return NULL;
}

int rk_storage_init_by_id(int id) {
	LOG_INFO("begin\n");
	char entry[128] = {'\0'};

	// set rk_storage_group[id].g_video_param
	rk_storage_group[id].g_video_param.level = 52;
	snprintf(entry, 127, "video.%d:width", id);
	rk_storage_group[id].g_video_param.width = rk_param_get_int(entry, -1);
	snprintf(entry, 127, "video.%d:height", id);
	rk_storage_group[id].g_video_param.height = rk_param_get_int(entry, -1);
	snprintf(entry, 127, "video.%d:max_rate", id);
	rk_storage_group[id].g_video_param.bit_rate = rk_param_get_int(entry, -1);
	snprintf(entry, 127, "video.%d:dst_frame_rate_den", id);
	rk_storage_group[id].g_video_param.frame_rate_den = rk_param_get_int(entry, -1);
	snprintf(entry, 127, "video.%d:dst_frame_rate_num", id);
	rk_storage_group[id].g_video_param.frame_rate_num = rk_param_get_int(entry, -1);
	snprintf(entry, 127, "video.%d:output_data_type", id);
	const char *output_data_type = rk_param_get_string(entry, NULL);
	if (output_data_type)
		memcpy(rk_storage_group[id].g_video_param.codec, output_data_type, strlen(output_data_type));
	snprintf(entry, 127, "video.%d:h264_profile", id);
	const char *h264_profile = rk_param_get_string(entry, NULL);
	if (!strcmp(h264_profile, "high"))
		rk_storage_group[id].g_video_param.profile = 100;
	else if (!strcmp(h264_profile, "main"))
		rk_storage_group[id].g_video_param.profile = 77;
	else if (!strcmp(h264_profile, "baseline"))
		rk_storage_group[id].g_video_param.profile = 66;
	memcpy(rk_storage_group[id].g_video_param.format, "NV12", strlen("NV12"));
	// set g_audio_param
	rk_storage_group[id].g_audio_param.channels = rk_param_get_int("audio.0:channels", 2);
	rk_storage_group[id].g_audio_param.sample_rate = rk_param_get_int("audio.0:sample_rate", 16000);
	rk_storage_group[id].g_audio_param.frame_size = rk_param_get_int("audio.0:frame_size", 1024);
	const char *format = rk_param_get_string("audio.0:format", NULL);
	if (format)
		memcpy(rk_storage_group[id].g_audio_param.format, format, strlen(format));
	const char *codec = rk_param_get_string("audio.0:encode_type", NULL);
	if (codec)
		memcpy(rk_storage_group[id].g_audio_param.codec, codec, strlen(codec));

	snprintf(entry, 127, "storage.%d:file_path", id);
	rk_storage_group[id].file_path = rk_param_get_string(entry, "/userdata");
	// create file_path if no exit
	DIR *d = opendir(rk_storage_group[id].file_path);
	if(d == NULL) {
		if(mkdir(rk_storage_group[id].file_path, 0777) == -1){
			LOG_ERROR("Create %s fail\n", rk_storage_group[id].file_path);
			return -1;
		}
	}

	snprintf(entry, 127, "storage.%d:file_format", id);
	rk_storage_group[id].file_format = rk_param_get_string(entry, "mp4");
	snprintf(entry, 127, "storage.%d:file_duration", id);
	rk_storage_group[id].file_duration = rk_param_get_int(entry, 60);

	snprintf(entry, 127, "storage.%d:enable", id);
	if (rk_param_get_int(entry, 0) == 0) {
		LOG_INFO("storage[%d]:enable is 0\n", id);
		return 0;
	}

	if (rk_storage_group[id].g_storage_signal)
		rk_signal_destroy(rk_storage_group[id].g_storage_signal);
	rk_storage_group[id].g_storage_signal = rk_signal_create(0, 1);
	if (!rk_storage_group[id].g_storage_signal) {
		LOG_ERROR("create signal fail\n");
		return -1;
	}
	rk_storage_group[id].g_record_run_ = 1;
	pthread_create(&rk_storage_group[id].record_thread_id, NULL, rk_storage_record, (void *)&id);
	LOG_INFO("end\n");

	return 0;
}

int rk_storage_deinit_by_id(int id) {
	LOG_INFO("begin\n");
	char entry[128] = {'\0'};
	for (int id =0; id<STORAGE_NUM; id++) {
		snprintf(entry, 127, "storage.%d:enable", id);
		if (rk_param_get_int(entry, 0) == 0) {
			LOG_INFO("storage[%d]:enable is 0\n", id);
			return 0;
		}
		rk_storage_group[id].g_record_run_ = 0;
		if (rk_storage_group[id].g_storage_signal) {
			rk_signal_give(rk_storage_group[id].g_storage_signal);
			pthread_join(rk_storage_group[id].record_thread_id, NULL);
			rk_signal_destroy(rk_storage_group[id].g_storage_signal);
			rk_storage_group[id].g_storage_signal = NULL;
		}
	}
	LOG_INFO("end\n");

	return 0;
}

// TODO, need record plan
int rk_storage_init() {
	for (int i=0;i<STORAGE_NUM;i++) {
		rk_storage_init_by_id(i);
	}
	return 0;
}

int rk_storage_deinit() {
	for (int i=0;i<STORAGE_NUM;i++) {
		rk_storage_deinit_by_id(i);
	}
	return 0;
}

int rk_storage_write_video_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                                 int64_t present_time, int key_frame) {
	if (rk_storage_group[id].g_record_run_)
		rkmuxer_write_video_frame(id, buffer, buffer_size, present_time, key_frame);
	return 0;
}

int rk_storage_write_audio_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                                 int64_t present_time) {
	if (rk_storage_group[id].g_record_run_)
		rkmuxer_write_audio_frame(id, buffer, buffer_size, present_time);
	return 0;
}

int rk_storage_record_start() {
	// 主码流
	LOG_INFO("start\n");
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	snprintf(rk_storage_group[0].file_name, 128, "%s/%d%02d%02d%02d%02d%02d.%s", rk_storage_group[0].file_path, tm.tm_year + 1900,
	         tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, rk_storage_group[0].file_format);
	LOG_INFO("file_name is %s\n", rk_storage_group[0].file_name);
	rkmuxer_deinit(0);
	rkmuxer_init(0, NULL, rk_storage_group[0].file_name, &rk_storage_group[0].g_video_param, &rk_storage_group[0].g_audio_param);
	rk_storage_group[0].g_record_run_ = 1;
	LOG_INFO("end\n");

	return 0;
}

int rk_storage_record_stop() {
	// 主码流
	LOG_INFO("start\n");
	rkmuxer_deinit(0);
	rk_storage_group[0].g_record_run_ = 0;
	LOG_INFO("end\n");

	return 0;
}

int rk_stoarge_record_statue_get(int *value) {
	*value = rk_storage_group[0].g_record_run_;
	return 0;
}