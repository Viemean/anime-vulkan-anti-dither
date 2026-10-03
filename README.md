# Vulkan Anti-Dither Layer & Game Patches

## 1. 项目说明
通用二游反虚化Vulkan Layer

## 2. 工作原理

### 2.1 Vulkan 图形驱动层
* 作为标准 Vulkan Implicit Layer 随 Vulkan Loader 自动注入游戏进程；
* 拦截 `vkCreateShaderModule`，在翻译层（DXVK / VKD3D-Proton）生成 SPIR-V 字节码阶段注入处理流水线；
* 运用反向切片追踪（Backward Demote Slicing）与 SSA 正反向级联数据流分析，解耦 `NMin` / `FMin` 中混叠的网格淡出噪声与屏幕空间面部投影遮罩，中和针对角色材质的 `OpKill` 与 `OpDemoteToHelperInvocation`。

### 2.2 Mod
* 拦截 `vkCmdDrawIndexed` 原生渲染调用；
* 依据渲染图元的几何特征（如 `indexCount` 索引计数）进行函数指针直调过滤；
* 仅针对指定模型的独立绘制调用执行拦截跳过。

### 2.3 Addon
* 承接 Vulkan 图形层无法处理的引擎内部状态限制；
* 挂载于 Vulkan 逻辑设备创建生命周期（`on_device_created` 回调），在实例初始化并确认配置后拉起独立后台工作线程；
* **利用动态链接库注入后与宿主游戏处于同一进程地址空间、具备完全同等内存读写权限的原理**，直接在宿主进程内存中执行特征码扫描，通过修改内存页属性单点写入热补丁，拦截视距剔除判断函数，或常驻写入目标帧率变量。

---

## 3. 运行环境与版本建议
* **兼容运行环境**：建议保持使用最新的 **GE-Proton** 或 **DWProton**，以保证对 SPIR-V 扩展规范（如 `SPV_EXT_demote_to_helper_invocation`、`PhysicalStorageBufferAddresses`）的完整支持。

---

## 4. 支持游戏列表

| 游戏名称 | 英文标识 / 进程名关键字 | 翻译层后端 | 特性支持 |
| :--- | :--- | :--- | :--- |
| **鸣潮 (Wuthering Waves)** | `Client-Win64-Shipping`, `wuwa` | DXVK / VKD3D | 反虚化 |
| **原神 (Genshin Impact)** | `GenshinImpact`, `YuanShen` | DXVK | 反虚化、帧率解锁 |
| **崩坏：星穹铁道 (Honkai: Star Rail)** | `StarRail` | DXVK | 反虚化、帧率解锁 |
| **绝区零 (Zenless Zone Zero)** | `ZenlessZoneZero`, `zzz` | DXVK | 反虚化 |
| **崩坏3 (Honkai Impact 3rd)** | `BH3`, `HonkaiImpact3` | DXVK | 反虚化 |
| **异环 (Neverness To Everness)** | `HTGame`, `HT-Win64` | DXVK / VKD3D | 反虚化、相机防裁剪补丁 |
| **蓝色星原：旅谣 (Azur Promilia)** | `AzurPromilia`, `AP-Win64` | DXVK / VKD3D | 反虚化 |
| **明日方舟：终末地 (Arknights: Endfield)**| `Endfield`, `zmd` | VKD3D / DXVK | 反虚化 |
| **少女前线2：追放 (Girls' Frontline 2)** | `GF2_Exilium` | DXVK | 反虚化 |
| **幻塔 (Tower of Fantasy)** | `QRSL`, `TowerOfFantasy` | DXVK / VKD3D | 反虚化 |
| **二重螺旋 (Duet Night Abyss)** | `EM-Win64-Shipping`, `DNA` | DXVK / VKD3D | 反虚化，对游戏世界有一定破坏效果，会导致lod距离加载范围内树木变成卡片 |
| **星痕共鸣 (Star)** | `Star`, `star` | DXVK | 反虚化 |

---

## 5. 环境变量说明

环境变量具备最高生效优先级，会覆盖静态配置文件中的对应项。

### 5.1 主要环境变量

| 环境变量 | 可选值 | 说明 |
| :--- | :--- | :--- |
| `DISABLE_ANTI_DITHER` | `1` | Vulkan Loader 隐式层总开关。设置为 `1` 时完全停用该层加载。 |
| `ANTI_DITHER_ENABLED` | `1`, `0` | 反虚化核心开关。`1` 强制启用，`0` 强制禁用。默认时由内置进程白名单自动判断开启。 |
| `ANTI_DITHER_TARGETS` | 字符串 | 自定义目标进程识别关键字，多个以逗号分隔（如 `MyGame,TestApp`）。 |
| `ANTI_DITHER_UNLOCK_FPS` | `1`, `0` | 米哈游游戏（原神、星铁）帧率限制解除开关。`1` 开启，`0` 关闭。 |
| `ANTI_DITHER_TARGET_FPS` | 整数 | 目标帧率设定。`0` 表示不限帧，可选 `30` 以上为具体目标帧数（默认为 `120`）。 |

### 5.2 开发与诊断变量

<details>
<summary>展开查看开发与诊断调试变量</summary>

| 环境变量 | 可选值 | 说明 |
| :--- | :--- | :--- |
| `ANTI_DITHER_LOG` | `1`, `0` | 详细运行时日志开关。日志输出至 `/tmp/game_anti_dither/logs/<进程名>.log`。 |
| `ANTI_DITHER_DUMP` | `1`, `0` | 着色器转储开关。将截获的原始与改写后 SPIR-V 导出至 `/tmp/game_anti_dither/dumps/<进程名>/`。 |
| `ANTI_DITHER_EXCLUDE_HASHES` | 十六进制 Hash 列表 | 着色器黑名单。豁免指定 Hash 的改写（如 `0xfe0ddc28,0x527c85c0`）。 |
| `ANTI_DITHER_FORCE_HASHES` | 十六进制 Hash 列表 | 着色器白名单。强制对指定 Hash 执行网点消除。 |
| `ANTI_DITHER_FORCE_ALL` | `1`, `0` | 极端排查开关。无条件消除所有着色器中的 Discard/Demote 指令。 |
| `ANTI_DITHER_HASH_MIN` | 十六进制 Hash | 配合 `ANTI_DITHER_FORCE_ALL` 使用的 Hash 二分检索下界。 |
| `ANTI_DITHER_HASH_MAX` | 十六进制 Hash | 配合 `ANTI_DITHER_FORCE_ALL` 使用的 Hash 二分检索上界。 |

</details>

---

## 6. 配置文件说明

* **基准配置文件路径**：`~/.config/anti_dither/rules.conf`
* **配置优先级**：命令行或启动项环境变量（如 `ANTI_DITHER_ENABLED=1`）具有最高覆盖权；未指定环境变量时，以静态配置文件设定为准。

---

## 7. 免责声明

本项目提供的帧率解锁与热补丁功能仅供兼容性研究与图形显示优化用途，使用者需知悉并自行评估在包含反作弊或在线联网游戏环境下运行可能产生的账户封禁风险。
