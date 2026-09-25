# Vulkan 反虚化驱动层

基于 Vulkan Layer 规范与 SPIR-V 运行时字节码分析实现游戏的反虚化插件。工作在系统 Vulkan Loader 与显卡驱动之间，不修改游戏本体文件。

## 特性

- **无侵入性**：驱动层拦截着色器创建，不修改游戏客户端文件。
- **管线兼容**：同时兼容 DX11（DXVK）与 DX12（vkd3d-proton）。
- **模组兼容**：与 3DMigoto / WWMI 解耦，无 DLL 链式加载冲突。
- **精准过滤**：通过 SSA 数据流分析仅中和相机点阵消隐（`OpKill` / `OpDemote`），保留贴图 Alpha Cutout 镂空。
- **按需激活**：读取 `/proc/self/cmdline` 匹配游戏进程，非目标程序直通运行。

## 安装使用

### 1. 下载与安装

从 Releases 页面下载 `wuwa-vulkan-layer.tar.gz`，解压后运行安装脚本：

```bash
tar -xvf wuwa-vulkan-layer.tar.gz
./install.sh
```

脚本将驱动层注册至用户 Vulkan 隐式层目录（`~/.local/share/vulkan/implicit_layer.d/`）。

### 2. 启动配置（容器沙盒环境）

在 Steam Runtime 或 Lutris（使用 `umu-run`）等沙盒环境中，如隐式层未自动加载，需在启动项或环境变量中显式指定路径：

- **Steam 启动选项**：
  ```bash
  VK_ADD_IMPLICIT_LAYER_PATH="$HOME/.local/share/vulkan/implicit_layer.d" VK_LAYER_PATH="$HOME/.local/share/vulkan/implicit_layer.d" VK_INSTANCE_LAYERS="VK_LAYER_WUWA_antidither" %command%
  ```
- **Lutris 环境变量**：
  ```text
  VK_ADD_IMPLICIT_LAYER_PATH = /home/<用户名>/.local/share/vulkan/implicit_layer.d
  VK_LAYER_PATH = /home/<用户名>/.local/share/vulkan/implicit_layer.d
  VK_INSTANCE_LAYERS = VK_LAYER_WUWA_antidither
  ```

### 3. 卸载

```bash
./uninstall.sh
```
### 4. 支持的游戏

- 鸣潮（DXVK / VKD3D）
- 蓝色星原：旅谣（DXVK）

## 运行验证

查看驱动层日志：

```bash
cat ~/.local/share/wuwa_vulkan_layer.log
```

日志输出示例：
```text
[WuWa-Vulkan-Layer] Process: ...Client-Win64-Shipping.exe | Enabled: 1 | Dump: 0
[WuWa-Vulkan-Layer] WordCount: 5181 | DitherNOP: 2 | CutoutKept: 0
```
- `DitherNOP > 0`：表示相机消隐指令已中和。
- `CutoutKept > 0`：表示材质 Alpha 镂空已保留。

## 源码编译

依赖：`meson`、`ninja`、`gcc`/`clang`、`vulkan-headers`。

```bash
./vulkan-layer/build.sh
```

编译产物输出至 `vulkan-layer/dist/`。
