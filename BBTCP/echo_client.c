/**
 * @file echo_client.c
 * @brief 回声客户端示例
 * @version 1.0
 * @date 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "bb_client.h"

 // 客户端事件回调函数
void __stdcall ClientCallback(HBBCLIENT client, int eventType, const char* data, ULONG size) {
    printf("回调类型: %d\n", eventType);
    switch (eventType) {
    case BB_EVENT_CONNECTED:
        printf("[客户端] 连接服务器成功\n");

        // 连接成功后发送测试消息
        {
            const char* message = "Hello, Server!";
            BB_Client_Send(client, message, (ULONG)strlen(message));
            printf("[客户端] 发送消息: %s\n", message);
        }
        break;

    case BB_EVENT_DATA_RECEIVED:
        printf("[客户端] 收到服务器回复: %.*s\n", size, data);

        // 收到回复后3秒关闭连接
        Sleep(3000);
        BB_Client_Close(client);
        break;

    case BB_EVENT_DISCONNECTED:
        printf("[客户端] 连接已断开\n");
        break;
    }
}

int main() {
    printf("=== BB网络库回声客户端示例 ===\n");

    // 1. 加载客户端库
    if (!BB_Client_Load()) {
        printf("错误: 无法加载客户端库\n");
        return -1;
    }
    printf("[系统] 客户端库加载成功\n");

    // 2. 初始化客户端
    if (!BB_Client_Initialize(0)) {
        printf("错误: 客户端初始化失败\n");
        BB_Client_Cleanup();
        return -1;
    }
    printf("[系统] 客户端初始化成功\n");

    // 3. 连接服务器
    printf("[客户端] 正在连接服务器 127.0.0.1:8080...\n");
    HBBCLIENT client = BB_Client_Connect(
        ClientCallback,     // 回调函数
        L"127.0.0.1",      // 服务器IP
        8080,              // 服务器端口
        5000,              // 超时时间(毫秒)
        4096,              // 缓冲区大小
        0,                 // 最大数据包大小
        BB_PROXY_NONE,     // 代理类型
        NULL,              // 代理IP
        0,                 // 代理端口
        NULL,              // 代理账号
        NULL,              // 代理密码
        FALSE              // 不使用IPv6
    );

    if (client == NULL) {
        printf("错误: 连接服务器失败\n");
        BB_Client_Cleanup();
        return -1;
    }

    printf("[系统] 连接请求已发送，等待回调...\n");

    // 4. 等待操作完成
    Sleep(10000);

    // 5. 清理资源
    BB_Client_Cleanup();
    printf("[系统] 客户端已退出\n");

    return 0;
}