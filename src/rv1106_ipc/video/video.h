// Copyright 2022 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
int rk_video_init();
int rk_video_deinit();
int rk_video_restart();
int rk_video_get_gop(int stream_id, int *value);
int rk_video_set_gop(int stream_id, int value);
int rk_video_get_max_rate(int stream_id, int *value);
int rk_video_set_max_rate(int stream_id, int value);
int rk_video_get_RC_mode(int stream_id, const char **value);
int rk_video_set_RC_mode(int stream_id, const char *value);
int rk_video_get_output_data_type(int stream_id, const char **value);
int rk_video_set_output_data_type(int stream_id, const char *value);
int rk_video_get_rc_quality(int stream_id, const char **value);
int rk_video_set_rc_quality(int stream_id, const char *value);
int rk_video_get_smart(int stream_id, const char **value);
int rk_video_set_smart(int stream_id, const char *value);
int rk_video_get_svc(int stream_id, const char **value);
int rk_video_set_svc(int stream_id, const char *value);
int rk_video_get_stream_type(int stream_id, const char **value);
int rk_video_set_stream_type(int stream_id, const char *value);
int rk_video_get_h264_profile(int stream_id, const char **value);
int rk_video_set_h264_profile(int stream_id, const char *value);
int rk_video_get_resolution(int stream_id, char **value);
int rk_video_set_resolution(int stream_id, const char *value);
int rk_video_get_frame_rate(int stream_id, char **value);
int rk_video_set_frame_rate(int stream_id, const char *value);
int rk_video_get_frame_rate_in(int stream_id, char **value);
int rk_video_set_frame_rate_in(int stream_id, const char *value);
int rk_take_photo();