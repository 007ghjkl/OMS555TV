# TASK-029 Phase 9 最终 README、复现与现场演示验收记录

> 日期：2026-09-06
>
> 结论：通过；Phase 9 与当前 MVP 已关闭。

## 1. 验收范围

本任务重构最终 README，新增现场演示手册、完整任务索引和 16 项 MVP 证据矩阵；从三个全新目录复现 Host、RS485 Firmware 与纯 C 测试，并在当前安全低压台架执行实机短套件预检。TASK-029 不新增产品功能，也不重跑 Phase 6 的 10 分钟 stability 或 Phase 7 物理断线流程。

源码基线为提交 `45428ac`（TASK-028 完成后）。本记录中的构建目录、SessionLog 和最终现场报告均由 `.gitignore` 排除，不进入版本库。

## 2. 环境与版本

| 项目 | 本次观察值 |
|---|---|
| 平台 | Windows x64，PowerShell 7 |
| Qt | 6.8.3 `msvc2022_64` |
| Host 编译器 | MSVC 19.51.36256.0 |
| 系统 CMake / Ninja | CMake 4.4.3 / Ninja 1.13.2 |
| Firmware CMake / Ninja | STM32Cube bundle CMake 4.3.1 / Ninja 1.13.2 |
| ARM GCC | GNU Tools for STM32 14.3.1 |
| DUT/链路 | NUCLEO-F411RE，Firmware 0.2，USART1/自动换向 RS485，115200 8N1，Slave 1，约 20 cm 安全低压点对点台架 |
| 运行时端口 | 枚举到 COM3、COM6；本次 USB-RS485 预检选择 COM6，端口号不作为固定配置 |

## 3. 全新 Host 构建与全部 CTest

