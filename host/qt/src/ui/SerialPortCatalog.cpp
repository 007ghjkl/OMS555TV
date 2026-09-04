#include "ui/SerialPortCatalog.h"

#include <QSerialPortInfo>

#include <algorithm>

namespace oms555tv::ui {

QString SerialPortEntry::displayName() const
{
    if (description.trimmed().isEmpty()) {
        return portName;
    }
    return QStringLiteral("%1 — %2").arg(portName, description.trimmed());
}

QVector<SerialPortEntry> QtSerialPortCatalog::availablePorts() const
{
    QVector<SerialPortEntry> result;
    const auto ports = QSerialPortInfo::availablePorts();
    result.reserve(ports.size());
    for (const auto &port : ports) {
        SerialPortEntry entry;
        entry.portName = port.portName();
        entry.description = port.description();
        entry.manufacturer = port.manufacturer();
        entry.serialNumber = port.serialNumber();
        if (port.hasVendorIdentifier()) {
            entry.vendorIdentifier = port.vendorIdentifier();
        }
        if (port.hasProductIdentifier()) {
            entry.productIdentifier = port.productIdentifier();
        }
        result.append(std::move(entry));
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.portName.localeAwareCompare(right.portName) < 0;
    });
    return result;
}

} // namespace oms555tv::ui
