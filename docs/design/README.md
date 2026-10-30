# 圆屏界面与资源

生产界面以 LVGL 绘制，设计基准为 466×466 圆屏。拍摄页顶部下滑进入设备状态，底部上滑进入连接流程；设备页底部返回。全屏模式使用独立的 466×466 快门资源，保留顶部和底部手势区域。

`images/` 与 `prompts/` 保存六组早期设计参考及提示词，并非实际界面。`rendered/` 是生产 UI 的软件渲染，连接和电量数据为模拟值，不是实机照片。

## 预览

准备 LVGL 9.4.0 后运行：

```sh
python tools/project.py preview --lvgl-source managed_components/lvgl__lvgl
```

也可将 LVGL 检出至 `.tools/lvgl-preview`，然后省略 `--lvgl-source`。命令构建原生预览、运行输入和渲染测试，并更新 `rendered/`。模拟检查不包含硬件传输耗时或实屏帧率。

## 资源生成

中文文案修改后，使用固定 SHA-256 的 Noto Sans CJK SC 字体和锁定的 lv_font_conv 1.5.3 更新字库。字体放在 `.tools/fonts/NotoSansCJKsc-Regular.otf`，执行：

```sh
npm ci --prefix tools/fonts
npm --prefix tools/fonts run generate
```

图像资源由 `tools/assets/generate.mjs` 从 `components/ui/assets/` 的固定哈希原稿生成，依赖版本由 lockfile 固定：

```sh
npm ci --prefix tools/assets
npm --prefix tools/assets run generate
```

FUJIFILM 原稿及其 ImageGen 编辑图、X-E5 原稿和快门图片均保留以支持溯源与重新生成；商标和许可说明见[第三方声明](../../THIRD_PARTY_NOTICES.md)。普通固件构建使用已生成的 C 资源，不需要 Node.js。
