#pragma once
#include <QWidget>
#include <QTcpSocket>
#include <QProcess>

class QLabel;

class ArtworkPage : public QWidget {
    Q_OBJECT
public:
    explicit ArtworkPage(QWidget *parent = nullptr);
    void connectToServer(const QString &host, quint16 port = 9998);
    void startRecognition();
    void stopRecognition();
    void stopTTS();

signals:
    void goHome();
    void emergencyStop();
    void artworkReceived(const QString &name, const QString &artist,
                         const QString &year,  const QString &genre,
                         const QString &technique, const QString &location,
                         const QString &description);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();

private:
    void setArtworkInfo(const QString &name, const QString &artist,
                        const QString &year,  const QString &genre,
                        const QString &technique, const QString &location,
                        const QString &description);
    void speakText(const QString &text);
    void sendCommand(const QString &cmd);  // ← 추가

    QLabel *titleLabel, *artistLabel, *yearLabel;
    QLabel *genreLabel, *locationLabel, *descLabel;

    QTcpSocket *socket;
    QByteArray  buffer;
    QProcess   *ttsProcess;

    bool m_active = false;   // ← 추가
};