# 串口通信与 OTA 工具

`lumaflow.ps1` 使用 PowerShell 和 .NET `System.IO.Ports.SerialPort`，无需第三方模块。
面向 Windows，可用 Windows PowerShell 5.1 或 PowerShell 7 执行；以下命令在 `firmware/` 目录运行。
默认串口配置为固件 USART1 的 `1000000 8N1`、无流控；USB 串口适配器需要支持 1 Mbps。

```powershell
pwsh -NoProfile -File scripts/lumaflow.ps1 -Help
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command ports
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command info -Port COM3
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command ping -Port COM3 -Text hello
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command enter-bootloader -Port COM3
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command status -Port COM3
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command boot-application -Port COM3
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command imu -Port COM3
```

使用 Windows PowerShell 时将 `pwsh` 换成 `powershell`。
`-Help`（或 `-h`）显示全部命令、参数和示例，无需指定串口，也不会打开串口或读取镜像。
`-TimeoutMs` 默认 5000（5 秒），范围为 1～60000；`-BaudRate` 默认 1000000，可用于匹配旧版或自定义固件。
`ping` 以 UTF-8 编码 `-Text`，最多 32 字节，并核对回显。
`info` 显示当前镜像类型、代码中的版本、最大 payload 和能力位；`status` 仅 Bootloader 支持。
`enter-bootloader` 会判断当前镜像，在 Application 下发复位命令后立即重新握手，直到 Bootloader 响应。
`boot-application` 在 Bootloader 中启动有效镜像，并等待 Application 响应；已经处于 Application 时直接成功。

每条请求都检查 COBS、CRC16、响应标志、序号、命令及设备状态码。
损坏、超长或无关帧被丢弃，可通过 `-Verbose` 查看诊断；操作失败返回非零退出码。
串口在退出或异常时释放。请关闭占用同一串口的终端或其他工具。
指定串口不存在或被占用时，工具会每隔 100 ms 重试打开，直到串口可用；等待没有时间上限，
可用 Ctrl+C 取消。无效参数等非串口可用性错误会直接报错，可用 `-Verbose` 查看每次打开失败的原因。
打开串口后，PING、设备信息、IMU 和升级状态查询在没有有效响应时，每隔约 100 ms 重发同一请求；
5 秒是每条命令所有重试共用的总超时，不会在每次重试时重新计时。收到设备错误码时立即报错；
成功响应即使 payload 为空也算收到响应。损坏或无关帧不会终止重试。
复位和启动命令采用设备信息探测：只在设备仍处于原镜像、且未收到切换确认时重发；
收到确认后仅继续探测，避免重复复位。每次镜像切换（包括探测和重发）共用 `-TimeoutMs` 的总超时。

## IMU 持续读取

烧录本次 Application 后，`lumaflow.ps1 -Command imu` 返回一次最新三轴数据和器件健康状态。
持续读取使用 `imu.ps1`，无需第三方模块：

```powershell
pwsh -NoProfile -File scripts/imu.ps1 -Port COM3
# 输出 1000 个不同读取序号后退出：
pwsh -NoProfile -File scripts/imu.ps1 -Port COM3 -Count 1000
# CSV 保存（从当前 PowerShell 调用）：
.\scripts\imu.ps1 -Port COM3 -Count 1000 | Set-Content -Encoding utf8 imu.csv
pwsh -NoProfile -File scripts/imu.ps1 -Help
```

固件由 TIM3 每 10ms 调度 I2C 读取，读取频率不依赖脚本。脚本每 10ms 请求最新值，
跳过重复序号，默认持续运行直到 Ctrl+C，并在退出或异常时释放串口。`-Count 0` 表示持续运行。
先检查当前镜像及 GET_IMU 能力，Bootloader 或旧 Application 会报错，不自动切换镜像。
支持 `-BaudRate`、`-TimeoutMs`、`-Verbose`、`-Help` / `-h`；串口等待行为与通用工具一致。

设备进入 Stop1 后关闭串口，持续读取会超时退出；摇晃唤醒设备后重新运行脚本。

