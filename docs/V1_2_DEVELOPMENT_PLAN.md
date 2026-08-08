# ESP32-S3 WiFi 数据记录仪 V1.2 增量开发计划

## 1. V1.1 当前架构

应用组件由 `main/CMakeLists.txt` 注册，入口为 `app_main()`。现有模块和真实入口如下。

| 模块 | 文件 | 主要入口/函数 | 当前职责 |
| --- | --- | --- | --- |
| 系统入口 | `main/app_main.c` | `app_main()`、`uart_rx_task()`、`tud_mount_cb()`、`tud_umount_cb()` | 初始化 NVS、SD、配置、记录器、双 UART、路由、WiFi、Web、控制台、CDC、MSC |
| WiFi AP+STA | `main/wifi_config.c` | `wifi_config_init()`、`wifi_apply_ap()`、`wifi_connect_sta()` | `WIFI_MODE_APSTA`、AP 参数应用、STA 连接/重连和 IP 状态 |
| HTTP Server | `main/web_server.c` | `web_server_start()` 及各 URI handler | 状态、WiFi/AP、文件、记录、实时流、发送、重启和配置接口 |
| 网页 | `main/index.html` | `refreshStatus()`、`pollStream()`、文件/记录/配置函数 | 手机浏览器本地操作界面，编译时嵌入固件 |
| UART CH1/CH2 | `main/uart_driver.c/.h` | `user_uart_init_channel()`、`user_uart_read_channel()`、`user_uart_write_channel()` | CH1=UART0 TX43/RX44；CH2=UART2 TX17/RX18；收发计数和 TX 回调 |
| 数据路由 | `main/data_router.c/.h` | `data_router_forward()` | CH1 RX 到 CH2 TX、CH2 RX 到 CH1 TX，统计发送和丢失字节 |
| SD 卡 | `main/sd_card.c/.h` | `sd_card_init()`、`sd_card_unmount_fs()`、`sd_card_remount_fs()` | SDMMC 挂载、卸载、重新挂载和状态管理 |
| 数据记录 | `main/data_recorder.c/.h` | `recorder_start()`、`recorder_stop()`、`recorder_write_*()` | 双通道环形缓冲、文件写入、flush/fsync、实时流 |
| 文件管理 | `main/web_server.c` | `files_list_safe_handler()`、`file_download_safe_handler()`、`file_delete_safe_handler()` | `/sdcard` 记录文件列表、下载和删除 |
| USB CDC | `main/usb_cdc.c/.h`、`main/app_main.c` | `usb_cdc_init()`、`usb_cdc_write()`、`usb_cdc_rx_handler()` | TinyUSB CDC 底层可收发；应用层 RX handler 当前为空 |
| USB MSC | `main/usb_msc.c/.h` | `usb_msc_init()`、TinyUSB MSC 回调 | 将 SD 块设备提供给 USB 主机；挂载时让出 FATFS |
| NVS/参数 | `main/config_manager.c/.h` | `config_manager_load()`、`config_manager_save()`、各 setter | NVS 为主存储，SD `config.json` 兼容/镜像，失败回滚 |

## 2. 已完成功能（静态代码和编译确认）

- AP+STA 混合模式，以及 STA 断线重连状态处理。
- 手机通过 AP 网页访问和配置 AP/STA。
- 两路 UART 初始化、读取、发送和字节统计。
- CH1 RX 到 CH2 TX、CH2 RX 到 CH1 TX 双向路由。
- 网页实时显示 RX/TX，网页向指定通道发送十六进制数据。
- 双通道 RX 独立 SD 文件记录、缓存、flush/fsync 和丢失计数。
- 局域网页开始/停止记录。
- 记录文件列表、下载和删除。
- Type-C USB MSC 块读写，以及 USB 连接时停止记录/卸载 FATFS、断开后重新挂载并按配置恢复记录。
- UART 波特率、WiFi、路由、自动记录、实时流间隔写入 NVS。

以上结论表示调用链存在且 V1.2 基线编译通过；本阶段未烧录，不能替代当前开发板实测结论。

## 3. 部分完成功能

| 功能 | 当前状态 | 缺口 |
| --- | --- | --- |
| USB CDC | 底层 TinyUSB CDC 已初始化，具备读写函数 | `usb_cdc_rx_handler()` 丢弃全部输入；没有命令协议、参数配置和实时数据订阅 |
| SD 记录 | 两路 RX 写入独立文件 | TX 只进入实时流，没有写入 SD；需先明确文件格式和是否记录路由复制数据 |
| UART 配置 | 两路 UART 可设置波特率 | 配置结构只有一个 `baudrate`，Web handler 同时设置两路 |
| 网页实时流 | `/api/stream` 和 200 ms 轮询存在 | `setInterval(pollStream, 200)` 无请求在途保护，慢网络可能重叠请求/重复显示 |
| WiFi STA | 能连接外部 WiFi | 得到 IP 后没有后台服务器客户端、校时或远程业务 |
| 文件功能 | 网页可列表、下载、删除 | 没有选择文件上传后台服务器 |

