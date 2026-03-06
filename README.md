BB Network Library (BB 库)
BB Network Library 是一个基于 Windows 完成端口（IOCP）模型设计的高性能异步 TCP 网络通信库。它采用 C 语言编写，旨在为 Windows 开发者提供轻量、高效且易于集成的网络通信能力。

核心特性
高性能 IOCP 架构：利用 Windows 底层异步 IO 模型，支持大规模并发连接与高吞吐量数据传输。

异步回调机制：通过事件回调（Event Callback）处理连接、接收数据和断开连接，避免阻塞主线程。

协议灵活：支持 TCP 协议，并内置对 SOCKS4、SOCKS5 及 HTTP 代理的支持，适用于复杂网络环境。

跨兼容性：统一采用宽字符（WCHAR）处理，确保在不同 Windows 区域设置下的稳定表现。

资源透明化：提供简洁的内存与字符串管理工具，保障内存使用的安全性。

架构说明
BB 库将逻辑分为两层，以降低系统耦合：

公共层 (bb_server.h, bb_client.h)：为开发者提供简洁、稳定的 API 接口，不直接包含繁琐的 Windows 系统头文件。

实现层 (bb_internal.h, bb_server.c, bb_client.c)：处理底层的 winsock2.h 与 IOCP 状态机，封装了复杂的网络 IO 逻辑。

快速入门
1. 初始化
在调用网络功能前，请先初始化库。可以将 threadCount 设置为 0，让库根据 CPU 核心数自动优化线程池大小。

C

// 客户端初始化示例
BB_Client_Load();
BB_Client_Initialize(0); 
2. 服务器实现 (示例片段)
C

HBBSERVER server = BB_Server_Create(8080, AcceptCallback, RecvCallback, CloseCallback, NULL, 10, TRUE, TRUE, 4096, FALSE, 4096, 0);
3. 事件回调
通过 __stdcall 回调函数处理网络事件：

C

void __stdcall RecvCallback(HBBSERVER server, HBBCCLIENT client, const char* data, ULONG size) {
    // 处理接收到的数据
    BB_Server_Send(client, data, size); // 回声示例
}
编译与环境要求
操作系统：Windows Vista 或更高版本。

工具链：支持 MSVC 的 IDE (Visual Studio)。

依赖库：ws2_32.lib, Crypt32.lib（已在内部通过 #pragma comment 自动引入）。

注意事项
内存释放：对于通过 BB_Server_GetClientIPW 或 BB_Client_GetUserString 获取的字符串，调用者必须使用 BB_Free 手动释放内存，否则会导致内存泄漏。

粘包处理：由于 TCP 是流式协议，目前库将接收到的原始字节流透传给回调函数。建议在应用层实现包头长度字段以解析完整的数据包。

线程模型：库内部创建的工作线程与宿主进程共享资源。在集成到大型框架时，请注意线程池的并发竞争情况。

许可证
本项目遵循 MIT 协议
