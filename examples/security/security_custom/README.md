# VLink Security Custom 示例

## 概述

本示例演示 `set_security_callbacks()` 用法——用自定义 encrypt/decrypt 回调**完全绕过**内置 AES-128-CBC，让上层接入业务自有算法（SM4、ChaCha20、HSM、白盒密码等）。

示例覆盖：

1. 通过命名空间函数(`xor_cipher::encrypt` / `xor_cipher::decrypt`)安装 XOR 对称密码
2. 用 lambda 捕获实现 ROT-N 替换密码
3. encrypt/decrypt 回调的契约与失败语义

## 文件说明

| 文件 | 说明 |
|------|------|
| `security_custom.cc` | 主示例源码 |
| `xor_cipher.h` | 演示用 XOR 密码实现（带 `kDefaultKey` 与 `xor_transform`） |
| `CMakeLists.txt` | 构建配置 |

## 构建与运行

```bash
cmake -B build -S . -DENABLE_EXAMPLES=ON -DENABLE_WHOLE_EXAMPLES=ON -DENABLE_SECURITY=ON
cmake --build build --target example_security_custom
./build/output/bin/example_security_custom
```

## 核心 API

```cpp
// 回调签名（在 include/vlink/extension/security.h 中）：
using Callback = vlink::MoveFunction<bool(const Bytes& in, Bytes& out)>;

// 同时安装 encrypt 与 decrypt（必须成对）：
void set_security_callbacks(Security::Callback&& encrypt, Security::Callback&& decrypt);
```

| 行为 | 说明 |
|------|------|
| 安装后 AES 被完全绕过 | 不再调用 OpenSSL，回调即是唯一加解密路径 |
| 双端必须用对称兼容的回调 | 双端 encrypt/decrypt 函数必须使用相同密钥与算法；XOR 自互反指"encrypt 与 decrypt 同一函数",不代表"双端 key 可不同" |
| 回调返回 `false` | 视为加/解密失败：发送端丢弃消息、接收端不触发用户回调 |
| 与 `set_security_key()` 关系 | 安装回调后 AES 路径不再被走，但 `set_security_key()` 仍合法调用——它只更新内部 key，对回调路径无影响（源码：`src/extension/security.cc:88-95, 110-112, 164-166`） |

## 回调最小实现

```cpp
auto enc = [key](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
    out = vlink::Bytes::create(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        out[i] = in.data()[i] ^ key[i % key.size()];
    }
    return true;
};
auto dec = enc;  // XOR 自互反

pub.set_security_callbacks(enc, dec);
```

## 注意事项

- 回调在 `publish` / `listen` 的同一线程被调用，**禁止阻塞**热路径。
- `in` 为空时回调应直接返回 `true`，与默认实现的"空输入直通"约定一致。
- `set_security_callbacks()` 当前**不**对 `intra://` 与 `dds://` CDR 类型做传输检查（不像 `set_security_key()` 会 fatal），在这两种传输上调用会静默接受但加密路径不被触发；需用户自行避免。
- XOR 仅用于演示，**严禁**生产使用。

## 相关文档

- [doc/09-security.md](../../../doc/09-security.md) §9.6 自定义加密回调
- [`../security_basic/`](../security_basic/) — 内置 AES-128-CBC 用法
- `include/vlink/extension/security.h` — `Security` 类完整接口
