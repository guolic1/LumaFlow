# LumaFlow 固件

目标芯片为 STM32G031G8（64 KB Flash、8 KB RAM），使用 CMake、Ninja 和 Arm GNU Toolchain。

## 工程结构

| 目录 | 内容 |
| --- | --- |
| `application/` | LED 扫描、QMA6100P 采集、PIC/FLIP 液体效果 |
| `bootloader/` | 应用校验、启动与串口升级 |
| `common/` | 串口协议及平台适配 |
| `board/` | CubeMX 工程、生成代码及厂商驱动 |
| `scripts/` | PowerShell 串口工具与 OTA 脚本 |
| `tests/` | 主机测试 |

## 开发与构建

用 VS Code 打开 `LumaFlow.code-workspace`，安装工作区推荐扩展。
将 `.vscode/settings.example.json` 复制为 `.vscode/settings.json`，填写 CMake、Ninja、Arm 工具链及 OpenOCD 路径。本地设置不提交 Git；命令行构建时，CMake 需要在 PATH 中。

以下命令在 `firmware/` 目录运行：

```powershell
cmake --preset Application
cmake --build --preset Application-Release --parallel

cmake --preset Bootloader
cmake --build --preset Bootloader-Release --parallel
```

需要调试时将 `Release` 换为 `Debug`。产物位于 `build/<Application 或 Bootloader>/<Debug 或 Release>/`，包含 ELF、HEX、BIN 和 map。

VS Code 中选择 Configure Preset（镜像）和 Build Preset（Debug/Release），即可使用状态栏的“编译”“下载”“调试”按钮。手写 C/C++ 使用项目 `.clang-format`，`board/` 保留生成代码的格式。

## 下载与升级

Bootloader 首次安装使用 OpenOCD；Application 可通过串口 OTA 升级：

```powershell
.\scripts\ota.ps1 -Port COM9
```

默认构建 Application Release、升级并启动。烧录已有镜像：

```powershell
.\scripts\ota.ps1 -Port COM9 -Image build/Application/Release/LumaFlow.bin
```

OTA 仅接受 Application 原始 BIN。Bootloader 自动写入校验元数据；仅用调试器下载 Application ELF 不足以使其通过启动校验。

| Flash 区域 | 地址范围 |
| --- | --- |
| Bootloader | `0x08000000`～`0x08001FFF` |
| Application | `0x08002000`～`0x0800FFDF` |
| 应用元数据 | `0x0800FFE0`～`0x0800FFFF` |

复位后，有效应用会在 3 秒等待窗口后启动；窗口内收到有效串口命令则停留在 Bootloader。应用无效时保留串口升级入口。

## 运行功能

- LED：217 颗查理复用 LED，每帧 161 个时隙，TIM14 刷新约 120Hz，双缓冲，无灰度。
- IMU：QMA6100P，I2C 地址 `0x12`，±8g（1024 LSB/g）；TIM3 每 10ms 调度主循环读取，不使用 IMU 外部中断。
- 液体：默认运行 64 粒子、13×13 网格的定点 PIC/FLIP，目标 25Hz；IMU 无效或数据过期时暂停运动。实际计算耗时通过 `fluid` 命令查询。
- 坐标：LED 位号从右向左、从下向上增加；IMU X 向右、Y 向下。
- 休眠：各轴相对参考值的加速度变化均小于 0.1g 持续 30 秒后关闭 LED、暂停液体，MCU 进入普通 Sleep；保留 100Hz IMU 采样和串口，任一轴变化达到 0.5g 时恢复。阈值与超时在 `application/motion_sleep_config.h` 调节。

液体效果参数集中在 `application/fluid_config.h`，修改后重新构建 Application。

矩阵使用 PF2，需预先配置 Option Bytes 的 `NRST_MODE=2`，并断开 R5 上拉；固件不自动修改 Option Bytes。Application 使用 PA13/PA14 扫描 LED，运行时不可使用 SWD；Bootloader 保留这两个引脚的调试功能。

## 串口使用

串口配置为 `1000000 8N1`。查询设备、持续读取 IMU 和查询液体负载：

```powershell
.\scripts\lumaflow.ps1 -Command info -Port COM9
.\scripts\imu.ps1 -Port COM9
.\scripts\lumaflow.ps1 -Command fluid -Port COM9
```

IMU 输出 CSV，XYZ 单位为 g，Ctrl+C 停止。工具参数、字段说明和升级恢复步骤见 [scripts/README.md](scripts/README.md)。

## 主机测试

```powershell
cmake -S tests -B build/ProtocolTests -G Ninja
cmake --build build/ProtocolTests
ctest --test-dir build/ProtocolTests --output-on-failure
```

PowerShell 工具测试命令见 [scripts/README.md](scripts/README.md#验证)。
