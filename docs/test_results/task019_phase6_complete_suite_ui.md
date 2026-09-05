# TASK-019 Phase 6 正式套件与 UI 集成验证记录

> 日期：2026-09-05
>
> 结论：通过；允许进入 TASK-020。

## 1. 验证范围

本记录对应 TASK-019，验证正式 v2 套件、覆盖目录、TestEngine Fake 全套、控制器元数据和 Qt offscreen UI。验证只使用 `FakeModbusClient`、共享虚拟单调时钟与 offscreen 平台，不打开串口，不访问开发板或真实 RS485，也不执行 TASK-020 的真实 10 分钟验收。

## 2. 正式资产

- `testcases/phase6/phase6-rs485-full.json`：20 条启用用例，功能 8、协议 4、边界 4、数据一致性 2、自动恢复 1、稳定性 1。
- `testcases/phase6/phase6-fake-boundaries.json`：4 条仅限 Fake 的确定性超时、int16 极值与负温度缩放用例。
- 两套件合计 24 个唯一 ID；自动测试逐 ID 比对 JSON、`docs/phase6_coverage_matrix.md` 和 `docs/test_cases.md`。
- 所有成功写入均采用 `write_and_verify`，执行写前读取、独立回读和原值恢复；TC-B005 是被 0x03 明确拒绝的非法写请求，不产生成功写入。

## 3. 自动验证

全新构建目录：`build-host-task019`。

```powershell
$projectRoot = (Get-Location).Path
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
& cmd.exe /d /s /c "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && cmake -S $projectRoot\host\qt -B $projectRoot\build-host-task019 -G Ninja -DCMAKE_PREFIX_PATH=D:\Dev\Qt\6.8.3\msvc2022_64 -DCMAKE_BUILD_TYPE=Debug"
& cmd.exe /d /s /c "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && cmake --build $projectRoot\build-host-task019 --parallel"
ctest --test-dir build-host-task019 --output-on-failure
```

结果：配置、130 步全新构建成功；最终复跑 20/20 CTest 通过，总测试时间 8.72 秒。

新增 `host.testing.phase6_suite` 的结果：

- 主套件全通过路径：20/20 PASS。
- 主套件共执行 648 个请求，RequestId 全部非零且唯一，Fake 脚本全部消费，结束后无在途请求并释放 Testing owner。
- TC-P007 保存 15 个不可变复合步骤结果；TC-R-AUTO-001 保存异常与恢复两个步骤。
- TC-S001 保持正式 600000 ms 配置，虚拟执行 600 次采样，600 成功、0 失败、0 超时、600 个有效 RTT；内存证据不超过 64 条，JSONL 核对 600 条稳定性 attempt 均已写入。
- 四个成功写入用例均为“写前读取、写入、独立回读、恢复”四个请求，末步骤为成功的 `RestoreOriginal`。
- Fake 边界套件 4/4 PASS；确定性 timeout 只由结构化 `ResponseTimeout` 判定。
- 另从正式 JSON 选择用例注入断言 FAIL、串口 ERROR、恢复失败升级 `CleanupFailed`/ERROR 和稳定性中止，终态均符合规范。

新增/扩展的 offscreen UI 验证覆盖：

- 正式 v2 主套件加载并显示 20 行；
- sequence 当前 step ID、索引、重复次数；
- 自动恢复最终两个复合步骤、attempt sequence 与证据摘要；
- stability 当前迭代 1/600、运行中按钮响应和立即中止；
- 最终稳定性 total/success/failure/timeout、RTT、证据保留策略和 Session ID。

## 4. 回归结果

全部既有 v1/v2 Loader、断言、TestEngine、Phase 5 UI、通信后端、监控/配置 UI 和应用 smoke 测试均通过，无 Phase 5 回归。

## 5. Review

- 必须修复：无。
- 建议修复：无。
- 可选优化：TASK-020 在真实 RS485 上原样运行 20 条主套件，记录真实 10 分钟稳定性、逐类结果、阈值最终恢复和证据路径。

TASK-019 的实现、测试、文档同步与 Review 已完成。该结论只允许进入 TASK-020，不能宣称 Phase 6 已关闭或真实 RS485 主套件已通过。
