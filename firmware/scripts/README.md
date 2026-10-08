# 串口工具

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

主机测试不需要硬件，也不会打开真实串口：

```powershell
pwsh -NoProfile -File scripts/tests/serial_test.ps1
```
