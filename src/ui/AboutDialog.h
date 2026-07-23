#pragma once
#include <QDialog>
#include <QWidget>

class AboutWidget : public QWidget {
    Q_OBJECT
public:
    explicit AboutWidget(QWidget* parent = nullptr);
    ~AboutWidget() override = default;

private:
    void setupUI();
};

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
    ~AboutDialog() override = default;
};
