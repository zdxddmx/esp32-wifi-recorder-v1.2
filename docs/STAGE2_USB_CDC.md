# V1.2 第二阶段：USB CDC 通信基础层

## 1. 原有 CDC 状态

- `main/usb_cdc.c` 已调用 ESP-IDF 5.4 的 `tinyusb_driver_install()` 和 `tusb_cdc_acm_init()`。
- CDC RX 事件由 `usb_cdc_rx_callback()` 读取，但上层 `app_main.c::usb_cdc_rx_handler()` 仅丢弃 `data` 和 `len`，没有可用的数据通道。
- `usb_cdc_write()` 可直接写 TinyUSB TX buffer，但没有连接检查、统一并发入口、队列或字节统计。
- `usb_cdc_read()` 存在，但没有任务消费路径。
- CDC 与 MSC 已使用同一个自定义复合描述符和同一次 TinyUSB 初始化。

## 2. 修改和新增文件

修改：

- `main/app_main.c`：删除空 CDC 回调，改为初始化 CDC bridge。
- `main/usb_cdc.c/.h`：保留唯一 TinyUSB 初始化，增加 CDC 连接状态回调和连接查询。
- `main/CMakeLists.txt`：注册新 bridge 源文件。

新增：

- `main/usb_cdc_bridge.c`
- `main/usb_cdc_bridge.h`
- `docs/STAGE2_USB_CDC.md`

UART、数据路由、Web、SD 和 MSC 业务文件没有修改。

## 3. CDC 架构

```text
PC USB OUT
  -> TinyUSB task
  -> usb_cdc_rx_callback()
  -> bridge_rx_callback()       非阻塞复制到 RX Queue
  -> cdc_rx_task()
  -> future command parser      本阶段为可关闭的 raw echo
  -> usb_cdc_bridge_send()
  -> TX Queue
  -> cdc_tx_task()
  -> usb_cdc_write()
  -> TinyUSB CDC IN
  -> PC
```

TinyUSB 主任务使用 `CONFIG_TINYUSB_TASK_PRIORITY=5`、4096-byte stack，并固定到 CPU1。Bridge RX/TX 任务各使用优先级 4、3072-byte stack，USB 回调不执行协议解析、NVS、WiFi、SD 或阻塞等待。

## 4. RX 数据流

1. ESP-IDF `tud_cdc_rx_cb()` 在 TinyUSB 任务上下文触发已注册的 `usb_cdc_rx_callback()`。
2. 底层使用 `tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, ...)` 读取最多 512 字节。
3. `bridge_rx_callback()` 只检查参数、复制数据并用等待时间 0 写入 RX Queue。
4. `cdc_rx_task()` 阻塞等待队列并在任务上下文处理；`ESP_LOGD` 记录接收长度。
5. 正式 command parser 后续应接在 `cdc_rx_task()` 中标注的入口，替换调试 echo，不进入 TinyUSB 回调。

RX Queue 满时不阻塞 TinyUSB，当前剩余块被丢弃并累加 `rx_dropped_bytes`。

## 5. TX 数据流

- `usb_cdc_bridge_send(data, len)` 是二进制安全的统一发送入口，使用调用者提供的显式长度，支持 `0x00`、`0x0D`、`0x0A` 和任意字节。
- `usb_cdc_bridge_send_string(text)` 是字符串测试入口，仅该函数使用 `strlen()`。
- 所有调用者只向 TX Queue 非阻塞入队；只有 `cdc_tx_task()` 调用底层 `usb_cdc_write()`，避免多任务数据交叉。
- 底层 TinyUSB TX busy 时，TX task 最多重试 200 ms；超时或断连后丢弃未发送部分并计入 `tx_dropped_bytes`。
- `tx_bytes` 表示已进入 TinyUSB TX buffer 的字节数，不代表主机应用已经消费。

## 6. CDC + MSC 共存方式

复合描述符保持原样：

| 功能 | Interface | Endpoint |
| --- | --- | --- |
| CDC control | 0 | notification IN `0x81` |
| CDC data | 1 | OUT `0x02`、IN `0x82` |
| MSC | 2 | OUT `0x03`、IN `0x83` |