CSV 列为 `sequence,timestamp_ms,x_g,y_g,z_g,new_data,read_errors,missed_ticks,host_skipped`。
XYZ 单位为 g，当前固件量程为 ±8g；脚本按设备上报的量程换算，也兼容旧 ±2g 固件。`new_data` 的 bit0/1/2 分别表示 X/Y/Z 自上次读取后更新。
序号对每次成功的设备读取递增，因此 NEWDATA=0 的记录仍可能出现。时间戳为设备读取开始时的毫秒数。
`read_errors` 是设备累计 I2C 读取失败数，`missed_ticks` 是设备主循环漏处理周期累计数，
`host_skipped` 是相邻输出之间未被脚本取到的成功读取数；这些列可以区分设备采集和主机取数的遗漏。
主机调度、串口往返与终端显示可能使实际输出少于每秒 100 行，工具不保证保存每个样本。

初始化或读取错误会立即报告；尚未得到首个有效样本、或读取序号一直不变时，最多等待 `-TimeoutMs`。
此时返回非零退出码，不把旧缓存当成持续更新的数据。传感器 ODR 配置与并行扫描需要实板验证，详见固件 README。

## OTA 升级

专用烧录入口为 `ota.ps1`。默认先配置并编译 Application Release，再调用现有的
`lumaflow.ps1 -Command ota` 烧录生成的 BIN，校验成功后启动 Application：

```powershell
powershell -NoProfile -File scripts/ota.ps1 -Port COM3
# 使用 Debug 配置，或升级后停留在 Bootloader：
powershell -NoProfile -File scripts/ota.ps1 -Port COM3 -Configuration Debug -NoBoot
# 烧录已有 BIN，跳过配置和编译：
powershell -NoProfile -File scripts/ota.ps1 -Port COM3 -Image build/Application/Release/LumaFlow.bin
powershell -NoProfile -File scripts/ota.ps1 -Help
```

自动构建以脚本所在的 `firmware/` 为根目录，与启动脚本时的当前目录无关；
输出为 `build/Application/<Release 或 Debug>/LumaFlow.bin`。
`-Image` 的相对路径按调用者当前目录解析；指定后跳过构建，`-Configuration` 不参与镜像选择。
CMake 优先使用 `.vscode/settings.json` 的 `cmake.cmakePath`，未设置时从 `PATH` 查找；
Arm 工具链和 Ninja 的查找仍由现有 CMake 工具链配置负责。
配置或编译失败会立即停止，不烧录旧 BIN；镜像验证和 OTA 错误通过非零退出码返回。
支持 `-BaudRate`（默认 1000000）、`-TimeoutMs`（默认 5000）、`-FirmwareVersion`（默认 0）、
`-NoBoot`、`-Verbose` 和 `-Help` / `-h`，串口等待与重试行为与通用工具一致。
使用 PowerShell 7 时可将 `powershell` 换为 `pwsh`。

工具实现固件已有的串口升级协议，只升级 Application，Bootloader 仍需用调试器安装。
输入为链接到 `0x08002000` 的原始 `.bin`，不包含 Bootloader 或末尾元数据。
不接受 `.elf`、Intel HEX 或完整 Flash 镜像。CMake 构建会自动生成供 OTA 使用的 BIN：

```powershell
cmake --preset Application
cmake --build --preset Application-Release --parallel
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command ota -Port COM3 -Image build/Application/Release/LumaFlow.bin -FirmwareVersion 1
# 升级后保持在 Bootloader，稍后手动 boot-application：
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command ota -Port COM3 -Image build/Application/Release/LumaFlow.bin -FirmwareVersion 1 -NoBoot
```

`-FirmwareVersion` 是写入升级元数据的 `uint32` 值，默认 0，由调用者定义；
它不会修改 Application 源码的主/次/补丁版本，因此 `info` 的版本显示不会随此参数改变。
`-Image`、`-FirmwareVersion` 和 `-NoBoot` 仅用于 `ota`。

工具在打开串口前验证文件大小和向量表。完整流程为：计算 IEEE CRC32 → 判断当前镜像，
必要时进入 Bootloader 并重新握手 → 检查升级能力和状态 → `BEGIN_UPDATE` →
按设备允许的大小连续发送 `WRITE_CHUNK` → `END_UPDATE` → 查询并确认 complete/OK →
默认请求启动 Application。最多每块 120 字节，非末块保持 8 字节对齐；元数据由 Bootloader 写入。
传输时显示进度。每条擦除、写入和最终校验命令同样使用 `-TimeoutMs`，默认等待最多 5 秒。

