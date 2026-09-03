#include "communication/RtuCodec.h"

#include <QTest>

using namespace oms555tv::communication;
using oms555tv::device::PduAddress;

namespace {

QByteArray frame(std::initializer_list<quint8> bytes)
{
    QByteArray result;
    for (const quint8 byte : bytes) {
        result.append(static_cast<char>(byte));
    }
    return appendModbusCrc(std::move(result));
}

RtuDecodeResult decodeRead(const QByteArray &bytes,
                           quint16 count = 2,
                           bool complete = true)
{
    return decodeResponseAdu(1,
                             ReadRequestDescriptor{PduAddress(0), count},
                             bytes,
                             complete);
}

} // namespace

class RtuCodecTest final : public QObject
{
    Q_OBJECT

private slots:
    void crcKnownVectorsAndMutation()
    {
        QCOMPARE(modbusCrc16({}), quint16(0xFFFF));
        const QByteArray requestPayload = QByteArray::fromHex("010300000001");
        QCOMPARE(modbusCrc16(requestPayload), quint16(0x0A84));
        QCOMPARE(appendModbusCrc(requestPayload), QByteArray::fromHex("010300000001840a"));
        QVERIFY(hasValidModbusCrc(QByteArray::fromHex("010300000001840a")));

        QByteArray changed = requestPayload;
        changed[5] = 2;
        QVERIFY(modbusCrc16(changed) != modbusCrc16(requestPayload));
        QVERIFY(!hasValidModbusCrc(QByteArray::fromHex("010300000001850a")));
        QVERIFY(!hasValidModbusCrc(QByteArray(1, '\0')));
    }

    void requestFramesUseStandardByteOrderAndTask005Vectors()
    {
        QCOMPARE(encodeRequestAdu(1, ReadRequestDescriptor{PduAddress(0), 1}),
                 QByteArray::fromHex("010300000001840a"));
        QCOMPARE(encodeRequestAdu(1, ReadRequestDescriptor{PduAddress(39), 2}),
                 QByteArray::fromHex("0103002700027400"));
        QCOMPARE(encodeRequestAdu(1, WriteRequestDescriptor{PduAddress(9), 550}),
                 QByteArray::fromHex("010600090226d972"));
    }

    void normalReadAndWriteResponsesDecode()
    {
        const auto read = decodeRead(frame({1, 3, 4, 0x12, 0x34, 0xFF, 0xFE}));
        QCOMPARE(read.status, RtuDecodeStatus::Success);
        QCOMPARE(read.readValues, QVector<quint16>({0x1234, 0xFFFE}));
        QCOMPARE(read.crcStatus, CrcStatus::Valid);
        QVERIFY(read.receivedCrc.has_value());
        QCOMPARE(read.receivedCrc, read.calculatedCrc);

        const RequestDescriptor write = WriteRequestDescriptor{PduAddress(9), 550};
        const QByteArray response = encodeRequestAdu(1, write);
        const auto decoded = decodeResponseAdu(1, write, response, true);
        QCOMPARE(decoded.status, RtuDecodeStatus::Success);
    }

    void exceptionResponsesPreserveKnownAndUnknownCodes()
    {
        for (const quint8 exception : {quint8(1), quint8(2), quint8(3), quint8(4), quint8(0xFF)}) {
            const auto result = decodeRead(frame({1, 0x83, exception}));
            QCOMPARE(result.status, RtuDecodeStatus::Error);
            QCOMPARE(result.error->category, ErrorCategory::RemoteException);
            QCOMPARE(*result.error->exceptionCode, exception);
            QCOMPARE(result.error->code, remoteExceptionErrorCode(exception));
            QCOMPARE(result.crcStatus, CrcStatus::Valid);
        }
    }

