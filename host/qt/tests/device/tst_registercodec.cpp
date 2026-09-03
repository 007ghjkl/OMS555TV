#include "device/RegisterCodec.h"

#include <QTest>

#include <array>
#include <cmath>
#include <limits>
#include <variant>

using namespace oms555tv::device;

namespace {

QVector<RegisterBlock> validSnapshotBlocks()
{
    return {
        {measurementsReadBlock.startAddress, {0x0320, 0x0000, 0xFFFF, 0xFE70, 3300}},
        {thresholdsReadBlock.startAddress, {600, 600, 600, 400}},
        {statusReadBlock.startAddress, {0x000F, 0x0021}},
        {diagnosticsReadBlock.startAddress, {0xFFFF, 0xFFFF, 0xFFFF}},
        {versionReadBlock.startAddress, {1, 2}},
    };
}

template<typename T>
const CodecError &codecError(const CodecResult<T> &result)
{
    return std::get<CodecError>(result);
}

} // namespace

class RegisterCodecTest final : public QObject
{
    Q_OBJECT

private slots:
    void addressesAndReadBlocksMatchTheRegisterMap()
    {
        struct AddressExpectation {
            HoldingRegister reg;
            quint16 pdu;
            quint32 document;
        };

        constexpr std::array<AddressExpectation, 16> expectations{{
            {HoldingRegister::PhaseATemperature, 0, 40001},
            {HoldingRegister::PhaseBTemperature, 1, 40002},
            {HoldingRegister::PhaseCTemperature, 2, 40003},
            {HoldingRegister::AmbientTemperature, 3, 40004},
            {HoldingRegister::LightMillivolts, 4, 40005},
            {HoldingRegister::PhaseAThreshold, 9, 40010},
            {HoldingRegister::PhaseBThreshold, 10, 40011},
            {HoldingRegister::PhaseCThreshold, 11, 40012},
            {HoldingRegister::AmbientThreshold, 12, 40013},
            {HoldingRegister::AlarmStatus, 19, 40020},
            {HoldingRegister::DeviceStatus, 20, 40021},
            {HoldingRegister::CommunicationErrorCount, 29, 40030},
            {HoldingRegister::UptimeLow, 30, 40031},
            {HoldingRegister::UptimeHigh, 31, 40032},
            {HoldingRegister::FirmwareMajor, 39, 40040},
            {HoldingRegister::FirmwareMinor, 40, 40041},
        }};

        for (const auto &expectation : expectations) {
            const PduAddress address = pduAddress(expectation.reg);
            QCOMPARE(address.value(), expectation.pdu);
            QCOMPARE(documentationNumber(address), expectation.document);
        }

        QCOMPARE(deviceSnapshotReadBlocks.size(), std::size_t(5));
        QCOMPARE(measurementsReadBlock.startAddress.value(), quint16(0));
        QCOMPARE(measurementsReadBlock.count, quint16(5));
        QCOMPARE(measurementsReadBlock.endAddress().value(), quint16(4));
        QCOMPARE(thresholdsReadBlock.startAddress.value(), quint16(9));
        QCOMPARE(thresholdsReadBlock.count, quint16(4));
        QCOMPARE(thresholdsReadBlock.endAddress().value(), quint16(12));
        QCOMPARE(statusReadBlock.startAddress.value(), quint16(19));
        QCOMPARE(statusReadBlock.endAddress().value(), quint16(20));
        QCOMPARE(diagnosticsReadBlock.startAddress.value(), quint16(29));
        QCOMPARE(diagnosticsReadBlock.endAddress().value(), quint16(31));
        QCOMPARE(versionReadBlock.startAddress.value(), quint16(39));
        QCOMPARE(versionReadBlock.endAddress().value(), quint16(40));
    }

