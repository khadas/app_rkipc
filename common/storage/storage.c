// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "storage.h"
#include "common.h"
#include "rkmuxer.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <cJSON.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <pthread.h>
#include <rkfsmk.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/vfs.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "storage.c"

#define STORAGE_NUM 3
#define SIZE_1MB (1024 * 1024)

#define FILE_SIZE (64 * SIZE_1MB)

#define JSON_KEY_FOLDER_NAME "FolderName"
#define JSON_KEY_FILE_NUMBER "FileNumber"
#define JSON_KEY_TOTAL_SIZE "TotalSize"
#define JSON_KEY_TOTAL_SPACE "TotalSpace"
#define JSON_KEY_FILE_ARRAY "FileArray"
#define JSON_KEY_FILE_NAME "FileName"
#define JSON_KEY_MODIFY_TIME "ModifyTime"
#define JSON_KEY_FILE_SIZE "FileSize"
#define JSON_KEY_FILE_SPACE "FileSpace"

#define rkipc_failure (-1)
#define MAX_TYPE_NMSG_LEN 32
#define MAX_ATTR_LEN 256
#define MAX_STRLINE_LEN 1024
typedef int (*rkipc_reg_msg_cb)(void *, int, void *, int, void *);
void *sd_phandle = NULL;
rkipc_str_dev_attr sd_DevAttr;

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

typedef struct _rkipc_str_file {
	struct _rkipc_str_file *next;
	char filename[RKIPC_MAX_FILE_PATH_LEN];
	time_t stTime;
	off_t stSize;
	off_t stSpace;
	mode_t stMode;
} rkipc_str_file;

typedef struct {
	char cpath[RKIPC_MAX_FILE_PATH_LEN];
	rkipc_sort_condition sort_cond;
	int wd;
	int file_num;
	off_t totalSize;
	off_t totalSpace;
	pthread_mutex_t mutex;
	rkipc_str_file *pstFileListFirst;
	rkipc_str_file *pstFileListLast;
} rkipc_str_folder;

typedef struct {
	char dev_path[RKIPC_MAX_FILE_PATH_LEN];
	char cDevType[MAX_TYPE_NMSG_LEN];
	char cDevAttr1[MAX_ATTR_LEN];
	rkipc_mount_status s32MountStatus;
	pthread_t fileScanTid;
	int folder_num;
	int s32TotalSize;
	int s32FreeSize;
	int s32FsckQuit;
	rkipc_str_folder *pstFolder;
} rkipc_str_dev_sta;

typedef struct _rkipc_tmsg_element {
	struct _rkipc_tmsg_element *next;
	int msg;
	char *data;
	int s32DataLen;
} rkipc_tmsg_element;

typedef struct {
	rkipc_tmsg_element *first;
	rkipc_tmsg_element *last;
	int num;
	int quit;
	pthread_mutex_t mutex;
	pthread_cond_t notEmpty;
	rkipc_reg_msg_cb recMsgCb;
	pthread_t recTid;
	void *pHandlePath;
} rkipc_tmsg_buffer;

typedef enum {
	MSG_DEV_ADD = 1,
	MSG_DEV_REMOVE = 2,
	MSG_DEV_CHANGED = 3,
} rkipc_enum_msg;

typedef struct {
	rkipc_tmsg_buffer stMsgHd;
	pthread_t eventListenerTid;
	int eventListenerRun;
	rkipc_str_dev_sta stDevSta;
	rkipc_str_dev_attr stDevAttr;
} rkipc_storage_handle;

static int rkipc_storage_RKFSCK(rkipc_storage_handle *pHandle, rkipc_str_dev_attr *pdevAttr);

static rkipc_str_dev_attr rkipc_storage_GetParam(rkipc_storage_handle *pHandle) {
	return pHandle->stDevAttr;
}

rkipc_str_dev_attr rkipc_storage_GetDevAttr(void *pHandle) {
	return rkipc_storage_GetParam((rkipc_storage_handle *)pHandle);
}

static int rkipc_storage_CreateFolder(char *folder) {
	int i, len;

	rkipc_check_pointer(folder, rkipc_failure);

	len = strlen(folder);
	if (!len) {
		LOG_ERROR("Invalid path.\n");
		return -1;
	}

	for (i = 1; i < len; i++) {
		if (folder[i] != '/')
			continue;

		folder[i] = 0;
		if (access(folder, R_OK)) {
			if (mkdir(folder, 0755)) {
				LOG_ERROR("mkdir error\n");
				return -1;
			}
		}
		folder[i] = '/';
	}

	if (access(folder, R_OK)) {
		if (mkdir(folder, 0755)) {
			LOG_ERROR("mkdir error\n");
			return -1;
		}
	}

	LOG_DEBUG("Create %s finished\n", folder);
	return 0;
}

static int rkipc_storage_ReadTimeout(int fd, unsigned int u32WaitMs) {
	int ret = 0;

	if (u32WaitMs > 0) {
		fd_set readFdset;
		struct timeval timeout;

		FD_ZERO(&readFdset);
		FD_SET(fd, &readFdset);

		timeout.tv_sec = u32WaitMs / 1000;
		timeout.tv_usec = (u32WaitMs % 1000) * 1000;

		do {
			ret = select(fd + 1, &readFdset, NULL, NULL, &timeout);
		} while (ret < 0 && errno == EINTR);

		if (ret == 0) {
			ret = -1;
			errno = ETIMEDOUT;
		} else if (ret == 1) {
			return 0;
		}
	}

	return ret;
}

static int rkipc_storage_GetDiskSize(char *path, int *totalSize, int *freeSize) {
	struct statfs diskInfo;

	rkipc_check_pointer(path, rkipc_failure);
	rkipc_check_pointer(totalSize, rkipc_failure);
	rkipc_check_pointer(freeSize, rkipc_failure);

	if (statfs(path, &diskInfo)) {
		LOG_ERROR("statfs[%s] failed", path);
		return -1;
	}

	*totalSize = (diskInfo.f_bsize * diskInfo.f_blocks) >> 10;
	*freeSize = (diskInfo.f_bfree * diskInfo.f_bsize) >> 10;
	return 0;
}

static int rkipc_storage_GetMountDev(char *path, char *dev, char *type, char *attributes) {
	FILE *fp;
	char strLine[MAX_STRLINE_LEN];
	char *tmp;

	rkipc_check_pointer(dev, rkipc_failure);
	rkipc_check_pointer(path, rkipc_failure);
	rkipc_check_pointer(type, rkipc_failure);
	rkipc_check_pointer(attributes, rkipc_failure);

	if ((fp = fopen("/proc/mounts", "r")) == NULL) {
		LOG_ERROR("Open file error!");
		return -1;
	}

	while (!feof(fp)) {
		fgets(strLine, MAX_STRLINE_LEN, fp);
		tmp = strstr(strLine, path);

		if (tmp) {
			char MountPath[RKIPC_MAX_FILE_PATH_LEN];
			sscanf(strLine, "%s %s %s %s", dev, MountPath, type, attributes);

			fclose(fp);
			return 0;
		}
	}

	fclose(fp);
	return -1;
}

static int rkipc_storage_GetMountPath(char *dev, char *path, int s32PathLen) {
	int ret = -1;
	FILE *fp;
	char strLine[MAX_STRLINE_LEN];
	char *tmp;

	rkipc_check_pointer(dev, rkipc_failure);
	rkipc_check_pointer(path, rkipc_failure);

	if ((fp = fopen("/proc/mounts", "r")) == NULL) {
		LOG_ERROR("Open file error!\n");
		return -1;
	}

	memset(path, 0, s32PathLen);
	while (!feof(fp)) {
		fgets(strLine, MAX_STRLINE_LEN, fp);
		tmp = strstr(strLine, dev);

		if (tmp) {
			int len;
			char *s = strstr(strLine, " ") + 1;
			char *e = strstr(s, " ");
			len = e - s;

			if ((len > 0) && (len < s32PathLen)) {
				memcpy(path, s, len);
				ret = 0;
			} else {
				LOG_ERROR("len[%d], s32PathLen[%d]", len, s32PathLen);
				ret = -2;
			}

			goto exit;
		}
	}

exit:
	fclose(fp);
	return ret;
}

static bool rkipc_storage_FileCompare(rkipc_str_file *existingFile, rkipc_str_file *newFile,
                                      rkipc_sort_condition cond) {
	bool ret = false;

	switch (cond) {
	case SORT_MODIFY_TIME: {
		ret = (newFile->stTime <= existingFile->stTime);
		break;
	}
	case SORT_FILE_NAME: {
		ret = (strcmp(newFile->filename, existingFile->filename) <= 0);
		break;
	}
	case SORT_BUTT: {
		ret = false;
		LOG_ERROR("Invalid condition.");
		break;
	}
	}

	return ret;
}

static int rkipc_storage_FileListCheck(rkipc_str_folder *folder, char *filename,
                                       struct stat *statbuf) {
	int ret = 0;
	rkipc_str_file *tmp = NULL;

	rkipc_check_pointer(folder, rkipc_failure);
	rkipc_check_pointer(filename, rkipc_failure);

	pthread_mutex_lock(&folder->mutex);

	if (folder->pstFileListFirst) {
		rkipc_str_file *tmp_1 = NULL;
		tmp = folder->pstFileListFirst;

		if (!strcmp(tmp->filename, filename)) {
			ret = 1;
		} else {
			while (tmp->next) {
				if (!strcmp(tmp->next->filename, filename)) {
					ret = 1;
					break;
				}
				tmp = tmp->next;
			}
		}
	}

	pthread_mutex_unlock(&folder->mutex);

	return ret;
}

