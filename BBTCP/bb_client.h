/**
 * @file bb_client.h
 * @brief TCP客户端公共接口
 * @version 2.0
 * @date 2024
 * @note 统一使用宽字符(WCHAR)以提高Windows平台兼容性
 */

#ifndef BB_CLIENT_H
#define BB_CLIENT_H

#include "bb_common.h"
#include "bb_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

    // ==================== 客户端公共接口函数 ====================

    /**
     * @brief 加载客户端库（初始化Winsock）
     * @return 成功返回1，失败返回0
     */
    BB_NET_API int WINAPI BB_Client_Load();

    /**
     * @brief 初始化客户端库
     * @param threadCount 工作线程数量，为0时自动根据CPU核心数计算
     * @return 成功返回1，失败返回0
     * @note 此函数是线程安全的，可多次调用
     */
    BB_NET_API int WINAPI BB_Client_Initialize(DWORD threadCount);

    /**
     * @brief 创建TCP客户端连接
     * @param callback 事件回调函数
     * @param serverIP 服务器IP地址或域名（宽字符）
     * @param serverPort 服务器端口
     * @param timeout 连接超时时间（毫秒）
     * @param bufferSize 接收缓冲区大小（字节）
     * @param maxPacketSize 最大数据包大小（字节）
     * @param proxyType 代理类型：BB_PROXY_NONE, BB_PROXY_SOCKS4, BB_PROXY_SOCKS5, BB_PROXY_HTTP
     * @param proxyIP 代理服务器IP地址（宽字符）
     * @param proxyPort 代理服务器端口
     * @param proxyAccount 代理认证账号（宽字符）
     * @param proxyPassword 代理认证密码（宽字符）
     * @param useIPv6 是否使用IPv6
     * @return 成功返回客户端句柄，失败返回NULL
     */
    BB_NET_API HBBCLIENT WINAPI BB_Client_Connect(BB_CLIENT_EVENT_CALLBACK callback,
        LPCWSTR serverIP,
        USHORT serverPort,
        int timeout,
        int bufferSize,
        DWORD maxPacketSize,
        int proxyType,
        LPCWSTR proxyIP,
        USHORT proxyPort,
        LPCWSTR proxyAccount,
        LPCWSTR proxyPassword,
        BOOL useIPv6);

    /**
     * @brief 发送数据
     * @param client 客户端句柄
     * @param data 要发送的数据
     * @param size 数据大小（字节）
     * @return 成功返回1，失败返回0
     */
    BB_NET_API int WINAPI BB_Client_Send(HBBCLIENT client, const char* data, ULONG size);

    /**
     * @brief 关闭客户端连接
     * @param client 客户端句柄
     * @return 成功返回TRUE，失败返回FALSE
     */
    BB_NET_API BOOL WINAPI BB_Client_Close(HBBCLIENT client);

    /**
     * @brief 设置客户端用户字符串数据
     * @param client 客户端句柄
     * @param userData 用户字符串数据（宽字符）
     */
    BB_NET_API void WINAPI BB_Client_SetUserString(HBBCLIENT client, LPCWSTR userData);

    /**
     * @brief 获取客户端用户字符串数据
     * @param client 客户端句柄
     * @return 用户字符串数据（宽字符）
     * @note 返回的字符串必须使用 BB_FreeString 函数释放
     */
    BB_NET_API WCHAR* WINAPI BB_Client_GetUserString(HBBCLIENT client);

    /**
     * @brief 设置客户端用户整型数据
     * @param client 客户端句柄
     * @param userData 用户整型数据
     */
    BB_NET_API void WINAPI BB_Client_SetUserInt(HBBCLIENT client, int userData);

    /**
     * @brief 获取客户端用户整型数据
     * @param client 客户端句柄
     * @return 用户整型数据
     */
    BB_NET_API int WINAPI BB_Client_GetUserInt(HBBCLIENT client);

    /**
     * @brief 清理客户端库
     * @return 成功返回1，失败返回0
     * @note 调用此函数后，所有客户端功能将不可用
     */
    BB_NET_API int WINAPI BB_Client_Cleanup();

#ifdef __cplusplus
}
#endif

#endif // BB_CLIENT_H
