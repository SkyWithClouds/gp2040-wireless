# 开发与实板验收

阶段 1 已在 Windows 验收免驱、输入与 1P 灯。阶段 2 和阶段 3 已通过云端构建，双板无线和全链路待实板验收。[两块 NOLOGO 的引导器记录](../boards/nologo/nologo_promicro/README.md) · [v1.0 线格式与确认语义](PROTOCOL_SPEC.md)

## 模块边界

| 目录 | 拥有的状态 | 对外接口 |
|---|---|---|
| `pico-host/` | Hitbox 最终快照、输入变化队列、会话发起、已应用灯版本 | 通过 GP0/GP1 的 PIO-USB Host 读取 XInput，处理原板拔插；SPI 主机向 #1 送记录；玩家灯通过原板 OUT 端点返回 |
| `radio-tx/` | SPI 已接收进度、无线待发送队列、上次有效接收端状态 | 通过 SPIS + EasyDMA 接 Pico，通过 Nordic ESB PTX 接 #2；把预装的状态送回 MISO |
| `usb-rx/` | 会话号、USB 待发队列、USB 完成进度、主机最新灯态 | ESB PRX 接无线输入，Zephyr 自定义 USB Vendor Class 接 Windows；通过 ACK Payload 回报状态 |
| `common/`、`tests/` | 三端共用线格式和固定向量 | `gp_encode`、`gp_decode` 负责格式校验。会话归属、队列边界、顺序与超时由应用状态机检查 |

## 在本地运行协议测试

在 Linux、macOS 或 WSL 安装带 C11 支持的 C 编译器，从仓库根目录运行。测试使用断言，编译时保留断言。

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -Icommon tests/protocol_test.c -o /tmp/test-protocol
/tmp/test-protocol
```

成功时输出：

```text
8 wire vectors, 1536 single-bit errors and malformed records passed
```

固定向量覆盖全部八类消息，逐位翻转覆盖会话前缀、记录和 CRC。测试还检查长度、保留位、非法类型和字段。新增协议行为时，把状态机测试与字节测试分开，例如用“同一序号重复送达”验证只提交一次 USB IN。

## 完成 NOLOGO 板定义

1. 将两块板分别标记为 #1、#2，断开外部电源，逐块进入 UF2 引导盘并保存 `INFO_UF2.TXT`。记录实板丝印、修订号、文件中实际列出的 Model、Board-ID、引导器及 SoftDevice 版本。两块板的信息分别核对。
2. 按上述版本找到对应的引导器板定义、链接脚本和内存布局，核对 SoftDevice 是否占用低地址 Flash、应用起始地址、应用上限、引导器及设置页的保留范围。若信息文件缺少地址或版本，向板厂核对引导器构建资料。排针兼容 nice!nano 只能佐证连线，不能确定 Flash 布局。[Adafruit nRF52 引导器的映像格式与地址说明](https://github.com/adafruit/Adafruit_nRF52_Bootloader)
3. 在 `boards/nologo/nologo_promicro/` 复核已有的 `board.yml`、设备树和对应的板配置，使目标名与工作流的 `nologo_promicro/nrf52840` 一致。在设备树声明实板分区，将应用链接至已核实的代码分区，配置 UF2 输出。核对 nRF52840 应用 UF2 的 family ID（Adafruit nRF52840 为 `0xADA52840`），并确认实板引导器接受该格式。
4. 刷写前检查生成的 `zephyr.dts`、链接 map 和 UF2 数据块。向量表和链接起址应落在应用起始地址，每个数据块的起止地址都在应用分区内。把实板版本、已验证地址范围与验证记录放在该板目录，便于后来者选择固件。

## GitHub Actions 构建入口

[工作流](../.github/workflows/build.yml) 位于默认分支。具有写权限的贡献者可在 **Actions → Bridge CI → Run workflow** 选择分支和 `stage`，再点 **Run workflow**。也可以从已关联该仓库的检出目录运行：

```sh
gh workflow run build.yml -f stage=xinput
```

将参数改为 `esb` 或 `full` 可选择后续阶段。普通 push / pull request 只运行 `protocol`；手动构建须先通过该任务。每次测试记录运行链接、提交 SHA、阶段、板版本和主机系统，以便复现。

| 阶段 | 固件任务 | 成功后上传的 artifact |
|---|---|---|
| `xinput` | `usb-rx` | `usb-rx-xinput-<sha>` |
| `esb` | `usb-rx`、`radio-tx` | `usb-rx-esb-<sha>`、`radio-tx-esb-<sha>` |
| `full` | 两个 nRF 应用及 `pico-host` | `usb-rx-full-<sha>`、`radio-tx-full-<sha>`、`pico-host-full-<sha>` |

nRF 任务使用 Zephyr Arm 工具链 0.16.8 和 [west 清单](../west.yml) 中的 nRF Connect SDK v2.9.2，通过 `--no-sysbuild` 和 `BRIDGE_STAGE` 选择应用。Pico 任务按提交固定 Pico SDK、Pico-PIO-USB、tusb_xinput 及 picotool。使用 `PICO_BOARD=pico` 和 `pico_add_extra_outputs` 生成 RP2040 UF2。上传前逐块验证 Family ID、Flash 地址、复位向量及负载与链接二进制是否一致。首次手动 `full` 运行的协议测试、两个 Nordic 构建和 Pico 构建全部通过。[构建记录](https://github.com/SkyWithClouds/gp2040-wireless/actions/runs/45192790253)

## 实板验收顺序

### 1 · 接收端 XInput 枚举

当前应用分区为 `[0x26000, 0xEC000)`，其后保留存储区，`0xF4000` 起为引导器及设置。Family ID 为 `0xADA52840`。刷写前核对两块板各自的引导器资料；若实板要求 `0x27000` 等其他起址，先修改设备树及 UF2 校验脚本后重新构建。[nice!nano 参考布局](https://github.com/zmkfirmware/zmk/blob/main/app/module/boards/nicekeyboards/nice_nano/nice_nano.dts)

阶段 1 使用共享私有测试 VID/PID `1209:0001`，这个编号不唯一，仅限内部试验。发行硬件前请在 `usb-rx/prj.conf` 换成获分配的 VID/PID。若 Windows 缓存了另一台相同 VID/PID 的测试设备，使用干净的 Windows 实例复测。[pid.codes 测试编号使用条件](https://pid.codes/1209/0001/)

- 只接 #2。在无第三方 INF 的 Windows 10/11 上以新设备实例枚举，记录 VID/PID、兼容 ID、系统绑定的驱动。用 `XInputGetState` 读取已知测试输入，逐项核对按钮、方向、摇杆和扳机。
- 用 USBPcap 记录控制请求、IN 完成及主机真实发出的玩家灯 OUT。将总线上的命令与 #2 调试记录对照。另用命令样本覆盖灭灯、1P–4P 和截断 OUT；确认下一个 OUT 仍能被接收。保存枚举记录与抓包作为本阶段基线。

### 2 · 双 nRF ESB 链路

以下命令运行与固件相同的收发状态机，用可控时钟和延迟的 USB 完成事件检查满窗口、丢包、过期确认、序号回绕、断联和停滞清零。

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -Icommon -Iusb-rx/src -Iradio-tx/src \
  tests/esb_test.c usb-rx/src/receiver.c radio-tx/src/transmitter.c -o /tmp/test-esb
/tmp/test-esb
gh workflow run build.yml -f stage=esb
```

