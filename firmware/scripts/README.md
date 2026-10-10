# 串口通信与 OTA 工具

`lumaflow.ps1` 使用 PowerShell 和 .NET `System.IO.Ports.SerialPort`，无需第三方模块。
面向 Windows，可用 Windows PowerShell 5.1 或 PowerShell 7 执行；以下命令在 `firmware/` 目录运行。
默认串口配置为固件 USART1 的 `1000000 8N1`、无流控；USB 串口适配器需要支持 1 Mbps。

```powershell
pwsh -NoProfile -File scripts/lumaflow.ps1 -Help
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command ports
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command info -Port COM3
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command ping -Port COM3 -Text hello
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command pwm -Port COM3 -Duty 500
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command enter-bootloader -Port COM3
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command status -Port COM3
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command boot-application -Port COM3
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
打开串口后，PING、设备信息、升级状态查询和 PWM 设置在没有有效响应时，每隔约 100 ms 重发同一请求；
5 秒是每条命令所有重试共用的总超时，不会在每次重试时重新计时。收到设备错误码时立即报错；
成功响应即使 payload 为空也算收到响应。损坏或无关帧不会终止重试。
复位和启动命令采用设备信息探测：只在设备仍处于原镜像、且未收到切换确认时重发；
收到确认后仅继续探测，避免重复复位。每次镜像切换（包括探测和重发）共用 `-TimeoutMs` 的总超时。

## 单灯 PWM 亮度测试

需要运行 Application `0.2.0` 或支持能力位 bit 3 的固件。`pwm` 设置 D1 的占空比，
频率固定为 1 kHz；必须显式提供整数 `-Duty`，范围 `0–1000`，每单位代表 `0.1%`。

```powershell
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command pwm -Port COM3 -Duty 1     # 0.1%
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command pwm -Port COM3 -Duty 100   # 10.0%
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command pwm -Port COM3 -Duty 500   # 50.0%
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command pwm -Port COM3 -Duty 1000  # 100.0%
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command pwm -Port COM3 -Duty 0     # 关闭
```

成功时返回 `Duty`、`DutyPercent`、`FrequencyHz`。设置在下一个 PWM 周期边界生效，
脚本退出后保持最后设置值；复位后恢复 0，不写入 Flash。
该命令沿用 COBS/CRC16 二进制协议，不是向串口发送 ASCII 文本 `500`；脚本负责编码和验证回显。
设置绝对占空比可安全重发，沿用默认 5 秒总超时；设备错误码立即终止。
Bootloader 或旧 Application 会返回 `BAD_COMMAND`，需先升级并启动新版 Application。
`-Duty` 只适用于 `pwm`，缺少参数或超出范围时在打开串口前报错。

D1 阳极为 PA3，阴极为 PA2。测试前确认 LED/GPIO 限流条件；低占空比不限制峰值电流。
完整引脚、定时器配置和示波器验证方法见 [固件 README](../README.md#d1-单灯-pwm-测试)。

## OTA 升级

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

主机测试不需要硬件，也不会打开真实串口：

```powershell
pwsh -NoProfile -File scripts/tests/cli_test.ps1
pwsh -NoProfile -File scripts/tests/connection_test.ps1
pwsh -NoProfile -File scripts/tests/pwm_test.ps1
pwsh -NoProfile -File scripts/tests/serial_test.ps1
pwsh -NoProfile -File scripts/tests/ota_test.ps1
```

OTA 测试包括协议测试、复位后丢失一次探测、完整/最大镜像、块大小协商、非对齐末块、
不启动模式、输入校验、复位/启动请求及 ACK 丢失、复位延迟、切换超时、错误偏移、Flash/CRC 错误、
错误响应和提交状态异常。协议测试验证静默重发、重试共用超时、设备错误立即失败以及擦写命令不重复发送。
CLI 测试验证 `-Help` / `-h` 的输出，以及帮助模式跳过串口和镜像操作。
连接测试模拟串口不存在、被占用后恢复，以及无效参数立即失败。
PWM 测试验证 0/1000 端点、0.1% 分辨率、小端编码、参数校验、响应回显和静默重发。
模拟测试不能证明 USB 串口驱动的 1 Mbps 支持、真实复位窗口或 Flash 擦写；这些需要开发板实测。
