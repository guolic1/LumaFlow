# PIC/FLIP 实现工作计划

Created Date: 2026-10-11
Type: feature
Review Scope: 模拟核心、Application 接入、只读性能查询及对应测试

## WorkPlan Review

- Status: pending
- 用户已要求试用方案 4；在该授权内实现，不把计划评审状态作为额外审批门槛。

## Governing Documents

- `docs/design/pic-flip-fluid-design.md`

## Phase 1：模拟核心

- [x] P1-T1：固定规模的定点 PIC/FLIP、六边形碰壁、LED 映射和密度渲染；对应设计实现与 AC-001/002/003。
  - Scope: `fluid_sim.c/.h`、主机测试与测试 CMake。
  - Depends on: none。
  - Verification: 核心 RED/GREEN、CTest、ASan/UBSan、状态大小与持续重力测试。

## Phase 2：运行与性能查询

- [x] P2-T1：分段调度、IMU 故障冻结、显示背压及微秒测量；对应设计实现、AC-002/003/004。
  - Scope: `fluid_effect.c/.h`、main、IMU 平台计时、Application CMake 与集成测试。
  - Depends on: P1-T1。
  - Verification: 调度/故障/回绕主机测试，Application Debug/Release 和栈检查。
- [x] P2-T2：0x31 与 PowerShell 性能解码、使用文档；对应性能查询契约与 AC-004。
  - Scope: commands、公共 ID、lumaflow.ps1 与对应 C/PowerShell 测试、README。
  - Depends on: P2-T1。
  - Verification: 实际 COBS/CRC 协议查询、脚本模拟串口和现有脚本回归。

## Phase 3：依据板测修复误亮与负载

- [x] P3-T1：IO17 非高电平源时拉低；检查空帧、全亮、217 个单灯、帧切换和直接负载约束。
- [x] P3-T2：精确倒数及面邻接表、核心 -O2、Application Flash 预取；验证数学边界、1200 步差分及 ARM 指令数。
- [x] P3-T3：64 粒子及一次分离，缩放填充参数、兼容旧性能查询；检查实际 C 画面、CTest、ASan/UBSan、ARM 构建和 PowerShell 回归。
- [ ] 板上复测 LastStepUs、MissedSteps、IMU missed_ticks 及左上斜边微亮；未自动烧录。
- [ ] 照片已定位 IO16 阳极暗亮线及 R5 外部上拉供电路径；PF2 为 GPIO 时断开 R5 后实板复测，IO17 拉低不能处理此路径。

## Completion

- [x] 所有软件验证完成，报告实际命令、内存与未验证的硬件性能。
- [x] 按核心、运行接入两个可独立验证的功能边界提交。
