/**
 * @file bb_server.c
 * @brief TCP服务器实现
 * @version 2.0
 * @date 2024
 * @note 使用IOCP完成端口模型，支持高并发连接
 */
#include "bb_internal.h"  // 包含内部头文件
#include "bb_server.h"
#include "bb_utils.h"
#include <assert.h>

 // ==================== 服务器全局变量 ====================

 /** @brief 服务器接收IOCP句柄 */
static HANDLE g_bbServerRecvIOCP = NULL;

/** @brief 服务器接受IOCP句柄 */
static HANDLE g_bbServerAcceptIOCP = NULL;

/** @brief 服务器运行标志 */
static volatile BOOL g_bbServerRunning = FALSE;

/** @brief 服务器工作线程数量 */
static DWORD g_bbServerThreadCount = 0;

// ==================== 内部数据结构定义 ====================

/** @brief TCP服务器内部结构 */
typedef struct BB_TCP_SERVER {
    SOCKET socket;                      /**< 监听套接字 */
    int postAcceptCount;                /**< 预投递Accept数量 */
    BOOL reuseAddress;                  /**< 地址重用标志 */
    BOOL noDelay;                       /**< Nagle算法禁用标志 */
    int socketBufferSize;               /**< 套接字缓冲区大小 */
    BOOL isRunning;                     /**< 运行状态 */
    int serverKey;                      /**< 服务器标识键 */
    BOOL isIPv6;                        /**< IPv6标志 */
    ULONG bufferSize;                   /**< 缓冲区大小 */
    ULONG maxPacketSize;                /**< 最大数据包大小 */

    BB_SERVER_ACCEPT_CALLBACK acceptCallback;   /**< 接受连接回调 */
    BB_SERVER_RECV_CALLBACK recvCallback;       /**< 接收数据回调 */
    BB_SERVER_CLOSE_CALLBACK closeCallback;     /**< 连接关闭回调 */
    LPFN_ACCEPTEX acceptExFunc;                 /**< AcceptEx函数指针 */
} BB_TCP_SERVER, * P_BB_TCP_SERVER;

/** @brief 服务器端客户端连接结构 */
typedef struct BB_TCP_SERVER_CLIENT {
    SOCKET socket;                      /**< 客户端套接字 */
    HBBCLIENT clientKey;                /**< 客户端标识键 */
    DWORD64 connectTime;                /**< 连接时间 */
    struct BB_TCP_SERVER_OPERATION* operation;  /**< 关联的操作结构 */
    BOOL isDisconnected;                /**< 断开连接标志 */
    BOOL isIPv6;                        /**< IPv6标志 */
} BB_TCP_SERVER_CLIENT, * P_BB_TCP_SERVER_CLIENT;

/** @brief 服务器操作结构 */
typedef struct BB_TCP_SERVER_OPERATION {
    OVERLAPPED overlapped;              /**< 重叠IO结构 */
    int operationType;                  /**< 操作类型 */
    DWORD dataLength;                   /**< 数据长度 */
    DWORD bytesTransferred;             /**< 传输字节数 */
    DWORD bufferCapacity;               /**< 缓冲区容量 */
    DWORD bufferOffset;                 /**< 缓冲区偏移 */
    char* buffer;                       /**< 数据缓冲区 */
    P_BB_TCP_SERVER server;             /**< 关联的服务器 */
    P_BB_TCP_SERVER_CLIENT client;      /**< 关联的客户端 */
} BB_TCP_SERVER_OPERATION, * P_BB_TCP_SERVER_OPERATION;

// ==================== 静态函数声明 ====================

