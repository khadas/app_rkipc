// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "common.h"
#include "log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "audio.c"

void rk_audio_init() { LOG_INFO("%s\n", __func__); }

void rk_audio_deinit() { LOG_INFO("%s\n", __func__); }