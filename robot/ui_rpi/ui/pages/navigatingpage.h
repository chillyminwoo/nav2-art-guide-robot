#pragma once
#include <QWidget>
#include <QString>

class QLabel;
class QProgressBar;
class QTimer;
class QProcess;

class NavigatingPage : public QWidget {
    Q_OBJECT
public:
    explicit NavigatingPage(QWidget *parent = nullptr);
    void setDestination(const QString &dest);
    void showArrival(const QString &dest);
    void stopTTS();
    QString currentDest() const { return currentDest_; }

signals:
    void goHome();
    void emergencyStop();

private:
    void speakText(const QString &text);

    QLabel       *destLabel;
    QLabel       *statusLabel;
    QProgressBar *progressBar;
    QTimer       *timer;
    QProcess     *ttsProcess;
    QString       currentDest_;   // ← 언더스코어로 멤버 변수명 변경
    int           progress;
};