`BEGIN_UPDATE` 会擦除旧 Application，升级中断后旧镜像无法启动。
设备错误、超时、响应长度异常或偏移不一致都会立即停止。`BEGIN_UPDATE`、`WRITE_CHUNK`、
`END_UPDATE` 不会自动重发：当前固件没有请求去重机制，已成功执行但 ACK 丢失时，
重复擦除或写入会被状态机拒绝；Flash 忙时连续发送也可能溢出接收缓冲区。
超时表示结果未知，可先用 `status` 查询：若 complete/OK，可手动 `boot-application`；
若 receiving，需要手动复位或重新上电后再次执行完整 OTA，因为当前固件拒绝在 receiving 状态开始新升级。
failed 状态可直接重新执行 OTA。工具不跨进程断点续传。

## 验证

PIC/FLIP 固件启动后默认运行液体效果。查询实际负载：

```powershell
powershell -NoProfile -File scripts/lumaflow.ps1 -Command fluid -Port COM9
```

`CompletedSteps` 是已完成模拟步数；`LastWorkUs` / `MaxWorkUs` 是各计算片段及渲染耗时之和，包含抢占中断；`MaxSliceUs` 是最长单片段。`LastStepUs` 包含段间 IMU/串口和故障暂停，属于墙钟耗时。`WorkBudgetPercent` 为 LastWorkUs 占目标周期的百分比，按设备返回的 `TargetHz` 计算（25Hz 为 40ms），不能直接当作纯 CPU 使用率。`MissedSteps` 表示因计算忙、显示背压、IMU 无效等未启动的模拟周期。`StateBytes` 是效果全部静态状态（含输出缓冲），`TargetHz` 仅表示目标频率；脚本兼容 25Hz、30Hz 和旧版 50Hz 固件。

先确认 IMU 健康，再连续查询：正常应看到步骤持续增加、MissedSteps 不持续增加。LastStepUs 超过 `1000000 / TargetHz` µs 说明该步未在一个目标周期内完成；同时用 `imu.ps1` 检查 missed_ticks。旧固件或 Bootloader 不支持 0x31，会明确返回 BAD_COMMAND。只读查询允许静默重试。

主机测试不需要硬件，也不会打开真实串口：

```powershell
pwsh -NoProfile -File scripts/tests/cli_test.ps1
pwsh -NoProfile -File scripts/tests/connection_test.ps1
pwsh -NoProfile -File scripts/tests/serial_test.ps1
pwsh -NoProfile -File scripts/tests/ota_test.ps1
pwsh -NoProfile -File scripts/tests/ota_flash_test.ps1
pwsh -NoProfile -File scripts/tests/imu_test.ps1
pwsh -NoProfile -File scripts/tests/fluid_test.ps1
```

OTA 测试包括协议测试、复位后丢失一次探测、完整/最大镜像、块大小协商、非对齐末块、
不启动模式、输入校验、复位/启动请求及 ACK 丢失、复位延迟、切换超时、错误偏移、Flash/CRC 错误、
错误响应和提交状态异常。协议测试验证静默重发、重试共用超时、设备错误立即失败以及擦写命令不重复发送。
CLI 测试验证 `-Help` / `-h` 的输出，以及帮助模式跳过串口和镜像操作。
连接测试模拟串口不存在、被占用后恢复，以及无效参数立即失败。
专用烧录入口测试在临时目录模拟 CMake 和 OTA 命令，验证默认 Release、Debug、已有 BIN、
带空格路径、参数传递、配置/编译失败停止、输出缺失及 OTA 错误码传递；不会打开真实串口。
模拟测试不能证明 USB 串口驱动的 1 Mbps 支持、真实复位窗口或 Flash 擦写；这些需要开发板实测。
IMU 测试覆盖协议长度与字段校验、14 位符号/量程换算、连续循环、序号去重与回绕、漏取统计、
器件错误、无新读取超时、只读请求重试，以及帮助模式跳过串口操作。
