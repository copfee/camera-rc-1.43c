# 第三方来源

| 内容 | 来源与版本 | 许可 |
| --- | --- | --- |
| 板卡引脚、显示初始化序列、电压分压与音频配置 | [Waveshare 1.43C](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.43C)，commit `8cea11ddae6167dd591d1f5465fd7b615080360f` | Apache-2.0，Copyright 2026 Waveshare |
| 中文字库 | [Noto Sans CJK SC Regular](https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf) | SIL OFL 1.1 |
| 字体生成工具 | lv_font_conv 1.5.3 | MIT |
| 固件 SDK | ESP-IDF v5.5.3，commit `2c211b236707889e8400c4dc5644dd5c4ee071e0` | Apache-2.0 及其组件各自许可 |
| BLE | h2zero/esp-nimble-cpp 2.5.0 | Apache-2.0 |
| UI | LVGL 9.4.0 | MIT；Montserrat 字体有其随附许可 |
| 显示传输 | espressif/esp_lcd_sh8601 2.0.1 | Apache-2.0 |
| ES8311 音频驱动 | espressif/esp_codec_dev 1.5.8 | Apache-2.0 |

板卡适配已从原例程重写：移除录音／文件系统／演示功能与单例 I2C 封装，加入错误处理、坐标边界、独立 DMA/LVGL 显示移植和 ADC 有效性判断。快门反馈使用原创合成 PCM，音频接线和功放增益来自固定提交的 `04_Audio_Test`，ES8311 初始化参考 ESP-IDF v5.5.3 的 CC0-1.0 示例 `examples/peripherals/i2s/i2s_codec/i2s_es8311/main/i2s_es8311_example.c`。仍保留板厂归属与许可全文于 `LICENSES/Waveshare-Apache-2.0.txt`。

字体原文件 SHA-256：`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`。中文生成字体命名为 **RC font**，仅包含界面所需字形，许可全文见 `LICENSES/Noto-OFL-1.1.txt`。原字体元数据：Copyright 2014-2021 Adobe (http://www.adobe.com/). Noto is a trademark of Google Inc.

[furble](https://github.com/gkoh/furble/tree/0cac22aacc3d9ef250be40849118af78a5f39f27)（MIT，Copyright (c) 2020 Guo-Rong Koh）是本项目富士 Basic/Secure 广播识别、UUID、握手顺序、订阅规则及快门报文的来源，固定提交 `0cac22aacc3d9ef250be40849118af78a5f39f27`。协议已改写为本项目的异步状态机与 NimBLE host 传输，未引入 Arduino 或 M5Stack 运行时。许可全文见 `LICENSES/furble-MIT.txt`。与上游的有意差异：只保存一台；开机自动连接；数字比对要求用户确认；必须检查快门写权限、每次写入结果及中性释放；所有取消和发送均由单 owner 任务处理。上游兼容报告不等于本项目 X-E5 实机验收。

构建依赖保留各自 LICENSE；发布源代码时保留本文件及 LICENSES。imagegen 图稿仅用于本项目视觉设计，不构成富士官方产品或协议认证。
