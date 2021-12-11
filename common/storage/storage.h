// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
int rk_storage_init();
int rk_storage_deinit();
int rk_storage_write_video_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                                 int64_t present_time, int key_frame);
int rk_storage_write_audio_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                                 int64_t present_time);
int rk_storage_record_start();
int rk_storage_record_stop();
int rk_stoarge_record_statue_get(int *value);