# LumaFlow 固件开发

目标芯片为 STM32G031G8，使用现有 CubeMX CMake 工程、Arm GNU Toolchain、Ninja 和 OpenOCD。以下 VS Code 配置面向 Windows。

## 源码与构建结构

```text
firmware/
├── LumaFlow.code-workspace        # VS Code 工作区，以 firmware 为根目录
├── .vscode/                      # 编译、下载、调试和本地工具路径配置
├── CMakeLists.txt                 # 手工维护，组织固件目标和各子目录
├── CMakePresets.json              # 顶层 Debug / Release 配置
├── cmake/gcc-arm-none-eabi.cmake   # 顶层构建使用的工具链
├── application/
│   ├── CMakeLists.txt             # 显式列出应用源码
│   ├── main.c                     # 应用入口 main()
│   └── application.ld             # 手工维护的应用链接脚本
├── bootloader/                    # 当前为空，尚未加入构建
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

任务会将指定的工具链目录加入自己的 `PATH`，并显式传入 Ninja 路径。更换编译器后，应先清理 `firmware/build/Debug` 中的旧 CMake 缓存，再重新编译。新机器初次使用 CMake Tools 面板时，选择 `Debug` preset；三个状态栏按钮固定使用 Debug，避免下载与调试使用不同固件。

## 三个按钮

| 按钮 | 执行过程 |
| --- | --- |
| 编译 | `cmake --preset Debug`，然后 `cmake --build --preset Debug --parallel` |
| 下载 | 检查本地调试路径 → 编译 → OpenOCD 写入 ELF、校验、复位运行、退出 |
| 调试 | 检查本地调试路径 → 编译 → Cortex-Debug 启动 OpenOCD/GDB、下载并停在 `main` |
