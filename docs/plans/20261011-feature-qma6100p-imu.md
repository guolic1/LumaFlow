# QMA6100P 采集工作计划

Created Date: 2026-10-11
Type: feature
Review Scope: Application IMU、串口只读命令和 PowerShell 取数工具

## WorkPlan Review

- **Status**: pending
- 用户已授权本次实现范围；没有额外审批门槛。

## Governing Documents

- Design Doc: `docs/design/qma6100p-imu.md`

## Implementation Phases

### Phase 1：驱动与 100Hz 采集

- [ ] P1-T1：增加 QMA6100P 驱动、TIM3/I2C 适配、主循环采集和错误状态；接入初始化与停止流程。
  - Source: 设计“实现”“验证与风险”；AC-001、AC-002。
  - Scope: `firmware/application/`、`firmware/tests/`、固件 README。
  - Depends on: none。
  - Verification: 先失败后通过的主机驱动/采集测试，Application Debug/Release 构建。

### Phase 2：串口持续取数

- [ ] P2-T1：增加 GET_IMU、能力位、解码与持续 CSV 脚本，更新协议和工具说明。
  - Source: 设计“串口契约”“验证与风险”；AC-003、AC-004。
  - Scope: Application 命令表、公共命令常量、PowerShell 脚本及对应测试、README。
  - Depends on: P1-T1。
  - Verification: 真实命令表的协议测试、模拟串口持续取数测试、现有脚本回归和 Bootloader 构建。

## Completion Criteria

- [ ] 两个切片测试与构建通过。
- [ ] 改动仅包含当前需求，提交按独立功能拆分。
- [ ] 明确报告实板读取、ODR 与并行扫描尚未测量。
