# 外部资料

## Additional Resources

| Label | Description | Access Method | Owner / Notes |
|---|---|---|---|
| qma6100p-register-notes | 用户提供的 Rev. A1 寄存器整理 | `C:/Users/58286/Downloads/QMA6100P_寄存器说明.md` | 本机资料，作为寄存器定义的主要依据 |
| qma6100p-riot-clock | RIOT 官方 QMA6100P 驱动时钟编码 | https://github.com/RIOT-OS/RIOT/blob/master/drivers/include/qma6100p.h | 2026-10-11 核对：MCLK=51.2kHz 编码 4，ODR=100Hz 编码 0；仅采用编码事实，不复制实现 |
| qma6100p-qst-rev-e | QST 官方 QMA6100P Datasheet Rev. E，54 页 | https://www.qstcorp.com/upload/pdf/202512/13-52-20%20QMA6100P%20Datasheet%20Rev%20E.pdf | 2026-10-11 核对：§6.3（第 14 页）完整启动序列、§9.44 OTP 状态、§9.51–53 模拟配置；补充 Rev. A1 缺失信息，适用性仍需板上验收 |

## Update History

| Date | Change |
|---|---|
| 2026-10-11 | 记录 QMA6100P 寄存器和缺失时钟编码的资料入口 |
| 2026-10-11 | 追查三轴满量程固定输出，补充 QST Rev. E 的完整启动依据 |
