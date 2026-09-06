#pragma once

#include <QMainWindow>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace oms555tv::configuration { class ConfigurationService; }
namespace oms555tv::diagnostics { class CommunicationDiagnosticsModel; }
namespace oms555tv::logging { class SessionLogService; }
namespace oms555tv::report { class ReportExportController; }
namespace oms555tv::testing { class TestAutomationController; }
namespace oms555tv::ui { class MonitoringViewModel; }

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(oms555tv::ui::MonitoringViewModel &viewModel,
                        QWidget *parent = nullptr);
    MainWindow(oms555tv::ui::MonitoringViewModel &viewModel,
               oms555tv::configuration::ConfigurationService &configuration,
               oms555tv::diagnostics::CommunicationDiagnosticsModel &diagnostics,
               oms555tv::logging::SessionLogService &sessionLog,
               QWidget *parent = nullptr);
    MainWindow(oms555tv::ui::MonitoringViewModel &viewModel,
               oms555tv::configuration::ConfigurationService &configuration,
               oms555tv::diagnostics::CommunicationDiagnosticsModel &diagnostics,
               oms555tv::logging::SessionLogService &sessionLog,
               oms555tv::testing::TestAutomationController &automation,
               QWidget *parent = nullptr);
    MainWindow(oms555tv::ui::MonitoringViewModel &viewModel,
               oms555tv::configuration::ConfigurationService &configuration,
               oms555tv::diagnostics::CommunicationDiagnosticsModel &diagnostics,
               oms555tv::logging::SessionLogService &sessionLog,
               oms555tv::testing::TestAutomationController &automation,
               oms555tv::report::ReportExportController &reportExport,
               QWidget *parent = nullptr);

private:
    MainWindow(oms555tv::ui::MonitoringViewModel &viewModel,
               oms555tv::configuration::ConfigurationService *configuration,
               oms555tv::diagnostics::CommunicationDiagnosticsModel *diagnostics,
               oms555tv::logging::SessionLogService *sessionLog,
               oms555tv::testing::TestAutomationController *automation,
               oms555tv::report::ReportExportController *reportExport,
               QWidget *parent);
    QLabel *makeValueLabel(const QString &objectName);
    void render();
    void renderConfiguration();
    void renderDiagnostics();
    void renderDiagnosticDetails();
    void renderSessionLog();
    void renderTesting();
    void renderTestDetails();
    void renderReport();

    oms555tv::ui::MonitoringViewModel &viewModel_;
    oms555tv::configuration::ConfigurationService *configuration_ = nullptr;
    oms555tv::diagnostics::CommunicationDiagnosticsModel *diagnostics_ = nullptr;
    oms555tv::logging::SessionLogService *sessionLog_ = nullptr;
    oms555tv::testing::TestAutomationController *automation_ = nullptr;
    oms555tv::report::ReportExportController *reportExport_ = nullptr;

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

    QDoubleSpinBox *thresholdSpins_[4]{};
    QLabel *thresholdCurrentLabels_[4]{};
    QPushButton *readThresholdsButton_ = nullptr;
    QPushButton *writeThresholdsButton_ = nullptr;
    QPushButton *cancelConfigurationButton_ = nullptr;
    QLabel *configurationStateLabel_ = nullptr;
    QPlainTextEdit *configurationResult_ = nullptr;

    QComboBox *diagnosticLevelFilter_ = nullptr;
    QComboBox *diagnosticResultFilter_ = nullptr;
    QLineEdit *diagnosticRequestFilter_ = nullptr;
    QPushButton *clearDiagnosticsButton_ = nullptr;
    QTableWidget *diagnosticTable_ = nullptr;
    QPlainTextEdit *diagnosticDetails_ = nullptr;

    QPushButton *startSessionButton_ = nullptr;
    QPushButton *endSessionButton_ = nullptr;
    QPushButton *clearSessionLogButton_ = nullptr;
    QLabel *sessionStateLabel_ = nullptr;
    QLabel *sessionPathLabel_ = nullptr;
    QLabel *sessionErrorLabel_ = nullptr;
    QPlainTextEdit *sessionLogView_ = nullptr;

    QLineEdit *testSuitePath_ = nullptr;
    QPushButton *browseTestSuiteButton_ = nullptr;
    QPushButton *loadTestSuiteButton_ = nullptr;
    QPushButton *runSelectedTestsButton_ = nullptr;
    QPushButton *runAllTestsButton_ = nullptr;
    QPushButton *skipTestButton_ = nullptr;
    QPushButton *abortTestsButton_ = nullptr;
    QCheckBox *resumeMonitoringCheck_ = nullptr;
    QLabel *testWorkflowStateLabel_ = nullptr;
    QLabel *testSuiteSummaryLabel_ = nullptr;
    QLabel *testProgressLabel_ = nullptr;
    QLabel *testCurrentStepLabel_ = nullptr;
    QLabel *testStatisticsLabel_ = nullptr;
    QGroupBox *guidedPanel_ = nullptr;
    QLabel *guidedPromptTitleLabel_ = nullptr;
    QLabel *guidedInstructionLabel_ = nullptr;
    QLabel *guidedSafetyLabel_ = nullptr;
    QLabel *guidedCountdownLabel_ = nullptr;
    QLabel *guidedObservationProgressLabel_ = nullptr;
    QLabel *guidedRecoveryTimingLabel_ = nullptr;
    QPushButton *guidedConfirmButton_ = nullptr;
    QPushButton *guidedCancelButton_ = nullptr;
    QLabel *guidedRestorationReminderLabel_ = nullptr;
    QPlainTextEdit *testLoadErrors_ = nullptr;
    QTableWidget *testCaseTable_ = nullptr;
    QPlainTextEdit *testCaseDetails_ = nullptr;

    QLabel *reportGateLabel_ = nullptr;
    QLabel *reportRunLabel_ = nullptr;
    QLabel *reportSummaryLabel_ = nullptr;
    QLabel *reportEvidenceLabel_ = nullptr;
    QLabel *reportConfiguredMetadataLabel_ = nullptr;
    QLineEdit *reportTesterEdit_ = nullptr;
    QLineEdit *reportDeviceModelEdit_ = nullptr;
    QLineEdit *reportTestBenchEdit_ = nullptr;
    QPlainTextEdit *reportEnvironmentEdit_ = nullptr;
    QLineEdit *reportOutputDirectoryEdit_ = nullptr;
    QLineEdit *reportFileNameEdit_ = nullptr;
    QPushButton *reportBrowseDirectoryButton_ = nullptr;
    QPushButton *generateHtmlReportButton_ = nullptr;
    QPushButton *openHtmlReportButton_ = nullptr;
    QLabel *reportExportStateLabel_ = nullptr;
    QLabel *reportExportResultLabel_ = nullptr;
};
