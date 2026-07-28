#include "ModelDetailsDialog.h"
#include "Tone3000Dialog.h"
#include <QDir>
#include <QScrollArea>
#include <QStyle>

ModelDetailsDialog::ModelDetailsDialog(std::shared_ptr<AudioNode> node, AudioEngine* engine, QWidget* parent)
    : QDialog(parent), m_node(node), m_engine(engine) {
    setWindowTitle("Model Details - TONE3000");
    resize(560, 560);
    setStyleSheet("QDialog { background-color: #1A1A1D; color: #E0E0E0; }");

    if (m_node) {
        m_filePath = QString::fromStdString(m_node->getModelFilePath());
        m_sourceUrl = QString::fromStdString(m_node->getModelSourceUrl());
        m_meta = m_node->getModelMetadata();
    }

    setupUI();
}

void ModelDetailsDialog::setupUI() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    // Scrollable area for content
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; } QWidget { background: transparent; }");

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // --- HERO BANNER ---
    QFrame* headerCard = new QFrame(scrollContent);
    headerCard->setStyleSheet("QFrame { background-color: #242528; border-radius: 8px; border: 1px solid #333438; padding: 14px; }");
    auto* headerLayout = new QVBoxLayout(headerCard);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    size_t slash = m_filePath.toStdString().find_last_of("/\\");
    std::string filename = (slash != std::string::npos) ? m_filePath.toStdString().substr(slash + 1) : m_filePath.toStdString();
    const std::string& displayName = m_node ? m_node->getModelDisplayName() : "";
    QString titleStr = QString::fromStdString(displayName.empty() ? (m_meta.toneTitle.empty() ? filename : m_meta.toneTitle) : displayName);

    QLabel* titleLabel = new QLabel(titleStr, headerCard);
    titleLabel->setWordWrap(true);
    titleLabel->setStyleSheet("font-size: 17px; font-weight: bold; color: #FFFFFF; background: transparent; border: none;");
    headerLayout->addWidget(titleLabel);

    const QString captureName = QString::fromStdString(m_meta.toneTitle);
    if (!captureName.isEmpty() && captureName != titleStr) {
        auto* captureLabel = new QLabel(QString("Capture: %1").arg(captureName.toHtmlEscaped()), headerCard);
        captureLabel->setWordWrap(true);
        captureLabel->setStyleSheet("font-size: 12px; color: #B0BEC5; background: transparent; border: none;");
        headerLayout->addWidget(captureLabel);
    }

    // Badges Row
    QHBoxLayout* badgeLayout = new QHBoxLayout();
    badgeLayout->setSpacing(6);

    QLabel* namBadge = new QLabel("NAM", headerCard);
    namBadge->setStyleSheet("background-color: #00B0FF; color: #000000; font-weight: bold; font-size: 10px; border-radius: 3px; padding: 2px 6px;");
    badgeLayout->addWidget(namBadge);

    QString verStr = QString::fromStdString(m_meta.version);
    if (verStr.isEmpty()) verStr = "v0.5.x";
    QLabel* verBadge = new QLabel(verStr, headerCard);
    verBadge->setStyleSheet("background-color: #37474F; color: #80DEEA; font-weight: bold; font-size: 10px; border-radius: 3px; padding: 2px 6px;");
    badgeLayout->addWidget(verBadge);

    QString gearTypeStr = QString::fromStdString(m_meta.gearType);
    if (!gearTypeStr.isEmpty()) {
        QLabel* typeBadge = new QLabel(gearTypeStr, headerCard);
        typeBadge->setStyleSheet("background-color: #2E7D32; color: #FFFFFF; font-weight: bold; font-size: 10px; border-radius: 3px; padding: 2px 6px;");
        badgeLayout->addWidget(typeBadge);
    }
    badgeLayout->addStretch();
    headerLayout->addLayout(badgeLayout);

    QString authorStr = QString::fromStdString(m_meta.author.empty() ? m_meta.modeledBy : m_meta.author);
    if (authorStr.isEmpty()) authorStr = "Unknown Creator";
    QLabel* authorLabel = new QLabel(QString("Created by <b>%1</b>").arg(authorStr.toHtmlEscaped()), headerCard);
    authorLabel->setStyleSheet("font-size: 12px; color: #B0BEC5; background: transparent; border: none; margin-top: 4px;");
    headerLayout->addWidget(authorLabel);

    mainLayout->addWidget(headerCard);

    // --- DESCRIPTION SECTION ---
    QString descStr = QString::fromStdString(m_meta.description).trimmed();
    if (!descStr.isEmpty()) {
        QFrame* descCard = new QFrame(scrollContent);
        descCard->setStyleSheet("QFrame { background-color: #242528; border-radius: 8px; border: 1px solid #333438; padding: 14px; }");
        auto* descLayout = new QVBoxLayout(descCard);
        descLayout->setContentsMargins(0, 0, 0, 0);
        descLayout->setSpacing(6);

        QLabel* descHeader = new QLabel("DESCRIPTION / CAPTURE NOTES", descCard);
        descHeader->setStyleSheet("font-weight: bold; font-size: 11px; color: #80DEEA; background: transparent; border: none;");
        descLayout->addWidget(descHeader);

        QString formattedDesc = descStr.toHtmlEscaped().replace("\n", "<br>");
        QLabel* descText = new QLabel(formattedDesc, descCard);
        descText->setWordWrap(true);
        descText->setTextInteractionFlags(Qt::TextSelectableByMouse);
        descText->setStyleSheet("font-size: 11px; color: #D1D5DB; line-height: 1.4; background: transparent; border: none;");
        descLayout->addWidget(descText);

        mainLayout->addWidget(descCard);
    }

    // --- GEAR SPECIFICATIONS ---
    QFrame* gearCard = new QFrame(scrollContent);
    gearCard->setStyleSheet("QFrame { background-color: #242528; border-radius: 8px; border: 1px solid #333438; padding: 14px; }");
    auto* gearLayout = new QVBoxLayout(gearCard);
    gearLayout->setContentsMargins(0, 0, 0, 0);
    gearLayout->setSpacing(6);

    QLabel* gearHeader = new QLabel("GEAR & CAPTURE DETAILS", gearCard);
    gearHeader->setStyleSheet("font-weight: bold; font-size: 11px; color: #80DEEA; background: transparent; border: none;");
    gearLayout->addWidget(gearHeader);

    QString gearMakeStr = QString::fromStdString(m_meta.gearMake).trimmed();
    QString gearModelStr = QString::fromStdString(m_meta.gearModel).trimmed();
    QString makeModelStr;
    if (gearMakeStr.isEmpty()) makeModelStr = gearModelStr;
    else if (gearModelStr.isEmpty() || gearModelStr.contains(gearMakeStr, Qt::CaseInsensitive)) makeModelStr = gearModelStr;
    else makeModelStr = gearMakeStr + " " + gearModelStr;
    if (makeModelStr.isEmpty()) makeModelStr = "Not specified";

    QLabel* makeModelLabel = new QLabel(QString("<b>Make & Model:</b> %1").arg(makeModelStr.toHtmlEscaped()), gearCard);
    makeModelLabel->setStyleSheet("font-size: 11px; color: #CFD8DC; background: transparent; border: none;");
    makeModelLabel->setWordWrap(true);
    gearLayout->addWidget(makeModelLabel);

    if (!m_meta.tags.empty()) {
        QLabel* tagsHeader = new QLabel("<b>Tags:</b>", gearCard);
        tagsHeader->setStyleSheet("font-size: 11px; color: #CFD8DC; background: transparent; border: none;");
        gearLayout->addWidget(tagsHeader);

        QHBoxLayout* tagsPillLayout = new QHBoxLayout();
        tagsPillLayout->setSpacing(4);

        QStringList tagItems = QString::fromStdString(m_meta.tags).split(",", Qt::SkipEmptyParts);
        for (QString tag : tagItems) {
            tag = tag.trimmed();
            if (tag.isEmpty()) continue;
            QLabel* tagPill = new QLabel(tag, gearCard);
            tagPill->setStyleSheet("background-color: #1A232A; color: #80DEEA; font-size: 10px; border-radius: 10px; padding: 2px 8px; border: 1px solid #2B3D4F;");
            tagsPillLayout->addWidget(tagPill);
        }
        tagsPillLayout->addStretch();
        gearLayout->addLayout(tagsPillLayout);
    }

    mainLayout->addWidget(gearCard);

    // --- TECHNICAL SPECIFICATIONS ---
    QFrame* techCard = new QFrame(scrollContent);
    techCard->setStyleSheet("QFrame { background-color: #242528; border-radius: 8px; border: 1px solid #333438; padding: 14px; }");
    auto* techLayout = new QVBoxLayout(techCard);
    techLayout->setContentsMargins(0, 0, 0, 0);
    techLayout->setSpacing(6);

    QLabel* techHeader = new QLabel("TECHNICAL SPECIFICATIONS", techCard);
    techHeader->setStyleSheet("font-weight: bold; font-size: 11px; color: #80DEEA; background: transparent; border: none;");
    techLayout->addWidget(techHeader);

    QString archStr = QString::fromStdString(m_meta.architecture.empty() ? "NAM Neural Engine" : m_meta.architecture);
    QLabel* archLabel = new QLabel(QString("<b>Architecture:</b> %1").arg(archStr.toHtmlEscaped()), techCard);
    archLabel->setStyleSheet("font-size: 11px; color: #CFD8DC; background: transparent; border: none;");
    techLayout->addWidget(archLabel);

    QString loudnessStr = (m_meta.loudness != 0.0) ? QString("%1 dB").arg(QString::number(m_meta.loudness, 'f', 1)) : "Not reported";
    QLabel* loudnessLabel = new QLabel(QString("<b>Loudness:</b> %1").arg(loudnessStr), techCard);
    loudnessLabel->setStyleSheet("font-size: 11px; color: #CFD8DC; background: transparent; border: none;");
    techLayout->addWidget(loudnessLabel);

    QString sampleRateStr = (m_meta.sampleRate > 0.0) ? QString("%1 kHz").arg(QString::number(m_meta.sampleRate / 1000.0, 'f', 1)) : "48.0 kHz";
    QLabel* srLabel = new QLabel(QString("<b>Native Sample Rate:</b> %1").arg(sampleRateStr), techCard);
    srLabel->setStyleSheet("font-size: 11px; color: #CFD8DC; background: transparent; border: none;");
    techLayout->addWidget(srLabel);

    mainLayout->addWidget(techCard);

    // --- FILE LOCATION ---
    QFrame* pathCard = new QFrame(scrollContent);
    pathCard->setStyleSheet("QFrame { background-color: #242528; border-radius: 8px; border: 1px solid #333438; padding: 14px; }");
    auto* pathLayout = new QVBoxLayout(pathCard);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(4);

    QLabel* pathHeader = new QLabel("FILE LOCATION", pathCard);
    pathHeader->setStyleSheet("font-weight: bold; font-size: 11px; color: #80DEEA; background: transparent; border: none;");
    pathLayout->addWidget(pathHeader);

    QString pathDisplay = m_filePath;
    if (pathDisplay.startsWith(QDir::homePath())) {
        pathDisplay.replace(0, QDir::homePath().length(), "~");
    }
    if (pathDisplay.isEmpty()) pathDisplay = "No local file path recorded";

    QHBoxLayout* pathRow = new QHBoxLayout();
    pathRow->setSpacing(6);

    QLabel* pathLabel = new QLabel(pathDisplay, pathCard);
    pathLabel->setWordWrap(true);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setStyleSheet("font-size: 10px; color: #4FC3F7; background: transparent; border: none;");
    pathRow->addWidget(pathLabel, 1);

    QPushButton* copyBtn = new QPushButton("Copy", pathCard);
    copyBtn->setStyleSheet("QPushButton { background-color: #333338; color: #E0E0E0; border-radius: 4px; padding: 3px 8px; font-size: 10px; border: none; } QPushButton:hover { background-color: #44444A; }");
    pathRow->addWidget(copyBtn);
    connect(copyBtn, &QPushButton::clicked, this, &ModelDetailsDialog::onCopyPath);

    pathLayout->addLayout(pathRow);
    mainLayout->addWidget(pathCard);

    scrollArea->setWidget(scrollContent);
    rootLayout->addWidget(scrollArea, 1);

    // --- ACTION BUTTONS TOOLBAR ---
    QFrame* actionCard = new QFrame(this);
    actionCard->setStyleSheet("QFrame { background-color: transparent; border: none; }");
    auto* actionLayout = new QHBoxLayout(actionCard);
    actionLayout->setContentsMargins(0, 4, 0, 0);
    actionLayout->setSpacing(6);

    QPushButton* browserBtn = new QPushButton("Open in TONE3000 Browser", actionCard);
    browserBtn->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    browserBtn->setStyleSheet("QPushButton { background-color: #2E7D32; color: white; font-weight: bold; border-radius: 4px; padding: 8px 12px; font-size: 11px; border: none; } QPushButton:hover { background-color: #388E3C; }");
    actionLayout->addWidget(browserBtn);
    connect(browserBtn, &QPushButton::clicked, this, &ModelDetailsDialog::onOpenTone3000Browser);

    if (!m_sourceUrl.isEmpty()) {
        QPushButton* webBtn = new QPushButton("View on Web", actionCard);
        webBtn->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
        webBtn->setStyleSheet("QPushButton { background-color: #00897B; color: white; font-weight: bold; border-radius: 4px; padding: 8px 12px; font-size: 11px; border: none; } QPushButton:hover { background-color: #009688; }");
        actionLayout->addWidget(webBtn);
        connect(webBtn, &QPushButton::clicked, this, &ModelDetailsDialog::onOpenOnWeb);
    }

    QPushButton* exportBtn = new QPushButton("Export NAM...", actionCard);
    exportBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    exportBtn->setStyleSheet("QPushButton { background-color: #333338; color: #E0E0E0; font-weight: bold; border-radius: 4px; padding: 8px 12px; font-size: 11px; border: none; } QPushButton:hover { background-color: #44444A; }");
    actionLayout->addWidget(exportBtn);
    connect(exportBtn, &QPushButton::clicked, this, &ModelDetailsDialog::onExportNam);

    QPushButton* closeBtn = new QPushButton("Close", actionCard);
    closeBtn->setStyleSheet("QPushButton { background-color: #424242; color: white; font-weight: bold; border-radius: 4px; padding: 8px 14px; font-size: 11px; border: none; } QPushButton:hover { background-color: #616161; }");
    actionLayout->addWidget(closeBtn);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    rootLayout->addWidget(actionCard);
}

