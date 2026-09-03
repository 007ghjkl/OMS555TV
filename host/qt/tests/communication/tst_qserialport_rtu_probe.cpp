#include <QIODevice>
#include <QSerialPort>
#include <QTest>

class QSerialPortRtuProbeTest final : public QObject
{
    Q_OBJECT

private slots:
    void publicApiExposesRawByteStreamAndAsyncSignals()
    {
        using WriteBytes = qint64 (QIODevice::*)(const QByteArray &);
        using ReadAllBytes = QByteArray (QIODevice::*)();
        using ReadyReadSignal = void (QIODevice::*)();
        using BytesWrittenSignal = void (QIODevice::*)(qint64);
        using ErrorSignal = void (QSerialPort::*)(QSerialPort::SerialPortError);

        const WriteBytes writeBytes = static_cast<WriteBytes>(&QIODevice::write);
        const ReadAllBytes readAllBytes = &QIODevice::readAll;
        const ReadyReadSignal readyRead = &QIODevice::readyRead;
        const BytesWrittenSignal bytesWritten = &QIODevice::bytesWritten;
        const ErrorSignal errorOccurred = &QSerialPort::errorOccurred;

        QVERIFY(writeBytes != nullptr);
        QVERIFY(readAllBytes != nullptr);
        QVERIFY(readyRead != nullptr);
        QVERIFY(bytesWritten != nullptr);
        QVERIFY(errorOccurred != nullptr);
    }

    void publicApiConfiguresTheMvpSerialParametersWithoutOpeningHardware()
    {
        QSerialPort port;
        port.setPortName(QStringLiteral("OMS555TV_API_PROBE"));

        QVERIFY(port.setBaudRate(QSerialPort::Baud115200));
        QVERIFY(port.setDataBits(QSerialPort::Data8));
        QVERIFY(port.setParity(QSerialPort::NoParity));
        QVERIFY(port.setStopBits(QSerialPort::OneStop));
        QVERIFY(port.setFlowControl(QSerialPort::NoFlowControl));

        QCOMPARE(port.portName(), QStringLiteral("OMS555TV_API_PROBE"));
        QCOMPARE(port.baudRate(), qint32(QSerialPort::Baud115200));
        QCOMPARE(port.dataBits(), QSerialPort::Data8);
        QCOMPARE(port.parity(), QSerialPort::NoParity);
        QCOMPARE(port.stopBits(), QSerialPort::OneStop);
        QCOMPARE(port.flowControl(), QSerialPort::NoFlowControl);
        QVERIFY(!port.isOpen());
    }
};

QTEST_APPLESS_MAIN(QSerialPortRtuProbeTest)

#include "tst_qserialport_rtu_probe.moc"
