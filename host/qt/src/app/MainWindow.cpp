#include "app/MainWindow.h"

#include "configuration/ConfigurationService.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "ui/MonitoringViewModel.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
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
#include <QVBoxLayout>

#include <array>
#include <chrono>

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

std::array<oms555tv::device::Temperature, 4> thresholdArray(
    const oms555tv::device::AlarmThresholds &value)
{
    return {value.phaseA, value.phaseB, value.phaseC, value.ambient};
}

} // namespace

MainWindow::MainWindow(oms555tv::ui::MonitoringViewModel &viewModel,
                       QWidget *parent)
    : MainWindow(viewModel, nullptr, nullptr, nullptr, parent)
{
}

MainWindow::MainWindow(
    oms555tv::ui::MonitoringViewModel &viewModel,
    oms555tv::configuration::ConfigurationService &configuration,
    oms555tv::diagnostics::CommunicationDiagnosticsModel &diagnostics,
    oms555tv::logging::SessionLogService &sessionLog,
    QWidget *parent)
    : MainWindow(viewModel, &configuration, &diagnostics, &sessionLog, parent)
{
}

MainWindow::MainWindow(
    oms555tv::ui::MonitoringViewModel &viewModel,
    oms555tv::configuration::ConfigurationService *configuration,
    oms555tv::diagnostics::CommunicationDiagnosticsModel *diagnostics,
    oms555tv::logging::SessionLogService *sessionLog,
    QWidget *parent)
    : QMainWindow(parent)
    , viewModel_(viewModel)
    , configuration_(configuration)
    , diagnostics_(diagnostics)
    , sessionLog_(sessionLog)
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
            this, [this] { render(); renderConfiguration(); });
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
    }
    render();
    renderConfiguration();
    renderDiagnostics();
    renderSessionLog();
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
    portCombo_->setEnabled(state.connectionFieldsEnabled);
    slaveAddressSpin_->setEnabled(state.connectionFieldsEnabled);
    timeoutSpin_->setEnabled(state.connectionFieldsEnabled);
    refreshPortsButton_->setEnabled(state.refreshPortsEnabled);
    periodCombo_->setEnabled(state.targetPeriodEnabled);
    connectButton_->setEnabled(state.connectEnabled);
    disconnectButton_->setEnabled(state.disconnectEnabled && (!configuration_ || !configuration_->busy()));
    startMonitoringButton_->setEnabled(state.startMonitoringEnabled && (!configuration_ || !configuration_->busy()));
    stopMonitoringButton_->setEnabled(state.stopMonitoringEnabled);
    recoverButton_->setEnabled(state.recoverEnabled);
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
    readThresholdsButton_->setEnabled(idle && !busy);
    writeThresholdsButton_->setEnabled(idle && !busy);
    cancelConfigurationButton_->setEnabled(busy);
    for (auto *spin : thresholdSpins_) spin->setEnabled(idle && !busy);
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
