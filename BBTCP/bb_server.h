/**
 * @file bb_server.h
 * @brief TCP服务器公共接口
 * @version 2.0
 * @date 2024
 * @note 统一使用宽字符(WCHAR)以提高Windows平台兼容性
 */

#ifndef BB_SERVER_H
#define BB_SERVER_H

#include "bb_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

    // ==================== 服务器公共接口函数 ====================

    /**
     * @brief 加载服务器库（初始化Winsock）
     * @return 成功返回1，失败返回0
     */
    int WINAPI BB_Server_Load();


    /**
     * @brief 初始化服务器库
     * @param threadCount 工作线程数量，为0时自动根据CPU核心数计算
     * @return 成功返回1，失败返回0
     * @note 此函数是线程安全的，可多次调用
     */
    int WINAPI BB_Server_Initialize(DWORD threadCount);

    /**
     * @brief 创建TCP服务器
     * @param port 服务器监听端口
     * @param acceptCallback 接受新连接回调函数
     * @param recvCallback 接收数据回调函数
     * @param closeCallback 连接关闭回调函数
     * @param bindIP 绑定IP地址，NULL表示绑定所有地址
     * @param postAcceptCount 预投递的Accept操作数量
     * @param reuseAddr 是否启用地址重用
     * @param noDelay 是否禁用Nagle算法
     * @param socketBufferSize 套接字缓冲区大小
     * @param useIPv6 是否使用IPv6
     * @param bufferSize 接收缓冲区大小
     * @param maxPacketSize 最大数据包大小
     * @return 成功返回服务器句柄，失败返回NULL
     */
    HBBSERVER WINAPI BB_Server_Create(USHORT port,
        BB_SERVER_ACCEPT_CALLBACK acceptCallback,
        BB_SERVER_RECV_CALLBACK recvCallback,
        BB_SERVER_CLOSE_CALLBACK closeCallback,
        LPCWSTR bindIP,
        int postAcceptCount,
        BOOL reuseAddr,
        BOOL noDelay,
        int socketBufferSize,
        BOOL useIPv6,
        DWORD bufferSize,
        DWORD maxPacketSize);

    /**
     * @brief 服务器发送数据到客户端
     * @param client 客户端连接句柄
     * @param data 要发送的数据
     * @param size 数据大小（字节）
     * @return 成功返回TRUE，失败返回FALSE
     */
    BOOL WINAPI BB_Server_Send(HBBCCLIENT client, LPCVOID data, ULONG size);

    /**
     * @brief 断开客户端连接
     * @param client 客户端连接句柄
     * @return 成功返回TRUE，失败返回FALSE
     */
    BOOL WINAPI BB_Server_Disconnect(HBBCCLIENT client);

    /**
     * @brief 关闭服务器
     * @param server 服务器句柄
     * @return 成功返回TRUE，失败返回FALSE
     */
    BOOL WINAPI BB_Server_Close(HBBSERVER server);

    /**
     * @brief 获取服务器本地IP地址
     * @param server 服务器句柄
     * @return 成功时返回包含IP地址的宽字符串指针，失败返回NULL
     * @note 返回的字符串必须使用 BB_FreeString 函数释放
     */
    WCHAR* WINAPI BB_Server_GetLocalIP(HBBSERVER server);

    /**
     * @brief 获取服务器本地端口
     * @param server 服务器句柄
     * @return 端口号，失败返回0
     */
    USHORT WINAPI BB_Server_GetLocalPort(HBBSERVER server);

    /**
     * @brief 设置服务器运行状态
     * @param server 服务器句柄
     * @param isRunning 运行状态
     */
    void WINAPI BB_Server_SetRunning(HBBSERVER server, BOOL isRunning);

    /**
     * @brief 设置服务器标识键
     * @param server 服务器句柄
     * @param key 标识键
     */
    void WINAPI BB_Server_SetKey(HBBSERVER server, int key);

    /**
     * @brief 获取服务器标识键
     * @param server 服务器句柄
     * @return 标识键
     */
    int WINAPI BB_Server_GetKey(HBBSERVER server);

    /**
     * @brief 设置Accept回调函数
     * @param server 服务器句柄
     * @param callback 回调函数
     */
    void WINAPI BB_Server_SetAcceptCallback(HBBSERVER server, BB_SERVER_ACCEPT_CALLBACK callback);

    /**
     * @brief 设置Recv回调函数
     * @param server 服务器句柄
     * @param callback 回调函数
     */
    void WINAPI BB_Server_SetRecvCallback(HBBSERVER server, BB_SERVER_RECV_CALLBACK callback);

    /**
     * @brief 设置Close回调函数
     * @param server 服务器句柄
     * @param callback 回调函数
     */
    void WINAPI BB_Server_SetCloseCallback(HBBSERVER server, BB_SERVER_CLOSE_CALLBACK callback);

    /**
     * @brief 设置客户端标识键
     * @param client 客户端连接句柄
     * @param key 标识键
     */
    void WINAPI BB_Server_SetClientKey(HBBCCLIENT client, HBBCLIENT key);

    /**
     * @brief 获取客户端标识键
     * @param client 客户端连接句柄
     * @return 标识键
     */
    HBBCLIENT WINAPI BB_Server_GetClientKey(HBBCCLIENT client);

    /**
     * @brief 获取客户端连接时间
     * @param client 客户端连接句柄
     * @return 连接时间戳（毫秒）
     */
    DWORD64 WINAPI BB_Server_GetClientConnectTime(HBBCCLIENT client);

    /**
     * @brief 获取客户端IP地址（宽字符版本）
     * @param client 客户端连接句柄
     * @return IP地址宽字符串，失败返回NULL
     * @note 返回的字符串必须使用 BB_FreeString 函数释放
     */
    WCHAR* WINAPI BB_Server_GetClientIPW(HBBCCLIENT client);

    /**
     * @brief 获取客户端端口
     * @param client 客户端连接句柄
     * @return 端口号，失败返回0
     */
    USHORT WINAPI BB_Server_GetClientPort(HBBCCLIENT client);

    /**
     * @brief 清理服务器库
     * @return 成功返回1，失败返回0
     * @note 调用此函数后，所有服务器功能将不可用
     */
    int WINAPI BB_Server_Cleanup();

#ifdef __cplusplus
}
#endif

#endif // BB_SERVER_H