#pragma once

#include <memory>
#include <vector>

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QMainWindow>

#include "FileTypes.h"
#include "m2audio/Aiff.h"
#include "m2model/AitdImage.h"
#include "m2model/AitdPak.h"
#include "m2model/AitdRoom.h"
#include "m2stream/Stream.h"
#include "m2texture/Texture.h"

class QAudioSink;
class QBuffer;
class QCheckBox;
class QComboBox;
class QLabel;
class QMediaPlayer;
class QAudioOutput;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTemporaryFile;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QVideoWidget;
class QPoint;

namespace m2suite {

class ImageViewport;
class ModelViewport;
class WaveformView;

// Converter + Visualizer module. Left dock: filterable multi-select file
// browser. Center: zoomable image viewport / waveform / video widget with
// playback controls. Right: info pane. A logo banner sits in the
// bottom-right corner of the viewing area.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    // Out-of-line: members hold unique_ptrs to types only forward-declared
    // here (QTemporaryFile), so the destructor must live in the .cpp.
    ~MainWindow() override;

private slots:
    void openFolder();
    void openSingleFile();
    void currentFileChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void expandArchiveItem(QTreeWidgetItem* item);
    void refreshTexturePreview();
    void applyTypeFilter();
    void exportSelected();
    void playClicked();
    void stopClicked();
    void cinepakTick();
    void animTick();
    void setDarkMode(bool dark);
    void fileTreeContextMenu(const QPoint& pos);
    void showAboutDialog();
    void extractDiscImage();
    void convertImagesToUtf();
    void convertAudioTo3do();

private:
    void scanFolderIntoTree(const QString& rootPath);
    void openPath(const QString& path, FileType type);
    void showTextureFile(const QString& path);
    void showCelFile(const QString& path);
    void showAnimFile(const QString& path);
    void showImagFile(const QString& path);
    void showStandardImageFile(const QString& path);
    void showAudioFile(const QString& path, FileType type);
    void showWavFile(const QString& path);
    void showStreamFile(const QString& path);
    void showAitdPakFile(const QString& path);
    void showAitdImageFile(const QString& path);
    void showAitdRoomsFile(const QString& path);
    void showSoundCatalogue(const QString& path);
    void aitdTick();
    // Looks beside the PAK (and one level up) for an AITD_PakEdit-style
    // "*_PAK_DB.json" and returns entry-index -> name for this archive.
    // Empty when no database is present, which is the normal case.
    QHash<int, QString> loadAitdNameDatabase(const QString& pakPath);
    void showElfFile(const QString& path);
    void showM1vcFile(const QString& path);
    void showFormTextFile(const QString& path, FileType type);
    void showDspFile(const QString& path);
    void playMpegFromFile(const QString& mpegPath, bool hasVideo);
    void showInfoOnly(const QString& path, FileType type);
    void playClickedImpl();
    void cinepakTickImpl();
    void displayImage(const QImage& image);
    void stopAllPlayback();
    void startPcmPlayback();
    // Remuxes extracted MPEG elementary streams into a .mpg via the
    // system ffmpeg (PATH or C:\ffmpeg) for reliable QMediaPlayer playback
    // with synced audio. Returns an empty string if ffmpeg is unavailable.
    QString remuxMpeg(const QString& videoEs, const QString& audioEs);
    int exportOneFile(const QString& path, FileType type, const QString& outDir,
                       QStringList& errors);

    QTreeWidget* fileTree_ = nullptr;
    QComboBox* typeFilter_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QCheckBox* autoplayCheck_ = nullptr;
    QPlainTextEdit* textView_ = nullptr; // disassembly / text content page
    ImageViewport* imageView_ = nullptr;
    WaveformView* waveformView_ = nullptr;
    QVideoWidget* videoWidget_ = nullptr;
    QStackedWidget* viewStack_ = nullptr;
    QLabel* placeholder_ = nullptr;
    QLabel* banner_ = nullptr;
    QPlainTextEdit* infoView_ = nullptr;
    QComboBox* textureSelect_ = nullptr;
    QComboBox* lodSelect_ = nullptr;
    QSlider* zoomSlider_ = nullptr;
    QSpinBox* zoomSpin_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;

    std::vector<m2texture::Texture> textures_;
    std::vector<QImage> animFrames_;
    enum class SelectorMode { None, Textures, AnimFrames, AitdBodies, AitdRooms, SoundCatalogue };
    SelectorMode selectorMode_ = SelectorMode::None;

    // --- Playback state ---
    enum class PlaybackKind { None, Pcm, Cinepak, Mpeg, CelAnim };
    PlaybackKind playbackKind_ = PlaybackKind::None;
    // PCM audio (AIFF / WAV / SNDS-SDX2)
    std::vector<int16_t> pcm_;
    int pcmRate_ = 0;
    int pcmChannels_ = 0;
    QAudioSink* audioSink_ = nullptr;
    QBuffer* audioBuffer_ = nullptr;
    // Cinepak film
    std::unique_ptr<m2stream::Stream> stream_;
    std::unique_ptr<m2stream::CinepakDecoder> cinepak_;
    QTimer* cinepakTimer_ = nullptr;
    size_t cinepakFrame_ = 0;
    std::vector<uint8_t> cinepakBuffer_;
    // Wall-clock master for A/V sync: each film frame carries a DataStreamer
    // timestamp (in stream ticks). We convert elapsed real time to ticks via
    // streamHz_ (derived from the audio track's true duration when present,
    // else the ~240 Hz default) and show the newest frame whose timestamp is
    // due. Timestamp-based, so variable frame spacing and long clips stay
    // synced without assuming a constant frame rate.
    QElapsedTimer playbackClock_;
    double streamHz_ = 240.0;
    uint32_t firstFrameTime_ = 0;
    // Cel-animation playback (ANIM chunks and bare cel chains): loops
    // continuously so multi-frame files are obviously animations.
    QTimer* animTimer_ = nullptr;
    size_t animFrame_ = 0;
    // AITD 3D model view: a rotating point cloud of the selected body.
    std::unique_ptr<m2model::AitdPak> aitdPak_;
    m2model::AitdBody aitdBody_;
    QString aitdPakPath_;
    QTimer* aitdTimer_ = nullptr;
    ModelViewport* modelView_ = nullptr;
    QHash<int, QString> aitdNames_; // entry index -> name, from a PAK_DB.json
    std::vector<m2model::AitdRoom> aitdRooms_; // the open floor, if any
    // Sound catalogue: the whole file stays resident so each sound can be
    // decoded on demand without re-reading it.
    std::vector<uint8_t> catalogueData_;
    std::vector<m2audio::AiffCatalogueEntry> catalogue_;
    QString cataloguePath_;
    // MPEG via Qt Multimedia on an extracted elementary stream
    QMediaPlayer* mediaPlayer_ = nullptr;
    QAudioOutput* mediaAudio_ = nullptr;
    std::unique_ptr<QTemporaryFile> mpegTemp_;
};

} // namespace m2suite
