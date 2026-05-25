#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>

class NavigationManager : public QObject {
    Q_OBJECT
public:
    explicit NavigationManager(QObject *parent = nullptr);
    void connectToServer(const QString &host, quint16 port = 9997);
    void sendDestination(const QString &dest);
    void sendEmergencyStop();

signals:
    void arrived(const QString &dest);
    void connected();
    void disconnected();
    void emergencyStopped();

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();

private:
    QTcpSocket *socket;
    QByteArray  buffer;
    QString     serverHost;
    quint16     serverPort;
};