# GP2040-CE Hitbox 外挂 2.4 GHz 无线桥

让一台成品 GP2040-CE Hitbox 无线连接 Windows PC。Hitbox 的原装 USB 线插入外挂发射端，电脑旁的接收端通过 USB 提供 Xbox 360 风格的 XInput 接口。整套设计无需拆机或改写 Hitbox 固件，拔掉外挂即可恢复原来的有线玩法。

## 三仓 2×2 生态

| 发射端 ↓ / 接收端 → | PC 迷你接收器 | 主机认证接收盒 |
|---|---|---|
| 外挂成品 Hitbox | pico-host + radio-tx → usb-rx | pico-host + radio-tx → rx-bridge + gp2040-rx |
| 单板 18 键 Hitbox | hitbox-nrf52840 → usb-rx | hitbox-nrf52840 → rx-bridge + gp2040-rx |

[外挂发射端与 PC 接收器](https://github.com/SkyWithClouds/gp2040-wireless) · [单板 Hitbox 与网页](https://github.com/SkyWithClouds/nrf52840-wireless-hitbox) · [主机接收盒](https://github.com/SkyWithClouds/gp2040-wireless-console)

## 无线匹配与双打（v1.1 预览）

出厂网络 ID 为 `B74632DA`，信道 40，速率 2 Mbps。槽位 1 在本机使用 Pipe 1，其基础地址与前缀 C5 保留旧 Pipe 0 的空口地址，原版 v1.0 板可继续配合使用。Slot 2–4 需要升级对应发射板和 PC 接收器。玩家槽位决定无线输入路由到哪个 XInput 接口；Windows 和游戏自行分配控制器序号。

1. 在桌面 Chrome / Edge 用 USB 数据线连接要配置的无线板，打开[WebHID 配置台](https://skywithclouds.github.io/nrf52840-wireless-hitbox/)。修改外挂发射板或主机盒无线桥前，断开板间供电和信号线，单独给 NOLOGO 插 USB。
2. 在「无线匹配与接收器管理」生成一次网络 ID，给同一组设备填写相同的八位 ID 和信道。信道 2–80 对应 2402–2480 MHz。不同比赛台可分别选 ID；同频设备仍共享空口时间。
3. PC 接收器默认启用 2 个槽位，可设 1–4。两台发射端分别选 1、2。主机盒固定使用槽位 1，允许修改 ID、信道。每台保存后等待回读，再完全断电重启；单板 Hitbox 拔线进入无线即加载已保存的设置。
4. PC 接收器网页每约 150 ms 显示每槽状态、收到的有效帧、漏收心跳、重复输入和超时次数，计数从开机开始。漏收心跳按同一会话的心跳编号缺口累计，真实射频丢包率需借助空口测量。
5. 双打实测时让两人同时点按，按住 1P 后断开 1P 电源，检查 1P 释放、2P 仍可操作。交换角色重复，再测修改信道、恢复出厂参数和断电保存。USB 枚举、射频吞吐及实际输入延迟待实板验证。


**阶段 1 已通过 Windows 实板验收。** 阶段 2 提供两块 NOLOGO 的无线测试固件。阶段 3 提供 Pico、发射板、接收板三份正式输入固件。三个阶段均可在 GitHub Actions 构建。首次 `full` 云端构建的四个任务已通过，双板无线和全链路待实板验收。[首轮 full 构建](https://github.com/SkyWithClouds/gp2040-wireless/actions/runs/45192790253)

## 设计目标

- **保留原板手感。** Hitbox 继续处理改键、相反方向输入（SOCD）和快捷键。外挂按原板报告边界转发，保留一次双键投中的两个按键，也保留快速点按的按下与松开。
- **短时丢包保序，断线释放按键。** 轻微丢包时补传缺失状态。明显断线后清零，恢复时同步当前按键，避免把先前的连招延迟送出。
- **无线玩，有线配。** 设计支持将 Windows 的 1P–4P 玩家灯命令传回原板；修改配置时把 Hitbox 直接插电脑。PC 预览版支持同一接收器连接最多四台发射端。

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
| 协议与 CI | v1.0 线格式、C11 编解码、测试向量、Actions 分阶段入口 | 线格式、三级确认、三端状态机在 CI 中测试 |
| 1 · `xinput` | 接收端在 Windows 自动绑定 XInput，收发测试输入和玩家灯命令 | Windows 实板验收通过 |
| 2 · `esb` | 两块 NOLOGO 互通，验证丢包恢复和状态确认 | 已实现，待双板实测 |
| 3 · `full` | 接入 Pico 和真实 Hitbox，对比有线手感与附加延迟 | 云端三端构建通过，待全链路实测 |

刷机时按板的角色选文件。`full` 包的 01 是 Pico，02 是手柄侧 NOLOGO #1，03 是电脑侧 NOLOGO #2。首次联调可先用 `esb` 只测两块 NOLOGO。[实板验收清单](docs/USER_GUIDE.md#实板验收清单)

## 从哪里开始

| 你想做什么 | 阅读 |
|---|---|
| 核对接线、下载固件、分阶段上板 | [玩家使用指南](docs/USER_GUIDE.md) |
| 贡献代码、运行测试、配置板型和云端构建 | [开发与实板验收](docs/DEVELOPMENT.md) |
| 实现兼容端、查字段、调试抓包和时序 | [v1.0 协议规范](docs/PROTOCOL_SPEC.md) |

## 目录

```text
common/protocol.h                三端共用的字节编解码与 CRC
tests/protocol_test.c            固定线格式向量与错误帧测试
pico-host/                       PIO-USB XInput 主机、输入队列、SPI 主机、CDC 日志
radio-tx/                        ESB 发射、SPI 从机、阶段 2 测试输入
usb-rx/                          XInput 接收端，阶段 2 增加 ESB 收包
boards/nologo/nologo_promicro/   NOLOGO 板定义，两块实板已确认 0x26000 起址
west.yml                         Nordic SDK 版本清单
.github/workflows/build.yml      Bridge CI 工作流
docs/                            玩家、开发与协议文档
```

原创代码与文档采用 [MIT 许可证](LICENSE)。