#1 默认内置节拍发生器，经与 SPI 同一套状态机发送。收到第一条有效 SPI HELLO 后，#1 在本次上电期间改用外部输入和心跳；要恢复内置测试，断开 SPI 主机并重启 #1。#2 仅把会话收到的输入提交到 USB。阶段 2 的最后一秒用于观察灯命令到达 #1 后返回 PC 的完整往返。内置输入源不设置原板灯已应用标志。

用 USBPcap 记录 `0x81` 的成功 IN 数据及 `0x02` 的玩家灯 OUT，按[玩家指南](USER_GUIDE.md)的时序核对。4 ms 段预期四个按钮值为 `1000 → 3000 → 2000 → 0000`（十六进制）。记录第一个和最后一个变化的主机时间。只断 #1，在 A 保持期间用同一时间基准记录断电边沿和 #2 中立 IN；50 ms 从最后一次有效新输入或新心跳起算，实际 USB 时间还包括主机轮询。按相反顺序上电及重插 #2，均应恢复到当前快照。

- 用确定的输入序列联调 #1 → #2。人为跳过一次 ACK Payload、重复一帧、制造缺号并跨越 65535 回绕。比较无线记录与 USB 报告，检查每个有效变化恰好提交一次、顺序正确、累计确认不倒退。
- 让 USB 暂停接收，分别观察窗口填满、队列溢出及停滞恢复。断开一端并重新上电，检查旧会话的输入和灯应答被丢弃。恢复只允许先清零再同步当前状态。
- 用能按协议驱动 SPI 的测试主机检查 #1 的 SCK、CS、READY 和 MISO；逻辑分析仪核对准备、撤销、24 字节事务和下一次确认。记录有积压时的拉取间隔、空闲心跳以及灯状态的 ACK Payload。真实 Pico 与原板的灯效留到下一阶段。

