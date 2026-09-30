# LumaFlow 固件开发

目标芯片为 STM32G031G8，使用现有 CubeMX CMake 工程、Arm GNU Toolchain、Ninja 和 OpenOCD。以下 VS Code 配置面向 Windows。

## 打开工程

用 VS Code 打开仓库根目录的 `LumaFlow.code-workspace`。

首次打开时安装工作区推荐的扩展：

- C/C++：代码导航和诊断。
- CMake Tools：CMake 工程支持。
- Cortex-Debug：嵌入式断点、单步、变量、寄存器和内存调试。
- VsCode Action Buttons：状态栏上的“编译”“下载”“调试”按钮。

按钮未出现时执行 `Developer: Reload Window`，并确认工作区已受信任、状态栏已显示。

## 本地路径配置

将 `.vscode/settings.example.json` 复制为 `.vscode/settings.json`。

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