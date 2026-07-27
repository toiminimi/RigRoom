#include "AboutDialog.h"
#include "Version.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QPixmap>

AboutWidget::AboutWidget(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void AboutWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    auto* container = new QWidget();
    container->setStyleSheet("background: transparent; color: #e0e6ed; font-family: 'Segoe UI', Ubuntu, sans-serif;");
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(14);

    // 1. Header Section
    auto* headerLayout = new QHBoxLayout();
    auto* logoLabel = new QLabel(container);
    logoLabel->setPixmap(QPixmap(":/branding/rigroom-icon.png").scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setFixedSize(52, 52);
    headerLayout->addWidget(logoLabel);

    auto* titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);
    
    auto* nameVerLayout = new QHBoxLayout();
    auto* titleLabel = new QLabel("RigRoom", container);
    titleLabel->setStyleSheet("font-size: 19px; font-weight: 800; color: #ffffff;");
    auto* verLabel = new QLabel(QString("v") + RIGROOM_VERSION_STRING, container);
    verLabel->setStyleSheet("font-size: 10px; font-weight: bold; color: #00e5ff; background-color: rgba(0, 229, 255, 0.12); padding: 2px 7px; border-radius: 4px;");
    nameVerLayout->addWidget(titleLabel);
    nameVerLayout->addWidget(verLabel);
    nameVerLayout->addStretch();
    titleLayout->addLayout(nameVerLayout);

    auto* tagline = new QLabel("High-Performance Open-Source Guitar Multieffects & Audio Plugin Host", container);
    tagline->setStyleSheet("font-size: 12px; color: #8a94a6;");
    titleLayout->addWidget(tagline);

    auto* repoLabel = new QLabel(
        "<a href=\"https://github.com/toiminimi/RigRoom\" style=\"color: #00e5ff; text-decoration: none;\">https://github.com/toiminimi/RigRoom</a>",
        container
    );
    repoLabel->setOpenExternalLinks(true);
    repoLabel->setStyleSheet("font-size: 11px; margin-top: 2px;");
    titleLayout->addWidget(repoLabel);

    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();
    layout->addLayout(headerLayout);

    // Divider 1
    auto* div1 = new QFrame(container);
    div1->setFrameShape(QFrame::HLine);
    div1->setStyleSheet("color: #242834; background-color: #242834; border: none; height: 1px;");
    layout->addWidget(div1);

    // 2. Core Features Section (Flat bullet points)
    auto* featHeader = new QLabel("<b>Core Engine Features</b>", container);
    featHeader->setStyleSheet("font-size: 13px; color: #ffffff;");
    layout->addWidget(featHeader);

    auto* featList = new QLabel(
        "• <b>Multi-Format Plugin Hosting:</b> Native LV2, VST3, CLAP, and Neural Amp Modeler (NAM) support.<br>"
        "• <b>Flexible Routing Canvas:</b> Drag-and-drop signal flow layout with parallel split & mix branches.<br>"
        "• <b>TONE3000 Integration:</b> Search and load neural amp models directly inside the application.<br>"
        "• <b>Low-Latency Engine:</b> Real-time JACK Audio Connection Kit and PipeWire support.<br>"
        "• <b>Open License:</b> Released under the GNU General Public License v3.0 (GPLv3).",
        container
    );
    featList->setWordWrap(true);
    featList->setStyleSheet("color: #b0b8c8; font-size: 12px; line-height: 1.5; padding-left: 4px;");
    layout->addWidget(featList);

    // Divider 2
    auto* div2 = new QFrame(container);
    div2->setFrameShape(QFrame::HLine);
    div2->setStyleSheet("color: #242834; background-color: #242834; border: none; height: 1px;");
    layout->addWidget(div2);

    // 3. Open Source Components & Trademarks
    auto* creditsHeader = new QLabel("<b>Open Source Credits & Trademarks</b>", container);
    creditsHeader->setStyleSheet("font-size: 13px; color: #ffffff;");
    layout->addWidget(creditsHeader);

    auto* creditsText = new QLabel(
        "• <b>Qt 6 Framework</b> (LGPLv3) — <a href=\"https://www.qt.io/\" style=\"color: #00e5ff; text-decoration: none;\">qt.io</a><br>"
        "• <b>JACK Audio Connection Kit</b> (LGPL) — <a href=\"https://jackaudio.org/\" style=\"color: #00e5ff; text-decoration: none;\">jackaudio.org</a><br>"
        "• <b>Lilv & Suil Libraries</b> (ISC License) — <a href=\"https://drobilla.net/software/lilv\" style=\"color: #00e5ff; text-decoration: none;\">drobilla.net</a><br>"
        "• <b>CLAP C API Specification</b> (MIT License) — <a href=\"https://clap.technology/\" style=\"color: #00e5ff; text-decoration: none;\">clap.technology</a><br>"
        "• <b>Steinberg VST3 SDK</b> (MIT License) — <a href=\"https://www.steinberg.net/\" style=\"color: #00e5ff; text-decoration: none;\">steinberg.net</a>",
        container
    );
    creditsText->setOpenExternalLinks(true);
    creditsText->setWordWrap(true);
    creditsText->setStyleSheet("color: #9aa4b6; font-size: 11px; line-height: 1.5; padding-left: 4px;");
    layout->addWidget(creditsText);

    auto* tmNotice = new QLabel(
        "<i>VST is a registered trademark of Steinberg Media Technologies GmbH. All plugin names and trademarks belong to their respective owners.</i>",
        container
    );
    tmNotice->setWordWrap(true);
    tmNotice->setStyleSheet("color: #727c90; font-size: 10px; font-style: italic; padding-top: 4px;");
    layout->addWidget(tmNotice);

    layout->addStretch();

    scrollArea->setWidget(container);
    mainLayout->addWidget(scrollArea);
}

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("About RigRoom");
    resize(560, 440);
    setMinimumSize(500, 380);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* aboutWidget = new AboutWidget(this);
    layout->addWidget(aboutWidget);

    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    auto* closeBtn = new QPushButton("Close", this);
    closeBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #00b0ff;
            color: #ffffff;
            border: none;
            border-radius: 6px;
            padding: 7px 22px;
            font-weight: bold;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: #00c4ff;
        }
    )");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(closeBtn);

    layout->addLayout(bottomLayout);
}