    void measurementsDecodeSignedTemperaturesAndLightVoltage()
    {
        const RegisterBlock block{
            measurementsReadBlock.startAddress,
            {0x0320, 0x0000, 0xFFFF, 0xFE70, 3300},
        };
        const auto result = decodeMeasurements(block);

        QVERIFY(std::holds_alternative<Measurements>(result));
        const auto &measurements = std::get<Measurements>(result);
        QCOMPARE(measurements.phaseATemperature.deciCelsius, qint16(800));
        QCOMPARE(measurements.phaseATemperature.celsius(), 80.0);
        QCOMPARE(measurements.phaseBTemperature.deciCelsius, qint16(0));
        QCOMPARE(measurements.phaseCTemperature.deciCelsius, qint16(-1));
        QCOMPARE(measurements.phaseCTemperature.celsius(), -0.1);
        QCOMPARE(measurements.ambientTemperature.deciCelsius, qint16(-400));
        QCOMPARE(measurements.lightMillivolts, quint16(3300));

        const auto typicalResult = decodeMeasurements(
            {measurementsReadBlock.startAddress, {0x0160, 0, 0, 0, 0}});
        QVERIFY(std::holds_alternative<Measurements>(typicalResult));
        const auto &typical = std::get<Measurements>(typicalResult);
        QCOMPARE(typical.phaseATemperature.deciCelsius, qint16(352));
        QCOMPARE(typical.phaseATemperature.celsius(), 35.2);
        QCOMPARE(typical.lightMillivolts, quint16(0));
    }

    void measurementBlockErrorsAreStructured()
    {
        const auto addressResult = decodeMeasurements({PduAddress(1), {0, 0, 0, 0, 0}});
        QVERIFY(std::holds_alternative<CodecError>(addressResult));
        const auto &addressError = codecError(addressResult);
        QCOMPARE(static_cast<int>(addressError.code),
                 static_cast<int>(CodecErrorCode::AddressMismatch));
        QCOMPARE(addressError.actualAddress->value(), quint16(1));
        QCOMPARE(addressError.expectedAddress->value(), quint16(0));

        const auto shortResult = decodeMeasurements({PduAddress(0), {0, 0, 0, 0}});
        QVERIFY(std::holds_alternative<CodecError>(shortResult));
        const auto &shortError = codecError(shortResult);
        QCOMPARE(static_cast<int>(shortError.code),
                 static_cast<int>(CodecErrorCode::RegisterCountMismatch));
        QCOMPARE(*shortError.actualRegisterCount, qsizetype(4));
        QCOMPARE(*shortError.expectedRegisterCount, qsizetype(5));

        const auto longResult = decodeMeasurements({PduAddress(0), {0, 0, 0, 0, 0, 0}});
        QVERIFY(std::holds_alternative<CodecError>(longResult));
        QCOMPARE(static_cast<int>(codecError(longResult).code),
                 static_cast<int>(CodecErrorCode::RegisterCountMismatch));

        const auto lowResult = decodeMeasurements({PduAddress(0), {0xFE6F, 0, 0, 0, 0}});
        QVERIFY(std::holds_alternative<CodecError>(lowResult));
        const auto &lowError = codecError(lowResult);
        QCOMPARE(static_cast<int>(lowError.code),
                 static_cast<int>(CodecErrorCode::ValueOutOfRange));
        QCOMPARE(static_cast<int>(lowError.field),
                 static_cast<int>(DeviceField::PhaseATemperature));
        QCOMPARE(*lowError.actualValue, -401.0);
        QCOMPARE(*lowError.minimumValue, -400.0);
        QCOMPARE(*lowError.maximumValue, 800.0);

        const auto highResult = decodeMeasurements({PduAddress(0), {0, 0x0321, 0, 0, 0}});
        QVERIFY(std::holds_alternative<CodecError>(highResult));
        QCOMPARE(static_cast<int>(codecError(highResult).field),
                 static_cast<int>(DeviceField::PhaseBTemperature));

        const auto lightResult = decodeMeasurements({PduAddress(0), {0, 0, 0, 0, 3301}});
        QVERIFY(std::holds_alternative<CodecError>(lightResult));
        const auto &lightError = codecError(lightResult);
        QCOMPARE(static_cast<int>(lightError.field),
                 static_cast<int>(DeviceField::LightMillivolts));
        QCOMPARE(*lightError.actualValue, 3301.0);
    }

    void thresholdsDecodeAllBoundaries()
    {
        const auto result = decodeThresholds(
            {thresholdsReadBlock.startAddress, {0xFE70, 0xFFFF, 0x0000, 0x0320}});

        QVERIFY(std::holds_alternative<AlarmThresholds>(result));
        const auto &thresholds = std::get<AlarmThresholds>(result);
        QCOMPARE(thresholds.phaseA.deciCelsius, qint16(-400));
        QCOMPARE(thresholds.phaseB.deciCelsius, qint16(-1));
        QCOMPARE(thresholds.phaseC.deciCelsius, qint16(0));
        QCOMPARE(thresholds.ambient.deciCelsius, qint16(800));
    }

