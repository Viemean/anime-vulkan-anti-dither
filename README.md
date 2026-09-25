# 《鸣潮》Vulkan 反虚化驱动层 (WuWa Vulkan Anti-Dither Layer)

基于 Khronos Vulkan Layer 规范与 SPIR-V 运行时 SSA 数据流分析实现的《鸣潮》近身网格消隐中和驱动层。

## 特性说明

- **驱动层拦截**：工作在系统 Vulkan Loader 与显卡驱动之间，不修改游戏目录内的任何文件。
- **双管线支持**：同时支持 DX11（通过 DXVK）与 DX12（通过 vkd3d-proton），无需切换补丁。
- **模组兼容**：与 WWMI（3DMigoto）等 Mod 工具解耦，不产生 DLL 链式加载冲突。
- **精准过滤**：基于单遍向前的 SSA 数据流依赖分析，仅中和相机点阵消隐（`OpKill` / `OpDemote`），保留头发、草木镂空等 Alpha Cutout 材质。
- **针对性激活**：自动读取 `/proc/self/cmdline` 匹配游戏主程序，非目标程序直接直通，无额外性能开销。

---

## 产物下载与安装

直接在 [Releases](https://github.com/SpectrumQT/wuwa-anti-dither/releases) 页面下载构建好的发布包：`wuwa-vulkan-layer.tar.gz`。

### 1. 自动安装

解压发布包并在终端中执行：

```bash
./install.sh
```

脚本将动态库与 Layer 描述清单注册至当前用户的 Vulkan 目录：
- `~/.local/share/vulkan/implicit_layer.d/`
- `~/.local/share/vulkan/explicit_layer.d/`

### 2. 容器沙盒穿透配置 (Lutris / Steam)

在 Linux 环境下，Steam Runtime 或 Lutris（使用 `umu-run`）运行于 Bubblewrap 沙盒（`pressure-vessel`）内，沙盒默认覆盖了用户隐式层目录。需要在启动项中显式注入 Layer 路径：

- **Lutris 配置**：
  进入游戏配置 -> 系统选项 -> 环境变量（Environment variables），添加：
  ```text
  VK_ADD_IMPLICIT_LAYER_PATH = /home/<用户名>/.local/share/vulkan/implicit_layer.d
  VK_LAYER_PATH = /home/<用户名>/.local/share/vulkan/implicit_layer.d
  VK_INSTANCE_LAYERS = VK_LAYER_WUWA_antidither
  ```
- **Steam 启动选项**：
  ```bash
  VK_ADD_IMPLICIT_LAYER_PATH="$HOME/.local/share/vulkan/implicit_layer.d" VK_LAYER_PATH="$HOME/.local/share/vulkan/implicit_layer.d" VK_INSTANCE_LAYERS="VK_LAYER_WUWA_antidither" %command%
  ```

### 3. 卸载

```bash
./uninstall.sh
```

---

## 运行日志验证

驱动层支持宿主与容器沙盒双向同步日志：

```bash
cat ~/.local/share/wuwa_vulkan_layer.log
```

输出示例：
```text
[WuWa-Vulkan-Layer] Process: ...Client-Win64-Shipping.exe | Enabled: 1 | Dump: 0
[WuWa-Vulkan-Layer] WordCount: 5181 | DitherNOP: 2 | CutoutKept: 0
```
- `DitherNOP > 0`：表示相机近身遮挡消隐指令已成功中和；
- `CutoutKept > 0`：表示贴图 Alpha 镂空材质已正常保留。

---

## 本地编译

依赖：`meson`、`ninja`、`gcc`/`clang`、`vulkan-headers`。

```bash
./vulkan-layer/build.sh
```

构建产物输出至 `vulkan-layer/dist/` 目录。

