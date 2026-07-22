#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QApplication>
#include <QClipboard>
#include <memory>

#include "../audio/AudioNode.h"
#include "../audio/AudioEngine.h"

class ModelDetailsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ModelDetailsDialog(std::shared_ptr<AudioNode> node, AudioEngine* engine, QWidget* parent = nullptr);
    ~ModelDetailsDialog() override = default;

private slots:
    void onOpenTone3000Browser();
    void onOpenOnWeb();
    void onExportNam();
    void onCopyPath();

private:
    void setupUI();

    std::shared_ptr<AudioNode> m_node;
    AudioEngine* m_engine;
    QString m_filePath;
    QString m_sourceUrl;
    AudioNode::ModelMetadata m_meta;
};