- Interface 总数为 3，没有冲突。
- CDC 和 MSC endpoint 编号没有冲突。
- `usb_cdc.c` 仍是唯一调用 `tinyusb_driver_install()` 的应用模块。
- `usb_msc_init()` 仍只确认静态 MSC callbacks 已链接，没有重复初始化 TinyUSB。
- `tud_mount_cb()` 和 `tud_umount_cb()` 的停止记录、FATFS 卸载、SD 重挂载及自动恢复逻辑未修改。
- 是否在真实 Windows 主机上同时枚举 CDC 和 MSC，仍需烧录后实测。

## 7. 队列大小

| 队列 | 深度 | 单帧最大值 | 有效载荷容量 |
| --- | ---: | ---: | ---: |
| RX Queue | 8 | 512 bytes | 4096 bytes |
| TX Queue | 8 | 512 bytes | 4096 bytes |

单次 `usb_cdc_bridge_send()` 最大 512 字节。超过限制返回 `ESP_ERR_INVALID_SIZE`，不进行部分入队。后续若正式协议需要更大帧，应在协议层分片，不能扩大后假定无流控。

## 8. 错误处理和统计

| 情况 | 处理 |
| --- | --- |
| NULL 数据 | `ESP_ERR_INVALID_ARG` |
| 长度为 0 或超过 512 | `ESP_ERR_INVALID_SIZE` |
| Bridge 未初始化/CDC 未连接 | `ESP_ERR_INVALID_STATE` |
| TX Queue 满 | `ESP_ERR_NO_MEM`，累加 TX drop |
| RX Queue 满 | 回调立即返回，累加 RX drop |
| TinyUSB TX busy | TX task 有界重试 200 ms |
| USB 中途拔出 | TX task停止发送并统计未发送字节，不访问已失效主机状态 |

提供 `usb_cdc_bridge_get_rx_bytes()`、`usb_cdc_bridge_get_tx_bytes()` 和 `usb_cdc_bridge_get_stats()`。成功初始化后队列和任务常驻，未增加循环分配；初始化中途失败会删除已创建的任务和队列。

## 9. 调试 Echo

`usb_cdc_bridge.h` 中 `USB_CDC_DEBUG_ECHO` 默认值为 1：PC 发送的原始字节由 RX task 放入 TX Queue 并原样返回。它只是阶段自检，不包含命令、帧头、校验或产品协议。接入正式 parser 时将宏设为 0。

## 10. 编译结果

| 项目 | 结果 |
| --- | --- |
| ESP-IDF | v5.4 |
| Target | esp32s3 |
| `idf.py build` | 成功，退出码 0 |
| 输出固件 | `build/esp_recorder_v1.0.bin` |
| 固件大小 | `0xfd1d0` bytes |
| 分区剩余 | `0x302e30` bytes，75% |

编译成功证明新模块已配置、编译和链接，不代表 CDC echo 或 CDC+MSC 已通过开发板验证。

## 11. 硬件测试准备情况

2026-08-08 的 Windows 只读枚举结果：

- PnP 检测到 `USB-SERIAL CH340 (COM5)`，Instance ID 为 `USB\VID_1A86&PID_7523\6&19A9457F&0&1`。这是 USB-TTL 适配器，不能证明本固件的原生 USB CDC 已枚举。
- 未检测到项目描述符对应的 `VID_303A&PID_4002`、`ESP Recorder CDC` 或 `ESP Recorder MSC`。
- `Win32_SerialPort` 仅列出蓝牙 COM4、COM6、COM7、COM90，没有列出可确认的 ESP32-S3 CDC COM。
- 无法唯一识别目标 ESP32-S3 原生 USB 设备，因此本阶段未烧录，也没有猜测 COM 口。

烧录后硬件验收应依次检查：复合设备枚举、MSC 盘符、CDC COM、打开串口后的 DTR 连接状态、文本 `hello` 原样回显、包含 `00 0D 0A` 的二进制回显、队列压力和拔插恢复。

## 12. 后续正式协议接入位置

正式协议只应接入 `main/usb_cdc_bridge.c::cdc_rx_task()` 中的标记位置，并复用 `usb_cdc_bridge_send()` 返回响应。不得在 `usb_cdc_rx_callback()` 或 `bridge_rx_callback()` 中执行解析、配置保存、WiFi 操作或文件操作。
