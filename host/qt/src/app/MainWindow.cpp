#include "app/MainWindow.h"

#include "ui/MonitoringViewModel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(oms555tv::ui::MonitoringViewModel &viewModel,
                       QWidget *parent)
    : QMainWindow(parent)
    , viewModel_(viewModel)
{
    setWindowTitle(QStringLiteral("OMS555TV 实时监控平台"));
    resize(1050, 800);

    auto *content = new QWidget;
    auto *root = new QVBoxLayout(content);
    root->setSpacing(10);

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
    connectionLayout->addWidget(new QLabel(QStringLiteral("串口："), connectionGroup), 0, 0);
    connectionLayout->addWidget(portCombo_, 0, 1, 1, 3);
    connectionLayout->addWidget(refreshPortsButton_, 0, 4);
    connectionLayout->addWidget(new QLabel(QStringLiteral("Slave ID："), connectionGroup), 1, 0);
    connectionLayout->addWidget(slaveAddressSpin_, 1, 1);
    connectionLayout->addWidget(new QLabel(QStringLiteral("响应超时："), connectionGroup), 1, 2);
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
    monitorLayout->addWidget(new QLabel(QStringLiteral("目标周期："), monitorGroup), 0, 0);
    monitorLayout->addWidget(periodCombo_, 0, 1);
    monitorLayout->addWidget(startMonitoringButton_, 0, 2);
    monitorLayout->addWidget(stopMonitoringButton_, 0, 3);
    monitorLayout->addWidget(new QLabel(QStringLiteral("应用状态："), monitorGroup), 1, 0);
    monitorLayout->addWidget(appStateLabel_, 1, 1);
    monitorLayout->addWidget(new QLabel(QStringLiteral("连接状态："), monitorGroup), 1, 2);
    monitorLayout->addWidget(connectionStateLabel_, 1, 3);
    monitorLayout->addWidget(new QLabel(QStringLiteral("设备健康："), monitorGroup), 2, 0);
    monitorLayout->addWidget(healthLabel_, 2, 1);
    monitorLayout->addWidget(new QLabel(QStringLiteral("数据有效性："), monitorGroup), 2, 2);
    monitorLayout->addWidget(freshnessLabel_, 2, 3);
    monitorLayout->addWidget(new QLabel(QStringLiteral("实际有效周期："), monitorGroup), 3, 0);
    monitorLayout->addWidget(effectivePeriodLabel_, 3, 1);
    monitorLayout->addWidget(new QLabel(QStringLiteral("周期超限："), monitorGroup), 3, 2);
    monitorLayout->addWidget(overrunLabel_, 3, 3);
    monitorLayout->addWidget(new QLabel(QStringLiteral("最后成功快照："), monitorGroup), 4, 0);
    monitorLayout->addWidget(lastSuccessfulLabel_, 4, 1, 1, 3);
    monitorLayout->addWidget(new QLabel(QStringLiteral("最近错误："), monitorGroup), 5, 0);
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
    statisticsLayout->addWidget(new QLabel(QStringLiteral("请求："), statisticsGroup), 0, 0);
    statisticsLayout->addWidget(requestsLabel_, 0, 1);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("成功："), statisticsGroup), 0, 2);
    statisticsLayout->addWidget(succeededLabel_, 0, 3);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("失败："), statisticsGroup), 0, 4);
    statisticsLayout->addWidget(failedLabel_, 0, 5);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("超时："), statisticsGroup), 1, 0);
    statisticsLayout->addWidget(timedOutLabel_, 1, 1);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("成功率："), statisticsGroup), 1, 2);
    statisticsLayout->addWidget(successRateLabel_, 1, 3);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("RTT："), statisticsGroup), 2, 0);
    statisticsLayout->addWidget(rttLabel_, 2, 1, 1, 5);
    root->addWidget(statisticsGroup);
    root->addStretch();

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(content);
    setCentralWidget(scrollArea);

    connect(&viewModel_, &oms555tv::ui::MonitoringViewModel::stateChanged,
            this, [this] { render(); });
    connect(refreshPortsButton_, &QPushButton::clicked,
            &viewModel_, &oms555tv::ui::MonitoringViewModel::refreshPorts);
    connect(portCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            viewModel_.setSelectedPortName(portCombo_->itemData(index).toString());
        }
    });
    connect(slaveAddressSpin_, &QSpinBox::valueChanged,
            &viewModel_, &oms555tv::ui::MonitoringViewModel::setSlaveAddress);
    connect(timeoutSpin_, &QSpinBox::valueChanged,
            &viewModel_, &oms555tv::ui::MonitoringViewModel::setResponseTimeoutMs);
    connect(periodCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            viewModel_.setTargetPeriodMs(periodCombo_->itemData(index).toInt());
        }
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
    render();
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
    for (const auto &port : state.ports) {
        portCombo_->addItem(port.displayName(), port.portName);
    }
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
    disconnectButton_->setEnabled(state.disconnectEnabled);
    startMonitoringButton_->setEnabled(state.startMonitoringEnabled);
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
