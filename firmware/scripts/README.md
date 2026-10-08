# 串口通信与 OTA 工具

`lumaflow.ps1` 使用 PowerShell 和 .NET `System.IO.Ports.SerialPort`，无需第三方模块。
面向 Windows，可用 Windows PowerShell 5.1 或 PowerShell 7 执行；以下命令在 `firmware/` 目录运行。
默认串口配置为固件 USART1 的 `2000000 8N1`、无流控；USB 串口适配器需要支持 2 Mbps。

```powershell
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command ports
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command info -Port COM3
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command ping -Port COM3 -Text hello
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command enter-bootloader -Port COM3
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command status -Port COM3
pwsh -NoProfile -File scripts/lumaflow.ps1 -Command boot-application -Port COM3
```

使用 Windows PowerShell 时将 `pwsh` 换成 `powershell`。
`-TimeoutMs` 默认 2000，范围为 1～60000；`-BaudRate` 默认 2000000，仅用于匹配自定义固件。
`ping` 以 UTF-8 编码 `-Text`，最多 32 字节，并核对回显。
`info` 显示当前镜像类型、代码中的版本、最大 payload 和能力位；`status` 仅 Bootloader 支持。
`enter-bootloader` 会判断当前镜像，在 Application 下发复位命令后立即使用短超时重新握手。
`boot-application` 仅在 Bootloader 中且镜像有效时成功。

每条请求都检查 COBS、CRC16、响应标志、序号、命令及设备状态码。
损坏、超长或无关帧被丢弃，可通过 `-Verbose` 查看诊断；操作失败返回非零退出码。
串口在退出或异常时释放。请关闭占用同一串口的终端或其他工具。

## OTA 升级

工具实现固件已有的串口升级协议，只升级 Application，Bootloader 仍需用调试器安装。
输入为链接到 `0x08002000` 的原始 `.bin`，不包含 Bootloader 或末尾元数据。
不接受 `.elf`、Intel HEX 或完整 Flash 镜像。若构建结果只有 ELF，可用已有 Arm GNU Toolchain 转换：

```powershell
arm-none-eabi-objcopy -O binary build/Application/Release/LumaFlow.elf build/Application/Release/LumaFlow.bin
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
传输时显示进度。擦除和最终校验的响应超时至少为 10000 ms，其余操作使用 `-TimeoutMs`。

`BEGIN_UPDATE` 会擦除旧 Application，升级中断后旧镜像无法启动。
设备错误、超时、响应长度异常或偏移不一致都会立即停止；擦写命令不会自动重发。
超时表示结果未知，可先用 `status` 查询：若 complete/OK，可手动 `boot-application`；
若 receiving，需要手动复位或重新上电后再次执行完整 OTA，因为当前固件拒绝在 receiving 状态开始新升级。
failed 状态可直接重新执行 OTA。工具不跨进程断点续传。

## 验证

主机测试不需要硬件，也不会打开真实串口：

```powershell
pwsh -NoProfile -File scripts/tests/serial_test.ps1
pwsh -NoProfile -File scripts/tests/ota_test.ps1
```

OTA 测试包括协议测试、复位后丢失一次探测、完整/最大镜像、块大小协商、非对齐末块、
不启动模式、输入校验、丢失 ACK、错误偏移、Flash/CRC 错误、错误响应和提交状态异常。
模拟测试不能证明 USB 串口驱动的 2 Mbps 支持、真实复位窗口或 Flash 擦写；这些需要开发板实测。