static BOOL BB_Server_CreateClient(P_BB_TCP_SERVER_CLIENT client, P_BB_TCP_SERVER server);
static void BB_Server_PostAccept(P_BB_TCP_SERVER server);
static BOOL BB_Server_CreateInternal(P_BB_TCP_SERVER server,
    USHORT port,
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
static BOOL BB_Server_ProcessRecvData(P_BB_TCP_SERVER server, P_BB_TCP_SERVER_OPERATION operation);
static BOOL BB_Server_CloseInternal(P_BB_TCP_SERVER server);
static BOOL BB_Server_DisconnectClient(P_BB_TCP_SERVER_CLIENT client);
static BOOL BB_Server_SendData(P_BB_TCP_SERVER_CLIENT client, const char* data, DWORD length);
static DWORD WINAPI BB_Server_AcceptIOCPWorker(LPVOID param);
static DWORD WINAPI BB_Server_RecvIOCPWorker(LPVOID param);

// ==================== 服务器核心函数实现 ====================

/**
 * @brief 创建客户端连接结构
 */
static BOOL BB_Server_CreateClient(P_BB_TCP_SERVER_CLIENT client, P_BB_TCP_SERVER server) {
    if (client == NULL || server == NULL) {
        return FALSE;
    }

    // 初始化客户端结构
    ZeroMemory(client, sizeof(BB_TCP_SERVER_CLIENT));
    client->isDisconnected = FALSE;
    client->socket = INVALID_SOCKET;
    client->operation = NULL;
    client->connectTime = 0;
    client->isIPv6 = server->isIPv6;

    // 创建客户端套接字
    client->socket = WSASocket(client->isIPv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (client->socket == INVALID_SOCKET) {
        return FALSE;
    }

    // 设置套接字选项
    setsockopt(client->socket, SOL_SOCKET, SO_SNDBUF, (char*)&server->socketBufferSize, sizeof(server->socketBufferSize));
    setsockopt(client->socket, SOL_SOCKET, SO_RCVBUF, (char*)&server->socketBufferSize, sizeof(server->socketBufferSize));

    if (server->noDelay) {
        const BOOL noDelayOpt = TRUE;
        setsockopt(client->socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelayOpt, sizeof(noDelayOpt));
    }

    if (server->reuseAddress) {
        const BOOL reuseOpt = TRUE;
        setsockopt(client->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseOpt, sizeof(reuseOpt));
    }

    // 创建操作结构
    client->operation = (P_BB_TCP_SERVER_OPERATION)BB_Alloc(sizeof(BB_TCP_SERVER_OPERATION));
    if (client->operation == NULL) {
        BB_SAFE_CLOSE_SOCKET(client->socket);
        return FALSE;
    }

    ZeroMemory(client->operation, sizeof(BB_TCP_SERVER_OPERATION));

    // 计算地址长度和缓冲区大小
    DWORD addressLength = client->isIPv6 ?
        sizeof(SOCKADDR_IN6) + 16 :
        sizeof(SOCKADDR_IN) + 16;

    // 确保缓冲区足够大以容纳AcceptEx所需的地址信息
    DWORD requiredBufferSize = max(addressLength * 2, server->bufferSize);

    client->operation->bufferCapacity = requiredBufferSize;
    client->operation->buffer = (char*)BB_Alloc(requiredBufferSize);
    if (client->operation->buffer == NULL) {
        BB_SAFE_FREE(client->operation);
        BB_SAFE_CLOSE_SOCKET(client->socket);
        return FALSE;
    }

    client->operation->bufferOffset = 0;
    client->operation->server = server;
    client->operation->client = client;
    client->operation->operationType = 1;  // Accept状态

    // 关联到Recv IOCP
    if (CreateIoCompletionPort((HANDLE)client->socket, g_bbServerRecvIOCP, 0, 0) != g_bbServerRecvIOCP) {
        BB_SAFE_FREE(client->operation->buffer);
        BB_SAFE_FREE(client->operation);
        BB_SAFE_CLOSE_SOCKET(client->socket);
        return FALSE;
    }

    // 投递AcceptEx操作
    DWORD bytesReceived = 0;
    if (server->acceptExFunc(server->socket, client->socket, client->operation->buffer,
        0, addressLength, addressLength, &bytesReceived,
        (LPOVERLAPPED)client->operation)) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            BB_SAFE_FREE(client->operation->buffer);
            BB_SAFE_FREE(client->operation);
            BB_SAFE_CLOSE_SOCKET(client->socket);
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * @brief 投递Accept操作
 */
static void BB_Server_PostAccept(P_BB_TCP_SERVER server) {
    BOOL success = FALSE;
    int maxAttempts = 10; // 防止无限循环

    while (!success && maxAttempts-- > 0) {
        P_BB_TCP_SERVER_CLIENT client = (P_BB_TCP_SERVER_CLIENT)BB_Alloc(sizeof(BB_TCP_SERVER_CLIENT));
        if (client != NULL) {
            if (BB_Server_CreateClient(client, server)) {
                success = TRUE;
            }
            else {
                BB_SAFE_FREE(client);
            }
        }
    }
}

/**
 * @brief 内部服务器创建函数
 */
static BOOL BB_Server_CreateInternal(P_BB_TCP_SERVER server,
    USHORT port,
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
    DWORD maxPacketSize) {

    server->socket = WSASocket(useIPv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (server->socket == INVALID_SOCKET) {
        return FALSE;
    }

    // 初始化服务器参数
    server->postAcceptCount = postAcceptCount;
    server->reuseAddress = reuseAddr;
    server->noDelay = noDelay;
    server->socketBufferSize = socketBufferSize;
    server->isRunning = TRUE;
    server->acceptCallback = acceptCallback;
    server->recvCallback = recvCallback;
    server->closeCallback = closeCallback;
    server->serverKey = 0;
    server->bufferSize = bufferSize;
    server->maxPacketSize = maxPacketSize;
    server->isIPv6 = useIPv6;

    // 获取AcceptEx函数指针
    DWORD bytesReturned = 0;
    GUID acceptExGuid = WSAID_ACCEPTEX;
    if (WSAIoctl(server->socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &acceptExGuid, sizeof(acceptExGuid),
        &server->acceptExFunc, sizeof(server->acceptExFunc),
        &bytesReturned, NULL, NULL) == SOCKET_ERROR) {
        BB_SAFE_CLOSE_SOCKET(server->socket);
        return FALSE;
    }

    // 设置套接字选项
    setsockopt(server->socket, SOL_SOCKET, SO_SNDBUF, (char*)&server->socketBufferSize, sizeof(server->socketBufferSize));
    setsockopt(server->socket, SOL_SOCKET, SO_RCVBUF, (char*)&server->socketBufferSize, sizeof(server->socketBufferSize));

    if (server->noDelay) {
        BOOL noDelayOpt = TRUE;
        setsockopt(server->socket, IPPROTO_TCP, TCP_NODELAY, (char*)&noDelayOpt, sizeof(noDelayOpt));
    }

    if (server->reuseAddress) {
        BOOL reuseOpt = TRUE;
        setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuseOpt, sizeof(reuseOpt));
    }

    // 关联到Accept IOCP
    if (CreateIoCompletionPort((HANDLE)server->socket, g_bbServerAcceptIOCP, 0, 0) != g_bbServerAcceptIOCP) {
        BB_SAFE_CLOSE_SOCKET(server->socket);
        return FALSE;
    }

    // 地址解析和绑定
    ADDRINFOW hints;
    PADDRINFOW result = NULL;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = useIPv6 ? AF_INET6 : AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    wchar_t portStr[6];
    ZeroMemory(portStr, sizeof(portStr));
    swprintf_s(portStr, _countof(portStr), L"%u", port);

    // 使用 GetAddrInfoW 解析地址
    int error = GetAddrInfoW(bindIP, portStr, &hints, &result);
    if (error != 0) {
        BB_SAFE_CLOSE_SOCKET(server->socket);
        return FALSE;
    }

    // 尝试绑定到解析出的地址
    BOOL bindSuccess = FALSE;
    for (PADDRINFOW ptr = result; ptr != NULL; ptr = ptr->ai_next) {
        if (bind(server->socket, ptr->ai_addr, (int)ptr->ai_addrlen) == 0) {
            bindSuccess = TRUE;
            break;
        }
    }

    FreeAddrInfoW(result);

    if (!bindSuccess) {
        BB_SAFE_CLOSE_SOCKET(server->socket);
        return FALSE;
    }

    // 开始监听
    if (listen(server->socket, SOMAXCONN) == SOCKET_ERROR) {
        BB_SAFE_CLOSE_SOCKET(server->socket);
        return FALSE;
    }

    // 投递Accept操作
    if (server->postAcceptCount <= 0) {
        server->postAcceptCount = 1;
    }

    for (int i = 0; i < server->postAcceptCount; i++) {
        BB_Server_PostAccept(server);
    }

    return TRUE;
}

/**
 * @brief 处理接收到的数据
 */
static BOOL BB_Server_ProcessRecvData(P_BB_TCP_SERVER server, P_BB_TCP_SERVER_OPERATION operation) {
    if (operation == NULL || operation->client == NULL) {
        return FALSE;
    }

    operation->operationType = 2; // 设置为接收状态

    WSABUF wsaBuffer;
    DWORD flags = 0;
    DWORD bytesReceived = 0;

    wsaBuffer.buf = operation->buffer;
    wsaBuffer.len = server->bufferSize;

    if (WSARecv(operation->client->socket, &wsaBuffer, 1, &bytesReceived, &flags,
        (LPWSAOVERLAPPED)operation, NULL) != 0) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * @brief 内部关闭服务器
 */
static BOOL BB_Server_CloseInternal(P_BB_TCP_SERVER server) {
    if (server == NULL) {
        return FALSE;
    }

    shutdown(server->socket, SD_BOTH);
    BB_SAFE_CLOSE_SOCKET(server->socket);
    return TRUE;
}

/**
 * @brief 断开客户端连接
 */
static BOOL BB_Server_DisconnectClient(P_BB_TCP_SERVER_CLIENT client) {
    if (client == NULL) {
        return FALSE;
    }

    client->isDisconnected = TRUE;
    shutdown(client->socket, SD_BOTH);
    BB_SAFE_CLOSE_SOCKET(client->socket);
    return TRUE;
}

/**
 * @brief 发送数据到客户端
 */
static BOOL BB_Server_SendData(P_BB_TCP_SERVER_CLIENT client, const char* data, DWORD length) {
    if (client == NULL || data == NULL || length == 0 || client->socket == INVALID_SOCKET) {
        return FALSE;
    }

    WSABUF wsaBuffer;
    wsaBuffer.buf = (char*)data;
    wsaBuffer.len = length;

    DWORD bytesSent = 0;
    if (WSASend(client->socket, &wsaBuffer, 1, &bytesSent, 0, NULL, NULL) != 0) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            return FALSE;
        }
    }

    return TRUE;
}

// ==================== IOCP工作线程实现 ====================

/**
 * @brief Accept IOCP工作线程
 */
static DWORD WINAPI BB_Server_AcceptIOCPWorker(LPVOID param) {
    while (g_bbServerRunning) {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        P_BB_TCP_SERVER_OPERATION operation = NULL;

        if (!GetQueuedCompletionStatus(g_bbServerAcceptIOCP, &bytesTransferred,
            &completionKey, (LPOVERLAPPED*)&operation, INFINITE)) {
            if (operation == NULL) {
                continue;
            }

            int error = WSAGetLastError();
            if (error == 995) { // 操作已中止
                if (operation->client != NULL) {
                    operation->client->clientKey = NULL;
                    BB_SAFE_CLOSE_SOCKET(operation->client->socket);
                    BB_SAFE_FREE(operation->buffer);
                    if (!operation->client->isDisconnected) {
                        BB_SAFE_FREE(operation->client);
                    }
                }
                BB_SAFE_FREE(operation);
                continue;
            }
        }

        if (operation == NULL || operation->server == NULL) {
            if (operation != NULL && operation->client != NULL) {
                operation->client->clientKey = NULL;
                BB_SAFE_CLOSE_SOCKET(operation->client->socket);
                BB_SAFE_FREE(operation->buffer);
                if (!operation->client->isDisconnected) {
                    BB_SAFE_FREE(operation->client);
                }
            }
            BB_SAFE_FREE(operation);
            continue;
        }

        if (operation->client == NULL) {
            BB_SAFE_FREE(operation);
            continue;
        }

        if (operation->operationType != 1) {
            // 连接关闭
            if (operation->server->closeCallback != NULL && operation->client->socket != INVALID_SOCKET) {
                if (!operation->client->isDisconnected) {
                    operation->server->closeCallback((HBBSERVER)operation->server, (HBBCCLIENT)operation->client);
                }
            }

            operation->client->clientKey = NULL;
            BB_SAFE_CLOSE_SOCKET(operation->client->socket);
            BB_SAFE_FREE(operation->buffer);
            if (!operation->client->isDisconnected) {
                BB_SAFE_FREE(operation->client);
            }
            BB_SAFE_FREE(operation);
            continue;
        }

        // 投递新的Accept
        BB_Server_PostAccept(operation->server);

        // 检查服务器是否停止
        if (!operation->server->isRunning) {
            operation->client->clientKey = NULL;
            BB_SAFE_CLOSE_SOCKET(operation->client->socket);
            BB_SAFE_FREE(operation->buffer);
            if (!operation->client->isDisconnected) {
                BB_SAFE_FREE(operation->client);
            }
            BB_SAFE_FREE(operation);
            continue;
        }

        // 更新连接时间
        operation->client->connectTime = BB_GetTickCount64();
        setsockopt(operation->client->socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
            (char*)&operation->server->socket, sizeof(operation->server->socket));

        // 调用Accept回调
        if (operation->server->acceptCallback != NULL) {
            operation->server->acceptCallback((HBBSERVER)operation->server, (HBBCCLIENT)operation->client);
            if (operation->client->socket == INVALID_SOCKET) {
                operation->client->clientKey = NULL;
                BB_SAFE_FREE(operation->buffer);
                if (!operation->client->isDisconnected) {
                    BB_SAFE_FREE(operation->client);
                }
                BB_SAFE_FREE(operation);
                continue;
            }
        }

        // 开始接收数据
        if (!BB_Server_ProcessRecvData(operation->server, operation)) {
            if (operation->server->closeCallback != NULL && operation->client->socket != INVALID_SOCKET) {
                if (!operation->client->isDisconnected) {
                    operation->server->closeCallback((HBBSERVER)operation->server, (HBBCCLIENT)operation->client);
                }
            }

            operation->client->clientKey = NULL;
            BB_SAFE_CLOSE_SOCKET(operation->client->socket);
            BB_SAFE_FREE(operation->buffer);
            if (!operation->client->isDisconnected) {
                BB_SAFE_FREE(operation->client);
            }
            BB_SAFE_FREE(operation);
        }
    }

    BB_SAFE_CLOSE_HANDLE(g_bbServerAcceptIOCP);
    return 0;
}

/**
 * @brief Recv IOCP工作线程
 */
static DWORD WINAPI BB_Server_RecvIOCPWorker(LPVOID param) {
    while (g_bbServerRunning) {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        P_BB_TCP_SERVER_OPERATION operation = NULL;

        if (!GetQueuedCompletionStatus(g_bbServerRecvIOCP, &bytesTransferred,
            &completionKey, (LPOVERLAPPED*)&operation, INFINITE)) {
            if (operation == NULL || operation->server == NULL) {
                continue;
            }


            if (WSAGetLastError() == 995) { // 操作已中止
                if (operation->client != NULL) {
                    operation->client->clientKey = NULL;
                    BB_SAFE_CLOSE_SOCKET(operation->client->socket);
                    BB_SAFE_FREE(operation->buffer);
                    if (!operation->client->isDisconnected) {
                        BB_SAFE_FREE(operation->client);
                    }
                }
                BB_SAFE_FREE(operation);
                continue;
            }
        }

        if (operation == NULL || operation->server == NULL || operation->client == NULL) {
            if (operation != NULL) {
                if (operation->client != NULL) {
                    operation->client->clientKey = NULL;
                    BB_SAFE_CLOSE_SOCKET(operation->client->socket);
                    BB_SAFE_FREE(operation->buffer);
                    if (!operation->client->isDisconnected) {
                        BB_SAFE_FREE(operation->client);
                    }
                }
                else {
                    BB_SAFE_FREE(operation->buffer);
                }
            }
            BB_SAFE_FREE(operation);
            continue;
        }

        if (bytesTransferred == 0) {
            // 连接断开
            if (operation->server->closeCallback != NULL && operation->client->socket != INVALID_SOCKET) {
                if (!operation->client->isDisconnected) {
                    operation->server->closeCallback((HBBSERVER)operation->server, (HBBCCLIENT)operation->client);
                }
            }

            operation->client->clientKey = NULL;
            BB_SAFE_CLOSE_SOCKET(operation->client->socket);
            BB_SAFE_FREE(operation->buffer);
            if (!operation->client->isDisconnected) {
                BB_SAFE_FREE(operation->client);
            }
            BB_SAFE_FREE(operation);
            continue;
        }

        // 处理接收到的数据
        //operation->bytesTransferred = bytesTransferred;

        if (operation->server->recvCallback != NULL) {
            operation->server->recvCallback((HBBSERVER)operation->server, (HBBCCLIENT)operation->client,
                operation->buffer, bytesTransferred);
        }

        if (operation->client->socket == INVALID_SOCKET) {
            operation->client->clientKey = NULL;
            BB_SAFE_FREE(operation->buffer);
            if (!operation->client->isDisconnected) {
                BB_SAFE_FREE(operation->client);
            }
            BB_SAFE_FREE(operation);
            continue;
        }

        // 继续接收数据
        if (!BB_Server_ProcessRecvData(operation->server, operation)) {
            if (operation->server->closeCallback != NULL && operation->client->socket != INVALID_SOCKET) {
                if (!operation->client->isDisconnected) {
                    operation->server->closeCallback((HBBSERVER)operation->server, (HBBCCLIENT)operation->client);
                }
            }

            operation->client->clientKey = NULL;
            BB_SAFE_CLOSE_SOCKET(operation->client->socket);
            BB_SAFE_FREE(operation->buffer);
            if (!operation->client->isDisconnected) {
                BB_SAFE_FREE(operation->client);
            }
            BB_SAFE_FREE(operation);
        }
    }

    BB_SAFE_CLOSE_HANDLE(g_bbServerRecvIOCP);
    return 0;
}

// ==================== 服务器公共接口函数实现 ====================

int WINAPI BB_Server_Load() {
    if (g_bbServerAcceptIOCP != NULL) {
        return 1;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return 0;
    }

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        WSACleanup();
        return 0;
    }

    g_bbServerAcceptIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (g_bbServerAcceptIOCP == NULL) {
        WSACleanup();
        return 0;
    }

    g_bbServerRecvIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (g_bbServerRecvIOCP == NULL) {
        CloseHandle(g_bbServerAcceptIOCP);
        g_bbServerAcceptIOCP = NULL;
        WSACleanup();
        return 0;
    }

    g_bbServerRunning = TRUE;
    return 1;
}

int WINAPI BB_Server_Free() {
    g_bbServerThreadCount = 0;
    return 1;
}

int WINAPI BB_Server_Initialize(DWORD threadCount) {
    if (g_bbServerThreadCount > 0) {
        return 1; // 已经初始化
    }

    // 设置线程数
    if (threadCount == 0) {
        SYSTEM_INFO systemInfo;
        GetSystemInfo(&systemInfo);
        g_bbServerThreadCount = systemInfo.dwNumberOfProcessors * 2;
        if (g_bbServerThreadCount < 2) {
            g_bbServerThreadCount = 2;
        }
    }
    else {
        g_bbServerThreadCount = threadCount;
    }

    // 创建工作线程（每个IOCP创建相同数量的线程）
    DWORD threadsPerIOCP = g_bbServerThreadCount / 2;
    if (threadsPerIOCP < 1) threadsPerIOCP = 1;

    for (DWORD i = 0; i < threadsPerIOCP; i++) {
        HANDLE acceptThread = CreateThread(NULL, 0, BB_Server_AcceptIOCPWorker, NULL, 0, NULL);
        HANDLE recvThread = CreateThread(NULL, 0, BB_Server_RecvIOCPWorker, NULL, 0, NULL);

        if (acceptThread != NULL) CloseHandle(acceptThread);
        if (recvThread != NULL) CloseHandle(recvThread);
    }

    return 1;
}

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
    DWORD maxPacketSize) {

    

    P_BB_TCP_SERVER server = (P_BB_TCP_SERVER)BB_Alloc(sizeof(BB_TCP_SERVER));
    if (server == NULL) {
        return NULL;
    }

    ZeroMemory(server, sizeof(BB_TCP_SERVER));
    server->socket = INVALID_SOCKET;

    BOOL result = BB_Server_CreateInternal(server, port, acceptCallback, recvCallback, closeCallback,
        bindIP, postAcceptCount, reuseAddr, noDelay,
        socketBufferSize, useIPv6, bufferSize, maxPacketSize);

    if (result) {
        return (HBBSERVER)server;
    }

    BB_SAFE_FREE(server);
    return NULL;
}

