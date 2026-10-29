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

[furble](https://github.com/gkoh/furble/tree/0cac22aacc3d9ef250be40849118af78a5f39f27)（MIT，Copyright (c) 2020 Guo-Rong Koh）是本项目富士 Basic/Secure 广播识别、UUID、握手顺序、订阅规则及快门报文的来源，固定提交 `0cac22aacc3d9ef250be40849118af78a5f39f27`。协议已改写为本项目的异步状态机与 NimBLE host 传输，未引入 Arduino 或 M5Stack 运行时。许可全文见 `LICENSES/furble-MIT.txt`。上游 `patches/ble_gap.patch` 的地址类型重查逻辑移植为 `tools/cmake/furble_nimble_patch.cmake`，仅生成构建目录副本，不修改 SDK；省略上游地址日志，保留查询返回码。与上游的有意差异：数字比对须人工确认；禁止旧绑定恢复失败后自动新配对；只保存一台；开机自动连接；必须检查快门写权限、每次写入结果及中性释放；所有取消和发送均由单 owner 任务处理。上游兼容报告不等于本项目 X-E5 实机验收。

构建依赖保留各自 LICENSE；发布源代码时保留本文件及 LICENSES。imagegen 图稿仅用于本项目视觉设计，不构成富士官方产品或协议认证。

拍摄页 FUJIFILM 标识原稿由用户提供（原文件名 `Fujifilm_logo.svg`），保存于 `components/ui/assets/`；SHA-256 为 `d86cf1e0208b9898846f9ccd93bc514be3130fcb64492e5fc915a4e08d7dd8f2`。当前固件使用以原稿为参考、由 ImageGen 编辑并经用户选择的图片标识。FUJIFILM 商标及标识归其权利人所有，不随本项目的代码许可证授予商标权利。设备页同时显示归属提示。

图像资源离线生成工具为 @resvg/resvg-js 2.6.2（MPL-2.0）及 pngjs 7.0.0（MIT），依赖版本与完整性由 `tools/assets/package-lock.json` 固定；生成工具不进入固件。

拍摄页 X-E5 型号标识由用户提供（原文件名 `x-e5logo-ef75c1dd-bd31-4f9e-8fe2-ac3f4e329bb0.webp`），原稿 SHA-256 为 `6ebaa8f2ffa8e796c71f999eb778f6457bbaeae0960c958bfed04dd1e088945a`。仅解码并等比缩放，未重绘字形或改变颜色；其商标及标识归相应权利人所有。RGBA PNG 中间文件 SHA-256 为 `829b85754267e291d3abbd566ea88b6d20f76e82f995daa201a87520c8673e93`；一次性解码使用 sharp 0.35.5（Apache-2.0），该工具不进入固件或普通构建依赖。