static int rkipc_storage_FileListAdd(rkipc_str_folder *folder, char *filename,
                                     struct stat *statbuf) {
	rkipc_str_file *tmp = NULL;
	rkipc_str_file *tmp_1 = NULL;
	int file_num = 0;

	rkipc_check_pointer(folder, rkipc_failure);
	rkipc_check_pointer(filename, rkipc_failure);

	pthread_mutex_lock(&folder->mutex);

	tmp_1 = (rkipc_str_file *)malloc(sizeof(rkipc_str_file));

	if (!tmp_1) {
		LOG_ERROR("tmp malloc failed.");
		pthread_mutex_unlock(&folder->mutex);
		return -1;
	}

	sprintf(tmp_1->filename, "%s", filename);
	tmp_1->stSize = statbuf->st_size;
	tmp_1->stSpace = statbuf->st_blocks << 9;
	tmp_1->stTime = statbuf->st_mtime;
	tmp_1->next = NULL;

	if (folder->pstFileListFirst) {
		tmp = folder->pstFileListFirst;
		if (tmp_1->stTime >= tmp->stTime) {
			tmp_1->next = tmp;
			folder->pstFileListFirst = tmp_1;
		} else {
			while (tmp->next) {
				if (tmp_1->stTime >= tmp->next->stTime) {
					tmp_1->next = tmp->next;
					tmp->next = tmp_1;
					break;
				}
				tmp = tmp->next;
			}
			if (tmp->next == NULL) {
				tmp->next = tmp_1;
				folder->pstFileListLast = tmp_1;
			}
		}
	} else {
		folder->pstFileListFirst = tmp_1;
		folder->pstFileListLast = tmp_1;
	}

	folder->totalSize += tmp_1->stSize;
	folder->totalSpace += tmp_1->stSpace;
	folder->file_num++;

	pthread_mutex_unlock(&folder->mutex);
	return 0;
}

static int rkipc_storage_FileListDel(rkipc_str_folder *folder, char *filename) {
	int file_num = 0;
	off_t totalSize = 0;
	off_t totalSpace = 0;
	rkipc_str_file *next = NULL;

	rkipc_check_pointer(folder, rkipc_failure);
	rkipc_check_pointer(filename, rkipc_failure);

	pthread_mutex_lock(&folder->mutex);

again:
	if (folder->pstFileListFirst) {
		rkipc_str_file *tmp = folder->pstFileListFirst;
		if (!strcmp(tmp->filename, filename)) {
			folder->pstFileListFirst = folder->pstFileListFirst->next;
			free(tmp);
			tmp = folder->pstFileListFirst;
			if (folder->pstFileListFirst == NULL) {
				folder->pstFileListLast = NULL;
			}
			goto again;
		}

		while (tmp) {
			next = tmp->next;
			totalSize += tmp->stSize;
			totalSpace += tmp->stSpace;
			file_num++;
			if (next == NULL) {
				folder->pstFileListLast = tmp;
				break;
			}
			if (!strcmp(next->filename, filename)) {
				tmp->next = next->next;
				free(next);
				next = tmp->next;
				if (tmp->next == NULL)
					folder->pstFileListLast = tmp;
			}
			tmp = next;
		}
	}
	folder->file_num = file_num;
	folder->totalSize = totalSize;
	folder->totalSpace = totalSpace;

	pthread_mutex_unlock(&folder->mutex);
	return 0;
}

static int rkipc_storage_FileListSave(rkipc_str_folder pstFolder,
                                      rkipc_str_folder_attr pstFolderAttr, char *mount_path) {
	int i, len;
	char dataFileName[RKIPC_MAX_FILE_PATH_LEN];
	char jsonFileName[2 * RKIPC_MAX_FILE_PATH_LEN];
	FILE *fp;
	cJSON *folder = NULL;
	cJSON *fileArray = NULL;
	cJSON *info = NULL;
	char *folderStr = NULL;
	rkipc_str_file *tmp = pstFolder.pstFileListFirst;

	folder = cJSON_CreateObject();
	cJSON_AddStringToObject(folder, JSON_KEY_FOLDER_NAME, pstFolderAttr.folder_path);
	cJSON_AddNumberToObject(folder, JSON_KEY_FILE_NUMBER, pstFolder.file_num);
	cJSON_AddNumberToObject(folder, JSON_KEY_TOTAL_SIZE, pstFolder.totalSize);
	cJSON_AddNumberToObject(folder, JSON_KEY_TOTAL_SPACE, pstFolder.totalSpace);
	cJSON_AddItemToObject(folder, JSON_KEY_FILE_ARRAY, fileArray = cJSON_CreateArray());
	for (i = 0; i < pstFolder.file_num && tmp != NULL; i++) {
		cJSON_AddItemToArray(fileArray, info = cJSON_CreateObject());
		cJSON_AddStringToObject(info, JSON_KEY_FILE_NAME, tmp->filename);
		cJSON_AddNumberToObject(info, JSON_KEY_MODIFY_TIME, tmp->stTime);
		cJSON_AddNumberToObject(info, JSON_KEY_FILE_SIZE, tmp->stSize);
		cJSON_AddNumberToObject(info, JSON_KEY_FILE_SPACE, tmp->stSpace);
		tmp = tmp->next;
	}
	folderStr = cJSON_Print(folder);
	cJSON_Delete(folder);

	len = strlen(pstFolderAttr.folder_path) - 2;
	strncpy(dataFileName, pstFolderAttr.folder_path + 1, len);
	dataFileName[len] = '\0';
	sprintf(jsonFileName, "%s/.%s.json", mount_path, dataFileName);
	LOG_DEBUG("Save fileList data in %s", jsonFileName);

	if ((fp = fopen(jsonFileName, "w+")) == NULL) {
		LOG_ERROR("Open %s error!", jsonFileName);
		free(folderStr);
		return -1;
	}

	if (fwrite(folderStr, strlen(folderStr), 1, fp) != 1) {
		LOG_ERROR("Write file error!");
		fclose(fp);
		free(folderStr);
		return -1;
	}

	fclose(fp);
	sync();
	free(folderStr);
	return 0;
}

