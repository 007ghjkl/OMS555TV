#include "app/MainWindow.h"

#include "configuration/ConfigurationService.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "report/ReportExportController.h"
#include "testing/TestAutomationController.h"
#include "ui/MonitoringViewModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>

namespace {

QString configurationStateText(oms555tv::configuration::ConfigurationState state)
{
    using State = oms555tv::configuration::ConfigurationState;
    switch (state) {
    case State::Idle: return QStringLiteral("空闲");
    case State::AcquiringOwner: return QStringLiteral("正在获取 ManualDebug owner");
    case State::ReadingCurrent: return QStringLiteral("正在读取当前阈值");
    case State::Writing: return QStringLiteral("正在写入阈值");
    case State::Verifying: return QStringLiteral("正在独立回读验证");
    case State::Cancelling: return QStringLiteral("正在取消");
    case State::ReleasingOwner: return QStringLiteral("正在释放 ManualDebug owner");
    }
    return QStringLiteral("未知");
}

QString itemStatusText(oms555tv::configuration::ConfigurationItemStatus status)
{
    using Status = oms555tv::configuration::ConfigurationItemStatus;
    switch (status) {
    case Status::Pending: return QStringLiteral("待执行");
    case Status::Succeeded: return QStringLiteral("成功");
    case Status::WriteFailed: return QStringLiteral("写入失败");
    case Status::ReadbackFailed: return QStringLiteral("回读失败");
    case Status::ReadbackMismatch: return QStringLiteral("回读不一致");
    case Status::Cancelled: return QStringLiteral("已取消");
    }
    return QStringLiteral("未知");
}

QString channelText(oms555tv::device::TemperatureChannel channel)
{
    using Channel = oms555tv::device::TemperatureChannel;
    switch (channel) {
    case Channel::PhaseA: return QStringLiteral("A 相");
    case Channel::PhaseB: return QStringLiteral("B 相");
    case Channel::PhaseC: return QStringLiteral("C 相");
    case Channel::Ambient: return QStringLiteral("环境");
    }
    return QStringLiteral("未知");
}

QString testRequestText(const oms555tv::testing::TestCase &testCase)
{
    using Type = oms555tv::testing::TestCaseType;
    if (testCase.declaredType == Type::Sequence) {
        return QStringLiteral("sequence steps=%1 repeat=%2")
            .arg(testCase.sequence.steps.size()).arg(testCase.sequence.repeatCount);
    }
    const auto &request = testCase.request;
    const QString requestText = request.function
            == oms555tv::testing::ModbusFunction::ReadHoldingRegisters
        ? QStringLiteral("0x03 addr=%1 count=%2")
              .arg(request.address.value()).arg(request.count)
        : QStringLiteral("0x06 addr=%1 value=%2")
              .arg(request.address.value()).arg(request.rawValue);
    if (testCase.declaredType == Type::Consistency) {
        return QStringLiteral("%1 samples=%2 interval=%3 ms")
            .arg(requestText).arg(testCase.consistency.sampleCount)
            .arg(testCase.consistency.interval.count());
    }
    if (testCase.declaredType == Type::Stability) {
        return QStringLiteral("%1 duration=%2 ms interval=%3 ms")
            .arg(requestText).arg(testCase.stability.duration.count())
            .arg(testCase.stability.interval.count());
    }
    return requestText;
}

QString testExpectedText(const oms555tv::testing::ExpectedAssertion &expected)
{
    using Type = oms555tv::testing::AssertionType;
    switch (expected.type) {
    case Type::Equals:
        return QStringLiteral("equals %1 %2").arg(expected.value).arg(expected.unit);
    case Type::Range:
        return QStringLiteral("range [%1, %2] %3")
            .arg(expected.minimum).arg(expected.maximum).arg(expected.unit);
    case Type::RegisterSequence: {
        QStringList values;
        for (const auto value : expected.values) values << QString::number(value);
        return QStringLiteral("sequence [%1]").arg(values.join(QStringLiteral(", ")));
    }
    case Type::BitMask:
        return QStringLiteral("bitmask mask=0x%1 value=0x%2")
            .arg(expected.mask, 4, 16, QLatin1Char('0'))
            .arg(expected.value, 4, 16, QLatin1Char('0'));
    case Type::ModbusException:
        return QStringLiteral("Modbus exception 0x%1")
            .arg(expected.exceptionCode, 2, 16, QLatin1Char('0'));
    case Type::Elements:
        return QStringLiteral("elements count=%1").arg(expected.elements.size());
    case Type::ResponseTimeout:
        return QStringLiteral("response timeout");
    case Type::UInt32: {
        using Comparison = oms555tv::testing::UInt32Comparison;
        switch (expected.uint32.comparison) {
        case Comparison::Range:
            return QStringLiteral("uint32 low-word-first range [%1, %2] %3")
                .arg(expected.uint32.minimum).arg(expected.uint32.maximum)
                .arg(expected.uint32.unit);
        case Comparison::NonDecreasing:
            return QStringLiteral("uint32 low-word-first non-decreasing %1")
                .arg(expected.uint32.unit);
        case Comparison::StrictlyIncreasing:
            return QStringLiteral("uint32 low-word-first strictly-increasing %1")
                .arg(expected.uint32.unit);
        }
        break;
    }
    case Type::StabilitySummary:
        return QStringLiteral(
            "stability total>=%1 successes>=%2 failures<=%3 timeouts<=%4 failure-rate<=%5 ppm")
            .arg(expected.stability.minimumTotal)
            .arg(expected.stability.minimumSuccesses)
            .arg(expected.stability.maximumFailures)
            .arg(expected.stability.maximumTimeouts)
            .arg(expected.stability.maximumFailureRatePpm);
    }
    return QStringLiteral("--");
}

QString testResultActualText(const oms555tv::testing::TestCaseResult &result)
{
    if (result.guidedRecovery) {
        return oms555tv::testing::guidedTerminalReasonName(
            result.guidedRecovery->terminalReason);
    }
    if (result.assertion) return result.assertion->actualSummary;
    if (result.error) return result.error->diagnostic;
    if (result.skipReason) {
        return oms555tv::testing::testSkipReasonName(*result.skipReason);
    }
    return QStringLiteral("--");
}

QString testAttemptDetails(const oms555tv::testing::TestRequestAttemptResult &attempt)
{
    const auto &request = attempt.requestResult;
    const auto &evidence = request.evidence;
    const QString rtt = evidence.rtt
        ? QStringLiteral("%1 ms").arg(
              std::chrono::duration<double, std::milli>(*evidence.rtt).count(), 0, 'f', 3)
        : QStringLiteral("--");
    QString text = QStringLiteral(
        "步骤=%1 attempt=%2%3 RequestId=%4 状态=%5 RTT=%6\nTX=%7\nRX=%8")
        .arg(oms555tv::testing::testStepPurposeName(attempt.purpose))
        .arg(attempt.stepAttempt)
        .arg(attempt.retry ? QStringLiteral("（retry: %1）").arg(attempt.retryReason)
                           : QString{})
        .arg(attempt.requestId.value)
        .arg(oms555tv::diagnostics::requestStateName(request.state), rtt,
             oms555tv::diagnostics::byteArrayHex(evidence.txAdu),
             oms555tv::diagnostics::byteArrayHex(evidence.rxAdu));
    if (!attempt.logicalStepId.isEmpty() || attempt.logicalStepIndex >= 0) {
        text.prepend(QStringLiteral("复合步骤=%1 index=%2 repetition=%3\n")
            .arg(attempt.logicalStepId.isEmpty() ? QStringLiteral("--")
                                                 : attempt.logicalStepId)
            .arg(attempt.logicalStepIndex + 1)
            .arg(attempt.repetition + 1));
    }
    if (request.error) {
        text += QStringLiteral("\n错误类别=%1 错误码=%2：%3")
            .arg(static_cast<int>(request.error->category))
            .arg(static_cast<int>(request.error->code))
            .arg(request.error->diagnostic);
    }
    return text;
}

std::array<oms555tv::device::Temperature, 4> thresholdArray(
    const oms555tv::device::AlarmThresholds &value)
{
    return {value.phaseA, value.phaseB, value.phaseC, value.ambient};
}

} // namespace

MainWindow::MainWindow(oms555tv::ui::MonitoringViewModel &viewModel,
                       QWidget *parent)
    : MainWindow(viewModel, nullptr, nullptr, nullptr, nullptr, nullptr, parent)
{
}

MainWindow::MainWindow(
    oms555tv::ui::MonitoringViewModel &viewModel,
    oms555tv::configuration::ConfigurationService &configuration,
    oms555tv::diagnostics::CommunicationDiagnosticsModel &diagnostics,
    oms555tv::logging::SessionLogService &sessionLog,
    QWidget *parent)
    : MainWindow(viewModel, &configuration, &diagnostics, &sessionLog, nullptr,
                 nullptr, parent)
{
}

MainWindow::MainWindow(
    oms555tv::ui::MonitoringViewModel &viewModel,
    oms555tv::configuration::ConfigurationService &configuration,
    oms555tv::diagnostics::CommunicationDiagnosticsModel &diagnostics,
    oms555tv::logging::SessionLogService &sessionLog,
    oms555tv::testing::TestAutomationController &automation,
    QWidget *parent)
    : MainWindow(viewModel, &configuration, &diagnostics, &sessionLog,
                 &automation, nullptr, parent)
{
}

MainWindow::MainWindow(
    oms555tv::ui::MonitoringViewModel &viewModel,
    oms555tv::configuration::ConfigurationService &configuration,
    oms555tv::diagnostics::CommunicationDiagnosticsModel &diagnostics,
    oms555tv::logging::SessionLogService &sessionLog,
    oms555tv::testing::TestAutomationController &automation,
    oms555tv::report::ReportExportController &reportExport,
    QWidget *parent)
    : MainWindow(viewModel, &configuration, &diagnostics, &sessionLog,
                 &automation, &reportExport, parent)
{
}

