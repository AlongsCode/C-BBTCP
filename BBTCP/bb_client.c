/**
 * @file bb_client.c
 * @brief TCP客户端实现
 * @version 2.0
 * @date 2024
 * @note 使用IOCP完成端口模型，支持代理连接
 */
#include "bb_internal.h"  // 包含内部头文件
#include "bb_client.h"
#include "bb_utils.h"
#include <assert.h>




 // ==================== 客户端全局变量 ====================

 /** @brief 客户端IOCP句柄 */
static HANDLE g_bbClientIOCP = NULL;

/** @brief 客户端运行标志 */
static volatile BOOL g_bbClientRunning = TRUE;

/** @brief 客户端工作线程数量 */
static DWORD g_bbClientThreadCount = 0;

// ==================== 内部数据结构定义 ====================

/** @brief TCP客户端内部结构 */
typedef struct BB_TCP_CLIENT {
    SOCKET socket;                      /**< 套接字句柄 */
    int bufferSize;                     /**< 缓冲区大小 */
    DWORD maxPacketSize;                /**< 最大数据包大小 */
    BB_CLIENT_EVENT_CALLBACK eventCallback; /**< 事件回调函数 */
    LONG volatile isConnected;          /**< 连接状态 */
    int userDataInt;                    /**< 用户整型数据 */
    WCHAR* userDataString;              /**< 用户字符串数据 */
} BB_TCP_CLIENT, * P_BB_TCP_CLIENT;

/** @brief TCP客户端操作结构 */
typedef struct BB_TCP_CLIENT_OPERATION {
    OVERLAPPED overlapped;              /**< 重叠IO结构 */
    P_BB_TCP_CLIENT client;             /**< 关联的客户端 */
    int operationType;                  /**< 操作类型 */
    char* buffer;                       /**< 数据缓冲区 */
    DWORD dataLength;                   /**< 数据长度 */
    DWORD bytesTransferred;             /**< 传输字节数 */
    DWORD bufferCapacity;               /**< 缓冲区容量 */
    DWORD bufferOffset;                 /**< 缓冲区偏移 */
    SOCKET socketHandle;                /**< 套接字句柄 */
} BB_TCP_CLIENT_OPERATION, * P_BB_TCP_CLIENT_OPERATION;

// ==================== 静态函数声明 ====================

static BOOL BB_Client_SOCKS4ProxyW(P_BB_TCP_CLIENT client, LPCWSTR host, WORD port,
    LPCWSTR username, LPCWSTR password);

static BOOL BB_Client_SOCKS5ProxyW(P_BB_TCP_CLIENT client, LPCWSTR host, WORD port,
    LPCWSTR username, LPCWSTR password);

static BOOL BB_Client_HTTPProxyW(P_BB_TCP_CLIENT client, LPCWSTR host, WORD port,
    LPCWSTR username, LPCWSTR password);

