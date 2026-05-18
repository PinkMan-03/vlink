# security/ -- 安全加密示例

通信安全功能教程，涵盖 AES 加密、自定义加密和 SSL/TLS。

| 工程 | 说明 |
|------|------|
| `security_basic` | 内置 AES-128-GCM 对称加密（`Security::Config::key`） |
| `security_custom` | 自定义加密回调 |
| `security_rsa` | RSA-OAEP hybrid + 可选 RSA-PSS 签名认证 |
| `security_ssl` | 传输层 SSL/TLS 加密 |

## 1. 相关文档

详细原理参见 [doc/09-security.md](../../doc/09-security.md)。
