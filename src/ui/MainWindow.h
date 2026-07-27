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

class ExternalPluginUIWindow;
class QSplitter;

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
    void onPrevPreset();
    void onNextPreset();
    void onSlotMinusClicked();
    void onSlotPlusClicked();
    void onBufferSizeChanged(int index);
    void onNodeSelected(std::shared_ptr<AudioNode> node);
    void updateCPUStatus();
    void showPluginControls(std::shared_ptr<AudioNode> node);
    void onPluginDoubleClicked(std::shared_ptr<AudioNode> node);
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
    void closeAllPluginUIs();
    void closePluginUIForNode(AudioNode* node);
    bool raisePluginUIForNode(AudioNode* node);
    void registerExternalUI(ExternalPluginUIWindow* uiWin);
    void unregisterExternalUI(ExternalPluginUIWindow* uiWin);
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
    bool savePluginPreset(const std::shared_ptr<AudioNode>& node, const QString& name);
    bool loadPluginPreset(const std::shared_ptr<AudioNode>& node, const QString& name);
    bool isValidPluginPresetName(const QString& name) const;
    void loadFavoritePlugins();
    void saveFavoritePlugins() const;
    
    AudioEngine m_engine;
    LilvWorld* m_lilvWorld = nullptr;
    std::vector<LilvWorld*> m_retiredLilvWorlds;
    
    // UI Elements
    NodeCanvas* m_canvas = nullptr;
    QSplitter* m_workspaceSplitter = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QPushButton* m_savePresetButton = nullptr;
    QToolButton* m_prevPresetBtn = nullptr;
    QToolButton* m_nextPresetBtn = nullptr;
    QToolButton* m_slotMinusBtn = nullptr;
    QToolButton* m_slotPlusBtn = nullptr;
    QLabel* m_slotCountLabel = nullptr;
    QTimer* m_saveFeedbackTimer = nullptr;
    void updatePresetNavigationButtons();
    void updateSlotControls();
    void triggerSaveFeedback();
    int m_globalDefaultSlots = 6;
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
    QToolButton* m_dspCpuButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolButton* m_xrunButton = nullptr;
    
    QDialog* m_settingsDialog = nullptr;
    QLineEdit* m_apiKeyEdit = nullptr;
    std::function<void()> m_updateKeyStatusFunc;

    // Custom plugin search paths
    QStringList m_customLV2Paths;
    QStringList m_customVST3Paths;
    QStringList m_customCLAPPaths;

    float m_inputLevelDecay = 0.0f;
    float m_outputLevelDecay = 0.0f;
    int m_inputClipHoldTicks = 0;
    int m_outputClipHoldTicks = 0;
    float m_smoothedCpuLoad = 0.0f;
    float m_peakCpuLoad = 0.0f;
    
    QWidget* m_paramContainer = nullptr;
    QVBoxLayout* m_paramLayout = nullptr;
    QLabel* m_noParamLabel = nullptr;
    struct ParameterControlBinding {
        uint32_t index;
        std::function<void(float)> setValue;
    };
    std::shared_ptr<AudioNode> m_parameterControlNode;
    std::vector<ParameterControlBinding> m_parameterControlBindings;
    
    QTimer* m_statusTimer = nullptr;
    QTimer* m_audioPortTimer = nullptr;
    bool m_audioConfigured = false;
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
    
public:
    struct PluginInfo {
        std::string name;
        std::string uri;
        std::string category;
        std::string brand;
        QString thumbnailPath;
        bool isLV2 = false;
        int audioInputs = 2;
        int audioOutputs = 2;
        int controlPorts = 0;
        std::string version;
        std::string description;
        std::vector<std::string> features;
        std::string path;
        bool hasNativeGUI = false;
    };
    std::vector<PluginInfo> m_availablePlugins;
    QSet<QString> m_favoritePluginUris;
    QList<ExternalPluginUIWindow*> m_externalUiWindows;
};
