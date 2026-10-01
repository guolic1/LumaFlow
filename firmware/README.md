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
│   ├── commands.c/.h              # Application 命令表
│   ├── main.c                     # 应用入口 main()
│   └── application.ld             # 手工维护的应用链接脚本
├── bootloader/
│   ├── CMakeLists.txt             # 显式列出 Bootloader 源码
│   ├── commands.c/.h              # Bootloader 命令表
│   ├── jump_to_application.c/.h   # 应用检查与跳转
│   ├── main.c                     # Bootloader 入口 main()
│   └── bootloader.ld              # 手工维护的 Bootloader 链接脚本
├── common/
│   ├── platform/                  # USART1 适配
│   └── protocol/                  # COBS、CRC16、命令分发和公共命令
├── tests/                         # 可在主机运行的协议测试
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

应用和 Bootloader 分别使用手工维护的 `application/application.ld` 和 `bootloader/bootloader.ld`。64 KB Flash 划分如下，两个分区均按 2 KB 擦除页对齐：

| 镜像 | Flash 起始地址 / 向量表地址 | Flash 结束地址（含） | 容量 |
| --- | --- | --- | --- |
| Bootloader | `0x08000000` | `0x08001FFF` | 8 KB |
| Application | `0x08002000` | `0x0800FFFF` | 56 KB |

两份镜像的 RAM 均从 `0x20000000` 开始，容量为 8 KB，堆预留为 0，栈预留为 1 KB；尚未单独预留升级元数据区域。链接器按各自 Flash 分区限制镜像大小。CubeMX 重新生成的 `.ld` 不会覆盖手工脚本，内存布局及堆栈预留需在手工脚本中维护。

Bootloader 每次复位都会初始化 USART1 并等待 200 ms：收到一帧 CRC 正确的命令后停留在命令模式，否则在应用向量表有效时清理外设状态并跳转到 Application；应用向量无效时永久停留在命令模式，从而保留串口重刷入口。Application 的 `ENTER_BOOTLOADER` 命令会在响应完成后执行软件复位，上位机需要在新的 200 ms 窗口内发送合法命令。应用有效性目前只检查初始栈和复位入口范围，尚未加入整镜像 CRC、升级元数据和 Flash 写入命令。

## 串口命令协议

Application 和 Bootloader 分别链接 `common/` 下的同一份协议与 USART 适配源码，各自提供独立命令表。共享代码不会放在 Application Flash 中，因此应用缺失或损坏时不影响 Bootloader 串口恢复能力。

USART1 使用 `2000000 8N1`。RX 采用循环 DMA，并通过 DMA 半传输、传输完成和 USART IDLE 中断把数据写入 256 字节软件环形缓冲区；TX 使用普通模式 DMA。协议不使用动态内存，最大 payload 为 128 字节。线上帧为 `COBS(raw_frame) + 0x00`，多字节整数均为小端：

| raw frame 字段 | 长度 | 说明 |
| --- | --- | --- |
| version | 1 | 当前为 `1` |
| flags | 1 | bit 0 为 `1` 表示响应 |
| sequence | 1 | 响应回显请求序号 |
| command | 1 | 命令编号 |
| status | 1 | 请求必须为 `0`，响应为状态码 |
| payload length | 2 | payload 字节数 |
| payload | 0～128 | 命令数据 |
| CRC16-CCITT | 2 | 初值 `0xFFFF`，覆盖此前全部 raw frame 字节 |

当前命令如下：

| ID | 命令 | Bootloader | Application | 说明 |
| --- | --- | --- | --- | --- |
| `0x01` | `PING` | 是 | 是 | 原样返回最多 32 字节 payload |
| `0x02` | `GET_INFO` | 是 | 是 | 返回协议、镜像和能力信息 |
| `0x10` | `ENTER_BOOTLOADER` | 否 | 是 | 响应完成后软件复位，主机随后重新握手 |
| `0x11` | `BOOT_APPLICATION` | 是 | 否 | 应用向量表有效时响应并跳转 |

`GET_INFO` 的 12 字节 payload 依次为：协议版本、镜像类型、固件主/次/补丁版本、保留字节、最大 payload（2 字节）、能力位（4 字节）。镜像类型 `1` 表示 Bootloader，`2` 表示 Application。

协议核心的主机测试位于 `tests/`，可独立配置并运行：

```powershell
cmake -S tests -B build/ProtocolTests -G Ninja
cmake --build build/ProtocolTests
ctest --test-dir build/ProtocolTests --output-on-failure
```

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

Application 和 Bootloader 均通过 `board/cmake/stm32cubemx` 编译同一份 `board/startup_stm32g031xx.s`，分别链接到各自 ELF 中；无需在两个源码目录复制 startup，也不要在同一目标中重复添加。startup 提供向量表和 `Reset_Handler`，初始化栈、`.data`、`.bss` 及 C 运行环境后调用本镜像的 `main()`。

两个镜像目录的 `CMakeLists.txt` 为编译 `SystemInit()` 的 `STM32_Drivers` 目标启用 `USER_VECT_TAB_ADDRESS`，并分别设置 `VECT_TAB_OFFSET=0x00000000U`（Bootloader）和 `0x00002000U`（Application）。`Reset_Handler` 在进入 `main()` 前调用 `SystemInit()`，将 `SCB->VTOR` 设置为各自 Flash 分区起始地址；无需修改 CubeMX 生成的系统源码。调整分区时，必须同步修改链接脚本和对应编译偏移。后续实现 Bootloader 跳转时，还需完成硬件状态交接、栈和向量表切换，再进入应用向量表中的复位入口，不能只直接调用应用 `main()`。

## 打开工程

用 VS Code 打开 `firmware/LumaFlow.code-workspace`。工作区根目录为 `firmware`，所有 VS Code 配置都位于该目录内。

从旧布局迁移后，请重新打开这个工作区文件。三个状态栏按钮和扩展推荐由工作区文件提供；仅打开文件夹不会加载这些工作区设置。

首次打开时安装工作区推荐的扩展：

- C/C++：代码导航和诊断。
- CMake Tools：CMake 工程支持。
- Cortex-Debug：嵌入式断点、单步、变量、寄存器和内存调试。
- VsCode Action Buttons：状态栏上的“编译”“下载”“调试”按钮。

## 代码格式化

`firmware/.clang-format` 统一手写 C/C++ 代码风格：4 空格缩进、大括号独占一行、100 列换行，并保留头文件顺序和注释排版。工作区使用 C/C++ 扩展内置的 clang-format，打开源文件后按 `Shift+Alt+F` 格式化，无需另外安装格式化扩展。

`board/.clang-format` 禁用该目录及子目录的格式化和头文件排序，保留 CubeMX 生成代码及厂商驱动的原始格式。命令行使用 `clang-format --style=file`，让工具按源文件路径查找配置；不要显式指定顶层配置文件，否则会绕过 `board` 的禁用设置。

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
