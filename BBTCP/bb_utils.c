/**
 * @file bb_utils.c
 * @brief 网络库工具函数实现
 * @version 2.0
 * @date 2024
 * @note 提供内存管理、字符串转换、线程安全等基础功能
 */
#include "bb_internal.h"  // 包含内部头文件
#include "bb_utils.h"
#include <assert.h>

 // ==================== 内存管理函数实现 ====================

void* BB_Alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    void* ptr = malloc(size);
    if (ptr != NULL) {
        memset(ptr, 0, size); // 自动清零
    }
    return ptr;
}

void* BB_Realloc(void* ptr, size_t new_size) {
    if (new_size == 0) {
        BB_Free(ptr);
        return NULL;
    }

    void* new_ptr = realloc(ptr, new_size);
    if (new_ptr != NULL && ptr == NULL) {
        // 如果是新分配，确保清零
        memset(new_ptr, 0, new_size);
    }
    return new_ptr;
}

void BB_Free(void* ptr) {
    if (ptr != NULL) {
        free(ptr);
    }
}

void BB_FreeString(void* str) {
    BB_Free(str);
}

// ==================== 工具函数实现 ====================

DWORD64 BB_GetTickCount64() {
    static LARGE_INTEGER frequency = { 0 };
    static LARGE_INTEGER startCount = { 0 };
    LARGE_INTEGER currentCount;

    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&startCount);
    }

    QueryPerformanceCounter(&currentCount);
    return (DWORD64)((currentCount.QuadPart - startCount.QuadPart) * 1000 / frequency.QuadPart);
}
