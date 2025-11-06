/**
 * @file bb_internal.h
 * @brief BB网络库内部头文件
 * @version 2.0
 * @date 2024
 *
 * @note 此头文件包含所有Windows网络头文件，仅在实现文件中使用
 */

#ifndef BB_INTERNAL_H
#define BB_INTERNAL_H

 // 定义必要的宏
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define NOMINMAX

// 包含Windows网络头文件
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

// 链接库
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Crypt32.lib")

// 设置Windows版本
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0600  // 支持Vista及以上版本

// ==================== 内部宏定义 ====================

/**
 * @brief 安全关闭套接字
 * @param socket 要关闭的套接字
 */
#define BB_SAFE_CLOSE_SOCKET(socket) do { \
    if ((socket) != INVALID_SOCKET) { \
        closesocket(socket); \
        (socket) = INVALID_SOCKET; \
    } \
} while(0)

 /**
  * @brief 安全关闭句柄
  * @param handle 要关闭的句柄
  */
#define BB_SAFE_CLOSE_HANDLE(handle) do { \
    if ((handle) != NULL && (handle) != INVALID_HANDLE_VALUE) { \
        CloseHandle(handle); \
        (handle) = NULL; \
    } \
} while(0)

  /**
   * @brief 安全释放内存并置空指针
   * @param ptr 要释放的内存指针
   */
#define BB_SAFE_FREE(ptr) do { \
    if ((ptr) != NULL) { \
        BB_Free(ptr); \
        (ptr) = NULL; \
    } \
} while(0)

#endif // BB_INTERNAL_H