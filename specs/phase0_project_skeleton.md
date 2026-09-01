# Phase 0 工程骨架技术规范

> 状态：已按 `TASK-001` 实现并验证

## 1. 输入

- 本机 Qt 6.8.3 MSVC 2022 x64、CMake 和 Ninja。
- `PROJECT_SPEC.md`、架构草案与本规范。
- `docs/hardware_baseline.md` 基线 1.0（固件子任务的已满足输入）。

## 2. 输出

- 可配置和构建的 Qt Widgets 应用目标。
- 可由 CTest 运行的 Host 测试目标。
- Host 模块目录和最小日志 API。
- 按 NUCLEO-F411RE/STM32F411RET6、HSI/PLL 100 MHz 和已确认引脚生成的 STM32 HAL 最小工程。
- 真实可复现的构建说明。

## 3. Host 构建约束

- 使用 CMake，不引入其他构建系统。
- C++ 标准为 C++17。
- 首选 Qt 6.8.3 MSVC 2022 x64；必须通过 `find_package` 查找 Qt，不硬编码版本目录到源文件。
- 最小组件为 Core、Widgets、Test；SerialPort/SerialBus 仅在技术验证或后续模块需要时加入。
- 启用 `AUTOMOC`；构建目录位于源码树外的 `build-*` 目录。

## 4. 模块边界

- UI 不直接创建或操作底层串口协议对象。
- 日志 API 不依赖具体页面，可由 UI 和文件后端共同消费。
- 通信抽象必须允许 Fake 实现；Phase 0 可仅定义最小接口或验证性适配器，不实现完整协议。

## 5. 日志最小模型

日志记录至少包含时间戳、级别、模块和消息。级别枚举包含 `Debug`、`Info`、`Warning`、`Error`、`Test`。原始帧、请求 ID 和用例 ID 在后续规范扩展。

## 6. 测试

- CTest 必须能发现基础测试。
- 基础测试验证应用级纯 C++/Qt 组件可链接和执行，不依赖串口或硬件。
- 技术验证测试不得被误计为产品测试用例。

## 7. 错误处理

- CMake 缺少 Qt 组件时必须以明确错误结束。
- ARM GCC、GDB Server 或 Programmer CLI 路径未验证时，不得声称固件构建、调试或烧录通过。
- RS485 模块缺失不阻塞 Phase 0；当前通信只验证 USART2 VCP，不得写成 RS485 验证结果。
- 不通过硬编码路径或复制 SDK 文件规避工具链问题。

## 8. 验收与文档同步

只有实际执行配置、构建和测试后才能标记 Host 子任务完成。STM32 子任务必须有真实构建输出；若硬件未确认，Phase 0 保持部分未完成并明确说明。
