# plugin/ -- 插件系统示例

VLink 插件化扩展机制，支持运行时动态加载模块。

| 工程 | 说明 |
|------|------|
| `plugin_basic` | `Plugin::load<T>()` 基本加载流程 |
| `plugin_create` | 自定义插件的创建和导出 |
| `plugin_runnable` | `RunablePluginInterface` 可运行插件 |
| `plugin_schema` | `SchemaPluginInterface` 统一 schema 反射插件 |

## 相关文档

详细原理参见 [doc/19-extensions.md](../../doc/19-extensions.md)。
