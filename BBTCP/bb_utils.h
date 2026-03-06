/**
 * @file bb_utils.h
 * @brief BB网络库工具函数
 * @version 2.0
 * @date 2024
 */

#ifndef BB_UTILS_H
#define BB_UTILS_H
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "bb_common.h"

#ifdef __cplusplus
extern "C" {
#endif

    // ==================== 内存管理函数 ====================

    /**
     * @brief 分配并清零内存
     * @param size 要分配的内存大小
     * @return 成功返回内存指针，失败返回NULL
     */
    void* BB_Alloc(size_t size);

    /**
     * @brief 重新分配内存
     * @param ptr 原内存指针
     * @param new_size 新内存大小
     * @return 成功返回新内存指针，失败返回NULL
     */
    void* BB_Realloc(void* ptr, size_t new_size);

    /**
     * @brief 释放内存（安全，可传入NULL）
     * @param ptr 要释放的内存指针
     */
    void BB_Free(void* ptr);

    /**
     * @brief 释放字符串内存（与BB_Free相同，提供语义化接口）
     * @param str 要释放的字符串指针
     */
    void BB_FreeString(void* str);

    // ==================== 工具函数 ====================

    /**
     * @brief 获取高精度时间戳（64位，避免49天溢出问题）
     * @return 当前时间戳（毫秒）
     */
    DWORD64 BB_GetTickCount64();

#ifdef __cplusplus
}
#endif

#endif // BB_UTILS_H
