#pragma once
#include <QMainWindow>
#include <QStackedWidget>

class StandbyPage;
class DestinationPage;
class NavigatingPage;
class ArtworkPage;
class NavigationManager;
class QRPage;

enum class Page { QR, Standby, Destination, Navigating, Artwork };

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void showPage(Page page);

private:
    QStackedWidget    *stack;
    QRPage            *qrPage;       // ← 추가
    StandbyPage       *standbyPage;
    DestinationPage   *destPage;
    NavigatingPage    *navPage;
    ArtworkPage       *artPage;
    NavigationManager *navManager;
};