    void thresholdEncoding_data()
    {
        QTest::addColumn<int>("channel");
        QTest::addColumn<double>("celsius");
        QTest::addColumn<quint16>("address");
        QTest::addColumn<quint16>("rawValue");
        QTest::addColumn<qint16>("deciCelsius");

        QTest::newRow("phase-a-minimum") << int(TemperatureChannel::PhaseA) << -40.0
                                          << quint16(9) << quint16(0xFE70) << qint16(-400);
        QTest::newRow("phase-b-negative") << int(TemperatureChannel::PhaseB) << -0.1
                                           << quint16(10) << quint16(0xFFFF) << qint16(-1);
        QTest::newRow("phase-c-zero") << int(TemperatureChannel::PhaseC) << 0.0
                                      << quint16(11) << quint16(0x0000) << qint16(0);
        QTest::newRow("ambient-maximum") << int(TemperatureChannel::Ambient) << 80.0
                                         << quint16(12) << quint16(0x0320) << qint16(800);
    }

    void thresholdEncoding()
    {
        QFETCH(int, channel);
        QFETCH(double, celsius);
        QFETCH(quint16, address);
        QFETCH(quint16, rawValue);
        QFETCH(qint16, deciCelsius);

        const auto result = encodeAlarmThreshold(static_cast<TemperatureChannel>(channel), celsius);
        QVERIFY(std::holds_alternative<RegisterWrite>(result));
        const auto &write = std::get<RegisterWrite>(result);
        QCOMPARE(write.address.value(), address);
        QCOMPARE(write.rawValue, rawValue);
        QCOMPARE(write.value.deciCelsius, deciCelsius);

        const auto roundTrip = decodeThresholds(
            {thresholdsReadBlock.startAddress,
             {write.rawValue, write.rawValue, write.rawValue, write.rawValue}});
        QVERIFY(std::holds_alternative<AlarmThresholds>(roundTrip));
        QCOMPARE(std::get<AlarmThresholds>(roundTrip).phaseA.deciCelsius, deciCelsius);
    }

    void thresholdEncodingRejectsInvalidInputsInStableOrder()
    {
        const auto invalidChannel = encodeAlarmThreshold(
            static_cast<TemperatureChannel>(99), std::numeric_limits<double>::quiet_NaN());
        QVERIFY(std::holds_alternative<CodecError>(invalidChannel));
        QCOMPARE(static_cast<int>(codecError(invalidChannel).code),
                 static_cast<int>(CodecErrorCode::InvalidChannel));

        const auto nanResult = encodeAlarmThreshold(
            TemperatureChannel::PhaseA, std::numeric_limits<double>::quiet_NaN());
        QVERIFY(std::holds_alternative<CodecError>(nanResult));
        QCOMPARE(static_cast<int>(codecError(nanResult).code),
                 static_cast<int>(CodecErrorCode::NonFiniteValue));

        for (const double nonFinite : {std::numeric_limits<double>::infinity(),
                                       -std::numeric_limits<double>::infinity()}) {
            const auto result = encodeAlarmThreshold(TemperatureChannel::PhaseA, nonFinite);
            QVERIFY(std::holds_alternative<CodecError>(result));
            QCOMPARE(static_cast<int>(codecError(result).code),
                     static_cast<int>(CodecErrorCode::NonFiniteValue));
        }

        const auto precisionResult = encodeAlarmThreshold(TemperatureChannel::PhaseA, 20.05);
        QVERIFY(std::holds_alternative<CodecError>(precisionResult));
        QCOMPARE(static_cast<int>(codecError(precisionResult).code),
                 static_cast<int>(CodecErrorCode::InvalidPrecision));

        for (const double outOfRange : {-40.1, 80.1}) {
            const auto result = encodeAlarmThreshold(TemperatureChannel::Ambient, outOfRange);
            QVERIFY(std::holds_alternative<CodecError>(result));
            const auto &error = codecError(result);
            QCOMPARE(static_cast<int>(error.code),
                     static_cast<int>(CodecErrorCode::ValueOutOfRange));
            QCOMPARE(*error.minimumValue, -40.0);
            QCOMPARE(*error.maximumValue, 80.0);
        }
    }

