# LumaFlow 固件开发

目标芯片为 STM32G031G8，使用现有 CubeMX CMake 工程、Arm GNU Toolchain、Ninja 和 OpenOCD。以下 VS Code 配置面向 Windows。

## 源码与构建结构

```text
firmware/
├── LumaFlow.code-workspace        # VS Code 工作区，以 firmware 为根目录
├── .vscode/                      # 编译、下载、调试和本地工具路径配置
├── CMakeLists.txt                 # 手工维护，组织固件目标和各子目录
├── CMakePresets.json              # 镜像配置与 Debug / Release 编译选项
├── cmake/gcc-arm-none-eabi.cmake   # 顶层构建使用的工具链
├── application/
│   ├── CMakeLists.txt             # 显式列出应用源码
│   ├── main.c                     # 应用入口 main()
│   └── application.ld             # 手工维护的应用链接脚本
├── bootloader/
│   ├── CMakeLists.txt             # 显式列出 Bootloader 源码
│   ├── main.c                     # Bootloader 入口 main()
│   └── bootloader.ld              # 手工维护的 Bootloader 链接脚本
└── board/                        # CubeMX 工程与全部生成内容
    ├── LumaFlow.ioc
    ├── .mxproject
    ├── Core/
    ├── Drivers/
    ├── startup_stm32g031xx.s
    ├── STM32G031xx_FLASH.ld
    ├── CMakeLists.txt
    ├── CMakePresets.json
    └── cmake/
```
`board/.gitignore` 忽略生成的顶层 CMake 工程、Preset、两份工具链、`*.ld` 和 `build/`。`cmake/stm32cubemx/CMakeLists.txt`、源码、驱动、启动文件、`.ioc` 及 `.mxproject` 继续保留。

应用使用 `application/application.ld`，需纳入版本管理；当前仍为从 `0x08000000` 开始的完整 64 KB Flash，尚未划分 Bootloader 区域。分区大小确定后，再分别调整应用和 Bootloader 的独立链接脚本。CubeMX 重新生成的 `.ld` 不会覆盖手工脚本，内存布局及堆栈预留需在手工脚本中维护。

Bootloader 使用 `bootloader/bootloader.ld`，目前同样占用 `0x08000000` 起的 64 KB Flash 地址范围，RAM 为 8 KB，堆预留为 0，栈预留为 1 KB。两份镜像的地址范围重叠，目前只能分别下载运行，不能作为分区固件同时烧录。Bootloader 入口目前执行 `HAL_Init()`、`SystemClock_Config()` 后进入主循环，尚未实现升级或跳转到应用。

## 选择配置和编译选项

顶层 CMake 通过 `LUMAFLOW_IMAGE` 选择 `application`（默认）或 `bootloader`，每次构建只加入对应目录的 `main.c` 和 `.ld`。

使用 Ninja Multi-Config，将镜像配置和编译选项分开选择：

| 选择项 | 可选值 | VS Code 命令 |
| --- | --- | --- |
| 配置（Configure Preset） | `Application`、`Bootloader` | `CMake: Select Configure Preset` |
| 编译选项（Build Preset） | `Debug`、`Release` | `CMake: Select Build Preset` |

