#pragma once
#include <QWidget>
#include <QTcpSocket>
#include <QProcess>

class QLabel;

class QRPage : public QWidget {
    Q_OBJECT
public:
    explicit QRPage(QWidget *parent = nullptr);

    void connectToServer(const QString &host, quint16 port = 9994);
    void reset();
    void stopTTS();

signals:
    void authSuccess();

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();

private:
    void showWaiting();
    void showFail();
    void speakText(const QString &text);  // interruptible 파라미터 제거 (killAllTTS로 통합)
    void killAllTTS();

    QLabel     *iconLabel;
    QLabel     *titleLabel;
    QLabel     *subLabel;

    QTcpSocket *socket;
    QByteArray  buffer;
    QProcess   *ttsProcess;
    QProcess   *genProcess = nullptr;

    bool  m_ttsBusy            = false;
    bool  m_pendingAuthSuccess = false;  // AUTH_OK TTS 완료 후 authSuccess emit
    bool  m_pendingShowWaiting = false;  // AUTH_FAIL TTS 완료 후 showWaiting 호출
};
