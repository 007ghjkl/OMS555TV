#include "communication/QSerialPortTransport.h"

namespace oms555tv::communication {
namespace {

QSerialPort::Parity toQtParity(SerialParity parity)
{
    switch (parity) {
    case SerialParity::None:
        return QSerialPort::NoParity;
    case SerialParity::Even:
        return QSerialPort::EvenParity;
    case SerialParity::Odd:
        return QSerialPort::OddParity;
    }
    Q_UNREACHABLE_RETURN(QSerialPort::NoParity);
}

QSerialPort::StopBits toQtStopBits(SerialStopBits stopBits)
{
    return stopBits == SerialStopBits::Two
        ? QSerialPort::TwoStop
        : QSerialPort::OneStop;
}

} // namespace

QSerialPortTransport::QSerialPortTransport(QObject *parent)
    : SerialTransport(parent)
{
    connect(&port_, &QSerialPort::readyRead, this, &SerialTransport::readyRead);
    connect(&port_, &QSerialPort::errorOccurred, this,
            [this](QSerialPort::SerialPortError error) {
                emit errorOccurred(static_cast<int>(error));
            });
}

bool QSerialPortTransport::configure(const SerialPortConfig &config)
{
    Q_ASSERT(!port_.isOpen());
    port_.setPortName(config.portName.trimmed());
    return port_.setBaudRate(config.baudRate, QSerialPort::AllDirections)
        && port_.setDataBits(QSerialPort::Data8)
        && port_.setParity(toQtParity(config.parity))
        && port_.setStopBits(toQtStopBits(config.stopBits))
        && port_.setFlowControl(QSerialPort::NoFlowControl);
}

bool QSerialPortTransport::open()
{
    return port_.open(QIODevice::ReadWrite);
}

void QSerialPortTransport::close()
{
    if (port_.isOpen()) {
        port_.close();
    }
}

bool QSerialPortTransport::isOpen() const
{
    return port_.isOpen();
}

qint64 QSerialPortTransport::write(const QByteArray &bytes)
{
    return port_.write(bytes);
}

QByteArray QSerialPortTransport::readAll()
{
    return port_.readAll();
}

QString QSerialPortTransport::errorString() const
{
    return port_.errorString();
}

int QSerialPortTransport::errorCode() const
{
    return static_cast<int>(port_.error());
}

} // namespace oms555tv::communication