BOOL WINAPI BB_Server_Send(HBBCCLIENT client, LPCVOID data, ULONG size) {
    if (client == NULL || data == NULL || size == 0) {
        return FALSE;
    }

    P_BB_TCP_SERVER_CLIENT internalClient = (P_BB_TCP_SERVER_CLIENT)client;
    return BB_Server_SendData(internalClient, (const char*)data, size);
}

BOOL WINAPI BB_Server_Disconnect(HBBCCLIENT client) {
    if (client == NULL) {
        return FALSE;
    }

    P_BB_TCP_SERVER_CLIENT internalClient = (P_BB_TCP_SERVER_CLIENT)client;
    return BB_Server_DisconnectClient(internalClient);
}

BOOL WINAPI BB_Server_Close(HBBSERVER server) {
    if (server == NULL) {
        return FALSE;
    }

    P_BB_TCP_SERVER internalServer = (P_BB_TCP_SERVER)server;
    BB_Server_CloseInternal(internalServer);
    BB_SAFE_FREE(internalServer);
    return TRUE;
}

WCHAR* WINAPI BB_Server_GetLocalIP(HBBSERVER server) {
    if (server == NULL) {
        return NULL;
    }

    P_BB_TCP_SERVER internalServer = (P_BB_TCP_SERVER)server;
    if (internalServer->socket == INVALID_SOCKET) {
        return NULL;
    }

    WCHAR ipBuffer[64];
    ZeroMemory(ipBuffer, sizeof(ipBuffer));

    int family = internalServer->isIPv6 ? AF_INET6 : AF_INET;

    if (family == AF_INET6) {
        struct sockaddr_in6 addr;
        int addrLen = sizeof(addr);
        if (getsockname(internalServer->socket, (struct sockaddr*)&addr, &addrLen) == 0) {
            if (InetNtopW(family, &addr.sin6_addr, ipBuffer, _countof(ipBuffer)) != NULL) {
                return _wcsdup(ipBuffer);
            }
        }
    }
    else {
        struct sockaddr_in addr;
        int addrLen = sizeof(addr);
        if (getsockname(internalServer->socket, (struct sockaddr*)&addr, &addrLen) == 0) {
            if (InetNtopW(family, &addr.sin_addr, ipBuffer, _countof(ipBuffer)) != NULL) {
                return _wcsdup(ipBuffer);
            }
        }
    }

    return NULL;
}

