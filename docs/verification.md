# 验证记录 · camera-rc 0.2.0

日期：2026-10-29（Asia/Shanghai）。目标板 Waveshare ESP32-S3-Touch-AMOLED-1.43C。此记录区分主机／编译验证与尚未执行的硬件验收。

| 检查 | 结果 |
| --- | --- |
| 可移植核心 | 通过：异步对焦与快门确认计时、长按、取消、双输入、旧完成、断线与释放失败 |
| 富士协议会话 | 通过：Basic/Secure 广播、握手、状态应答、必需与可选订阅的立即／异步失败、两次写入、释放优先、记录校验、地址变化、超时与有限重试 |
| 本地音效数据 | 通过 |
| Python 工具 | 本次 8 项通过，包括 NimBLE 补丁的 SDK 保留／不支持源码拒绝、固定 SDK、NVS 擦除确认、包校验、路径越界和字体覆盖 |
| LVGL 原生输入与主机检查 | 19 项 CTest 通过，其中 16 项 UI 输入检查；扫描选择／取消、数字确认／拒绝、重试、快门与触摸异常等 |
| C++ 格式 | 固定项目格式检查通过 |
| 字体 | 使用锁定 lv_font_conv 1.5.3 与固定 SHA-256 的 Noto 字体重新生成 20/24px 字库，中文覆盖通过 |
| ESP-IDF 固件 | 精确 v5.5.3 编译通过，SDK commit `2c211b236707889e8400c4dc5644dd5c4ee071e0` |
| Imagegen | 内置工具生成六张独立 v2 图稿，已保存图片及完整提示词，并人工检查 |
| 原生渲染 | 已检查拍摄、扫描、进度、设备、安全确认及失败页面，466×466；使用模拟数据 |
| 固件包 | 0.2.0，带偏移与 SHA-256；状态为 `upstream-implemented-hardware-unverified` |

本次本机身份恢复固件 `build/camera_rc.bin`：1174640 字节，SHA-256：

```text
bb84770a824a77e7cd96ea8c6a4c39eef14367506ebcdc80e213396d9e2062d8
```

## 实施说明

已从未启用的手柄 Server 迁移到 furble 固定提交 `0cac22aacc3d9ef250be40849118af78a5f39f27` 的富士客户端协议。所有必要协议检查均在运行时执行，握手和中性释放确认前不启用拍摄。源码来源与 MIT 许可已保留。

SDK 组件管理器读取进程信息时受到本机沙箱限制，授权后使用项目现有固定 SDK 完成编译；没有更换 SDK 或修改其实现。构建、SDK、日志与固件包均在现有忽略目录内。

保留 `build/furble_compat/ble_gap.c` 的地址类型备用查询补丁；共享 SDK 原文件不变。已恢复首次／重新配对显示数字并由用户确认，关闭 SDK 自动重配对，保存连接仅恢复原 LTK。新增 `camera_rc/local_irk_v1` 本机 IRK 持久化，从原绑定中实际分发的 IRK 迁移并恢复活动身份及解析列表，不删除绑定或清空 NVS。

本次已执行：WSL Ubuntu 的 `python3 tools/project.py test`，3 项 CTest、8 项 Python 测试全部通过；Windows 的 `python tools/project.py format-check` 通过。回归覆盖新配对数字显示、人工确认／拒绝、安全门槛、确认失败、断线、取消、队列溢出及旧会话事件；保存重连禁止静默创建新绑定。界面代码与字形未修改，表中的 UI 预览与输入结果沿用此前记录，本次未重新执行预览。

本次 Windows `tools/project.py build` 使用精确 IDF v5.5.3（上述固定 commit）通过全部编译和链接。`compile_commands.json` 确认蓝牙库使用本项目补丁副本及 `MYNEWT_VAL_BLE_RESTART_PAIR=0`。应用镜像 1174640 字节，应用分区剩余 81%；`process()` / `secure()` / `restore_local_identity()` 静态栈帧分别为 784 / 208 / 320 字节，均小于已有 2048 字节限制。固件已保留 NVS 刷入 COM3，包为 `dist/camera-rc-0.2.0.zip`。

## 本次实机重启验证

诊断确认 LTK 双向一致，但旧固件重启后的活动本机 IRK 与实际分发值不同。修复后保留原绑定，从本端 bond 恢复 IRK 并持久化；第一次恢复成功。随后用户真实关机开机，启动日志 3880 ms 完成原绑定加密，12280 ms 完成握手并 Ready，无数字比对、新配对或绑定删除。日志：`build/log/security-owner-inspection.log`、`security-irk-restored.log`、`security-coldboot-verified.log`。具体结论与边界见 `reconnect-investigation.md`。

最终镜像补充解析条目错误传播后再次编译并刷入，`build/log/security-final-verified.log` 显示 3636 ms 原绑定加密、12086 ms Ready；固件包中的应用哈希与上述最终镜像一致，三个镜像校验通过。

## 尚待实机验证

- X-E5 准确固件版本未知；新版首次／重新配对的相机端人工数字确认尚未另行删除正常绑定来实测。
- 已通过上述一次真实开发板关机开机；更多重复循环、相机重启、超距返回、休眠唤醒和异常断线仍待验收。
- AF-S/AF-C/MF 短按、长按连拍与取消后的相机端释放效果待验收。
- 板卡触摸方向、显示传输、音频、电压校准、功耗、两小时稳定性与栈余量待实测。
- 本次 Windows / COM3 刷写通过；其他平台 USB 刷写尚未执行。

上游报告和自动化通过不能代替实机兼容性。具体操作见 `acceptance.md`，协议来源与边界见 `protocol.md`。本版本不提供录像或录音入口。
