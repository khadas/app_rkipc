/****************************************************************************
 *
 *    Copyright (c) 2021 by Rockchip Corp.  All rights reserved.
 *
 *    The material in this file is confidential and contains trade secrets
 *    of Rockchip Corporation. This is proprietary information owned by
 *    Rockchip Corporation. No part of this work may be disclosed,
 *    reproduced, copied, transmitted, or used in any way for any purpose,
 *    without the express written permission of Rockchip Corporation.
 *
 *****************************************************************************/

#ifndef __ROCKIVA_COMMON_H__
#define __ROCKIVA_COMMON_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/********************************************************************/
/*                             宏定义                                */
/********************************************************************/

#define ROCKIVA_MAX_FRAMERATE (60)     /* 最大帧率 */
#define ROCKIVA_AREA_POINT_NUM_MAX (6) /* 最大区域点数 */
#define ROCKIVA_AREA_NUM_MAX (16)      /* 最大检测和屏蔽区域数 */

#define ROCKIVA_PIXEL_RATION_CONVERT(base, x) ((x)*10000 / (base)) /* 像素到万分比的转换 */
#define ROCKIVA_RATIO_PIXEL_CONVERT(base, x) (((x) * (base)) / 10000) /* 万分比到像素的转换 */

#define ROCKIVA_MAX_OBJ_NUM (40) /* 场景人数最大目标个数 */

#define ROCKIVA_PATH_LENGTH (128) /* 文件路径字符串长度 */

/********************************************************************/
/*                              枚举                                 */
/********************************************************************/

/* 日志打印级别 */
typedef enum {
	ROCKIVA_LOG_DEBUG = 1, /* debug级别 */
	ROCKIVA_LOG_INFO = 2,  /* info级别 */
	ROCKIVA_LOG_WARN = 3,  /* warn级别 */
	ROCKIVA_LOG_ERROR = 4, /* error级别 */
	ROCKIVA_LOG_FATAL = 5, /* fatal级别 */
} RockIvaLogLevel;

/* 函数返回错误码 */
typedef enum {
	ROCKIVA_RET_SUCCESS = 0,         /* 成功 */
	ROCKIVA_RET_FAIL = -1,           /* 失败 */
	ROCKIVA_RET_NULL_PTR = -2,       /* 空指针 */
	ROCKIVA_RET_INVALID_HANDLE = -3, /* 无效句柄 */
	ROCKIVA_RET_LICENSE_ERROR = -4,  /* License错误 */
	ROCKIVA_RET_UNSUPPORTED = -5,    /* 不支持 */
	ROCKIVA_RET_STREAM_SWITCH = -6,  /* 码流切换 */
	ROCKIVA_RET_BUFFER_FULL = -7,    /* 缓存区域满 */
} RockIvaRetCode;

/* 图像像素格式 */
typedef enum {
	ROCKIVA_IMAGE_FORMAT_GRAY8 = 0,     /* Gray8 */
	ROCKIVA_IMAGE_FORMAT_RGB888,        /* RGB888 */
	ROCKIVA_IMAGE_FORMAT_BGR888,        /* BGR888 */
	ROCKIVA_IMAGE_FORMAT_RGBA8888,      /* RGBA8888 */
	ROCKIVA_IMAGE_FORMAT_BGRA8888,      /* BGRA8888 */
	ROCKIVA_IMAGE_FORMAT_YUV420P_YU12,  /* YUV420P YU12 */
	ROCKIVA_IMAGE_FORMAT_YUV420P_YV12,  /* YUV420P YV12 */
	ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12, /* YUV420SP NV12 */
	ROCKIVA_IMAGE_FORMAT_YUV420SP_NV21, /* YUV420SP NV21 */
	ROCKIVA_IMAGE_FORMAT_JPEG,
} RockIvaImageFormat;

typedef enum {
	ROCKIVA_IMAGE_TRANSFORM_NONE = 0x00,       ///< 正常
	ROCKIVA_IMAGE_TRANSFORM_FLIP_H = 0x01,     ///< 水平翻转
	ROCKIVA_IMAGE_TRANSFORM_FLIP_V = 0x02,     ///< 垂直翻转
	ROCKIVA_IMAGE_TRANSFORM_ROTATE_90 = 0x04,  ///< 顺时针90度
	ROCKIVA_IMAGE_TRANSFORM_ROTATE_180 = 0x03, ///< 顺时针180度
	ROCKIVA_IMAGE_TRANSFORM_ROTATE_270 = 0x07, ///< 顺时针270度
} RockIvaImageTransform;

/* 执行回调结果状态码 */
typedef enum {
	ROCKIVA_SUCCESS = 0,            /* 运行结果正常   */
	ROCKIVA_UNKNOWN = 1,            /* 错误类型未知   */
	ROCKIVA_NULL_PTR = 2,           /* 操作空指针     */
	ROCKIVA_ALLOC_FAILED = 3,       /* 申请空间失败   */
	ROCKIVA_INVALID_INPUT = 4,      /* 无效的输入     */
	ROCKIVA_EXECUTE_FAILED = 6,     /* 内部执行错误   */
	ROCKIVA_NOT_CONFIGURED = 7,     /* 未配置的类型   */
	ROCKIVA_NO_CAPACITY = 8,        /* 业务已建满     */
	ROCKIVA_BUFFER_FULL = 9,        /* 缓存区域满     */
	ROCKIVA_LICENSE_ERROR = 10,     /* license异常   */
	ROCKIVA_JPEG_DECODE_ERROR = 11, /* 解码出错       */
	ROCKIVA_DECODER_EXIT = 12       /* 解码器内部退出  */
} RockIvaExecuteStatus;