USHORT WINAPI BB_Server_GetLocalPort(HBBSERVER server) {
    if (server == NULL) {
        return 0;
    }

    P_BB_TCP_SERVER internalServer = (P_BB_TCP_SERVER)server;
    if (internalServer->socket == INVALID_SOCKET) {
        return 0;
    }

    if (internalServer->isIPv6) {
        struct sockaddr_in6 addr;
        int addrLen = sizeof(addr);

        if (getsockname(internalServer->socket, (struct sockaddr*)&addr, &addrLen) == 0) {
            return ntohs(addr.sin6_port);
        }
    }
    else {
        struct sockaddr_in addr;
        int addrLen = sizeof(addr);

        if (getsockname(internalServer->socket, (struct sockaddr*)&addr, &addrLen) == 0) {
            return ntohs(addr.sin_port);
        }
    }

    return 0;
}

void WINAPI BB_Server_SetRunning(HBBSERVER server, BOOL isRunning) {
    if (server != NULL) {
        P_BB_TCP_SERVER internalServer = (P_BB_TCP_SERVER)server;
        internalServer->isRunning = isRunning;
    }
}

void WINAPI BB_Server_SetKey(HBBSERVER server, int key) {
    if (server != NULL) {
        P_BB_TCP_SERVER internalServer = (P_BB_TCP_SERVER)server;
        internalServer->serverKey = key;
    }
}

