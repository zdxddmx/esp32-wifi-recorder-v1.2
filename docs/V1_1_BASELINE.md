# V1.1 基线记录

## 1. 工程身份

| 项目 | 记录 |
| --- | --- |
| 原始只读工程 | `D:\zhuomian\weite\ESP32\esp_recorder_v1.1_full_features` |
| V1.2 开发工程 | `D:\zhuomian\weite\ESP32\esp_recorder_v1.2_dev` |
| 建立日期 | 2026-08-08 |
| 原始工程 Git | 不是 Git 仓库；`git status`、`git branch --show-current`、`git log -10 --oneline` 均返回 `fatal: not a git repository` |
| 开发工程 Git | 新建独立 Git 仓库 |
| 基线分支 | `main` |
| 开发分支 | `feature/v1.2-product-completion` |
| 基线提交 | `9cc42fd5cebbfeeae7d18335d558fffc5faf29a0` |
| 基线标签 | `handover-v1.1-baseline` |

## 2. 复制结果

- 使用 `robocopy /E /XD build` 从原始工程复制到开发工程。
- 共复制 1956 个文件、约 13.07 MB，失败数为 0。
- 复制后 `robocopy /L` 差异检查退出码为 0。
- 原工程和开发工程同时存在；开发工程未复制旧 `build` 目录。
- 原始工程业务源码没有修改。

## 3. 构建环境

| 项目 | 结果 |
| --- | --- |
| ESP-IDF | `ESP-IDF v5.4` |
| IDF 路径 | `D:\23178\esp-idf` |
| Python | `C:\Users\23178\.espressif\python_env\idf5.4_py3.8_env\Scripts\python.exe` |
| CMake | 3.30.2 |
| Ninja | 1.12.1 |
| 编译器 | `xtensa-esp-elf-gcc 14.2.0` |
| Target | `esp32s3` |

非交互 PowerShell 缺少 `PROCESSOR_ARCHITECTURE`，官方 `export.ps1` 无法识别 Shell。构建时通过官方 `idf_tools.py export --format key-value` 装载同一套工具链环境，没有改变工程配置。

## 4. sdkconfig 状态

- `CONFIG_IDF_TARGET="esp32s3"`。
- 构建前后 SHA-256 均为 `0574A8B632B81579876AE30332406BDB105C88F6994288FE9DF0C629BA05AB12`。
- 未运行 `idf.py set-target`，未重新生成或人工修改 `sdkconfig`。

## 5. 路径修复

复制后的 `dependencies.lock` 仍引用原工程中的 TinyUSB 绝对路径。开发分支仅将该路径改为：

`D:\zhuomian\weite\ESP32\esp_recorder_v1.2_dev\components\espressif__tinyusb`

这是复制后的依赖路径修复，不涉及业务逻辑。修复后执行 `idf.py fullclean` 和全量重编，CMake 输出确认两个 TinyUSB 组件均来自开发工程。

## 6. 基线构建结果

| 项目 | 结果 |
| --- | --- |
| 命令 | `idf.py build` |
| 最终退出码 | 0 |
| 结果 | `Project build complete` |
| 应用固件 | `build/esp_recorder_v1.0.bin` |
| 应用大小 | `0xfc520` bytes |
| 最小应用分区 | `0x400000` bytes |
| 剩余空间 | `0x303ae0` bytes，75% |
| 烧录 | 未执行 |
| 硬件运行验证 | 本阶段未执行 |

编译成功只证明当前配置能够配置、编译和链接，不代表 WiFi、UART、SD、USB 或时序行为已经在 V1.2 固件上完成硬件验证。

## 7. 变更边界

- 原工程：未修改。
- 开发工程业务代码：未修改。
- 开发工程配置修复：仅 `dependencies.lock` 的本地组件绝对路径。
- 新增文档：`docs/V1_1_BASELINE.md`、`docs/V1_2_DEVELOPMENT_PLAN.md`。
- `build` 为重新生成的忽略目录，不纳入 Git。
