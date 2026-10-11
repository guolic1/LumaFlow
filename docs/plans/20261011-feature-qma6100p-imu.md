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

- [x] P1-T1：增加 QMA6100P 驱动、TIM3/I2C 适配、主循环采集和错误状态；接入初始化与停止流程。
  - Source: 设计“实现”“验证与风险”；AC-001、AC-002。
  - Scope: `firmware/application/`、`firmware/tests/`、固件 README。
  - Depends on: none。
  - Verification: 先失败后通过的主机驱动/采集测试，Application Debug/Release 构建。

### Phase 2：串口持续取数

- [x] P2-T1：增加 GET_IMU、能力位、解码与持续 CSV 脚本，更新协议和工具说明。
  - Source: 设计“串口契约”“验证与风险”；AC-003、AC-004。
  - Scope: Application 命令表、公共命令常量、PowerShell 脚本及对应测试、README。
  - Depends on: P1-T1。
  - Verification: 真实命令表的协议测试、模拟串口持续取数测试、现有脚本回归和 Bootloader 构建。

## Completion Criteria

- [x] 两个切片测试与构建通过。
- [x] 改动仅包含当前需求，提交按独立功能拆分。
- [x] 明确报告实板读取、ODR 与并行扫描尚未测量。

## 验证记录

- 驱动与采集 RED：新增主机测试在缺少相应实现时配置失败；GREEN：实现后 CTest 5/5 通过。
- 串口 RED：真实 Application 命令表尚不支持 0x30，响应长度断言失败；GREEN：命令接入后通过。
- 脚本 RED：GET_IMU 解码函数、持续读取脚本尚不存在，测试失败；GREEN：实现后通过。
- `cmake --preset Application`、`cmake --preset Bootloader`：配置成功。
- `cmake --build --preset Application-Debug --parallel`、`Application-Release`：通过；最终 RAM 2928B，Flash 分别 9632B / 7840B。
- `cmake --build --preset Bootloader-Debug --parallel`、`Bootloader-Release`：通过。
- WSL 中 `cmake -S firmware/tests -B firmware/build/ProtocolTests -G Ninja`、`cmake --build firmware/build/ProtocolTests`、`ctest --test-dir firmware/build/ProtocolTests --output-on-failure`：5/5 通过（实际执行采用工作树绝对路径）。
- PowerShell 7：`scripts/tests/cli_test.ps1`、`connection_test.ps1`、`ota_test.ps1`、`ota_flash_test.ps1`、`imu_test.ps1` 均通过；connection、ota、imu 测试同时执行 `serial_test.ps1`。
- Windows PowerShell 5.1：`powershell -NoProfile -File firmware/scripts/tests/imu_test.ps1` 通过。
- `clang-format --dry-run --Werror` 对所有新增和修改的 C/H 文件通过；`git diff --check` 通过。
- 未烧录开发板；真实总线恢复、实际 ODR、三轴数据、定时抖动及 Ctrl+C 时真实串口释放仍需板上验收。
