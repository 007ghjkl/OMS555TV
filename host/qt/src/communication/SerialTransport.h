#pragma once

#include "communication/CommunicationTypes.h"

#include <QObject>

#include <functional>

namespace oms555tv::communication {

// Worker 内部的最小串口边界；生产实现封装 QSerialPort，测试可注入可控替身。
class SerialTransport : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~SerialTransport() override = default;

    [[nodiscard]] virtual bool configure(const SerialPortConfig &config) = 0;
    [[nodiscard]] virtual bool open() = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    [[nodiscard]] virtual qint64 write(const QByteArray &bytes) = 0;
    [[nodiscard]] virtual QByteArray readAll() = 0;
    [[nodiscard]] virtual QString errorString() const = 0;
    [[nodiscard]] virtual int errorCode() const = 0;

signals:
    void readyRead();
    void errorOccurred(int errorCode);
};

using SerialTransportFactory = std::function<SerialTransport *()>;

} // namespace oms555tv::communication
