#include "navigationmanager.h"
#include <QTimer>
#include <QtEndian>

NavigationManager::NavigationManager(QObject *parent)
    : QObject(parent), socket(new QTcpSocket(this)), serverPort(9997)
{
    connect(socket, &QTcpSocket::connected,    this, &NavigationManager::onConnected);
    connect(socket, &QTcpSocket::readyRead,    this, &NavigationManager::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &NavigationManager::onDisconnected);
}

void NavigationManager::connectToServer(const QString &host, quint16 port) {
    serverHost = host;
    serverPort = port;
    socket->connectToHost(host, port);
}

void NavigationManager::onConnected() {
    qDebug() << "NavigationManager 서버 연결 완료";
    emit connected();
}

void NavigationManager::onDisconnected() {
    qDebug() << "NavigationManager 연결 끊김. 3초 후 재연결...";
    emit disconnected();
    QTimer::singleShot(3000, this, [this]{
        socket->connectToHost(serverHost, serverPort);
    });
}

void NavigationManager::sendDestination(const QString &dest) {
    if (socket->state() != QAbstractSocket::ConnectedState) return;
    QByteArray msg = dest.toUtf8();
    quint32 size = qToBigEndian<quint32>(msg.size());
    socket->write(reinterpret_cast<const char*>(&size), 4);
    socket->write(msg);
}

void NavigationManager::sendEmergencyStop() {
    if (socket->state() != QAbstractSocket::ConnectedState) return;
    QByteArray msg = "ESTOP";
    quint32 size = qToBigEndian<quint32>(msg.size());
    socket->write(reinterpret_cast<const char*>(&size), 4);
    socket->write(msg);
    emit emergencyStopped();
}

void NavigationManager::onReadyRead() {
    buffer.append(socket->readAll());

    while (buffer.size() >= 4) {
        quint32 msgLen = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(buffer.constData()));
        if (buffer.size() < 4 + (int)msgLen) break;

        QByteArray msgData = buffer.mid(4, msgLen);
        buffer.remove(0, 4 + msgLen);

        QString msg = QString::fromUtf8(msgData);
        qDebug() << "NavigationManager 수신:" << msg;

        // "ARRIVED:화장실" → dest = "화장실"
        if (msg.startsWith("ARRIVED:")) {
            emit arrived(msg.section(':', 1));
        }
    }
}