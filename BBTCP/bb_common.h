/**
 * @file bb_common.h
 * @brief BB网络库公共定义
 * @version 2.0
 * @date 2024
 *
 * @note 此头文件不包含任何Windows网络头文件，避免引用污染
 */

#ifndef BB_COMMON_H
#define BB_COMMON_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

    // ==================== 句柄类型定义 ====================

    /** @brief 客户端连接句柄 */
    typedef void* HBBCLIENT;

    /** @brief 服务器实例句柄 */
    typedef void* HBBSERVER;

    /** @brief 服务器端客户端连接句柄 */
    typedef void* HBBCCLIENT;

    // ==================== 代理类型定义 ====================

    /** @brief 无代理 */
#define BB_PROXY_NONE             0

/** @brief SOCKS4代理 */
#define BB_PROXY_SOCKS4           1

/** @brief SOCKS5代理 */
#define BB_PROXY_SOCKS5           2

/** @brief HTTP代理 */
#define BB_PROXY_HTTP             3

// ==================== 事件类型定义 ====================

/** @brief 连接成功事件 */
#define BB_EVENT_CONNECTED        1

/** @brief 数据接收事件 */
#define BB_EVENT_DATA_RECEIVED    2

/** @brief 连接断开事件 */
#define BB_EVENT_DISCONNECTED     3

// ==================== 错误码定义 ====================

/** @brief 操作结果枚举 */
    typedef enum {
        BB_SUCCESS = 0,                 /**< 操作成功 */
        BB_ERROR_INVALID_PARAM = 1,     /**< 参数错误 */
        BB_ERROR_NETWORK_INIT = 2,      /**< 网络初始化失败 */
        BB_ERROR_SOCKET_CREATE = 3,     /**< 套接字创建失败 */
        BB_ERROR_MEMORY_ALLOC = 4,      /**< 内存分配失败 */
        BB_ERROR_CONNECT_FAILED = 5,    /**< 连接失败 */
        BB_ERROR_IO_OPERATION = 6,      /**< IO操作失败 */
        BB_ERROR_PROXY_FAILED = 7,      /**< 代理连接失败 */
        BB_ERROR_TIMEOUT = 8,           /**< 操作超时 */
        BB_ERROR_CONNECTION_CLOSED = 9  /**< 连接已关闭 */
    } BB_RESULT;

    // ==================== 回调函数定义 ====================

    /**
     * @brief 服务器接受新连接回调函数
     * @param server 服务器句柄
     * @param client 客户端连接句柄
     */
    typedef void(__stdcall* BB_SERVER_ACCEPT_CALLBACK)(HBBSERVER server, HBBCCLIENT client);

    /**
     * @brief 服务器接收数据回调函数
     * @param server 服务器句柄
     * @param client 客户端连接句柄
     * @param data 接收到的数据指针
     * @param size 数据大小（字节）
     */
    typedef void(__stdcall* BB_SERVER_RECV_CALLBACK)(HBBSERVER server, HBBCCLIENT client, const char* data, ULONG size);

    /**
     * @brief 服务器连接关闭回调函数
     * @param server 服务器句柄
     * @param client 客户端连接句柄
     */
    typedef void(__stdcall* BB_SERVER_CLOSE_CALLBACK)(HBBSERVER server, HBBCCLIENT client);

    /**
     * @brief 客户端事件回调函数
     * @param client 客户端句柄
     * @param eventType 事件类型：BB_EVENT_xxx
     * @param data 事件相关数据（接收数据事件为数据指针，其他事件为NULL）
     * @param size 数据大小（字节）
     */
    typedef void(__stdcall* BB_CLIENT_EVENT_CALLBACK)(HBBCLIENT client, int eventType, const char* data, ULONG size);

#ifdef __cplusplus
}
#endif

#endif // BB_COMMON_H