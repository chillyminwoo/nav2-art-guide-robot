#pragma once
#include <QWidget>

class StandbyPage : public QWidget {
    Q_OBJECT
public:
    explicit StandbyPage(QWidget *parent = nullptr);
signals:
    void activated();
protected:
    void mousePressEvent(QMouseEvent *) override;
};