static BOOL BB_Client_ConnectInternal(P_BB_TCP_CLIENT client,
    BB_CLIENT_EVENT_CALLBACK callback,
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

static BOOL BB_Client_StartRecv(P_BB_TCP_CLIENT client, P_BB_TCP_CLIENT_OPERATION operation);

static BOOL BB_Client_InitRecvAfterConnect(P_BB_TCP_CLIENT client, P_BB_TCP_CLIENT_OPERATION operation);

static BOOL BB_Client_ProcessRecvData(P_BB_TCP_CLIENT client, P_BB_TCP_CLIENT_OPERATION operation);

static BOOL BB_Client_SendData(P_BB_TCP_CLIENT client, const char* data, DWORD length);

static BOOL BB_Client_CloseInternal(P_BB_TCP_CLIENT client);

static DWORD WINAPI BB_Client_IOCPWorkerThread(LPVOID param);

// ==================== 代理功能实现 ====================

/**
 * @brief SOCKS5代理连接（宽字符版本）
 */
static BOOL BB_Client_SOCKS5ProxyW(P_BB_TCP_CLIENT client, LPCWSTR host, WORD port,
    LPCWSTR username, LPCWSTR password) {

    unsigned long blockingMode = 0;
    ioctlsocket(client->socket, FIONBIO, &blockingMode);

    BYTE buffer[1024];

    // 发送认证方法协商
    buffer[0x00] = 0x05; // SOCKS版本
    buffer[0x01] = 0x02; // 支持的方法数量
    buffer[0x02] = 0x00; // 无认证
    buffer[0x03] = 0x02; // 用户名密码认证

    int sendResult = send(client->socket, (const char*)buffer, 4, 0);
    if (sendResult <= 0) {
        return FALSE;
    }

    int recvResult = recv(client->socket, (char*)buffer, sizeof(buffer), 0);
    if (recvResult <= 0) {
        return FALSE;
    }

    if (buffer[0] != 0x05) {
        return FALSE;
    }

    // 处理认证
    if (buffer[1] == 0x02) {
        int bufferPos = 0;

        // 转换用户名和密码为UTF-8
        char utf8_username[256] = { 0 };
        char utf8_password[256] = { 0 };
        int userLen = 0;
        int passLen = 0;

        if (username != NULL) {
            userLen = WideCharToMultiByte(CP_UTF8, 0, username, -1, utf8_username, sizeof(utf8_username) - 1, NULL, NULL);
            if (userLen > 0) userLen--; // 去掉null终止符
        }

        if (password != NULL) {
            passLen = WideCharToMultiByte(CP_UTF8, 0, password, -1, utf8_password, sizeof(utf8_password) - 1, NULL, NULL);
            if (passLen > 0) passLen--; // 去掉null终止符
        }

        buffer[bufferPos++] = 0x01; // 认证版本
        buffer[bufferPos++] = (BYTE)userLen;
        if (userLen > 0) {
            memcpy(buffer + bufferPos, utf8_username, userLen);
            bufferPos += userLen;
        }
        buffer[bufferPos++] = (BYTE)passLen;
        if (passLen > 0) {
            memcpy(buffer + bufferPos, utf8_password, passLen);
            bufferPos += passLen;
        }

        sendResult = send(client->socket, (const char*)buffer, bufferPos, 0);
        if (sendResult <= 0) {
            return FALSE;
        }

        recvResult = recv(client->socket, (char*)buffer, sizeof(buffer), 0);
        if (recvResult <= 0) {
            return FALSE;
        }

        if (buffer[0] != 0x01 || buffer[1] != 0x00) {
            return FALSE;
        }
    }
    else if (buffer[1] != 0x00) {
        return FALSE;
    }

    // 发送连接请求
    int bufferPos = 0;

    // 转换主机名为UTF-8
    char utf8_host[256] = { 0 };
    int hostLen = WideCharToMultiByte(CP_UTF8, 0, host, -1, utf8_host, sizeof(utf8_host) - 1, NULL, NULL);
    if (hostLen > 0) hostLen--; // 去掉null终止符

    buffer[bufferPos++] = 0x05; // SOCKS版本
    buffer[bufferPos++] = 0x01; // 连接命令
    buffer[bufferPos++] = 0x00; // 保留
    buffer[bufferPos++] = 0x03; // 域名类型
    buffer[bufferPos++] = (BYTE)hostLen;
    memcpy(buffer + bufferPos, utf8_host, hostLen);
    bufferPos += hostLen;
    buffer[bufferPos++] = (port >> 8);
    buffer[bufferPos++] = (BYTE)(port >> 0);

    sendResult = send(client->socket, (const char*)buffer, bufferPos, 0);
    if (sendResult <= 0) {
        return FALSE;
    }

    recvResult = recv(client->socket, (char*)buffer, sizeof(buffer), 0);
    if (recvResult <= 0) {
        return FALSE;
    }

    if (buffer[0] != 0x05 || buffer[1] != 0x00) {
        return FALSE;
    }

    return TRUE;
}
static int BB_Base64Encode(const BYTE* srcData, int srcLength, BYTE* dstBuffer, int dstSize) {
    const char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i, blockCount = srcLength / 3;

    if (dstSize < (blockCount + 1) * 4) {
        return -1; // 缓冲区不足
    }

    for (i = 0; i < blockCount; i++) {
        dstBuffer[i * 4 + 0] = base64Chars[(srcData[i * 3 + 0] >> 2)];
        dstBuffer[i * 4 + 1] = base64Chars[((srcData[i * 3 + 0] & 0x03) << 4) | (srcData[i * 3 + 1] >> 4)];
        dstBuffer[i * 4 + 2] = base64Chars[((srcData[i * 3 + 1] & 0x0F) << 2) | (srcData[i * 3 + 2] >> 6)];
        dstBuffer[i * 4 + 3] = base64Chars[srcData[i * 3 + 2] & 0x3F];
    }

    // 处理剩余字节
    int remaining = srcLength % 3;
    if (remaining == 1) {
        dstBuffer[blockCount * 4 + 0] = base64Chars[(srcData[blockCount * 3 + 0] >> 2)];
        dstBuffer[blockCount * 4 + 1] = base64Chars[((srcData[blockCount * 3 + 0] & 0x03) << 4)];
        dstBuffer[blockCount * 4 + 2] = '=';
        dstBuffer[blockCount * 4 + 3] = '=';
        blockCount++;
    }
    else if (remaining == 2) {
        dstBuffer[blockCount * 4 + 0] = base64Chars[(srcData[blockCount * 3 + 0] >> 2)];
        dstBuffer[blockCount * 4 + 1] = base64Chars[((srcData[blockCount * 3 + 0] & 0x03) << 4) | (srcData[blockCount * 3 + 1] >> 4)];
        dstBuffer[blockCount * 4 + 2] = base64Chars[((srcData[blockCount * 3 + 1] & 0x0F) << 2)];
        dstBuffer[blockCount * 4 + 3] = '=';
        blockCount++;
    }

    dstBuffer[blockCount * 4] = '\0';
    return blockCount * 4;
}

/**
 * @brief HTTP代理连接（宽字符版本）
 */
static BOOL BB_Client_HTTPProxyW(P_BB_TCP_CLIENT client, LPCWSTR host, WORD port,
    LPCWSTR username, LPCWSTR password) {

    char httpRequest[8192];
    int requestLen = 0;

    // 转换主机名为ANSI
    char ansiHost[256] = { 0 };
    WideCharToMultiByte(CP_ACP, 0, host, -1, ansiHost, sizeof(ansiHost) - 1, NULL, NULL);

    // 构建HTTP CONNECT请求
    requestLen += wsprintfA(httpRequest + requestLen, "CONNECT %s:%d HTTP/1.1\r\n", ansiHost, port);
    requestLen += wsprintfA(httpRequest + requestLen, "Host: %s:%d\r\n", ansiHost, port);
    requestLen += wsprintfA(httpRequest + requestLen, "Proxy-Connection: Keep-Alive\r\n");

    // 添加代理认证信息
    if (username != NULL && username[0] != '\0') {
        char authInfo[256];
        char base64Auth[512];

        // 转换用户名和密码为ANSI
        char ansiUsername[256] = { 0 };
        char ansiPassword[256] = { 0 };
        WideCharToMultiByte(CP_ACP, 0, username, -1, ansiUsername, sizeof(ansiUsername) - 1, NULL, NULL);
        if (password != NULL) {
            WideCharToMultiByte(CP_ACP, 0, password, -1, ansiPassword, sizeof(ansiPassword) - 1, NULL, NULL);
        }

        int authLen = wsprintfA(authInfo, "%s:%s", ansiUsername, ansiPassword);

        // Base64编码
        DWORD base64Len = sizeof(base64Auth);
        if (BB_Base64Encode((BYTE*)authInfo, authLen, (BYTE*)base64Auth, base64Len) > 0) {
            requestLen += wsprintfA(httpRequest + requestLen, "Proxy-Authorization: Basic %s\r\n", base64Auth);
        }
    }

    requestLen += wsprintfA(httpRequest + requestLen, "Content-length: 0\r\n\r\n");

    unsigned long blockingMode = 0;
    ioctlsocket(client->socket, FIONBIO, &blockingMode);

    int sendResult = send(client->socket, httpRequest, requestLen, 0);
    if (sendResult <= 0) {
        return FALSE;
    }

    // 读取HTTP响应
    int totalReceived = 0;
    int headerEnd = 0;

    while (TRUE) {
        int recvResult = recv(client->socket, httpRequest + totalReceived,
            sizeof(httpRequest) - totalReceived - 1, 0);
        if (recvResult <= 0) {
            return FALSE;
        }

        totalReceived += recvResult;
        httpRequest[totalReceived] = '\0';

        // 查找HTTP头终止标记
        for (headerEnd = 4; headerEnd <= totalReceived; headerEnd++) {
            if (httpRequest[headerEnd - 4] == '\r' && httpRequest[headerEnd - 3] == '\n' &&
                httpRequest[headerEnd - 2] == '\r' && httpRequest[headerEnd - 1] == '\n') {
                break;
            }
        }

        if (headerEnd <= totalReceived) {
            break;
        }
    }

    // 检查HTTP响应状态
    if (strncmp(httpRequest, "HTTP/1.0 200", 12) != 0 &&
        strncmp(httpRequest, "HTTP/1.1 200", 12) != 0) {
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief SOCKS4代理连接（宽字符版本）
 */
static BOOL BB_Client_SOCKS4ProxyW(P_BB_TCP_CLIENT client, LPCWSTR host, WORD port,
    LPCWSTR username, LPCWSTR password) {

    password; // 未使用参数

    unsigned long blockingMode = 0;
    ioctlsocket(client->socket, FIONBIO, &blockingMode);

    ADDRINFOW* result = NULL;
    ADDRINFOW hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int ret = GetAddrInfoW(host, NULL, &hints, &result);
    if (ret != 0) {
        return FALSE;
    }

    DWORD ipAddress = ((struct sockaddr_in*)result->ai_addr)->sin_addr.s_addr;
    FreeAddrInfoW(result);

    BYTE buffer[1024];
    int bufferPos = 0;

    // 构建SOCKS4请求包
    buffer[bufferPos++] = 0x04; // SOCKS版本
    buffer[bufferPos++] = 0x01; // 连接命令
    buffer[bufferPos++] = (BYTE)(port >> 8);
    buffer[bufferPos++] = (BYTE)(port >> 0);
    buffer[bufferPos++] = (BYTE)(ipAddress >> 0x00);
    buffer[bufferPos++] = (BYTE)(ipAddress >> 0x08);
    buffer[bufferPos++] = (BYTE)(ipAddress >> 0x10);
    buffer[bufferPos++] = (BYTE)(ipAddress >> 0x18);

    // 添加用户名
    if (username != NULL) {
        int userLen = (int)wcslen(username);
        BYTE* begin = buffer + bufferPos;
        for (int i = 0; i < userLen; i++) {
            if (username[i] > 0xFF) {
                return FALSE;
            }
            begin[i] = (BYTE)(username[i] & 0xFF);
        }
        bufferPos += userLen;
    }
    buffer[bufferPos++] = 0x00; // 用户名终止

    int sendResult = send(client->socket, (const char*)buffer, bufferPos, 0);
    if (sendResult <= 0) {
        return FALSE;
    }

    int recvResult = recv(client->socket, (char*)buffer, sizeof(buffer), 0);
    if (recvResult <= 0) {
        return FALSE;
    }

    // 检查SOCKS4响应
    if (buffer[0] != 0x00 || buffer[1] != 0x5A) {
        return FALSE;
    }

    return TRUE;
}

// ==================== 客户端核心函数实现 ====================

/**
 * @brief 内部连接函数
 */
static BOOL BB_Client_ConnectInternal(P_BB_TCP_CLIENT client,
    BB_CLIENT_EVENT_CALLBACK callback,
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
    BOOL useIPv6) {

    if (g_bbClientIOCP == NULL) {
        return FALSE;
    }

    // 参数验证
    if (serverIP == NULL || timeout <= 0 || bufferSize <= 0) {
        return FALSE;
    }

    // 初始化客户端结构
    client->eventCallback = callback;
    client->bufferSize = bufferSize;
    client->maxPacketSize = maxPacketSize;
    client->isConnected = FALSE;
    client->userDataInt = 0;
    client->userDataString = NULL;

    // 创建套接字
    client->socket = WSASocket(useIPv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (client->socket == INVALID_SOCKET) {
        return FALSE;
    }

    // 关联到IOCP
    if (CreateIoCompletionPort((HANDLE)client->socket, g_bbClientIOCP, 0, 0) != g_bbClientIOCP) {
        BB_SAFE_CLOSE_SOCKET(client->socket);
        return FALSE;
    }

    // 设置套接字选项
    DWORD socketTimeout = 40960;
    setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&socketTimeout, sizeof(socketTimeout));
    setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, (char*)&socketTimeout, sizeof(socketTimeout));

    // 设置非阻塞模式
    unsigned long nonBlocking = 1;
    ioctlsocket(client->socket, FIONBIO, &nonBlocking);

    // 连接服务器
    BOOL connectResult = FALSE;
    const WCHAR* connectIP = (proxyType != BB_PROXY_NONE && proxyIP != NULL) ? proxyIP : serverIP;
    USHORT connectPort = (proxyType != BB_PROXY_NONE) ? proxyPort : serverPort;

    if (useIPv6) {
        struct addrinfoW hints, * addrInfo = NULL;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET6;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        WCHAR portStr[6];
        swprintf_s(portStr, _countof(portStr), L"%u", connectPort);

        if (GetAddrInfoW(connectIP, portStr, &hints, &addrInfo) == 0) {
            connectResult = (connect(client->socket, addrInfo->ai_addr, (int)addrInfo->ai_addrlen) == 0);
            FreeAddrInfoW(addrInfo);
        }
    }
    else {
        // 对于IPv4，使用WSAStringToAddressW直接转换
        struct sockaddr_in serverAddr;
        ZeroMemory(&serverAddr, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(connectPort);

        // 将宽字符IP地址转换为sockaddr_in
        INT addrLen = sizeof(serverAddr);
        WCHAR ipPortStr[128];
        ZeroMemory(ipPortStr, sizeof(ipPortStr));
        swprintf_s(ipPortStr, _countof(ipPortStr), L"%s:%u", connectIP, connectPort);

        if (WSAStringToAddressW(ipPortStr, AF_INET, NULL, (LPSOCKADDR)&serverAddr, &addrLen) == 0) {
            connectResult = (connect(client->socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == 0);
        }
        else {
            // 如果直接转换失败，尝试使用getaddrinfo
            struct addrinfoW hints, * addrInfo = NULL;
            ZeroMemory(&hints, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            WCHAR portStr[6];
            ZeroMemory(portStr, sizeof(portStr));
            swprintf_s(portStr, _countof(portStr), L"%u", connectPort);

            if (GetAddrInfoW(connectIP, portStr, &hints, &addrInfo) == 0) {
                connectResult = (connect(client->socket, addrInfo->ai_addr, (int)addrInfo->ai_addrlen) == 0);
                FreeAddrInfoW(addrInfo);
            }
        }
    }

    // 等待连接完成
    if (!connectResult) {
        struct timeval selectTimeout;
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(client->socket, &writeSet);
        selectTimeout.tv_sec = timeout / 1000;
        selectTimeout.tv_usec = (timeout % 1000) * 1000;

        int selectResult = select((int)client->socket + 1, NULL, &writeSet, NULL, &selectTimeout);
        if (selectResult <= 0) {
            BB_SAFE_CLOSE_SOCKET(client->socket);
            return FALSE;
        }
    }

    // 代理连接处理
    if (proxyType != BB_PROXY_NONE) {
        BOOL proxyResult = FALSE;
        switch (proxyType) {
        case BB_PROXY_SOCKS4:
            proxyResult = BB_Client_SOCKS4ProxyW(client, serverIP, serverPort,
                proxyAccount, proxyPassword);
            break;
        case BB_PROXY_SOCKS5:
            proxyResult = BB_Client_SOCKS5ProxyW(client, serverIP, serverPort,
                proxyAccount, proxyPassword);
            break;
        case BB_PROXY_HTTP:
            proxyResult = BB_Client_HTTPProxyW(client, serverIP, serverPort,
                proxyAccount, proxyPassword);
            break;
        default:
            BB_SAFE_CLOSE_SOCKET(client->socket);
            return FALSE;
        }

        if (!proxyResult) {
            BB_SAFE_CLOSE_SOCKET(client->socket);
            return FALSE;
        }
    }

    // 创建操作结构并投递到IOCP
    P_BB_TCP_CLIENT_OPERATION operation = (P_BB_TCP_CLIENT_OPERATION)BB_Alloc(sizeof(BB_TCP_CLIENT_OPERATION));
    if (operation == NULL) {
        BB_SAFE_CLOSE_SOCKET(client->socket);
        return FALSE;
    }

    ZeroMemory(operation, sizeof(BB_TCP_CLIENT_OPERATION));
    operation->client = client;
    operation->operationType = 1;  // 连接完成状态
    operation->socketHandle = client->socket;

    // 投递连接完成通知
    if (!PostQueuedCompletionStatus(g_bbClientIOCP, 0, (ULONG_PTR)client, (LPOVERLAPPED)operation)) {
        BB_SAFE_FREE(operation);
        BB_SAFE_CLOSE_SOCKET(client->socket);
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief 开始接收数据
 */
static BOOL BB_Client_StartRecv(P_BB_TCP_CLIENT client, P_BB_TCP_CLIENT_OPERATION operation) {

    operation->client = client;
    operation->operationType = 2;

    WSABUF wsaBuffer;
    wsaBuffer.buf = operation->buffer + operation->bufferOffset;
    wsaBuffer.len = operation->bufferCapacity - operation->bufferOffset;

    DWORD flags = 0;
    DWORD bytesReceived = 0;

    if (WSARecv(client->socket, &wsaBuffer, 1, &bytesReceived, &flags,
        (LPWSAOVERLAPPED)operation, NULL) != 0) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * @brief 连接完成后初始化接收
 */
static BOOL BB_Client_InitRecvAfterConnect(P_BB_TCP_CLIENT client, P_BB_TCP_CLIENT_OPERATION operation) {
    BB_SAFE_FREE(operation->buffer);

    operation->buffer = (char*)BB_Alloc(client->bufferSize);
    if (operation->buffer == NULL) {
        return FALSE;
    }
    operation->bufferCapacity = client->bufferSize;

    operation->operationType = 2;  // 接收状态
    operation->bufferOffset = 0;
    operation->client = client;

    return BB_Client_StartRecv(client, operation);
}

/**
 * @brief 处理接收到的数据
 */
static BOOL BB_Client_ProcessRecvData(P_BB_TCP_CLIENT client, P_BB_TCP_CLIENT_OPERATION operation) {
    if (client->eventCallback != NULL) {
        client->eventCallback((HBBCLIENT)client, BB_EVENT_DATA_RECEIVED,
            operation->buffer, operation->bytesTransferred);
    }

    operation->bufferOffset = 0;
    return BB_Client_StartRecv(client, operation);
}

/**
 * @brief 发送数据
 */
static BOOL BB_Client_SendData(P_BB_TCP_CLIENT client, const char* data, DWORD length) {
    if (client == NULL || data == NULL || length == 0 || client->socket == INVALID_SOCKET) {
        return FALSE;
    }

    int result = send(client->socket, data, length, 0);
    return result != SOCKET_ERROR;
}

/**
 * @brief 内部关闭客户端
 */
static BOOL BB_Client_CloseInternal(P_BB_TCP_CLIENT client) {
    if (client == NULL) {
        return FALSE;
    }

    client->isConnected = TRUE;
    BB_SAFE_CLOSE_SOCKET(client->socket);
    client->socket = INVALID_SOCKET;
    BB_SAFE_FREE(client->userDataString);
    return TRUE;
}

// ==================== IOCP工作线程 ====================

/**
 * @brief 客户端IOCP工作线程
 */
static DWORD WINAPI BB_Client_IOCPWorkerThread(LPVOID param) {
    while (g_bbClientRunning) {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        P_BB_TCP_CLIENT_OPERATION operation = NULL;

        if (!GetQueuedCompletionStatus(g_bbClientIOCP, &bytesTransferred,
            &completionKey, (LPOVERLAPPED*)&operation, INFINITE)) {
            if (operation == NULL) {
                continue;
            }

            // IO操作失败，通知连接断开
            if (operation->client->eventCallback != NULL && operation->client->isConnected == FALSE) {
                operation->client->eventCallback((HBBCLIENT)operation->client, BB_EVENT_DISCONNECTED, NULL, 0);
            }

            BB_SAFE_FREE(operation->buffer);
            BB_SAFE_FREE(operation->client->userDataString);
            operation->client->isConnected = FALSE;
            BB_SAFE_FREE(operation);
            continue;
        }

        if (operation == NULL) {
            continue;
        }
        if (operation->client->isConnected) {
            BB_SAFE_FREE(operation->buffer);
            BB_SAFE_FREE(operation->client->userDataString);
            operation->client->isConnected = FALSE;
            BB_SAFE_FREE(operation);
            continue;
        }


        operation->bytesTransferred = bytesTransferred;

        switch (operation->operationType) {
        case 1:  // 连接完成
            //operation->client->isConnected = TRUE;
            if (operation->client->eventCallback != NULL) {
                operation->client->eventCallback((HBBCLIENT)operation->client, BB_EVENT_CONNECTED, NULL, 0);
            }

            if (!BB_Client_InitRecvAfterConnect(operation->client, operation)) {
                if (operation->client->eventCallback != NULL && operation->client->isConnected == FALSE) {
                    operation->client->eventCallback((HBBCLIENT)operation->client, BB_EVENT_DISCONNECTED, NULL, 0);
                }
                if (operation->client->isConnected) {
                    BB_SAFE_FREE(operation->buffer);
                    BB_SAFE_FREE(operation->client->userDataString);
                    operation->client->isConnected = FALSE;
                    BB_SAFE_FREE(operation);
                    continue;
                }

                BB_SAFE_CLOSE_SOCKET(operation->client->socket);
                BB_SAFE_FREE(operation->buffer);
                BB_SAFE_FREE(operation->client->userDataString);
                operation->client->isConnected = FALSE;
                BB_SAFE_FREE(operation);
                continue;
            }
            break;

        case 2:  // 数据接收
            if (bytesTransferred == 0) {
                // 连接断开
                if (operation->client->eventCallback != NULL) {
                    operation->client->eventCallback((HBBCLIENT)operation->client, BB_EVENT_DISCONNECTED, NULL, 0);
                }
                BB_SAFE_CLOSE_SOCKET(operation->client->socket);
                BB_SAFE_FREE(operation->buffer);
                BB_SAFE_FREE(operation->client->userDataString);
                operation->client->isConnected = FALSE;
                BB_SAFE_FREE(operation);
                continue;
            }

            if (!BB_Client_ProcessRecvData(operation->client, operation)) {
                if (operation->client->eventCallback != NULL && !operation->client->isConnected) {
                    operation->client->eventCallback(operation->client, BB_EVENT_DISCONNECTED, NULL, 0);
                }
                if (operation->client->isConnected) {
                    BB_SAFE_FREE(operation->buffer);
                    BB_SAFE_FREE(operation->client->userDataString);
                    operation->client->isConnected = FALSE;
                    BB_SAFE_FREE(operation);
                    continue;
                }

                BB_SAFE_CLOSE_SOCKET(operation->client->socket);
                BB_SAFE_FREE(operation->buffer);
                BB_SAFE_FREE(operation->client->userDataString);
                operation->client->isConnected = FALSE;
                BB_SAFE_FREE(operation);
                continue;
            }
            break;
        }
    }

    BB_SAFE_CLOSE_HANDLE(g_bbClientIOCP);
    return 0;
}

// ==================== 客户端公共接口函数实现 ====================

int WINAPI BB_Client_Load() {
    if (g_bbClientIOCP != NULL) {
        return 1;
    }

    struct WSAData wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 0;
    }

    if (LOBYTE(wsa.wVersion) != 2 || HIBYTE(wsa.wVersion) != 2) {
        WSACleanup();
        return 0;
    }

    g_bbClientIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (g_bbClientIOCP == NULL) {
        WSACleanup();
        return 0;
    }

    return 1;
}

int WINAPI BB_Client_Initialize(DWORD threadCount) {
    if (g_bbClientThreadCount > 0) {
        return 1; // 已经初始化
    }

    // 设置线程数
    if (threadCount == 0) {
        SYSTEM_INFO systemInfo;
        GetSystemInfo(&systemInfo);
        g_bbClientThreadCount = systemInfo.dwNumberOfProcessors * 2;
        if (g_bbClientThreadCount < 2) {
            g_bbClientThreadCount = 2;
        }
    }
    else {
        g_bbClientThreadCount = threadCount;
    }

    // 创建工作线程
    for (DWORD i = 0; i < g_bbClientThreadCount; i++) {
        HANDLE thread = CreateThread(NULL, 0, BB_Client_IOCPWorkerThread, NULL, 0, NULL);
        if (thread != NULL) {
            CloseHandle(thread);
        }
    }
    return 1;
}

HBBCLIENT WINAPI BB_Client_Connect(BB_CLIENT_EVENT_CALLBACK callback,
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
    BOOL useIPv6) {

    if (serverIP == NULL) {
        return NULL;
    }

    P_BB_TCP_CLIENT client = (P_BB_TCP_CLIENT)BB_Alloc(sizeof(BB_TCP_CLIENT));
    if (client == NULL) {
        return NULL;
    }

    ZeroMemory(client, sizeof(BB_TCP_CLIENT));
    client->socket = INVALID_SOCKET;

    BOOL result = BB_Client_ConnectInternal(client, callback, serverIP, serverPort,
        timeout, bufferSize, maxPacketSize, proxyType,
        proxyIP, proxyPort, proxyAccount, proxyPassword, useIPv6);

    if (result) {
        return (HBBCLIENT)client;
    }

    BB_SAFE_FREE(client);
    return NULL;
}

int WINAPI BB_Client_Send(HBBCLIENT client, const char* data, ULONG size) {
    if (client == NULL || data == NULL || size == 0) {
        return 0;
    }

    P_BB_TCP_CLIENT internalClient = (P_BB_TCP_CLIENT)client;
    return BB_Client_SendData(internalClient, data, size) ? 1 : 0;
}

BOOL WINAPI BB_Client_Close(HBBCLIENT client) {
    if (client == NULL) {
        return FALSE;
    }

    P_BB_TCP_CLIENT internalClient = (P_BB_TCP_CLIENT)client;
    BB_Client_CloseInternal(internalClient);
    while (internalClient->isConnected) {
        Sleep(1);
    }
    BB_SAFE_FREE(internalClient);
    return TRUE;
}

void WINAPI BB_Client_SetUserString(HBBCLIENT client, LPCWSTR userData) {
    if (client == NULL) {
        return;
    }

    P_BB_TCP_CLIENT internalClient = (P_BB_TCP_CLIENT)client;
    BB_SAFE_FREE(internalClient->userDataString);

    if (userData != NULL) {
        size_t dataLength = wcslen(userData);
        internalClient->userDataString = (WCHAR*)BB_Alloc((dataLength + 1) * sizeof(WCHAR));
        if (internalClient->userDataString != NULL) {
            wcscpy_s(internalClient->userDataString, dataLength + 1, userData);
        }
    }
}

WCHAR* WINAPI BB_Client_GetUserString(HBBCLIENT client) {
    if (client == NULL) {
        return NULL;
    }

    P_BB_TCP_CLIENT internalClient = (P_BB_TCP_CLIENT)client;
    if (internalClient->userDataString == NULL) {
        return NULL;
    }

    size_t dataLength = wcslen(internalClient->userDataString);
    WCHAR* copy = (WCHAR*)BB_Alloc((dataLength + 1) * sizeof(WCHAR));
    if (copy != NULL) {
        wcscpy_s(copy, dataLength + 1, internalClient->userDataString);
    }
    return copy;
}

void WINAPI BB_Client_SetUserInt(HBBCLIENT client, int userData) {
    if (client != NULL) {
        P_BB_TCP_CLIENT internalClient = (P_BB_TCP_CLIENT)client;
        internalClient->userDataInt = userData;
    }
}

int WINAPI BB_Client_GetUserInt(HBBCLIENT client) {
    if (client == NULL) {
        return 0;
    }

    P_BB_TCP_CLIENT internalClient = (P_BB_TCP_CLIENT)client;
    return internalClient->userDataInt;
}

int WINAPI BB_Client_Cleanup() {
    g_bbClientRunning = FALSE;
    return 1;
}