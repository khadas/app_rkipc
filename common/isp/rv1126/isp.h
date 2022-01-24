// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
int rk_isp_init(int cam_id, char *iqfile_path);
int rk_isp_deinit(int cam_id);
// image adjustment
int rk_isp_get_contrast(int cam_id, int *value);
int rk_isp_set_contrast(int cam_id, int value);
int rk_isp_get_brightness(int cam_id, int *value);
int rk_isp_set_brightness(int cam_id, int value);
int rk_isp_get_saturation(int cam_id, int *value);
int rk_isp_set_saturation(int cam_id, int value);
int rk_isp_get_sharpness(int cam_id, int *value);
int rk_isp_set_sharpness(int cam_id, int value);
int rk_isp_get_hue(int cam_id, int *value);
int rk_isp_set_hue(int cam_id, int value);
// exposure
int rk_isp_get_exposure_mode(int cam_id, const char **value);
int rk_isp_set_exposure_mode(int cam_id, const char *value);
int rk_isp_get_gain_mode(int cam_id, const char **value);
int rk_isp_set_gain_mode(int cam_id, const char *value);
int rk_isp_get_exposure_time(int cam_id, const char **value);
int rk_isp_set_exposure_time(int cam_id, const char *value);
int rk_isp_get_exposure_gain(int cam_id, int *value);
int rk_isp_set_exposure_gain(int cam_id, int value);
// night_to_day
// blc
int rk_isp_get_hdr(int cam_id, const char **value);
int rk_isp_set_hdr(int cam_id, const char *value);
int rk_isp_get_blc_region(int cam_id, const char **value);
int rk_isp_set_blc_region(int cam_id, const char *value);
int rk_isp_get_hlc(int cam_id, const char **value);
int rk_isp_set_hlc(int cam_id, const char *value);
int rk_isp_get_hdr_level(int cam_id, int *value);
int rk_isp_set_hdr_level(int cam_id, int value);
int rk_isp_get_blc_strength(int cam_id, int *value);
int rk_isp_set_blc_strength(int cam_id, int value);
int rk_isp_get_hlc_level(int cam_id, int *value);
int rk_isp_set_hlc_level(int cam_id, int value);
int rk_isp_get_dark_boost_level(int cam_id, int *value);
int rk_isp_set_dark_boost_level(int cam_id, int value);
// white_blance
int rk_isp_get_white_blance_style(int cam_id, const char **value);
int rk_isp_set_white_blance_style(int cam_id, const char *value);
int rk_isp_get_white_blance_red(int cam_id, int *value);
int rk_isp_set_white_blance_red(int cam_id, int value);
int rk_isp_get_white_blance_green(int cam_id, int *value);
int rk_isp_set_white_blance_green(int cam_id, int value);
int rk_isp_get_white_blance_blue(int cam_id, int *value);
int rk_isp_set_white_blance_blue(int cam_id, int value);
// enhancement
int rk_isp_get_noise_reduce_mode(int cam_id, const char **value);
int rk_isp_set_noise_reduce_mode(int cam_id, const char *value);
int rk_isp_get_dehaze(int cam_id, const char **value);
int rk_isp_set_dehaze(int cam_id, const char *value);
int rk_isp_get_gray_scale_mode(int cam_id, const char **value);
// int rk_isp_set_gray_scale_mode(int cam_id, const char *value);
int rk_isp_get_distortion_correction(int cam_id, const char **value);
int rk_isp_set_distortion_correction(int cam_id, const char *value);
int rk_isp_get_spatial_denoise_level(int cam_id, int *value);
int rk_isp_set_spatial_denoise_level(int cam_id, int value);
int rk_isp_get_temporal_denoise_level(int cam_id, int *value);
int rk_isp_set_temporal_denoise_level(int cam_id, int value);
int rk_isp_get_dehaze_level(int cam_id, int *value);
int rk_isp_set_dehaze_level(int cam_id, int value);
int rk_isp_get_fec_level(int cam_id, int *value);
int rk_isp_set_fec_level(int cam_id, int value);
int rk_isp_get_ldch_level(int cam_id, int *value);
int rk_isp_set_ldch_level(int cam_id, int value);
// video_adjustment
int rk_isp_get_power_line_frequency_mode(int cam_id, const char **value);
int rk_isp_set_power_line_frequency_mode(int cam_id, const char *value);
int rk_isp_get_image_flip(int cam_id, const char **value);
int rk_isp_set_image_flip(int cam_id, const char *value);
