# camera-rc · X-E5 圆屏遥控器

面向 **Waveshare ESP32-S3-Touch-AMOLED-1.43C** 的 ESP-IDF 工程。

**0.2.0 已实现 furble 富士 Basic／Secure 连接与拍照协议，尚待本项目 X-E5 实机验收。** 遥控器主动扫描并连接相机，已移除原先受阻塞的手柄方案和录像按钮。请使用 1.43C 板卡；不要套用同名非 C 版配置。

包含单相机保存、开机自动连接、有限重连、异步拍摄状态机、BOOT／触摸仲裁、极简圆屏 UI、中文字体与本地快门音效。协议依据为固定上游源码，缺失服务、认证失败、必要订阅失败或中性释放失败时不会启用拍摄。

新版设计使用内置 imagegen 生成六张独立参考图，实际界面使用原生 LVGL 控件。查看[设计与提示词](docs/design/README.md)、[原生主页预览](docs/design/rendered/ready.png)及[验证记录](docs/verification.md)。预览中的连接和电量为模拟值。

## 快速开始

安装 Git、Python **3.10–3.13**（建议 3.13）和 CMake。Windows 安装 Git for Windows 与 CMake，并勾选加入 PATH；Python 可通过 `py -3.13` 调用。SDK 与工具默认安装在工程 `.tools/` 内，不修改系统 SDK。

```sh
# macOS / Linux；首次准备环境需要网络和约数 GB 空间
python3.13 tools/project.py setup
python3.13 tools/project.py doctor
python3.13 tools/project.py test
python3.13 tools/project.py build
python3.13 tools/project.py flash --port /dev/cu.usbmodemXXXX
python3.13 tools/project.py monitor --port /dev/cu.usbmodemXXXX
```

```powershell
# Windows 11
py -3.13 tools/project.py setup
py -3.13 tools/project.py test
py -3.13 tools/project.py build
py -3.13 tools/project.py flash --port COM5
py -3.13 tools/project.py monitor --port COM5
```

也可使用 `./project.sh`、`./project.ps1`、`project.cmd`；设置 `RC_PYTHON` 可指定启动器使用的 Python。已安装 SDK 时，`IDF_PATH` 必须指向精确的 ESP-IDF **v5.5.3**；`IDF_TOOLS_PATH` 可复用对应工具目录。构建工具拒绝误用其他 SDK 版本。

`test` 无需 SDK 或开发板，只需 Python、CMake 和 C++17 编译器；Windows 可使用 Visual Studio Build Tools 的“使用 C++ 的桌面开发”。串口存在多个 USB 设备时，工具不会猜测刷写目标，必须传 `--port`。没有 USB 数据串口时，请先检查数据线及下载模式。

常用命令：

| 命令 | 作用 |
| --- | --- |
| `doctor` | 检查 SDK、工具链与串口 |
| `setup` | 安装固定 SDK、工具链及 Python 环境 |
| `build` | 构建 ESP32-S3 固件 |
| `flash --port …` | 编译并刷写，保留 NVS |
| `monitor --port …` | 查看串口日志；Ctrl+] 退出 |
| `test` | 运行核心 C++ 与 Python 工具测试 |
| `preview` | 用 LVGL 9.4.0 源码渲染原生界面并测试触摸交互 |
| `format-check` | 检查 C++ 格式，需要 clang-format |
| `package` | 输出带偏移、校验和和独立刷写器的 ZIP |
| `erase --port … --confirm-erase` | 全片擦除，会删除所有配对记录 |

## 使用

首次使用在相机开启蓝牙及配对，点击首屏状态或设备页“扫描相机”，从列表选择目标并在两分钟内完成。Secure 在遥控器显示六位配对码，须与相机核对并人工确认；重新配对同样如此。列表最多 16 台，名称相同可比较身份后缀和信号。连接完成包括加密、绑定、必要握手、中性释放及保存，不等同于只有 BLE 链路。

只保存一台相机；有记录时开机自动连接。意外断线最多恢复两轮，失败后可手动重试；主动断开后停止自动连接，点击连接或重启后恢复。更换相机须先在设备页确认忘记。普通刷写保留配对，不自动清空 NVS；相机端可能也需要人工删除旧记录。

构建时将 furble 的 NimBLE 地址类型兼容补丁应用到 `build/furble_compat/ble_gap.c`：密钥查询失败时交换 PUBLIC／RANDOM 类型再查，优先复用已有 LTK。已保存相机只恢复旧绑定，缺少 LTK 或认证失败时停止，不自动转入新配对或删除绑定；须显式忘记后重新配对。共享 SDK 保持原样；补丁位置不匹配时构建报错。

本机 IRK 单独保存在 `camera_rc/local_irk_v1`；首次升级可从已有绑定中恢复此前实际分发的身份，再恢复活动 IRK 与控制器解析列表。该记录不会随“忘记相机”删除。已在用户开发板上验证一次关机开机后保留原绑定自动加密并进入 Ready，详见[重连调查](docs/reconnect-investigation.md)。

主页仅保留大快门，短按先请求对焦再拍照，长按保持快门、松手停止；连拍档位和速度由相机设置。BOOT 始终作为拍照键，滑动取消和双输入会释放。相机没有曝光或合焦反馈时只显示“指令已发送”。富士手机遥控协议没有已验证的独立录像命令，因此不提供录像入口。

点击“设备状态”或右滑进入信息页，左滑返回。页面可上下滚动，显示本机电量估算、电压、充电、真实连接和信号；不展示未提供的相机电量、固件或录像状态。闲置 30 秒调暗、60 秒熄屏；首次触摸仅唤醒，BOOT 直接响应拍照。不会自动深度睡眠。

触摸快门或按下去抖后的 BOOT 播放约 100ms 本地快门音效；长按一次，同时输入不积压。未连接时仍提供本地反馈，不发送相机命令；声音不表示相机已经拍摄。音频故障不阻止 UI 启动。

协议依据、时限与验证边界详见[协议说明](docs/protocol.md)。实机兼容结果必须记录实际相机固件、镜头和设置，不能由自动化测试推断。

## 工程导览

```text
main/                     依赖组装与启动
components/remote_core/   无 ESP-IDF 依赖的模型、状态机和电量曲线
components/camera_ble/    富士协议会话、异步 NimBLE 传输与 NVS 配对身份
components/board/         1.43C 显示、CST820、ADC、充电状态、BOOT
components/app_runtime/   唯一相机任务、队列、取消代次和视图快照
components/ui/            LVGL 圆屏 UI、中文文案及生成字库
tests/                    模拟相机与自动化测试
tools/                    跨平台构建、刷机、字体、打包工具
docs/                     协议、架构、硬件和验收记录
docs/design/              imagegen 图稿、原始提示词和设计说明
```

进一步阅读：[架构与扩展](docs/architecture.md)、[硬件与故障排查](docs/hardware.md)、[实机验收](docs/acceptance.md)、[验证记录](docs/verification.md)。本项目不包含图传、GPS、OTA、照片传输或多相机同时控制。