static int rkipc_storage_FileListLoad(rkipc_str_folder *pstFolder,
                                      rkipc_str_folder_attr pstFolderAttr, char *mount_path) {
	int i, len;
	long long lenStr;
	char *str;
	char dataFileName[RKIPC_MAX_FILE_PATH_LEN];
	char jsonFileName[2 * RKIPC_MAX_FILE_PATH_LEN];
	FILE *fp;
	cJSON *value = NULL;
	cJSON *folder = NULL;
	cJSON *fileArray = NULL;
	cJSON *info = NULL;
	rkipc_str_file *tmp = NULL;

	rkipc_check_pointer(pstFolder, rkipc_failure);

	len = strlen(pstFolderAttr.folder_path) - 2;
	strncpy(dataFileName, pstFolderAttr.folder_path + 1, len);
	dataFileName[len] = '\0';
	sprintf(jsonFileName, "%s/.%s.json", mount_path, dataFileName);
	LOG_DEBUG("Load fileList data from %s", jsonFileName);

	if ((fp = fopen(jsonFileName, "r")) == NULL) {
		LOG_ERROR("Open %s error!", jsonFileName);
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	lenStr = ftell(fp);
	str = (char *)malloc(lenStr + 1);
	if (str == NULL) {
		LOG_ERROR("malloc str failed!");
		fclose(fp);
		return -1;
	}

	fseek(fp, 0, SEEK_SET);
	if (fread(str, lenStr, 1, fp) != 1) {
		LOG_ERROR("Read file error!");
		fclose(fp);
		free(str);
		return -1;
	}
	str[lenStr] = '\0';
	fclose(fp);

	if ((folder = cJSON_Parse(str)) == NULL) {
		LOG_ERROR("Parse error!");
		free(str);
		return -1;
	}
	free(str);

	pthread_mutex_lock(&pstFolder->mutex);
	value = cJSON_GetObjectItem(folder, JSON_KEY_FILE_NUMBER);
	pstFolder->file_num = value->valuedouble;
	value = cJSON_GetObjectItem(folder, JSON_KEY_TOTAL_SIZE);
	pstFolder->totalSize = value->valuedouble;
	value = cJSON_GetObjectItem(folder, JSON_KEY_TOTAL_SPACE);
	pstFolder->totalSpace = value->valuedouble;
	if ((fileArray = cJSON_GetObjectItem(folder, JSON_KEY_FILE_ARRAY)) == NULL) {
		LOG_ERROR("Get fileArray object item error!");
		cJSON_Delete(folder);
		pthread_mutex_unlock(&pstFolder->mutex);
		return -1;
	}

	for (i = 0; i < pstFolder->file_num; i++) {
		tmp = (rkipc_str_file *)malloc(sizeof(rkipc_str_file));
		if (!tmp) {
			LOG_ERROR("tmp malloc failed.");
			cJSON_Delete(folder);
			pthread_mutex_unlock(&pstFolder->mutex);
			return -1;
		}

		memset(tmp, 0, sizeof(rkipc_str_file));
		info = cJSON_GetArrayItem(fileArray, i);
		value = cJSON_GetObjectItem(info, JSON_KEY_FILE_NAME);
		sprintf(tmp->filename, "%s", value->valuestring);
		value = cJSON_GetObjectItem(info, JSON_KEY_FILE_SIZE);
		tmp->stSize = value->valuedouble;
		value = cJSON_GetObjectItem(info, JSON_KEY_FILE_SPACE);
		tmp->stSpace = value->valuedouble;
		value = cJSON_GetObjectItem(info, JSON_KEY_MODIFY_TIME);
		tmp->stTime = value->valuedouble;
		tmp->next = NULL;

		if (pstFolder->pstFileListFirst) {
			pstFolder->pstFileListLast->next = tmp;
			pstFolder->pstFileListLast = tmp;
		} else {
			pstFolder->pstFileListFirst = tmp;
			pstFolder->pstFileListLast = tmp;
		}
	}

	cJSON_Delete(folder);
	pthread_mutex_unlock(&pstFolder->mutex);
	return 0;
}

static int rkipc_storage_Repair(rkipc_storage_handle *pHandle, rkipc_str_dev_attr *pdevAttr) {
	int i;
	int j;
	int ret = 0;
	char file[3 * RKIPC_MAX_FILE_PATH_LEN];

	for (i = 0; i < pdevAttr->folder_num; i++) {
		rkipc_str_folder *folder = &pHandle->stDevSta.pstFolder[i];
		rkipc_str_file *current = NULL;
		rkipc_str_file *next = NULL;

		pthread_mutex_lock(&folder->mutex);
		current = folder->pstFileListFirst;
		if (current) {
			snprintf(file, 3 * RKIPC_MAX_FILE_PATH_LEN, "%s%s%s", pdevAttr->mount_path,
			         pdevAttr->pstFolderAttr[i].folder_path, current->filename);
			if ((current->stSize == 0) || (repair_mp4(file) == REPA_FAIL)) {
				LOG_ERROR("Delete %s file. %lld", file, current->stSize);
				if (remove(file))
					LOG_ERROR("Delete %s file error.", file);
				folder->pstFileListFirst = current->next;
				free(current);
				if (folder->pstFileListFirst == NULL)
					folder->pstFileListLast = NULL;
				else if (folder->pstFileListFirst->next == NULL)
					folder->pstFileListLast = folder->pstFileListFirst;
				folder->file_num--;
			}
		}
		current = folder->pstFileListFirst;

		for (j = 0; j < 8 && current && current->next; j++) {
			snprintf(file, 3 * RKIPC_MAX_FILE_PATH_LEN, "%s%s%s", pdevAttr->mount_path,
			         pdevAttr->pstFolderAttr[i].folder_path, current->next->filename);
			if ((current->next->stSize == 0) || (repair_mp4(file) == REPA_FAIL)) {
				LOG_ERROR("Delete %s file. %lld", file, current->next->stSize);
				if (remove(file))
					LOG_ERROR("Delete %s file error.", file);
				next = current->next;
				current->next = next->next;
				free(next);
				if (current->next == NULL)
					folder->pstFileListLast = current;
				folder->file_num--;
			}
			current = current->next;
		}
		pthread_mutex_unlock(&folder->mutex);
	}
	sync();

	return ret;
}

static void *rkipc_storage_FileMonitorThread(void *arg) {
	rkipc_storage_handle *pHandle = (rkipc_storage_handle *)arg;
	int fd;
	int len;
	int nread;
	char buf[BUFSIZ];
	struct inotify_event *event;
	int j;

	if (!pHandle) {
		LOG_ERROR("invalid pHandle");
		return NULL;
	}
	LOG_INFO("rkipc_storage_FileMonitorThread\n");
	prctl(PR_SET_NAME, "rkipc_storage_FileMonitorThread", 0, 0, 0);
	fd = inotify_init();
	if (fd < 0) {
		LOG_ERROR("inotify_init failed");
		return NULL;
	}

	for (j = 0; j < pHandle->stDevSta.folder_num; j++) {
		pHandle->stDevSta.pstFolder[j].wd = inotify_add_watch(
		    fd, pHandle->stDevSta.pstFolder[j].cpath,
		    IN_CREATE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM | IN_CLOSE_WRITE | IN_UNMOUNT);
	}

	memset(buf, 0, BUFSIZ);
	while (pHandle->stDevSta.s32MountStatus == DISK_MOUNTED) {
		if (rkipc_storage_ReadTimeout(fd, 10))
			continue;

		len = read(fd, buf, BUFSIZ - 1);
		nread = 0;
		while (len > 0) {
			event = (struct inotify_event *)&buf[nread];
			if (event->mask & IN_UNMOUNT)
				pHandle->stDevSta.s32MountStatus = DISK_UNMOUNTED;

			if (event->len > 0) {
				for (j = 0; j < pHandle->stDevSta.folder_num; j++) {
					if (event->wd == pHandle->stDevSta.pstFolder[j].wd) {
						if (event->mask & IN_MOVED_TO) {
							char d_name[RKIPC_MAX_FILE_PATH_LEN];
							struct stat statbuf;
							sprintf(d_name, "%s%s", pHandle->stDevSta.pstFolder[j].cpath,
							        event->name);
							if (lstat(d_name, &statbuf)) {
								LOG_ERROR("lstat[%s](IN_MOVED_TO) failed", d_name);
							} else {
								if ((rkipc_storage_FileListCheck(&pHandle->stDevSta.pstFolder[j],
								                                 event->name, &statbuf) == 0) &&
								    rkipc_storage_FileListAdd(&pHandle->stDevSta.pstFolder[j],
								                              event->name, &statbuf))
									LOG_ERROR("FileListAdd failed");
							}
						}

						if ((event->mask & IN_DELETE) || (event->mask & IN_MOVED_FROM))
							if (rkipc_storage_FileListDel(&pHandle->stDevSta.pstFolder[j],
							                              event->name))
								LOG_ERROR("FileListDel failed");

						if (event->mask & IN_CLOSE_WRITE) {
							char d_name[RKIPC_MAX_FILE_PATH_LEN];
							struct stat statbuf;
							sprintf(d_name, "%s%s", pHandle->stDevSta.pstFolder[j].cpath,
							        event->name);
							if (lstat(d_name, &statbuf)) {
								LOG_ERROR("lstat[%s](IN_CLOSE_WRITE) failed", d_name);
							} else {
								if (statbuf.st_size == 0) {
									if (remove(d_name))
										LOG_ERROR("Delete %s file error.", d_name);
								} else if ((rkipc_storage_FileListCheck(
								                &pHandle->stDevSta.pstFolder[j], event->name,
								                &statbuf) == 0) &&
								           rkipc_storage_FileListAdd(
								               &pHandle->stDevSta.pstFolder[j], event->name,
								               &statbuf)) {
									LOG_ERROR("FileListAdd failed");
								}
							}
						}
					}
				}
			}

			nread = nread + sizeof(struct inotify_event) + event->len;
			len = len - sizeof(struct inotify_event) - event->len;
		}
	}

	LOG_DEBUG("Exit!");
	close(fd);
	return NULL;
}

static void *rkipc_storage_FileScanThread(void *arg) {
	rkipc_storage_handle *pHandle = (rkipc_storage_handle *)arg;
	int cnt = 0;
	int i;
	pthread_t fileMonitorTid = 0;
	rkipc_str_dev_attr devAttr;

	if (!pHandle) {
		LOG_ERROR("invalid pHandle\n");
		return NULL;
	}
	devAttr = rkipc_storage_GetParam(pHandle);
	prctl(PR_SET_NAME, "file_scan_thread", 0, 0, 0);
	LOG_INFO("%s, %s, %s, %s\n", devAttr.mount_path, pHandle->stDevSta.dev_path,
	         pHandle->stDevSta.cDevType, pHandle->stDevSta.cDevAttr1);

	if (pHandle->stDevSta.s32MountStatus != DISK_UNMOUNTED) {
		LOG_INFO("devAttr.folder_num = %d\n", devAttr.folder_num);
		pHandle->stDevSta.folder_num = devAttr.folder_num;
		pHandle->stDevSta.pstFolder =
		    (rkipc_str_folder *)malloc(sizeof(rkipc_str_folder) * devAttr.folder_num);

		if (!pHandle->stDevSta.pstFolder) {
			LOG_ERROR("pHandle->stDevSta.pstFolder malloc failed.\n");
			return NULL;
		}
		memset(pHandle->stDevSta.pstFolder, 0, sizeof(rkipc_str_folder) * devAttr.folder_num);
		for (i = 0; i < pHandle->stDevSta.folder_num; i++) {
			sprintf(pHandle->stDevSta.pstFolder[i].cpath, "%s%s", devAttr.mount_path,
			        devAttr.pstFolderAttr[i].folder_path);
			LOG_INFO("%s\n", pHandle->stDevSta.pstFolder[i].cpath);
			pthread_mutex_init(&(pHandle->stDevSta.pstFolder[i].mutex), NULL);
			if (rkipc_storage_CreateFolder(pHandle->stDevSta.pstFolder[i].cpath)) {
				LOG_ERROR("CreateFolder failed\n");
				goto file_scan_out;
			}
		}
	}

	/*if (rkipc_storage_RKFSCK(pHandle, &devAttr) == RKFSCK_ID_ERR) {
	    LOG_ERROR("RKFSCK_ID_ERR");
	    if (pHandle->stDevSta.s32MountStatus != DISK_UNMOUNTED)
	        pHandle->stDevSta.s32MountStatus = DISK_FORMAT_ERR;
	    goto file_scan_out;
	}

	rkipc_storage_Repair(pHandle, &devAttr);*/

	if (pHandle->stDevSta.s32MountStatus == DISK_UNMOUNTED)
		goto file_scan_out;
	else
		pHandle->stDevSta.s32MountStatus = DISK_MOUNTED;

	if (pHandle->stDevSta.s32MountStatus != DISK_UNMOUNTED) {
		if (rkipc_storage_GetDiskSize(devAttr.mount_path, &pHandle->stDevSta.s32TotalSize,
		                              &pHandle->stDevSta.s32FreeSize)) {
			LOG_ERROR("GetDiskSize failed\n");
			return NULL;
		}
	} else {
		pHandle->stDevSta.s32TotalSize = 0;
		pHandle->stDevSta.s32FreeSize = 0;
	}
	LOG_INFO("s32TotalSize = %d, s32FreeSize = %d\n", pHandle->stDevSta.s32TotalSize,
	         pHandle->stDevSta.s32FreeSize);

	if (pthread_create(&fileMonitorTid, NULL, rkipc_storage_FileMonitorThread, (void *)pHandle)) {
		LOG_ERROR("FileMonitorThread create failed.\n");
		goto file_scan_out;
	}

	while (pHandle->stDevSta.s32MountStatus == DISK_MOUNTED) {
		if (cnt++ > 50) {
			int limit;
			off_t totalSpace = 0;
			cnt = 0;
			for (i = 0; i < devAttr.folder_num; i++) {
				if (devAttr.pstFolderAttr[i].num_limit == true) {
					char file[3 * RKIPC_MAX_FILE_PATH_LEN];

					pthread_mutex_lock(&pHandle->stDevSta.pstFolder[i].mutex);
					limit = pHandle->stDevSta.pstFolder[i].file_num;
					if (limit > devAttr.pstFolderAttr[i].s32Limit) {
						if (pHandle->stDevSta.pstFolder[i].pstFileListLast) {
							sprintf(file, "%s%s%s", devAttr.mount_path,
							        devAttr.pstFolderAttr[i].folder_path,
							        pHandle->stDevSta.pstFolder[i].pstFileListLast->filename);
							LOG_INFO("Delete file:%s\n", file);
							pthread_mutex_unlock(&pHandle->stDevSta.pstFolder[i].mutex);

							if (remove(file)) {
								char filename[RKIPC_MAX_FILE_PATH_LEN];
								snprintf(filename, RKIPC_MAX_FILE_PATH_LEN, "%s",
								         pHandle->stDevSta.pstFolder[i].pstFileListLast->filename);
								rkipc_storage_FileListDel(&pHandle->stDevSta.pstFolder[i],
								                          filename);
								LOG_ERROR("Delete %s file error.\n", file);
							}
							usleep(100);
							cnt = 51;
							continue;
						}
					}
					pthread_mutex_unlock(&pHandle->stDevSta.pstFolder[i].mutex);
				}
			}

			if (rkipc_storage_GetDiskSize(devAttr.mount_path, &pHandle->stDevSta.s32TotalSize,
			                              &pHandle->stDevSta.s32FreeSize)) {
				LOG_ERROR("GetDiskSize failed\n");
				goto file_scan_out;
			}

			if (pHandle->stDevSta.s32FreeSize <= (devAttr.free_size_del_min * 1024))
				devAttr.auto_delete = 1;

			if (pHandle->stDevSta.s32FreeSize >= (devAttr.free_size_del_max * 1024))
				devAttr.auto_delete = 0;

			if (devAttr.auto_delete) {
				for (i = 0; i < devAttr.folder_num; i++) {
					pthread_mutex_lock(&pHandle->stDevSta.pstFolder[i].mutex);
					if (devAttr.pstFolderAttr[i].num_limit == false)
						totalSpace += pHandle->stDevSta.pstFolder[i].totalSpace;
					pthread_mutex_unlock(&pHandle->stDevSta.pstFolder[i].mutex);
				}
				if (totalSpace) {
					for (i = 0; i < devAttr.folder_num; i++) {
						if (devAttr.pstFolderAttr[i].num_limit == false) {
							char file[3 * RKIPC_MAX_FILE_PATH_LEN];
							pthread_mutex_lock(&pHandle->stDevSta.pstFolder[i].mutex);
							limit = pHandle->stDevSta.pstFolder[i].totalSpace * 100 / totalSpace;
							if (limit > devAttr.pstFolderAttr[i].s32Limit) {
								if (pHandle->stDevSta.pstFolder[i].pstFileListLast) {
									sprintf(
									    file, "%s%s%s", devAttr.mount_path,
									    devAttr.pstFolderAttr[i].folder_path,
									    pHandle->stDevSta.pstFolder[i].pstFileListLast->filename);
									LOG_INFO("Delete file:%s\n", file);
									pthread_mutex_unlock(&pHandle->stDevSta.pstFolder[i].mutex);

									if (remove(file)) {
										char filename[RKIPC_MAX_FILE_PATH_LEN];
										snprintf(filename, RKIPC_MAX_FILE_PATH_LEN, "%s",
										         pHandle->stDevSta.pstFolder[i]
										             .pstFileListLast->filename);
										rkipc_storage_FileListDel(&pHandle->stDevSta.pstFolder[i],
										                          filename);
										LOG_ERROR("Delete %s file error.\n", file);
									}
									usleep(100);
									cnt = 51;
									continue;
								}
							}
							pthread_mutex_unlock(&pHandle->stDevSta.pstFolder[i].mutex);
						}
					}
				}
			}
		}
		usleep(10000);
	}

file_scan_out:
	if (fileMonitorTid)
		if (pthread_join(fileMonitorTid, NULL))
			LOG_ERROR("FileMonitorThread join failed.\n");
	LOG_DEBUG("out\n");

	if (pHandle->stDevSta.pstFolder) {
		free(pHandle->stDevSta.pstFolder);
		pHandle->stDevSta.pstFolder = NULL;
	}
	pHandle->stDevSta.folder_num = 0;

	return NULL;
}

static void cb(void *userdata, char *filename, int dir, struct stat *statbuf) {
	if (dir == 0) {
		rkipc_str_folder *pstFolder = (rkipc_str_folder *)userdata;
		rkipc_storage_FileListAdd(pstFolder, filename, statbuf);
	}
}

static int rkipc_storage_RKFSCK(rkipc_storage_handle *pHandle, rkipc_str_dev_attr *pdevAttr) {
	int i;
	int ret = 0;
	struct reg_para para;

	para.folder_num = 4;
	para.folder = (struct folder_para *)malloc(sizeof(struct folder_para) * para.folder_num);
	if (para.folder == NULL) {
		LOG_ERROR("malloc para.folder failed!\n");
		return -1;
	}

	memcpy(para.format_id, pdevAttr->format_id, RKIPC_MAX_FORMAT_ID_LEN);
	para.check_format_id = pdevAttr->check_format_id;
	para.quit = &pHandle->stDevSta.s32FsckQuit;
	pHandle->stDevSta.s32FsckQuit = 0;
	for (i = 0; i < para.folder_num; i++) {
		para.folder[i].path = pdevAttr->pstFolderAttr[i].folder_path;
		para.folder[i].userdata = &pHandle->stDevSta.pstFolder[i];
		para.folder[i].cb = &cb;
	}

	umount2(pHandle->stDevAttr.mount_path, MNT_DETACH);
	ret = rkfsmk_fat_check(pHandle->stDevSta.dev_path, &para);
	sync();
	mount(pHandle->stDevAttr.dev_path, pHandle->stDevAttr.mount_path, "vfat",
	      MS_NOATIME | MS_NOSUID, NULL);

	return ret;
}

static int rkipc_storage_DevAdd(char *dev, rkipc_storage_handle *pHandle) {
	int ret;
	rkipc_str_dev_attr stDevAttr;
	char mountPath[RKIPC_MAX_FILE_PATH_LEN];

	rkipc_check_pointer(dev, rkipc_failure);
	rkipc_check_pointer(pHandle, rkipc_failure);

	stDevAttr = rkipc_storage_GetParam(pHandle);
	LOG_INFO("%s, %s", dev, mountPath);

	if (stDevAttr.dev_path[0]) {
		if (strcmp(stDevAttr.dev_path, dev)) {
			LOG_ERROR("stDevAttr.dev_path[%s] != dev[%s]\n", stDevAttr.dev_path, dev);
			return -1;
		}
		sprintf(pHandle->stDevSta.dev_path, stDevAttr.dev_path);
	}

	ret = rkipc_storage_GetMountPath(dev, mountPath, RKIPC_MAX_FILE_PATH_LEN);
	if (ret) {
		LOG_ERROR("rkipc_storage_GetMountPath failed[%d]\n", ret);
		if (stDevAttr.dev_path[0]) {
			pHandle->stDevSta.s32MountStatus = DISK_NOT_FORMATTED;
		}
		return ret;
	}

	if (stDevAttr.mount_path[0]) {
		if (strcmp(stDevAttr.mount_path, mountPath)) {
			LOG_ERROR("stDevAttr.mount_path[%s] != mountPath[%s]\n", stDevAttr.mount_path,
			          mountPath);
			return -1;
		}
	} else {
		sprintf(stDevAttr.mount_path, mountPath);
	}

	ret = rkipc_storage_GetMountDev(stDevAttr.mount_path, pHandle->stDevSta.dev_path,
	                                pHandle->stDevSta.cDevType, pHandle->stDevSta.cDevAttr1);
	if (ret) {
		LOG_ERROR("rkipc_storage_GetMountDev failed[%d]\n", ret);
		return ret;
	}

	pHandle->stDevSta.s32MountStatus = DISK_SCANNING;
	if (pthread_create(&pHandle->stDevSta.fileScanTid, NULL, rkipc_storage_FileScanThread,
	                   (void *)pHandle))
		LOG_ERROR("FileScanThread create failed.\n");

	return 0;
}

static int rkipc_storage_DevRemove(char *dev, rkipc_storage_handle *pHandle) {
	rkipc_check_pointer(dev, rkipc_failure);
	rkipc_check_pointer(pHandle, rkipc_failure);

	if (!strcmp(pHandle->stDevSta.dev_path, dev)) {
		pHandle->stDevSta.s32MountStatus = DISK_UNMOUNTED;
		pHandle->stDevSta.s32TotalSize = 0;
		pHandle->stDevSta.s32FreeSize = 0;
		pHandle->stDevSta.s32FsckQuit = 1;

		if (pHandle->stDevSta.fileScanTid) {
			if (pthread_join(pHandle->stDevSta.fileScanTid, NULL))
				LOG_ERROR("FileScanThread join failed.");
			pHandle->stDevSta.fileScanTid = 0;
		}
		umount2(pHandle->stDevAttr.mount_path, MNT_DETACH);
	}

	return 0;
}

static int rkipc_storage_MsgPutMsgToBuffer(rkipc_tmsg_buffer *buf, rkipc_tmsg_element *elm) {
	rkipc_check_pointer(buf, rkipc_failure);
	rkipc_check_pointer(elm, rkipc_failure);

	if (NULL != elm->next)
		elm->next = NULL;

	pthread_mutex_lock(&buf->mutex);
	if (buf->first) {
		rkipc_tmsg_element *tmp = buf->first;
		while (tmp->next != NULL) {
			tmp = tmp->next;
		}
		tmp->next = elm;
	} else {
		buf->first = elm;
	}
	buf->num++;

	pthread_cond_signal(&buf->notEmpty);
	pthread_mutex_unlock(&buf->mutex);
	return 0;
}

static rkipc_tmsg_element *rkipc_storage_MsgGetMsgFromBufferTimeout(rkipc_tmsg_buffer *buf,
                                                                    int s32TimeoutMs) {
	rkipc_tmsg_element *elm = NULL;
	struct timeval timeNow;
	struct timespec timeout;

	if (!buf) {
		return NULL;
	}

	pthread_mutex_lock(&buf->mutex);
	if (0 == buf->num) {
		gettimeofday(&timeNow, NULL);
		timeout.tv_sec = timeNow.tv_sec + s32TimeoutMs / 1000;
		timeout.tv_nsec = (timeNow.tv_usec + (s32TimeoutMs % 1000) * 1000) * 1000;
		pthread_cond_timedwait(&buf->notEmpty, &buf->mutex, &timeout);
	}

	if (buf->num > 0) {
		elm = buf->first;
		if (1 == buf->num) {
			buf->first = buf->last = NULL;
			buf->num = 0;
		} else {
			buf->first = buf->first->next;
			buf->num--;
		}
	}

	pthread_mutex_unlock(&buf->mutex);
	return elm;
}

static int rkipc_storage_MsgFreeMsg(rkipc_tmsg_element *elm) {
	rkipc_check_pointer(elm, rkipc_failure);

	if (elm->data != NULL) {
		free(elm->data);
		elm->data = NULL;
	}
	free(elm);
	elm = NULL;

	return 0;
}

static void *rkipc_storage_MsgRecMsgThread(void *arg) {
	rkipc_tmsg_buffer *msgBuffer = (rkipc_tmsg_buffer *)arg;

	if (!msgBuffer) {
		LOG_ERROR("invalid msgBuffer");
		return NULL;
	}

	prctl(PR_SET_NAME, "rkipc_storage_MsgRecMsgThread", 0, 0, 0);
	while (msgBuffer->quit == 0) {
		rkipc_tmsg_element *elm = rkipc_storage_MsgGetMsgFromBufferTimeout(msgBuffer, 50);

		if (elm) {
			if (msgBuffer->recMsgCb)
				msgBuffer->recMsgCb(msgBuffer, elm->msg, elm->data, elm->s32DataLen,
				                    msgBuffer->pHandlePath);
			if (rkipc_storage_MsgFreeMsg(elm))
				LOG_ERROR("Free msg failed.");
		}
	}

	LOG_DEBUG("out");
	return NULL;
}

static int rkipc_storage_MsgRecCb(void *hd, int msg, void *data, int s32DataLen, void *pHandle) {
	LOG_INFO("msg = %d\n", msg);
	switch (msg) {
	case MSG_DEV_ADD:
		if (rkipc_storage_DevAdd((char *)data, (rkipc_storage_handle *)pHandle)) {
			LOG_ERROR("DevAdd failed\n");
			return -1;
		}
		break;
	case MSG_DEV_REMOVE:
		if (rkipc_storage_DevRemove((char *)data, (rkipc_storage_handle *)pHandle)) {
			LOG_ERROR("DevRemove failed\n");
			return -1;
		}
		break;
	case MSG_DEV_CHANGED:
		break;
	}

	return 0;
}

static int rkipc_storage_MsgCreate(rkipc_reg_msg_cb recMsgCb, rkipc_storage_handle *pHandle) {
	rkipc_check_pointer(pHandle, rkipc_failure);

	pHandle->stMsgHd.first = NULL;
	pHandle->stMsgHd.last = NULL;
	pHandle->stMsgHd.num = 0;
	pHandle->stMsgHd.quit = 0;
	pHandle->stMsgHd.recMsgCb = recMsgCb;
	pHandle->stMsgHd.pHandlePath = (void *)pHandle;

	pthread_mutex_init(&(pHandle->stMsgHd.mutex), NULL);
	pthread_cond_init(&(pHandle->stMsgHd.notEmpty), NULL);
	if (pthread_create(&(pHandle->stMsgHd.recTid), NULL, rkipc_storage_MsgRecMsgThread,
	                   (void *)(&pHandle->stMsgHd))) {
		LOG_ERROR("RecMsgThread create failed!");
		return -1;
	}

	return 0;
}

static int rkipc_storage_MsgDestroy(rkipc_storage_handle *pHandle) {
	rkipc_check_pointer(pHandle, rkipc_failure);

	pHandle->stMsgHd.quit = 1;
	if (pHandle->stMsgHd.recTid)
		if (pthread_join(pHandle->stMsgHd.recTid, NULL)) {
			LOG_ERROR("RecMsgThread join failed!");
			return -1;
		}

	return 0;
}

static int rkipc_storage_MsgSendMsg(int msg, char *data, int s32DataLen, rkipc_tmsg_buffer *buf) {
	rkipc_tmsg_element *elm = NULL;

	rkipc_check_pointer(buf, rkipc_failure);
	rkipc_check_pointer(data, rkipc_failure);

	elm = (rkipc_tmsg_element *)malloc(sizeof(rkipc_tmsg_element));
	if (!elm) {
		LOG_ERROR("elm malloc failed.");
		return -1;
	}

	memset(elm, 0, sizeof(rkipc_tmsg_element));
	elm->msg = msg;
	elm->data = NULL;
	elm->s32DataLen = s32DataLen;

	if (data && s32DataLen > 0) {
		elm->data = (char *)malloc(s32DataLen);
		if (!elm->data) {
			LOG_ERROR("elm->data malloc failed.");
			free(elm);
			return -1;
		}
		memset(elm->data, 0, s32DataLen);
		memcpy(elm->data, data, s32DataLen);
	}

	elm->next = NULL;

	if (rkipc_storage_MsgPutMsgToBuffer(buf, elm)) {
		if (!elm->data)
			free(elm->data);
		free(elm);
		LOG_ERROR("Put msg to buffer failed.");
		return -1;
	}

	return 0;
}

static char *rkipc_storage_Search(char *buf, int len, const char *str) {
	char *ret = 0;
	int i = 0;

	ret = strstr(buf, str);
	if (ret)
		return ret;
	for (i = 1; i < len; i++) {
		if (buf[i - 1] == 0) {
			ret = strstr(&buf[i], str);
			if (ret)
				return ret;
		}
	}
	return ret;
}

static char *rkipc_storage_Getparameters(char *buf, int len, const char *str) {
	char *ret = rkipc_storage_Search(buf, len, str);

	if (ret)
		ret += strlen(str) + 1;

	return ret;
}

static void *rkipc_storage_EventListenerThread(void *arg) {
	rkipc_storage_handle *pHandle = (rkipc_storage_handle *)arg;
	int sockfd;
	int len;
	int bufLen = 2000;
	char buf[bufLen];
	struct iovec iov;
	struct msghdr msg;
	struct sockaddr_nl sa;
	struct timeval timeout;

	if (!pHandle) {
		LOG_ERROR("invalid pHandle");
		return NULL;
	}

	timeout.tv_sec = 0;
	timeout.tv_usec = 100000;
	prctl(PR_SET_NAME, "event_monitor", 0, 0, 0);

	sa.nl_family = AF_NETLINK;
	sa.nl_groups = NETLINK_KOBJECT_UEVENT;
	sa.nl_pid = 0;
	iov.iov_base = (void *)buf;
	iov.iov_len = bufLen;
	memset(&msg, 0, sizeof(msg));
	msg.msg_name = (void *)&sa;
	msg.msg_namelen = sizeof(sa);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	sockfd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
	if (sockfd == -1) {
		LOG_ERROR("socket creating failed:%s\n", strerror(errno));
		return NULL;
	}

	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void *)&timeout,
	           (socklen_t)sizeof(struct timeval));

	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		LOG_ERROR("bind error:%s\n", strerror(errno));
		goto err_event_listener;
	}

	while (pHandle->eventListenerRun) {
		len = recvmsg(sockfd, &msg, 0);
		if (len < 0) {
			// LOG_WARN("receive time out");
		} else if (len < MAX_TYPE_NMSG_LEN || len > bufLen) {
			LOG_WARN("invalid message\n");
		} else {
			char *p = strstr(buf, "libudev");

			if (p == buf) {
				if (rkipc_storage_Search(buf, len, "DEVTYPE=partition") ||
				    rkipc_storage_Search(buf, len, "DEVTYPE=disk")) {
					char *dev = rkipc_storage_Getparameters(buf, len, "DEVNAME");

					if (rkipc_storage_Search(buf, len, "ACTION=add")) {
						if (rkipc_storage_MsgSendMsg(MSG_DEV_ADD, dev, strlen(dev) + 1,
						                             &(pHandle->stMsgHd)))
							LOG_ERROR("Send msg: MSG_DEV_ADD failed.\n");
					} else if (rkipc_storage_Search(buf, len, "ACTION=remove")) {
						LOG_INFO("%s remove\n", dev);
						if (rkipc_storage_MsgSendMsg(MSG_DEV_REMOVE, dev, strlen(dev) + 1,
						                             &(pHandle->stMsgHd)))
							LOG_ERROR("Send msg: MSG_DEV_REMOVE failed.");
					} else if (rkipc_storage_Search(buf, len, "ACTION=change")) {
						LOG_INFO("%s change\n", dev);
						if (rkipc_storage_MsgSendMsg(MSG_DEV_CHANGED, dev, strlen(dev) + 1,
						                             &(pHandle->stMsgHd)))
							LOG_ERROR("Send msg: MSG_DEV_CHANGED failed.\n");
					}
				}
			}
		}
	}