## 4. 未完成功能

- Type-C CDC 参数配置、实时数据显示和 USB 配网。
- PC 上位机程序及最终通信协议。
- 后台服务器连接、设备认证、心跳和重连策略。
- 远程 TX/RX、远程控制和远程文件传输。
- ESP32 OTA、固件签名/校验、回滚策略。
- IMU 固件升级协议和状态机。
- SNTP/RTC 自动校时。
- RS485 DE/RE 控制和接口驱动。
- CAN/TWAI 驱动和业务协议。
- 网页身份认证、会话和权限控制。
- APP；没有明确需求，本计划不安排提前开发。

## 5. 技术债

1. `dependencies.lock` 原有绝对路径绑定旧工程；已在开发分支修正并复编。
2. UART 两路配置耦合，共用一个波特率字段。
3. 记录器 RX 和 TX 的持久化行为不对称。
4. 网页实时流采用短轮询且没有并发请求保护。
5. 未校时导致文件名和实时记录可能显示 1970 年时间。
6. HTTP 管理接口没有认证；不能直接暴露到不可信网络。
7. CDC 应用入口为空，后续必须把传输、协议解析和业务命令分层，避免逻辑堆入 `app_main.c`。
8. SD/FATFS 与 USB MSC 是共享资源，高负载切换、拔出恢复和掉电一致性仍需硬件压力测试。

## 6. 当前真实数据流

```text
CH1 RX (UART0 RX44)
  -> uart_rx_task(CH1)
  -> recorder_write_rx_channel(CH1)
       -> recorder_live_push(CH1, RX) -> /api/stream -> Web
       -> recorder_write_raw(CH1) -> REC_CH1_*.bin
  -> data_router_forward(CH1)
       -> user_uart_write_channel(CH2) -> UART2 TX17
       -> TX callback -> recorder_write_tx_channel(CH2)
            -> recorder_live_push(CH2, TX) -> /api/stream -> Web
            -> 不写 SD

CH2 RX (UART2 RX18)
  -> uart_rx_task(CH2)
  -> recorder_write_rx_channel(CH2)
       -> recorder_live_push(CH2, RX) -> /api/stream -> Web
       -> recorder_write_raw(CH2) -> REC_CH2_*.bin
  -> data_router_forward(CH2)
       -> user_uart_write_channel(CH1) -> UART0 TX43
       -> TX callback -> recorder_write_tx_channel(CH1)
            -> recorder_live_push(CH1, TX) -> /api/stream -> Web
            -> 不写 SD

Web /api/send(channel, hex)
  -> user_uart_write_channel(channel)
  -> TX callback -> Web 实时流
  -> 不写 SD

USB 主机挂载
  -> tud_mount_cb()
  -> recorder_stop()
  -> sd_card_unmount_fs()
  -> USB MSC 直接读写 SD 扇区

USB 主机断开
  -> tud_umount_cb()
  -> sd_card_remount_fs()
  -> auto_start=true 时 recorder_start()
```

## 7. 推荐开发阶段与依赖顺序

建议保留原顺序主体，但增加协议/硬件资料门禁，并把低风险网页修复提前到独立阶段，避免它与通信协议开发混在一起。

| 阶段 | 目标 | 主要修改模块 | 验收方法 | 硬件实测 |
| --- | --- | --- | --- | --- |
| 2 | 定义并实现 CDC 通信基础 | 新增 `cdc_transport.*`，小范围扩展 `usb_cdc.*`、`app_main.c` | PC 枚举、分包/粘包/错误帧测试、编译 | 必须 |
| 3 | CDC 参数配置和实时数据 | 新增 `device_protocol.*`，复用 `config_manager`、`data_recorder` | CDC 设置/读取参数，双路 RX/TX 实时订阅 | 必须 |
| 4 | USB 配网 | 协议命令层、`wifi_config` 适配 | CDC 写入 AP/STA 参数，重启后保持并真实联网 | 必须 |
| 5 | 自动校时 | 新增 `time_sync.*`，接入 WiFi GOT_IP | SNTP 成功/失败/重连，文件时间正确 | 必须联网 |
| 6 | 完善 SD RX/TX 记录 | 扩展 `data_recorder.*`，必要时版本化文件格式 | 同时产生可核对的 RX/TX 数据，停止时无尾部丢失 | 必须 |
| 7 | CH1/CH2 独立串口参数 | 扩展配置结构、迁移旧 NVS、UART/Web/CDC 适配 | 两路不同波特率重启保持，旧配置可迁移 | 必须 |
| 8 | 修复网页实时流并发 | `index.html`，必要时扩展 stream API | 慢网络下无重叠请求、重复或顺序倒退 | 建议 |
| 9 | 网页认证 | 新增认证模块，保护 HTTP API | 未登录拒绝、会话过期、修改密码和恢复流程 | 建议 |
| 10 | PC 上位机 | 独立 PC 工程，使用阶段 2-4 稳定协议 | 配置、实时流、记录和文件操作端到端 | 必须 |
| 11 | 后台服务器客户端 | 新增 `backend_client.*`、设备状态机 | TLS/认证、心跳、断线重连和限流 | 必须联网/服务器 |
| 12 | 远程实时数据和控制 | 后台客户端、路由/记录接口适配 | 双向实时流、控制权限、拥塞和断线测试 | 必须 |
| 13 | 远程文件传输 | 新增 `file_transfer.*` | 分片、校验、续传、空间不足和并发记录测试 | 必须 |
| 14 | ESP32 OTA | 新增 `ota_manager.*`、分区/安全策略 | 签名校验、断电恢复、失败回滚、版本防降级 | 必须 |
| 15 | IMU 固件升级 | 协议明确后新增 `imu_upgrade.*` | 实机升级、校验、失败恢复，不影响 ESP32 OTA | 必须有 IMU |
| 16 | RS485 | 硬件确认后扩展串口物理层适配 | DE/RE 时序、收发切换、总线冲突和长线测试 | 必须有硬件 |
| 17 | CAN/TWAI | 硬件确认后新增 `can_transport.*` | 波特率、滤波、错误恢复、总线负载测试 | 必须有硬件 |