/********************************************************************/
/*                            类型定义                               */
/********************************************************************/

typedef void *RockIvaHandle;

/********************************************************************/
/*                           结构体                                  */
/********************************************************************/
typedef struct {
	void *memAddr;    /* 内存地址 */
	uint32_t memSize; /* 内存长度 */
} RockIvaMemInfo;

/* 点坐标 */
typedef struct {
	uint16_t x; /* 横坐标，万分比表示，0~9999有效 ，防止因缩放关系导致传输过程中出现异常 */
	uint16_t y; /* 纵坐标，万分比表示，0~9999有效 ，防止因缩放关系导致传输过程中出现异常 */
} RockIvaPoint;

/* 线坐标 */
typedef struct {
	RockIvaPoint head; /* 头坐标(竖直方向走向的线上方为头) */
	RockIvaPoint tail; /* 尾坐标(竖直方向走向的线下方为尾) */
} RockIvaLine;

/* 正四边形坐标 */
typedef struct {
	RockIvaPoint topLeft;     /* 左上角坐标 */
	RockIvaPoint bottomRight; /* 右下角坐标 */
} RockIvaRectangle;

/* 四边形坐标 */
typedef struct {
	RockIvaPoint topLeft;     /* 左上角坐标 */
	RockIvaPoint topRight;    /* 右上角坐标 */
	RockIvaPoint bottomLeft;  /* 左下角坐标 */
	RockIvaPoint bottomRight; /* 右下角坐标 */
} RockIvaQuadrangle;

/* 区域结构体 */
typedef struct {
	uint32_t pointNum;                               /* 点个数 */
	RockIvaPoint points[ROCKIVA_AREA_POINT_NUM_MAX]; /* 围成区域的所有点坐标 */
} RockIvaArea;

/* 多区域结构体 */
typedef struct {
	uint32_t areaNum;
	RockIvaArea areas[ROCKIVA_AREA_NUM_MAX];
} RockIvaAreas;

/* 目标大小 */
typedef struct {
	uint16_t width;  /* 宽度 */
	uint16_t height; /* 高度 */
} RockIvaSize;

/* 图像信息 */
typedef struct {
	uint16_t width;                      /* 图像宽度 */
	uint16_t height;                     /* 图像高度 */
	RockIvaImageFormat format;           /* 图像格式 */
	RockIvaImageTransform transformMode; /* 旋转模式 */
} RockIvaImageInfo;

/* 图像 */
typedef struct {
	uint32_t frameId;      /* 原始采集帧序号 */
	uint32_t channelId;    /* 通道号 */
	RockIvaImageInfo info; /* 图像信息 */
	uint32_t size;         /* 图像数据大小, 图像格式为JPEG时需要 */
	uint8_t *dataAddr;     /* 输入数据地址 */
	uint8_t *dataPhyAddr;  /* 输入数据物理地址 */
	int32_t dataFd;        /* 输入数据fd */
} RockIvaImage;

/* 抓拍图像类型 */
typedef enum {
	ROCKIVA_CAPTURE_IMAGE_NONE,           /* 不返回抓拍图像 */
	ROCKIVA_CAPTURE_IMAGE_ENCODED_TARGET, /* 返回编码的目标区域图像 */
	ROCKIVA_CAPTURE_IMAGE_ENCODED_ALL,    /* 返回编码的目标区域图像和编码的全图 */
	ROCKIVA_CAPTURE_IMAGE_DATA_TARGET,    /* 返回目标区域图像数据 */
	ROCKIVA_CAPTURE_IMAGE_DATA_ALL,       /* 返回目标区域图像数据和全图数据 */
} RockIvaCaptureImageType;

typedef struct {
	RockIvaCaptureImageType type; /* 抓拍图像类型 */
	uint16_t expandRatio;         /* 抓拍的目标区域图像的扩展比例(万分比) */
} RockIvaCaptureImageRule;

/********************************************************************/

/* 多媒体操作函数 */
typedef struct {
	int (*ROCKIVA_Yuv2Jpeg)(RockIvaImage *inputImg, RockIvaRectangle yuvRect,
	                        RockIvaImage *outputImg);
} RockIvaMediaOps;

/* 算法全局参数配置 */
typedef struct {
	RockIvaLogLevel RockIvaLogLevel;     /* 日志等级 */
	char logPath[ROCKIVA_PATH_LENGTH];   /* 日志输出路径 */
	char modelPath[ROCKIVA_PATH_LENGTH]; /* 存放算法模型的路径 */
	RockIvaMemInfo license;              /* License信息 */
	RockIvaMediaOps mediaOps;            /* 媒体操作函数集 */
	uint32_t
	    coreMask; /* 指定使用哪个NPU核跑(仅RK3588平台有效) 0x1: core0; 0x2: core1; 0x4: core2 */
} RockIvaGlobalInitParam;

/**
 * @brief 算法SDK全局初始化配置
 *
 * @param globalInit [IN] 初始化参数
 * @return RockIvaRetCode
 */
RockIvaRetCode ROCKIVA_GlobalInit(RockIvaHandle *handle, const RockIvaGlobalInitParam *param);

/**
 * @brief 算法SDK全局销毁释放
 *
 * @return RockIvaRetCode
 */
RockIvaRetCode ROCKIVA_GlobalRelease(RockIvaHandle handle);

#ifdef __cplusplus
}
#endif /* end of __cplusplus */

#endif /* end of #ifndef __ROCKIVA_COMMON_H__ */
