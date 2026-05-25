#include "destinationpage.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QProcess>

static const QString BTN_STYLE = R"(
    QPushButton {
        background-color: #1e2530;
        color: #ffffff;
        border: 1px solid #2d3748;
        border-radius: 12px;
        font-size: 22px;
        font-weight: bold;
        padding: 10px;
    }
    QPushButton:pressed {
        background-color: #185FA5;
        border-color: #185FA5;
    }
)";

static const QString ESTOP_STYLE = R"(
    QPushButton {
        background-color: #A32D2D;
        color: #ffffff;
        border: none;
        border-radius: 8px;
        font-size: 16px;
        font-weight: bold;
        padding: 8px 20px;
    }
    QPushButton:pressed { background-color: #7a2020; }
)";

DestinationPage::DestinationPage(QWidget *parent) : QWidget(parent) {
    ttsProcess = new QProcess(this);
    setStyleSheet("background-color: #0d1117;");

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);

    // 상단바
    QWidget *topBar = new QWidget;
    topBar->setFixedHeight(60);
    topBar->setStyleSheet("background-color: #0C447C;");
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    QLabel *topTitle = new QLabel("목적지를 선택하세요");
    topTitle->setStyleSheet("color: white; font-size: 20px; font-weight: bold;");
    QPushButton *eStop = new QPushButton("긴급 정지");
    eStop->setFixedSize(120, 40);
    eStop->setStyleSheet(ESTOP_STYLE);
    connect(eStop, &QPushButton::clicked, [this]{
        speakText("긴급 정지합니다");
        emit emergencyStop();
    });
    topLayout->addWidget(topTitle);
    topLayout->addStretch();
    topLayout->addWidget(eStop);

    // 버튼 그리드
    QWidget *gridWidget = new QWidget;
    QGridLayout *grid = new QGridLayout(gridWidget);
    grid->setSpacing(16);
    grid->setContentsMargins(30, 30, 30, 30);

    addDestButton(grid, "전시관 A", "Hall A", 0, 0);
    addDestButton(grid, "전시관 B", "Hall B", 0, 1);
    addDestButton(grid, "전시관 C", "Hall C", 0, 2);
    addDestButton(grid, "화장실", "Toilet", 1, 0);
    addDestButton(grid, "출입구", "Exit", 1, 1);

    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);

    root->addWidget(topBar);
    root->addWidget(gridWidget, 1);
}

void DestinationPage::addDestButton(QGridLayout *grid, const QString &label,
                                    const QString &sub, int row, int col) {
    QPushButton *btn = new QPushButton;
    btn->setStyleSheet(BTN_STYLE);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    btn->setFixedSize(150, 100);

    QVBoxLayout *btnLayout = new QVBoxLayout(btn);
    QLabel *mainLabel = new QLabel(label);
    mainLabel->setAlignment(Qt::AlignCenter);
    mainLabel->setStyleSheet("color: #ffffff; font-size: 24px; font-weight: bold; background: transparent;");
    QLabel *subLabel = new QLabel(sub);
    subLabel->setAlignment(Qt::AlignCenter);
    subLabel->setStyleSheet("color: #888888; font-size: 14px; background: transparent;");
    btnLayout->addWidget(mainLabel);
    btnLayout->addWidget(subLabel);

    connect(btn, &QPushButton::clicked, [this, label]{ emit destSelected(label); });
    grid->addWidget(btn, row, col);
}
void DestinationPage::stopTTS() {
    if (ttsProcess->state() != QProcess::NotRunning) {
        ttsProcess->kill();
        ttsProcess->waitForFinished(300);
    }
}

void DestinationPage::speakText(const QString &text) {
    if (ttsProcess->state() != QProcess::NotRunning) {
        ttsProcess->kill();
        ttsProcess->waitForFinished(500);
    }
    QString safeText = text;
    safeText.replace("'", " ");
    QString script = QString(
        "from gtts import gTTS; gTTS('%1', lang='ko').save('/tmp/tts_dest.mp3')"
    ).arg(safeText);

    QProcess *gen = new QProcess(this);
    connect(gen, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, gen](int, QProcess::ExitStatus){
                gen->deleteLater();
                ttsProcess->start("mpg123", QStringList() << "/tmp/tts_dest.mp3");
            });
    gen->start("python3", QStringList() << "-c" << script);
}