独立目录：`build-task029-host`，创建时间 2026-09-06 19:27:34（Asia/Shanghai）。

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -NoLogo
cmake -S host/qt -B build-task029-host -G Ninja -DCMAKE_PREFIX_PATH=D:/Dev/Qt/6.8.3/msvc2022_64 -DCMAKE_BUILD_TYPE=Debug
cmake --build build-task029-host --parallel
ctest --test-dir build-task029-host --output-on-failure
```

- 配置成功，识别 MSVC 19.51.36256.0；Vulkan headers 缺失是未使用的可选项，不影响生成。
- 完整构建 173/173 步成功。
- CTest 27/27 通过，0 失败，总耗时 15.42 秒；`host.application.smoke` 通过且不访问硬件。
- `build-task029-host/src/oms555tv_host.exe`：5,256,704 字节；SHA-256 `9FDBFDE1753658DDFECE9DA30D1A7AC73A8B9D297BF1AA27811B892C40D74138`。

## 4. RS485 Firmware 构建与纯 C 测试

RS485 ARM 独立目录：`build-task029-firmware-rs485`，创建时间 2026-09-06 19:29:03。

```powershell
$bundleRoot = Join-Path $env:LOCALAPPDATA 'stm32cube\bundles'
$env:Path = "$(Join-Path $bundleRoot 'gnu-tools-for-stm32\14.3.1+st.2\bin');$(Join-Path $bundleRoot 'ninja\1.13.2+st.1\bin');$env:Path"
& "$bundleRoot\cmake\4.3.1+st.1\bin\cmake.exe" -S firmware/stm32 -B build-task029-firmware-rs485 -G Ninja -DCMAKE_TOOLCHAIN_FILE=firmware/stm32/cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug -DOMS555TV_MODBUS_TRANSPORT=USART1_RS485
& "$bundleRoot\cmake\4.3.1+st.1\bin\cmake.exe" --build build-task029-firmware-rs485 --parallel
```

- 配置明确输出 `Modbus transport: USART1_RS485`；ARM 构建 36/36 步成功。
- 链接结果：RAM 3,344 B/128 KiB（2.55%），Flash 29,444 B/512 KiB（5.62%）。
- `.ioc` 移除本机自定义包路径、启用 CubeMX 默认 Firmware 位置，并继续固定 `STM32Cube FW_F4 V1.28.3`。该组合符合 [STM32CubeMX 6.18.1 官方位置选择说明](https://dev.st.com/stm32cube-docs/stm32cubemx/6.18.1/en/docs/markup/CubeMX_UserManual/chapters/04_4_stm32cubemx_user_interface.html)。随后再次配置通过，仍明确输出 `Modbus transport: USART1_RS485`，Ninja 确认无需重编；最终 `oms555tv_firmware.elf` 为 940,704 字节，SHA-256 `E7DC414A7D3B520DBC76DEDB823E5D5FCCF7E280668C8EFEABD978A049588779`。

纯 C 独立目录 `build-task029-firmware-tests` 使用 MSVC/Ninja 配置和构建成功；CTest 聚合目标 1/1 通过，直接执行 `phase1_unit_tests.exe` 得到 35/35 cases PASS、0 failures。

本任务没有重新烧录固件；当前 DUT 已运行并响应 Firmware 0.2 的 USART1/RS485 生产镜像。新 ELF 的构建通过不等于已烧录该 ELF。

## 5. 实机 CLI 预检

开始时间约 2026-09-06 19:30:22，使用本次 Host 构建的 `task016_rs485_validation.exe`、运行时选择的 COM6 和仓库原始 `testcases/functional/phase5-smoke.json`：

```powershell
$env:Path = "D:\Dev\Qt\6.8.3\msvc2022_64\bin;$env:Path"
.\build-task029-host\task016_rs485_validation.exe --port COM6 --suite .\testcases\functional\phase5-smoke.json --timeout-ms 500
```

结果：

- Phase 5 短套件 8/8 PASS；0 FAIL、0 ERROR、0 SKIPPED。
- A/B/C/环境温度为 27.5/27.4/27.5/25.0 ℃，光敏模拟电压为 1552 mV，Firmware 为 0.2，非法地址得到 Modbus 0x02。
- 11 个 Testing 请求全部保留 TX/RX；结果、诊断和 TEST 日志的 RequestId 集合均为 11 个且一致。
- 阈值前后均为 A/B/C 60.0 ℃、环境 40.0 ℃；`read_before_write`、0x06、独立 `verify_readback` 与 `restore_original` 全部成功。
- SessionLog：45 行、18,873 字节，SHA-256 `E5562F03B521EFDD82FBAED6F1C6AD946DF150113F0D6224767DBBCD75457BAF`；原始文件位于忽略目录，不提交。

该 CLI 预检证明通信和短套件在彩排前可用，但不能替代生产 UI 的现场主流程。

## 6. 现场 UI 主流程

用户按 [现场演示手册](../demo_runbook.md) 使用本次全新构建的生产 `oms555tv_host.exe` 完成主流程，并确认页面与生成报告无异常。未执行可选的 Phase 7 物理断线段，也未重新烧录固件。

### 6.1 时间与连接

- Session ID：`eaafc82e-acb3-4a75-b0cb-131e911440d9`。
- 会话：2026-09-06 19:43:28～19:46:55（Asia/Shanghai），约 3 分 27 秒；报告于 19:47:25 生成。从开始会话到报告生成约 3 分 57 秒，属于限定时长短演示。
- 运行时端口：COM6；115200 8N1、无流控、Slave 1、500 ms。COM6 仅为本次枚举结果。
- 用户完成最终 Online 复核、停止监控、主动断开和 Host 关闭，UI 全程可操作。

### 6.2 监控与配置

- 监控 owner 共 169 个请求，169 成功、0 失败；RTT 最小/平均/最大为 21.120/30.975/51.812 ms。
- 日志序列包含 33 个完整五块监控批次及停止前的 4 个成功请求，超过“至少 10 个有效周期”的彩排要求；完整快照继续只由完整批次发布。
- 第一次整块读取阈值为 A/B/C 60.0 ℃、环境 40.0 ℃。
- 配置操作 2 先整块读取原值，将 A 相写为 60.1 ℃，并对四路逐项执行 0x06 与独立 0x03 回读；全部成功。
- 配置操作 3 读到 A 相 60.1 ℃ 后，将四路恢复为 60.0/60.0/60.0/40.0 ℃，逐项写入和独立回读全部成功。
- 配置操作 4 的最终整块独立读取再次得到 60.0/60.0/60.0/40.0 ℃。

### 6.3 短套件、SessionLog 与报告

- 生产 UI 从仓库相对路径加载 `testcases/functional/phase5-smoke.json`。
- Run 1：2026-09-06 19:46:08.487～19:46:13.530，持续 5,043 ms；8 PASS、0 FAIL、0 ERROR、0 SKIPPED。
- 测试 RequestId 为 189～199，共 11 个；10 个正常成功响应，非法地址用例得到预期 Modbus 0x02 并判定 PASS。RTT 最小/平均/最大为 21.150/30.888/48.066 ms。
- `P5-THRESHOLD-RESTORE` 的 `read_before_write`、`write`、`verify_readback`、`restore_original` 四步均保留独立 RequestId、TX/RX 和 RTT。
- SessionLog 原始文件位于忽略目录，共 242 行、91,412 字节；SHA-256 `6E8F7914ED4E88A59C430436F4309AE2E690A557279749FD2236727EA74ECDE9`。
- 报告相对路径：`output/reports/phase5-smoke_20260906-114608-487Z_run-1.html`；46,231 字节；SHA-256 `E9093F6B912C89EC9DA5FAFE45081FDE2E80D56A25CE7335B8B0F581A72CE8E5`。
- 报告显示 Firmware 0.2、同一 Session ID、COM6/115200 8N1/Slave 1/500 ms、8/8 PASS 和 RequestId 189～199；报告中的 SessionLog 大小与 SHA-256 和磁盘工件完全一致。
- 报告测试人员为脱敏称谓“现场演示操作员”；测试台架和环境说明保持“未填写/不可用”，未由系统猜测。真实台架边界由本记录显式说明。
- 自动检查确认报告无脚本、无 HTTP/HTTPS 外部资源、无 Windows 用户目录；本地绝对项目路径只作为未提交报告中的 SessionLog 纯文本引用。用户人工打开报告并确认页面与内容无异常。

### 6.4 展示素材引用

本次 README 使用 TASK-028 已验收的 [安全低压 RS485 台架](../assets/hardware-rs485-bench.jpg)、[实时监控](../assets/host-monitoring.png)、[自动化测试结果](../assets/host-automation-results.png) 和 [HTML 报告](../assets/host-report.png)；[参数配置](../assets/host-configuration.png) 与 [通信诊断](../assets/host-diagnostics.png) 可从素材清单到达。它们是此前同一生产 Host/台架的真实脱敏素材，不冒充本次彩排截图；本次新增结论来自 SessionLog、报告和用户人工确认。

## 7. 文档、安全与仓库检查

最终检查结果：

- README 覆盖 `PROJECT_SPEC.md` 第 32 节要求的项目简介、截图、系统架构、硬件、软件环境、构建、烧录/连接、Host 使用、Modbus、自动化/半自动测试、报告示例、亮点、限制和后续规划。
- 新增现场演示手册、完整任务历史索引和 16 项证据矩阵；架构图、截图、示例报告、寄存器表、测试计划和 Bug 记录均可从 README 到达。
- 仓库 Markdown 相对链接检查 95 个文件、0 个失效目标；Mermaid 使用仓库内文本，不依赖外部渲染服务。
- Git 跟踪的 36 个 JSON 文件全部可解析；正式 Schema/Loader 语义仍由 27/27 Host CTest 覆盖。
- 文档收口后再次运行全部 Host CTest，27/27 通过、0 失败、总耗时 10.65 秒；Firmware 纯 C 可执行文件仍为 35/35 cases PASS。
- 将 CubeMX `.ioc` 从本机自定义包路径改为默认 Firmware 位置，并从历史记录中移除不必要的唯一设备贴纸编号；重新扫描未发现本机用户名、Windows 用户目录或该唯一编号。
- `build-task029-*`、原始 SessionLog 和现场报告均被 `.gitignore` 排除；仓库未加入构建产物或运行输出。
- 提交前删除仓库根目录下 38 个被 `/build-*/` 忽略的历史构建目录（约 13.84 GiB），仅保留本任务最新的 Host、Firmware RS485 和 Firmware 纯 C 测试验证目录；这些目录均可由已记录命令重新生成。
- 展示图片的脱敏、EXIF、哈希和真实性继续由 [TASK-028 记录](task028_phase9_demo_assets.md) 保证；本任务未编辑图片或示例报告。

## 8. MVP 证据矩阵与最终 Review

逐项状态见 [MVP 最低验收标准证据矩阵](../mvp_acceptance_matrix.md)。第 1～14 项引用历史分层证据，第 15 项由最终 README/技术文档与链接检查关闭，第 16 项由本次生产 UI 彩排关闭；16 项全部 PASS。

### 必须修复

无。

### 已在 Review 中修复

- README 从开发流水重构为评审者/使用者入口，并把完整任务历史移至独立索引。
- 构建命令不再硬编码用户目录，Firmware 明确要求独立 `USART1_RS485` 构建和产物核对。
- 删除历史文档中的唯一设备贴纸编号，将 `.ioc` 的本机 Cube Repository 绝对路径切换为可移植的默认 Firmware 位置。

### 可选后续优化

Linux 验证、8/24 小时实机稳定性、工业长线/终端/偏置/隔离/EMC、USB 自动重连、Raw Frame/CRC 注入、原生 PDF、安装包与 CI/CD 均需另立任务，不阻塞 MVP。

## 9. 结论

TASK-029 的最终 README、复现命令、Host/Firmware 独立构建、全部自动回归、实机预检、生产 UI 现场彩排、配置恢复、报告一致性、16 项证据矩阵、安全清洁检查和最终 Review 均已完成。Phase 9 与当前 MVP 关闭；结论仍只适用于已记录的 Windows、安全低压、约 20 cm 非隔离点对点 RS485 台架。