err_event_listener:
	if (close(sockfd))
		LOG_ERROR("Close sockfd failed.\n");

	LOG_DEBUG("out");
	return NULL;
}

static int rkipc_storage_ParameterInit(rkipc_storage_handle *pstHandle,
                                       rkipc_str_dev_attr *pstDevAttr) {
	int i, quota;
	const char *folder_name = NULL;
	const char *mount_path = NULL;

	rkipc_check_pointer(pstHandle, rkipc_failure);

	if (pstDevAttr) {
		if (pstDevAttr->pstFolderAttr) {
			sprintf(pstHandle->stDevAttr.mount_path, pstDevAttr->mount_path);
			sprintf(pstHandle->stDevAttr.dev_path, pstDevAttr->dev_path);
			pstHandle->stDevAttr.auto_delete = pstDevAttr->auto_delete;
			pstHandle->stDevAttr.free_size_del_min = pstDevAttr->free_size_del_min;
			pstHandle->stDevAttr.free_size_del_max = pstDevAttr->free_size_del_max;
			pstHandle->stDevAttr.folder_num = pstDevAttr->folder_num;
			pstHandle->stDevAttr.check_format_id = pstDevAttr->check_format_id;
			memcpy(pstHandle->stDevAttr.format_id, pstDevAttr->format_id, RKIPC_MAX_FORMAT_ID_LEN);
			memcpy(pstHandle->stDevAttr.volume, pstDevAttr->volume, RKIPC_MAX_VOLUME_LEN);

			pstHandle->stDevAttr.pstFolderAttr = (rkipc_str_folder_attr *)malloc(
			    sizeof(rkipc_str_folder_attr) * pstHandle->stDevAttr.folder_num);
			if (!pstHandle->stDevAttr.pstFolderAttr) {
				LOG_ERROR("pstHandle->stDevAttr.pstFolderAttr malloc failed.\n");
				return -1;
			}
			memset(pstHandle->stDevAttr.pstFolderAttr, 0,
			       sizeof(rkipc_str_folder_attr) * pstHandle->stDevAttr.folder_num);

			for (i = 0; i < pstDevAttr->folder_num; i++) {
				pstHandle->stDevAttr.pstFolderAttr[i].sort_cond =
				    pstDevAttr->pstFolderAttr[i].sort_cond;
				pstHandle->stDevAttr.pstFolderAttr[i].num_limit =
				    pstDevAttr->pstFolderAttr[i].num_limit;
				pstHandle->stDevAttr.pstFolderAttr[i].s32Limit =
				    pstDevAttr->pstFolderAttr[i].s32Limit;
				sprintf(pstHandle->stDevAttr.pstFolderAttr[i].folder_path,
				        pstDevAttr->pstFolderAttr[i].folder_path);
			}

			for (i = 0; i < pstDevAttr->folder_num; i++) {
				LOG_INFO("DevAttr set:  AutoDel--%d, FreeSizeDel--%d~%d, Path--%s%s, "
				         "Limit--%d\n",
				         pstHandle->stDevAttr.auto_delete, pstHandle->stDevAttr.free_size_del_min,
				         pstHandle->stDevAttr.free_size_del_max, pstHandle->stDevAttr.mount_path,
				         pstHandle->stDevAttr.pstFolderAttr[i].folder_path,
				         pstHandle->stDevAttr.pstFolderAttr[i].s32Limit);
			}

			LOG_DEBUG("Set user-defined device attributes done.\n");
			return 0;
		} else {
			LOG_ERROR("The device attributes set failed.\n");
			return -1;
		}
	}

	LOG_DEBUG("Set default device attributes.\n");
	mount_path = rk_param_get_string("storage.0:mount_path", "/data");
	sprintf(pstHandle->stDevAttr.mount_path, mount_path);
	sprintf(pstHandle->stDevAttr.dev_path, "dev/mmcblk2"); /// dev/mmcblk2p1
	pstHandle->stDevAttr.auto_delete = 1;
	pstHandle->stDevAttr.free_size_del_min = 500;
	pstHandle->stDevAttr.free_size_del_max = 1000;
	pstHandle->stDevAttr.folder_num = 3;
	pstHandle->stDevAttr.pstFolderAttr = (rkipc_str_folder_attr *)malloc(
	    sizeof(rkipc_str_folder_attr) * pstHandle->stDevAttr.folder_num);

	if (!pstHandle->stDevAttr.pstFolderAttr) {
		LOG_ERROR("stDevAttr.pstFolderAttr malloc failed.\n");
		return -1;
	}
	memset(pstHandle->stDevAttr.pstFolderAttr, 0,
	       sizeof(rkipc_str_folder_attr) * pstHandle->stDevAttr.folder_num);

	quota = rk_param_get_int("storage.0:video_quota", 30);
	folder_name = rk_param_get_string("storage.0:folder_name", NULL);
	pstHandle->stDevAttr.pstFolderAttr[0].sort_cond = SORT_FILE_NAME;
	pstHandle->stDevAttr.pstFolderAttr[0].num_limit = false;
	pstHandle->stDevAttr.pstFolderAttr[0].s32Limit = quota;
	sprintf(pstHandle->stDevAttr.pstFolderAttr[0].folder_path, folder_name);

	quota = rk_param_get_int("storage.1:video_quota", 30);
	folder_name = rk_param_get_string("storage.1:folder_name", NULL);
	pstHandle->stDevAttr.pstFolderAttr[1].sort_cond = SORT_FILE_NAME;
	pstHandle->stDevAttr.pstFolderAttr[1].num_limit = false;
	pstHandle->stDevAttr.pstFolderAttr[1].s32Limit = quota;
	sprintf(pstHandle->stDevAttr.pstFolderAttr[1].folder_path, folder_name);

	quota = rk_param_get_int("storage.2:video_quota", 30);
	folder_name = rk_param_get_string("storage.2:folder_name", NULL);
	pstHandle->stDevAttr.pstFolderAttr[2].sort_cond = SORT_FILE_NAME;
	pstHandle->stDevAttr.pstFolderAttr[2].num_limit = false;
	pstHandle->stDevAttr.pstFolderAttr[2].s32Limit = quota;
	sprintf(pstHandle->stDevAttr.pstFolderAttr[2].folder_path, folder_name);

	for (i = 0; i < pstHandle->stDevAttr.folder_num; i++) {
		LOG_INFO("DevAttr set:  AutoDel--%d, FreeSizeDel--%d~%d, Path--%s%s, Limit--%d\n",
		         pstHandle->stDevAttr.auto_delete, pstHandle->stDevAttr.free_size_del_min,
		         pstHandle->stDevAttr.free_size_del_max, pstHandle->stDevAttr.mount_path,
		         pstHandle->stDevAttr.pstFolderAttr[i].folder_path,
		         pstHandle->stDevAttr.pstFolderAttr[i].s32Limit);
	}

	return 0;
}