MainWindow::MainWindow(
    oms555tv::ui::MonitoringViewModel &viewModel,
    oms555tv::configuration::ConfigurationService *configuration,
    oms555tv::diagnostics::CommunicationDiagnosticsModel *diagnostics,
    oms555tv::logging::SessionLogService *sessionLog,
    oms555tv::testing::TestAutomationController *automation,
    oms555tv::report::ReportExportController *reportExport,
    QWidget *parent)
    : QMainWindow(parent)
    , viewModel_(viewModel)
    , configuration_(configuration)
    , diagnostics_(diagnostics)
    , sessionLog_(sessionLog)
    , automation_(automation)
    , reportExport_(reportExport)
{
    setWindowTitle(QStringLiteral("OMS555TV 监控与诊断平台"));
    resize(1180, 860);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("mainTabs"));

    auto *content = new QWidget;
    auto *root = new QVBoxLayout(content);
    auto *title = new QLabel(QStringLiteral("OMS555TV 实时监控平台"), content);
    QFont titleFont = title->font();
    titleFont.setPointSize(17);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);

    auto *connectionGroup = new QGroupBox(QStringLiteral("RS485 连接"), content);
    auto *connectionLayout = new QGridLayout(connectionGroup);
    portCombo_ = new QComboBox(connectionGroup);
    portCombo_->setObjectName(QStringLiteral("portCombo"));
    refreshPortsButton_ = new QPushButton(QStringLiteral("刷新串口"), connectionGroup);
    refreshPortsButton_->setObjectName(QStringLiteral("refreshPortsButton"));
    slaveAddressSpin_ = new QSpinBox(connectionGroup);
    slaveAddressSpin_->setObjectName(QStringLiteral("slaveAddressSpin"));
    slaveAddressSpin_->setRange(1, 247);
    timeoutSpin_ = new QSpinBox(connectionGroup);
    timeoutSpin_->setObjectName(QStringLiteral("timeoutSpin"));
    timeoutSpin_->setRange(1, 60000);
    timeoutSpin_->setSuffix(QStringLiteral(" ms"));
    auto *serialParams = new QLabel(
        QStringLiteral("115200 baud / 8 数据位 / 无校验 / 1 停止位 / 无流控"),
        connectionGroup);
    serialParams->setObjectName(QStringLiteral("serialParamsLabel"));
    connectButton_ = new QPushButton(QStringLiteral("连接"), connectionGroup);
    connectButton_->setObjectName(QStringLiteral("connectButton"));
    disconnectButton_ = new QPushButton(QStringLiteral("断开"), connectionGroup);
    disconnectButton_->setObjectName(QStringLiteral("disconnectButton"));
    recoverButton_ = new QPushButton(QStringLiteral("从错误恢复"), connectionGroup);
    recoverButton_->setObjectName(QStringLiteral("recoverButton"));
    connectionLayout->addWidget(new QLabel(QStringLiteral("串口：")), 0, 0);
    connectionLayout->addWidget(portCombo_, 0, 1, 1, 3);
    connectionLayout->addWidget(refreshPortsButton_, 0, 4);
    connectionLayout->addWidget(new QLabel(QStringLiteral("Slave ID：")), 1, 0);
    connectionLayout->addWidget(slaveAddressSpin_, 1, 1);
    connectionLayout->addWidget(new QLabel(QStringLiteral("响应超时：")), 1, 2);
    connectionLayout->addWidget(timeoutSpin_, 1, 3);
    connectionLayout->addWidget(serialParams, 2, 0, 1, 5);
    auto *connectionButtons = new QHBoxLayout;
    connectionButtons->addWidget(connectButton_);
    connectionButtons->addWidget(disconnectButton_);
    connectionButtons->addWidget(recoverButton_);
    connectionButtons->addStretch();
    connectionLayout->addLayout(connectionButtons, 3, 0, 1, 5);
    root->addWidget(connectionGroup);

    auto *monitorGroup = new QGroupBox(QStringLiteral("监控状态与调度"), content);
    auto *monitorLayout = new QGridLayout(monitorGroup);
    periodCombo_ = new QComboBox(monitorGroup);
    periodCombo_->setObjectName(QStringLiteral("periodCombo"));
    for (const int period : {100, 500, 1000, 2000}) {
        periodCombo_->addItem(QStringLiteral("%1 ms").arg(period), period);
    }
    startMonitoringButton_ = new QPushButton(QStringLiteral("开始监控"), monitorGroup);
    startMonitoringButton_->setObjectName(QStringLiteral("startMonitoringButton"));
    stopMonitoringButton_ = new QPushButton(QStringLiteral("停止监控"), monitorGroup);
    stopMonitoringButton_->setObjectName(QStringLiteral("stopMonitoringButton"));
    appStateLabel_ = makeValueLabel(QStringLiteral("appStateLabel"));
    connectionStateLabel_ = makeValueLabel(QStringLiteral("connectionStateLabel"));
    healthLabel_ = makeValueLabel(QStringLiteral("healthLabel"));
    freshnessLabel_ = makeValueLabel(QStringLiteral("freshnessLabel"));
    lastSuccessfulLabel_ = makeValueLabel(QStringLiteral("lastSuccessfulLabel"));
    lastErrorLabel_ = makeValueLabel(QStringLiteral("lastErrorLabel"));
    lastErrorLabel_->setWordWrap(true);
    effectivePeriodLabel_ = makeValueLabel(QStringLiteral("effectivePeriodLabel"));
    overrunLabel_ = makeValueLabel(QStringLiteral("overrunLabel"));
    monitorLayout->addWidget(new QLabel(QStringLiteral("目标周期：")), 0, 0);
    monitorLayout->addWidget(periodCombo_, 0, 1);
    monitorLayout->addWidget(startMonitoringButton_, 0, 2);
    monitorLayout->addWidget(stopMonitoringButton_, 0, 3);
    monitorLayout->addWidget(new QLabel(QStringLiteral("应用状态：")), 1, 0);
    monitorLayout->addWidget(appStateLabel_, 1, 1);
    monitorLayout->addWidget(new QLabel(QStringLiteral("连接状态：")), 1, 2);
    monitorLayout->addWidget(connectionStateLabel_, 1, 3);
    monitorLayout->addWidget(new QLabel(QStringLiteral("设备健康：")), 2, 0);
    monitorLayout->addWidget(healthLabel_, 2, 1);
    monitorLayout->addWidget(new QLabel(QStringLiteral("数据有效性：")), 2, 2);
    monitorLayout->addWidget(freshnessLabel_, 2, 3);
    monitorLayout->addWidget(new QLabel(QStringLiteral("实际有效周期：")), 3, 0);
    monitorLayout->addWidget(effectivePeriodLabel_, 3, 1);
    monitorLayout->addWidget(new QLabel(QStringLiteral("周期超限：")), 3, 2);
    monitorLayout->addWidget(overrunLabel_, 3, 3);
    monitorLayout->addWidget(new QLabel(QStringLiteral("最后成功快照：")), 4, 0);
    monitorLayout->addWidget(lastSuccessfulLabel_, 4, 1, 1, 3);
    monitorLayout->addWidget(new QLabel(QStringLiteral("最近错误：")), 5, 0);
    monitorLayout->addWidget(lastErrorLabel_, 5, 1, 1, 3);
    root->addWidget(monitorGroup);

    auto *measurementsGroup = new QGroupBox(QStringLiteral("设备实时数据"), content);
    auto *measurementsLayout = new QFormLayout(measurementsGroup);
    phaseATemperatureLabel_ = makeValueLabel(QStringLiteral("phaseATemperatureLabel"));
    phaseBTemperatureLabel_ = makeValueLabel(QStringLiteral("phaseBTemperatureLabel"));
    phaseCTemperatureLabel_ = makeValueLabel(QStringLiteral("phaseCTemperatureLabel"));
    ambientTemperatureLabel_ = makeValueLabel(QStringLiteral("ambientTemperatureLabel"));
    lightMillivoltsLabel_ = makeValueLabel(QStringLiteral("lightMillivoltsLabel"));
    firmwareVersionLabel_ = makeValueLabel(QStringLiteral("firmwareVersionLabel"));
    uptimeLabel_ = makeValueLabel(QStringLiteral("uptimeLabel"));
    measurementsLayout->addRow(QStringLiteral("A 相温度："), phaseATemperatureLabel_);
    measurementsLayout->addRow(QStringLiteral("B 相温度："), phaseBTemperatureLabel_);
    measurementsLayout->addRow(QStringLiteral("C 相温度："), phaseCTemperatureLabel_);
    measurementsLayout->addRow(QStringLiteral("环境温度："), ambientTemperatureLabel_);
    measurementsLayout->addRow(QStringLiteral("光敏模拟电压："), lightMillivoltsLabel_);
    measurementsLayout->addRow(QStringLiteral("Firmware 版本："), firmwareVersionLabel_);
    measurementsLayout->addRow(QStringLiteral("设备运行时间："), uptimeLabel_);
    auto *alarmGroup = new QGroupBox(QStringLiteral("告警与设备状态"), content);
    auto *alarmLayout = new QFormLayout(alarmGroup);
    phaseAAlarmLabel_ = makeValueLabel(QStringLiteral("phaseAAlarmLabel"));
    phaseBAlarmLabel_ = makeValueLabel(QStringLiteral("phaseBAlarmLabel"));
    phaseCAlarmLabel_ = makeValueLabel(QStringLiteral("phaseCAlarmLabel"));
    ambientAlarmLabel_ = makeValueLabel(QStringLiteral("ambientAlarmLabel"));
    deviceStatusLabel_ = makeValueLabel(QStringLiteral("deviceStatusLabel"));
    deviceStatusLabel_->setWordWrap(true);
    alarmLayout->addRow(QStringLiteral("A 相高温："), phaseAAlarmLabel_);
    alarmLayout->addRow(QStringLiteral("B 相高温："), phaseBAlarmLabel_);
    alarmLayout->addRow(QStringLiteral("C 相高温："), phaseCAlarmLabel_);
    alarmLayout->addRow(QStringLiteral("环境高温："), ambientAlarmLabel_);
    alarmLayout->addRow(QStringLiteral("传感器/状态字："), deviceStatusLabel_);
    auto *dataRow = new QHBoxLayout;
    dataRow->addWidget(measurementsGroup, 1);
    dataRow->addWidget(alarmGroup, 2);
    root->addLayout(dataRow);

    auto *statisticsGroup = new QGroupBox(QStringLiteral("通信统计（当前进程会话）"), content);
    auto *statisticsLayout = new QGridLayout(statisticsGroup);
    requestsLabel_ = makeValueLabel(QStringLiteral("requestsLabel"));
    succeededLabel_ = makeValueLabel(QStringLiteral("succeededLabel"));
    failedLabel_ = makeValueLabel(QStringLiteral("failedLabel"));
    timedOutLabel_ = makeValueLabel(QStringLiteral("timedOutLabel"));
    successRateLabel_ = makeValueLabel(QStringLiteral("successRateLabel"));
    rttLabel_ = makeValueLabel(QStringLiteral("rttLabel"));
    statisticsLayout->addWidget(new QLabel(QStringLiteral("请求：")), 0, 0);
    statisticsLayout->addWidget(requestsLabel_, 0, 1);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("成功：")), 0, 2);
    statisticsLayout->addWidget(succeededLabel_, 0, 3);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("失败：")), 0, 4);
    statisticsLayout->addWidget(failedLabel_, 0, 5);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("超时：")), 1, 0);
    statisticsLayout->addWidget(timedOutLabel_, 1, 1);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("成功率：")), 1, 2);
    statisticsLayout->addWidget(successRateLabel_, 1, 3);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("RTT：")), 2, 0);
    statisticsLayout->addWidget(rttLabel_, 2, 1, 1, 5);
    root->addWidget(statisticsGroup);
    root->addStretch();
    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(content);
    tabs->addTab(scrollArea, QStringLiteral("实时监控"));

    if (configuration_ && diagnostics_ && sessionLog_) {
        auto *configurationPage = new QWidget;
        auto *configurationLayout = new QVBoxLayout(configurationPage);
        auto *notice = new QLabel(
            QStringLiteral("配置仅在“已连接（空闲）”状态可用；监控中请先受控停止。"));
        notice->setWordWrap(true);
        configurationLayout->addWidget(notice);
        auto *thresholdGroup = new QGroupBox(QStringLiteral("四路告警阈值"));
        auto *thresholdLayout = new QGridLayout(thresholdGroup);
        thresholdLayout->addWidget(new QLabel(QStringLiteral("通道")), 0, 0);
        thresholdLayout->addWidget(new QLabel(QStringLiteral("当前已验证值")), 0, 1);
        thresholdLayout->addWidget(new QLabel(QStringLiteral("编辑值")), 0, 2);
        const QStringList channelNames{QStringLiteral("A 相"), QStringLiteral("B 相"),
                                       QStringLiteral("C 相"), QStringLiteral("环境")};
        const std::array<double, 4> defaults{60.0, 60.0, 60.0, 40.0};
        for (int index = 0; index < 4; ++index) {
            thresholdCurrentLabels_[index] = new QLabel(QStringLiteral("--"), thresholdGroup);
            thresholdCurrentLabels_[index]->setObjectName(
                QStringLiteral("thresholdCurrent%1").arg(index));
            thresholdSpins_[index] = new QDoubleSpinBox(thresholdGroup);
            thresholdSpins_[index]->setObjectName(QStringLiteral("thresholdSpin%1").arg(index));
            thresholdSpins_[index]->setRange(-40.0, 80.0);
            thresholdSpins_[index]->setDecimals(1);
            thresholdSpins_[index]->setSingleStep(0.1);
            thresholdSpins_[index]->setSuffix(QStringLiteral(" ℃"));
            thresholdSpins_[index]->setValue(defaults[index]);
            thresholdLayout->addWidget(new QLabel(channelNames[index]), index + 1, 0);
            thresholdLayout->addWidget(thresholdCurrentLabels_[index], index + 1, 1);
            thresholdLayout->addWidget(thresholdSpins_[index], index + 1, 2);
        }
        configurationLayout->addWidget(thresholdGroup);
        auto *buttons = new QHBoxLayout;
        readThresholdsButton_ = new QPushButton(QStringLiteral("读取阈值"));
        readThresholdsButton_->setObjectName(QStringLiteral("readThresholdsButton"));
        writeThresholdsButton_ = new QPushButton(QStringLiteral("写入并回读"));
        writeThresholdsButton_->setObjectName(QStringLiteral("writeThresholdsButton"));
        cancelConfigurationButton_ = new QPushButton(QStringLiteral("取消配置"));
        cancelConfigurationButton_->setObjectName(QStringLiteral("cancelConfigurationButton"));
        configurationStateLabel_ = new QLabel;
        configurationStateLabel_->setObjectName(QStringLiteral("configurationStateLabel"));
        buttons->addWidget(readThresholdsButton_);
        buttons->addWidget(writeThresholdsButton_);
        buttons->addWidget(cancelConfigurationButton_);
        buttons->addWidget(configurationStateLabel_);
        buttons->addStretch();
        configurationLayout->addLayout(buttons);
        configurationResult_ = new QPlainTextEdit;
        configurationResult_->setObjectName(QStringLiteral("configurationResult"));
        configurationResult_->setReadOnly(true);
        configurationResult_->setPlaceholderText(QStringLiteral("逐项写入与回读结果将在这里显示"));
        configurationLayout->addWidget(configurationResult_, 1);
        tabs->addTab(configurationPage, QStringLiteral("参数配置"));

        auto *diagnosticsPage = new QWidget;
        auto *diagnosticsLayout = new QVBoxLayout(diagnosticsPage);
        auto *filters = new QHBoxLayout;
        diagnosticLevelFilter_ = new QComboBox;
        diagnosticLevelFilter_->setObjectName(QStringLiteral("diagnosticLevelFilter"));
        diagnosticLevelFilter_->addItems({QStringLiteral("全部级别"), QStringLiteral("DEBUG"),
                                          QStringLiteral("INFO"), QStringLiteral("WARN"),
                                          QStringLiteral("ERROR"), QStringLiteral("TEST")});
        diagnosticResultFilter_ = new QComboBox;
        diagnosticResultFilter_->setObjectName(QStringLiteral("diagnosticResultFilter"));
        diagnosticResultFilter_->addItems({QStringLiteral("全部结果"), QStringLiteral("成功"),
                                           QStringLiteral("非成功")});
        diagnosticRequestFilter_ = new QLineEdit;
        diagnosticRequestFilter_->setObjectName(QStringLiteral("diagnosticRequestFilter"));
        diagnosticRequestFilter_->setPlaceholderText(QStringLiteral("精确 RequestId"));
        clearDiagnosticsButton_ = new QPushButton(QStringLiteral("清空诊断记录"));
        clearDiagnosticsButton_->setObjectName(QStringLiteral("clearDiagnosticsButton"));
        filters->addWidget(diagnosticLevelFilter_);
        filters->addWidget(diagnosticResultFilter_);
        filters->addWidget(diagnosticRequestFilter_);
        filters->addWidget(clearDiagnosticsButton_);
        diagnosticsLayout->addLayout(filters);
        auto *splitter = new QSplitter(Qt::Vertical);
        diagnosticTable_ = new QTableWidget(splitter);
        diagnosticTable_->setObjectName(QStringLiteral("diagnosticTable"));
        diagnosticTable_->setColumnCount(8);
        diagnosticTable_->setHorizontalHeaderLabels({
            QStringLiteral("时间"), QStringLiteral("RequestId"), QStringLiteral("owner"),
            QStringLiteral("结果"), QStringLiteral("功能码"), QStringLiteral("请求"),
            QStringLiteral("RTT"), QStringLiteral("错误码")});
        diagnosticTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        diagnosticTable_->setSelectionMode(QAbstractItemView::SingleSelection);
        diagnosticTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        diagnosticTable_->horizontalHeader()->setStretchLastSection(true);
        diagnosticDetails_ = new QPlainTextEdit(splitter);
        diagnosticDetails_->setObjectName(QStringLiteral("diagnosticDetails"));
        diagnosticDetails_->setReadOnly(true);
        splitter->addWidget(diagnosticTable_);
        splitter->addWidget(diagnosticDetails_);
        diagnosticsLayout->addWidget(splitter, 1);
        tabs->addTab(diagnosticsPage, QStringLiteral("通信调试"));

        if (automation_) {
            auto *testingPage = new QWidget;
            auto *testingLayout = new QVBoxLayout(testingPage);
            auto *suiteRow = new QHBoxLayout;
            testSuitePath_ = new QLineEdit;
            testSuitePath_->setObjectName(QStringLiteral("testSuitePath"));
            testSuitePath_->setPlaceholderText(
                QStringLiteral("选择 testcases/ 下的 JSON 测试套件"));
            browseTestSuiteButton_ = new QPushButton(QStringLiteral("浏览"));
            browseTestSuiteButton_->setObjectName(QStringLiteral("browseTestSuiteButton"));
            loadTestSuiteButton_ = new QPushButton(QStringLiteral("加载套件"));
            loadTestSuiteButton_->setObjectName(QStringLiteral("loadTestSuiteButton"));
            suiteRow->addWidget(testSuitePath_, 1);
            suiteRow->addWidget(browseTestSuiteButton_);
            suiteRow->addWidget(loadTestSuiteButton_);
            testingLayout->addLayout(suiteRow);

            testSuiteSummaryLabel_ = new QLabel;
            testSuiteSummaryLabel_->setObjectName(QStringLiteral("testSuiteSummaryLabel"));
            testSuiteSummaryLabel_->setWordWrap(true);
            testLoadErrors_ = new QPlainTextEdit;
            testLoadErrors_->setObjectName(QStringLiteral("testLoadErrors"));
            testLoadErrors_->setReadOnly(true);
            testLoadErrors_->setMaximumHeight(100);
            testLoadErrors_->setPlaceholderText(QStringLiteral("加载错误和 JSON 路径将在这里显示"));
            testingLayout->addWidget(testSuiteSummaryLabel_);
            testingLayout->addWidget(testLoadErrors_);

            auto *testButtons = new QHBoxLayout;
            runSelectedTestsButton_ = new QPushButton(QStringLiteral("执行选中"));
            runSelectedTestsButton_->setObjectName(QStringLiteral("runSelectedTestsButton"));
            runAllTestsButton_ = new QPushButton(QStringLiteral("执行全部"));
            runAllTestsButton_->setObjectName(QStringLiteral("runAllTestsButton"));
            skipTestButton_ = new QPushButton(QStringLiteral("跳过选中待执行用例"));
            skipTestButton_->setObjectName(QStringLiteral("skipTestButton"));
            abortTestsButton_ = new QPushButton(QStringLiteral("中止"));
            abortTestsButton_->setObjectName(QStringLiteral("abortTestsButton"));
            resumeMonitoringCheck_ = new QCheckBox(QStringLiteral("完成后恢复此前监控"));
            resumeMonitoringCheck_->setObjectName(QStringLiteral("resumeMonitoringCheck"));
            testButtons->addWidget(runSelectedTestsButton_);
            testButtons->addWidget(runAllTestsButton_);
            testButtons->addWidget(skipTestButton_);
            testButtons->addWidget(abortTestsButton_);
            testButtons->addWidget(resumeMonitoringCheck_);
            testButtons->addStretch();
            testingLayout->addLayout(testButtons);

            auto *stateRow = new QGridLayout;
            testWorkflowStateLabel_ = new QLabel;
            testWorkflowStateLabel_->setObjectName(QStringLiteral("testWorkflowStateLabel"));
            testProgressLabel_ = new QLabel;
            testProgressLabel_->setObjectName(QStringLiteral("testProgressLabel"));
            testCurrentStepLabel_ = new QLabel;
            testCurrentStepLabel_->setObjectName(QStringLiteral("testCurrentStepLabel"));
            testStatisticsLabel_ = new QLabel;
            testStatisticsLabel_->setObjectName(QStringLiteral("testStatisticsLabel"));
            stateRow->addWidget(new QLabel(QStringLiteral("工作流：")), 0, 0);
            stateRow->addWidget(testWorkflowStateLabel_, 0, 1);
            stateRow->addWidget(new QLabel(QStringLiteral("进度：")), 0, 2);
            stateRow->addWidget(testProgressLabel_, 0, 3);
            stateRow->addWidget(new QLabel(QStringLiteral("当前步骤：")), 1, 0);
            stateRow->addWidget(testCurrentStepLabel_, 1, 1, 1, 3);
            stateRow->addWidget(new QLabel(QStringLiteral("统计：")), 2, 0);
            stateRow->addWidget(testStatisticsLabel_, 2, 1, 1, 3);
            testingLayout->addLayout(stateRow);

            guidedPanel_ = new QGroupBox(QStringLiteral("引导式人工操作"));
            guidedPanel_->setObjectName(QStringLiteral("guidedPanel"));
            auto *guidedLayout = new QVBoxLayout(guidedPanel_);
            guidedPromptTitleLabel_ = new QLabel;
            guidedPromptTitleLabel_->setObjectName(QStringLiteral("guidedPromptTitleLabel"));
            guidedPromptTitleLabel_->setWordWrap(true);
            guidedInstructionLabel_ = new QLabel;
            guidedInstructionLabel_->setObjectName(QStringLiteral("guidedInstructionLabel"));
            guidedInstructionLabel_->setWordWrap(true);
            guidedSafetyLabel_ = new QLabel;
            guidedSafetyLabel_->setObjectName(QStringLiteral("guidedSafetyLabel"));
            guidedSafetyLabel_->setWordWrap(true);
            guidedCountdownLabel_ = new QLabel;
            guidedCountdownLabel_->setObjectName(QStringLiteral("guidedCountdownLabel"));
            guidedObservationProgressLabel_ = new QLabel;
            guidedObservationProgressLabel_->setObjectName(
                QStringLiteral("guidedObservationProgressLabel"));
            guidedRecoveryTimingLabel_ = new QLabel;
            guidedRecoveryTimingLabel_->setObjectName(
                QStringLiteral("guidedRecoveryTimingLabel"));
            guidedRestorationReminderLabel_ = new QLabel;
            guidedRestorationReminderLabel_->setObjectName(
                QStringLiteral("guidedRestorationReminderLabel"));
            guidedRestorationReminderLabel_->setWordWrap(true);
            auto *guidedButtons = new QHBoxLayout;
            guidedConfirmButton_ = new QPushButton(QStringLiteral("确认已完成"));
            guidedConfirmButton_->setObjectName(QStringLiteral("guidedConfirmButton"));
            guidedCancelButton_ = new QPushButton(QStringLiteral("取消测试"));
            guidedCancelButton_->setObjectName(QStringLiteral("guidedCancelButton"));
            guidedButtons->addWidget(guidedConfirmButton_);
            guidedButtons->addWidget(guidedCancelButton_);
            guidedButtons->addStretch();
            guidedLayout->addWidget(guidedPromptTitleLabel_);
            guidedLayout->addWidget(guidedInstructionLabel_);
            guidedLayout->addWidget(guidedSafetyLabel_);
            guidedLayout->addWidget(guidedCountdownLabel_);
            guidedLayout->addWidget(guidedObservationProgressLabel_);
            guidedLayout->addWidget(guidedRecoveryTimingLabel_);
            guidedLayout->addWidget(guidedRestorationReminderLabel_);
            guidedLayout->addLayout(guidedButtons);
            testingLayout->addWidget(guidedPanel_);
            auto *guidedRefreshTimer = new QTimer(this);
            guidedRefreshTimer->setInterval(100);
            connect(guidedRefreshTimer, &QTimer::timeout,
                    this, &MainWindow::renderTesting);
            guidedRefreshTimer->start();

            auto *testSplitter = new QSplitter(Qt::Vertical);
            testCaseTable_ = new QTableWidget(testSplitter);
            testCaseTable_->setObjectName(QStringLiteral("testCaseTable"));
            testCaseTable_->setColumnCount(9);
            testCaseTable_->setHorizontalHeaderLabels({
                QStringLiteral("ID"), QStringLiteral("名称"), QStringLiteral("类别"),
                QStringLiteral("类型"), QStringLiteral("状态"), QStringLiteral("请求"),
                QStringLiteral("预期"), QStringLiteral("实际/原因"), QStringLiteral("耗时")});
            testCaseTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
            testCaseTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
            testCaseTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
            testCaseTable_->horizontalHeader()->setStretchLastSection(true);
            testCaseDetails_ = new QPlainTextEdit(testSplitter);
            testCaseDetails_->setObjectName(QStringLiteral("testCaseDetails"));
            testCaseDetails_->setReadOnly(true);
            testCaseDetails_->setPlaceholderText(
                QStringLiteral("选择用例以查看复合步骤、稳定性统计和证据保留摘要"));
            testSplitter->addWidget(testCaseTable_);
            testSplitter->addWidget(testCaseDetails_);
            testingLayout->addWidget(testSplitter, 1);
            tabs->addTab(testingPage, QStringLiteral("自动化测试"));
        }

        if (reportExport_) {
            auto *reportPage = new QWidget;
            auto *reportLayout = new QVBoxLayout(reportPage);
            auto *notice = new QLabel(
                QStringLiteral("报告只绑定最近一次完整终态结果。测试运行中禁止导出；"
                               "HTML 为离线自包含文件，PDF 可通过浏览器打印，"
                               "本应用不提供原生 PDF。"), reportPage);
            notice->setWordWrap(true);
            reportLayout->addWidget(notice);

            reportGateLabel_ = new QLabel(reportPage);
            reportGateLabel_->setObjectName(QStringLiteral("reportGateLabel"));
            reportGateLabel_->setWordWrap(true);
            reportLayout->addWidget(reportGateLabel_);

            auto *previewGroup = new QGroupBox(QStringLiteral("最近一次完整结果"), reportPage);
            auto *previewLayout = new QVBoxLayout(previewGroup);
            reportRunLabel_ = new QLabel(previewGroup);
            reportRunLabel_->setObjectName(QStringLiteral("reportRunLabel"));
            reportRunLabel_->setWordWrap(true);
            reportRunLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
            reportSummaryLabel_ = new QLabel(previewGroup);
            reportSummaryLabel_->setObjectName(QStringLiteral("reportSummaryLabel"));
            reportSummaryLabel_->setWordWrap(true);
            reportEvidenceLabel_ = new QLabel(previewGroup);
            reportEvidenceLabel_->setObjectName(QStringLiteral("reportEvidenceLabel"));
            reportEvidenceLabel_->setWordWrap(true);
            reportConfiguredMetadataLabel_ = new QLabel(previewGroup);
            reportConfiguredMetadataLabel_->setObjectName(
                QStringLiteral("reportConfiguredMetadataLabel"));
            reportConfiguredMetadataLabel_->setWordWrap(true);
            previewLayout->addWidget(reportRunLabel_);
            previewLayout->addWidget(reportSummaryLabel_);
            previewLayout->addWidget(reportEvidenceLabel_);
            previewLayout->addWidget(reportConfiguredMetadataLabel_);
            reportLayout->addWidget(previewGroup);

            auto *metadataGroup = new QGroupBox(QStringLiteral("报告元数据"), reportPage);
            auto *metadataLayout = new QFormLayout(metadataGroup);
            auto *projectName = new QLineEdit(QStringLiteral("OMS555TV"), metadataGroup);
            projectName->setObjectName(QStringLiteral("reportProjectName"));
            projectName->setReadOnly(true);
            reportTesterEdit_ = new QLineEdit(metadataGroup);
            reportTesterEdit_->setObjectName(QStringLiteral("reportTesterEdit"));
            reportTesterEdit_->setPlaceholderText(
                QStringLiteral("必填；仅由操作员输入，不读取 Windows 用户名"));
            reportDeviceModelEdit_ = new QLineEdit(metadataGroup);
            reportDeviceModelEdit_->setObjectName(QStringLiteral("reportDeviceModelEdit"));
            reportDeviceModelEdit_->setPlaceholderText(
                QStringLiteral("仅在套件未配置设备型号时补缺"));
            reportTestBenchEdit_ = new QLineEdit(metadataGroup);
            reportTestBenchEdit_->setObjectName(QStringLiteral("reportTestBenchEdit"));
            reportTestBenchEdit_->setPlaceholderText(
                QStringLiteral("仅在套件未配置测试台架时补缺"));
            reportEnvironmentEdit_ = new QPlainTextEdit(metadataGroup);
            reportEnvironmentEdit_->setObjectName(QStringLiteral("reportEnvironmentEdit"));
            reportEnvironmentEdit_->setMaximumHeight(80);
            reportEnvironmentEdit_->setPlaceholderText(
                QStringLiteral("仅在套件未配置环境说明时补缺"));
            metadataLayout->addRow(QStringLiteral("项目名称（system-observed）："), projectName);
            metadataLayout->addRow(QStringLiteral("测试人员（operator-entered，必填）："),
                                   reportTesterEdit_);
            metadataLayout->addRow(QStringLiteral("设备型号补充（operator-entered）："),
                                   reportDeviceModelEdit_);
            metadataLayout->addRow(QStringLiteral("测试台架补充（operator-entered）："),
                                   reportTestBenchEdit_);
            metadataLayout->addRow(QStringLiteral("环境说明补充（operator-entered）："),
                                   reportEnvironmentEdit_);
            reportLayout->addWidget(metadataGroup);

            auto *outputGroup = new QGroupBox(QStringLiteral("HTML 输出"), reportPage);
            auto *outputLayout = new QFormLayout(outputGroup);
            auto *directoryRow = new QWidget(outputGroup);
            auto *directoryLayout = new QHBoxLayout(directoryRow);
            directoryLayout->setContentsMargins(0, 0, 0, 0);
            reportOutputDirectoryEdit_ = new QLineEdit(directoryRow);
            reportOutputDirectoryEdit_->setObjectName(
                QStringLiteral("reportOutputDirectoryEdit"));
            reportOutputDirectoryEdit_->setText(
                oms555tv::report::ReportExportController::defaultOutputDirectory());
            reportBrowseDirectoryButton_ = new QPushButton(
                QStringLiteral("选择目录"), directoryRow);
            reportBrowseDirectoryButton_->setObjectName(
                QStringLiteral("reportBrowseDirectoryButton"));
            directoryLayout->addWidget(reportOutputDirectoryEdit_, 1);
            directoryLayout->addWidget(reportBrowseDirectoryButton_);
            reportFileNameEdit_ = new QLineEdit(outputGroup);
            reportFileNameEdit_->setObjectName(QStringLiteral("reportFileNameEdit"));
            outputLayout->addRow(QStringLiteral("输出目录："), directoryRow);
            outputLayout->addRow(QStringLiteral("文件名（不覆盖）："), reportFileNameEdit_);
            reportLayout->addWidget(outputGroup);

            auto *reportButtons = new QHBoxLayout;
            generateHtmlReportButton_ = new QPushButton(
                QStringLiteral("生成 HTML 报告"), reportPage);
            generateHtmlReportButton_->setObjectName(
                QStringLiteral("generateHtmlReportButton"));
            openHtmlReportButton_ = new QPushButton(
                QStringLiteral("打开报告"), reportPage);
            openHtmlReportButton_->setObjectName(QStringLiteral("openHtmlReportButton"));
            reportExportStateLabel_ = new QLabel(reportPage);
            reportExportStateLabel_->setObjectName(QStringLiteral("reportExportStateLabel"));
            reportButtons->addWidget(generateHtmlReportButton_);
            reportButtons->addWidget(openHtmlReportButton_);
            reportButtons->addWidget(reportExportStateLabel_);
            reportButtons->addStretch();
            reportLayout->addLayout(reportButtons);
            reportExportResultLabel_ = new QLabel(reportPage);
            reportExportResultLabel_->setObjectName(QStringLiteral("reportExportResultLabel"));
            reportExportResultLabel_->setWordWrap(true);
            reportExportResultLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
            reportLayout->addWidget(reportExportResultLabel_);
            reportLayout->addStretch();
            auto *reportScroll = new QScrollArea;
            reportScroll->setWidgetResizable(true);
            reportScroll->setWidget(reportPage);
            tabs->addTab(reportScroll, QStringLiteral("测试报告"));
        }

        auto *sessionPage = new QWidget;
        auto *sessionLayout = new QVBoxLayout(sessionPage);
        auto *sessionButtons = new QHBoxLayout;
        startSessionButton_ = new QPushButton(QStringLiteral("开始会话"));
        startSessionButton_->setObjectName(QStringLiteral("startSessionButton"));
        endSessionButton_ = new QPushButton(QStringLiteral("结束会话"));
        endSessionButton_->setObjectName(QStringLiteral("endSessionButton"));
        clearSessionLogButton_ = new QPushButton(QStringLiteral("清空 UI 日志"));
        sessionStateLabel_ = new QLabel;
        sessionStateLabel_->setObjectName(QStringLiteral("sessionStateLabel"));
        sessionButtons->addWidget(startSessionButton_);
        sessionButtons->addWidget(endSessionButton_);
        sessionButtons->addWidget(clearSessionLogButton_);
        sessionButtons->addWidget(sessionStateLabel_);
        sessionButtons->addStretch();
        sessionLayout->addLayout(sessionButtons);
        sessionPathLabel_ = new QLabel;
        sessionPathLabel_->setObjectName(QStringLiteral("sessionPathLabel"));
        sessionPathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sessionPathLabel_->setWordWrap(true);
        sessionErrorLabel_ = new QLabel;
        sessionErrorLabel_->setObjectName(QStringLiteral("sessionErrorLabel"));
        sessionErrorLabel_->setWordWrap(true);
        sessionLayout->addWidget(sessionPathLabel_);
        sessionLayout->addWidget(sessionErrorLabel_);
        sessionLogView_ = new QPlainTextEdit;
        sessionLogView_->setObjectName(QStringLiteral("sessionLogView"));
        sessionLogView_->setReadOnly(true);
        sessionLayout->addWidget(sessionLogView_, 1);
        tabs->addTab(sessionPage, QStringLiteral("会话日志"));
    }
    setCentralWidget(tabs);

    connect(&viewModel_, &oms555tv::ui::MonitoringViewModel::stateChanged,
            this, [this] { render(); renderConfiguration(); renderTesting(); });
    connect(refreshPortsButton_, &QPushButton::clicked,
            &viewModel_, &oms555tv::ui::MonitoringViewModel::refreshPorts);
    connect(portCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) viewModel_.setSelectedPortName(portCombo_->itemData(index).toString());
    });
    connect(slaveAddressSpin_, &QSpinBox::valueChanged,
            &viewModel_, &oms555tv::ui::MonitoringViewModel::setSlaveAddress);
    connect(timeoutSpin_, &QSpinBox::valueChanged,
            &viewModel_, &oms555tv::ui::MonitoringViewModel::setResponseTimeoutMs);
    connect(periodCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) viewModel_.setTargetPeriodMs(periodCombo_->itemData(index).toInt());
    });
    connect(connectButton_, &QPushButton::clicked,
            &viewModel_, &oms555tv::ui::MonitoringViewModel::connectDevice);
    connect(disconnectButton_, &QPushButton::clicked,
            &viewModel_, &oms555tv::ui::MonitoringViewModel::disconnectDevice);
    connect(startMonitoringButton_, &QPushButton::clicked,
            &viewModel_, &oms555tv::ui::MonitoringViewModel::startMonitoring);
    connect(stopMonitoringButton_, &QPushButton::clicked,
            &viewModel_, &oms555tv::ui::MonitoringViewModel::stopMonitoring);
    connect(recoverButton_, &QPushButton::clicked,
            &viewModel_, &oms555tv::ui::MonitoringViewModel::recover);

    if (configuration_ && diagnostics_ && sessionLog_) {
        connect(configuration_, &oms555tv::configuration::ConfigurationService::stateChanged,
                this, [this] { render(); renderConfiguration(); });
        connect(configuration_, &oms555tv::configuration::ConfigurationService::thresholdsChanged,
                this, [this] { renderConfiguration(); });
        connect(configuration_, &oms555tv::configuration::ConfigurationService::operationCompleted,
                this, [this] { renderConfiguration(); });
        connect(readThresholdsButton_, &QPushButton::clicked, this, [this] {
            const auto submission = configuration_->readThresholds();
            if (submission.rejection) configurationResult_->setPlainText(submission.rejection->diagnostic);
        });
        connect(writeThresholdsButton_, &QPushButton::clicked, this, [this] {
            oms555tv::configuration::ThresholdValues values{};
            for (int index = 0; index < 4; ++index) values[index] = thresholdSpins_[index]->value();
            const auto submission = configuration_->writeThresholds(values);
            if (submission.rejection) configurationResult_->setPlainText(submission.rejection->diagnostic);
        });
        connect(cancelConfigurationButton_, &QPushButton::clicked,
                configuration_, &oms555tv::configuration::ConfigurationService::cancel);
        connect(diagnostics_, &oms555tv::diagnostics::CommunicationDiagnosticsModel::recordsChanged,
                this, &MainWindow::renderDiagnostics);
        connect(diagnosticLevelFilter_, &QComboBox::currentIndexChanged,
                this, [this] { renderDiagnostics(); });
        connect(diagnosticResultFilter_, &QComboBox::currentIndexChanged,
                this, [this] { renderDiagnostics(); });
        connect(diagnosticRequestFilter_, &QLineEdit::textChanged,
                this, [this] { renderDiagnostics(); });
        connect(clearDiagnosticsButton_, &QPushButton::clicked,
                diagnostics_, &oms555tv::diagnostics::CommunicationDiagnosticsModel::clear);
        connect(diagnosticTable_, &QTableWidget::itemSelectionChanged,
                this, &MainWindow::renderDiagnosticDetails);
        connect(sessionLog_, &oms555tv::logging::SessionLogService::entriesChanged,
                this, &MainWindow::renderSessionLog);
        connect(sessionLog_, &oms555tv::logging::SessionLogService::sessionStateChanged,
                this, [this] { renderSessionLog(); });
        connect(sessionLog_, &oms555tv::logging::SessionLogService::fileError,
                this, [this] { renderSessionLog(); });
        connect(startSessionButton_, &QPushButton::clicked, this, [this] {
            sessionLog_->startSession({
                {QStringLiteral("application"), QCoreApplication::applicationName()},
                {QStringLiteral("version"), QCoreApplication::applicationVersion()},
            });
            renderSessionLog();
        });
        connect(endSessionButton_, &QPushButton::clicked, this, [this] {
            sessionLog_->endSession();
            renderSessionLog();
        });
        connect(clearSessionLogButton_, &QPushButton::clicked,
                sessionLog_, &oms555tv::logging::SessionLogService::clearMemory);
        if (automation_) {
            connect(automation_, &oms555tv::testing::TestAutomationController::stateChanged,
                    this, [this] {
                render();
                renderConfiguration();
                renderDiagnostics();
                renderTesting();
            });
            connect(automation_, &oms555tv::testing::TestAutomationController::suiteChanged,
                    this, &MainWindow::renderTesting);
            connect(automation_, &oms555tv::testing::TestAutomationController::currentStepChanged,
                    this, &MainWindow::renderTesting);
            connect(browseTestSuiteButton_, &QPushButton::clicked, this, [this] {
                const QString path = QFileDialog::getOpenFileName(
                    this, QStringLiteral("选择测试套件"), testSuitePath_->text(),
                    QStringLiteral("JSON 测试套件 (*.json)"));
                if (!path.isEmpty()) testSuitePath_->setText(path);
            });
            connect(loadTestSuiteButton_, &QPushButton::clicked, this, [this] {
                (void)automation_->loadSuiteFile(testSuitePath_->text());
            });
            connect(resumeMonitoringCheck_, &QCheckBox::toggled,
                    automation_, &oms555tv::testing::TestAutomationController::setResumeMonitoring);
            const auto monitorConfig = [this] {
                oms555tv::monitor::MonitorConfig config;
                config.targetPeriod = std::chrono::milliseconds(viewModel_.state().targetPeriodMs);
                config.requestTimeout = std::chrono::milliseconds(viewModel_.state().responseTimeoutMs);
                return config;
            };
            connect(runAllTestsButton_, &QPushButton::clicked, this, [this, monitorConfig] {
                (void)automation_->runAll(monitorConfig());
            });
            connect(runSelectedTestsButton_, &QPushButton::clicked,
                    this, [this, monitorConfig] {
                QSet<QString> selected;
                for (const auto &index : testCaseTable_->selectionModel()->selectedRows(0)) {
                    selected.insert(index.data().toString());
                }
                (void)automation_->runSelected(selected, monitorConfig());
            });
            connect(skipTestButton_, &QPushButton::clicked, this, [this] {
                const auto rows = testCaseTable_->selectionModel()->selectedRows(0);
                if (!rows.isEmpty()) (void)automation_->skipCase(rows.front().data().toString());
            });
            connect(abortTestsButton_, &QPushButton::clicked,
                    automation_, &oms555tv::testing::TestAutomationController::abort);
            connect(guidedConfirmButton_, &QPushButton::clicked, this, [this] {
                const auto guided = automation_->guidedView();
                (void)automation_->confirmGuidedAction(guided.token);
            });
            connect(guidedCancelButton_, &QPushButton::clicked, this, [this] {
                const auto guided = automation_->guidedView();
                (void)automation_->cancelGuidedAction(guided.token);
            });
            connect(testCaseTable_, &QTableWidget::itemSelectionChanged,
                    this, &MainWindow::renderTestDetails);
        }
        if (reportExport_) {
            connect(reportExport_, &oms555tv::report::ReportExportController::stateChanged,
                    this, &MainWindow::renderReport);
            connect(reportTesterEdit_, &QLineEdit::textChanged,
                    this, &MainWindow::renderReport);
            connect(reportBrowseDirectoryButton_, &QPushButton::clicked, this, [this] {
                const QString path = QFileDialog::getExistingDirectory(
                    this, QStringLiteral("选择报告输出目录"),
                    reportOutputDirectoryEdit_->text());
                if (!path.isEmpty()) reportOutputDirectoryEdit_->setText(path);
            });
            connect(generateHtmlReportButton_, &QPushButton::clicked, this, [this] {
                oms555tv::report::ReportExportRequest request;
                request.operatorMetadata.tester = reportTesterEdit_->text();
                request.operatorMetadata.deviceModel = reportDeviceModelEdit_->text();
                request.operatorMetadata.testBench = reportTestBenchEdit_->text();
                request.operatorMetadata.environmentDescription =
                    reportEnvironmentEdit_->toPlainText();
                request.outputDirectory = reportOutputDirectoryEdit_->text();
                request.fileName = reportFileNameEdit_->text();
                (void)reportExport_->exportHtml(request);
            });
            connect(openHtmlReportButton_, &QPushButton::clicked, this, [this] {
                const auto &outcome = reportExport_->lastOutcome();
                if (outcome.succeeded()
                    && !QDesktopServices::openUrl(QUrl::fromLocalFile(*outcome.filePath))) {
                    reportExportResultLabel_->setText(
                        QStringLiteral("报告已生成，但无法调用系统浏览器打开：%1")
                            .arg(*outcome.filePath));
                }
            });
        }
    }
    render();
    renderConfiguration();
    renderDiagnostics();
    renderSessionLog();
    renderTesting();
    renderReport();
}

