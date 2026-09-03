#pragma once

#include "communication/SerialTransport.h"

#include <QSerialPort>

namespace oms555tv::communication {

class QSerialPortTransport final : public SerialTransport
{
    Q_OBJECT

public:
    explicit QSerialPortTransport(QObject *parent = nullptr);

    [[nodiscard]] bool configure(const SerialPortConfig &config) override;
    [[nodiscard]] bool open() override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] qint64 write(const QByteArray &bytes) override;
    [[nodiscard]] QByteArray readAll() override;
    [[nodiscard]] QString errorString() const override;
    [[nodiscard]] int errorCode() const override;

private:
    QSerialPort port_;
};

} // namespace oms555tv::communication
