// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#ifndef __RKIPC_STORAGE_H__
#define __RKIPC_STORAGE_H__

#include <stdbool.h>
#include <stdint.h>

#define RKIPC_MAX_FORMAT_ID_LEN 8
#define RKIPC_MAX_VOLUME_LEN 11
#define RKIPC_MAX_FILE_PATH_LEN 256

/* Pointer Check */
#define rkipc_check_pointer(p, errcode)                                                            \
	do {                                                                                           \
		if (!(p)) {                                                                                \
			LOG_DEBUG("pointer[%s] is NULL", #p);                                                  \
			return errcode;                                                                        \
		}                                                                                          \
	} while (0)

typedef enum {
	DISK_UNMOUNTED = 0,
	DISK_NOT_FORMATTED,
	DISK_FORMAT_ERR,
	DISK_SCANNING,
	DISK_MOUNTED,
	DISK_MOUNT_BUTT,
} rkipc_mount_status;

typedef enum {
	LIST_ASCENDING = 0,
	LIST_DESCENDING,
	LIST_BUTT,
} rkipc_sort_type;

typedef enum {
	SORT_MODIFY_TIME = 0,
	SORT_FILE_NAME,
	SORT_BUTT,
} rkipc_sort_condition;

typedef struct {
	char folder_path[RKIPC_MAX_FILE_PATH_LEN];
	rkipc_sort_condition sort_cond;
	bool num_limit;
	int s32Limit;
} rkipc_str_folder_attr;

typedef struct {
	char dev_path[RKIPC_MAX_FILE_PATH_LEN];
	char mount_path[RKIPC_MAX_FILE_PATH_LEN];
	int free_size_del_min;
	int free_size_del_max;
	int auto_delete;
	int folder_num;
	char format_id[RKIPC_MAX_FORMAT_ID_LEN];
	char volume[RKIPC_MAX_VOLUME_LEN];
	int check_format_id;
	rkipc_str_folder_attr *pstFolderAttr;
} rkipc_str_dev_attr;

typedef struct {
	char filename[RKIPC_MAX_FILE_PATH_LEN];
	long stSize;
	long long stTime;
	void *thumb;
} rkipc_fileinfo;

typedef struct {
	char path[RKIPC_MAX_FILE_PATH_LEN];
	int file_num;
	rkipc_fileinfo *file;
} rkipc_filelist;

typedef struct {
	int list_num;
	rkipc_filelist *list;
} rkipc_filelist_array;

int rk_storage_init();
int rk_storage_deinit();
int rk_storage_write_video_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                                 int64_t present_time, int key_frame);
int rk_storage_write_audio_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                                 int64_t present_time);
int rk_storage_record_start();
int rk_storage_record_stop();
int rk_stoarge_record_statue_get(int *value);

char *rkipc_get_quota_info(int id);
char *rkipc_get_hdd_list(int id);
char *rkipc_get_snap_plan_by_id(int id);
char *rkipc_get_current_path();
char *rkipc_get_advanced_para();
// num:The number of files to delete
// namel_ist:The list of files to be deleted, the number of lists matches the num
char *rkipc_response_delete(int id, int num, char *name_list);

// TODO
/************当前未做接口
 * 根据id获取quota：当前仅做SD卡，两个接口内容一致
 * 设置配额：adk那边没做过动态修改配额，存在风险
 * 根据id获取hdd_list：当前仅做SD卡，两个接口内容一致
 * 根据id设置snap_plan：未见有抓拍计划
 * 设置advanced para
 * 文件搜索功能：adk内无相关接口
 */

#ifdef __cplusplus
}
#endif
#endif