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
#include <cstdint>
#include <functional>
#include <memory>
#include "../audio/AudioEngine.h"
#include "NodeCanvas.h"
#include "../preset/PresetLibrary.h"
#include "../preset/SceneModel.h"
#include "../control/RigController.h"
#include "../control/MidiMap.h"
#include <lilv/lilv.h>

class ExternalPluginUIWindow;
class QSplitter;
class QScrollArea;
class Tone3000ImageLoader;
class FootswitchTile;
class MidiRouter;
class QMenu;

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
    void onPresetButtonClicked();
    void onPrevPreset();
    void onNextPreset();
    void onPrevBank();
    void onNextBank();
    void onSlotMinusClicked();
    void onSlotPlusClicked();
    void onBufferSizeChanged(int index);
    void onNodeSelected(std::shared_ptr<AudioNode> node);
    void onNodeBypassToggled(std::shared_ptr<AudioNode> node);
    void updateCPUStatus();
    void showPluginControls(std::shared_ptr<AudioNode> node);
    void onPluginDoubleClicked(std::shared_ptr<AudioNode> node);
    void onPlusButtonClicked(int row, int col, QPoint screenPos, int insert);
    void onInputHardwareChanged(int index);
    void onOutputHardwareChanged(int index);
    void onInputModeChanged(int index);
    void onOutputModeChanged(int index);
    void onInputGainChanged(int value);
    void onOutputGainChanged(int value);
    void onNodeContextMenuRequested(int row, int col, QPoint screenPos);
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
    QString presetsDirPath() const;
    // Loads the preset in a library slot. Asks about unsaved changes first,
    // unless `remote` (MIDI): then edits are discarded with a status message.
    bool loadSlot(int slot, bool remote = false);
    // Asks for a name (and target slot when targetSlot < 0) and saves the board there.
    bool savePresetToSlot(int targetSlot, const QString& suggestedName = QString());
    int promptTargetSlot(int defaultSlot, const QString& presetName);
    // Prompts for a name when requestedName is null.
    void renamePresetInSlot(int slot, const QString& requestedName = QString());
    void startPresetRename();
    void startSlotRename(int slot, QWidget* tile);
    void showSlotTileMenu(int slot, QWidget* tile, const QPoint& globalPos);
    void updateCanvasInfo();
    void duplicatePresetInSlot(int slot);
    void deletePresetInSlot(int slot);
    // Performance bar: bank selector + A-D slot buttons.
    void rebuildSlotButtons();
    void setViewBank(int bank);
    void renameViewBank();
    void onSlotButtonClicked(int indexInBank);
    void refreshSceneMarkers();
    void startSceneRename(int index, QWidget* tile);

    // Scenes
    SceneModel::BoardState captureBoardState() const;
    std::shared_ptr<AudioNode> findNodeById(const std::string& id) const;
    void resetScenesFromBoard();
    void selectScene(int index);
    void applySceneChanges(const SceneModel::Changes& changes);
    void rebuildSceneBar();
    void showSceneMenu(int index, const QPoint& globalPos);
    void toggleParamSceneControl(const std::shared_ptr<AudioNode>& node, uint32_t index);
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
    std::vector<struct PluginEntry> buildPluginEntries() const;
    // Opens the plugin browser; returns the chosen URI or an empty string.
    QString choosePlugin();
    std::shared_ptr<AudioNode> createPluginNode(const std::string& uri);
    // Creates the plugin and places it (insert = open a column there).
    bool addPluginAt(const QString& uri, int row, int col, int insert);
    // Adds after the last block of the main lane.
    bool appendPluginToChain(const QString& uri);
    void togglePluginLibrary();
    QPointer<class PluginBrowserDialog> m_pluginLibrary;
    QStringList m_recentPluginUris;
    void saveFavoritePlugins() const;
    
    AudioEngine m_engine;
    LilvWorld* m_lilvWorld = nullptr;
    std::vector<LilvWorld*> m_retiredLilvWorlds;
    
    // UI Elements
    NodeCanvas* m_canvas = nullptr;
    QSplitter* m_workspaceSplitter = nullptr;
    int m_viewBank = 0;
    QLabel* m_bankLabel = nullptr;
    QToolButton* m_bankPrevBtn = nullptr;
    QToolButton* m_bankNextBtn = nullptr;
    QToolButton* m_slotGridBtn = nullptr;
    QHBoxLayout* m_slotBarLayout = nullptr;
    std::vector<FootswitchTile*> m_slotButtons;
    QLabel* m_presetNameLabel = nullptr; // canvas info label (click: show bank, double-click: rename)
    QMenu* m_presetMenu = nullptr;
    QWidget* m_columnsGroup = nullptr;
    PresetLibrary m_presetLibrary;
    SceneModel m_scenes;
    RigController* m_rig = nullptr;
    void setupRigController();

    // MIDI
    GlobalMidiConfig m_midiConfig = GlobalMidiConfig::defaults();
    // Pickup state per controlled parameter: whether the controller has caught
    // up with the value, and where it was last seen.
    std::map<std::pair<std::string, uint32_t>, std::pair<bool, float>> m_midiPickup;
    // Called whenever values change under the controller's feet.
    void resetMidiPickup() { m_midiPickup.clear(); }
    // Applies an incoming controller value, honouring the assignment's takeover.
    void applyMidiParam(const std::string& nodeId, uint32_t index, float normalized);
    PresetMidiMap m_presetMidi;
    MidiRouter* m_midi = nullptr;
    QToolButton* m_midiIndicator = nullptr;
    QTimer* m_midiIndicatorTimer = nullptr;
    std::function<void(const QString&)> m_midiLastMessageSink; // Settings readout
    void setupMidi();
    void applyMidiAction(const MidiAction& action);
    void setMidiInput(const QString& port);
    void startMidiLearn(const QString& what, std::function<void(const MidiMessage&)> onMessage);
    void learnBlockMidi(const std::shared_ptr<AudioNode>& node);
    void learnParamMidi(const std::shared_ptr<AudioNode>& node, uint32_t paramIndex);
    void showMidiAssignmentsDialog();
    bool rejectGlobalMidiConflict(int cc);
    QWidget* buildMidiSettingsTab();
    void openSettings(int tabIndex = -1);
    class QTabWidget* m_settingsTabs = nullptr;
    int m_midiTabIndex = -1;
    QWidget* m_midiLearnBubble = nullptr;
    QWidget* m_sceneBar = nullptr;
    QHBoxLayout* m_sceneBarLayout = nullptr;
    QPushButton* m_savePresetButton = nullptr;
    QToolButton* m_slotMinusBtn = nullptr;
    QToolButton* m_slotPlusBtn = nullptr;
    QLabel* m_slotCountLabel = nullptr;
    QTimer* m_saveFeedbackTimer = nullptr;
    QTimer* m_sceneMarkerTimer = nullptr; // debounces marker refresh after edits
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
    int m_lastCpuDisplay = -1;
    int m_lastCpuSeverity = -1;
    int m_lastInputMeterValue = -1;
    int m_lastOutputMeterValue = -1;
    uint32_t m_lastXrunCount = UINT32_MAX;
    
    QWidget* m_paramContainer = nullptr;
    QScrollArea* m_paramScroll = nullptr;
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
    int m_currentSlot = -1;
    QString m_currentPresetName; // empty = unsaved "Untitled" board
    void setUnsavedChanges(bool unsaved);
    bool promptUnsavedChanges();

    QNetworkAccessManager* m_networkManager = nullptr;
    Tone3000ImageLoader* m_toneImageLoader = nullptr;
    QNetworkReply* m_currentDownloadReply = nullptr;
    // redirectUrl: follow-up request after a redirect (the stored variant URL is not changed).
    void downloadVariant(std::shared_ptr<AudioNode> node, int variantIdx, QPointer<QComboBox> combo, QPointer<QLabel> fileLabel, bool isRedirect = false, const QString& redirectUrl = QString());
    
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
        std::string author;
        std::string license;
    };
    std::vector<PluginInfo> m_availablePlugins;
    // Plugin GUI previews (experimental, off by default).
    class PluginPreviewService* m_previewService = nullptr;
    bool m_pluginPreviewsEnabled = false;
    bool m_pluginPreviewsOnImport = false;   // only then are new plugins done automatically
    class QLabel* m_previewStatusLabel = nullptr;
    class QPushButton* m_previewGenerateButton = nullptr;
    class QPushButton* m_previewCancelButton = nullptr;
    class QCheckBox* m_previewToggle = nullptr;
    class QCheckBox* m_previewImportToggle = nullptr;
    class QComboBox* m_previewScopeCombo = nullptr;
    QStringList previewCandidateUris() const;
    void updatePreviewStatus(const QString& text = QString());
    void startPluginPreviews(int mode, bool quiet);
    // Keeps the boxes, the button text and the counts in step with the settings.
    void refreshPreviewControls();
    int previewScopeMode() const;
    QSet<QString> m_favoritePluginUris;
    QList<ExternalPluginUIWindow*> m_externalUiWindows;
};