### 3 · Pico 与真实 Hitbox

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -Icommon -Ipico-host/src -Iusb-rx/src -Iradio-tx/src \
  tests/full_test.c pico-host/src/source.c usb-rx/src/receiver.c radio-tx/src/transmitter.c -o /tmp/test-full
/tmp/test-full
gh workflow run build.yml --ref main -f stage=full
```

该测试将三个固件的状态机串接，模拟上一事务预装的 MISO、下一次无线事务返回的 ACK 及 Windows 每毫秒完成一份 IN。用 120 次连续 1 kHz 输入变化叠加一次无线丢包和一次 ACK 丢失，比较最终 USB 报告。另测窗口满、USB 停滞、32 条缓冲溢出、单板重启、LED 版本回绕及拔插。这里的毫秒值来自模拟时钟，实板延迟应单独测量。

Pico 的系统时钟设为 120 MHz。原生 USB 端口 0 提供 CDC，PIO-USB 端口 1 在 GP0/GP1 上运行。两套 TinyUSB 任务在同一主循环非阻塞调度。固定版本的 `tusb_xinput` 在构建目录应用 [适配补丁](../pico-host/tusb_xinput.patch)，对齐 SDK 的类驱动 ABI，给有线 XInput 中断 IN 设置 1 ms 轮询间隔。设备可以 NAK，只有完整的 20 字节报告才生成输入快照。读取原始按键字、扳机和四轴，每个不同状态保留一次。

`source.c` 只在收到第一份有效原板报告后发 HELLO。SPI 会话切换时冻结 SYNC 快照，后续变化入 32 条缓冲。飞行窗口最多 8 帧，按 `usb_done` 释放。#1 的 `full` 构建只接受 SPI 输入，#2 共用阶段 2 的 ESB 和 USB 交付路径。实板测量需计入主机轮询、射频重传和等待 SPI READY 的时间。

原生 Micro-USB 打开 CDC 后每秒输出一行。用其连接电脑供电时，按玩家指南隔离外部 5 V。Pico 的 CDC 使用样机测试编号 `1209:0002`。

| 日志字段 | 核对内容 |
|---|---|
| `pad / active / reports / changes` | 上游 USB 地址、同步是否完成、收到的有效报告数、实际变化数 |
| `seq / tx / rx / done` | 当前序号和三跳累计进度，稳定长按时四个值应追平 |
| `resets / reason / spi_bad / poll_late` | 会话恢复次数、最近的复位原因、SPI 校验失败数、等待确认时超过 2 ms 的调度次数 |
| `led_out / mode / gen / applied / applied_gen` | 原板 OUT 成功次数、最近主机灯命令与版本、本会话是否有成功回执及最后成功的版本 |

USBPcap 可记录 PC 一侧的 IN 和 `01 03 mode` OUT。要验证 Pico 实际的 1 ms IN 轮询、SPI 硬件 READY 和端到端延迟，用 USB 分析仪或逻辑分析仪在同一时钟基准采集相应的信号。长按期间减少重复 USB 报告是正常现象。

先把同一块 Hitbox 直连同一台 Windows PC 记录基线，再接入全链路。将 Pico 读到的 XInput 报告与 Windows 接收报告逐字段、逐事件比较。

| 场景 | 通过条件 |
|---|---|
| 极速点按与同帧双键 | 正常会话内按下、松开的数量和顺序一致。原板同一报告中的双键到电脑仍在同一份报告 |
| 长按蓄力 | 连续按住 60 秒期间无额外松键。拔掉 Hitbox、给 Pico 或 #1 断电时，检查复位通知或超时清零 |
| 原板快捷键切模式 | 原板切换后的方向位和四个轴按原报告同步，旧方向正常释放 |
| 灯与恢复 | 抓到 Pico 发往 Hitbox 的灯 OUT 成功完成；支持灯效的板再核对外观。逐块重启、USB 拔插，验证恢复后没有延迟补招 |
| 附加延迟 | 对直连与无线使用相同触发点和主机观测点，报告样本数、中位数、P95、P99、最坏值及丢包/漏键数。2–3 ms 是待测目标 |

通过编译、Windows 自动绑定和全链路实测是三种独立结果，发布记录应分别注明。
