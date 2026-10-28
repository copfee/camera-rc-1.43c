# 极简圆屏 UI · v2

使用内置 imagegen 生成六张**独立**设计参考，所有最终图稿均保存在本项目 `images/`，完整提示词在 `prompts/`，同名 `*-v2` 文件一一对应：

| 图稿 | 界面 |
| --- | --- |
| `01-shutter-v2.png` | 单一大快门主页 |
| `02-scan-v2.png` | 相机扫描与选择 |
| `03-connecting-v2.png` | 连接阶段与取消 |
| `04-device-v2.png` | 本机电量、连接信息与管理 |
| `05-confirm-v2.png` | 六位数字安全配对确认 |
| `06-error-v2.png` | 连接失败与重试 |

设计基准 466×466、1.43 英寸圆形 AMOLED。纯黑 `#000000`、暖白 `#F0ECE1`、灰 `#8F908D`、就绪绿 `#8BC5A1`、过程琥珀 `#D4AE76`。224px 快门、20/24px 中文、至少 56px 主要操作高度。没有录像或录音入口，没有金属纹理和复杂阴影；设备信息采用中央可滚动区域，配对身份过长省略显示。

生成图是参考而非实机截图，示例值为模拟数据。生产界面使用 LVGL 原生控件；简化生成图中的装饰图标，列表滚动支持 16 个候选，所有交互基于运行时快照。旧版三张图稿仅作历史资料，不代表当前产品。

`rendered/` 是真实生产 UI 代码的 LVGL 软件渲染（模拟数据），不是 imagegen 图，也不是实机照片。运行：

```sh
python3.13 tools/project.py preview
```

输出 `ready`、`status`、`pairing`、`connecting`、`confirm`、`error` 等图，并运行扫描选择／取消、数字确认／拒绝、重试、滑动取消、触摸异常恢复、首次触摸唤醒、快门长按等输入测试。

字体使用固定 SHA-256 的 Noto Sans CJK SC 与锁定的 lv_font_conv 1.5.3，详见第三方声明。更新文案后运行 `npm --prefix tools/fonts run generate`；普通构建不需要 Node 或 imagegen。设计参考图不可直接贴作可交互界面。
