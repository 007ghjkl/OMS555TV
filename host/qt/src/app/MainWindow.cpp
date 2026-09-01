#include "app/MainWindow.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("OMS555TV 自动化测试验证平台"));
    resize(720, 420);

    auto *centralWidget = new QWidget(this);
    auto *layout = new QVBoxLayout(centralWidget);

    auto *title = new QLabel(QStringLiteral("OMS555TV 自动化测试验证平台"), centralWidget);
    QFont titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *phase = new QLabel(QStringLiteral("Phase 0 工程骨架"), centralWidget);
    auto *status = new QLabel(QStringLiteral("设备状态：未连接（本阶段不执行硬件通信）"), centralWidget);

    layout->addWidget(title);
    layout->addWidget(phase);
    layout->addWidget(status);
    layout->addStretch();

    setCentralWidget(centralWidget);
}
