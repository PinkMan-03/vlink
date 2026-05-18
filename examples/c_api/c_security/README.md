# C API Security 示例

## 概述

本示例演示在纯 C 代码中使用 VLink 的应用层加密 API：

- **独立 `vlink_security_handle_t`**：脱离传输层的 encrypt / decrypt 往返，对 raw symmetric key 与 PBKDF2 passphrase 两种密钥派生方式各做一次。
- **原子 `vlink_create_secure_*`**：使用 `vlink_create_secure_publisher` / `vlink_create_secure_subscriber` 等函数在创建节点时一次性装入 `vlink_security_config_t`，传输层自动加密发送、解密接收。

加密算法：AES-128-GCM（AEAD）+ 12B 随机 nonce + 16B 认证 tag。可通过 `vlink_security_config_t` 切换到 RSA-OAEP hybrid（设置 `public_key_pem` / `private_key_pem`）或 RSA-PSS 签名（设置 `signing_key_pem` / `verify_key_pem`）。

## 编译运行

启用 `ENABLE_SECURITY=ON` + `ENABLE_C_API=ON` + `ENABLE_EXAMPLES=ON`：

```bash
cmake -S . -B build -DENABLE_SECURITY=ON -DENABLE_C_API=ON -DENABLE_EXAMPLES=ON
cmake --build build --target example_c_security
./build/output/bin/example_c_security
```

`shm://` 部分需要在后台先跑 iceoryx 守护进程：

```bash
iox-roudi &
```

未启动 RouDi 时 pub/sub 段会优雅跳过；独立 standalone 段始终可跑。

## 核心 API

### 配置结构体

```c
typedef struct {
  const char* key;                  // SHA-256 截断为 16B AES-128
  const char* passphrase;           // PBKDF2 入参（与 pbkdf2_salt 搭配）
  const uint8_t* pbkdf2_salt;       // ≥ 16 字节
  size_t pbkdf2_salt_size;
  uint32_t pbkdf2_iterations;       // 默认 200000，传 0 也按默认
  const char* public_key_pem;       // 对端 RSA 公钥（PEM）
  const char* private_key_pem;      // 本端 RSA 私钥（PEM）
  const char* signing_key_pem;      // 本端签名私钥
  const char* verify_key_pem;       // 对端验签公钥
  vlink_security_callback_t encrypt_callback;
  vlink_security_callback_t decrypt_callback;
  void* callback_user_data;
} vlink_security_config_t;

void vlink_security_config_init(vlink_security_config_t* cfg);  // 清零 + 默认值
```

### 独立加解密

```c
vlink_security_config_t cfg;
vlink_security_config_init(&cfg);
cfg.key = "my-shared-secret";

vlink_security_handle_t sec = vlink_security_create(&cfg);

uint8_t* cipher = NULL;
size_t   cipher_size = 0;
vlink_security_encrypt(sec, plain, plain_size, &cipher, &cipher_size);

uint8_t* recovered = NULL;
size_t   recovered_size = 0;
vlink_security_decrypt(sec, cipher, cipher_size, &recovered, &recovered_size);

vlink_security_free_buffer(cipher);
vlink_security_free_buffer(recovered);
vlink_security_destroy(sec);
```

### 原子 create_secure

```c
vlink_publisher_handle_t pub;
vlink_create_secure_publisher("shm://demo/secure", &schema, &pub, &cfg);
vlink_publish(pub, data, size);    // 自动加密
vlink_destroy_publisher(&pub);
```

六种节点都有对应的原子构造接口：
`vlink_create_secure_publisher` / `_subscriber` / `_server` / `_client` / `_setter` / `_getter`。

### 返回码

| 代码 | 值 | 含义 |
|---|---|---|
| `VLINK_RET_NO_ERROR` | 0 | 成功 |
| `VLINK_RET_INVALID_ERROR` | 2 | NULL / 无效句柄 / 长度为 0 但指针非空 |
| `VLINK_RET_MEMORY_ERROR` | 3 | MemoryPool 分配失败 |
| `VLINK_RET_RUNTIME_ERROR` | 4 | C++ 构造抛异常（极少） |
| `VLINK_RET_TRANSFER_ERROR` | 5 | encrypt/decrypt 失败（错 key / 篡改 / 短输入） |

## 限制

- `intra://` 与 `dds://` 配 CDR 类型不支持 security —— `vlink_create_secure_*` 会拒绝 `Security::Config` 并返回 `VLINK_RET_INVALID_ERROR`。
- 因为 `Security` 在 `vlink_create_secure_*` 内部于 `listen()` / `init()` 之前装配，所以不存在“前几条消息可能走明文”的窗口。
- `vlink_security_config_t.encrypt_callback` 与 `decrypt_callback` 必须**成对**安装，单独一个会被忽略并打 warning。

## 相关

- C++ 同语义示例：`examples/security/security_basic` / `security_custom` / `security_rsa`
- 头文件：`include/vlink/external/c_api.h`（vlink_security_* / vlink_ssl_options_*）
- 文档：`doc/09-security.md`、`doc/18-c-api.md`
