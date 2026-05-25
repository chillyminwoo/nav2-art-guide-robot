#include "qrpage.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QtEndian>
#include <QProcess>

QRPage::QRPage(QWidget *parent)
    : QWidget(parent)
    , socket(new QTcpSocket(this))
    , ttsProcess(new QProcess(this))
{
    setStyleSheet("background-color: #0d1117;");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(24);

    iconLabel = new QLabel("🎫");
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet("font-size: 80px; background: transparent;");

    titleLabel = new QLabel("QR 코드를 스캔해 주세요");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(
        "color: #000000; font-size: 28px; font-weight: bold; background: transparent;");

    subLabel = new QLabel("입구의 QR 리더기에 티켓을 제시하세요");
    subLabel->setAlignment(Qt::AlignCenter);
    subLabel->setStyleSheet(
        "color: #888888; font-size: 16px; background: transparent;");

    layout->addWidget(iconLabel);
    layout->addWidget(titleLabel);
    layout->addWidget(subLabel);

    // ── ttsProcess finished: 생성자에서 딱 한 번만 연결 ──────────
    connect(ttsProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
                m_ttsBusy = false;
                qDebug() << "QRPage: TTS 재생 완료";

                if (m_pendingAuthSuccess) {
                    // AUTH_OK TTS 완료 → 페이지 전환
                    m_pendingAuthSuccess = false;
                    emit authSuccess();
                } else if (m_pendingShowWaiting) {
                    // AUTH_FAIL TTS 완료 → 대기 화면 복구
                    m_pendingShowWaiting = false;
                    showWaiting();
                }
            });

    // 소켓 연결
    connect(socket, &QTcpSocket::connected,    this, &QRPage::onConnected);
    connect(socket, &QTcpSocket::readyRead,    this, &QRPage::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &QRPage::onDisconnected);
}

void QRPage::connectToServer(const QString &host, quint16 port) {
    socket->connectToHost(host, port);
}

void QRPage::reset() {
    killAllTTS();   // 플래그 포함 전부 초기화
    showWaiting();
}

// genProcess(gTTS 생성)와 ttsProcess(mpg123 재생) 모두 즉시 종료
void QRPage::killAllTTS() {
    if (genProcess && genProcess->state() != QProcess::NotRunning) {
        genProcess->kill();
        genProcess->waitForFinished(300);
    }
    if (ttsProcess->state() != QProcess::NotRunning) {
        ttsProcess->kill();
        ttsProcess->waitForFinished(300);
    }
    m_ttsBusy            = false;
    m_pendingAuthSuccess = false;
    m_pendingShowWaiting = false;
}

void QRPage::stopTTS() {
    killAllTTS();
}

void QRPage::onConnected() {
    qDebug() << "QRPage: server.py(9994) 연결 완료";
}

void QRPage::onDisconnected() {
    qDebug() << "QRPage: 연결 끊김. 3초 후 재연결...";
    QTimer::singleShot(3000, this, [this]{
        socket->connectToHost(socket->peerName(), socket->peerPort());
    });
}

void QRPage::onReadyRead() {
    buffer.append(socket->readAll());

    while (buffer.size() >= 4) {
        quint32 msgLen = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(buffer.constData()));

        if (buffer.size() < 4 + static_cast<int>(msgLen))
            break;

        QByteArray msgData = buffer.mid(4, msgLen);
        buffer.remove(0, 4 + msgLen);

        QString msg = QString::fromUtf8(msgData);
        qDebug() << "QRPage 수신:" << msg;

        if (msg == "AUTH_OK") {
            // 진행 중인 TTS 모두 즉시 종료 후 새로 시작
            killAllTTS();

            iconLabel->setText("✅");
            titleLabel->setText("인증 성공!");
            titleLabel->setStyleSheet(
                "color: #4CAF50; font-size: 28px; font-weight: bold; background: transparent;");
            subLabel->setText("입장을 환영합니다");

            // TTS 완료 후 authSuccess emit
            m_pendingAuthSuccess = true;
            speakText("티켓이 확인되었습니다");

        } else if (msg == "AUTH_FAIL") {
            // TTS 재생 중이면 무시 → 말이 끊기지 않음
            if (m_ttsBusy) {
                qDebug() << "QRPage: TTS 재생 중 → AUTH_FAIL 무시";
                continue;
            }

            showFail();

            // TTS 완료 후 showWaiting() 호출 (타이머 조건 제거)
            m_pendingShowWaiting = true;
            speakText("티켓 확인이 불가합니다");
        }
    }
}

void QRPage::showWaiting() {
    iconLabel->setText("🎫");
    titleLabel->setText("QR 코드를 스캔해 주세요");
    titleLabel->setStyleSheet(
        "color: #000000; font-size: 28px; font-weight: bold; background: transparent;");
    subLabel->setText("입구의 QR 리더기에 티켓을 제시하세요");
}

void QRPage::showFail() {
    iconLabel->setText("❌");
    titleLabel->setText("인증 실패");
    titleLabel->setStyleSheet(
        "color: #e53935; font-size: 28px; font-weight: bold; background: transparent;");
    subLabel->setText("유효하지 않거나 이미 사용된 티켓입니다");
}

void QRPage::speakText(const QString &text) {
    m_ttsBusy = true;

    QString safeText = text;
    safeText.replace("'", " ");

    QString script = QString(
        "from gtts import gTTS; gTTS('%1', lang='ko').save('/tmp/tts_qr.mp3')"
    ).arg(safeText);

    // 이전 genProcess가 남아있으면 정리
    if (genProcess) {
        if (genProcess->state() != QProcess::NotRunning) {
            genProcess->kill();
            genProcess->waitForFinished(300);
        }
        genProcess->deleteLater();
        genProcess = nullptr;
    }

    genProcess = new QProcess(this);
    connect(genProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
                Q_UNUSED(exitCode); Q_UNUSED(status);
                // kill된 경우(exit code != 0)에는 mpg123 실행하지 않음
                if (ttsProcess->state() == QProcess::NotRunning && m_ttsBusy) {
                    ttsProcess->start("mpg123", QStringList() << "/tmp/tts_qr.mp3");
                }
                if (genProcess) {
                    genProcess->deleteLater();
                    genProcess = nullptr;
                }
            });

    genProcess->start("python3", QStringList() << "-c" << script);
}
