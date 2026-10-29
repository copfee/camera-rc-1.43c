# 开发板重启后连接超时调查

当前结论：已在本轮实机确认本机 IRK 恢复失效；LTK 存在且双向一致。已从原绑定恢复并独立持久化本机身份、重建解析列表，用户关机开机后 3880 ms 恢复加密、12280 ms 到达 Ready，无新配对。首次／重新配对已恢复显示数字并人工确认。详见末尾“本机身份修复与实机复验”。

日期：2026-10-29（Asia/Shanghai）。以下保留早期只读调查与上一轮移植过程；本轮已修改固件、保留 NVS 刷写并完成真实关机开机复验。

阅读边界：下文“已确认的事实”至“自动化检查”为仓库早期调查记录；当时引用的旧原始日志未在 Windows 工作区中。后续实际读取与采集的本轮日志、修复及验证，见末尾“本机身份修复与实机复验”。

## 已确认的事实

- 用户观察：相机关机再开机可以自动连接，开发板关机再开机出现连接超时。异常时仅开发板出现数字比对，相机没有出现对应确认界面。
- `build/log/bond-restore-diagnostic.log` 与现有监视器记录 `build/log/idf_py_stdout_output_71241` 均显示：相机元数据有效，恢复一个 BLE bond；按相机身份查询成功，LTK 与对端 IRK 均存在。
- 多次尝试成功建立 BLE 链路，但安全认证阶段收到 `BLE_SM_IOACT_NUMCMP`（操作码 4），再次进入数字比对。因此不能把问题归为仅扫描失败或元数据未保存。
- 现有监视器记录的同一轮尝试：设备运行时间 74952 ms 进入确认；80080 ms 主控分发“确认验证码”，80081 ms 回到安全认证；其后直到日志中的下一次启动，没有记录认证完成。确认输入确实到达主控，不能据此归因为用户未确认。
- 本次采集的 `build/log/reconnect-confirm-inspection.log` 显示另一轮：294888 ms 发起安全认证，295738 ms 进入确认，324888 ms 本机发起断开并报告超时。该轮片段没有确认输入，不能与上一轮已确认的尝试混为一谈。
- 系统已有一个串口监视器；额外读取同一串口会造成采集片段缺字或缺行。后续优先读取原监视器输出，避免多个读取者竞争。

## 定位与边界

故障已定位到已有绑定的安全会话恢复／重新配对阶段；尚未到达相机 GATT 握手或快门操作。超时只是最终结果。

代码中，`Radio::secure()` 调用 `ble_gap_security_initiate()`。固定 IDF 5.5.3 的实现先查询相机身份对应的 LTK，存在时先尝试恢复加密。该 SDK 的 `BLE_RESTART_PAIR` 默认为 1：遇到 `BLE_ERR_PINKEY_MISSING` 对应的内部状态 518 时，会自动重新发起配对，并跳过该分支原本的加密完成事件通知。因此，“有 LTK，但随后出现数字比对”与旧密钥被远端拒绝后的自动重新配对相符；这仍是待验证的路径，现有日志没有直接记录该底层错误码。

相机仅重启时，开发板保留运行中的本机 BLE 身份与协议栈状态；开发板重启时，需要重新从 NVS 恢复本机身份、绑定及解析列表。该差异指向冷启动恢复问题，但不能仅凭这一点认定本机 IRK 改变、LTK 损坏或相机丢失记录。

当前绑定诊断只证明对端记录可读取，未验证活动本机 IRK 与持久化本机 IRK 是否一致，也未记录对端拒绝旧密钥的原因。`Radio::gap()` 还未单独处理／报告 `BLE_GAP_EVENT_REPEAT_PAIRING` 与 IDF 中拼写为 `BLE_GAP_EVENT_PARING_COMPLETE` 的事件；断开原因虽已入队，未输出到日志。

## 后续应验证的内容

1. 在 owner 任务记录断开原因、重复配对与配对完成状态；回调只投递事件。
2. 在安全 API 内比较活动本机 IRK 与持久化值，只输出是否一致、查询结果等布尔信息，不打印密钥、摘要或序列信息。
3. 如 SDK 自动重配对继续掩盖原始错误，用单独诊断构建显式暴露该错误，再决定修复；不将猜测当作已验证原因。
4. 成功修复必须实测开发板断电重启后直接恢复加密并进入 Ready；正常恢复不应要求再次比对数字。首次配对仍保留人工确认。