QLabel *MainWindow::makeValueLabel(const QString &objectName)
{
    auto *label = new QLabel(this);
    label->setObjectName(objectName);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

void MainWindow::render()
{
    const auto &state = viewModel_.state();
    const bool testBusy = automation_ && automation_->busy();
    const QSignalBlocker portBlocker(portCombo_);
    portCombo_->clear();
    for (const auto &port : state.ports) portCombo_->addItem(port.displayName(), port.portName);
    portCombo_->setCurrentIndex(portCombo_->findData(state.selectedPortName));
    const QSignalBlocker slaveBlocker(slaveAddressSpin_);
    const QSignalBlocker timeoutBlocker(timeoutSpin_);
    const QSignalBlocker periodBlocker(periodCombo_);
    slaveAddressSpin_->setValue(state.slaveAddress);
    timeoutSpin_->setValue(state.responseTimeoutMs);
    periodCombo_->setCurrentIndex(periodCombo_->findData(state.targetPeriodMs));
    portCombo_->setEnabled(state.connectionFieldsEnabled && !testBusy);
    slaveAddressSpin_->setEnabled(state.connectionFieldsEnabled && !testBusy);
    timeoutSpin_->setEnabled(state.connectionFieldsEnabled && !testBusy);
    refreshPortsButton_->setEnabled(state.refreshPortsEnabled && !testBusy);
    periodCombo_->setEnabled(state.targetPeriodEnabled && !testBusy);
    connectButton_->setEnabled(state.connectEnabled && !testBusy);
    disconnectButton_->setEnabled(state.disconnectEnabled && !testBusy
                                  && (!configuration_ || !configuration_->busy()));
    startMonitoringButton_->setEnabled(state.startMonitoringEnabled && !testBusy
                                      && (!configuration_ || !configuration_->busy()));
    stopMonitoringButton_->setEnabled(state.stopMonitoringEnabled && !testBusy);
    recoverButton_->setEnabled(state.recoverEnabled && !testBusy);
    appStateLabel_->setText(state.appStateText);
    connectionStateLabel_->setText(state.connectionStateText);
    healthLabel_->setText(state.deviceHealthText);
    freshnessLabel_->setText(state.dataFreshnessText);
    lastSuccessfulLabel_->setText(state.lastSuccessfulText);
    lastErrorLabel_->setText(state.lastErrorText);
    effectivePeriodLabel_->setText(state.effectivePeriodText);
    overrunLabel_->setText(state.overrunText);
    phaseATemperatureLabel_->setText(state.phaseATemperatureText);
    phaseBTemperatureLabel_->setText(state.phaseBTemperatureText);
    phaseCTemperatureLabel_->setText(state.phaseCTemperatureText);
    ambientTemperatureLabel_->setText(state.ambientTemperatureText);
    lightMillivoltsLabel_->setText(state.lightMillivoltsText);
    phaseAAlarmLabel_->setText(state.phaseAAlarmText);
    phaseBAlarmLabel_->setText(state.phaseBAlarmText);
    phaseCAlarmLabel_->setText(state.phaseCAlarmText);
    ambientAlarmLabel_->setText(state.ambientAlarmText);
    deviceStatusLabel_->setText(state.deviceStatusText);
    firmwareVersionLabel_->setText(state.firmwareVersionText);
    uptimeLabel_->setText(state.uptimeText);
    requestsLabel_->setText(state.requestsText);
    succeededLabel_->setText(state.succeededText);
    failedLabel_->setText(state.failedText);
    timedOutLabel_->setText(state.timedOutText);
    successRateLabel_->setText(state.successRateText);
    rttLabel_->setText(state.rttText);
    const bool stale = state.dataFreshnessText.startsWith(QStringLiteral("陈旧"));
    freshnessLabel_->setStyleSheet(stale
        ? QStringLiteral("font-weight: bold; color: #a14c00;")
        : QStringLiteral("font-weight: bold;"));
    const bool abnormal = state.deviceHealthText.contains(QStringLiteral("退化"))
        || state.deviceHealthText.contains(QStringLiteral("离线"));
    healthLabel_->setStyleSheet(abnormal
        ? QStringLiteral("font-weight: bold; color: #a00000;")
        : QStringLiteral("font-weight: bold;"));
}

void MainWindow::renderConfiguration()
{
    if (!configuration_) return;
    const bool idle = viewModel_.state().appState == oms555tv::app::AppState::ConnectedIdle;
    const bool busy = configuration_->busy();
    const bool testBusy = automation_ && automation_->busy();
    readThresholdsButton_->setEnabled(idle && !busy && !testBusy);
    writeThresholdsButton_->setEnabled(idle && !busy && !testBusy);
    cancelConfigurationButton_->setEnabled(busy);
    for (auto *spin : thresholdSpins_) spin->setEnabled(idle && !busy && !testBusy);
    configurationStateLabel_->setText(configurationStateText(configuration_->state()));
    if (configuration_->thresholds()) {
        const auto values = thresholdArray(*configuration_->thresholds());
        for (int index = 0; index < 4; ++index) {
            thresholdCurrentLabels_[index]->setText(
                QStringLiteral("%1 ℃").arg(values[index].celsius(), 0, 'f', 1));
            if (!busy) {
                const QSignalBlocker blocker(thresholdSpins_[index]);
                thresholdSpins_[index]->setValue(values[index].celsius());
            }
        }
    }
    if (!configuration_->lastResult()) return;
    const auto &result = *configuration_->lastResult();
    QStringList lines;
    lines << QStringLiteral("操作 %1：%2")
        .arg(result.operationId.value)
        .arg(result.succeeded ? QStringLiteral("成功")
                              : result.cancelled ? QStringLiteral("已取消")
                                                 : QStringLiteral("失败/部分失败"));
    for (const auto &item : result.items) {
        QString line = QStringLiteral("%1：%2，原值 %3 ℃，期望 %4 ℃")
            .arg(channelText(item.channel), itemStatusText(item.status))
            .arg(item.before.celsius(), 0, 'f', 1)
            .arg(item.expected.celsius(), 0, 'f', 1);
        if (item.actual) line += QStringLiteral("，实际 %1 ℃").arg(item.actual->celsius(), 0, 'f', 1);
        if (item.writeRequestId) line += QStringLiteral("，写 RequestId=%1").arg(item.writeRequestId->value);
        if (item.readbackRequestId) line += QStringLiteral("，回读 RequestId=%1").arg(item.readbackRequestId->value);
        lines << line;
    }
    if (result.error && !result.error->diagnostic.isEmpty()) lines << result.error->diagnostic;
    configurationResult_->setPlainText(lines.join(QLatin1Char('\n')));
}

void MainWindow::renderDiagnostics()
{
    if (!diagnostics_) return;
    if (clearDiagnosticsButton_) {
        clearDiagnosticsButton_->setEnabled(!automation_ || !automation_->busy());
    }
    oms555tv::diagnostics::DiagnosticFilter filter;
    const int levelIndex = diagnosticLevelFilter_->currentIndex();
    if (levelIndex > 0) filter.level = static_cast<oms555tv::logging::LogLevel>(levelIndex - 1);
    if (diagnosticResultFilter_->currentIndex() == 1) filter.succeeded = true;
    if (diagnosticResultFilter_->currentIndex() == 2) filter.succeeded = false;
    const QString requestText = diagnosticRequestFilter_->text().trimmed();
    if (!requestText.isEmpty()) {
        bool ok = false;
        const quint64 requestId = requestText.toULongLong(&ok);
        filter.requestId = oms555tv::communication::RequestId{ok ? requestId : 0};
    }
    const auto records = diagnostics_->filtered(filter);
    diagnosticTable_->setRowCount(records.size());
    for (int row = 0; row < records.size(); ++row) {
        const auto &result = records[row].result;
        const auto &evidence = result.evidence;
        const QString rtt = evidence.rtt
            ? QStringLiteral("%1 ms").arg(
                  std::chrono::duration<double, std::milli>(*evidence.rtt).count(), 0, 'f', 3)
            : QStringLiteral("--");
        const QStringList cells{
            evidence.completedUtc.toLocalTime().toString(QStringLiteral("HH:mm:ss.zzz")),
            QString::number(result.requestId.value),
            oms555tv::diagnostics::ownerName(evidence.owner),
            oms555tv::diagnostics::requestStateName(result.state),
            QStringLiteral("0x%1").arg(evidence.functionCode, 2, 16, QLatin1Char('0')).toUpper(),
            oms555tv::diagnostics::descriptorSummary(result.descriptor),
            rtt,
            result.error ? QString::number(static_cast<int>(result.error->code)) : QStringLiteral("--"),
        };
        for (int column = 0; column < cells.size(); ++column) {
            auto *item = new QTableWidgetItem(cells[column]);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(result.requestId.value));
            diagnosticTable_->setItem(row, column, item);
        }
    }
    diagnosticTable_->resizeColumnsToContents();
    renderDiagnosticDetails();
}

