# CameraFrame 零拷贝图像帧容器示例

## 概述

本示例演示 VLink 的 `zerocopy::CameraFrame` 容器，它是专门为传输摄像头图像帧设计的零拷贝数据结构。示例拆分为**生产者**（`producer`）和**消费者**（`consumer`）两个独立可执行程序，模拟真实的摄像头数据传输场景。

![零拷贝 Loan 机制](../zerocopy_loan/images/zerocopy-loan-flow.png)

## 源文件说明

| 文件 | 描述 |
|------|------|
| `frame_producer.h` | 帧生产辅助：`FrameConfig` 结构体、`create_test_frame()` 工厂函数 |
| `frame_consumer.h` | 帧消费辅助：`print_frame_info()`、`validate_frame()`、`compute_checksum()` |
| `producer.cc` | 生产者：创建并发布 CameraFrame |
| `consumer.cc` | 消费者：接收并验证 CameraFrame |

## 架构设计

![CameraFrame Producer Consumer](images/camera-frame-producer-consumer.png)

## CameraFrame 结构

`CameraFrame` 在 64 位平台上恰好占 80 字节（通过 `static_assert` 验证），加上可变长度的像素数据缓冲区。

```
[Header(40B)] [metadata(40B)] [pixel_data(N bytes)]
```

## 像素格式

| 值 | 格式 | 描述 |
|----|------|------|
| 1-5 | YUV 系列 | YUV420, YUV422, YUV444, NV12, NV21 |
| 6-9 | Packed YUV | YUYV, YVYU, UYVY, VYUY |
| 10-12 | RGB 系列 | BGR888, RGB888 Packed/Planar |
| 101-103 | 压缩格式 | JPEG, H.264, H.265 |

## 视频流类型

| 值 | 类型 | 描述 |
|----|------|------|
| 1 | kStreamI | I 帧（关键帧） |
| 2 | kStreamP | P 帧（预测帧） |
| 3 | kStreamB | B 帧（双向预测帧） |

## 编译与运行

```bash
cd build
cmake .. && make example_camera_producer example_camera_consumer

# 终端 1：启动消费者
./output/bin/example_camera_consumer

# 终端 2：启动生产者
./output/bin/example_camera_producer
```

## 关键 API

### 创建帧（生产者端）

```cpp
zerocopy::CameraFrame frame;
frame.set_width(1920);
frame.set_height(1080);
frame.set_format(zerocopy::CameraFrame::kFormatNv12);
frame.create(1920 * 1080 * 3 / 2);  // NV12 大小
frame.header.seq = seq_number;
pub.publish(frame);
```

### 接收帧（消费者端）

```cpp
sub.listen([](const zerocopy::CameraFrame& frame) {
  // frame.width(), frame.height(), frame.format()
  // frame.data(), frame.size()
  // frame.header.seq, frame.header.time_meas
});
```

## 所有权模型

| 模式 | 创建方式 | is_owner |
|------|---------|----------|
| 拥有 | `create(size)` / `deep_copy()` | true |
| 借用 | `shallow_copy()` / `operator<<` | false |
| 转移 | `move_copy()` | true（源变为空） |

## 实际应用场景

- 摄像头驱动将帧数据发布到 `shm://camera/front`
- 感知算法订阅并处理帧（可以用浅拷贝避免重复分配）
- 录制系统将帧序列化到 Bag 文件
- 压缩帧（JPEG/H.264）直接存储编码数据

## 注意事项

- 32 位架构不受支持（会触发编译期警告）
- `operator<<` 后数据指针引用源 `Bytes` 的内存，`Bytes` 必须存活更久
- `fill_data` 执行深拷贝，适合从硬件驱动拷贝数据
- 结构体大小固定 80 字节，不受图像分辨率影响
- 使用 `shm://` 传输时配合 loan API 可实现真正的零拷贝

## 相关文档

详细原理参见 [doc/10-zerocopy.md](../../../doc/10-zerocopy.md)。