int WINAPI BB_Server_GetKey(HBBSERVER server) {
    if (server == NULL) {
        return 0;
    }

    P_BB_TCP_SERVER internalServer = (P_BB_TCP_SERVER)server;
    return internalServer->serverKey;
}

void WINAPI BB_Server_SetAcceptCallback(HBBSERVER server, BB_SERVER_ACCEPT_CALLBACK callback) {
    if (server != NULL) {
        P_BB_TCP_SERVER internalServer = (P_BB_TCP_SERVER)server;
        internalServer->acceptCallback = callback;
    }
}

void WINAPI BB_Server_SetRecvCallback(HBBSERVER server, BB_SERVER_RECV_CALLBACK callback) {
    if (server != NULL) {
        P_BB_TCP_SERVER internalServer = (P_BB_TCP_SERVER)server;
        internalServer->recvCallback = callback;
    }
}

void WINAPI BB_Server_SetCloseCallback(HBBSERVER server, BB_SERVER_CLOSE_CALLBACK callback) {
    if (server != NULL) {
        P_BB_TCP_SERVER internalServer = (P_BB_TCP_SERVER)server;
        internalServer->closeCallback = callback;
    }
}

void WINAPI BB_Server_SetClientKey(HBBCCLIENT client, HBBCLIENT key) {
    if (client != NULL) {
        P_BB_TCP_SERVER_CLIENT internalClient = (P_BB_TCP_SERVER_CLIENT)client;
        internalClient->clientKey = key;
    }
}