    void candidateErrorPrecedenceIsStable()
    {
        QByteArray trailing = frame({1, 3, 4, 0, 1, 0, 2});
        trailing.append('\0');
        auto result = decodeRead(trailing);
        QCOMPARE(result.error->code, ErrorCode::UnexpectedTrailingBytes);
        QCOMPARE(result.crcStatus, CrcStatus::NotAvailable);

        QByteArray badCrcWrongSlave = frame({2, 3, 4, 0, 1, 0, 2});
        badCrcWrongSlave[badCrcWrongSlave.size() - 1] ^= 1;
        result = decodeRead(badCrcWrongSlave);
        QCOMPARE(result.error->code, ErrorCode::ResponseCrcMismatch);

        result = decodeRead(frame({2, 3, 4, 0, 1, 0, 2}));
        QCOMPARE(result.error->code, ErrorCode::WrongServerAddress);

        result = decodeRead(frame({1, 4, 4, 0, 1, 0, 2}));
        QCOMPARE(result.error->code, ErrorCode::UnexpectedFunction);

        result = decodeRead(frame({1, 3, 2, 0, 1}));
        QCOMPARE(result.error->code, ErrorCode::InvalidByteCount);
    }

    void writeEchoAndMalformedCandidatesAreRejected()
    {
        const RequestDescriptor write = WriteRequestDescriptor{PduAddress(9), 550};
        auto result = decodeResponseAdu(1, write, frame({1, 6, 0, 10, 2, 38}), true);
        QCOMPARE(result.error->code, ErrorCode::WriteEchoMismatch);

        result = decodeResponseAdu(1, write, frame({1, 6, 0, 9, 2, 39}), true);
        QCOMPARE(result.error->code, ErrorCode::WriteEchoMismatch);

        result = decodeResponseAdu(1, write, QByteArray::fromHex("01"), true);
        QCOMPARE(result.error->code, ErrorCode::ResponseCrcMissing);

        result = decodeResponseAdu(1, write, QByteArray::fromHex("01060009"), true);
        QCOMPARE(result.error->code, ErrorCode::MalformedResponse);
    }

    void everyFragmentBoundaryProducesTheSameResult()
    {
        const QByteArray complete = frame({1, 3, 4, 0x12, 0x34, 0x56, 0x78});
        const auto expected = decodeRead(complete);
        QCOMPARE(expected.status, RtuDecodeStatus::Success);

        for (qsizetype split = 0; split < complete.size(); ++split) {
            QByteArray accumulator;
            accumulator.append(complete.first(split));
            const auto partial = decodeRead(accumulator, 2, false);
            QCOMPARE(partial.status, RtuDecodeStatus::NeedMoreData);
            accumulator.append(complete.sliced(split));
            const auto actual = decodeRead(accumulator, 2, false);
            QCOMPARE(actual.status, expected.status);
            QCOMPARE(actual.readValues, expected.readValues);
        }

        QByteArray bytewise;
        for (qsizetype index = 0; index < complete.size(); ++index) {
            bytewise.append(complete.at(index));
            const auto result = decodeRead(bytewise, 2, false);
            QCOMPARE(result.status,
                     index + 1 == complete.size()
                         ? RtuDecodeStatus::Success
                         : RtuDecodeStatus::NeedMoreData);
        }

        const RequestDescriptor write = WriteRequestDescriptor{PduAddress(9), 550};
        const QByteArray writeResponse = encodeRequestAdu(1, write);
        for (qsizetype split = 0; split < writeResponse.size(); ++split) {
            QByteArray accumulator = writeResponse.first(split);
            QCOMPARE(decodeResponseAdu(1, write, accumulator, false).status,
                     RtuDecodeStatus::NeedMoreData);
            accumulator.append(writeResponse.sliced(split));
            QCOMPARE(decodeResponseAdu(1, write, accumulator, false).status,
                     RtuDecodeStatus::Success);
        }
    }
};

QTEST_APPLESS_MAIN(RtuCodecTest)

#include "tst_rtucodec.moc"