static int rkipc_storage_ParameterDeinit(rkipc_storage_handle *pHandle) {
	rkipc_check_pointer(pHandle, rkipc_failure);

	if (pHandle->stDevAttr.pstFolderAttr) {
		free(pHandle->stDevAttr.pstFolderAttr);
		pHandle->stDevAttr.pstFolderAttr = NULL;
	}

	return 0;
}

static int rkipc_storage_AutoDeleteInit(rkipc_storage_handle *pstHandle) {
	rkipc_str_dev_attr stDevAttr;
	LOG_INFO("rkipc_storage_AutoDeleteInit\n");
	rkipc_check_pointer(pstHandle, rkipc_failure);
	stDevAttr = rkipc_storage_GetParam(pstHandle);
	LOG_INFO("mountpath:%s,devpath:%s,devtype:%s,devattr:%s\n", stDevAttr.mount_path,
	         pstHandle->stDevSta.dev_path, pstHandle->stDevSta.cDevType,
	         pstHandle->stDevSta.cDevAttr1);
	if (!rkipc_storage_GetMountDev(stDevAttr.mount_path, pstHandle->stDevSta.dev_path,
	                               pstHandle->stDevSta.cDevType, pstHandle->stDevSta.cDevAttr1)) {
		pstHandle->stDevSta.s32MountStatus = DISK_SCANNING;
		if (pthread_create(&(pstHandle->stDevSta.fileScanTid), NULL, rkipc_storage_FileScanThread,
		                   (void *)(pstHandle))) {
			LOG_ERROR("FileScanThread create failed.\n");
			return -1;
		}
	} else {
		pstHandle->stDevSta.s32MountStatus = DISK_UNMOUNTED;
		LOG_ERROR("GetMountDev failed.\n");
		return -1;
	}

	return 0;
}

