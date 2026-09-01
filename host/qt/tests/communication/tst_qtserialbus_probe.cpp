#include <QModbusClient>
#include <QModbusPdu>
#include <QModbusReply>
#include <QModbusRequest>
#include <QTest>

class QtSerialBusProbeTest final : public QObject
{
    Q_OBJECT

private slots:
    void publicApiAcceptsRawPdu()
    {
        const QByteArray payload = QByteArray::fromHex("00000002");
        const QModbusRequest request(QModbusPdu::ReadHoldingRegisters, payload);

        QCOMPARE(request.functionCode(), QModbusPdu::ReadHoldingRegisters);
        QCOMPARE(request.data(), payload);

        using SendRawRequest = QModbusReply *(QModbusClient::*)(const QModbusRequest &, int);
        const SendRawRequest publicApi = &QModbusClient::sendRawRequest;
        QVERIFY(publicApi != nullptr);
    }
};

QTEST_APPLESS_MAIN(QtSerialBusProbeTest)

#include "tst_qtserialbus_probe.moc"
