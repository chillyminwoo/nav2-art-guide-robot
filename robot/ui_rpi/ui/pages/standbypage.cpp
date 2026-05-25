#include "standbypage.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QMouseEvent>

StandbyPage::StandbyPage(QWidget *parent) : QWidget(parent) {
    setStyleSheet("background-color: #0d1117;");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(20);

    QLabel *icon = new QLabel("🤖");
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("font-size: 72px; background: transparent;");

    QLabel *title = new QLabel("미술관 안내 로봇");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color: #ffffff; font-size: 32px; font-weight: bold; background: transparent;");

    QLabel *subtitle = new QLabel("화면을 터치하면 안내를 시작합니다");
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet("color: #888888; font-size: 18px; background: transparent;");

    layout->addWidget(icon);
    layout->addWidget(title);
    layout->addWidget(subtitle);
}

void StandbyPage::mousePressEvent(QMouseEvent *) {
    emit activated();
}