每个阶段采用同一停止门：先保存 Git 状态和需求输入，小范围实现，执行 `idf.py build`；涉及硬件的阶段必须烧录到确认的板卡并保留真实日志/文件/网页证据，之后才可标记完成。未经用户确认不自动进入下一阶段。

## 8. 各阶段接口边界

- CDC、后台和 CAN/RS485 都应作为传输适配层，不直接操作网页 DOM 或拼接 SD 文件路径。
- 参数修改统一通过 `config_manager`，新增字段必须带旧版本迁移和默认值策略。
- 实时数据源统一复用记录器的 live event 或新增发布层，避免 Web、CDC、后台各自复制 UART 读取任务。
- 文件操作统一经过受控文件服务层，保持文件名检查、记录中占用检查和 SD/MSC 所有权规则。
- OTA 与 IMU 升级必须分离：ESP32 OTA 使用 ESP-IDF OTA 分区；IMU 升级使用已确认的外设协议。

## 9. 分阶段验收规则

1. 静态检查只能标记“代码存在”。
2. `idf.py build` 成功只能标记“编译通过”。
3. USB、UART、SD、WiFi、服务器、OTA、RS485、CAN 必须在真实硬件上验证后才能标记“完成”。
4. 不使用模拟数据替代开发板结果。
5. 每阶段至少保存：分支/commit、完整构建日志、测试步骤、硬件标识、真实输出和失败项。
6. 回归项至少覆盖 AP 网页、双 UART 路由、SD 记录、文件管理、NVS 持久化和 MSC 切换。

## 10. 必须硬件实测的内容

- CDC 枚举、收发吞吐、拔插和 CDC+MSC 复合设备共存。
- 两路 UART 不同波特率、460800 持续传输和丢失计数。
- SD 长时间记录、停止尾部刷新、掉电、空间不足和 MSC 所有权切换。
- AP+STA 并行、断线重连、SNTP、后台和 OTA。
- IMU 实机升级、RS485 方向控制、CAN 总线错误恢复。
- PC 上位机与最终硬件的端到端交互。

## 11. 当前缺失的外部资料

| 功能 | 必需资料 |
| --- | --- |
| CDC/PC 上位机 | 命令 ID、帧格式、编码、长度、校验、响应码、超时、兼容策略、产品交互稿 |
| 后台服务器 | 协议、地址、端口、TLS/证书、认证、设备 ID、心跳、消息结构、重连和限流 |
| 远程文件 | 上传 API、分片大小、校验、断点续传、并发策略、保留/删除规则 |
| ESP32 OTA | 固件发布地址、签名证书、版本策略、回滚与防降级要求 |
| IMU 升级 | IMU 型号、升级传输接口、Bootloader 协议、分包、校验、重试和恢复流程 |
| RS485 | 收发器型号、供电、ESP32 UART/GPIO、DE/RE 接法及有效电平、终端/偏置要求 |
| CAN | 收发器型号、TX/RX GPIO、晶振/时钟、波特率、采样点、终端电阻、报文定义 |
| 网页认证 | 用户模型、首次密码、恢复方式、会话时长、局域网/外网安全边界 |
| SD RX/TX 格式 | 是否记录路由复制 TX、时间戳精度、通道/方向标记、文件兼容要求 |

## 12. 高风险功能

- USB CDC 与 MSC 复合设备共享 TinyUSB 生命周期，错误的重初始化会造成枚举失败或 SD 并发访问。
- SD 记录和 MSC 块访问若所有权切换不完整，可能造成 FAT 损坏或数据丢失。
- OTA、IMU 升级存在设备变砖风险，必须有校验、断电恢复和回滚设计。
- 后台远程控制和网页认证涉及凭据、TLS、权限与命令重放，不能在协议未定时先写假实现。
- RS485/CAN GPIO 和收发器参数错误可能造成总线冲突或硬件损伤，资料未确认前不编码。
- 独立 UART 配置涉及 NVS 数据结构迁移，必须保持旧设备配置可读取。