void MainWindow::renderDiagnosticDetails()
{
    if (!diagnostics_ || !diagnosticTable_->currentItem()) {
        if (diagnosticDetails_) diagnosticDetails_->clear();
        return;
    }
    const quint64 id = diagnosticTable_->currentItem()->data(Qt::UserRole).toULongLong();
    for (auto iterator = diagnostics_->records().crbegin();
         iterator != diagnostics_->records().crend(); ++iterator) {
        if (iterator->result.requestId.value == id) {
            diagnosticDetails_->setPlainText(
                oms555tv::diagnostics::diagnosticDetails(*iterator));
            return;
        }
    }
    diagnosticDetails_->clear();
}

void MainWindow::renderSessionLog()
{
    if (!sessionLog_) return;
    startSessionButton_->setEnabled(!sessionLog_->active());
    endSessionButton_->setEnabled(sessionLog_->active());
    sessionStateLabel_->setText(sessionLog_->active()
        ? QStringLiteral("会话活动中：%1").arg(sessionLog_->sessionId())
        : QStringLiteral("无活动会话"));
    sessionPathLabel_->setText(sessionLog_->currentFilePath().isEmpty()
        ? QStringLiteral("文件：尚未创建")
        : QStringLiteral("文件：%1").arg(sessionLog_->currentFilePath()));
    sessionErrorLabel_->setText(sessionLog_->lastError()
        ? QStringLiteral("文件错误：%1").arg(sessionLog_->lastError()->diagnostic)
        : QStringLiteral("文件错误：无"));
    QStringList lines;
    lines.reserve(sessionLog_->entries().size());
    for (const auto &entry : sessionLog_->entries()) {
        lines << oms555tv::logging::formatLogEntry(entry);
    }
    sessionLogView_->setPlainText(lines.join(QLatin1Char('\n')));
    sessionLogView_->moveCursor(QTextCursor::End);
}

