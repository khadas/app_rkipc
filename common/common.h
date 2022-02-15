// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>

#include "iniparser.h"
#include "log.h"
#include "param.h"

void *rk_signal_create(int defval, int maxval);
void rk_signal_destroy(void *sem);
int rk_signal_wait(void *sem, int timeout);
void rk_signal_give(void *sem);
void rk_signal_reset(void *sem);