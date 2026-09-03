#include "communication/CommunicationTypes.h"

#include <QMetaType>
#include <QTest>

#include <limits>

using namespace oms555tv::communication;
using oms555tv::device::PduAddress;

class CommunicationTypesTest final : public QObject
{
    Q_OBJECT

private slots:
    void connectionConfigurationValidation()
    {
        ModbusConnectionConfig config;
        config.serial.portName = QStringLiteral(" COM7 ");
        QVERIFY(!validateConfig(config));

        config.serial.portName.clear();
        QCOMPARE(validateConfig(config)->code, ErrorCode::EmptyPortName);
        config.serial.portName = QStringLiteral("COM7");

        config.serial.baudRate = 0;
        QCOMPARE(validateConfig(config)->code, ErrorCode::UnsupportedConfiguration);
        config.serial.baudRate = 115200;
        config.serial.dataBits = 7;
        QCOMPARE(validateConfig(config)->code, ErrorCode::UnsupportedConfiguration);
        config.serial.dataBits = 8;
        config.serial.parity = static_cast<SerialParity>(99);
        QCOMPARE(validateConfig(config)->code, ErrorCode::UnsupportedConfiguration);
        config.serial.parity = SerialParity::Even;
        config.serial.stopBits = static_cast<SerialStopBits>(99);
        QCOMPARE(validateConfig(config)->code, ErrorCode::UnsupportedConfiguration);
        config.serial.stopBits = SerialStopBits::Two;
        QVERIFY(!validateConfig(config));

        for (const quint8 address : {quint8(0), quint8(248), quint8(255)}) {
            config.serverAddress = address;
            QCOMPARE(validateConfig(config)->code, ErrorCode::InvalidServerAddress);
        }
        config.serverAddress = 1;

        config.defaultResponseTimeout = std::chrono::milliseconds(0);
        QCOMPARE(validateConfig(config)->code, ErrorCode::InvalidTimeout);
        config.defaultResponseTimeout = std::chrono::milliseconds(60001);
        QCOMPARE(validateConfig(config)->code, ErrorCode::InvalidTimeout);
        config.defaultResponseTimeout = std::chrono::milliseconds(1);
        QVERIFY(!validateConfig(config));
        config.defaultResponseTimeout = std::chrono::milliseconds(60000);
        QVERIFY(!validateConfig(config));

        config.maxPendingRequests = 0;
        QCOMPARE(validateConfig(config)->code, ErrorCode::UnsupportedConfiguration);
        config.maxPendingRequests = 1025;
        QCOMPARE(validateConfig(config)->code, ErrorCode::UnsupportedConfiguration);
    }

    void readRequestBoundaries()
    {
        QCOMPARE(validateReadRequest(PduAddress(0), 0)->code, ErrorCode::InvalidQuantity);
        QVERIFY(!validateReadRequest(PduAddress(0), 1));
        QVERIFY(!validateReadRequest(PduAddress(0), 125));
        QCOMPARE(validateReadRequest(PduAddress(0), 126)->code, ErrorCode::InvalidQuantity);
        QVERIFY(!validateReadRequest(PduAddress(0xFFFF), 1));
        QCOMPARE(validateReadRequest(PduAddress(0xFFFF), 2)->code,
                 ErrorCode::AddressRangeOverflow);
        QVERIFY(!validateReadRequest(PduAddress(0xFF83), 125));
        QCOMPARE(validateReadRequest(PduAddress(0xFF84), 125)->code,
                 ErrorCode::AddressRangeOverflow);
    }

    void requestTimeoutAndRemoteExceptionCodesAreStable()
    {
        RequestOptions options;
        options.responseTimeout = std::chrono::milliseconds(0);
        QCOMPARE(validateRequestOptions(options)->code, ErrorCode::InvalidTimeout);
        options.responseTimeout = std::chrono::milliseconds(1);
        QVERIFY(!validateRequestOptions(options));
        options.responseTimeout = std::chrono::milliseconds(60000);
        QVERIFY(!validateRequestOptions(options));

        QCOMPARE(remoteExceptionErrorCode(0x01), ErrorCode::ModbusIllegalFunction);
        QCOMPARE(remoteExceptionErrorCode(0x02), ErrorCode::ModbusIllegalDataAddress);
        QCOMPARE(remoteExceptionErrorCode(0x03), ErrorCode::ModbusIllegalDataValue);
        QCOMPARE(remoteExceptionErrorCode(0x04), ErrorCode::ModbusUnknownException);
        QCOMPARE(remoteExceptionErrorCode(0xFF), ErrorCode::ModbusUnknownException);
    }

    void signalPayloadTypesAreRegisteredAndCopyable()
    {
        QVERIFY(QMetaType::fromType<OperationId>().isValid());
        QVERIFY(QMetaType::fromType<RequestId>().isValid());
        QVERIFY(QMetaType::fromType<ControlResult>().isValid());
        QVERIFY(QMetaType::fromType<OwnershipResult>().isValid());
        QVERIFY(QMetaType::fromType<ModbusRequestResult>().isValid());

        RtuTransactionEvidence evidence;
        evidence.txAdu = QByteArray::fromHex("010300000001840a");
        const RtuTransactionEvidence copy = evidence;
        QCOMPARE(copy.txAdu, evidence.txAdu);
    }

    void idsAreMonotonicNonZeroAndNeverWrap()
    {
        MonotonicIdGenerator<RequestId> normal;
        QCOMPARE(normal.next()->value, quint64(1));
        QCOMPARE(normal.next()->value, quint64(2));

        MonotonicIdGenerator<OperationId> boundary(
            std::numeric_limits<quint64>::max() - 1U);
        QCOMPARE(boundary.next()->value, std::numeric_limits<quint64>::max() - 1U);
        QCOMPARE(boundary.next()->value, std::numeric_limits<quint64>::max());
        QVERIFY(!boundary.next().has_value());
        QVERIFY(!boundary.next().has_value());
    }
};

QTEST_APPLESS_MAIN(CommunicationTypesTest)

#include "tst_communicationtypes.moc"