不能以自动确认数字、自动删除绑定、清空 NVS 或单纯延长超时作为“旧绑定已正确恢复”的证据。

## 自动化检查

`python3 tools/project.py test` 通过：3 项 CTest、7 项 Python 测试。现有主机测试使用模拟安全事件，不能验证 NimBLE NVS、真实 IRK 恢复或相机侧旧密钥接受行为；本次未改固件，无固件重新编译或实机修复结论。

## furble 对照复核

日期：2026-10-29（Asia/Shanghai）。本次只更新调查文档，未修改固件、SDK 或配对数据，未运行新的测试或固件构建。

### 上游如何保存和重连

逐项读取本项目原协议来源提交 `0cac22aacc3d9ef250be40849118af78a5f39f27` 的源码，并对照当前 master；本节涉及的 Device.cpp、Camera.cpp、CameraList.cpp、FujifilmSecure.cpp 和 ble_gap.patch 内容相同。

- [CameraList.cpp](https://github.com/gkoh/furble/blob/0cac22aacc3d9ef250be40849118af78a5f39f27/lib/furble/CameraList.cpp) 使用 Preferences 保存相机索引和序列化元数据。Secure 记录包含名称、地址、地址类型和五字节序列信息，不在该记录中自行保存 BLE LTK / IRK；绑定持久化由 NimBLE 负责。显式删除相机时才调用 `deleteBond()`。
- [Device.cpp](https://github.com/gkoh/furble/blob/0cac22aacc3d9ef250be40849118af78a5f39f27/lib/furble/Device.cpp) 从 eFuse MAC 生成稳定名称，初始化 NimBLE，双方均分发 ENC 和 ID 密钥，使用 `BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT`。[Camera.cpp](https://github.com/gkoh/furble/blob/0cac22aacc3d9ef250be40849118af78a5f39f27/lib/furble/Camera.cpp) 开启 bonding、MITM 和 Secure Connections。与本项目的核心安全配置一致。
- [FujifilmSecure.cpp](https://github.com/gkoh/furble/blob/0cac22aacc3d9ef250be40849118af78a5f39f27/lib/furble/FujifilmSecure.cpp) 对已保存相机先扫描配对服务广播、匹配序列信息，再连接并调用 `secureConnection()`；安全成功后才进行状态读取、确认、标识写入和订阅。本项目同样先安全认证再做 GATT 握手。一个实现差异是上游扫描回调仅报告找到目标，连接仍用保存的 `m_Address`；本项目使用匹配广播的当前地址。
- [上游 #208 的 X-E5 用户记录](https://github.com/gkoh/furble/issues/208) 中，Grumpytech4 于 2025-09-05 报告遥控器断电重启后可再次手动连接，相机不需再次输入。该记录证明上游曾实现此场景，不能代替本项目 ESP32-S3 / IDF 5.5.3 的实测。

### 此前遗漏：上游给 NimBLE 打了密钥查询补丁

[platformio.ini](https://github.com/gkoh/furble/blob/0cac22aacc3d9ef250be40849118af78a5f39f27/platformio.ini) 配置 `extra_scripts = pre:patches/apply.py`；[apply.py](https://github.com/gkoh/furble/blob/0cac22aacc3d9ef250be40849118af78a5f39f27/patches/apply.py) 修改 SDK 中的 `ble_gap.c`。不能仅对照相机协议文件而忽略此构建步骤。

[ble_gap.patch](https://github.com/gkoh/furble/blob/0cac22aacc3d9ef250be40849118af78a5f39f27/patches/ble_gap.patch) 修改 `ble_gap_security_initiate()`：按当前 peer 身份查询失败时，执行 `key_sec.peer_addr.type = !key_sec.peer_addr.type` 后再次查询。找到 LTK 则恢复加密，仍找不到才发起配对。它处理的是地址类型与存储索引不匹配，不是补写丢失的密钥。PUBLIC/RANDOM 的互换与 PUBLIC_ID/RANDOM_ID 转换不是同一件事。

本项目没有应用此补丁。核查本机 `C:/esp/v5.5.3/esp-idf`（`.git/HEAD` 为要求的 `2c211b236707889e8400c4dc5644dd5c4ee071e0`）所带 NimBLE：`ble_gap_security_initiate()` 只按实际 peer 身份查询一次；存储按地址值及类型精确匹配。因此，绑定存在但以另一类型存储时，确实可能直接进入新配对。

不过，不能直接认定缺补丁就是本次根因：

- 本项目 `log_bond_status()` 会执行 `type &= 1`，但这只将 2/3 归一到 0/1，不会互换 0/1；IDF 的 `ble_hs_conn_addrs()` 同样将身份类型 2/3 归一到 0/1。因此不能仅因诊断做过归一化，就断言诊断查询与 SDK 查询不同。
- 如果已有调查所述“认证前查询成功、LTK 存在”适用于同一失败会话，并且连接身份未在两次查询间变化，则补丁的备用查询分支不会起作用。此时应优先验证恢复加密是否被远端拒绝。

### 另一差异：上游默认确认数字，并会尝试删除失效绑定

上游固定依赖 esp-nimble-cpp 2.5.0，Camera 未重写 `onConfirmPasskey()`。[该版本 NimBLEClient.cpp](https://github.com/h2zero/esp-nimble-cpp/blob/2.5.0/src/NimBLEClient.cpp) 的默认回调直接执行 `injectConfirmPasskey(connInfo, true)`。因此“上游没有弹出验证码界面”不能证明它没有重新配对。本项目显示验证码来自真实 `BLE_SM_IOACT_NUMCMP` 事件，并非 UI 自行生成。

同一 NimBLEClient 实现收到 `BLE_ERR_PINKEY_MISSING` 对应的加密事件时会删除该 peer 的绑定，`secureConnection()` 可再试一次。本项目直接使用 host API，不经过这段 client 回调。IDF 5.5.3 的 `BLE_RESTART_PAIR` 默认为 1，`ble_sm_process_result()` 遇内部状态 518 可在发出原始加密完成事件前自动重配对；这也可能使应用只看到后续数字比对。

这些行为差异可以解释上游和本项目的交互不同，但不能据此要求本项目自动确认数字或删除绑定。相机没有提示的具体原因仍缺少远端或 HCI 证据。

### 对“密钥是否没有正确存储和加载”的判断

本项目 `sdkconfig.defaults` 已启用 `CONFIG_BT_NIMBLE_NVS_PERSIST=y`；`main.cpp` 先初始化 NVS；相机元数据写入后执行 `nvs_commit()`。[esp-nimble-cpp 2.5.0 初始化](https://github.com/h2zero/esp-nimble-cpp/blob/2.5.0/src/NimBLEDevice.cpp) 在启动 host 任务前调用 `ble_store_config_init()`。本机固定 SDK 的 NVS 存储包含 peer security 和独立的 `local_irk` 恢复，写入路径执行 `nvs_commit()`；启动路径先恢复/生成本机 IRK，再设置活动 IRK。静态代码没有发现明确关闭持久化或应用每次随机重建本机身份的逻辑。

因此目前有依据的结论是：已有绑定的恢复认证出现异常；尚不能证实密钥未落盘、加载错误或相机丢失了旧密钥。已有调查中的“对端 IRK 存在”也不等于“本机活动 IRK 与持久化值一致”。

下一次实机应在同一会话内依次记录：

1. 启动后本机 IRK 读取结果、活动值与持久化值是否相同；只记录布尔结果，不记录密钥或摘要。
2. 认证前实际身份地址类型、精确查询与另一 PUBLIC/RANDOM 类型查询的返回码、LTK 是否存在，以及扫描匹配目标与保存身份是否一致；不输出地址或序列信息。
3. 旧密钥恢复加密的原始完成状态；若 `BLE_RESTART_PAIR` 屏蔽该状态，使用可复现的独立诊断构建暴露它，不修改共享 SDK。
4. 重复配对、配对完成、加密完成和断开原因，均由回调投递、owner 任务记录，沿用队列满时关闭会话的策略。

“精确查询失败而备用类型成功”可证明上游地址类型补丁命中；若精确查询成功但远端返回密钥缺失，则继续核查本机身份、密钥一致性和相机记录。最终验收必须包含开发板真实断电重启后恢复加密并到达 Ready。

## 上一轮 furble 兼容修改（历史记录）

按上游实现移植两个行为：构建时对 NimBLE `ble_gap.c` 的项目内副本应用 PUBLIC/RANDOM 备用类型查询；owner 在活动安全会话中自动接受数字比对。首次选择相机、加密与 bond 校验、全部必要 GATT 握手和中性释放仍是进入 Ready 的条件。回调仅投递事件；确认失败关闭，取消、断线、队列溢出或过期会话后不再确认。安全 API 返回 `BLE_HS_EALREADY` 与上游同样视为流程已发起；设置本机地址类型失败则初始化失败。

共享 SDK、NVS 和原绑定不作迁移或清理。保留固定 IDF 的自动重新配对路径，没有移植 NimBLEClient 的自动删除 peer 绑定分支。自动确认可能参与新配对；不能据此承诺相机不需要确认或旧密钥恢复已经修好。

验证：`python3 tools/project.py test` 在 WSL Ubuntu 通过，3 项 CTest、8 项 Python 测试。新增自动确认后的加密/握手门槛、确认失败、断线、取消、队列溢出与过期事件回归，以及补丁不改 SDK、重复运行一致、未知或多匹配源码拒绝构建检查。固件构建结果另见 `docs/verification.md`；实机断电重启待验收。

## 本机身份修复与实机复验

本轮实际读取 `build/log/idf_py_stdout_output_3692`。重新配对在 90286 ms 完成加密和绑定，之后完成 GATT 与拍摄指令发送；开发板下一次启动仍读取到有效元数据及一个 bond。该次启动 3629 ms 精确查询返回 0、LTK / 对端 IRK 存在，但 4726 ms 再次请求数字比对，33631 ms 超时。地址类型备用查询没有触发，因此上一轮补丁不解决该次失败；自动确认也不能替代身份恢复。

新增只输出布尔值与返回码的诊断。`build/log/security-owner-inspection.log` 直接记录：按本机身份查询 SDK local IRK 返回 5（未找到）；本端绑定中的已分发 IRK 存在，却与活动 IRK 不一致；本端和对端 LTK 一致。这证明当前失败与本机身份恢复有关，不能再用“对端 IRK 存在”推断本机 IRK 正确。SDK 内部 local IRK 查找失败的更底层原因尚未独立确定。

修复在相机 owner 初始化时、任何扫描或连接之前执行：

1. 优先读取 `camera_rc/local_irk_v1`（16 字节、固定长度校验）。首次升级没有该记录时，仅从已保存相机的本端 bond 读取此前实际分发的 IRK；没有 bond 时才采用新初始化的活动 IRK。
2. 提交本机 IRK 到独立 NVS 记录；任何读取、长度、写入、提交或恢复错误均停止蓝牙连接，不静默丢弃已有绑定或换新身份。
3. 活动 IRK 不一致时设置正确 IRK，并重建所有已存对端的控制器解析条目。只改运行中的解析列表，不删除任何 peer bond。显式忘记相机也保留本机 IRK。
4. 关闭蓝牙库的 `BLE_RESTART_PAIR`，保存连接只允许复用 LTK，不自动进入新配对。首次／显式重新配对恢复六位数字显示及人工确认；取消、拒绝、断线、溢出与旧会话不能发送迟到确认。

`build/log/security-irk-restored.log` 首次升级记录“从已有绑定恢复=1”“已校正活动IRK=1”“解析列表返回码=0”。此后活动 IRK 与已分发值一致，允许新配对=0、RPA 与活动 IRK 一致，24735 ms 加密完成、32686 ms Ready，全程无 Compare 请求。

随后用户明确执行开发板关机再开机，保持相机与配对不变。`build/log/security-coldboot-verified.log` 记录：

- 1681 ms 从项目持久化记录校正活动 IRK，解析列表恢复成功；绑定数仍为 1。
- 3481 ms：允许新配对=0、RPA 与活动 IRK 一致。
- 3880 ms：安全完成返回 0，encrypted / bonded / authenticated 均为 1。
- 12280 ms：完成必要握手和中性释放，进入 Ready；没有数字比对或新配对。

日志中的“本机IRK查询=5 / 活动与存储IRK一致=0”专指 SDK 的原 local IRK 存储查询，并非项目新建的 `camera_rc/local_irk_v1`。后者在本次关机开机中成功读取并恢复；不要将两个存储位置混淆。

3 项 CTest、8 项 Python 测试、格式检查及精确 IDF 5.5.3 编译通过。固件已保留 NVS 刷入 COM3；没有导出或记录密钥、摘要或序列信息。此次实机通过的是原绑定的单次真实关机开机恢复；新版首次／重新配对数字确认的完整相机交互由主机回归覆盖，尚未为测试而删除当前正常绑定。

最终补充保留解析条目回调错误后再次编译、保留 NVS 刷入并复验，`build/log/security-final-verified.log` 记录 3636 ms 恢复加密、12086 ms Ready，仍为原绑定、无新配对。最终镜像及包校验见 `verification.md`。
