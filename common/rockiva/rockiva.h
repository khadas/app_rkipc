// Copyright 2020-2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __RKIPC_ROCKIVA_H__
#define __RKIPC_ROCKIVA_H__

#ifdef __cplusplus
extern "C" {
#endif

int rkipc_rockiva_init();
int rkipc_rockiva_deinit();
int rkipc_rockiva_write_rgb888_frame(uint16_t width, uint16_t height, uint32_t frame_id,
                                     unsigned char *buffer);
int rkipc_rockiva_write_rgb888_frame_by_fd(uint16_t width, uint16_t height, uint32_t frame_id,
                                           int32_t fd);
#ifdef __cplusplus
}
#endif
#endif