    void statusWordsExposeEveryDefinedBitAndPreserveReservedBits()
    {
        for (int bit = 0; bit < 4; ++bit) {
            const auto result = decodeStatus(
                {statusReadBlock.startAddress, {quint16(1U << bit), 0}});
            QVERIFY(std::holds_alternative<Status>(result));
            const auto &alarms = std::get<Status>(result).alarms;
            QCOMPARE(alarms.phaseATemperatureHigh(), bit == 0);
            QCOMPARE(alarms.phaseBTemperatureHigh(), bit == 1);
            QCOMPARE(alarms.phaseCTemperatureHigh(), bit == 2);
            QCOMPARE(alarms.ambientTemperatureHigh(), bit == 3);
        }

        for (int bit = 0; bit < 6; ++bit) {
            const auto result = decodeStatus(
                {statusReadBlock.startAddress, {0, quint16(1U << bit)}});
            QVERIFY(std::holds_alternative<Status>(result));
            const auto &device = std::get<Status>(result).device;
            QCOMPARE(device.running(), bit == 0);
            QCOMPARE(device.phaseASensorFault(), bit == 1);
            QCOMPARE(device.phaseBSensorFault(), bit == 2);
            QCOMPARE(device.phaseCSensorFault(), bit == 3);
            QCOMPARE(device.lightAdcFault(), bit == 4);
            QCOMPARE(device.ambientSimulated(), bit == 5);
        }

        const auto combined = decodeStatus({statusReadBlock.startAddress, {0xFFFF, 0xFFFF}});
        QVERIFY(std::holds_alternative<Status>(combined));
        const auto &status = std::get<Status>(combined);
        QCOMPARE(status.alarms.raw, quint16(0xFFFF));
        QCOMPARE(status.alarms.reservedBits(), quint16(0xFFF0));
        QVERIFY(status.alarms.phaseATemperatureHigh());
        QVERIFY(status.alarms.phaseBTemperatureHigh());
        QVERIFY(status.alarms.phaseCTemperatureHigh());
        QVERIFY(status.alarms.ambientTemperatureHigh());
        QCOMPARE(status.device.raw, quint16(0xFFFF));
        QCOMPARE(status.device.reservedBits(), quint16(0xFFC0));
        QVERIFY(status.device.running());
        QVERIFY(status.device.ambientSimulated());
    }

    void diagnosticsCombineLowWordBeforeHighWord()
    {
        struct Expectation {
            quint16 low;
            quint16 high;
            quint32 uptime;
        };
        constexpr std::array<Expectation, 5> expectations{{
            {0x0000, 0x0000, 0x00000000U},
            {0x0001, 0x0000, 0x00000001U},
            {0x0000, 0x0001, 0x00010000U},
            {0xFFFF, 0x0001, 0x0001FFFFU},
            {0xFFFF, 0xFFFF, 0xFFFFFFFFU},
        }};

        for (const auto &expectation : expectations) {
            const auto result = decodeDiagnostics(
                {diagnosticsReadBlock.startAddress,
                 {0xFFFF, expectation.low, expectation.high}});
            QVERIFY(std::holds_alternative<Diagnostics>(result));
            const auto &diagnostics = std::get<Diagnostics>(result);
            QCOMPARE(diagnostics.communicationErrorCount, quint16(0xFFFF));
            QCOMPARE(diagnostics.uptimeSeconds, expectation.uptime);
        }
    }

    void firmwareVersionPreservesFullRegisterRange()
    {
        const auto zero = decodeFirmwareVersion({versionReadBlock.startAddress, {0, 0}});
        QVERIFY(std::holds_alternative<FirmwareVersion>(zero));
        QCOMPARE(std::get<FirmwareVersion>(zero).major, quint16(0));
        QCOMPARE(std::get<FirmwareVersion>(zero).minor, quint16(0));

        const auto maximum = decodeFirmwareVersion(
            {versionReadBlock.startAddress, {0xFFFF, 0xFFFF}});
        QVERIFY(std::holds_alternative<FirmwareVersion>(maximum));
        QCOMPARE(std::get<FirmwareVersion>(maximum).major, quint16(0xFFFF));
        QCOMPARE(std::get<FirmwareVersion>(maximum).minor, quint16(0xFFFF));
    }

