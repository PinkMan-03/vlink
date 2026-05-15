# VLink Security Basic 示例

## 概述

本示例演示 `SecurityPublisher` / `SecuritySubscriber` 的基本用法，覆盖：

1. 使用内置默认密钥的 AES-128-CBC 加密
2. 通过 `set_security_key()` 注入自定义密钥
3. 密钥不一致时的解密失败行为
4. 加密 `Bytes` 原始字节
5. 安全加密的传输限制

> ⚠️ **传输要求**：`intra://` 与 `dds://` CDR 类型**不**支持安全加密；可用 `shm://`、`shm2://`、`zenoh://`、`mqtt://`、`fdbus://` 等跨进程后端。

## 文件说明

| 文件 | 说明 |
|------|------|
| `security_basic.cc` | 主示例源码，包含 5 个 Section |
| `security_common.h` | 公共辅助函数（打印默认算法、支持的 transport） |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all`（覆盖全后端，含 dds） |

## 构建与运行

```bash
cmake -B build -S . -DENABLE_EXAMPLES=ON -DENABLE_WHOLE_EXAMPLES=ON -DENABLE_SECURITY=ON
cmake --build build --target example_security_basic
./build/output/bin/example_security_basic
```

## 核心 API

```cpp
template <typename T>
class SecurityPublisher : public Publisher<T, SecurityType::kWithSecurity> { ... };

template <typename T>
class SecuritySubscriber : public Subscriber<T, SecurityType::kWithSecurity> { ... };

// 从 Node 基类继承：
void set_security_key(const std::string& key);  // 必须正好 16 字节
```

| API | 用途 | 调用时机 |
|-----|------|---------|
| `set_security_key(key)` | 注入业务密钥，替换内置 demo 密钥 | `init()` 前或后均可 |
| 不调 `set_security_key` | 沿用内置 demo 密钥（仅用于演示） | — |

## 工作原理

1. **加密**：`SecurityPublisher::publish()` 在发送前对 payload 调用 OpenSSL `EVP_aes_128_cbc()` 加密。
2. **解密**：`SecuritySubscriber` 在分发回调前对收到的 payload 解密；**解密失败时回调不会被触发**（消息被静默丢弃）。
3. **密钥长度**：底层 OpenSSL 直接读取 16 字节，因此 `set_security_key()` 参数**必须正好 16 字节**——短了会越界读、长了多余被忽略且令变量名误导。

## 注意事项

- 双端密钥必须完全相同；任一端漏调 `set_security_key()` 时会沿用内置 demo 密钥，与显式注入的另一端将无法通信。
- 内置 demo 密钥/IV 不应在生产中使用，**部署前必须**注入业务密钥。
- 若要替换 AES 为其它算法（SM4 / ChaCha20 / HSM 等），见 [`../security_custom/`](../security_custom/) 中 `set_security_callbacks()` 用法。

## 相关文档

- [doc/09-security.md](../../../doc/09-security.md) — 完整安全加密章节
- [`../security_custom/`](../security_custom/) — 自定义 encrypt/decrypt 回调
- [`../security_ssl/`](../security_ssl/) — 传输层 SSL/TLS
