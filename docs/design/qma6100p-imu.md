# QMA6100P 100Hz 采集设计

## 需求与验收

来源：用户要求编写 QMA6100P 驱动，以 100Hz 读取三轴加速度，使用定时器而非 IMU 外部中断，增加串口指令和持续读取脚本。

- AC-001：初始化器件为 ±2g、名义 ODR 100Hz；每个 10ms 定时周期最多读取一次 6 字节三轴数据。
- AC-002：保留 LED 的 TIM14 扫描和已有串口命令；I2C 失败有超时，故障不使主循环永久阻塞。
- AC-003：新增只读命令返回带序号、时间戳、NEWDATA 和错误状态的最新数据；没有有效数据时不能伪造成功样本。
- AC-004：PowerShell 脚本复用现有 COBS/CRC16 协议，不断读取并输出数据，Ctrl+C 退出且释放串口。

本次范围为三轴采集与取数；不增加 FIFO、校准、计步、IMU 外部中断或灰度功能。硬件可用性通过板上验收确认，主机测试仅证明软件契约。

## 仓库证据

| 位置 | 约束 |
|---|---|
| `firmware/application/led_matrix.c` | TIM14 已占用，优先级 0 |
| `firmware/common/platform/serial_uart.c` | USART1 使用 DMA，优先级 1 |
| `firmware/board/Core/Src/i2c.c` | PB6/PB7 的 I2C1 标准模式配置，复用，不修改生成文件 |
| 原理图网表 U5 | AD0 接 GND，7 位地址 0x12，SENB 接 3.3V |
| `firmware/application/commands.c`、`scripts/lumaflow.ps1` | 显式命令表、请求响应协议、PowerShell 标准库 |

## 实现

`qma6100p.c/.h` 负责寄存器初始化和 14 位符号解码，通过少量总线回调连接 STM32 和主机测试。软复位后写回 0，配置字段采用读改写并回读校验；保留未定义位。量程 ±2g，4096 LSB/g，启用单轴 shadow。所给手册缺少时钟编码，采用外部资源中核对的 MCLK=4、BW[4:0]=0。仅使用手册定义的寄存器，不采用其他版本的隐藏模拟寄存器初始化序列。

`imu_stm32.c` 复用 I2C1，使用重复 START 连读，NACK、总线错误或整笔事务超过 3ms 均失败并清理外设状态。TIM3 以 1MHz 计数（PSC=63、ARR=9999），100Hz 更新中断只递增周期计数，优先级 2；定时器也提供超时和时间戳。原子读取时间基准时仅短暂屏蔽中断。

`imu.c` 在主循环观察定时周期，读取并缓存最新完整结果。周期积压时合并为一次当前读取，统计漏处理周期，避免补读旧寄存器伪装成历史采样。失败保留上次样本但更新错误状态和计数；初始化失败仍允许串口和 LED 工作。进入 Bootloader 前停 TIM3。

### 串口契约

新增 `GET_IMU`（0x30），请求 payload 为空，仅 Application 支持；能力位 bit3 表示支持此命令。响应 payload 为 28 字节，所有多字节数均为小端：

| 偏移 | 格式 | 含义 |
|---|---|---|
| 0 | uint32 | 成功读取序号，每次成功 I2C 读取加 1，包括未更新数据 |
| 4 | uint32 | 读取开始时的毫秒时间戳，模 2^32 |
| 8/10/12 | int16 | X/Y/Z 原始值，-8192..8191，除以 4096 得 g |
| 14 | uint8 | NEWDATA 掩码，bit0/1/2 为 X/Y/Z |
| 15 | uint8 | bit0：曾取得有效样本；bit1：器件初始化完成 |
| 16 | uint32 | I2C 读取失败次数 |
| 20 | uint32 | 漏处理的定时周期数 |
| 24 | uint8 | 状态：0 OK、1 BUS_ERROR、2 CHIP_ID_ERROR、3 CONFIG_ERROR、4 NOT_READY、5 STOPPED |
| 25 | uint8 | 量程 g，固定 2 |
| 26 | uint16 | MCU 名义读取频率，固定 100Hz |

有效命令请求的协议状态为 OK，器件健康状态由 payload 表达。空样本以 valid=0 表达，不把全 0 当作有效数据。已有命令、Bootloader 能力和协议版本保持兼容。新命令有严格长度限制。

`lumaflow.ps1 -Command imu` 提供一次读取；`imu.ps1` 默认按 10ms 主机周期重复读取，输出 CSV（序号、毫秒、XYZ g、NEWDATA、漏取/错误统计），跳过重复序号。`-Count` 可限制输出样本数。读取错误立即报告并返回非零退出码；NOT_READY 只在有限启动等待内重试。不主动切换镜像。

## 验证与风险

先用主机测试证明寄存器保留位、回读错误、14 位边界、读取失败不污染样本，再测试采集周期合并、状态和串口序列化。PowerShell 模拟串口测试覆盖长度、符号换算、重复/漏取、器件错误和持续循环。运行现有 CTest、脚本测试和 Application Debug/Release、Bootloader 构建。

MCU 和传感器时钟独立，100Hz 定时读取不能保证每次都是新转换；NEWDATA 如实上报。shadow 只保证单轴高低字节一致，不保证 XYZ 同时刻。主机脚本读取最新值，不保证保存每个样本，序号跳变反映漏取。实板需确认时钟编码与芯片版本匹配、100Hz 周期、I2C 波形、故障恢复和 LED 并行时隙抖动。

## Implementation Approach

采用 foundation-first：先实现并验证器件驱动和采集，再接入命令与脚本。每个切片包含对应测试和使用文档；不增加依赖。

## External Resources Used

| Project Resource Label | Feature Identifier | Purpose |
|---|---|---|
| qma6100p-register-notes | 0x00、0x01..06、0x0F..11、0x21、0x36 | 初始化、地址和数据格式 |
| qma6100p-riot-clock | PM_MCLK_51K2、ODR_100HZ | 补充所给手册缺失的时钟编码 |
