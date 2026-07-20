#pragma once
#include <QMainWindow>
#include <QComboBox>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QDial>
#include <QToolButton>
#include <QDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>
#include <QPointer>
#include <functional>
#include <memory>
#include "../audio/AudioEngine.h"
#include "NodeCanvas.h"
#include <lilv/lilv.h>

class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class ExternalPluginUIWindow;
public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    
protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onNewPreset();
    void onSavePreset();
    void onSavePresetAs();
    void onRenamePreset();
    void onDeletePreset();
    void onPresetComboActivated(int index);
    void onLoadPreset();
    void onBufferSizeChanged(int index);
    void onNodeSelected(std::shared_ptr<AudioNode> node);
    void updateCPUStatus();
    void showPluginControls(std::shared_ptr<AudioNode> node);
    void onPlusButtonClicked(int row, int col, QPoint screenPos, bool isSecondOfCol);
    void onInputHardwareChanged(int index);
    void onOutputHardwareChanged(int index);
    void onInputModeChanged(int index);
    void onOutputModeChanged(int index);
    void onInputGainChanged(int value);
    void onOutputGainChanged(int value);
    void onNodeContextMenuRequested(int row, int col, QPoint screenPos);
    void showBranchControls(int row);
    void showRoutingNodeControls(int row, bool isSplit);

private:
    void setupUI();
    void scanPlugins();
    void savePresetToFile(const QString& path);
    void loadPresetFromFile(const QString& path);
    void refreshPresetList();
    void saveConfigSettings();
    void populateInputPorts();
    void populateOutputPorts();
    void refreshAudioPorts();
    void syncParameterControls();
    QString pluginPresetDirectory(const AudioNode& node) const;
    void refreshPluginPresetList(const std::shared_ptr<AudioNode>& node, QComboBox* combo, const QString& selected = {});
    bool savePluginPreset(const std::shared_ptr<AudioNode>& node, const QString& name);
    bool loadPluginPreset(const std::shared_ptr<AudioNode>& node, const QString& name);
    bool isValidPluginPresetName(const QString& name) const;
    void loadFavoritePlugins();
    void saveFavoritePlugins() const;
    
    AudioEngine m_engine;
    LilvWorld* m_lilvWorld = nullptr;
    
    // UI Elements
    NodeCanvas* m_canvas = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QComboBox* m_bufferSizeCombo = nullptr;
    QComboBox* m_hwInputModeCombo = nullptr;
    QComboBox* m_hwInputCombo = nullptr;
    QComboBox* m_hwOutputModeCombo = nullptr;
    QComboBox* m_hwOutputCombo = nullptr;
    QToolButton* m_inputGainLabel = nullptr;
    QToolButton* m_outputGainLabel = nullptr;
    QProgressBar* m_inputMeter = nullptr;
    QProgressBar* m_outputMeter = nullptr;
    QProgressBar* m_inputPopupMeter = nullptr;
    QProgressBar* m_outputPopupMeter = nullptr;
    QProgressBar* m_cpuBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    
    QDialog* m_settingsDialog = nullptr;
    float m_inputLevelDecay = 0.0f;
    float m_outputLevelDecay = 0.0f;
    
    QWidget* m_paramContainer = nullptr;
    QVBoxLayout* m_paramLayout = nullptr;
    QLabel* m_noParamLabel = nullptr;
    struct ParameterControlBinding {
        uint32_t index;
        std::function<void(float)> setValue;
    };
    std::shared_ptr<AudioNode> m_parameterControlNode;
    std::vector<ParameterControlBinding> m_parameterControlBindings;
    std::string m_activePluginPresetNodeId;
    QString m_activePluginPresetName;
    
    QTimer* m_statusTimer = nullptr;
    QTimer* m_audioPortTimer = nullptr;
    std::vector<std::string> m_knownPhysicalInputs;
    std::vector<std::string> m_knownPhysicalOutputs;

    bool m_unsavedChanges = false;
    bool m_isLoadingPreset = false;
    int m_currentPresetIndex = -1;
    void setUnsavedChanges(bool unsaved);
    bool promptUnsavedChanges();

    QNetworkAccessManager* m_networkManager = nullptr;
    QNetworkReply* m_currentDownloadReply = nullptr;
    void downloadVariant(std::shared_ptr<AudioNode> node, int variantIdx, QPointer<QComboBox> combo, QPointer<QLabel> fileLabel, bool isRedirect = false);
    
    struct PluginInfo {
        std::string name;
        std::string uri;
        std::string category;
        std::string brand;
        QString thumbnailPath;
        bool isLV2;
    };
    std::vector<PluginInfo> m_availablePlugins;
    QSet<QString> m_favoritePluginUris;
};
