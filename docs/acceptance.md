# 验收标准

## M1：本地输入输出

| 编号 | 检查 | 方法 | 通过标准 |
| --- | --- | --- | --- |
| M1-01 | 启动日志 | 复位后采集 12 s | 输出模式 SLOW、周期 1000 ms、GPIO8/GPIO9；无 panic |
| M1-02 | LED 慢闪 | 目视 + 心跳日志 | 亮/灭各约 1 s |
| M1-03 | 短按切换 | 按 BOOT 3 次（间隔 ≥1 s） | 恰好 3 次模式事件：SLOW→FAST→SOLID→SLOW |
| M1-04 | 去抖 | 快速抖动按压 10 次 + 主机毛刺注入 | 每次物理按压至多一个事件；单采样毛刺不产生事件 |
| M1-05 | 快闪/常亮 | 目视 | FAST 约 200 ms 翻转；SOLID 常亮 |
| M1-06 | 异常按压 | 长按 >800 ms（实机）+ 极短触点（主机注入） | 长按忽略并记录；短于去抖窗口的触点不产生事件 |
| M1-07 | 心跳与资源 | 采集 ≥30 s | 每 10 s 一条心跳；空闲堆无持续下降趋势 |
| M1-08 | 主机测试（app_mode / sensor_mock / button_fsm） | `tools/test-host.ps1` | 全部通过 |
| M1-09 | 1 小时运行 | 相同负载连续运行 | 无意外复位、看门狗或持续内存下降（M1 收尾执行） |
| M1-10 | 虚拟传感器采样 | 复位采集 12 s | 每 2 s 一条采样；心跳含 sensor 状态；无 panic |
| M1-11 | 传感器缺失 | 关闭 mock 构建并运行 | 明确输出 `init failed: UNSUPPORTED`；LED/按键/心跳继续工作 |

## M1 执行记录（2026-10-30）

| 编号 | 状态 | 证据 / 说明 |
| --- | --- | --- |
| M1-01 | PASS | `evidence/M1/m1_boot.log`：模式 SLOW、周期 1000 ms、GPIO8/GPIO9 |
| M1-02 | PASS | 用户目视慢闪；心跳日志每 10 s |
| M1-03 | PASS | `m1_button_short.log`：4 次短按（130/130/140/170 ms），模式 FAST→SOLID→SLOW→FAST，每次恰好一个事件 |
| M1-04 | PASS | `m1_debounce_test.log`：10 次快速轻点（110–210 ms）→ 10 次事件，无重复；主机 `button_fsm` 覆盖单采样毛刺 |
| M1-05 | PASS | 用户目视快闪（200 ms）与常亮 |
| M1-06 | PASS | 过长（2.9–5.7 s）实机忽略并记录（`m1_button_diag.log`）；10 ms 触点被去抖过滤、6 ms 按压在 1 采样配置下触发 TOO_SHORT（主机 `button_fsm`）。人手最快约 80 ms，无法实机复现 <20 ms |
| M1-07 | PASS（初步） | 心跳正常；空闲堆稳定（331980 → 327764，任务栈增加后保持） |
| M1-08 | PASS | `ctest`：app_mode、sensor_mock、button_fsm 共 3/3 通过（`tools/test-host.ps1`） |
| M1-09 | NOT_RUN | 1 小时长稳留待 M1 收尾 |
| M1-10 | PASS | `m1_sensor_boot.log`：2 s 采样、seq 递增、心跳含 sensor OK |
| M1-11 | PASS | `m1_sensor_missing_boot.log`：`init failed: UNSUPPORTED`，本地功能继续 |

## 记录要求

- 每次验证保存原始串口日志到 `evidence/M1/`，文件名包含日期与场景。
- 串口由单进程独占；采集前关闭 monitor。使用 `tools/capture.py --reset` 保证从复位开始采集。
- 结论必须区分：编译结果、主机测试、实板测试；缺条件记 NOT_RUN 并写原因。
