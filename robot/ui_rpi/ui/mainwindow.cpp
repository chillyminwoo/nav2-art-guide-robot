#include "mainwindow.h"
#include "pages/qrpage.h"
#include "pages/standbypage.h"
#include "pages/destinationpage.h"
#include "pages/navigatingpage.h"
#include "pages/artworkpage.h"
#include "pages/navigationmanager.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Art Museum Robot");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    stack = new QStackedWidget(this);
    setCentralWidget(stack);

    // 페이지 생성
    qrPage      = new QRPage(this);
    standbyPage = new StandbyPage(this);
    destPage    = new DestinationPage(this);
    navPage     = new NavigatingPage(this);
    artPage     = new ArtworkPage(this);
    navManager  = new NavigationManager(this);

    // enum class Page 순서와 동일하게 추가
    stack->addWidget(qrPage);       // 0  Page::QR
    stack->addWidget(standbyPage);  // 1  Page::Standby
    stack->addWidget(destPage);     // 2  Page::Destination
    stack->addWidget(navPage);      // 3  Page::Navigating
    stack->addWidget(artPage);      // 4  Page::Artwork

    // ── QR 인증 ────────────────────────────────
    // 인증 성공 → StandbyPage
    connect(qrPage, &QRPage::authSuccess, [this]{
        qrPage->reset();
        showPage(Page::Standby);
    });

    // ── Standby → Destination ──────────────────
    connect(standbyPage, &StandbyPage::activated,
            [this]{ showPage(Page::Destination); });

    // ── Destination → Navigating ───────────────
    connect(destPage, &DestinationPage::destSelected,
            [this](const QString &dest){
                navManager->sendDestination(dest);
                navPage->setDestination(dest);
                showPage(Page::Navigating);
            });

    // ── 도착 처리 ──────────────────────────────
    connect(navManager, &NavigationManager::arrived,
            [this](const QString &dest){
                if (dest == "전시관 A" || dest == "전시관 B" || dest == "전시관 C") {
                    // 작품 전시관 → ArtworkPage
                    artPage->startRecognition();
                    showPage(Page::Artwork);
                } else if (dest == "출입구") {
                    // 출입구 도착 → QR 재인증
                    navPage->showArrival(dest);
                } else {
                    // 화장실 등 → 도착 메시지만 표시 (QR 불필요)
                    navPage->showArrival(dest);
                }
            });

    // ── navPage의 "메뉴로 돌아가기" 버튼 ─────────
    // 출입구 도착 후 버튼 누르면 QR 재인증, 그 외(화장실 등)는 Standby
    connect(navPage, &NavigatingPage::goHome,
            [this]{
                if (navPage->currentDest() == "출입구") {
                    qrPage->reset();
                    showPage(Page::QR);
                } else {
                    showPage(Page::Standby);
                }
            });

    // ── artPage의 "메뉴로 돌아가기" 버튼 → Standby ─
    connect(artPage, &ArtworkPage::goHome,
            [this]{
                artPage->stopRecognition();
                showPage(Page::Standby);
            });

    // ── 긴급 정지 처리 (모든 페이지 공통) ───────────
    auto handleEstop = [this]{
        navManager->sendEmergencyStop();
        artPage->stopRecognition();
        showPage(Page::Standby);
    };
    connect(destPage, &DestinationPage::emergencyStop, this, handleEstop);
    connect(navPage,  &NavigatingPage::emergencyStop,  this, handleEstop);
    connect(artPage,  &ArtworkPage::emergencyStop,     this, handleEstop);

    // ── 서버 연결 ──────────────────────────────
    const QString SERVER_IP = "10.10.16.53";
    qrPage->connectToServer(SERVER_IP, 9994);       // QR 인증 결과 수신
    navManager->connectToServer(SERVER_IP, 9997);
    artPage->connectToServer(SERVER_IP, 9998);

    // ── 시작 화면: QR 인증 ─────────────────────
    showPage(Page::QR);
}

void MainWindow::showPage(Page page) {
    qrPage->stopTTS();
    destPage->stopTTS();
    navPage->stopTTS();
    artPage->stopTTS();
    stack->setCurrentIndex(static_cast<int>(page));
}