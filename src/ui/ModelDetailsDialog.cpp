#include "ModelDetailsDialog.h"
#include "Tone3000Dialog.h"
#include <QDir>

ModelDetailsDialog::ModelDetailsDialog(std::shared_ptr<AudioNode> node, AudioEngine* engine, QWidget* parent)
    : QDialog(parent), m_node(node), m_engine(engine) {
    setWindowTitle("Model Details - TONE3000");
    resize(520, 480);
    setStyleSheet("QDialog { background-color: #1E1E1E; color: #E0E0E0; }");

    if (m_node) {
        m_filePath = QString::fromStdString(m_node->getModelFilePath());
        m_sourceUrl = QString::fromStdString(m_node->getModelSourceUrl());
        m_meta = m_node->getModelMetadata();
    }

    setupUI();
}

void ModelDetailsDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 18, 18, 18);
    mainLayout->setSpacing(12);

    // --- HERO BANNER ---
    QFrame* headerCard = new QFrame(this);
    headerCard->setStyleSheet("QFrame { background-color: #252528; border-radius: 8px; border: 1px solid #333336; padding: 12px; }");
    auto* headerLayout = new QVBoxLayout(headerCard);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    size_t slash = m_filePath.toStdString().find_last_of("/\\");
    std::string filename = (slash != std::string::npos) ? m_filePath.toStdString().substr(slash + 1) : m_filePath.toStdString();
    const std::string& displayName = m_node ? m_node->getModelDisplayName() : "";
    QString titleStr = QString::fromStdString(m_meta.toneTitle.empty() ? (displayName.empty() ? filename : displayName) : m_meta.toneTitle);

    QLabel* titleLabel = new QLabel(titleStr, headerCard);
    titleLabel->setWordWrap(true);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #00B0FF; background: transparent; border: none;");
    headerLayout->addWidget(titleLabel);

    QString authorStr = QString::fromStdString(m_meta.author.empty() ? m_meta.modeledBy : m_meta.author);
    if (authorStr.isEmpty()) authorStr = "Unknown Creator";
    QLabel* authorLabel = new QLabel(QString("Created by <b>%1</b>").arg(authorStr.toHtmlEscaped()), headerCard);
    authorLabel->setStyleSheet("font-size: 12px; color: #B0BEC5; background: transparent; border: none;");
    headerLayout->addWidget(authorLabel);

    mainLayout->addWidget(headerCard);

    // --- GEAR SPECIFICATIONS ---
    QFrame* gearCard = new QFrame(this);
    gearCard->setStyleSheet("QFrame { background-color: #252528; border-radius: 8px; border: 1px solid #333336; padding: 12px; }");
    auto* gearLayout = new QVBoxLayout(gearCard);
    gearLayout->setContentsMargins(0, 0, 0, 0);
    gearLayout->setSpacing(6);

    QLabel* gearHeader = new QLabel("🎸 GEAR SPECIFICATIONS", gearCard);
    gearHeader->setStyleSheet("font-weight: bold; font-size: 12px; color: #80DEEA; background: transparent; border: none;");
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

    QString gearTypeStr = QString::fromStdString(m_meta.gearType);
    if (!gearTypeStr.isEmpty()) {
        QLabel* typeLabel = new QLabel(QString("<b>Gear Type:</b> %1").arg(gearTypeStr.toHtmlEscaped()), gearCard);
        typeLabel->setStyleSheet("font-size: 11px; color: #CFD8DC; background: transparent; border: none;");
        gearLayout->addWidget(typeLabel);
    }

    if (!m_meta.tags.empty()) {
        QLabel* tagsLabel = new QLabel(QString("<b>Tags:</b> %1").arg(QString::fromStdString(m_meta.tags).toHtmlEscaped()), gearCard);
        tagsLabel->setWordWrap(true);
        tagsLabel->setStyleSheet("font-size: 11px; color: #B0BEC5; background: transparent; border: none;");
        gearLayout->addWidget(tagsLabel);
    }

    mainLayout->addWidget(gearCard);

    // --- TECHNICAL SPECIFICATIONS ---
    QFrame* techCard = new QFrame(this);
    techCard->setStyleSheet("QFrame { background-color: #252528; border-radius: 8px; border: 1px solid #333336; padding: 12px; }");
    auto* techLayout = new QVBoxLayout(techCard);
    techLayout->setContentsMargins(0, 0, 0, 0);
    techLayout->setSpacing(6);

    QLabel* techHeader = new QLabel("⚙ TECHNICAL SPECIFICATIONS", techCard);
    techHeader->setStyleSheet("font-weight: bold; font-size: 12px; color: #80DEEA; background: transparent; border: none;");
    techLayout->addWidget(techHeader);

    QString archStr = QString::fromStdString(m_meta.architecture.empty() ? "NAM Core Engine" : m_meta.architecture);
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
    QFrame* pathCard = new QFrame(this);
    pathCard->setStyleSheet("QFrame { background-color: #252528; border-radius: 8px; border: 1px solid #333336; padding: 12px; }");
    auto* pathLayout = new QVBoxLayout(pathCard);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(4);

    QLabel* pathHeader = new QLabel("📁 FILE LOCATION", pathCard);
    pathHeader->setStyleSheet("font-weight: bold; font-size: 12px; color: #80DEEA; background: transparent; border: none;");
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

    mainLayout->addStretch();

    // --- ACTION BUTTONS TOOLBAR ---
    QFrame* actionCard = new QFrame(this);
    actionCard->setStyleSheet("QFrame { background-color: transparent; border: none; }");
    auto* actionLayout = new QHBoxLayout(actionCard);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(6);

    QPushButton* browserBtn = new QPushButton("⚡ Open in TONE3000 Browser", actionCard);
    browserBtn->setStyleSheet("QPushButton { background-color: #2E7D32; color: white; font-weight: bold; border-radius: 4px; padding: 8px 12px; font-size: 11px; border: none; } QPushButton:hover { background-color: #388E3C; }");
    actionLayout->addWidget(browserBtn);
    connect(browserBtn, &QPushButton::clicked, this, &ModelDetailsDialog::onOpenTone3000Browser);

    if (!m_sourceUrl.isEmpty()) {
        QPushButton* webBtn = new QPushButton("🔗 View on Web", actionCard);
        webBtn->setStyleSheet("QPushButton { background-color: #00897B; color: white; font-weight: bold; border-radius: 4px; padding: 8px 12px; font-size: 11px; border: none; } QPushButton:hover { background-color: #009688; }");
        actionLayout->addWidget(webBtn);
        connect(webBtn, &QPushButton::clicked, this, &ModelDetailsDialog::onOpenOnWeb);
    }

    QPushButton* exportBtn = new QPushButton("💾 Export NAM...", actionCard);
    exportBtn->setStyleSheet("QPushButton { background-color: #333338; color: #E0E0E0; font-weight: bold; border-radius: 4px; padding: 8px 12px; font-size: 11px; border: none; } QPushButton:hover { background-color: #44444A; }");
    actionLayout->addWidget(exportBtn);
    connect(exportBtn, &QPushButton::clicked, this, &ModelDetailsDialog::onExportNam);

    QPushButton* closeBtn = new QPushButton("Close", actionCard);
    closeBtn->setStyleSheet("QPushButton { background-color: #424242; color: white; font-weight: bold; border-radius: 4px; padding: 8px 14px; font-size: 11px; border: none; } QPushButton:hover { background-color: #616161; }");
    actionLayout->addWidget(closeBtn);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    mainLayout->addWidget(actionCard);
}

void ModelDetailsDialog::onOpenTone3000Browser() {
    Tone3000Dialog dialog(m_node.get(), m_engine, this);
    if (!m_meta.toneTitle.empty()) {
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