void MainWindow::renderTesting()
{
    if (!automation_ || !testCaseTable_) return;
    const bool busy = automation_->busy();
    const bool runnableState = viewModel_.state().appState == oms555tv::app::AppState::ConnectedIdle
        || viewModel_.state().appState == oms555tv::app::AppState::Monitoring;
    testSuitePath_->setEnabled(!busy);
    browseTestSuiteButton_->setEnabled(!busy);
    loadTestSuiteButton_->setEnabled(!busy);
    resumeMonitoringCheck_->setEnabled(!busy);
    {
        const QSignalBlocker blocker(resumeMonitoringCheck_);
        resumeMonitoringCheck_->setChecked(automation_->resumeMonitoring());
    }
    if (!automation_->suitePath().isEmpty() && !testSuitePath_->hasFocus()) {
        testSuitePath_->setText(automation_->suitePath());
    }
    const bool hasSuite = automation_->suite().has_value();
    const bool configurationBusy = configuration_ && configuration_->busy();
    runAllTestsButton_->setEnabled(hasSuite && !busy && !configurationBusy && runnableState);
    runSelectedTestsButton_->setEnabled(
        hasSuite && !busy && !configurationBusy && runnableState);
    skipTestButton_->setEnabled(
        automation_->state() == oms555tv::testing::TestAutomationState::Running
        && !automation_->guidedRunActive());
    abortTestsButton_->setEnabled(
        automation_->state() == oms555tv::testing::TestAutomationState::Running);
    testWorkflowStateLabel_->setText(
        oms555tv::testing::testAutomationStateName(automation_->state()));

    const auto guided = automation_->guidedView();
    guidedPanel_->setVisible(guided.visible);
    if (guided.visible) {
        guidedPromptTitleLabel_->setText(
            QStringLiteral("%1（%2）").arg(
                guided.title.isEmpty()
                    ? oms555tv::testing::guidedRunStateName(guided.state)
                    : guided.title,
                oms555tv::testing::guidedRunStateName(guided.state)));
        guidedInstructionLabel_->setText(
            guided.instruction.isEmpty() ? QStringLiteral("自动观察正在进行，请勿改变接线。")
                                         : guided.instruction);
        guidedSafetyLabel_->setText(
            guided.safetyNotice.isEmpty() ? QStringLiteral("安全提示：按当前步骤操作。")
                                          : QStringLiteral("安全提示：%1").arg(guided.safetyNotice));
        guidedCountdownLabel_->setText(
            QStringLiteral("剩余时间：%1 ms").arg(guided.remaining.count()));
        guidedObservationProgressLabel_->setText(
            guided.requiredConsecutiveMatches > 0
                ? QStringLiteral("连续样本：%1/%2；RequestId=%3")
                      .arg(guided.consecutiveMatches)
                      .arg(guided.requiredConsecutiveMatches)
                      .arg(guided.currentRequestId
                               ? QString::number(guided.currentRequestId->value)
                               : QStringLiteral("--"))
                : QStringLiteral("连续样本：--"));
        const auto timingText = [](const std::optional<std::chrono::milliseconds> &value) {
            return value ? QString::number(value->count()) + QStringLiteral(" ms")
                         : QStringLiteral("--");
        };
        const std::optional<std::chrono::milliseconds> first = guided.recoveryTiming
            ? std::optional(guided.recoveryTiming->toFirstSuccessfulResponse) : std::nullopt;
        const std::optional<std::chrono::milliseconds> stable = guided.recoveryTiming
            ? std::optional(guided.recoveryTiming->toStableRecovery) : std::nullopt;
        guidedRecoveryTimingLabel_->setText(
            QStringLiteral("恢复耗时：首次响应 %1；稳定恢复 %2")
                .arg(timingText(first), timingText(stable)));
        guidedRestorationReminderLabel_->setText(
            guided.restorationReminder.isEmpty()
                ? QString{} : QStringLiteral("接线恢复提醒：%1（软件尚未观察到稳定恢复）")
                                  .arg(guided.restorationReminder));
        const bool awaiting = guided.state
                == oms555tv::testing::GuidedRunState::WaitingForDisconnectConfirmation
            || guided.state
                == oms555tv::testing::GuidedRunState::WaitingForReconnectConfirmation;
        guidedConfirmButton_->setEnabled(
            awaiting && guided.allowedActions.contains(
                            oms555tv::testing::GuidedOperatorAction::Confirm));
        guidedCancelButton_->setEnabled(
            awaiting && guided.allowedActions.contains(
                            oms555tv::testing::GuidedOperatorAction::Cancel));
    }

    QStringList errorLines;
    if (!automation_->lastError().isEmpty()) errorLines << automation_->lastError();
    for (const auto &error : automation_->loadErrors()) {
        errorLines << QStringLiteral("%1 %2 suite=%3 case=%4：%5")
            .arg(oms555tv::testing::configErrorCodeName(error.code), error.path,
                 error.suiteId.isEmpty() ? QStringLiteral("--") : error.suiteId,
                 error.caseId.isEmpty() ? QStringLiteral("--") : error.caseId,
                 error.diagnostic);
    }
    testLoadErrors_->setPlainText(errorLines.join(QLatin1Char('\n')));

    if (!hasSuite) {
        testSuiteSummaryLabel_->setText(QStringLiteral("未加载有效套件"));
        testCaseTable_->setRowCount(0);
        testProgressLabel_->setText(QStringLiteral("0/0"));
        testStatisticsLabel_->setText(
            QStringLiteral("PASS 0 / FAIL 0 / ERROR 0 / SKIPPED 0 / NOT_RUN 0"));
        testCurrentStepLabel_->setText(QStringLiteral("--"));
        renderTestDetails();
        return;
    }

    const auto &suite = *automation_->suite();
    testSuiteSummaryLabel_->setText(QStringLiteral("%1（%2）— %3 条用例；%4")
        .arg(suite.name, suite.id).arg(suite.cases.size()).arg(suite.description));
    const auto snapshot = automation_->resultSnapshot();
    const bool snapshotMatches = snapshot && snapshot->suite
        && snapshot->suite->id == suite.id
        && snapshot->cases.size() == suite.cases.size();

    QSet<QString> selectedIds;
    for (const auto &index : testCaseTable_->selectionModel()->selectedRows(0)) {
        selectedIds.insert(index.data().toString());
    }
    testCaseTable_->clearSelection();
    testCaseTable_->setRowCount(suite.cases.size());
    int pass = 0;
    int fail = 0;
    int error = 0;
    int skipped = 0;
    int notRun = 0;
    int terminal = 0;
    for (int row = 0; row < suite.cases.size(); ++row) {
        const auto &testCase = suite.cases[row];
        const oms555tv::testing::TestCaseResult *result = snapshotMatches
            ? &snapshot->cases[row] : nullptr;
        const auto status = result ? result->status : oms555tv::testing::TestStatus::NotRun;
        switch (status) {
        case oms555tv::testing::TestStatus::Pass: ++pass; ++terminal; break;
        case oms555tv::testing::TestStatus::Fail: ++fail; ++terminal; break;
        case oms555tv::testing::TestStatus::Error: ++error; ++terminal; break;
        case oms555tv::testing::TestStatus::Skipped: ++skipped; ++terminal; break;
        case oms555tv::testing::TestStatus::NotRun: ++notRun; break;
        case oms555tv::testing::TestStatus::Running: break;
        }
        const QStringList cells{
            testCase.id,
            testCase.name,
            testCase.category,
            oms555tv::testing::testCaseTypeName(testCase.declaredType),
            oms555tv::testing::testStatusName(status),
            testRequestText(testCase),
            testExpectedText(testCase.expected),
            result ? testResultActualText(*result) : QStringLiteral("--"),
            result && result->finishedUtc.isValid()
                ? QStringLiteral("%1 ms").arg(result->duration.count())
                : QStringLiteral("--"),
        };
        for (int column = 0; column < cells.size(); ++column) {
            auto *item = new QTableWidgetItem(cells[column]);
            item->setData(Qt::UserRole, testCase.id);
            testCaseTable_->setItem(row, column, item);
        }
        if (selectedIds.contains(testCase.id)) {
            testCaseTable_->selectionModel()->select(
                testCaseTable_->model()->index(row, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
    testCaseTable_->resizeColumnsToContents();
    testProgressLabel_->setText(QStringLiteral("%1/%2").arg(terminal).arg(suite.cases.size()));
    testStatisticsLabel_->setText(
        QStringLiteral("PASS %1 / FAIL %2 / ERROR %3 / SKIPPED %4 / NOT_RUN %5")
            .arg(pass).arg(fail).arg(error).arg(skipped).arg(notRun));
    if (automation_->currentCaseId().isEmpty()) {
        testCurrentStepLabel_->setText(QStringLiteral("--"));
    } else if (automation_->currentStep() && automation_->currentRequestId()) {
        QString logicalProgress;
        const auto currentCase = std::find_if(
            suite.cases.cbegin(), suite.cases.cend(), [this](const auto &candidate) {
                return candidate.id == automation_->currentCaseId();
            });
        if (currentCase != suite.cases.cend()
            && currentCase->declaredType == oms555tv::testing::TestCaseType::Sequence) {
            logicalProgress = QStringLiteral("，复合步骤=%1（%2/%3），重复=%4/%5")
                .arg(automation_->currentLogicalStepId().isEmpty()
                         ? QStringLiteral("--") : automation_->currentLogicalStepId())
                .arg(automation_->currentLogicalStepIndex() + 1)
                .arg(currentCase->sequence.steps.size())
                .arg(automation_->currentRepetition() + 1)
                .arg(currentCase->sequence.repeatCount);
        } else if (currentCase != suite.cases.cend()
                   && currentCase->declaredType
                       == oms555tv::testing::TestCaseType::Stability) {
            const auto interval = currentCase->stability.interval.count();
            const auto maximumIterations = interval > 0
                ? (currentCase->stability.duration.count() + interval - 1) / interval : 0;
            logicalProgress = QStringLiteral("，稳定性迭代=%1/%2")
                .arg(automation_->currentLogicalStepIndex() + 1)
                .arg(maximumIterations);
        }
        testCurrentStepLabel_->setText(QStringLiteral(
            "case=%1，步骤=%2%3，attempt=%4，RequestId=%5")
            .arg(automation_->currentCaseId(),
                 oms555tv::testing::testStepPurposeName(*automation_->currentStep()))
            .arg(logicalProgress)
            .arg(automation_->currentAttempt())
            .arg(automation_->currentRequestId()->value));
    } else {
        testCurrentStepLabel_->setText(
            QStringLiteral("case=%1，准备下一步骤").arg(automation_->currentCaseId()));
    }
    renderTestDetails();
}

void MainWindow::renderTestDetails()
{
    if (!automation_ || !testCaseDetails_ || !testCaseTable_
        || !testCaseTable_->currentItem() || !automation_->suite()) {
        if (testCaseDetails_) testCaseDetails_->clear();
        return;
    }
    const QString id = testCaseTable_->currentItem()->data(Qt::UserRole).toString();
    const auto &suite = *automation_->suite();
    qsizetype index = -1;
    for (qsizetype candidate = 0; candidate < suite.cases.size(); ++candidate) {
        if (suite.cases[candidate].id == id) {
            index = candidate;
            break;
        }
    }
    if (index < 0) {
        testCaseDetails_->clear();
        return;
    }
    const auto &testCase = suite.cases[index];
    QStringList lines{
        QStringLiteral("ID：%1").arg(testCase.id),
        QStringLiteral("名称：%1").arg(testCase.name),
        QStringLiteral("类别/类型：%1 / %2")
            .arg(testCase.category, oms555tv::testing::testCaseTypeName(testCase.declaredType)),
        QStringLiteral("执行环境：%1")
            .arg(oms555tv::testing::executionEnvironmentName(testCase.environment)),
        QStringLiteral("请求：%1").arg(testRequestText(testCase)),
        QStringLiteral("预期：%1").arg(testExpectedText(testCase.expected)),
    };
    const auto snapshot = automation_->resultSnapshot();
    if (snapshot && snapshot->suite && snapshot->suite->id == suite.id
        && index < snapshot->cases.size()) {
        const auto &result = snapshot->cases[index];
        lines << QStringLiteral("状态：%1").arg(oms555tv::testing::testStatusName(result.status));
        if (result.assertion) {
            lines << QStringLiteral("实际：%1").arg(result.assertion->actualSummary);
            lines << QStringLiteral("断言：%1").arg(result.assertion->expectedSummary);
            for (const auto &difference : result.assertion->differences) {
                lines << QStringLiteral("差异：%1").arg(difference.reason);
            }
        }
        if (result.error) {
            lines << QStringLiteral("主错误 %1：%2")
                .arg(oms555tv::testing::testErrorCodeName(result.error->code),
                     result.error->diagnostic);
        }
        if (result.cleanupError) {
            lines << QStringLiteral("清理错误 %1：%2")
                .arg(oms555tv::testing::testErrorCodeName(result.cleanupError->code),
                     result.cleanupError->diagnostic);
        }
        if (!result.steps.isEmpty()) {
            lines << QStringLiteral("\n=== 复合步骤（%1）===").arg(result.steps.size());
            for (const auto &step : result.steps) {
                QStringList sequences;
                for (const auto sequence : step.attemptSequences) {
                    sequences << QString::number(sequence);
                }
                lines << QStringLiteral("步骤 %1（index=%2，repetition=%3）：%4，%5 ms")
                    .arg(step.stepId).arg(step.stepIndex + 1).arg(step.repetition + 1)
                    .arg(oms555tv::testing::testStatusName(step.status))
                    .arg(step.duration.count());
                lines << QStringLiteral("  预期：%1").arg(testExpectedText(step.expected));
                if (step.assertion) {
                    lines << QStringLiteral("  实际：%1").arg(step.assertion->actualSummary);
                    for (const auto &difference : step.assertion->differences) {
                        lines << QStringLiteral("  差异：%1").arg(difference.reason);
                    }
                }
                if (step.error) {
                    lines << QStringLiteral("  错误 %1：%2")
                        .arg(oms555tv::testing::testErrorCodeName(step.error->code),
                             step.error->diagnostic);
                }
                lines << QStringLiteral("  attempt sequence：%1")
                    .arg(sequences.isEmpty() ? QStringLiteral("--")
                                             : sequences.join(QStringLiteral(", ")));
            }
        }
        if (result.stability) {
            const auto rttText = [](const std::optional<qint64> &value) {
                return value ? QString::number(*value) + QStringLiteral(" ms")
                             : QStringLiteral("--");
            };
            const auto &statistics = *result.stability;
            lines << QStringLiteral("\n=== 稳定性聚合统计 ===");
            lines << QStringLiteral("样本：total=%1 success=%2 failure=%3 timeout=%4")
                .arg(statistics.total).arg(statistics.successes)
                .arg(statistics.failures).arg(statistics.timeouts);
            lines << QStringLiteral("RTT：有效=%1 缺失=%2 min=%3 avg=%4 max=%5")
                .arg(statistics.validRttSamples).arg(statistics.missingRttSamples)
                .arg(rttText(statistics.minimumRttMs), rttText(statistics.averageRttMs),
                     rttText(statistics.maximumRttMs));
        }
        if (result.guidedRecovery) {
            const auto &guidedResult = *result.guidedRecovery;
            lines << QStringLiteral("\n=== 引导式恢复结果 ===");
            lines << QStringLiteral("终态：%1；原因：%2；物理链路已观察恢复：%3")
                .arg(oms555tv::testing::guidedRunStateName(guidedResult.finalState),
                     oms555tv::testing::guidedTerminalReasonName(
                         guidedResult.terminalReason),
                     guidedResult.physicalLinkRestored ? QStringLiteral("是")
                                                       : QStringLiteral("否"));
            if (guidedResult.recoveryTiming) {
                lines << QStringLiteral("恢复耗时：首次=%1 ms，稳定=%2 ms")
                    .arg(guidedResult.recoveryTiming->toFirstSuccessfulResponse.count())
                    .arg(guidedResult.recoveryTiming->toStableRecovery.count());
            }
            if (guidedResult.recoveryInstructionRequired) {
                lines << QStringLiteral("接线恢复提醒：%1")
                    .arg(guidedResult.recoveryInstruction);
            }
            for (const auto &action : guidedResult.operatorActions) {
                lines << QStringLiteral("人工步骤 %1：token=%2，动作=%3，等待=%4 ms")
                    .arg(action.stepId, action.oneTimeToken,
                         action.action
                             ? oms555tv::testing::guidedOperatorActionName(*action.action)
                             : QStringLiteral("未操作"))
                    .arg(action.waitDuration.count());
            }
            for (const auto &observation : guidedResult.observations) {
                lines << QStringLiteral("观察 %1：目标=%2，结果=%3，连续=%4/%5")
                    .arg(observation.stepId,
                         oms555tv::testing::guidedObservationTargetName(observation.target))
                    .arg(static_cast<int>(observation.outcome))
                    .arg(observation.achievedConsecutiveMatches)
                    .arg(observation.requiredConsecutiveMatches);
            }
        }
        const auto &retention = result.evidenceRetention;
        lines << QStringLiteral("\n=== 证据保留摘要 ===");
        lines << QStringLiteral("策略：%1；total=%2 retained=%3 dropped=%4")
            .arg(oms555tv::testing::testEvidenceRetentionPolicyName(retention.policy))
            .arg(retention.totalAttempts).arg(retention.retainedAttempts)
            .arg(retention.droppedAttempts);
        lines << QStringLiteral("失败：total=%1 retained=%2；容量=%3")
            .arg(retention.totalFailures).arg(retention.retainedFailures)
            .arg(retention.configuredLimit);
        lines << QStringLiteral("说明：%1")
            .arg(retention.description.isEmpty() ? QStringLiteral("--")
                                                 : retention.description);
        lines << QStringLiteral("Session ID：%1")
            .arg(result.sessionId.isEmpty() ? QStringLiteral("--") : result.sessionId);
        for (const auto &attempt : result.attempts) {
            lines << QStringLiteral("\n--- attempt %1 ---\n%2")
                .arg(attempt.sequence).arg(testAttemptDetails(attempt));
        }
    } else {
        lines << QStringLiteral("状态：NOT_RUN");
    }
    testCaseDetails_->setPlainText(lines.join(QLatin1Char('\n')));
}

void MainWindow::renderReport()
{
    if (!reportExport_ || !reportRunLabel_) return;

    const auto &preview = reportExport_->preview();
    if (!preview) {
        reportRunLabel_->setText(QStringLiteral("尚无完整测试结果"));
        reportSummaryLabel_->setText(QStringLiteral("--"));
        reportEvidenceLabel_->setText(QStringLiteral("证据：--"));
        reportConfiguredMetadataLabel_->setText(QStringLiteral("套件元数据：--"));
    } else {
        reportRunLabel_->setText(
            QStringLiteral("suite=%1（%2），run=%3，Session ID=%4，终态=%5\n"
                           "UTC：%6 ～ %7；耗时 %8 ms")
                .arg(preview->suiteId, preview->suiteName)
                .arg(preview->runId)
                .arg(preview->sessionId.isEmpty() ? QStringLiteral("未采集")
                                                   : preview->sessionId,
                     oms555tv::testing::testStatusName(preview->status),
                     preview->startedUtc, preview->finishedUtc)
                .arg(preview->durationMs));
        reportSummaryLabel_->setText(
            QStringLiteral("用例 %1：PASS %2 / FAIL %3 / ERROR %4 / SKIPPED %5")
                .arg(preview->total).arg(preview->passed).arg(preview->failed)
                .arg(preview->errors).arg(preview->skipped));
        reportEvidenceLabel_->setText(
            QStringLiteral("证据保留：%1").arg(preview->evidenceRetention));
        const auto configured = [](const QString &value) {
            return value.isEmpty()
                ? QStringLiteral("未配置（可由操作员补充）[unavailable]")
                : value + QStringLiteral(" [suite-configured]");
        };
        reportConfiguredMetadataLabel_->setText(
            QStringLiteral("测试对象：%1 [suite-configured]\n设备型号：%2\n"
                           "测试台架：%3\n环境说明：%4")
                .arg(preview->suiteName, configured(preview->configuredDeviceModel),
                     configured(preview->configuredTestBench),
                     configured(preview->configuredEnvironment)));
        const qulonglong boundRun = reportFileNameEdit_->property("reportRunId").toULongLong();
        if (boundRun != preview->runId) {
            reportFileNameEdit_->setText(preview->suggestedFileName);
            reportFileNameEdit_->setProperty("reportRunId",
                                             QVariant::fromValue<qulonglong>(preview->runId));
        }
    }

    QString gate;
    bool enabled = true;
    if (!preview) {
        gate = QStringLiteral("NoCompletedResult：尚无可导出的完整测试结果");
        enabled = false;
    } else if (automation_ && automation_->busy()) {
        gate = QStringLiteral("TestRunInProgress：测试工作流运行期间禁止导出");
        enabled = false;
    } else if (reportExport_->busy()) {
        gate = QStringLiteral("ExportInProgress：报告正在后台生成，请勿重复提交");
        enabled = false;
    } else if (reportTesterEdit_->text().trimmed().isEmpty()) {
        gate = QStringLiteral("MissingRequiredMetadata：测试人员为必填项");
        enabled = false;
    } else {
        gate = QStringLiteral("READY：元数据有效，可一键生成 HTML 报告");
    }
    reportGateLabel_->setText(gate);
    generateHtmlReportButton_->setEnabled(enabled && reportExport_->canExport());
    reportTesterEdit_->setEnabled(!reportExport_->busy());
    reportDeviceModelEdit_->setEnabled(!reportExport_->busy());
    reportTestBenchEdit_->setEnabled(!reportExport_->busy());
    reportEnvironmentEdit_->setEnabled(!reportExport_->busy());
    reportOutputDirectoryEdit_->setEnabled(!reportExport_->busy());
    reportFileNameEdit_->setEnabled(!reportExport_->busy());
    reportBrowseDirectoryButton_->setEnabled(!reportExport_->busy());
    reportExportStateLabel_->setText(
        oms555tv::report::reportExportStateName(reportExport_->state()));

    const auto &outcome = reportExport_->lastOutcome();
    if (outcome.succeeded()) {
        reportExportResultLabel_->setText(
            QStringLiteral("成功：run=%1，%2 字节\n%3")
                .arg(outcome.runId).arg(outcome.fileSizeBytes).arg(*outcome.filePath));
    } else if (!outcome.errors.isEmpty()) {
        QStringList errors;
        for (const auto &error : outcome.errors) {
            errors << QStringLiteral("%1 %2：%3")
                .arg(oms555tv::report::reportErrorCodeName(error.code),
                     error.path, error.diagnostic);
        }
        reportExportResultLabel_->setText(errors.join(QLatin1Char('\n')));
    } else {
        reportExportResultLabel_->clear();
    }
    openHtmlReportButton_->setEnabled(outcome.succeeded());
}