static int rkipc_storage_AutoDeleteDeinit(rkipc_storage_handle *pHandle) {
	rkipc_check_pointer(pHandle, rkipc_failure);

	pHandle->stDevSta.s32MountStatus = DISK_UNMOUNTED;

	if (pHandle->stDevSta.fileScanTid)
		if (pthread_join(pHandle->stDevSta.fileScanTid, NULL))
			LOG_ERROR("FileScanThread join failed.\n");

	return 0;
}

static int rkipc_storage_ListenMsgInit(rkipc_storage_handle *pstHandle) {
	rkipc_check_pointer(pstHandle, rkipc_failure);

	pstHandle->eventListenerRun = 1;

	if (rkipc_storage_MsgCreate(&rkipc_storage_MsgRecCb, pstHandle)) {
		LOG_ERROR("Msg create failed.");
		return -1;
	}

	if (pthread_create(&pstHandle->eventListenerTid, NULL, rkipc_storage_EventListenerThread,
	                   (void *)pstHandle)) {
		LOG_ERROR("EventListenerThread create failed.");
		return -1;
	}

	return 0;
}

int rkipc_storage_func_Init(void **ppHandle, rkipc_str_dev_attr *pstDevAttr) {
	rkipc_storage_handle *pstHandle = NULL;

	if (*ppHandle) {
		LOG_ERROR("Storage handle has been inited.\n");
		return -1;
	}

	pstHandle = (rkipc_storage_handle *)malloc(sizeof(rkipc_storage_handle));
	if (!pstHandle) {
		LOG_ERROR("pstHandle malloc failed.\n");
		return -1;
	}
	memset(pstHandle, 0, sizeof(rkipc_storage_handle));

	if (rkipc_storage_ParameterInit(pstHandle, pstDevAttr)) {
		LOG_ERROR("Parameter init failed.\n");
		goto failed;
	}

	if (rkipc_storage_AutoDeleteInit(pstHandle))
		LOG_ERROR("AutoDelete init failed.\n");

	if (rkipc_storage_ListenMsgInit(pstHandle)) {
		LOG_ERROR("Listener and Msg init failed.\n");
		goto failed;
	}

	*ppHandle = (void *)pstHandle;
	return 0;

failed:
	if (pstHandle) {
		free(pstHandle);
	}

	return -1;
}

int rkipc_storage_func_deinit(void *pHandle) {
	rkipc_storage_handle *pstHandle = NULL;

	rkipc_check_pointer(pHandle, rkipc_failure);
	pstHandle = (rkipc_storage_handle *)pHandle;
	pstHandle->eventListenerRun = 0;
	pstHandle->stDevSta.s32FsckQuit = 1;

	if (pstHandle->eventListenerTid)
		if (pthread_join(pstHandle->eventListenerTid, NULL))
			LOG_ERROR("EventListenerThread join failed.");

	if (rkipc_storage_MsgDestroy(pstHandle))
		LOG_ERROR("Msg destroy failed.");

	if (rkipc_storage_AutoDeleteDeinit(pstHandle))
		LOG_ERROR("AutoDelete deinit failed.");

	if (rkipc_storage_ParameterDeinit(pstHandle))
		LOG_ERROR("Paramete deinit failed.");

	free(pstHandle);
	pstHandle = NULL;

	return 0;
}