状态栏显示这两个选择入口，点击即可切换；随后三个按钮均使用当前选择。CMake Tools 会按当前配置过滤编译选项，显示 `Debug`、`Release`，以及扩展内置的 `[Default]`；请明确选择前两项之一。该行为见 [CMake Tools 官方说明](https://github.com/microsoft/vscode-cmake-tools/blob/main/docs/cmake-presets.md#cmake-select-build-preset)。

Build Preset 在 JSON 内使用 `Application-Debug` 等唯一名称，界面显示名为 `Debug` 或 `Release`。输出 ELF 和 map 分别位于 `build/<配置>/<编译选项>/`，例如 `build/Bootloader/Debug/LumaFlow.elf`，四种组合互不覆盖。

在 `firmware` 目录执行（`cmake` 需在 `PATH` 中，或使用本地配置里的完整路径）：

```powershell
cmake --preset Bootloader
cmake --build build/Bootloader --config Debug --parallel
# 等效编译命令：cmake --build --preset Bootloader-Debug --parallel
```

切换为 `Release` 时只改变编译选项，不需要重新选择镜像。`Debug` 使用 `-Og -g3`，`Release` 使用 `-Os -g0`，并通过 CMake 的 `INTERPROCEDURAL_OPTIMIZATION_RELEASE` 为固件和 HAL/LL 驱动开启 LTO；源码单步调试请选择不启用 LTO 的 `Debug`。LTO 在链接阶段进行跨文件优化，仍会编译构建列表中的源文件。两种模式均保留未使用代码段的自动裁剪。代码补全由 CMake Tools 提供当前配置，下载与调试也从 CMake Tools 获取当前 ELF 路径。

从旧配置迁移后，重新选择一次 Configure Preset 和 Build Preset。原 `build/Debug`、`build/Release`、`build/Bootloader-Debug` 等目录不再使用，可自行删除。

工作区的 `C_Cpp.default.compileCommands` 已加入 `build/Application/compile_commands.json` 和 `build/Bootloader/compile_commands.json`。首次使用时分别执行 `cmake --preset Application`、`cmake --preset Bootloader` 生成数据库，无需先编译。Ninja Multi-Config 的每份数据库同时包含 Debug 和 Release 的记录，不会在这两个编译选项子目录下单独生成数据库。CMake Tools 仍优先为当前配置和编译选项提供 IntelliSense 参数；编译数据库用于补充未由它提供配置的文件。此优先级见 [C/C++ 扩展官方说明](https://code.visualstudio.com/docs/cpp/customize-cpp-settings)。

Application 和 Bootloader 均通过 `board/cmake/stm32cubemx` 编译同一份 `board/startup_stm32g031xx.s`，分别链接到各自 ELF 中；无需在两个源码目录复制 startup，也不要在同一目标中重复添加。startup 提供向量表和 `Reset_Handler`，初始化栈、`.data`、`.bss` 及 C 运行环境后调用本镜像的 `main()`。后续实现 Bootloader 跳转时，应进入应用向量表中的复位入口，并配合应用中断向量表重定位，不能只直接调用应用 `main()`。

## 打开工程

用 VS Code 打开 `firmware/LumaFlow.code-workspace`。工作区根目录为 `firmware`，所有 VS Code 配置都位于该目录内。

从旧布局迁移后，请重新打开这个工作区文件。三个状态栏按钮和扩展推荐由工作区文件提供；仅打开文件夹不会加载这些工作区设置。

首次打开时安装工作区推荐的扩展：

- C/C++：代码导航和诊断。
- CMake Tools：CMake 工程支持。
- Cortex-Debug：嵌入式断点、单步、变量、寄存器和内存调试。
- VsCode Action Buttons：状态栏上的“编译”“下载”“调试”按钮。

## 本地路径配置

首次配置时，将 `firmware/.vscode/settings.example.json` 复制为 `firmware/.vscode/settings.json`。已有的本地路径配置随目录迁移保留，该文件不提交 Git。以下任务中的 `${workspaceFolder}` 均指 `firmware`。

| 配置项 | 填写内容 |
| --- | --- |
| `cmake.cmakePath` | `cmake.exe` 的完整路径 |
| `lumaflow.ninjaPath` | `ninja.exe` 的完整路径 |
| `lumaflow.armToolchainPath` | Arm GNU Toolchain 的 `bin` 目录 |
| `C_Cpp.default.compilerPath` | 同一工具链的 `arm-none-eabi-gcc.exe` 完整路径 |
| `lumaflow.openocdPath` | `openocd.exe` 的完整路径 |
| `lumaflow.openocdConfig` | 自行准备的 OpenOCD `.cfg` 文件的完整路径；暂不使用硬件时留空 |

工具链文件会读取 `.vscode/settings.json` 中的 `lumaflow.armToolchainPath` 和 `lumaflow.ninjaPath`，供 CMake Tools 和命令行共同使用，无需重复填写路径；此文件须使用标准 JSON。未提供本地配置时，从 `PATH` 查找工具。更换编译器后，对 `build/Application` 和 `build/Bootloader` 中已使用的构建目录清理缓存并重新配置。首次打开时，先选择 `Application` 配置和 `Debug` 编译选项。

## 三个按钮

| 按钮 | 执行过程 |
| --- | --- |
| 编译 | 按当前 Configure Preset 配置，再按当前 Build Preset 编译 |
| 下载 | 检查本地调试路径 → 编译当前选择 → OpenOCD 写入对应 ELF、校验、复位运行、退出 |
| 调试 | 检查本地调试路径 → 编译当前选择 → Cortex-Debug 加载对应 ELF、下载并停在 `main` |
