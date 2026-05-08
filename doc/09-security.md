# 9. 安全加密

## 目录

- [9.1 概述](#91-概述)
- [9.2 编译要求](#92-编译要求)
- [9.3 SecurityType 与 Security 类别名](#93-securitytype-与-security-类别名)
- [9.4 Security 类](#94-security-类)
- [9.5 设置对称密钥](#95-设置对称密钥)
- [9.6 自定义加密回调](#96-自定义加密回调)
- [9.7 内置 AES-128-CBC 算法](#97-内置-aes-128-cbc-算法)
- [9.8 不支持安全加密的组合](#98-不支持安全加密的组合)
- [9.9 VLINK_SSL_* 环境变量（传输层 TLS）](#99-vlink_ssl_-环境变量传输层-tls)
- [9.10 完整使用示例](#910-完整使用示例)
- [9.11 性能与最佳实践](#911-性能与最佳实践)

---

## 9.1 概述

VLink 支持在**消息层面**对传输内容加密：加密发生在序列化之后、传输之前，解密发生在接收之后、反序列化之前，因此大多数传输后端对上层透明。

核心设计：

- **透明接入**：普通节点类型换成 `Security*` 别名，或把模板参数 `SecT` 设为 `SecurityType::kWithSecurity`，业务代码不变。
- **线程安全**：`Security` 类用 `std::mutex` 保护 EVP 上下文，多线程调用安全。
- **可替换算法**：默认 AES-128-CBC；通过 `set_callbacks()` 可注入任意算法（SM4、ChaCha20、HSM 等）。
- **按需开销**：`SecT == kWithoutSecurity`（默认）时没有任何加密代码路径。

支持的通信模型：

| 通信模型   | 发送端（加密）             | 接收端（解密）               |
| ---------- | -------------------------- | ---------------------------- |
| Event      | `SecurityPublisher<T>`     | `SecuritySubscriber<T>`      |
| Method     | `SecurityClient<Req,Resp>` | `SecurityServer<Req,Resp>`   |
| Field      | `SecuritySetter<T>`        | `SecurityGetter<T>`          |

![安全加密管道](images/security-pipeline.png)

---

## 9.2 编译要求

### 内置 AES 模式

顶层 CMake 选项 `ENABLE_SECURITY` 默认 `ON`，开启后会链接 OpenSSL 并在编译单元里定义宏 `VLINK_ENABLE_SECURITY`。

```cmake
# CMake 配置
cmake -DENABLE_SECURITY=ON ...

# 用户侧只需链接 vlink::vlink，OpenSSL 已通过 vlink 传递
target_link_libraries(my_app PRIVATE vlink::vlink)
```

未定义 `VLINK_ENABLE_SECURITY` 时（例如刻意关掉 `ENABLE_SECURITY`），`Security::encrypt()` / `decrypt()` 会打印 warning 并返回 `false`。

### 自定义回调模式

通过 `set_callbacks()` 注入算法时不依赖 OpenSSL，`ENABLE_SECURITY` 可关；此时仍可以使用 `SecurityPublisher` 等类型，只要回调自身是自洽的。

---

## 9.3 SecurityType 与 Security 类别名

所有通信节点的模板签名都携带 `SecT` 参数：

```cpp
template <typename MsgT, SecurityType SecT = SecurityType::kWithoutSecurity>
class Publisher;

template <typename ReqT, typename RespT = Traits::EmptyType,
          SecurityType SecT = SecurityType::kWithoutSecurity>
class Client;

// Subscriber / Server / Setter / Getter 同理
```

`SecurityType` 枚举（`include/vlink/impl/types.h`）：

| 值  | 枚举                           | 含义                     |
| --- | ------------------------------ | ------------------------ |
| 0   | `SecurityType::kWithoutSecurity` | 不启用加密（默认）       |
| 1   | `SecurityType::kWithSecurity`    | 启用消息级加密/解密      |

两种写法等价：

```cpp
// 直接指定模板参数
vlink::Publisher<MyMsg, vlink::SecurityType::kWithSecurity> pub("shm://topic");

// 使用 Security* 别名（推荐，更简洁）
vlink::SecurityPublisher<MyMsg> pub("shm://topic");
```

别名定义于各自头文件：`publisher.h` / `subscriber.h` / `client.h` / `server.h` / `setter.h` / `getter.h`，对应 `SecurityPublisher` / `SecuritySubscriber` / `SecurityClient` / `SecurityServer` / `SecuritySetter` / `SecurityGetter`。

---

## 9.4 Security 类

头文件：`include/vlink/extension/security.h`。`Node` 基类在 `SecT == kWithSecurity` 时持有一个 `Security` 实例，发送路径自动调用 `encrypt()`、接收路径自动调用 `decrypt()`。

接口：

```cpp
class Security final {
 public:
  using Callback = vlink::MoveFunction<bool(const Bytes& in, Bytes& out)>;

  Security();
  ~Security();

  void set_key(const std::string& key);
  void set_callbacks(Callback&& encrypt_callback, Callback&& decrypt_callback);

  bool encrypt(const Bytes& in, Bytes& out);
  bool decrypt(const Bytes& in, Bytes& out);
};
```

| 方法                 | 说明                                                        |
| -------------------- | ----------------------------------------------------------- |
| `set_key(key)`       | 设置 AES-128 密钥；传空字符串恢复默认 `"vlink"`             |
| `set_callbacks(e,d)` | 同时安装加密/解密回调；安装后**完全绕过内置 AES**           |
| `encrypt(in, out)`   | 加密，`in` 为空时原样返回 `true`；失败返回 `false`         |
| `decrypt(in, out)`   | 解密，`in` 为空时原样返回 `true`；失败返回 `false`         |

---

## 9.5 设置对称密钥

`Node::set_security_key(const std::string&)` 定义于 `include/vlink/node.h`，模板实现要求 `SecT == kWithSecurity`（用 `static_assert` 编译期校验）。

调用约定：

- 可在 `init()` 前或 `init()` 后调用。
- 双端必须**相同密钥**，否则解密失败，收到损坏的消息。
- 传空字符串恢复内置默认密钥。

默认密钥与 IV（源码 `src/extension/security.cc:40-41`）：

| 参数 | 默认值                 | OpenSSL 实际使用                 |
| ---- | ---------------------- | -------------------------------- |
| Key  | `"vlink"`              | AES-128-CBC 读取前 16 字节       |
| IV   | `"thun.lu@zohomail.cn"`| 读取前 16 字节                   |

> 默认密钥/IV 是公开的，**生产环境务必替换**。

示例：

```cpp
vlink::SecurityPublisher<MyMsg> pub("shm://secure/topic");
pub.set_security_key("my-32-char-production-secret-key");

vlink::SecuritySubscriber<MyMsg> sub("shm://secure/topic");
sub.set_security_key("my-32-char-production-secret-key");
sub.listen([](const MyMsg& msg) { /* 已自动解密 */ });
```

---

## 9.6 自定义加密回调

`Node::set_security_callbacks(Security::Callback&&, Security::Callback&&)`（`node.h`）用于注入非 AES 算法（SM4、ChaCha20、HSM 等）。同样需要 `SecT == kWithSecurity`。

回调签名：

```cpp
using Security::Callback = vlink::MoveFunction<bool(const Bytes& in, Bytes& out)>;
```

- 发送端：`in` 为明文，实现写入 `out` 作为密文。
- 接收端：`in` 为密文，实现写入 `out` 作为明文。
- 返回 `false` 时消息被丢弃。

调用约定：

- 安装后**完全绕过内置 AES**（不需要 `VLINK_ENABLE_SECURITY`）。
- `encrypt_callback` 和 `decrypt_callback` 必须成对安装（底层 `Security::set_callbacks()` 同时更新两者）。
- 传入两个 `nullptr` 可恢复到内置 AES 路径。

### 使用示例

```cpp
#include <vlink/publisher.h>
#include <vlink/subscriber.h>
#include <vlink/base/bytes.h>

// 自定义简单异或加密（仅用于演示，实际请勿使用）
const uint8_t kXorKey = 0xAB;

auto xor_encrypt = [](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
    out = vlink::Bytes::create(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        out[i] = in[i] ^ kXorKey;
    }
    return true;
};

auto xor_decrypt = [](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
    out = vlink::Bytes::create(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        out[i] = in[i] ^ kXorKey;  // 对称操作
    }
    return true;
};

// Publisher 端
vlink::SecurityPublisher<vlink::Bytes> pub("dds://secure/data");
pub.set_security_callbacks(std::move(xor_encrypt), std::move(xor_decrypt));

// Subscriber 端（注意：MoveFunction 已被 move 走，需要重新构造一份给 sub）
auto xor_encrypt2 = [](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
    out = vlink::Bytes::create(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        out[i] = in[i] ^ kXorKey;
    }
    return true;
};
auto xor_decrypt2 = [](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
    out = vlink::Bytes::create(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        out[i] = in[i] ^ kXorKey;
    }
    return true;
};

vlink::SecuritySubscriber<vlink::Bytes> sub("dds://secure/data");
sub.set_security_callbacks(std::move(xor_encrypt2), std::move(xor_decrypt2));
sub.listen([](const vlink::Bytes& msg) {
    // msg 已经被 xor_decrypt 解密
});
```

---

## 9.7 内置 AES-128-CBC 算法

源码 `src/extension/security.cc`，仅在 `VLINK_ENABLE_SECURITY` 定义时生效。

| 参数     | 值                                                   |
| -------- | ---------------------------------------------------- |
| 算法     | AES-128-CBC（OpenSSL EVP API）                       |
| 密钥     | OpenSSL 按 16 字节读取；短于 16 字节时 OpenSSL 行为  |
| IV       | OpenSSL 按 16 字节读取                               |
| 填充     | PKCS7（`EVP_CIPHER_CTX_set_padding(ctx, 1)`）        |
| 线程安全 | 是（`std::mutex` 保护 EVP 上下文）                   |

### 加密流程

```
发送端：
  原始消息 MsgT
      |
  序列化（Protobuf/Bytes/...）
      |
  encrypt(plain_bytes) -> cipher_bytes    [Security 层]
      |
  通过传输后端发送 cipher_bytes

接收端：
  收到 cipher_bytes
      |
  decrypt(cipher_bytes) -> plain_bytes    [Security 层]
      |
  反序列化 plain_bytes -> MsgT
      |
  用户回调(msg)
```

要点：

- **填充膨胀**：PKCS7 对齐到 16 字节块，加密最多增加 16 字节。小消息增幅相对显著。
- **空输入**：`Bytes::empty()` 时 `encrypt()` / `decrypt()` 立刻返回 `true`，不修改 `out`。
- **未启用**：未定义 `VLINK_ENABLE_SECURITY` 且未安装自定义回调时，`encrypt()` / `decrypt()` 打印 `VLOG_W` 并返回 `false`；消息被丢弃。

---

## 9.8 不支持安全加密的组合

`include/vlink/internal/node-inl.h:182-191` 中 `set_security_key()` 与 `set_security_callbacks()` 对以下传输组合会触发 `VLOG_F` 警告：

- `intra://`：进程内直接传对象，不进入序列化/加密管道。
- `dds://` 配合 CDR 类型（`is_cdr_type == true`）：CDR 直接交给 Fast-DDS 处理，不经过 VLink 的 Bytes 管道。

这些组合**当前只打印 fatal 级别日志，不抛出异常**，密钥也不会实际作用。其他传输后端（shm/shm2/ddsc/ddsr/ddst/zenoh/mqtt/fdbus/someip/qnx 以及 `dds://` 的非 CDR 类型）均支持消息级加密。

如需在 CDR 链路上保护消息，请使用 DDS Security 插件（FastDDS 官方方案）或传输层 TLS。

---

## 9.9 VLINK_SSL_* 环境变量（传输层 TLS）

以下环境变量由 `src/impl/ssl_options.cc` 读取，用于为 MQTT / 代理等传输层端点提供 TLS 选项。**它们与 9.4–9.8 节的消息级加密无关**。

| 变量                | 作用                                     |
| ------------------- | ---------------------------------------- |
| `VLINK_SSL_VERIFY`  | 是否验证对端证书                         |
| `VLINK_SSL_CA`      | CA 证书文件路径                          |
| `VLINK_SSL_CERT`    | 客户端证书文件路径                       |
| `VLINK_SSL_KEY`     | 客户端私钥文件路径                       |
| `VLINK_SSL_KEY_PASS`| 私钥解密密码                             |
| `VLINK_SSL_SNI`     | TLS SNI 主机名                           |
| `VLINK_SSL_CIPHERS` | 允许的 TLS 密码套件列表                  |

`SslOptions` 的优先级（`include/vlink/impl/ssl_options.h:218`）：显式 API 设置 > URL 查询参数 (`ssl.ca` 等) > 环境变量 (`VLINK_SSL_*`)。

---

## 9.10 完整使用示例

### 示例 1：三种模型的完整安全通信

来自 `examples/samples/shm_raw/shm_raw.cc` 的参考示例：

```cpp
#include <vlink/vlink.h>
#include <thread>

using namespace vlink;
using namespace std::chrono_literals;

int main() {
    // ----- Method 模型（Client/Server） -----
    SecurityServer<Bytes, Bytes> server("shm://example_raw/method");
    server.listen([](const Bytes& req, Bytes& resp) {
        if (req == Bytes{0x1, 0x2, 0x3}) {
            resp = Bytes::create(1024 * 1024);
            resp[0] = 0xA;
            resp[(1024 * 1024) - 1] = 0xB;
        }
    });

    SecurityClient<Bytes, Bytes> client("shm://example_raw/method");
    auto resp = client.invoke(Bytes{0x1, 0x2, 0x3});
    if (resp.has_value()) {
        VLOG_I("invoke size:", resp.value().size());
    }

    // ----- Event 模型（Publisher/Subscriber），使用自定义密钥 -----
    SecuritySubscriber<Bytes> sub("shm://example_raw/event");
    sub.set_security_key("custom_security");
    sub.listen([](const Bytes& msg) { VLOG_I("received:", msg.to_string()); });

    SecurityPublisher<Bytes> pub("shm://example_raw/event");
    pub.set_security_key("custom_security");
    pub.wait_for_subscribers();
    pub.publish(Bytes::from_string("hello1"));
    pub.publish(Bytes::from_string("hello2"));
    pub.publish(Bytes::from_string("hello3"));

    // ----- Field 模型（Setter/Getter） -----
    SecuritySetter<Bytes> setter("shm://example_raw/field");
    setter.set(Bytes{0xA, 0xB, 0xC});

    SecurityGetter<Bytes> getter("shm://example_raw/field");
    std::this_thread::sleep_for(100ms);

    auto ret = getter.get();
    if (ret.has_value()) {
        VLOG_I("field value:", ret.value());
    }

    return 0;
}
```

### 示例 2：Protobuf 消息的安全传输

```cpp
#include <vlink/publisher.h>
#include <vlink/subscriber.h>
#include "my_message.pb.h"    // Protobuf 生成的消息类

const std::string kSecretKey = "production-aes-key-32bytes-long!";

int main() {
    // 安全 Subscriber
    vlink::SecuritySubscriber<MyMessage> sub("dds://vehicle/status");
    sub.set_security_key(kSecretKey);
    sub.listen([](const MyMessage& msg) {
        // 解密后的 Protobuf 消息
        std::cout << "speed: " << msg.speed() << std::endl;
    });

    // 安全 Publisher
    vlink::SecurityPublisher<MyMessage> pub("dds://vehicle/status");
    pub.set_security_key(kSecretKey);
    pub.wait_for_subscribers();

    MyMessage msg;
    msg.set_speed(60.0f);
    msg.set_heading(180.0f);
    pub.publish(msg);

    return 0;
}
```

### 示例 3：使用自定义 SM4 加密（示意）

```cpp
#include <vlink/publisher.h>
#include <vlink/subscriber.h>
#include <vlink/base/bytes.h>

// 假设有一个 SM4 加密库
// #include "sm4.h"

vlink::Security::Callback make_sm4_encrypt(const std::string& key) {
    return [key](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
        // out = sm4_encrypt(in, key);
        // return true on success;
        out = in;  // 占位
        return true;
    };
}

vlink::Security::Callback make_sm4_decrypt(const std::string& key) {
    return [key](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
        // out = sm4_decrypt(in, key);
        out = in;  // 占位
        return true;
    };
}

int main() {
    const std::string sm4_key = "sm4-16-byte-key!";

    vlink::SecurityPublisher<vlink::Bytes> pub("dds://secure/channel");
    pub.set_security_callbacks(
        make_sm4_encrypt(sm4_key),  // 函数返回 Callback 是右值，可直接传入
        make_sm4_decrypt(sm4_key)
    );

    vlink::SecuritySubscriber<vlink::Bytes> sub("dds://secure/channel");
    sub.set_security_callbacks(
        make_sm4_encrypt(sm4_key),
        make_sm4_decrypt(sm4_key)
    );
    sub.listen([](const vlink::Bytes& msg) {
        // 处理已解密的消息
    });

    pub.wait_for_subscribers();
    pub.publish(vlink::Bytes::from_string("encrypted payload"));

    return 0;
}
```

### 示例 4：延迟初始化配合安全设置

```cpp
#include <vlink/publisher.h>

int main() {
    // 先构造但不初始化
    vlink::SecurityPublisher<std::string> pub(
        "dds://secure/log",
        vlink::InitType::kWithoutInit
    );

    // 在 init() 之前配置安全密钥
    pub.set_security_key("my-app-secret-key-2026");

    // 手动初始化
    pub.init();

    pub.wait_for_subscribers();
    pub.publish(std::string("secure log message"));

    return 0;
}
```

---

## 9.11 性能与最佳实践

### 开销来源

1. **加密计算**：AES-128-CBC 在 AES-NI 下吞吐量通常可达 GB/s 级，未量化数据取决于 CPU。
2. **内存分配**：`encrypt()`/`decrypt()` 内部分配 `std::vector<uint8_t>` 作为 cache，然后赋值给 `out`。
3. **填充膨胀**：PKCS7 最多增加 16 字节。

本仓库未提供官方性能基准，具体数值请在目标平台自行测量。

### 优化建议

- 目标平台启用 AES-NI / ARMv8 Crypto 扩展。
- 高频小消息可考虑聚合后再加密。
- 接入 HSM / 安全芯片时用 `set_security_callbacks()`。
- 对非敏感 topic 保持 `kWithoutSecurity`（默认即是，零额外开销）。

### 1. 密钥管理

```
不要将密钥硬编码在源代码中，推荐做法：
- 从环境变量读取
- 从加密配置文件读取（如 HSM 保护的密钥库）
- 使用密钥派生函数（KDF）从主密钥派生通信密钥
```

```cpp
// 从环境变量读取密钥
const char* key_env = std::getenv("VLINK_SECURITY_KEY");
if (key_env) {
    pub.set_security_key(std::string(key_env));
} else {
    VLOG_E("Security key not configured!");
    return -1;
}
```

### 2. 确保双端密钥一致

Publisher（或 Client/Setter）与 Subscriber（或 Server/Getter）必须使用**完全
相同的密钥或回调算法**。密钥不一致时，解密后的数据是随机乱码，反序列化失败会
触发日志警告，消息被丢弃。

### 3. 不要混用安全和非安全节点

同一 topic 上的安全节点无法与普通节点正常通信：

```cpp
// 错误示例：一端安全，另一端不安全
vlink::SecurityPublisher<Bytes> pub("dds://topic");  // 加密发送
vlink::Subscriber<Bytes> sub("dds://topic");          // 收到密文，无法解密

// 正确做法：双端均启用安全
vlink::SecurityPublisher<Bytes> pub("dds://topic");
vlink::SecuritySubscriber<Bytes> sub("dds://topic");
```

### 4. 安全模式不替代传输层安全

消息级加密保护 payload 内容，不保护：

- 元数据（topic 名称、发现消息、消息长度）
- 传输层握手和发现协议
- 重放攻击（没有内置序号校验）

纵深防御：传输层 TLS（MQTT）、DDS Security 插件加上 VLink 消息级加密。

### 5. intra:// 和 CDR 场景

`intra://` 和 `dds://`+CDR 组合**不生效**（见 9.8 节）。如需保护 CDR 链路，使用 DDS Security 插件或传输层 TLS。

### 6. 安全测试建议

```cpp
// 验证加密/解密对称性
vlink::Security sec;
sec.set_key("test-key");

vlink::Bytes plain = vlink::Bytes::from_string("hello world");
vlink::Bytes cipher;
vlink::Bytes recovered;

bool enc_ok = sec.encrypt(plain, cipher);
bool dec_ok = sec.decrypt(cipher, recovered);

assert(enc_ok && dec_ok);
assert(plain == recovered);
assert(plain != cipher);   // 加密后内容不同
```

---

**相关文档：**

- 传输后端安全兼容性详情请参阅 [传输后端与 URL](07-transport.md)
- 序列化层与安全管道的关系请参阅 [序列化](06-serialization.md)
- Node 生命周期与延迟初始化请参阅 [Node 生命周期](02-node-lifecycle.md)
- Bytes 类的详细 API 请参阅 [基础库](11-base-library.md)
