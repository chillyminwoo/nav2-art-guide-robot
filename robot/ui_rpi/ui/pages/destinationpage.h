#pragma once
#include <QWidget>
#include <QProcess>

class DestinationPage : public QWidget {
    Q_OBJECT
public:
    explicit DestinationPage(QWidget *parent = nullptr);
    void stopTTS();
signals:
    void destSelected(const QString &dest);
    void emergencyStop();
private:
    void addDestButton(class QGridLayout *grid, const QString &label,
                       const QString &sub, int row, int col);
    void speakText(const QString &text);

    QProcess *ttsProcess;
};