#include "artworkpage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTcpSocket>
#include <QProcess>
#include <QDataStream>
#include <QTimer>
#include <QtEndian>

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

ArtworkPage::ArtworkPage(QWidget *parent)
    : QWidget(parent)
    , socket(new QTcpSocket(this))
    , ttsProcess(new QProcess(this))
{
    setStyleSheet("background-color: #0d1117;");

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);

    // ── 상단바 ──────────────────────────────
    QWidget *topBar = new QWidget;
    topBar->setFixedHeight(60);
    topBar->setStyleSheet("background-color: #085041;");
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);

    QLabel *topTitle = new QLabel("작품 안내");
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

    // ── 중앙 컨텐츠 ─────────────────────────
    QWidget *center = new QWidget;
    QVBoxLayout *cl = new QVBoxLayout(center);
    cl->setAlignment(Qt::AlignCenter);
    cl->setSpacing(12);
    cl->setContentsMargins(40, 40, 40, 40);

    titleLabel = new QLabel("작품을 인식하는 중...");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(
        "color: #ffffff; font-size: 30px; font-weight: bold; background: transparent;");

    artistLabel = new QLabel;
    artistLabel->setAlignment(Qt::AlignCenter);
    artistLabel->setStyleSheet(
        "color: #aaaaaa; font-size: 20px; background: transparent;");

    yearLabel = new QLabel;
    yearLabel->setAlignment(Qt::AlignCenter);
    yearLabel->setStyleSheet(
        "color: #888888; font-size: 16px; background: transparent;");

    genreLabel = new QLabel;
    genreLabel->setAlignment(Qt::AlignCenter);
    genreLabel->setStyleSheet(
        "color: #888888; font-size: 16px; background: transparent;");

    locationLabel = new QLabel;
    locationLabel->setAlignment(Qt::AlignCenter);
    locationLabel->setStyleSheet(
        "color: #666666; font-size: 14px; background: transparent;");

    QFrame *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #2d3748;");

    descLabel = new QLabel;
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(
        "color: #cccccc; font-size: 16px; background: transparent; line-height: 1.6;");

    QPushButton *homeBtn = new QPushButton("메뉴로 돌아가기");
    homeBtn->setStyleSheet(HOME_STYLE);
    homeBtn->setFixedWidth(240);
    connect(homeBtn, &QPushButton::clicked, this, &ArtworkPage::goHome);

    cl->addWidget(titleLabel);
    cl->addWidget(artistLabel);
    cl->addWidget(yearLabel);
    cl->addWidget(genreLabel);
    cl->addWidget(locationLabel);
    cl->addSpacing(8);
    cl->addWidget(line);
    cl->addSpacing(8);
    cl->addWidget(descLabel);
    cl->addSpacing(24);
    cl->addWidget(homeBtn, 0, Qt::AlignCenter);

    root->addWidget(topBar);
    root->addWidget(center, 1);

    // ── 소켓 시그널 연결 ─────────────────────
    connect(socket, &QTcpSocket::connected,    this, &ArtworkPage::onConnected);
    connect(socket, &QTcpSocket::readyRead,    this, &ArtworkPage::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &ArtworkPage::onDisconnected);

    connect(this, &ArtworkPage::artworkReceived,
            this, &ArtworkPage::setArtworkInfo);
}

// ── 서버 연결 ────────────────────────────────
void ArtworkPage::connectToServer(const QString &host, quint16 port) {
    socket->connectToHost(host, port);
}

void ArtworkPage::onConnected() {
    qDebug() << "ArtworkPage 서버 연결 완료 (포트 9998)";
}

void ArtworkPage::onDisconnected() {
    qDebug() << "ArtworkPage 서버 연결 끊김. 3초 후 재연결...";
    QTimer::singleShot(3000, this, [this]{
        socket->connectToHost(socket->peerName(), socket->peerPort());
    });
}

// ── 인식 제어 ────────────────────────────────
void ArtworkPage::sendCommand(const QString &cmd) {
    if (socket->state() != QAbstractSocket::ConnectedState) return;
    QByteArray msg = cmd.toUtf8();
    quint32 size = qToBigEndian<quint32>(msg.size());
    socket->write(reinterpret_cast<const char*>(&size), 4);
    socket->write(msg);
}

void ArtworkPage::startRecognition() {
    m_active = true;
    buffer.clear();
    titleLabel->setText("작품을 인식하는 중...");
    artistLabel->clear();
    yearLabel->clear();
    genreLabel->clear();
    locationLabel->clear();
    descLabel->clear();
    sendCommand("START");
}

void ArtworkPage::stopRecognition() {
    m_active = false;
    sendCommand("STOP");
}

void ArtworkPage::stopTTS() {
    if (ttsProcess->state() != QProcess::NotRunning) {
        ttsProcess->kill();
        ttsProcess->waitForFinished(300);
    }
}

// ── 데이터 수신 ──────────────────────────────
void ArtworkPage::onReadyRead() {
    buffer.append(socket->readAll());

    if (!m_active) {
        buffer.clear();
        return;
    }

    while (buffer.size() >= 4) {
        quint32 msgLen = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(buffer.constData()));

        if (buffer.size() < 4 + (int)msgLen)
            break;

        QByteArray msgData = buffer.mid(4, msgLen);
        buffer.remove(0, 4 + msgLen);

        QString msg = QString::fromUtf8(msgData);
        QStringList parts = msg.split('|');
        if (parts.size() == 7) {
            emit artworkReceived(
                parts[0], parts[1], parts[2],
                parts[3], parts[4], parts[5], parts[6]
            );
        }
    }
}

// ── UI 업데이트 ──────────────────────────────
void ArtworkPage::setArtworkInfo(const QString &name,
                                  const QString &artist,
                                  const QString &year,
                                  const QString &genre,
                                  const QString &technique,
                                  const QString &location,
                                  const QString &description)
{
    titleLabel->setText(name);
    artistLabel->setText(artist);
    yearLabel->setText(year + "  ·  " + technique);
    genreLabel->setText(genre);
    locationLabel->setText("📍 " + location);
    descLabel->setText(description);

    QString ttsText = QString("%1. %2 작. %3년 작품입니다. %4")
                          .arg(name, artist, year, description);
    speakText(ttsText);
}

// ── TTS ─────────────────────────────────────
void ArtworkPage::speakText(const QString &text) {
    if (ttsProcess->state() != QProcess::NotRunning) {
        ttsProcess->kill();
        ttsProcess->waitForFinished(500);
    }

    QString safeText = text;
    safeText.replace("'", " ");

    QString script = QString(
        "from gtts import gTTS; gTTS('%1', lang='ko').save('/tmp/tts.mp3')"
    ).arg(safeText);

    QProcess *genProcess = new QProcess(this);
    connect(genProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, genProcess](int, QProcess::ExitStatus) {
                genProcess->deleteLater();
                ttsProcess->start("mpg123", QStringList() << "/tmp/tts.mp3");
            });
    genProcess->start("python3", QStringList() << "-c" << script);
}