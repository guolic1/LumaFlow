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
│   ├── application_image.c/.h     # 镜像元数据、向量表和 CRC32 校验
│   ├── application_update.c/.h    # Application 擦写与升级状态机
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

应用和 Bootloader 分别使用手工维护的 `application/application.ld` 和 `bootloader/bootloader.ld`。64 KB Flash 划分如下。升级元数据直接占用 Application 区域末尾 32 字节，不额外占用擦除页：

| 区域 | Flash 起始地址 | Flash 结束地址（含） | 容量 |
| --- | --- | --- | --- |
| Bootloader | `0x08000000` | `0x08001FFF` | 8 KB |
| Application 镜像 | `0x08002000` | `0x0800FFDF` | 57312 字节（`0xDFE0`） |
| Application 元数据 | `0x0800FFE0` | `0x0800FFFF` | 32 字节 |

两份镜像的 RAM 均从 `0x20000000` 开始，容量为 8 KB，堆预留为 0，栈预留为 1 KB。Application 链接脚本只允许使用前 57312 字节，防止链接结果覆盖末尾元数据。元数据与镜像尾部共享最后一个 2 KB 擦除页，但不会占用额外页。CubeMX 重新生成的 `.ld` 不会覆盖手工脚本，内存布局及堆栈预留需在手工脚本中维护。

Bootloader 每次复位都会初始化 USART1 并等待 200 ms：收到一帧 CRC 正确的命令后停留在命令模式，否则仅在提交标记、元数据、初始栈、复位入口和整镜像 CRC32 全部有效时清理外设状态并跳转到 Application；任一条件失败都会永久停留在命令模式，从而保留串口重刷入口。Application 的 `ENTER_BOOTLOADER` 命令会在响应完成后执行软件复位，上位机需要在新的 200 ms 窗口内发送合法命令。

元数据采用以下固定布局，多字节字段均为小端：

| 偏移 | 长度 | 字段 | 说明 |
| --- | --- | --- | --- |
| `0x00` | 4 | magic | 固定为 `0x414D554C` |
| `0x04` | 2 | format version | 当前为 `1` |
| `0x06` | 2 | header size | 固定为 `32` |
| `0x08` | 4 | image size | 从 `0x08002000` 开始参与校验的字节数 |
| `0x0C` | 4 | image CRC32 | IEEE CRC32，初值和最终异或值均为 `0xFFFFFFFF` |
| `0x10` | 4 | firmware version | 由上位机定义并写入的版本值 |
| `0x14` | 4 | reserved | 固定为 `0` |
| `0x18` | 8 | commit marker | 固定为 `0x4C554D4141505031`，升级最后一步写入 |

升级开始时先擦除包含提交标记的最后一页，再擦除其余 Application 页，因此擦除一旦开始，旧镜像立即失效。升级数据必须从偏移 0 开始连续发送；Bootloader 把前 8 字节向量表暂存在 RAM，只把后续镜像数据写入 Flash。全部数据收完后先用 RAM 中的向量表和 Flash 中的镜像正文计算 CRC32，通过后依次写入元数据前 24 字节、向量表，并再次从 Flash 校验 CRC32，最后才写入 8 字节提交标记。升级不完整、校验失败或任意阶段掉电时，提交标记不会等于完整固定值，复位后不会跳转到不完整的 Application。

启用此校验后，只有向量表而没有上述元数据的旧 Application 会被视为无效；通过调试器直接下载 Application 时，也必须另外生成并写入匹配的 32 字节元数据。通过升级协议下载时由 Bootloader 自动生成元数据。

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
| `0x11` | `BOOT_APPLICATION` | 是 | 否 | Application 完整校验有效时响应并跳转 |
| `0x20` | `BEGIN_UPDATE` | 是 | 否 | 校验升级参数并擦除 Application 区域 |
| `0x21` | `WRITE_CHUNK` | 是 | 否 | 按顺序写入一块镜像数据 |
| `0x22` | `END_UPDATE` | 是 | 否 | 校验镜像并完成原子提交 |
| `0x23` | `GET_UPDATE_STATUS` | 是 | 否 | 查询升级状态和下一个写入偏移 |

`GET_INFO` 的 12 字节 payload 依次为：协议版本、镜像类型、固件主/次/补丁版本、保留字节、最大 payload（2 字节）、能力位（4 字节）。镜像类型 `1` 表示 Bootloader，`2` 表示 Application。

升级命令 payload 和响应如下：

| 命令 | 请求 payload | 成功响应 payload |
| --- | --- | --- |
| `BEGIN_UPDATE` | `image_size:u32`、`image_crc32:u32`、`firmware_version:u32` | `max_chunk:u16`（当前为 120）、`reserved:u16`、`next_offset:u32` |
| `WRITE_CHUNK` | `offset:u32` 加 1～120 字节镜像数据 | `next_offset:u32` |
| `END_UPDATE` | 无 | 无 |
| `GET_UPDATE_STATUS` | 无 | `state:u8`、`last_result:u8`、`reserved:u16`、`next_offset:u32`、`image_size:u32` |

升级状态 `0`～`3` 依次表示 idle、receiving、complete、failed；`last_result` 的 `0`～`4` 依次表示成功、状态错误、参数错误、Flash 错误和校验错误。每个 `WRITE_CHUNK` 的偏移必须等于设备返回的 `next_offset`；第一块必须至少包含完整向量表，除最后一块外，块长度必须是 8 的倍数。串口升级使用停等方式：发送一条命令并收到响应后才能发送下一条，尤其不能在 Flash 擦写期间连续灌入多帧。Bootloader 的 `GET_INFO` 能力位 bit 2 表示支持 Application 升级。

响应状态码新增 `6`（参数错误）、`7`（Flash 操作错误）和 `8`（镜像校验错误）。Flash 操作错误或校验错误会将升级状态置为 failed，需要重新发送 `BEGIN_UPDATE` 从擦除开始；参数或顺序错误可根据 `GET_UPDATE_STATUS` 返回的 `next_offset` 修正后继续。

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
