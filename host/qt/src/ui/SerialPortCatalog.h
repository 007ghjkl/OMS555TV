#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace oms555tv::ui {

struct SerialPortEntry {
    QString portName;
    QString description;
    QString manufacturer;
    QString serialNumber;
    std::optional<quint16> vendorIdentifier;
    std::optional<quint16> productIdentifier;

    [[nodiscard]] QString displayName() const;
};

class ISerialPortCatalog
{
public:
    virtual ~ISerialPortCatalog() = default;
    [[nodiscard]] virtual QVector<SerialPortEntry> availablePorts() const = 0;
};

class QtSerialPortCatalog final : public ISerialPortCatalog
{
public:
    [[nodiscard]] QVector<SerialPortEntry> availablePorts() const override;
};

} // namespace oms555tv::ui
