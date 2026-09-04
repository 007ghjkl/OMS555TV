#pragma once

#include <QMainWindow>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace oms555tv::ui {
class MonitoringViewModel;
}

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(oms555tv::ui::MonitoringViewModel &viewModel,
                        QWidget *parent = nullptr);

private:
    QLabel *makeValueLabel(const QString &objectName);
    void render();

    oms555tv::ui::MonitoringViewModel &viewModel_;
    QComboBox *portCombo_ = nullptr;
    QComboBox *periodCombo_ = nullptr;
    QSpinBox *slaveAddressSpin_ = nullptr;
    QSpinBox *timeoutSpin_ = nullptr;
    QPushButton *refreshPortsButton_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QPushButton *disconnectButton_ = nullptr;
    QPushButton *startMonitoringButton_ = nullptr;
    QPushButton *stopMonitoringButton_ = nullptr;
    QPushButton *recoverButton_ = nullptr;
    QLabel *appStateLabel_ = nullptr;
    QLabel *connectionStateLabel_ = nullptr;
    QLabel *healthLabel_ = nullptr;
    QLabel *freshnessLabel_ = nullptr;
    QLabel *lastSuccessfulLabel_ = nullptr;
    QLabel *lastErrorLabel_ = nullptr;
    QLabel *effectivePeriodLabel_ = nullptr;
    QLabel *overrunLabel_ = nullptr;
    QLabel *phaseATemperatureLabel_ = nullptr;
    QLabel *phaseBTemperatureLabel_ = nullptr;
    QLabel *phaseCTemperatureLabel_ = nullptr;
    QLabel *ambientTemperatureLabel_ = nullptr;
    QLabel *lightMillivoltsLabel_ = nullptr;
    QLabel *phaseAAlarmLabel_ = nullptr;
    QLabel *phaseBAlarmLabel_ = nullptr;
    QLabel *phaseCAlarmLabel_ = nullptr;
    QLabel *ambientAlarmLabel_ = nullptr;
    QLabel *deviceStatusLabel_ = nullptr;
    QLabel *firmwareVersionLabel_ = nullptr;
    QLabel *uptimeLabel_ = nullptr;
    QLabel *requestsLabel_ = nullptr;
    QLabel *succeededLabel_ = nullptr;
    QLabel *failedLabel_ = nullptr;
    QLabel *timedOutLabel_ = nullptr;
    QLabel *successRateLabel_ = nullptr;
    QLabel *rttLabel_ = nullptr;
};
