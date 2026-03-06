/**
 * @file echo_server.c
 * @brief 回声服务器示例
 * @version 1.0
 * @date 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "bb_server.h"

 // 服务器接受连接回调
void __stdcall AcceptCallback(HBBSERVER server, HBBCCLIENT client) {
    WCHAR* clientIP = BB_Server_GetClientIPW(client);
    USHORT clientPort = BB_Server_GetClientPort(client);

    if (clientIP != NULL) {
        printf("[服务器] 客户端连接: %ls:%d\n", clientIP, clientPort);
        BB_Free(clientIP);
    }
}

// 服务器接收数据回调
void __stdcall RecvCallback(HBBSERVER server, HBBCCLIENT client, const char* data, ULONG size) {
    WCHAR* clientIP = BB_Server_GetClientIPW(client);

    if (clientIP != NULL) {
        printf("[服务器] 收到来自 %ls 的消息: %.*s\n", clientIP, size, data);
        BB_Free(clientIP);
    }

    // 回声：将收到的数据原样发回
    BB_Server_Send(client, data, size);
    printf("[服务器] 已回声回复客户端\n");
}

// 服务器连接关闭回调
void __stdcall CloseCallback(HBBSERVER server, HBBCCLIENT client) {
    WCHAR* clientIP = BB_Server_GetClientIPW(client);

    if (clientIP != NULL) {
        printf("[服务器] 客户端断开: %ls\n", clientIP);
        BB_Free(clientIP);
    }
}

int demo_sever() {
    printf("=== BB网络库回声服务器示例 ===\n");

    // 1. 加载服务器库
    if (!BB_Server_Load()) {
        printf("错误: 无法加载服务器库\n");
        return -1;
    }
    printf("[系统] 服务器库加载成功\n");

    // 2. 初始化服务器
    if (!BB_Server_Initialize(0)) {
        printf("错误: 服务器初始化失败\n");
        BB_Server_Cleanup();
        return -1;
    }
    printf("[系统] 服务器初始化成功\n");

    // 3. 创建服务器
    printf("[服务器] 正在启动服务器，端口 8080...\n");
    HBBSERVER server = BB_Server_Create(
        23461,               // 监听端口
        NULL,     // 接受连接回调
        NULL, //RecvCallback,       // 接收数据回调
        NULL, //CloseCallback,      // 连接关闭回调
        NULL,               // 绑定IP (NULL表示所有IP)
        10,                 // 预投递Accept数量
        TRUE,               // 地址重用
        TRUE,               // 禁用Nagle算法
        4096,               // 套接字缓冲区大小
        FALSE,              // 不使用IPv6
        4096,               // 接收缓冲区大小
        0                   // 最大数据包大小
    );

    if (server == NULL) {
        printf("错误: 创建服务器失败\n");
        BB_Server_Cleanup();
        return -1;
    }

    // 获取服务器信息
    WCHAR* localIP = BB_Server_GetLocalIP(server);
    USHORT localPort = BB_Server_GetLocalPort(server);

    if (localIP != NULL) {
        printf("[服务器] 服务器已启动: %ls:%d\n", localIP, localPort);
        BB_Free(localIP);
    }

    printf("[系统] 服务器运行中，按任意键停止...\n");

    // 4. 等待用户输入停止服务器
    getchar();

    // 5. 关闭服务器
    BB_Server_Close(server);
    printf("[服务器] 服务器已关闭\n");

    // 6. 清理资源
    BB_Server_Cleanup();
    printf("[系统] 服务器已退出\n");

    return 0;
}