void ModelDetailsDialog::onOpenTone3000Browser() {
    Tone3000Dialog dialog(m_node.get(), m_engine, this);
    if (!m_sourceUrl.isEmpty() && !m_meta.toneTitle.empty()) {
        dialog.setInitialTone(m_sourceUrl, QString::fromStdString(m_meta.toneTitle));
    } else if (!m_meta.toneTitle.empty()) {
        dialog.setInitialSearchQuery(QString::fromStdString(m_meta.toneTitle));
    }
    if (dialog.exec() == QDialog::Accepted) {
        std::string filePath = dialog.getDownloadedModelPath();
        if (!filePath.empty() && m_node) {
            m_engine->suspendProcessing();
            m_node->setFileProperty("http://github.com/mikeoliphant/neural-amp-modeler-lv2#model", filePath);
            m_engine->resumeProcessing();

            AudioNode::ModelMetadata meta = dialog.getDownloadedMetadata();
            m_node->setModelMetadata(meta);
            m_node->setModelVariants(dialog.getDownloadedVariants());

            size_t slash = filePath.find_last_of("/\\");
            std::string filename = (slash != std::string::npos) ? filePath.substr(slash + 1) : filePath;
            const QString toneName = dialog.getDownloadedToneName();
            m_node->setModelDisplayName((toneName.isEmpty() ? QString::fromStdString(filename) : toneName).toStdString());
            m_node->setModelSourceUrl(dialog.getDownloadedToneUrl().toStdString());
            
            accept();
        }
    }
}

void ModelDetailsDialog::onOpenOnWeb() {
    if (!m_sourceUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(m_sourceUrl));
    }
}

void ModelDetailsDialog::onExportNam() {
    if (!QFileInfo(m_filePath).isFile()) {
        QMessageBox::warning(this, "Export NAM", "The active NAM file is unavailable.");
        return;
    }
    const QString destination = QFileDialog::getSaveFileName(
        this, "Export NAM", QFileInfo(m_filePath).fileName(), "NAM Models (*.nam);;All Files (*)");
    if (destination.isEmpty()) return;
    if (QFileInfo(destination).absoluteFilePath() == QFileInfo(m_filePath).absoluteFilePath()) return;
    QFile::remove(destination);
    if (!QFile::copy(m_filePath, destination)) {
        QMessageBox::warning(this, "Export NAM", "Could not export the NAM file.");
    } else {
        QMessageBox::information(this, "Export NAM", "NAM file exported successfully.");
    }
}

void ModelDetailsDialog::onCopyPath() {
    QApplication::clipboard()->setText(m_filePath);
}
