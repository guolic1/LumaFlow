# PIC/FLIP 六边形液体显示设计

## 需求与验收

来源：用户要求试用 PIC/FLIP，并了解计算负载；已确认 LED 正六边形、位号从右到左及从下到上增加，IMU X 向右、Y 向下。范围为默认运行的无灰度液体效果和只读性能查询，不改变已有扫描、IMU 或 OTA 契约，不自动烧录。

- AC-001：连续粒子通过 MAC 网格完成速度转移、压力投影和 PIC/FLIP 混合，受 IMU 板面加速度驱动，粒子不越过六边形边界。
- AC-002：输出 217 个开关像素，符合用户确认的位号方向；保留 100Hz LED 刷新与 100Hz IMU 读取。
- AC-003：无动态分配、无浮点；固定规模和迭代上限，计算分段允许主循环处理 IMU/串口；传感器无效或过期时冻结画面。
- AC-004：通过只读串口查询实际计算耗时和漏处理模拟周期，明确区分板上测量与主机测试。

## 仓库依据

| 位置 | 约束 |
|---|---|
| `application/led_matrix.h` | `bool[217]` 提交，帧边界切换；未消费画面产生背压 |
| `application/imu.c`、`imu_stm32.c` | TIM3 10ms 周期，主循环 I2C；时钟可以复用作测量 |
| `application/application.ld`、工具链 | 8KB RAM、Cortex-M0+、预留 1KB 栈 |
| `application/commands.c`、`scripts/lumaflow.ps1` | 显式命令表与 PowerShell 请求响应客户端 |

## 实现

使用 13×13 单元的笛卡尔 MAC 网格、128 个等质量粒子、12 次压力迭代、90% FLIP/10% PIC，固定模拟步长 20ms。连续坐标和速度为 Q10；速度单位为单步位移，限速 0.5 单元/步。重力输入使用 `(-raw_x, -raw_y)`，保留大小并缩放为视觉效果加速度；不是按毫米标定的物理模型。仅低通板面分量，不归一化。初始粒子在容器下部规则铺放。

流程：积分和六边形碰壁 → 两次粒子分离 → 双线性 P2G 加权累加与归一化 → 保存投影前网格速度 → 压力投影（含局部过密的漂移补偿）→ 双线性 G2P，混合 PIC 速度和 FLIP 速度增量。压力使用原地速度投影，不存储压力数组。粒子用六个平面限制；压力网格以覆盖六边形的单元掩码近似斜边，不声称精确切割单元求解。

网格累加器与当前/旧速度复用 union；权重、单元类型/密度、渲染密度复用工作区，仅在相应阶段访问。模拟状态预算 3.5KB，另有 217 字节输出和少量调度统计。最终以 ARM 链接结果确认 RAM，并检查栈用量。粒子分离按 8 个粒子、转移按 16 个粒子、投影按一次迭代分段，不关中断。性能不能从桌面运行时间换算为 MCU 时间。

`fluid_effect` 在主循环使用 TIM3 调度，正常每两个 tick 启动一步。计算未完或显示背压时不累积补算，记录漏周期；等待显示期间保留待提交帧。IMU 状态非 OK、未就绪、未有效或样本超过 100ms 时暂停，不使用故障缓存驱动新运动。进入 Bootloader 仍走已有停止/复位流程。

LED 坐标按每行 9→17→9 生成，D1 位于下行右端，D109 位于中心，D217 位于上行左端。显示密度通过粒子到附近 LED 的核权重累加取阈值，不将粒子限制在 LED 中心；灰度不在本次范围内。

### 性能查询

新增 Application `GET_FLUID` (0x31)，空请求，能力 bit4；32 字节小端响应：0/4/8/12/16/20 为 uint32 的 completed_steps / last_work_us / max_work_us / last_step_us / missed_steps / max_slice_us；24 为 uint16 粒子数；26/27 为 uint8 网格宽高；28/30 为 uint16 的 state_bytes / rate_hz。work 为各计算片段和渲染的耗时之和（含抢占中断），step 为跨主循环的整步墙钟时间，包含传感器故障暂停。missed_steps 包含忙、背压、无效 IMU 等原因导致未启动的周期。查询不推进模拟。`lumaflow.ps1 -Command fluid` 解码输出，保留其他命令行为。

## Implementation Approach

foundation-first：先证明核心投影、边界、映射、持续稳定性，再接入 IMU/LED 调度和端到端只读查询。所有选择均为可逆的局部实现，不增加依赖或持久化数据，因此无需 ADR。

## 验证

- L1：CTest 验证投影降低散度、双向重力响应、所有粒子长期处于边界内、恒定粒子数、位号方向、分段完成和 RAM 预算；数值路径使用 ASan/UBSan 检查。
- L2：真实模拟核心配合假 IMU/时钟/LED，验证 50Hz 调度、背压、故障/过期冻结、恢复、计时回绕及漏周期统计；协议测试验证 0x31 字节布局、长度与能力位。
- PowerShell 模拟串口验证性能解码、无效响应、只读重试和 CLI 帮助；运行已有协议与 IMU 测试。
- Application Debug/Release 构建和栈用量检查；硬件未测时只报告规模和内存，不声称达到 50Hz。板上通过 fluid 查询耗时，并观察 IMU missed_ticks 与六边形倾斜效果。

## 资料

[Matthias Müller：How to write a FLIP Water Simulator](https://matthias-research.github.io/pages/tenMinutePhysics/18-flip.pdf)，用于算法事实；独立实现，不复制演示代码。

## 软件验证记录

主机 CTest 7/7 与 GCC `-O2` ASan/UBSan 7/7 通过；核心覆盖双向 X/Y 重力、投影降低散度、长期变向、重叠粒子分离和位号渲染。调度测试覆盖周期、背压、故障、过期、恢复和 tick/微秒回绕。PowerShell 5.1 的 7 项脚本测试，以及 PowerShell 7 的 fluid/imu 测试通过。

ARM 最终构建：Application Debug RAM 6592B、Flash 14864B；Release RAM 6584B、Flash 12292B，RAM 数字包含 1024B 栈预留。Release `effect` 符号占 3656B。主机核心结构大小 3388B；不同 ABI 可能存在枚举对齐差异，以固件查询为准。新增核心 Debug 单函数栈最大 88B，无递归；Release LTO 内联后的 main 栈帧为 184B，仍需结合中断嵌套核对，不能将单函数帧当作整个调用链的峰值。实际 C 核心导出 300 帧预览，观察了上下左右及斜向聚集；桌面结果不证明 MCU 达到 50Hz，尚未烧录或测量硬件耗时。