rkipc_mount_status rkipc_storage_GetMountStatus(void *pHandle) {
	rkipc_storage_handle *pstHandle;

	rkipc_check_pointer(pHandle, DISK_MOUNT_BUTT);
	pstHandle = (rkipc_storage_handle *)pHandle;
	return pstHandle->stDevSta.s32MountStatus;
}

int rkipc_storage_GetCapacity(void **ppHandle, int *totalSize, int *freeSize) {
	rkipc_storage_handle *pstHandle = NULL;
	rkipc_str_dev_attr stDevAttr;

	rkipc_check_pointer(ppHandle, rkipc_failure);
	rkipc_check_pointer(*ppHandle, rkipc_failure);
	rkipc_check_pointer(totalSize, rkipc_failure);
	rkipc_check_pointer(freeSize, rkipc_failure);
	pstHandle = (rkipc_storage_handle *)*ppHandle;
	stDevAttr = rkipc_storage_GetParam(pstHandle);

	if (pstHandle->stDevSta.s32MountStatus == DISK_MOUNTED) {
		rkipc_storage_GetDiskSize(stDevAttr.mount_path, &pstHandle->stDevSta.s32TotalSize,
		                          &pstHandle->stDevSta.s32FreeSize);
	} else {
		pstHandle->stDevSta.s32TotalSize = 0;
		pstHandle->stDevSta.s32FreeSize = 0;
	}
	*totalSize = pstHandle->stDevSta.s32TotalSize;
	*freeSize = pstHandle->stDevSta.s32FreeSize;

	*ppHandle = (void *)pstHandle;
	return 0;
}

int rkipc_storage_GetFileList(rkipc_filelist *list, void *pHandle, rkipc_sort_type sort) {
	int i, j;
	rkipc_storage_handle *pstHandle = NULL;

	rkipc_check_pointer(list, rkipc_failure);
	rkipc_check_pointer(pHandle, rkipc_failure);
	pstHandle = (rkipc_storage_handle *)pHandle;

	for (i = 0; i < pstHandle->stDevSta.folder_num; i++) {
		if (!strcmp(list->path, pstHandle->stDevSta.pstFolder[i].cpath))
			break;
	}

	if (i == pstHandle->stDevSta.folder_num) {
		LOG_ERROR("No folder found. Please check the folder path.\n");
		return -1;
	}

	pthread_mutex_lock(&pstHandle->stDevSta.pstFolder[i].mutex);

	rkipc_str_file *tmp = pstHandle->stDevSta.pstFolder[i].pstFileListFirst;
	list->file_num = pstHandle->stDevSta.pstFolder[i].file_num;
	list->file = (rkipc_fileinfo *)malloc(sizeof(rkipc_fileinfo) * list->file_num);
	if (!list->file) {
		LOG_ERROR("list->file malloc failed.");
		return -1;
	}
	memset(list->file, 0, sizeof(rkipc_fileinfo) * list->file_num);

	if (sort == LIST_ASCENDING) {
		for (j = 0; j < list->file_num && tmp != NULL; j++) {
			int len = strlen(tmp->filename) > (RKIPC_MAX_FILE_PATH_LEN - 1)
			              ? (RKIPC_MAX_FILE_PATH_LEN - 1)
			              : strlen(tmp->filename);
			strncpy(list->file[j].filename, tmp->filename, len);
			list->file[j].filename[len] = '\0';
			list->file[j].stSize = tmp->stSize;
			list->file[j].stTime = tmp->stTime;
			tmp = tmp->next;
		}
	} else {
		for (j = list->file_num - 1; j >= 0 && tmp != NULL; j--) {
			int len = strlen(tmp->filename) > (RKIPC_MAX_FILE_PATH_LEN - 1)
			              ? (RKIPC_MAX_FILE_PATH_LEN - 1)
			              : strlen(tmp->filename);
			strncpy(list->file[j].filename, tmp->filename, len);
			list->file[j].filename[len] = '\0';
			list->file[j].stSize = tmp->stSize;
			list->file[j].stTime = tmp->stTime;
			tmp = tmp->next;
		}
	}

	pthread_mutex_unlock(&pstHandle->stDevSta.pstFolder[i].mutex);
	return 0;
}

int rkipc_storage_FreeFileList(rkipc_filelist *list) {
	if (list->file) {
		free(list->file);
		list->file = NULL;
	}

	return 0;
}

int rkipc_storage_GetFileNum(char *fileListPath, void *pHandle) {
	int i;
	rkipc_storage_handle *pstHandle = NULL;

	rkipc_check_pointer(pHandle, rkipc_failure);
	pstHandle = (rkipc_storage_handle *)pHandle;

	for (i = 0; i < pstHandle->stDevSta.folder_num; i++) {
		if (!strcmp(fileListPath, pstHandle->stDevSta.pstFolder[i].cpath))
			break;
	}

	if (i == pstHandle->stDevSta.folder_num)
		return 0;

	return pstHandle->stDevSta.pstFolder[i].file_num;
}

char *rkipc_storage_GetDevPath(void *pHandle) {
	rkipc_storage_handle *pstHandle = NULL;

	rkipc_check_pointer(pHandle, NULL);
	pstHandle = (rkipc_storage_handle *)pHandle;

	return pstHandle->stDevSta.dev_path;
}

int rkipc_storage_Format(void *pHandle, char *cFormat) {
	int err = 0;
	rkipc_storage_handle *pstHandle = NULL;

	rkipc_check_pointer(pHandle, rkipc_failure);
	pstHandle = (rkipc_storage_handle *)pHandle;

	if (pstHandle->stDevSta.s32MountStatus != DISK_UNMOUNTED) {
		int ret = 0;
		sync();

		if (!pstHandle->stDevAttr.dev_path[0])
			return -1;

		if (pstHandle->stDevSta.s32MountStatus != DISK_NOT_FORMATTED)
			rkipc_storage_DevRemove(pstHandle->stDevAttr.dev_path, pstHandle);

		ret = rkfsmk_format_ex(pstHandle->stDevAttr.dev_path, pstHandle->stDevAttr.volume,
		                       pstHandle->stDevAttr.format_id);
		if (!ret)
			err = -1;
		ret = mount(pstHandle->stDevAttr.dev_path, pstHandle->stDevAttr.mount_path, cFormat,
		            MS_NOATIME | MS_NOSUID, NULL);
		if (ret == 0)
			rkipc_storage_DevAdd(pstHandle->stDevAttr.dev_path, pstHandle);
		else
			err = -1;
	}

	return err;
}

// get quota ,response set quota by id
char *rkipc_get_quota_info(int id) {

	cJSON *Array = NULL;
	cJSON *sd_info = NULL;
	Array = cJSON_CreateArray();
	cJSON_AddItemToArray(Array, sd_info = cJSON_CreateObject());
	cJSON_AddNumberToObject(sd_info, "iFreePictureQuota", 0);
	cJSON_AddNumberToObject(sd_info, "iFreeVideoQuota", 0);
	cJSON_AddNumberToObject(sd_info, "iPictureQuotaRatio", sd_DevAttr.pstFolderAttr[1].s32Limit);
	cJSON_AddStringToObject(sd_info, "iTotalPictureVolume", sd_DevAttr.volume);
	cJSON_AddStringToObject(sd_info, "iTotalVideoVolume", sd_DevAttr.volume);
	cJSON_AddNumberToObject(sd_info, "iVideoQuotaRatio", sd_DevAttr.pstFolderAttr[0].s32Limit);
	cJSON_AddNumberToObject(sd_info, "id", 0);

	char *out = cJSON_Print(sd_info);
	cJSON_Delete(sd_info);

	return out;
}
// get hdd_list
char *rkipc_get_hdd_list(int id) {
	int totalSize;
	int freeSize;
	rkipc_storage_handle *phandle = (rkipc_storage_handle *)sd_phandle;
	rkipc_storage_GetCapacity(sd_phandle, &totalSize, &freeSize);
	char status[12];
	if (phandle->stDevSta.s32MountStatus == DISK_MOUNTED) {
		strcpy(status, "mounted");
	} else {
		strcpy(status, "unmounted");
	}
	cJSON *Array = NULL;
	cJSON *sd_info = NULL;
	Array = cJSON_CreateArray();
	cJSON_AddItemToArray(Array, sd_info = cJSON_CreateObject());
	cJSON_AddNumberToObject(sd_info, "iFormatProg", 0);
	cJSON_AddNumberToObject(sd_info, "iFormatStatus", 0);
	cJSON_AddNumberToObject(sd_info, "iMediaSize", 0);
	cJSON_AddNumberToObject(sd_info, "iFreeSize", freeSize);
	cJSON_AddNumberToObject(sd_info, "iTotalSize", totalSize);
	cJSON_AddNumberToObject(sd_info, "id", 0);
	cJSON_AddStringToObject(sd_info, "sDev", "");
	cJSON_AddStringToObject(sd_info, "sFormatErr", "");
	cJSON_AddStringToObject(sd_info, "sMountPath", sd_DevAttr.mount_path);
	cJSON_AddStringToObject(sd_info, "sName", "SD Card");
	cJSON_AddStringToObject(sd_info, "sStatus", status);
	cJSON_AddStringToObject(sd_info, "sType", "");

	char *out = cJSON_Print(sd_info);
	cJSON_Delete(sd_info);

	return out;
}
// response set snap_plan by id
char *rkipc_get_snap_plan_by_id(int id) {
	cJSON *plan_info = NULL;
	plan_info = cJSON_CreateObject();
	cJSON_AddNumberToObject(plan_info, "iEnabled", 0);
	cJSON_AddNumberToObject(plan_info, "iImageQuality", 10);
	cJSON_AddNumberToObject(plan_info, "iShotInterval", 1000);
	cJSON_AddNumberToObject(plan_info, "iShotNumber", 4);
	cJSON_AddStringToObject(plan_info, "sImageType", "JPEG");
	cJSON_AddStringToObject(plan_info, "sResolution", "2688*1520");

	char *out = cJSON_Print(plan_info);
	cJSON_Delete(plan_info);

	return out;
}