HBBCLIENT WINAPI BB_Server_GetClientKey(HBBCCLIENT client) {
    if (client == NULL) {
        return NULL;
    }

    P_BB_TCP_SERVER_CLIENT internalClient = (P_BB_TCP_SERVER_CLIENT)client;
    return internalClient->clientKey;
}

DWORD64 WINAPI BB_Server_GetClientConnectTime(HBBCCLIENT client) {
    if (client == NULL) {
        return 0;
    }

    P_BB_TCP_SERVER_CLIENT internalClient = (P_BB_TCP_SERVER_CLIENT)client;
    return internalClient->connectTime;
}

WCHAR* WINAPI BB_Server_GetClientIPW(HBBCCLIENT client) {
    if (client == NULL) {
        return NULL;
    }

    wchar_t* ip_str = (wchar_t*)BB_Alloc(INET6_ADDRSTRLEN * sizeof(wchar_t));
    if (ip_str == NULL) {
        return NULL;
    }

    ZeroMemory(ip_str, INET6_ADDRSTRLEN * sizeof(wchar_t));

    P_BB_TCP_SERVER_CLIENT internalClient = (P_BB_TCP_SERVER_CLIENT)client;
    struct sockaddr_storage ss;
    int ss_len = sizeof(ss);

    if (getpeername(internalClient->socket, (struct sockaddr*)&ss, &ss_len) != 0) {
        BB_Free(ip_str);
        return NULL;
    }

    DWORD ip_str_len = INET6_ADDRSTRLEN;
    if (WSAAddressToStringW((LPSOCKADDR)&ss, ss_len, NULL, ip_str, &ip_str_len) != 0) {
        BB_Free(ip_str);
        return NULL;
    }

    return ip_str;
}

USHORT WINAPI BB_Server_GetClientPort(HBBCCLIENT client) {
    if (client == NULL) {
        return 0;
    }

    P_BB_TCP_SERVER_CLIENT internalClient = (P_BB_TCP_SERVER_CLIENT)client;
    if (internalClient->socket == INVALID_SOCKET) {
        return 0;
    }

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);

    if (getpeername(internalClient->socket, (struct sockaddr*)&addr, &addrLen) == 0) {
        if (addr.ss_family == AF_INET6) {
            return ntohs(((struct sockaddr_in6*)&addr)->sin6_port);
        }
        else if (addr.ss_family == AF_INET) {
            return ntohs(((struct sockaddr_in*)&addr)->sin_port);
        }
    }

    return 0;
}

void WINAPI BB_Server_SetClientSynchar(HBBCCLIENT client, BOOL nsynchar) {
    P_BB_TCP_SERVER_CLIENT internalClient = (P_BB_TCP_SERVER_CLIENT)client;
    unsigned long ul = nsynchar ? 0 : 1;
    ioctlsocket(internalClient->socket, FIONBIO, (unsigned long*)&ul);
}


int WINAPI BB_Server_Cleanup() {
    g_bbServerRunning = FALSE;

    return 1;
}
