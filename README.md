# GP2040-CE Hitbox 外挂 2.4 GHz 无线桥

让一台成品 GP2040-CE Hitbox 无线连接 Windows PC。Hitbox 的原装 USB 线插入外挂发射端，电脑旁的接收端通过 USB 提供 Xbox 360 风格的 XInput 接口。整套设计无需拆机或改写 Hitbox 固件，拔掉外挂即可恢复原来的有线玩法。

**当前已完成协议与 CI 骨架，三端固件处于分阶段开发状态。** 阶段 1 已提供用于 #2 接收板的独立 XInput 测试应用；Windows 自动绑定与板型分区待实板验收。无线和 Pico 主机仍在后续阶段。

## 设计目标

- **保留原板手感。** Hitbox 继续处理改键、相反方向输入（SOCD）和快捷键。外挂按原板报告边界转发，保留一次双键投中的两个按键，也保留快速点按的按下与松开。
- **短时丢包保序，断线释放按键。** 轻微丢包时补传缺失状态。明显断线后清零，恢复时同步当前按键，避免把先前的连招延迟送出。
- **无线玩，有线配。** 设计支持将 Windows 的 1P–4P 玩家灯命令传回原板；修改配置时把 Hitbox 直接插电脑。首版面向单人桌面一对一使用。

## 硬件链路

```text
成品 Hitbox ──USB──► 官方 Pico ──SPI──► NOLOGO #1 · 发射端
                                              │ 2.4 GHz ESB
                                              ▼
Windows PC ◄──USB / XInput── NOLOGO #2 · 接收端
```

两块 NOLOGO 均为 ProMicro nRF52840。Pico 用 GP0/GP1 接 Hitbox，原生 Micro-USB 口保留作供电和调试。

## 开发进度

| 阶段 | 交付或实板目标 | 当前状态 |
|---|---|---|
| 协议与 CI | v1.0 线格式、C11 编解码、测试向量、Actions 分阶段入口 | 骨架已完成，协议测试已在本地通过 |
| 1 · `xinput` | 接收端在 Windows 自动绑定 XInput，收发测试输入和玩家灯命令 | 应用已实现，待 Windows 实板验收 |
| 2 · `esb` | 两块 NOLOGO 互通，验证丢包恢复和状态确认 | 待实现与实测 |
| 3 · `full` | 接入 Pico 和真实 Hitbox，对比有线手感与附加延迟 | 待实现与实测 |

## 从哪里开始

| 你想做什么 | 阅读 |
|---|---|
| 买齐硬件、认引脚、了解未来如何刷机 | [玩家使用指南](docs/USER_GUIDE.md) |
| 贡献代码、运行测试、配置板型和云端构建 | [开发与实板验收](docs/DEVELOPMENT.md) |
| 实现兼容端、查字段、调试抓包和时序 | [v1.0 协议规范](docs/PROTOCOL_SPEC.md) |

## 目录

```text
common/protocol.h                三端共用的字节编解码与 CRC
tests/protocol_test.c            固定线格式向量与错误帧测试
pico-host/                       Pico 应用目录（预留）
radio-tx/                        NOLOGO 发射端应用目录（预留）
usb-rx/                          阶段 1 XInput 接收端
boards/nologo/nologo_promicro/   NOLOGO 板定义，按 0x26000 暂定分区
west.yml                         Nordic SDK 版本清单
.github/workflows/build.yml      Bridge CI 工作流
docs/                            玩家、开发与协议文档
```

原创代码与文档采用 [MIT 许可证](LICENSE)。
