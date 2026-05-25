#include "navigatingpage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTimer>
#include <QProcess>

static const QString ESTOP_STYLE = R"(
    QPushButton {
        background-color: #A32D2D; color: #ffffff;
        border: none; border-radius: 8px;
        font-size: 16px; font-weight: bold; padding: 8px 20px;
    }
    QPushButton:pressed { background-color: #7a2020; }
)";

static const QString HOME_STYLE = R"(
    QPushButton {
        background-color: #185FA5; color: #ffffff;
        border: none; border-radius: 8px;
        font-size: 18px; font-weight: bold; padding: 12px 40px;
    }
    QPushButton:pressed { background-color: #0C447C; }
)";

NavigatingPage::NavigatingPage(QWidget *parent)
    : QWidget(parent), progress(0), ttsProcess(new QProcess(this))
{
    setStyleSheet("background-color: #0d1117;");

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);

    // ── 상단바 ──────────────────────────────
    QWidget *topBar = new QWidget;
    topBar->setFixedHeight(60);
    topBar->setStyleSheet("background-color: #0C447C;");
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);

    QLabel *topTitle = new QLabel("이동 중");
    topTitle->setStyleSheet("color: white; font-size: 20px; font-weight: bold;");

    QPushButton *eStop = new QPushButton("긴급 정지");
    eStop->setFixedSize(120, 40);
    eStop->setStyleSheet(ESTOP_STYLE);
    connect(eStop, &QPushButton::clicked, [this]{
        timer->stop();
        speakText("긴급 정지합니다");
        emit emergencyStop();
    });

    topLayout->addWidget(topTitle);
    topLayout->addStretch();
    topLayout->addWidget(eStop);

    // ── 중앙 ────────────────────────────────
    QWidget *center = new QWidget;
    QVBoxLayout *cl = new QVBoxLayout(center);
    cl->setAlignment(Qt::AlignCenter);
    cl->setSpacing(24);

    QLabel *icon = new QLabel("🧭");
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("font-size: 64px; background: transparent;");

    destLabel = new QLabel();
    destLabel->setAlignment(Qt::AlignCenter);
    destLabel->setStyleSheet(
        "color: #ffffff; font-size: 28px; font-weight: bold; background: transparent;");

    progressBar = new QProgressBar;
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setFixedHeight(12);
    progressBar->setTextVisible(false);
    progressBar->setStyleSheet(R"(
        QProgressBar { background: #1e2530; border-radius: 6px; }
        QProgressBar::chunk { background: #185FA5; border-radius: 6px; }
    )");

    statusLabel = new QLabel("이동 중...");
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet(
        "color: #888888; font-size: 16px; background: transparent;");

    QPushButton *homeBtn = new QPushButton("메뉴로 돌아가기");
    homeBtn->setStyleSheet(HOME_STYLE);
    homeBtn->setFixedWidth(240);
    homeBtn->setVisible(false);
    connect(homeBtn, &QPushButton::clicked, this, &NavigatingPage::goHome);

    cl->addWidget(icon);
    cl->addWidget(destLabel);
    cl->addWidget(progressBar);
    cl->addWidget(statusLabel);
    cl->addSpacing(16);
    cl->addWidget(homeBtn, 0, Qt::AlignCenter);

    root->addWidget(topBar);
    root->addWidget(center, 1);

    homeBtn->setObjectName("homeBtn");

    // ── 타이머 ───────────────────────────────
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, [this]{
        progress += 2;
        progressBar->setValue(progress);
        if (progress >= 100) {
            timer->stop();
        }
    });
}

void NavigatingPage::setDestination(const QString &dest) {
    currentDest_ = dest;                               // ← 변경
    destLabel->setText(dest + " (으)로 이동 중...");
    progress = 0;
    progressBar->setValue(0);
    statusLabel->setText("이동 중...");

    if (auto *btn = findChild<QPushButton*>("homeBtn"))
        btn->setVisible(false);

    timer->start(200);
}

void NavigatingPage::showArrival(const QString &dest) {
    timer->stop();
    progressBar->setValue(100);

    QString msg;
    if (dest == "화장실") {
        destLabel->setText("화장실");
        msg = "화장실에 도착했습니다";
    } else if (dest == "출입구") {
        destLabel->setText("출입구");
        msg = "출입구에 도착했습니다";
    } else {
        destLabel->setText(dest);
        msg = dest + "에 도착했습니다";
    }

    statusLabel->setText(msg);
    speakText(msg);

    if (auto *btn = findChild<QPushButton*>("homeBtn"))
        btn->setVisible(true);
}

void NavigatingPage::stopTTS() {
    if (ttsProcess->state() != QProcess::NotRunning) {
        ttsProcess->kill();
        ttsProcess->waitForFinished(300);
    }
}

void NavigatingPage::speakText(const QString &text) {
    if (ttsProcess->state() != QProcess::NotRunning) {
        ttsProcess->kill();
        ttsProcess->waitForFinished(500);
    }
    QString safeText = text;
    safeText.replace("'", " ");
    QString script = QString(
        "from gtts import gTTS; gTTS('%1', lang='ko').save('/tmp/tts_nav.mp3')"
    ).arg(safeText);

    QProcess *gen = new QProcess(this);
    connect(gen, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, gen](int, QProcess::ExitStatus){
                gen->deleteLater();
                ttsProcess->start("mpg123", QStringList() << "/tmp/tts_nav.mp3");
            });
    gen->start("python3", QStringList() << "-c" << script);
}