char *rkipc_get_current_path() {
	cJSON *path_info = NULL;
	path_info = cJSON_CreateObject();
	cJSON_AddStringToObject(path_info, "sMountPath", sd_DevAttr.mount_path);
	char *out = cJSON_Print(path_info);
	cJSON_Delete(path_info);

	return out;
}
// search
// set advanced para
char *rkipc_get_advanced_para() {
	cJSON *para_info = NULL;
	para_info = cJSON_CreateObject();
	cJSON_AddNumberToObject(para_info, "iEnabled", 0);
	cJSON_AddNumberToObject(para_info, "id", 0);
	char *out = cJSON_Print(para_info);
	cJSON_Delete(para_info);

	return out;
}
// delete
char *rkipc_response_delete(int num, int id, char *name_list) {
	int ret;
	cJSON *del_info = NULL;
	del_info = cJSON_CreateObject();
	rkipc_storage_handle *phandle = (rkipc_storage_handle *)sd_phandle;
	if (id == 0) {
		for (int i = 0; i < num; i++) {
			ret = rkipc_storage_FileListDel(&phandle->stDevSta.pstFolder[0], &name_list[i]);
			if (ret) {
				LOG_ERROR("delete %s failed!\n", name_list[i]);
				cJSON_AddNumberToObject(del_info, "rst", 0);
				char *out = cJSON_Print(del_info);
				cJSON_Delete(del_info);
				return out;
			}
		}
	} else if (id == 1) {
		for (int i = 0; i < num; i++) {
			ret = rkipc_storage_FileListDel(&phandle->stDevSta.pstFolder[1], &name_list[i]);
			if (ret) {
				LOG_ERROR("delete %s failed!\n", name_list[i]);
				cJSON_AddNumberToObject(del_info, "rst", 0);
				char *out = cJSON_Print(del_info);
				cJSON_Delete(del_info);
				return out;
			}
		}
	} else if (id == 2) {
		for (int i = 0; i < num; i++) {
			ret = rkipc_storage_FileListDel(&phandle->stDevSta.pstFolder[2], &name_list[i]);
			if (ret) {
				LOG_ERROR("delete %s failed!\n", name_list[i]);
				cJSON_AddNumberToObject(del_info, "rst", 0);
				char *out = cJSON_Print(del_info);
				cJSON_Delete(del_info);
				return out;
			}
		}
	}
	cJSON_AddNumberToObject(del_info, "rst", 1);
	char *out = cJSON_Print(del_info);
	cJSON_Delete(del_info);
	return out;
}

static void *rk_storage_record(void *arg) {
	int *id_ptr = arg;
	int id = *id_ptr;
	printf("#Start %s thread, arg:%p\n", __func__, arg);

	while (rk_storage_group[id].g_record_run_) {
		time_t t = time(NULL);
		struct tm tm = *localtime(&t);
		snprintf(rk_storage_group[id].file_name, 128, "%s/%d%02d%02d%02d%02d%02d.%s",
		         rk_storage_group[id].file_path, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		         tm.tm_hour, tm.tm_min, tm.tm_sec, rk_storage_group[id].file_format);
		LOG_INFO("[%d], file_name is %s\n", id, rk_storage_group[id].file_name);
		rkmuxer_deinit(id);
		rkmuxer_init(id, NULL, rk_storage_group[id].file_name, &rk_storage_group[id].g_video_param,
		             &rk_storage_group[id].g_audio_param);
		rk_signal_wait(rk_storage_group[id].g_storage_signal,
		               rk_storage_group[id].file_duration * 1000);
	}
	rkmuxer_deinit(id);
	free(rk_storage_group[id].file_path);

	return NULL;
}

static size_t rkipc_stringcopy(char *dst, char *src, size_t siz) {
	if ((int)siz <= 0)
		return strlen(src);
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0) {
		while (--n != 0) {
			if ((*d++ = *s++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0'; /* NUL-terminate dst */
		while (*s++)
			;
	}

	return (s - src - 1); /* count does not include NUL */
}

int rk_storage_init_by_id(int id) {
	LOG_INFO("begin\n");
	int pos = 0;
	char entry[128] = {'\0'};
	const char *mount_path = NULL;
	const char *folder_name = NULL;
	rk_storage_group[id].file_path = malloc(128);

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
		memcpy(rk_storage_group[id].g_video_param.codec, output_data_type,
		       strlen(output_data_type));
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

	snprintf(entry, 127, "storage.%d:mount_path", id);
	mount_path = rk_param_get_string(entry, "/userdata");
	snprintf(entry, 127, "storage.%d:folder_name", id);
	folder_name = rk_param_get_string(entry, NULL);

	pos += rkipc_stringcopy(&rk_storage_group[id].file_path[pos], mount_path,
	                        pos < 128 ? 128 - pos : 0);
	pos += rkipc_stringcopy(&rk_storage_group[id].file_path[pos], folder_name,
	                        pos < 128 ? 128 - pos : 0);
	LOG_INFO("----filepath:%s\n", rk_storage_group[id].file_path);
	// rk_storage_group[id].file_path = rk_param_get_string(entry, "/userdata");
	// create file_path if no exit
	DIR *d = opendir(rk_storage_group[id].file_path);
	if (d == NULL) {
		if (mkdir(rk_storage_group[id].file_path, 0777) == -1) {
			LOG_ERROR("Create %s fail\n", rk_storage_group[id].file_path);
			return -1;
		}
	} else {
		closedir(d);
	}
	pos = 0;

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
	for (int id = 0; id < STORAGE_NUM; id++) {
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

int rkipc_SetDevAttr(rkipc_str_dev_attr *pstDevAttr) {
	int i, quota;
	const char *folder_name = NULL;
	const char *mount_path = NULL;
	rkipc_check_pointer(pstDevAttr, rkipc_failure);
	LOG_DEBUG("The DevAttr will be user-defined.\n");

	memset(pstDevAttr, 0, sizeof(rkipc_str_dev_attr));
	mount_path = rk_param_get_string("storage.0:mount_path", "/data");
	sprintf(pstDevAttr->mount_path, mount_path);
	pstDevAttr->auto_delete = 1;
	pstDevAttr->free_size_del_min = 500;
	pstDevAttr->free_size_del_max = 1000;
	pstDevAttr->folder_num = 3;
	pstDevAttr->pstFolderAttr =
	    (rkipc_str_folder_attr *)malloc(sizeof(rkipc_str_folder_attr) * pstDevAttr->folder_num);

	if (!pstDevAttr->pstFolderAttr) {
		LOG_ERROR("pstDevAttr->pstFolderAttr malloc failed.\n");
		return -1;
	}
	memset(pstDevAttr->pstFolderAttr, 0, sizeof(rkipc_str_folder_attr) * pstDevAttr->folder_num);

	quota = rk_param_get_int("storage.0:video_quota", 30);
	folder_name = rk_param_get_string("storage.0:folder_name", NULL);
	pstDevAttr->pstFolderAttr[0].sort_cond = SORT_FILE_NAME;
	pstDevAttr->pstFolderAttr[0].num_limit = false;
	pstDevAttr->pstFolderAttr[0].s32Limit = quota;
	sprintf(pstDevAttr->pstFolderAttr[0].folder_path, folder_name);

	quota = rk_param_get_int("storage.1:video_quota", 30);
	folder_name = rk_param_get_string("storage.1:folder_name", NULL);
	pstDevAttr->pstFolderAttr[1].sort_cond = SORT_FILE_NAME;
	pstDevAttr->pstFolderAttr[1].num_limit = false;
	pstDevAttr->pstFolderAttr[1].s32Limit = quota;
	sprintf(pstDevAttr->pstFolderAttr[1].folder_path, folder_name);

	quota = rk_param_get_int("storage.2:video_quota", 30);
	folder_name = rk_param_get_string("storage.2:folder_name", NULL);
	pstDevAttr->pstFolderAttr[2].sort_cond = SORT_FILE_NAME;
	pstDevAttr->pstFolderAttr[2].num_limit = false;
	pstDevAttr->pstFolderAttr[2].s32Limit = quota;
	sprintf(pstDevAttr->pstFolderAttr[2].folder_path, folder_name);

	return 0;
}

int rkipc_FreeDevAttr(rkipc_str_dev_attr devAttr) {
	if (devAttr.pstFolderAttr) {
		free(devAttr.pstFolderAttr);
		devAttr.pstFolderAttr = NULL;
	}

	return 0;
}

// TODO, need record plan
int rk_storage_init() {
	for (int i = 0; i < STORAGE_NUM; i++) {
		LOG_INFO("i:%d\n", i);
		rk_storage_init_by_id(i);
	}
	if (rkipc_SetDevAttr(&sd_DevAttr)) {
		LOG_ERROR("Set devAttr failed.\n");
		return -1;
	}

	if (rkipc_storage_func_Init(&sd_phandle, &sd_DevAttr)) {
		LOG_ERROR("Storage init failed.\n");
		return -1;
	}

	return 0;
}

int rk_storage_deinit() {
	for (int i = 0; i < STORAGE_NUM; i++) {
		rk_storage_deinit_by_id(i);
	}
	rkipc_FreeDevAttr(sd_DevAttr);
	rkipc_storage_func_deinit(sd_phandle);
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

	snprintf(rk_storage_group[0].file_name, 128, "%s/%d%02d%02d%02d%02d%02d.%s",
	         rk_storage_group[0].file_path, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	         tm.tm_hour, tm.tm_min, tm.tm_sec, rk_storage_group[0].file_format);
	LOG_INFO("file_name is %s\n", rk_storage_group[0].file_name);
	rkmuxer_deinit(0);
	rkmuxer_init(0, NULL, rk_storage_group[0].file_name, &rk_storage_group[0].g_video_param,
	             &rk_storage_group[0].g_audio_param);
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
