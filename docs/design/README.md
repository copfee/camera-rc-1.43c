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

设计基准 466×466、1.43 英寸圆形 AMOLED。纯黑 `#000000`、暖白 `#F0ECE1`、灰 `#8F908D`、就绪绿 `#8BC5A1`、过程琥珀 `#D4AE76`。260px 快门、20/24px 中文、至少 56px 主要操作高度。没有录像或录音入口，快门使用带龙头雕刻的黄铜纹理；设备信息采用中央可滚动区域，配对身份过长省略显示。

生成图是参考而非实机截图，示例值为模拟数据。生产界面使用 LVGL 原生控件；简化生成图中的装饰图标，列表滚动支持 16 个候选，所有交互基于运行时快照。

`rendered/` 是真实生产 UI 代码的 LVGL 软件渲染（模拟数据），不是 imagegen 图，也不是实机照片。运行：

```sh
python3.13 tools/project.py preview
# 也可复用固件构建已下载的 LVGL 9.4.0：
python3.13 tools/project.py preview --lvgl-source managed_components/lvgl__lvgl
```

输出 `ready`、`disconnected`、`status`、`status-actions`、`pairing`、`connecting`、`confirm`、`error` 等图，并运行扫描选择／取消、数字确认／拒绝、重试、滑动取消、触摸异常恢复、首次触摸唤醒、快门长按等输入测试。拍摄页点击顶部 FUJIFILM 标识进入设备状态页，设备页底部返回拍摄，左右滑动不切页；设备管理双按钮同排随内容滚动。另有连接圆环多轮循环及取消后动画清理测试。

刷新与动画周期为 16ms，触摸周期为 8ms；启动时也会覆盖旧 `sdkconfig` 中的 33ms 默认值。快门按下／释放切换黄铜按钮图片，只影响本地视觉反馈。新增的 `spinner-cadence`、`shutter-feedback`、`device-refresh` 使用真实 LVGL 定时器与 40 行局部缓冲，检查动态帧间隔、命令及时投递、局部重绘及静止时无重复刷新；不强制调用动画或显示刷新函数。这些检查不模拟硬件传输耗时，也不代表面板实测帧率。

连接圆环使用固定 90° 的 LVGL Arc，由单一定时器按实际经过时间旋转，周期为 1200ms。连续进度阶段仅原位更新标题与阶段文字，不重建弹层、不重置圆环；安全确认、失败、完成或取消才切换页面，并在删除圆环前停止定时器。`progress-continuity` 检查四圈首尾相位、弧长和可见像素、阶段／会话／名称变化时的对象复用、延迟调度后的相位追赶及取消后的定时器清理。

字体使用固定 SHA-256 的 Noto Sans CJK SC 与锁定的 lv_font_conv 1.5.3，详见第三方声明。更新文案后运行 `npm --prefix tools/fonts run generate`；普通构建不需要 Node 或 imagegen。设计参考图不可直接贴作可交互界面。

拍摄页和连接进度页顶部使用用户选择的 ImageGen 标识图。资源裁除透明留边并缩放为 196×46px，保留透明度，由固件与原生预览共用。输入图片位于 `components/ui/assets/fujifilm_glow_imagegen_reference.png`，生成脚本校验其 SHA-256。重新生成：`npm ci --prefix tools/assets` 后运行 `npm --prefix tools/assets run generate`；渲染器 @resvg/resvg-js 2.6.2 与 PNG 工具 pngjs 7.0.0 均固定版本。普通构建不运行资源生成器。

拍摄页连接状态恢复为文字：就绪显示绿色“已连接”，断开显示灰色“已断开”；移除呼吸灯、灰灯及其动画，状态整行及扩展点击范围放在快门下方。其余连接阶段保留说明和弹层。设备页保留连接文字，蓝牙名称和地址的标题与值分别同行显示，值过长时省略；无当前目标时展示已保存地址，没有记录时显示“尚未配对”。后续信息和管理按钮保持固定位置。商标归属提示随内容滚动，底部返回入口固定。

外晕参考图 `components/ui/assets/fujifilm_glow_imagegen_reference.png` 由内置 imagegen 根据现有标识生成，提示为增强柔和白色外晕、保留 FUJIFILM 字形及红色细节并使用透明背景。用户选择将该生成图直接用于固件标识。

拍摄页双标识布局：FUJIFILM 标识为 196×46px，位于 (135,22)，点击进入设备状态；X-E5 图使用原始 249×44 透明白字，等比装入 100×18px 透明画布，位于 (183,73)。快门为 260px，位于 (103,104)；拍摄反馈位于 y=384，连接状态位于 y=420，均使用 24px 字体。型号标识只作为固定图形，不参与蓝牙名称或型号识别。

X-E5 原稿为 `components/ui/assets/xe5_logo_source.webp`；使用 sharp 0.35.5 解码为同尺寸 RGBA PNG，保存为 `xe5_logo_source.png`，保留原图透明度和字形。资源生成脚本校验两者 SHA-256，并使用现有固定版本 resvg/pngjs 生成等比缩放 PNG 及 LVGL C 资源；常规构建只引用 C 资源。