    void completeSnapshotAcceptsReadBlocksInAnyOrder()
    {
        auto blocks = validSnapshotBlocks();
        const QVector<RegisterBlock> reordered{
            blocks[4], blocks[2], blocks[0], blocks[3], blocks[1],
        };
        const auto result = decodeDeviceSnapshot(reordered);

        QVERIFY(std::holds_alternative<DeviceSnapshot>(result));
        const auto &snapshot = std::get<DeviceSnapshot>(result);
        QCOMPARE(snapshot.measurements.phaseATemperature.deciCelsius, qint16(800));
        QCOMPARE(snapshot.measurements.phaseCTemperature.deciCelsius, qint16(-1));
        QCOMPARE(snapshot.measurements.lightMillivolts, quint16(3300));
        QCOMPARE(snapshot.thresholds.phaseA.deciCelsius, qint16(600));
        QVERIFY(snapshot.status.alarms.ambientTemperatureHigh());
        QVERIFY(snapshot.status.device.running());
        QVERIFY(snapshot.status.device.ambientSimulated());
        QCOMPARE(snapshot.diagnostics.communicationErrorCount, quint16(0xFFFF));
        QCOMPARE(snapshot.diagnostics.uptimeSeconds, quint32(0xFFFFFFFFU));
        QCOMPARE(snapshot.firmwareVersion.major, quint16(1));
        QCOMPARE(snapshot.firmwareVersion.minor, quint16(2));
    }

    void completeSnapshotReturnsErrorsInSpecifiedOrder()
    {
        auto unknownBlocks = validSnapshotBlocks();
        unknownBlocks.append({PduAddress(50), {0}});
        unknownBlocks.append({PduAddress(8), {0}});
        const auto unknown = decodeDeviceSnapshot(unknownBlocks);
        QVERIFY(std::holds_alternative<CodecError>(unknown));
        QCOMPARE(static_cast<int>(codecError(unknown).code),
                 static_cast<int>(CodecErrorCode::UnknownReadBlock));
        QCOMPARE(codecError(unknown).actualAddress->value(), quint16(8));

        auto duplicateBlocks = validSnapshotBlocks();
        duplicateBlocks.append(duplicateBlocks[2]);
        duplicateBlocks.append(duplicateBlocks[0]);
        duplicateBlocks.removeAt(1);
        const auto duplicate = decodeDeviceSnapshot(duplicateBlocks);
        QVERIFY(std::holds_alternative<CodecError>(duplicate));
        QCOMPARE(static_cast<int>(codecError(duplicate).code),
                 static_cast<int>(CodecErrorCode::DuplicateReadBlock));
        QCOMPARE(codecError(duplicate).actualAddress->value(), quint16(0));

        const QVector<RegisterBlock> missingBlocks{
            validSnapshotBlocks()[2], validSnapshotBlocks()[3], validSnapshotBlocks()[4],
        };
        const auto missing = decodeDeviceSnapshot(missingBlocks);
        QVERIFY(std::holds_alternative<CodecError>(missing));
        QCOMPARE(static_cast<int>(codecError(missing).code),
                 static_cast<int>(CodecErrorCode::MissingReadBlock));
        QVERIFY(!codecError(missing).actualAddress.has_value());
        QCOMPARE(codecError(missing).expectedAddress->value(), quint16(0));

        auto badCountBlocks = validSnapshotBlocks();
        badCountBlocks[0].values.removeLast();
        const auto badCount = decodeDeviceSnapshot(badCountBlocks);
        QVERIFY(std::holds_alternative<CodecError>(badCount));
        QCOMPARE(static_cast<int>(codecError(badCount).code),
                 static_cast<int>(CodecErrorCode::RegisterCountMismatch));

        auto badValues = validSnapshotBlocks();
        badValues[0].values[0] = 0x0321;
        badValues[1].values[0] = 0x0321;
        const auto badValue = decodeDeviceSnapshot(badValues);
        QVERIFY(std::holds_alternative<CodecError>(badValue));
        QCOMPARE(static_cast<int>(codecError(badValue).field),
                 static_cast<int>(DeviceField::PhaseATemperature));
    }
};

QTEST_APPLESS_MAIN(RegisterCodecTest)

#include "tst_registercodec.moc"
