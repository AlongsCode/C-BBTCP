
# ---

**🚀 BB Network Library (BB 库)**

**BB Network Library** 是一个高性能的 Windows IOCP 网络通信库，专为 C 语言开发者打造，旨在实现高并发场景下的高效异步通信。

## ---

**🏗️ 架构概览**

本库采用 **分层解耦设计**，确保核心 IO 逻辑与业务层之间的清晰边界。

* **公共层 (Interface Layer)**：对外暴露稳定的 C API，隔离了复杂的 Windows 网络头文件污染。  
* **实现层 (Internal Layer)**：处理 winsock2.h 状态机、IOCP 异步投递逻辑及资源自动管理。

## ---

**🛠️ 核心特性**

| 特性 | 描述 |
| :---- | :---- |
| **高性能 IOCP** | 利用 Windows 完成端口模型，支持成千上万的高并发连接。 |
| **异步事件模型** | 采用回调机制，避免 UI 或主逻辑线程阻塞。 |
| **多代理支持** | 原生支持 SOCKS4/SOCKS5 及 HTTP 代理，应对复杂网络。 |
| **Unicode 兼容** | 全面使用 WCHAR，确保全球化语言环境的稳定性。 |
| **内存安全** | 提供封装好的 BB\_Alloc/BB\_Free 内存工具，辅助防范泄漏。 |

## ---

**⚡ 快速入门**

### **1\. 初始化 (Initialization)**

初始化时可根据需要设定线程数，推荐设为 0 以启用自动核心数分配。

C

// 客户端加载与启动  
BB\_Client\_Load();  
BB\_Client\_Initialize(0); 

### **2\. 服务器创建 (Server Lifecycle)**

通过简洁的 API 快速实例化一个服务器监听：

C

HBBSERVER server \= BB\_Server\_Create(  
    8080, AcceptCallback, RecvCallback, CloseCallback,   
    NULL, 10, TRUE, TRUE, 4096, FALSE, 4096, 0  
);

### **3\. 事件回调处理 (Event Handling)**

使用标准回调处理数据包接收：

C

void \_\_stdcall RecvCallback(HBBSERVER server, HBBCCLIENT client, const char\* data, ULONG size) {  
    // 处理逻辑：例如回声服务  
    BB\_Server\_Send(client, data, size);   
}

## ---

**⚠️ 开发注意事项 (Best Practices)**

1. **内存管理**：对于通过库接口（如 BB\_Server\_GetClientIPW）获取的字符串内存，**务必在用后调用 BB\_Free**，否则将引发内存泄漏。  
2. **TCP 粘包与拆包**：TCP 是流式协议，回调可能收到不完整的数据。**强烈建议**在应用层增加自定义协议头（Header）来标记数据长度。  
3. **并发资源管理**：由于本库会内部创建工作线程池，若宿主程序同时也拥有大规模计算任务，请注意 CPU 调度优先级。

## ---

**📋 环境与依赖**

* **OS**: Windows Vista 及以上  
* **IDE**: Visual Studio (MSVC)  
* **Dependencies**: ws2\_32.lib, Crypt32.lib (已通过代码自动链接)

---

**许可证**: 遵循 MIT 开源协议。

