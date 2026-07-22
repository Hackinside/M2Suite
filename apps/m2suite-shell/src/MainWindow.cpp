#include "MainWindow.h"

#include <functional>

#include <QAction>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QAudioSink>
#include <QBuffer>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QStatusBar>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QLocale>
#include <QMediaPlayer>
#include <QMenu>
#include <QMenuBar>
#include <QProgressDialog>
#include <QScrollArea>
#include <QMessageBox>
#include <QDateTime>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QtLogging>
#include <QSettings>
#include <QStandardPaths>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QStyle>
#include <QStyleHints>
#include <QTemporaryFile>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVideoWidget>

#include "ImageViewport.h"
#include "ModelViewport.h"
#include "WaveformView.h"
#include "m2audio/Aiff.h"
#include "m2audio/Encoder.h"
#include "m2audio/Sdx2.h"
#include "m2audio/Wav.h"
#include "m2cel/Anim.h"
#include "m2cel/Cel.h"
#include "m2cel/Imag.h"
#include "Version.h"
#include "m2disasm/Elf.h"
#include "m2disc/OperaDisc.h"
#include "m2disasm/Pseudocode.h"
#include "m2texture/TextureEncoder.h"

namespace m2suite {

namespace {
constexpr int kPathRole = Qt::UserRole;
constexpr int kTypeRole = Qt::UserRole + 1;
// Set on a child item that stands for one entry inside an archive.
constexpr int kEntryRole = Qt::UserRole + 2;
// Marks the stand-in child that gives an archive its expander arrow; it is
// replaced by the real entries the first time the archive is expanded.
constexpr int kPlaceholderRole = Qt::UserRole + 3;
constexpr uint32_t kFourccSDX2 = ('S' << 24) | ('D' << 16) | ('X' << 8) | '2';
constexpr uint32_t kFourccCBD2 = ('C' << 24) | ('B' << 16) | ('D' << 8) | '2';
constexpr uint32_t kFourccNONE = ('N' << 24) | ('O' << 16) | ('N' << 8) | 'E';

QString findFfmpegPath() {
    QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        ffmpeg = QStringLiteral("C:/ffmpeg/bin/ffmpeg.exe");
        if (!QFileInfo::exists(ffmpeg)) {
            return QString();
        }
    }
    return ffmpeg;
}

// Minimal RIFF/WAVE (PCM16 LE) container around interleaved samples.
QByteArray makeWav(const std::vector<int16_t>& pcm, uint16_t channels, uint32_t rate) {
    QByteArray wav;
    if (pcm.empty() || channels == 0 || rate == 0) {
        return wav;
    }
    uint32_t dataBytes = uint32_t(pcm.size() * 2);
    auto append = [&wav](const void* p, int n) {
        wav.append(reinterpret_cast<const char*>(p), n);
    };
    uint32_t v32;
    uint16_t v16;
    append("RIFF", 4);
    v32 = 36 + dataBytes; append(&v32, 4);
    append("WAVEfmt ", 8);
    v32 = 16; append(&v32, 4);
    v16 = 1; append(&v16, 2);
    v16 = channels; append(&v16, 2);
    v32 = rate; append(&v32, 4);
    v32 = rate * channels * 2; append(&v32, 4);
    v16 = uint16_t(channels * 2); append(&v16, 2);
    v16 = 16; append(&v16, 2);
    append("data", 4);
    v32 = dataBytes; append(&v32, 4);
    append(pcm.data(), int(dataBytes));
    return wav;
}

// Decodes a stream's SNDS track to interleaved int16 PCM. Handles SDX2,
// CBD2, and uncompressed ('NONE') 8/16-bit samples (Logo.Cine ships raw
// 8-bit stereo). Returns empty when the compression is unsupported.
std::vector<int16_t> decodeSndsAudio(const m2stream::Stream& s) {
    const auto& a = s.audio();
    const auto& raw = s.audioData();
    switch (a.compression) {
        case kFourccSDX2:
            return m2audio::decodeSdx2(raw.data(), raw.size(), a.channels);
        case kFourccCBD2:
            return m2audio::decodeCbd2(raw.data(), raw.size(), a.channels);
        case kFourccNONE: {
            std::vector<int16_t> pcm;
            if (a.sampleSizeBits == 8) { // signed 8-bit, AIFF convention
                pcm.reserve(raw.size());
                for (uint8_t b : raw) {
                    pcm.push_back(int16_t(int16_t(int8_t(b)) << 8));
                }
            } else {
                pcm.reserve(raw.size() / 2);
                for (size_t i = 0; i + 1 < raw.size(); i += 2) {
                    pcm.push_back(int16_t((raw[i] << 8) | raw[i + 1])); // big-endian
                }
            }
            return pcm;
        }
        default:
            return {};
    }
}

// Tree item that sorts like a file manager: folders first, then files,
// each group case-insensitively by name.
class TreeItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override {
        bool thisIsDir = !data(0, kTypeRole).isValid();
        bool otherIsDir = !other.data(0, kTypeRole).isValid();
        if (thisIsDir != otherIsDir) {
            return thisIsDir;
        }
        return text(0).compare(other.text(0), Qt::CaseInsensitive) < 0;
    }
};

QImage rgbaToImage(const std::vector<m2texture::Rgba8>& rgba, uint32_t w, uint32_t h) {
    QImage image(int(w), int(h), QImage::Format_RGBA8888);
    for (uint32_t y = 0; y < h; ++y) {
        uchar* line = image.scanLine(int(y));
        for (uint32_t x = 0; x < w; ++x) {
            const auto& px = rgba[size_t(y) * w + x];
            line[x * 4 + 0] = px.r;
            line[x * 4 + 1] = px.g;
            line[x * 4 + 2] = px.b;
            line[x * 4 + 3] = px.a;
        }
    }
    return image;
}

QString fourccToString(uint32_t v) {
    QString s;
    for (int shift = 24; shift >= 0; shift -= 8) {
        char c = char((v >> shift) & 0xFF);
        s += (c >= 32 && c <= 126) ? QChar(c) : QChar('?');
    }
    return s;
}

// The filter combo is generated from fileCategoryOrder(), so a new
// FileType only has to be slotted into fileTypeCategory() to appear under
// the right heading. The previous hand-maintained switch over combo
// indices silently dropped every type added after it was written.
bool typeMatchesFilter(FileType type, int filterIndex) {
    const auto& order = fileCategoryOrder();
    if (filterIndex < 0 || filterIndex >= order.size()) {
        return true;
    }
    FileCategory want = order.at(filterIndex);
    return want == FileCategory::All || fileTypeCategory(type) == want;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("M2Suite — Converter + Visualizer"));
    resize(1200, 720);

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* openFolderAction = fileMenu->addAction(tr("Open &Folder (unpacked game)..."));
    openFolderAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(openFolderAction, &QAction::triggered, this, &MainWindow::openFolder);
    auto* openFileAction = fileMenu->addAction(tr("&Open File..."));
    openFileAction->setShortcut(QKeySequence::Open);
    connect(openFileAction, &QAction::triggered, this, &MainWindow::openSingleFile);
    auto* extractDiscAction = fileMenu->addAction(tr("Extract &Disc Image (ISO/BIN/IMG)..."));
    connect(extractDiscAction, &QAction::triggered, this, &MainWindow::extractDiscImage);
    auto* toUtfAction = fileMenu->addAction(tr("Convert Images to &UTF Texture..."));
    connect(toUtfAction, &QAction::triggered, this, &MainWindow::convertImagesToUtf);
    auto* toAudioAction = fileMenu->addAction(tr("Convert &Audio to 3DO Format..."));
    connect(toAudioAction, &QAction::triggered, this, &MainWindow::convertAudioTo3do);
    // No menu entry for loading a name database: the databases are bundled,
    // so archive entries are named out of the box. An external or updated
    // one is still picked up automatically from beside the game or beside
    // the executable — see loadAitdNameDatabase().
    fileMenu->addSeparator();
    auto* exportAction = fileMenu->addAction(tr("&Export Selected..."));
    exportAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportSelected);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* darkAction = viewMenu->addAction(tr("&Dark Mode"));
    darkAction->setCheckable(true);
    QSettings settings;
    bool dark = settings.value(QStringLiteral("darkMode"), false).toBool();
    darkAction->setChecked(dark);
    setDarkMode(dark);
    connect(darkAction, &QAction::toggled, this, &MainWindow::setDarkMode);

    auto* bgColorAction = viewMenu->addAction(tr("Viewport &Background Colour..."));
    connect(bgColorAction, &QAction::triggered, this, [this]() {
        QSettings s;
        QColor current =
            s.value(QStringLiteral("viewportBg"), QColor(0x67, 0x67, 0x67)).value<QColor>();
        QColor chosen =
            QColorDialog::getColor(current, this, tr("Choose Viewport Background Colour"));
        if (chosen.isValid()) {
            imageView_->setBackgroundColor(chosen);
            s.setValue(QStringLiteral("viewportBg"), chosen);
            qInfo() << "viewport background set to" << chosen.name();
        }
    });

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About M2Suite"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);

    // --- Left dock: type filter + file tree + export button ---
    typeFilter_ = new QComboBox;
    for (FileCategory c : fileCategoryOrder()) {
        typeFilter_->addItem(fileCategoryLabel(c));
    }
    connect(typeFilter_, &QComboBox::currentIndexChanged, this, &MainWindow::applyTypeFilter);

    fileTree_ = new QTreeWidget;
    fileTree_->setHeaderLabels({tr("File"), tr("Type"), tr("Size")});
    fileTree_->setColumnWidth(0, 220);
    fileTree_->setColumnWidth(1, 110);
    fileTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fileTree_, &QTreeWidget::currentItemChanged, this, &MainWindow::currentFileChanged);
    connect(fileTree_, &QTreeWidget::itemExpanded, this, &MainWindow::expandArchiveItem);
    connect(fileTree_, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::fileTreeContextMenu);

    exportButton_ = new QPushButton(tr("Export Selected..."));
    connect(exportButton_, &QPushButton::clicked, this, &MainWindow::exportSelected);

    autoplayCheck_ = new QCheckBox(tr("Autoplay on select"));
    autoplayCheck_->setChecked(true);

    auto* filesPane = new QWidget;
    auto* filesLayout = new QVBoxLayout(filesPane);
    filesLayout->setContentsMargins(2, 2, 2, 2);
    filesLayout->addWidget(typeFilter_);
    filesLayout->addWidget(fileTree_, 1);
    filesLayout->addWidget(autoplayCheck_);
    filesLayout->addWidget(exportButton_);
    filesPane->setMinimumWidth(160);

    // --- Center: selector/zoom/playback toolbar + stacked viewport ---
    textureSelect_ = new QComboBox;
    textureSelect_->setMinimumWidth(150);
    lodSelect_ = new QComboBox;
    lodSelect_->setMinimumWidth(120);
    connect(textureSelect_, &QComboBox::currentIndexChanged, this,
            &MainWindow::refreshTexturePreview);
    connect(lodSelect_, &QComboBox::currentIndexChanged, this,
            &MainWindow::refreshTexturePreview);

    playButton_ = new QPushButton(tr("▶ Play"));
    stopButton_ = new QPushButton(tr("■ Stop"));
    playButton_->setEnabled(false);
    stopButton_->setEnabled(false);
    connect(playButton_, &QPushButton::clicked, this, &MainWindow::playClicked);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopClicked);

    zoomSlider_ = new QSlider(Qt::Horizontal);
    zoomSlider_->setRange(ImageViewport::kMinZoom, ImageViewport::kMaxZoom);
    zoomSlider_->setValue(100);
    zoomSlider_->setSingleStep(10);
    zoomSlider_->setPageStep(50);
    zoomSlider_->setMaximumWidth(220);

    zoomSpin_ = new QSpinBox;
    zoomSpin_->setRange(ImageViewport::kMinZoom, ImageViewport::kMaxZoom);
    zoomSpin_->setValue(100);
    zoomSpin_->setSingleStep(10);
    zoomSpin_->setSuffix(QStringLiteral("%"));
    zoomSpin_->setKeyboardTracking(false);

    auto* toolbarRow = new QWidget;
    auto* toolbarLayout = new QHBoxLayout(toolbarRow);
    toolbarLayout->setContentsMargins(4, 4, 4, 4);
    toolbarLayout->addWidget(new QLabel(tr("Texture:")));
    toolbarLayout->addWidget(textureSelect_);
    toolbarLayout->addWidget(new QLabel(tr("LOD:")));
    toolbarLayout->addWidget(lodSelect_);
    toolbarLayout->addWidget(playButton_);
    toolbarLayout->addWidget(stopButton_);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(new QLabel(tr("Zoom:")));
    toolbarLayout->addWidget(zoomSlider_);
    toolbarLayout->addWidget(zoomSpin_);

    imageView_ = new ImageViewport;
    waveformView_ = new WaveformView;
    videoWidget_ = new QVideoWidget;
    placeholder_ = new QLabel(tr("Open a folder (unpacked game) or a file to begin."));
    placeholder_->setAlignment(Qt::AlignCenter);

    textView_ = new QPlainTextEdit;
    textView_->setReadOnly(true);
    textView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    {
        QFont mono(QStringLiteral("Consolas"));
        mono.setStyleHint(QFont::Monospace);
        textView_->setFont(mono);
    }

    modelView_ = new ModelViewport;

    viewStack_ = new QStackedWidget;
    viewStack_->addWidget(placeholder_);
    viewStack_->addWidget(imageView_);
    viewStack_->addWidget(waveformView_);
    viewStack_->addWidget(videoWidget_);
    viewStack_->addWidget(textView_);
    viewStack_->addWidget(modelView_);

    connect(zoomSlider_, &QSlider::valueChanged, imageView_, &ImageViewport::setZoomPercent);
    connect(zoomSpin_, &QSpinBox::valueChanged, imageView_, &ImageViewport::setZoomPercent);
    connect(imageView_, &ImageViewport::zoomChanged, this, [this](int percent) {
        zoomSlider_->blockSignals(true);
        zoomSlider_->setValue(percent);
        zoomSlider_->blockSignals(false);
        zoomSpin_->blockSignals(true);
        zoomSpin_->setValue(percent);
        zoomSpin_->blockSignals(false);
    });

    cinepakTimer_ = new QTimer(this);
    connect(cinepakTimer_, &QTimer::timeout, this, &MainWindow::cinepakTick);
    animTimer_ = new QTimer(this);
    connect(animTimer_, &QTimer::timeout, this, &MainWindow::animTick);
    aitdTimer_ = new QTimer(this);
    connect(aitdTimer_, &QTimer::timeout, this, &MainWindow::aitdTick);

    auto* centerPane = new QWidget;
    auto* centerLayout = new QVBoxLayout(centerPane);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addWidget(toolbarRow);
    centerLayout->addWidget(viewStack_, 1);

    infoView_ = new QPlainTextEdit;
    infoView_->setReadOnly(true);
    infoView_->setPlaceholderText(tr("File details will appear here."));

    // Logo banner: a white square card under the file-information panel,
    // scaled to at most 256x256, so the app name stays visible.
    banner_ = new QLabel;
    QPixmap logo(QStringLiteral(":/m2suite_icon.png"));
    banner_->setPixmap(logo.scaled(244, 244, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    banner_->setFixedSize(256, 256);
    banner_->setAlignment(Qt::AlignCenter);
    banner_->setStyleSheet(QStringLiteral(
        "background-color: white; border: 1px solid #cccccc; border-radius: 4px;"));

    // Copies the whole info panel (including the full path) for bug
    // reports — the details are otherwise tedious to retype.
    auto* copyInfoButton = new QPushButton(tr("Copy Info"));
    copyInfoButton->setToolTip(tr("Copy all file information, including the full path, "
                                   "to the clipboard"));
    connect(copyInfoButton, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(infoView_->toPlainText());
        statusBar()->showMessage(tr("File information copied to clipboard"), 3000);
    });

    auto* rightPane = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(infoView_, 1);
    rightLayout->addWidget(copyInfoButton);
    rightLayout->addWidget(banner_, 0, Qt::AlignHCenter);

    // One splitter for all three panes: unlike the previous QDockWidget
    // arrangement, every divider is freely draggable without first
    // resizing the main window.
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(filesPane);
    splitter->addWidget(centerPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 1);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({280, 700, 300});
    setCentralWidget(splitter);

    // Restore persisted viewport background colour and last-opened folder.
    QColor bg = settings.value(QStringLiteral("viewportBg"), QColor(0x67, 0x67, 0x67))
                     .value<QColor>();
    if (bg.isValid()) {
        imageView_->setBackgroundColor(bg);
    }
    QString lastFolder = settings.value(QStringLiteral("lastFolder")).toString();
    if (!lastFolder.isEmpty() && QFileInfo::exists(lastFolder)) {
        scanFolderIntoTree(lastFolder);
    }
}

MainWindow::~MainWindow() {
    stopAllPlayback();
}

void MainWindow::setDarkMode(bool dark) {
    QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark
                                                        : Qt::ColorScheme::Light);
    QSettings settings;
    settings.setValue(QStringLiteral("darkMode"), dark);
}

void MainWindow::openFolder() {
    QSettings settings;
    QString start = settings.value(QStringLiteral("lastFolder")).toString();
    QString dir =
        QFileDialog::getExistingDirectory(this, tr("Open Unpacked Game Folder"), start);
    if (dir.isEmpty()) {
        return;
    }
    scanFolderIntoTree(dir);
}

void MainWindow::extractDiscImage() {
    QSettings settings;
    QString start = settings.value(QStringLiteral("lastDiscDir")).toString();
    QString imagePath = QFileDialog::getOpenFileName(
        this, tr("Open 3DO Disc Image or Archive"), start,
        tr("Disc images and archives (*.iso *.bin *.img *.cue *.chd bigfile);;"
           "All files (*)"));
    if (imagePath.isEmpty()) {
        return;
    }
    settings.setValue(QStringLiteral("lastDiscDir"), QFileInfo(imagePath).absolutePath());

    // Extract next to the image into a folder named after it (the "valid
    // name of the disc image"), matching the *.unpacked convention already
    // used across the reference dumps.
    QFileInfo info(imagePath);
    QString outDir = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() +
                     QStringLiteral(".unpacked");

    qInfo() << "extracting disc image" << imagePath << "->" << outDir;
    QProgressDialog progress(tr("Reading disc filesystem..."), tr("Cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.show();
    QCoreApplication::processEvents();

    try {
        m2disc::OperaDisc disc = m2disc::OperaDisc::open(imagePath.toStdWString());
        bool canceled = false;
        int count = disc.extractAll(
            outDir.toStdWString(), [&](int n, const std::string& path) {
                if (n % 16 == 0) {
                    progress.setLabelText(tr("Extracting '%1'...\n%2 files")
                                               .arg(disc.volumeName().empty()
                                                         ? info.completeBaseName()
                                                         : QString::fromStdString(disc.volumeName()))
                                               .arg(n));
                    QCoreApplication::processEvents();
                    if (progress.wasCanceled()) {
                        canceled = true;
                    }
                }
                (void)path;
            });
        progress.reset();
        qInfo() << "disc extraction complete:" << count << "files"
                << (canceled ? "(canceled)" : "");

        QString msg = tr("Extracted %1 files from '%2' to:\n%3")
                           .arg(count)
                           .arg(QString::fromStdString(disc.volumeName()))
                           .arg(QDir::toNativeSeparators(outDir));
        QMessageBox box(QMessageBox::Information, tr("Extract Disc Image"), msg,
                         QMessageBox::Ok, this);
        QPushButton* openBtn = box.addButton(tr("Open Folder"), QMessageBox::ActionRole);
        QPushButton* browseBtn = box.addButton(tr("Browse in M2Suite"), QMessageBox::ActionRole);
        box.exec();
        if (box.clickedButton() == openBtn) {
            QProcess::startDetached(QStringLiteral("explorer.exe"),
                                     {QDir::toNativeSeparators(outDir)});
        } else if (box.clickedButton() == browseBtn) {
            scanFolderIntoTree(outDir);
        }
    } catch (const std::exception& discError) {
        // Not an Opera disc — it may be a game-specific archive instead
        // (Gex ships all its content in GXdata/bigfile).
        try {
            m2disc::BigFile bf = m2disc::BigFile::openFromFile(imagePath.toStdWString());
            progress.setLabelText(tr("Extracting archive..."));
            QCoreApplication::processEvents();
            int count = bf.extractAll(outDir.toStdWString(),
                                       [&](int n, const std::string&) {
                                           if (n % 16 == 0) {
                                               progress.setLabelText(
                                                   tr("Extracting archive...\n%1 files").arg(n));
                                               QCoreApplication::processEvents();
                                           }
                                       });
            progress.reset();
            qInfo() << "bigfile extraction complete:" << count << "entries";
            QMessageBox box(QMessageBox::Information, tr("Extract Archive"),
                             tr("Extracted %1 entries to:\n%2\n\n(The archive stores name "
                                "hashes rather than filenames, so entries are numbered — "
                                "browse the folder to identify them by content.)")
                                  .arg(count)
                                  .arg(QDir::toNativeSeparators(outDir)),
                             QMessageBox::Ok, this);
            QPushButton* browseBtn =
                box.addButton(tr("Browse in M2Suite"), QMessageBox::ActionRole);
            box.exec();
            if (box.clickedButton() == browseBtn) {
                scanFolderIntoTree(outDir);
            }
        } catch (const std::exception&) {
            progress.reset();
            qWarning() << "extraction failed:" << discError.what();
            QMessageBox::warning(this, tr("Extract Disc Image"),
                                  tr("Could not read this file as a 3DO disc image or a "
                                     "known archive:\n%1")
                                       .arg(discError.what()));
        }
    }
}

void MainWindow::convertImagesToUtf() {
    QSettings settings;
    QStringList images = QFileDialog::getOpenFileNames(
        this, tr("Choose Images to Convert to UTF"),
        settings.value(QStringLiteral("lastConvertDir")).toString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif);;All files (*)"));
    if (images.isEmpty()) {
        return;
    }
    settings.setValue(QStringLiteral("lastConvertDir"),
                       QFileInfo(images.front()).absolutePath());

    QString outDir = QFileDialog::getExistingDirectory(
        this, tr("Choose Output Folder"),
        settings.value(QStringLiteral("exportDir")).toString());
    if (outDir.isEmpty()) {
        return;
    }
    settings.setValue(QStringLiteral("exportDir"), outDir);

    int written = 0;
    QStringList errors;
    for (const QString& path : images) {
        QImage img(path);
        if (img.isNull()) {
            errors << tr("%1: could not load image").arg(QFileInfo(path).fileName());
            continue;
        }
        img = img.convertToFormat(QImage::Format_RGBA8888);
        std::vector<m2texture::Rgba8> pixels(size_t(img.width()) * img.height());
        for (int y = 0; y < img.height(); ++y) {
            const uchar* line = img.constScanLine(y);
            for (int x = 0; x < img.width(); ++x) {
                m2texture::Rgba8& p = pixels[size_t(y) * img.width() + x];
                p.r = line[x * 4 + 0];
                p.g = line[x * 4 + 1];
                p.b = line[x * 4 + 2];
                p.a = line[x * 4 + 3];
            }
        }
        QString out = QStringLiteral("%1/%2.utf").arg(outDir, QFileInfo(path).completeBaseName());
        try {
            auto opts = m2texture::defaultEncodeOptions(pixels.data(), uint32_t(img.width()),
                                                         uint32_t(img.height()));
            m2texture::writeUtfFile(out.toStdWString(), pixels.data(), uint32_t(img.width()),
                                     uint32_t(img.height()), opts);
            ++written;
            qInfo() << "converted" << path << "->" << out << "alphaDepth" << opts.alphaDepth;
        } catch (const std::exception& e) {
            errors << tr("%1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what()));
        }
    }

    QString summary = tr("%1 texture(s) written to\n%2").arg(written).arg(outDir);
    if (!errors.isEmpty()) {
        summary += tr("\n\n%1 failure(s):\n").arg(errors.size()) + errors.join('\n');
    }
    QMessageBox box(QMessageBox::Information, tr("Convert to UTF"), summary, QMessageBox::Ok,
                     this);
    QPushButton* openBtn = box.addButton(tr("Open Folder"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == openBtn) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                 {QDir::toNativeSeparators(outDir)});
    }
}

void MainWindow::openSingleFile() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open M2 File"), QString(),
        tr("All supported (*.utf *.cel *.anim *.aiff *.aif *.aifc *.wav *.str *.stream "
           "*.mov *.gif *.jpg *.png *.bmp);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }
    FileType type = sniffFileType(path);
    if (type == FileType::Unknown) {
        QMessageBox::information(this, tr("Unknown file type"),
                                  tr("Could not identify this file by magic bytes or "
                                     "extension. It may be an unsupported M2 format."));
        return;
    }
    openPath(path, type);
}

void MainWindow::scanFolderIntoTree(const QString& rootPathIn) {
    // Normalize: a trailing slash (e.g. "…\Repositories\") breaks the
    // parent-folder lookup below — QFileInfo::absolutePath() of children
    // never string-matches the slash-suffixed root, so every folder used
    // to escape the root item and the tree rendered scrambled.
    const QString rootPath = QDir::cleanPath(rootPathIn);

    qInfo() << "scanning folder" << rootPath;
    fileTree_->clear();
    fileTree_->setUpdatesEnabled(false);
    QStyle* st = style();
    QIcon dirIcon = st->standardIcon(QStyle::SP_DirIcon);
    QIcon fileIcon = st->standardIcon(QStyle::SP_FileIcon);

    auto* rootItem = new TreeItem(fileTree_);
    rootItem->setText(0, QFileInfo(rootPath).fileName());
    rootItem->setIcon(0, dirIcon);
    rootItem->setData(0, kPathRole, rootPath);

    QHash<QString, QTreeWidgetItem*> folderItems;
    folderItems.insert(rootPath, rootItem);

    std::function<QTreeWidgetItem*(const QString&)> folderItemFor =
        [&](const QString& dirPath) -> QTreeWidgetItem* {
        auto it = folderItems.find(dirPath);
        if (it != folderItems.end()) {
            return *it;
        }
        if (!dirPath.startsWith(rootPath) || dirPath.size() <= rootPath.size()) {
            return rootItem; // never walk above the chosen root
        }
        QFileInfo info(dirPath);
        QTreeWidgetItem* parent = folderItemFor(info.absolutePath());
        auto* item = new TreeItem(parent);
        item->setText(0, info.fileName());
        item->setIcon(0, dirIcon);
        item->setData(0, kPathRole, dirPath);
        folderItems.insert(dirPath, item);
        return item;
    };

    // Busy indicator for large folders: appears after ~400 ms, shows the
    // running counts, and lets the user cancel a huge scan (e.g. the
    // Repositories folder with hundreds of thousands of files). NOTE: with
    // a (0,0) busy range QProgressDialog never auto-shows (auto-show hangs
    // off setValue), so it is shown explicitly once the scan runs long.
    QProgressDialog progress(tr("Scanning folder..."), tr("Cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    QElapsedTimer scanClock;
    scanClock.start();

    int found = 0;
    int visited = 0;
    QDirIterator iter(rootPath, QDir::Files, QDirIterator::Subdirectories);
    while (iter.hasNext()) {
        QString filePath = iter.next();
        if (++visited % 64 == 0) {
            if (!progress.isVisible() && scanClock.elapsed() > 400) {
                progress.show();
            }
            if (progress.isVisible()) {
                progress.setLabelText(tr("Scanning folder...\n%1 files examined, %2 recognized")
                                           .arg(visited)
                                           .arg(found));
            }
            QCoreApplication::processEvents();
            if (progress.wasCanceled()) {
                qInfo() << "folder scan canceled after" << visited << "files";
                break;
            }
        }
        // Source-control internals make huge trees (FFmpeg/mame repos) and
        // are never game content.
        if (filePath.contains(QStringLiteral("/.git/")) ||
            filePath.contains(QStringLiteral("\\.git\\"))) {
            continue;
        }
        FileType type = sniffFileType(filePath);
        if (type == FileType::Unknown) {
            continue;
        }
        QFileInfo info(filePath);
        QTreeWidgetItem* parent = folderItemFor(info.absolutePath());
        auto* item = new TreeItem(parent);
        item->setText(0, info.fileName());
        item->setText(1, fileTypeLabel(type));
        item->setText(2, QLocale().formattedDataSize(info.size(), 1));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->setIcon(0, fileIcon);
        item->setData(0, kPathRole, filePath);
        item->setData(0, kTypeRole, int(type));
        if (!fileTypeHasPreview(type)) {
            item->setForeground(0, QBrush(QColor(150, 150, 150)));
            item->setForeground(1, QBrush(QColor(150, 150, 150)));
        }
        if (isAitdArchiveType(type)) {
            // Give the archive an expander without paying to open it during
            // the scan — a folder of AITD PAKs holds thousands of entries,
            // and decompressing them all here would make browsing crawl.
            auto* placeholder = new TreeItem(item);
            placeholder->setData(0, kPlaceholderRole, true);
            placeholder->setText(0, tr("(expand to list entries)"));
            placeholder->setForeground(0, QBrush(QColor(150, 150, 150)));
        }
        ++found;
    }
    progress.reset();
    fileTree_->setUpdatesEnabled(true);

    fileTree_->expandItem(rootItem);
    fileTree_->sortItems(0, Qt::AscendingOrder);
    applyTypeFilter();
    qInfo() << "scan complete:" << found << "recognized of" << visited << "files";
    QSettings().setValue(QStringLiteral("lastFolder"), rootPath);
    infoView_->setPlainText(
        tr("Scanned %1\n%2 recognized files.\n\nClick a file (or navigate with arrow keys) "
           "to preview it.\nSelect several (Ctrl/Shift) and use Export Selected to batch-"
           "convert.\nGrayed entries are recognized formats without a viewer yet.")
            .arg(rootPath)
            .arg(found));
}

void MainWindow::applyTypeFilter() {
    int filterIndex = typeFilter_->currentIndex();
    std::function<bool(QTreeWidgetItem*)> filterItem = [&](QTreeWidgetItem* item) -> bool {
        // Archive entries inherit their archive's visibility: filtering a
        // model archive out but leaving its models behind would be
        // nonsense, and the entries carry the archive's own type anyway.
        if (item->data(0, kEntryRole).isValid() ||
            item->data(0, kPlaceholderRole).isValid()) {
            item->setHidden(false);
            return true;
        }
        QVariant typeVar = item->data(0, kTypeRole);
        if (typeVar.isValid()) {
            bool visible = typeMatchesFilter(FileType(typeVar.toInt()), filterIndex);
            item->setHidden(!visible);
            // Hiding a parent hides its children implicitly, so there is no
            // need to walk into an archive that is itself filtered out.
            return visible;
        }
        bool anyVisible = false;
        for (int i = 0; i < item->childCount(); ++i) {
            if (filterItem(item->child(i))) {
                anyVisible = true;
            }
        }
        item->setHidden(!anyVisible);
        return anyVisible;
    };
    for (int i = 0; i < fileTree_->topLevelItemCount(); ++i) {
        filterItem(fileTree_->topLevelItem(i));
    }
}

void MainWindow::currentFileChanged(QTreeWidgetItem* current, QTreeWidgetItem*) {
    if (!current) {
        return;
    }
    // An archive entry: open its archive (if it isn't already) and jump
    // straight to that entry.
    QVariant entryVar = current->data(0, kEntryRole);
    if (entryVar.isValid()) {
        QString archivePath = current->data(0, kPathRole).toString();
        if (aitdPakPath_ != archivePath || selectorMode_ != SelectorMode::AitdBodies) {
            openPath(archivePath, FileType(current->data(0, kTypeRole).toInt()));
        }
        int entry = entryVar.toInt();
        if (entry >= 0 && entry < textureSelect_->count()) {
            textureSelect_->setCurrentIndex(entry);
        }
        return;
    }
    QVariant typeVar = current->data(0, kTypeRole);
    if (!typeVar.isValid()) {
        return;
    }
    openPath(current->data(0, kPathRole).toString(), FileType(typeVar.toInt()));
    if (autoplayCheck_->isChecked() && playButton_->isEnabled()) {
        playClicked();
    }
}

void MainWindow::expandArchiveItem(QTreeWidgetItem* item) {
    if (!item || item->childCount() != 1 ||
        !item->child(0)->data(0, kPlaceholderRole).isValid()) {
        return; // not an unexpanded archive
    }
    QVariant typeVar = item->data(0, kTypeRole);
    if (!typeVar.isValid()) {
        return;
    }
    FileType type = FileType(typeVar.toInt());
    QString path = item->data(0, kPathRole).toString();

    delete item->takeChild(0);

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QHash<int, QString> names = loadAitdNameDatabase(path);
    try {
        m2model::AitdPak pak = m2model::AitdPak::openFromFile(path.toStdWString());
        for (size_t i = 0; i < pak.entryCount(); ++i) {
            std::vector<uint8_t> raw = pak.read(i);
            m2model::AitdBody body = m2model::parseAitdBody(raw);
            bool isModel = body.valid && body.vertexCount() >= 4 && body.primitives.size() >= 2;

            auto* child = new TreeItem(item);
            QString name = names.value(int(i));
            child->setText(0, name.isEmpty() ? tr("Entry %1").arg(i)
                                              : tr("%1 — %2").arg(i).arg(name));
            child->setText(1, isModel ? tr("3D model (%1 verts)").arg(body.vertexCount())
                                       : tr("Data"));
            child->setText(2, QLocale().formattedDataSize(qint64(raw.size()), 1));
            child->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
            child->setData(0, kPathRole, path);
            child->setData(0, kTypeRole, int(type));
            child->setData(0, kEntryRole, int(i));
            if (!isModel) {
                child->setForeground(0, QBrush(QColor(150, 150, 150)));
                child->setForeground(1, QBrush(QColor(150, 150, 150)));
            }
        }
    } catch (const std::exception& e) {
        auto* child = new TreeItem(item);
        child->setText(0, tr("Failed to read archive: %1").arg(e.what()));
        child->setForeground(0, QBrush(QColor(200, 120, 120)));
    }
    QGuiApplication::restoreOverrideCursor();
    applyTypeFilter();
}

void MainWindow::fileTreeContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = fileTree_->itemAt(pos);
    if (!item) {
        return;
    }
    QString path = item->data(0, kPathRole).toString();
    bool isFile = item->data(0, kTypeRole).isValid();

    QMenu menu(this);
    QAction* exportAction = isFile ? menu.addAction(tr("Export...")) : nullptr;
    QAction* revealAction = menu.addAction(tr("Show in Folder"));
    QAction* chosen = menu.exec(fileTree_->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }
    if (chosen == exportAction) {
        exportSelected();
    } else if (chosen == revealAction) {
        QStringList args;
        if (isFile) {
            args << QStringLiteral("/select,") << QDir::toNativeSeparators(path);
        } else {
            args << QDir::toNativeSeparators(path);
        }
        QProcess::startDetached(QStringLiteral("explorer.exe"), args);
    }
}

void MainWindow::openPath(const QString& path, FileType type) {
    qInfo() << "open" << fileTypeLabel(type) << path;
    stopAllPlayback();
    playButton_->setEnabled(false);
    stopButton_->setEnabled(false);

    switch (type) {
        case FileType::UtfTexture:
            showTextureFile(path);
            break;
        case FileType::Cel:
            showCelFile(path);
            break;
        case FileType::Anim:
            showAnimFile(path);
            break;
        case FileType::Imag:
            showImagFile(path);
            break;
        case FileType::StandardImage:
            showStandardImageFile(path);
            break;
        case FileType::Aiff:
        case FileType::Aifc:
            showAudioFile(path, type);
            break;
        case FileType::Wav:
            showWavFile(path);
            break;
        case FileType::StreamFile:
        case FileType::FilmFile:
            // Both route through the same handler: Stream::load auto-detects
            // a standalone 'FILM' container and parses it into the same
            // Cinepak film fields a DataStreamer movie uses.
            showStreamFile(path);
            break;
        case FileType::Elf:
            showElfFile(path);
            break;
        case FileType::AitdRooms:
            // Rooms assemble into one floor plan rather than being browsed
            // entry by entry, so they get their own handler.
            showAitdRoomsFile(path);
            break;
        case FileType::AitdPak:
        case FileType::AitdArchive:
        case FileType::AitdAnimPak:
        case FileType::AitdMaskPak:
        case FileType::AitdSoundPak:
        case FileType::AitdScript:
            // Every other AITD PAK opens the same browser: entries are
            // listed and named, and any that parse as geometry render in
            // the viewport. A mask or script archive simply has none.
            showAitdPakFile(path);
            break;
        case FileType::AitdImage:
            showAitdImageFile(path);
            break;
        case FileType::SoundCatalogue:
            showSoundCatalogue(path);
            break;
        case FileType::M1vc:
            showM1vcFile(path);
            break;
        case FileType::Ddf:
        case FileType::Intl:
            showFormTextFile(path, type);
            break;
        case FileType::Instrument:
            showDspFile(path);
            break;
        default:
            showInfoOnly(path, type);
            break;
    }
}

void MainWindow::showAboutDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("About M2Suite"));

    auto* logo = new QLabel;
    logo->setPixmap(QPixmap(QStringLiteral(":/logo.png"))
                         .scaled(360, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);

    auto* mascot = new QLabel;
    mascot->setPixmap(QPixmap(QStringLiteral(":/mascot.png"))
                           .scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    mascot->setAlignment(Qt::AlignCenter);

    const int year = QDate::currentDate().year();
    auto* text = new QLabel;
    text->setTextFormat(Qt::RichText);
    text->setOpenExternalLinks(true);
    text->setWordWrap(true);
    text->setText(tr(
        "<h2>M2Suite</h2>"
        "<p><b>Converter &amp; Visualizer for 3DO / Panasonic M2 file formats</b></p>"
        "<p>Build %8 &nbsp;·&nbsp; %9</p>"
        "<p>Created with AI assistance, vibe-coded by <b>Hackinside</b>.<br>&copy; %1</p>"
        "<h3>Special Thanks</h3>"
        "<ul>"
        "<li><b>Trapexit</b> — 3DO documentation, 3it/3do-devkit and format reverse-engineering</li>"
        "<li>The 3DO / M2 preservation community and documentation maintainers</li>"
        "</ul>"
        "<h3>Reference source used</h3>"
        "<ul>"
        "<li>Dr. Tim Ferguson — Cinepak (CVID) decoder</li>"
        "<li>vgmstream — SDX2 / CBD2 audio decoders</li>"
        "<li>ppcd (org / ogamespec, CC0) — PowerPC disassembler</li>"
        "<li>FFmpeg — Cinepak &amp; standalone FILM reference, MPEG backend</li>"
        "<li>Mark Adler / AITD-tools — PKWARE explode (Alone in the Dark PAK)</li>"
        "<li>fitd / AITD_PakEdit — Alone in the Dark 3D body format</li>"
        "<li>3DO M2 SDK / Portfolio OS source (Mercury, ws_root)</li>"
        "<li>CelViewer, opera-libretro, scummvm image-codecs</li>"
        "</ul>"
        "<p style='color:gray'>Built with Qt %2. Not affiliated with Panasonic or "
        "the 3DO Company.</p>")
        .arg(year)
        .arg(QT_VERSION_STR)
        .arg(m2suite::buildNumber())
        .arg(QString::fromLatin1(m2suite::buildDate())));

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(logo, 1);
    topRow->addWidget(mascot, 0);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);

    auto* scroll = new QScrollArea;
    scroll->setWidget(text);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* layout = new QVBoxLayout(&dlg);
    layout->addLayout(topRow);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);
    dlg.resize(560, 620);
    dlg.exec();
}

void MainWindow::showElfFile(const QString& path) {
    selectorMode_ = SelectorMode::None;
    textureSelect_->clear();
    lodSelect_->clear();
    try {
        m2disasm::Elf elf = m2disasm::Elf::loadFromFile(path.toStdWString());

        QString info;
        info += QStringLiteral("File: %1\n\n").arg(path);
        info += QStringLiteral("ELF32 big-endian executable\n");
        info += QStringLiteral("Machine: %1\n")
                     .arg(elf.isPowerPC() ? QStringLiteral("PowerPC (M2 CPU)")
                                           : QStringLiteral("0x%1").arg(elf.machine(), 0, 16));
        info += QStringLiteral("Entry point: 0x%1\n").arg(elf.entryPoint(), 8, 16, QChar('0'));

        // 3DO application header (.hdr3do / _3DOBinHeader).
        const auto& h = elf.binHeader();
        if (h.valid) {
            info += QStringLiteral("\n3DO application header:\n");
            if (!h.name.empty()) {
                info += QStringLiteral("  Name: %1\n").arg(QString::fromStdString(h.name));
            }
            info += QStringLiteral("  Target OS: %1.%2   Stack: %3 bytes\n")
                         .arg(h.osVersion)
                         .arg(h.osRevision)
                         .arg(h.stack);
            if (h.flags) {
                info += QStringLiteral("  Flags: 0x%1%2%3\n")
                             .arg(h.flags, 2, 16, QChar('0'))
                             .arg(h.flags & 0x02 ? QStringLiteral(" [privileged]") : QString())
                             .arg(h.flags & 0x08 ? QStringLiteral(" [showinfo]") : QString());
            }
        }

        info += QStringLiteral("\nSymbols: %1   Imported folios: %2\n\nSections:\n")
                     .arg(elf.symbols().size())
                     .arg(elf.importRecords().size());
        for (const auto& s : elf.sections()) {
            if (s.name.empty() && s.size == 0) {
                continue;
            }
            info += QStringLiteral("  %1  addr 0x%2  size %3%4%5\n")
                         .arg(QString::fromStdString(s.name), -18)
                         .arg(s.addr, 8, 16, QChar('0'))
                         .arg(s.size)
                         .arg(s.executable() ? QStringLiteral("  [code]") : QString())
                         .arg(s.wasCompressed ? QStringLiteral("  [was LZSS-compressed]")
                                               : QString());
        }
        // Structured .imp3do imports: folio name, version, loader flags.
        if (!elf.importRecords().empty()) {
            info += QStringLiteral("\nImported OS folios (.imp3do):\n");
            for (const auto& imp : elf.importRecords()) {
                info += QStringLiteral("  %1 v%2.%3%4\n")
                             .arg(QString::fromStdString(imp.name), -18)
                             .arg(imp.version)
                             .arg(imp.revision)
                             .arg(imp.flags & 0x01 ? QStringLiteral("  [IMPORT_NOW]") : QString());
            }
        } else if (!elf.imports().empty()) {
            info += QStringLiteral("\n3DO imports (.imp3do):\n");
            for (const auto& imp : elf.imports()) {
                info += QStringLiteral("  %1\n").arg(QString::fromStdString(imp));
            }
        }
        infoView_->setPlainText(info);

        // Symbol-annotated disassembly, ANSI-C pseudocode reconstruction,
        // then a strings dump (display capped; Export writes the full set).
        std::string listing = elf.disassembleAll(20000);
        listing += "\n\n; ===== ANSI-C pseudocode reconstruction =====\n";
        listing += m2disasm::Pseudocode(elf).liftAll(400);
        listing += "\n";
        listing += elf.extractStrings();
        textView_->setPlainText(QString::fromStdString(listing));
        viewStack_->setCurrentWidget(textView_);
    } catch (const std::exception& e) {
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Failed to parse ELF:\n%1").arg(e.what()));
        infoView_->setPlainText(tr("File: %1\n\nError: %2").arg(path).arg(e.what()));
    }
}

void MainWindow::showM1vcFile(const QString& path) {
    selectorMode_ = SelectorMode::None;
    textureSelect_->clear();
    lodSelect_->clear();
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("could not open file");
        }
        QByteArray bytes = file.readAll();
        auto payload = m2stream::extractTag0Data(
            reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size()));
        if (payload.size() < 4 ||
            !(payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x01)) {
            throw std::runtime_error("M1VC DATA payload is not an MPEG stream");
        }

        // The DATA payload is a raw MPEG-1 video ES — write to a temp file
        // and hand it to the ffmpeg-backed player (same path as MPVD).
        mpegTemp_ = std::make_unique<QTemporaryFile>(
            QDir::tempPath() + QStringLiteral("/m2suite_m1vc_XXXXXX.m1v"));
        if (!mpegTemp_->open()) {
            throw std::runtime_error("could not create temp file");
        }
        mpegTemp_->write(reinterpret_cast<const char*>(payload.data()), qint64(payload.size()));
        mpegTemp_->flush();
        playMpegFromFile(mpegTemp_->fileName(), /*hasVideo=*/true);

        infoView_->setPlainText(tr("File: %1\n\nM1VC video (MPEG-1 in a TAG0 container)\n"
                                    "Payload: %2 KB\n\nPress Play.")
                                     .arg(path)
                                     .arg(payload.size() / 1024));
    } catch (const std::exception& e) {
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Failed to open M1VC:\n%1").arg(e.what()));
        infoView_->setPlainText(tr("File: %1\n\nError: %2").arg(path).arg(e.what()));
    }
}

void MainWindow::showFormTextFile(const QString& path, FileType type) {
    selectorMode_ = SelectorMode::None;
    textureSelect_->clear();
    lodSelect_->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        showInfoOnly(path, type);
        return;
    }
    QByteArray bytes = file.readAll();
    const auto* d = reinterpret_cast<const uint8_t*>(bytes.constData());
    size_t n = size_t(bytes.size());

    // A DDF/INTL file is an IFF FORM. Render a readable listing of its
    // chunks: 4-char id, size, and any printable ASCII body as text.
    QString out;
    out += tr("File: %1\n\n").arg(path);
    if (n >= 12 && d[0] == 'F' && d[1] == 'O' && d[2] == 'R' && d[3] == 'M') {
        char formType[5] = {char(d[8]), char(d[9]), char(d[10]), char(d[11]), 0};
        out += tr("IFF FORM '%1'\n\nChunks:\n").arg(formType);
        size_t pos = 12;
        while (pos + 8 <= n) {
            char id[5] = {char(d[pos]), char(d[pos + 1]), char(d[pos + 2]), char(d[pos + 3]), 0};
            uint32_t sz = (uint32_t(d[pos + 4]) << 24) | (d[pos + 5] << 16) |
                          (d[pos + 6] << 8) | d[pos + 7];
            out += QStringLiteral("  %1  (%2 bytes)").arg(id).arg(sz);
            // Show printable body inline (names, versions).
            QString body;
            for (size_t i = pos + 8; i < pos + 8 + sz && i < n && body.size() < 64; ++i) {
                char c = char(d[i]);
                if (c >= 32 && c <= 126) {
                    body += QChar(c);
                } else if (!body.isEmpty() && !body.endsWith(' ')) {
                    body += ' ';
                }
            }
            if (!body.trimmed().isEmpty()) {
                out += QStringLiteral("  \"%1\"").arg(body.trimmed());
            }
            out += '\n';
            pos += 8 + ((sz + 1) & ~size_t(1)); // IFF-85 even alignment
        }
    } else {
        out += tr("(not an IFF FORM — showing printable strings)\n\n");
        QString run;
        for (size_t i = 0; i < n; ++i) {
            char c = char(d[i]);
            if (c >= 32 && c <= 126) {
                run += QChar(c);
            } else {
                if (run.size() >= 4) {
                    out += run + '\n';
                }
                run.clear();
            }
        }
    }
    textView_->setPlainText(out);
    viewStack_->setCurrentWidget(textView_);
    infoView_->setPlainText(tr("File: %1\nType: %2\nSize: %3 bytes")
                                 .arg(path)
                                 .arg(fileTypeLabel(type))
                                 .arg(n));
}

void MainWindow::showDspFile(const QString& path) {
    selectorMode_ = SelectorMode::None;
    textureSelect_->clear();
    lodSelect_->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        showInfoOnly(path, FileType::Instrument);
        return;
    }
    QByteArray bytes = file.readAll();
    const auto* d = reinterpret_cast<const uint8_t*>(bytes.constData());
    size_t n = size_t(bytes.size());
    auto u32 = [d](size_t p) {
        return (uint32_t(d[p]) << 24) | (d[p + 1] << 16) | (d[p + 2] << 8) | d[p + 3];
    };
    auto id4 = [d](size_t p) {
        return QString::fromLatin1(reinterpret_cast<const char*>(d + p), 4);
    };

    QString out;
    out += tr("File: %1\n\n").arg(path);
    if (n < 12 || id4(0) != QStringLiteral("FORM")) {
        showFormTextFile(path, FileType::Instrument);
        return;
    }
    QString formType = id4(8);
    out += tr("IFF FORM '%1'  (%2 bytes)\n").arg(formType).arg(n);
    if (formType == QStringLiteral("3INS")) {
        out += tr("Type: 3DO DSP Instrument (verified 3INS header)\n");
    }
    out += QStringLiteral("\nChunks:\n");

    // Walk chunks recursively so the nested FORM DSPP is expanded. The DSPP
    // sub-chunks (DHDR/DCOD/DRSC/...) are decoded against the SDK structs in
    // audio/dspp_template.h.
    std::function<void(size_t, size_t, int)> walk = [&](size_t pos, size_t end, int depth) {
        QString indent(depth * 2, QChar(' '));
        while (pos + 8 <= end) {
            QString cid = id4(pos);
            uint32_t sz = u32(pos + 4);
            size_t body = pos + 8;
            if (body + sz > end) {
                sz = uint32_t(end - body);
            }
            out += QStringLiteral("%1%2  (%3 bytes)").arg(indent, cid).arg(sz);

            if (cid == QStringLiteral("FORM") && sz >= 4) {
                out += QStringLiteral("  form '%1'\n").arg(id4(body));
                walk(body + 4, body + sz, depth + 1);
            } else if (cid == QStringLiteral("NAME") || cid == QStringLiteral("NAM ")) {
                out += QStringLiteral("  \"%1\"\n").arg(QString::fromLatin1(
                    reinterpret_cast<const char*>(d + body), int(qMin<uint32_t>(sz, 64))));
            } else if (cid == QStringLiteral("DHDR") && sz >= 16) {
                // DSPPHeader: FunctionID, SiliconVersion, FormatVersion, Flags.
                uint32_t funcId = u32(body);
                uint32_t silicon = u32(body + 4);
                uint32_t fmt = u32(body + 8);
                uint32_t flags = u32(body + 12);
                static const char* kSilicon[] = {"?",      "BLUE", "RED",
                                                  "GREEN",  "ANVIL", "BULLDOG"};
                QString siliconName = (silicon < 6) ? QString::fromLatin1(kSilicon[silicon])
                                                     : QStringLiteral("0x%1").arg(silicon, 0, 16);
                out += QStringLiteral("\n%1  DSP header: FunctionID=%2  Silicon=%3(%4)  "
                                       "FormatVer=%5  Flags=0x%6%7\n")
                            .arg(indent)
                            .arg(funcId)
                            .arg(silicon)
                            .arg(siliconName)
                            .arg(fmt)
                            .arg(flags, 0, 16)
                            .arg(flags & 0x1 ? QStringLiteral(" [privileged]") : QString());
            } else if (cid == QStringLiteral("DCOD")) {
                out += QStringLiteral("  DSP code image (%1 instruction words)\n")
                            .arg(sz >= 8 ? (sz - 8) / 2 : 0);
            } else {
                out += QStringLiteral("\n");
            }
            pos = body + ((size_t(sz) + 1) & ~size_t(1)); // IFF even alignment
        }
    };
    walk(12, 8 + u32(4) < n ? 8 + u32(4) : n, 0);

    textView_->setPlainText(out);
    viewStack_->setCurrentWidget(textView_);
    infoView_->setPlainText(tr("File: %1\nType: %2\nSize: %3 bytes")
                                 .arg(path)
                                 .arg(fileTypeLabel(FileType::Instrument))
                                 .arg(n));
}

// --------------------------------------------------------------------------
// Playback

void MainWindow::playMpegFromFile(const QString& mpegPath, bool hasVideo) {
    mediaPlayer_ = new QMediaPlayer(this);
    mediaAudio_ = new QAudioOutput(this);
    mediaPlayer_->setAudioOutput(mediaAudio_);
    mediaPlayer_->setVideoOutput(videoWidget_);
    mediaPlayer_->setSource(QUrl::fromLocalFile(mpegPath));
    connect(mediaPlayer_, &QMediaPlayer::errorOccurred, this,
             [this](QMediaPlayer::Error, const QString& message) {
                 infoView_->appendPlainText(tr("\nPlayback error: %1").arg(message));
             });
    if (hasVideo) {
        viewStack_->setCurrentWidget(videoWidget_);
    }
    playbackKind_ = PlaybackKind::Mpeg;
    playButton_->setEnabled(true);
}

void MainWindow::stopAllPlayback() {
    playbackKind_ = PlaybackKind::None;
    cinepakTimer_->stop();
    if (animTimer_) {
        animTimer_->stop();
    }
    if (aitdTimer_) {
        aitdTimer_->stop();
    }
    if (audioSink_) {
        audioSink_->stop();
        delete audioSink_;
        audioSink_ = nullptr;
    }
    if (audioBuffer_) {
        delete audioBuffer_;
        audioBuffer_ = nullptr;
    }
    if (mediaPlayer_) {
        mediaPlayer_->stop();
        delete mediaPlayer_;
        mediaPlayer_ = nullptr;
    }
    if (mediaAudio_) {
        delete mediaAudio_;
        mediaAudio_ = nullptr;
    }
}

void MainWindow::startPcmPlayback() {
    QAudioFormat fmt;
    fmt.setSampleRate(pcmRate_);
    fmt.setChannelCount(pcmChannels_);
    fmt.setSampleFormat(QAudioFormat::Int16);

    QByteArray bytes(reinterpret_cast<const char*>(pcm_.data()),
                      qsizetype(pcm_.size() * sizeof(int16_t)));
    audioBuffer_ = new QBuffer;
    audioBuffer_->setData(bytes);
    audioBuffer_->open(QIODevice::ReadOnly);
    audioSink_ = new QAudioSink(fmt);
    audioSink_->start(audioBuffer_);
}

void MainWindow::playClicked() {
    try {
        qInfo() << "play clicked, kind" << int(playbackKind_);
        playClickedImpl();
    } catch (const std::exception& e) {
        qWarning() << "playback start failed:" << e.what();
        stopAllPlayback();
        infoView_->appendPlainText(tr("\nPlayback failed: %1").arg(e.what()));
    }
}

void MainWindow::playClickedImpl() {
    switch (playbackKind_) {
        case PlaybackKind::Pcm:
            if (audioSink_) { // restart from the top
                stopAllPlayback();
                playbackKind_ = PlaybackKind::Pcm;
            }
            startPcmPlayback();
            stopButton_->setEnabled(true);
            break;
        case PlaybackKind::Cinepak: {
            if (!stream_ || stream_->filmFrames().empty()) {
                break;
            }
            if (cinepakTimer_->isActive()) {
                break; // already playing — Stop first to restart
            }
            cinepakFrame_ = 0;
            cinepak_ = std::make_unique<m2stream::CinepakDecoder>();
            const auto& info = stream_->film();
            cinepakBuffer_.assign(size_t(info.width) * info.height * 4, 0);

            const auto& frames = stream_->filmFrames();
            firstFrameTime_ = frames.empty() ? 0 : frames.front().time;
            streamHz_ = 240.0; // 3DO DataStreamer default tick rate

            // Start SNDS audio when the film carries a decodable track
            // (SDX2/CBD2/raw), and derive streamHz from the film's
            // timestamp span vs the audio's true duration so video is
            // locked to the audio clock.
            if (stream_->hasAudio()) {
                pcm_ = decodeSndsAudio(*stream_);
            }
            if (!pcm_.empty()) {
                pcmRate_ = int(stream_->audio().sampleRate);
                pcmChannels_ = int(stream_->audio().channels);
                if (pcmRate_ > 0 && pcmChannels_ > 0 && frames.size() > 1 &&
                    frames.back().time > firstFrameTime_) {
                    double durationSec = double(pcm_.size() / pcmChannels_) / pcmRate_;
                    double tickSpan = double(frames.back().time - firstFrameTime_);
                    if (durationSec > 0.5) {
                        // Extend the tick span by one average frame so the
                        // last frame isn't held a whole frame too long.
                        double avgDelta = tickSpan / (frames.size() - 1);
                        streamHz_ = (tickSpan + avgDelta) / durationSec;
                    }
                }
                startPcmPlayback();
            }
            playbackClock_.restart();
            cinepakTick(); // show frame 0 immediately and arm the timer
            cinepakTimer_->start(15);
            stopButton_->setEnabled(true);
            break;
        }
        case PlaybackKind::Mpeg:
            if (mediaPlayer_) {
                mediaPlayer_->play();
                stopButton_->setEnabled(true);
            }
            break;
        case PlaybackKind::CelAnim:
            if (animFrames_.size() > 1 && !animTimer_->isActive()) {
                animTimer_->start();
                stopButton_->setEnabled(true);
            }
            break;
        default:
            break;
    }
}

void MainWindow::stopClicked() {
    qInfo() << "stop clicked";
    PlaybackKind kind = playbackKind_;
    stopAllPlayback();
    playbackKind_ = kind; // keep content loaded so Play works again
    stopButton_->setEnabled(false);
}

void MainWindow::showAitdPakFile(const QString& path) {
    stopAllPlayback();
    aitdPakPath_ = path;
    try {
        aitdPak_ = std::make_unique<m2model::AitdPak>(
            m2model::AitdPak::openFromFile(path.toStdWString()));
    } catch (const std::exception& e) {
        selectorMode_ = SelectorMode::None;
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Failed to open PAK:\n%1").arg(e.what()));
        return;
    }

    // Community name databases give entries real names ("Emily Hartwood"
    // rather than "Model 12"), which is the difference between browsing an
    // archive and searching it.
    aitdNames_ = loadAitdNameDatabase(path);

    // Populate the frame selector with entries that parse as bodies.
    selectorMode_ = SelectorMode::AitdBodies;
    textureSelect_->blockSignals(true);
    textureSelect_->clear();
    int firstBody = -1;
    for (size_t i = 0; i < aitdPak_->entryCount(); ++i) {
        m2model::AitdBody b = m2model::parseAitdBody(aitdPak_->read(i));
        QString name = aitdNames_.value(int(i));
        QString label;
        if (!b.valid) {
            label = tr("Entry %1 (not a model)").arg(i);
        } else if (name.isEmpty()) {
            label = tr("Model %1 (%2 verts)").arg(i).arg(b.vertexCount());
        } else {
            label = tr("%1 — %2").arg(i).arg(name);
        }
        textureSelect_->addItem(label);
        if (b.valid && firstBody < 0) {
            firstBody = int(i);
        }
    }
    textureSelect_->setCurrentIndex(firstBody < 0 ? 0 : firstBody);
    textureSelect_->blockSignals(false);
    lodSelect_->blockSignals(true);
    lodSelect_->clear();
    lodSelect_->blockSignals(false);

    // The LOD combo doubles as the render-mode picker for models.
    lodSelect_->blockSignals(true);
    lodSelect_->clear();
    lodSelect_->addItem(tr("Solid (materials)"));
    lodSelect_->addItem(tr("Solid (flat)"));
    lodSelect_->addItem(tr("Wireframe"));
    lodSelect_->addItem(tr("Points"));
    lodSelect_->setCurrentIndex(0);
    lodSelect_->blockSignals(false);

    refreshTexturePreview(); // renders the selected model
    aitdTimer_->start(40);   // ~25 fps spin
}

void MainWindow::convertAudioTo3do() {
    QStringList inputs = QFileDialog::getOpenFileNames(
        this, tr("Choose audio to convert"),
        QSettings().value(QStringLiteral("lastAudioIn")).toString(),
        tr("Audio (*.wav *.aiff *.aif *.aifc);;All files (*)"));
    if (inputs.isEmpty()) {
        return;
    }
    QSettings().setValue(QStringLiteral("lastAudioIn"), QFileInfo(inputs.front()).absolutePath());

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Convert audio to 3DO format"));
    auto* form = new QFormLayout;

    auto* codecBox = new QComboBox;
    const std::vector<m2audio::AudioCodec> codecs{
        m2audio::AudioCodec::Sdx2,  m2audio::AudioCodec::Cbd2,  m2audio::AudioCodec::Adp4,
        m2audio::AudioCodec::Sqs2,  m2audio::AudioCodec::Pcm16, m2audio::AudioCodec::Pcm8};
    for (auto c : codecs) {
        codecBox->addItem(QString::fromUtf8(m2audio::codecName(c)));
    }
    codecBox->setCurrentIndex(0); // SDX2, the recommended default

    auto* rateBox = new QComboBox;
    for (uint32_t r : m2audio::standardSampleRates()) {
        rateBox->addItem(tr("%1 Hz").arg(r), r);
    }
    rateBox->setCurrentIndex(int(std::distance(
        m2audio::standardSampleRates().begin(),
        std::find(m2audio::standardSampleRates().begin(),
                   m2audio::standardSampleRates().end(), 22050u))));

    auto* modeBox = new QComboBox;
    modeBox->addItem(tr("Mono"), 1);
    modeBox->addItem(tr("Stereo"), 2);

    auto* containerBox = new QComboBox;
    containerBox->addItem(tr("AIFC (compressed)"), int(m2audio::AudioContainer::Aifc));
    containerBox->addItem(tr("AIFF (uncompressed only)"), int(m2audio::AudioContainer::Aiff));

    auto* summary = new QLabel;
    summary->setWordWrap(true);
    summary->setMinimumWidth(420);

    form->addRow(tr("Codec:"), codecBox);
    form->addRow(tr("Sample rate:"), rateBox);
    form->addRow(tr("Mode:"), modeBox);
    form->addRow(tr("Container:"), containerBox);
    form->addRow(summary);

    auto currentOptions = [&] {
        m2audio::EncodeOptions o;
        o.codec = codecs[size_t(codecBox->currentIndex())];
        o.sampleRate = rateBox->currentData().toUInt();
        o.channels = uint32_t(modeBox->currentData().toInt());
        o.container = m2audio::AudioContainer(containerBox->currentData().toInt());
        return o;
    };

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto refresh = [&] {
        m2audio::EncodeOptions o = currentOptions();
        QString problem = QString::fromStdString(m2audio::validateOptions(o));
        uint32_t bps = m2audio::bitrateBps(o);
        QString text = tr("%1\n\nBitrate: %2 kbit/s — %3 KB per second of audio.")
                            .arg(QString::fromUtf8(m2audio::codecDescription(o.codec)))
                            .arg(bps / 1000.0, 0, 'f', 1)
                            .arg(bps / 8.0 / 1024.0, 0, 'f', 1);
        if (!problem.isEmpty()) {
            text += tr("\n\n⚠ %1").arg(problem);
        }
        summary->setText(text);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
    };
    for (QComboBox* box : {codecBox, rateBox, modeBox, containerBox}) {
        connect(box, &QComboBox::currentIndexChanged, &dlg, [&refresh] { refresh(); });
    }
    refresh();

    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QString outDir = QFileDialog::getExistingDirectory(
        this, tr("Where should the converted files go?"),
        QFileInfo(inputs.front()).absolutePath());
    if (outDir.isEmpty()) {
        return;
    }

    const m2audio::EncodeOptions options = currentOptions();
    const char* ext = (options.container == m2audio::AudioContainer::Aifc) ? "aifc" : "aiff";
    int written = 0;
    QStringList errors;
    for (const QString& in : inputs) {
        try {
            // Both readers decode to interleaved 16-bit, so the encoder
            // sees the same shape whichever the source was.
            m2audio::AudioBuffer buf;
            if (in.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive)) {
                m2audio::Wav w = m2audio::Wav::loadFromFile(in.toStdWString());
                buf.samples = w.decodePcm16();
                buf.sampleRate = w.sampleRate();
                buf.channels = w.channels();
            } else {
                m2audio::Aiff a = m2audio::Aiff::loadFromFile(in.toStdWString());
                buf.samples = a.decodePcm16();
                buf.sampleRate = a.sampleRate();
                buf.channels = a.channels();
            }
            if (buf.samples.empty()) {
                errors << tr("%1: no decodable audio").arg(QFileInfo(in).fileName());
                continue;
            }
            std::vector<uint8_t> out = m2audio::encodeAudioFile(buf, options);
            QString outPath = QStringLiteral("%1/%2.%3")
                                   .arg(outDir, QFileInfo(in).completeBaseName(),
                                        QString::fromUtf8(ext));
            QFile f(outPath);
            if (f.open(QIODevice::WriteOnly) &&
                f.write(reinterpret_cast<const char*>(out.data()), qint64(out.size())) ==
                    qint64(out.size())) {
                ++written;
            } else {
                errors << tr("%1: write failed").arg(outPath);
            }
        } catch (const std::exception& e) {
            errors << QStringLiteral("%1: %2").arg(QFileInfo(in).fileName(),
                                                     QString::fromUtf8(e.what()));
        }
    }

    QString msg = tr("Converted %1 of %2 file(s) to %3 at %4 Hz %5.")
                       .arg(written)
                       .arg(inputs.size())
                       .arg(QString::fromUtf8(m2audio::codecName(options.codec)))
                       .arg(options.sampleRate)
                       .arg(options.channels == 1 ? tr("mono") : tr("stereo"));
    if (!errors.isEmpty()) {
        msg += QStringLiteral("\n\n") + errors.join(QLatin1Char('\n'));
    }
    QMessageBox::information(this, tr("Convert audio"), msg);
}

QHash<int, QString> MainWindow::loadAitdNameDatabase(const QString& pakPath) {
    QHash<int, QString> names;
    QFileInfo pakInfo(pakPath);
    QString pakName = pakInfo.fileName().toUpper();

    // AITD_PakEdit ships hand-curated databases (AITD1_CD_PAK_DB.json,
    // AITD1_floppy_PAK_DB.json, ...) keyed by archive name then entry
    // index. They are the community's accumulated identification work; we
    // read them if present but never bundle them.
    QStringList candidates;
    // A database the user pointed at explicitly wins.
    QString chosen = QSettings().value(QStringLiteral("aitd/nameDb")).toString();
    if (!chosen.isEmpty() && QFileInfo::exists(chosen)) {
        candidates << chosen;
    }
    // Then anything sitting with the game.
    QStringList searchDirs{pakInfo.absolutePath()};
    QDir parent(pakInfo.absolutePath());
    if (parent.cdUp()) {
        searchDirs << parent.absolutePath();
    }
    searchDirs << QCoreApplication::applicationDirPath();
    for (const QString& dirPath : searchDirs) {
        QDir dir(dirPath);
        for (const QString& dbName :
             dir.entryList({QStringLiteral("*PAK_DB.json")}, QDir::Files, QDir::Name)) {
            candidates << dir.filePath(dbName);
        }
    }
    // Finally the bundled databases, so names appear with no setup at all.
    // The AITD_PakEdit distribution keeps its databases with the tool
    // rather than with the game, so relying on discovery alone found
    // nothing in the common case.
    QDir builtIn(QStringLiteral(":/aitd/aitd"));
    for (const QString& dbName :
         builtIn.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        candidates << builtIn.filePath(dbName);
    }

    {
        for (const QString& dbPath : candidates) {
            QFile f(dbPath);
            if (!f.open(QIODevice::ReadOnly)) {
                continue;
            }
            QJsonParseError err{};
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                continue;
            }
            QJsonObject all = doc.object().value(QStringLiteral("all_PAKs")).toObject();
            // Keys are archive filenames; match case-insensitively because
            // the 3DO discs and the DOS releases disagree on casing.
            for (auto it = all.begin(); it != all.end(); ++it) {
                if (it.key().toUpper() != pakName) {
                    continue;
                }
                QJsonObject entries = it.value().toObject();
                for (auto e = entries.begin(); e != entries.end(); ++e) {
                    bool intOk = false;
                    int idx = e.key().toInt(&intOk);
                    QString info = e.value().toObject().value(QStringLiteral("info")).toString();
                    // "?" is the database's placeholder for "not identified
                    // yet" — showing it would be worse than showing nothing.
                    if (intOk && !info.isEmpty() && info != QStringLiteral("?")) {
                        names.insert(idx, info);
                    }
                }
            }
            if (!names.isEmpty()) {
                return names;
            }
        }
    }
    return names;
}

void MainWindow::showAitdRoomsFile(const QString& path) {
    stopAllPlayback();
    aitdPakPath_ = path;
    std::vector<m2model::AitdRoom> rooms;
    size_t entries = 0;
    try {
        m2model::AitdPak pak = m2model::AitdPak::openFromFile(path.toStdWString());
        entries = pak.entryCount();
        // Every room of the floor lives in entry 0, behind an offset table;
        // entry 1 is the camera data.
        if (entries > 0) {
            rooms = m2model::parseAitdRoomArchive(pak.read(0));
        }
    } catch (const std::exception& e) {
        selectorMode_ = SelectorMode::None;
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Failed to open room archive:\n%1").arg(e.what()));
        return;
    }

    size_t validRooms = 0, colliders = 0, triggers = 0;
    for (const auto& r : rooms) {
        if (!r.valid) {
            continue;
        }
        ++validRooms;
        colliders += r.colliders.size();
        triggers += r.triggers.size();
    }

    // The whole floor as one body, so the existing camera, render modes and
    // OBJ export all work on it unchanged.
    //
    // Colliders only by default: triggers are tall volumes that sit above
    // the floor, and from any overhead angle they hide the layout the view
    // exists to show. The frame selector switches them on.
    aitdBody_ = m2model::buildRoomBody(rooms, /*includeTriggers=*/false);
    if (!aitdBody_.valid) {
        selectorMode_ = SelectorMode::None;
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(
            tr("No room geometry found in:\n%1\n\n%2 entries, none parsed as a room.")
                .arg(path)
                .arg(entries));
        return;
    }

    selectorMode_ = SelectorMode::AitdRooms;
    aitdRooms_ = std::move(rooms);
    textureSelect_->blockSignals(true);
    textureSelect_->clear();
    textureSelect_->addItem(tr("Floor: colliders (%1 rooms)").arg(validRooms));
    textureSelect_->addItem(tr("Floor: colliders + triggers"));
    textureSelect_->setCurrentIndex(0);
    textureSelect_->blockSignals(false);

    lodSelect_->blockSignals(true);
    lodSelect_->clear();
    lodSelect_->addItem(tr("Solid (materials)"));
    lodSelect_->addItem(tr("Solid (flat)"));
    lodSelect_->addItem(tr("Wireframe"));
    lodSelect_->addItem(tr("Points"));
    lodSelect_->setCurrentIndex(2); // wireframe reads a floor plan best
    lodSelect_->blockSignals(false);

    modelView_->setBody(aitdBody_);
    modelView_->setRenderMode(m2model::AitdRenderMode::Wireframe);
    modelView_->setSpinning(false); // a floor plan should hold still
    viewStack_->setCurrentWidget(modelView_);

    infoView_->setPlainText(
        tr("File: %1\n\nAlone in the Dark floor / room geometry\n"
           "Rooms: %2 of %3 entries\nColliders: %4\nTriggers: %5\n"
           "Extent: X[%6,%7] Y[%8,%9] Z[%10,%11]\n\n"
           "AITD's visuals are the pre-rendered backdrops; what a room "
           "stores is its collision volumes. Grey is walkable floor, blue a "
           "link to another room, deep blue an interactive box, dark red a "
           "script trigger.\n\n"
           "Drag to orbit, right-drag to pan, wheel to zoom. File > Export "
           "writes the floor as .obj + .mtl.")
            .arg(path)
            .arg(validRooms)
            .arg(entries)
            .arg(colliders)
            .arg(triggers)
            .arg(aitdBody_.bbox[0]).arg(aitdBody_.bbox[1])
            .arg(aitdBody_.bbox[2]).arg(aitdBody_.bbox[3])
            .arg(aitdBody_.bbox[4]).arg(aitdBody_.bbox[5]));
}

void MainWindow::showSoundCatalogue(const QString& path) {
    stopAllPlayback();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        selectorMode_ = SelectorMode::None;
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Cannot open:\n%1").arg(path));
        return;
    }
    QByteArray blob = f.readAll();
    f.close();

    catalogueData_.assign(blob.constData(), blob.constData() + blob.size());
    catalogue_ = m2audio::scanAiffCatalogue(catalogueData_.data(), catalogueData_.size());
    if (catalogue_.empty()) {
        selectorMode_ = SelectorMode::None;
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("No sounds found in:\n%1").arg(path));
        return;
    }

    selectorMode_ = SelectorMode::SoundCatalogue;
    cataloguePath_ = path;
    textureSelect_->blockSignals(true);
    textureSelect_->clear();
    for (size_t i = 0; i < catalogue_.size(); ++i) {
        textureSelect_->addItem(tr("Sound %1 (%2)")
                                     .arg(i)
                                     .arg(QLocale().formattedDataSize(
                                         qint64(catalogue_[i].size), 1)));
    }
    textureSelect_->setCurrentIndex(0);
    textureSelect_->blockSignals(false);
    lodSelect_->clear();

    refreshTexturePreview();
}

void MainWindow::showAitdImageFile(const QString& path) {
    stopAllPlayback();
    auto pages = m2model::loadAitdImages(path.toStdWString());
    if (pages.empty()) {
        selectorMode_ = SelectorMode::None;
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Not a readable AITD backdrop:\n%1").arg(path));
        return;
    }

    animFrames_.clear();
    animFrames_.reserve(pages.size());
    for (const auto& p : pages) {
        QImage img(p.rgba.data(), int(p.width), int(p.height), int(p.width) * 4,
                    QImage::Format_RGBA8888);
        animFrames_.push_back(img.copy()); // detach from the decoder's buffer
    }

    // Reuse the frame selector: .pics files hold several backdrop pages,
    // .bob/.pad hold exactly one. They are stills, so no timer is started.
    selectorMode_ = SelectorMode::AnimFrames;
    textureSelect_->blockSignals(true);
    textureSelect_->clear();
    for (size_t i = 0; i < animFrames_.size(); ++i) {
        textureSelect_->addItem(tr("Page %1").arg(i));
    }
    textureSelect_->setCurrentIndex(0);
    textureSelect_->blockSignals(false);
    lodSelect_->clear();

    displayImage(animFrames_.front());
    infoView_->setPlainText(
        tr("File: %1\n\nAlone in the Dark pre-rendered backdrop\n"
           "Pages: %2\nSize: %3x%4, 256-colour palette\n\n"
           "Layout: u16 width, u16 height, 768-byte RGB palette, then one "
           "palette index per pixel. .pics files pad each page to 64 KiB; "
           ".bob is a single un-padded page.")
            .arg(path)
            .arg(animFrames_.size())
            .arg(pages.front().width)
            .arg(pages.front().height));
}

void MainWindow::aitdTick() {
    if (selectorMode_ != SelectorMode::AitdBodies || !aitdBody_.valid) {
        return;
    }
    modelView_->stepSpin(0.02); // idle spin; suppressed while the user drags
}

void MainWindow::animTick() {
    if (animFrames_.size() < 2) {
        animTimer_->stop();
        return;
    }
    animFrame_ = (animFrame_ + 1) % animFrames_.size();
    imageView_->updateImage(animFrames_[animFrame_]);
    viewStack_->setCurrentWidget(imageView_);
    // Keep the frame selector in step without re-triggering its handler.
    if (selectorMode_ == SelectorMode::AnimFrames && textureSelect_) {
        textureSelect_->blockSignals(true);
        textureSelect_->setCurrentIndex(int(animFrame_));
        textureSelect_->blockSignals(false);
    }
}

void MainWindow::cinepakTick() {
    try {
        cinepakTickImpl();
    } catch (const std::exception& e) {
        qWarning() << "cinepak playback error:" << e.what();
        stopAllPlayback();
    }
}

void MainWindow::cinepakTickImpl() {
    if (!stream_ || !cinepak_) {
        cinepakTimer_->stop();
        stopButton_->setEnabled(false);
        return;
    }
    if (cinepakFrame_ >= stream_->filmFrames().size()) {
        // All frames shown; keep ticking only until audio drains.
        bool audioDone = !audioSink_ ||
                          audioSink_->state() == QAudio::IdleState ||
                          audioSink_->state() == QAudio::StoppedState;
        if (audioDone) {
            cinepakTimer_->stop();
            stopButton_->setEnabled(false);
        }
        return;
    }
    const auto& info = stream_->film();
    const auto& frames = stream_->filmFrames();

    // Timestamp pacing: convert elapsed real time to stream ticks, then show
    // the newest frame whose DataStreamer timestamp is due. Cinepak is
    // inter-frame compressed, so every intermediate frame must still be
    // decoded even when catching up (dropped display, not dropped decode).
    double dueTicks = firstFrameTime_ + playbackClock_.elapsed() * streamHz_ / 1000.0;
    if (cinepakFrame_ > 0 && double(frames[cinepakFrame_].time) > dueTicks) {
        return; // next frame isn't due yet — do NOT advance (advancing one
                // frame per timer tick played everything at ~66 fps)
    }
    size_t displayUpTo = cinepakFrame_;
    while (displayUpTo + 1 < frames.size() && double(frames[displayUpTo + 1].time) <= dueTicks) {
        ++displayUpTo;
    }

    for (size_t i = cinepakFrame_; i <= displayUpTo && i < frames.size(); ++i) {
        const auto& frame = frames[i];
        cinepak_->decodeFrame(frame.data.data(), frame.data.size(), cinepakBuffer_.data(),
                               info.width, info.height);
    }
    QImage image(cinepakBuffer_.data(), int(info.width), int(info.height),
                  int(info.width) * 4, QImage::Format_RGBA8888);
    imageView_->updateImage(image.copy());
    viewStack_->setCurrentWidget(imageView_);
    cinepakFrame_ = displayUpTo + 1;

    // End only once both the last frame is shown and (if present) the audio
    // has drained, so video never finishes ahead of the sound.
    if (cinepakFrame_ >= frames.size()) {
        bool audioDone = !audioSink_ ||
                          audioSink_->state() == QAudio::IdleState ||
                          audioSink_->state() == QAudio::StoppedState;
        if (audioDone) {
            cinepakTimer_->stop();
            stopButton_->setEnabled(false);
        }
    }
}

QString MainWindow::remuxMpeg(const QString& videoEs, const QString& audioEs) {
    QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        ffmpeg = QStringLiteral("C:/ffmpeg/bin/ffmpeg.exe");
        if (!QFileInfo::exists(ffmpeg)) {
            return QString();
        }
    }
    QString out = QDir::tempPath() +
                   QStringLiteral("/m2suite_mux_%1.mpg").arg(QDateTime::currentMSecsSinceEpoch());
    QStringList args{QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error")};
    if (!videoEs.isEmpty()) {
        args << QStringLiteral("-i") << videoEs;
    }
    if (!audioEs.isEmpty()) {
        args << QStringLiteral("-i") << audioEs;
    }
    args << QStringLiteral("-c") << QStringLiteral("copy") << out;
    QProcess proc;
    proc.start(ffmpeg, args);
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0 || !QFileInfo::exists(out)) {
        return QString();
    }
    return out;
}

// --------------------------------------------------------------------------
// File handlers

void MainWindow::showStreamFile(const QString& path) {
    selectorMode_ = SelectorMode::None;
    textureSelect_->clear();
    lodSelect_->clear();
    try {
        stream_ = std::make_unique<m2stream::Stream>(
            m2stream::Stream::loadFromFile(path.toStdWString()));
    } catch (const std::exception& e) {
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Failed to open stream:\n%1").arg(e.what()));
        infoView_->setPlainText(tr("File: %1\n\nError: %2").arg(path).arg(e.what()));
        return;
    }

    QString info;
    info += QStringLiteral("File: %1\n\n").arg(path);
    info += QStringLiteral("3DO DataStreamer file\n");
    info += QStringLiteral("Subscribers: ");
    QStringList subs;
    for (uint32_t tag : stream_->subscribers()) {
        subs << fourccToString(tag);
    }
    info += subs.join(QStringLiteral(", ")) + QStringLiteral("\n\n");

    if (stream_->hasFilm()) {
        const auto& f = stream_->film();
        info += QStringLiteral("Cinepak film: %1x%2, %3 frames (%4 in stream)\n")
                     .arg(f.width)
                     .arg(f.height)
                     .arg(f.count)
                     .arg(stream_->filmFrames().size());
        playbackKind_ = PlaybackKind::Cinepak;
        playButton_->setEnabled(true);
        // Show the first frame as the still preview.
        m2stream::CinepakDecoder preview;
        std::vector<uint8_t> buf(size_t(f.width) * f.height * 4, 0);
        const auto& frame0 = stream_->filmFrames().front();
        preview.decodeFrame(frame0.data.data(), frame0.data.size(), buf.data(), f.width,
                             f.height);
        QImage image(buf.data(), int(f.width), int(f.height), int(f.width) * 4,
                      QImage::Format_RGBA8888);
        displayImage(image.copy());
    }
    if (stream_->hasAudio()) {
        const auto& a = stream_->audio();
        info += QStringLiteral("Audio: %1 Hz, %2 ch, %3-bit, compression %4\n")
                     .arg(a.sampleRate)
                     .arg(a.channels)
                     .arg(a.sampleSizeBits)
                     .arg(fourccToString(a.compression));
        if (!stream_->hasFilm()) {
            pcm_ = decodeSndsAudio(*stream_);
            if (!pcm_.empty()) {
                pcmRate_ = int(a.sampleRate);
                pcmChannels_ = int(a.channels);
                waveformView_->setSamples(pcm_, pcmChannels_, double(pcmRate_));
                viewStack_->setCurrentWidget(waveformView_);
                playbackKind_ = PlaybackKind::Pcm;
                playButton_->setEnabled(true);
            } else {
                info += QStringLiteral("  (audio compression '%1' not yet decodable)\n")
                             .arg(fourccToString(a.compression));
            }
        }
    }
    if (stream_->hasMpegVideo() || stream_->hasMpegAudio()) {
        if (stream_->hasMpegVideo()) {
            info += QStringLiteral("MPEG video stream: %1 KB elementary stream\n")
                         .arg(stream_->mpegVideo().size() / 1024);
        }
        if (stream_->hasMpegAudio()) {
            info += QStringLiteral("MPEG audio stream: %1 KB\n")
                         .arg(stream_->mpegAudio().size() / 1024);
        }

        // Write the elementary streams to temp files, then remux into a
        // proper MPEG-PS container via system ffmpeg — QMediaPlayer's
        // probing is unreliable on raw ES, and the mux also syncs audio
        // with video. Falls back to the raw video ES if ffmpeg is missing.
        auto writeTemp = [](const std::vector<uint8_t>& data, const QString& suffix) {
            auto f = std::make_unique<QTemporaryFile>(QDir::tempPath() +
                                                        QStringLiteral("/m2suite_XXXXXX") + suffix);
            if (!f->open()) return std::unique_ptr<QTemporaryFile>();
            f->write(reinterpret_cast<const char*>(data.data()), qint64(data.size()));
            f->flush();
            return f;
        };
        std::unique_ptr<QTemporaryFile> videoTemp, audioTemp;
        if (stream_->hasMpegVideo()) {
            videoTemp = writeTemp(stream_->mpegVideo(), QStringLiteral(".m1v"));
        }
        if (stream_->hasMpegAudio()) {
            audioTemp = writeTemp(stream_->mpegAudio(), QStringLiteral(".mp2"));
        }

        QString source = remuxMpeg(videoTemp ? videoTemp->fileName() : QString(),
                                    audioTemp ? audioTemp->fileName() : QString());
        if (source.isEmpty() && videoTemp) {
            source = videoTemp->fileName(); // fallback: raw ES
            mpegTemp_ = std::move(videoTemp); // keep alive while playing
            info += QStringLiteral("(ffmpeg not found — playing raw video stream, no audio)\n");
        } else if (source.isEmpty() && audioTemp) {
            source = audioTemp->fileName();
            mpegTemp_ = std::move(audioTemp);
        }

        if (!source.isEmpty()) {
            mediaPlayer_ = new QMediaPlayer(this);
            mediaAudio_ = new QAudioOutput(this);
            mediaPlayer_->setAudioOutput(mediaAudio_);
            mediaPlayer_->setVideoOutput(videoWidget_);
            mediaPlayer_->setSource(QUrl::fromLocalFile(source));
            connect(mediaPlayer_, &QMediaPlayer::errorOccurred, this,
                     [this](QMediaPlayer::Error, const QString& message) {
                         infoView_->appendPlainText(tr("\nPlayback error: %1").arg(message));
                     });
            if (stream_->hasMpegVideo()) {
                viewStack_->setCurrentWidget(videoWidget_);
            } else {
                viewStack_->setCurrentWidget(placeholder_);
                placeholder_->setText(tr("MPEG audio — press Play."));
            }
            playbackKind_ = PlaybackKind::Mpeg;
            playButton_->setEnabled(true);
        }
    }

    if (playbackKind_ == PlaybackKind::None) {
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Stream recognized but no playable track found."));
    }
    infoView_->setPlainText(info);
}

void MainWindow::showTextureFile(const QString& path) {
    selectorMode_ = SelectorMode::Textures;
    try {
        textures_ = m2texture::Texture::loadAllFromFile(path.toStdWString());
    } catch (const std::exception& e) {
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Failed to open texture:\n%1").arg(e.what()));
        infoView_->setPlainText(tr("File: %1\n\nError: %2").arg(path).arg(e.what()));
        return;
    }

    textureSelect_->blockSignals(true);
    textureSelect_->clear();
    for (size_t i = 0; i < textures_.size(); ++i) {
        const auto& tex = textures_[i];
        textureSelect_->addItem(QStringLiteral("#%1  (%2x%3)")
                                     .arg(i)
                                     .arg(tex.lodWidth(0))
                                     .arg(tex.lodHeight(0)));
    }
    textureSelect_->setCurrentIndex(0);
    textureSelect_->blockSignals(false);

    lodSelect_->blockSignals(true);
    lodSelect_->clear();
    lodSelect_->blockSignals(false);

    infoView_->setPlainText(tr("File: %1").arg(path));
    refreshTexturePreview();
}

void MainWindow::refreshTexturePreview() {
    if (selectorMode_ == SelectorMode::AnimFrames) {
        int frame = textureSelect_->currentIndex();
        if (frame >= 0 && size_t(frame) < animFrames_.size()) {
            displayImage(animFrames_[size_t(frame)]);
        }
        return;
    }
    if (selectorMode_ == SelectorMode::SoundCatalogue) {
        int idx = textureSelect_->currentIndex();
        if (idx < 0 || size_t(idx) >= catalogue_.size()) {
            return;
        }
        const auto& e = catalogue_[size_t(idx)];
        try {
            m2audio::Aiff a =
                m2audio::Aiff::load(catalogueData_.data() + e.offset, size_t(e.size));
            pcm_ = a.decodePcm16();
            pcmRate_ = int(a.sampleRate());
            pcmChannels_ = int(a.channels());
            waveformView_->setSamples(pcm_, pcmChannels_, double(pcmRate_));
            viewStack_->setCurrentWidget(waveformView_);
            playButton_->setEnabled(!pcm_.empty());
            stopButton_->setEnabled(false);
            playbackKind_ = PlaybackKind::Pcm;

            double seconds = pcmRate_ > 0 && pcmChannels_ > 0
                                  ? double(pcm_.size()) / pcmChannels_ / pcmRate_
                                  : 0.0;
            char tag[5] = {};
            uint32_t fourcc = a.compressionFourcc();
            for (int k = 0; k < 4; ++k) {
                tag[k] = char((fourcc >> (24 - k * 8)) & 0xFF);
            }
            infoView_->setPlainText(
                tr("File: %1\n\n3DO sound catalogue — %2 sounds\n\n"
                   "Sound %3 of %2\nOffset: %4\nSize: %5 bytes\n"
                   "Format: %6 Hz, %7, %8\nDuration: %9 s\n\n"
                   "Each sound is a complete FORM/AIFF file, aligned to a "
                   "2048-byte CD sector. Press Play to hear the selected "
                   "one; Export Selected writes them all as WAV.")
                    .arg(cataloguePath_)
                    .arg(catalogue_.size())
                    .arg(idx)
                    .arg(e.offset)
                    .arg(e.size)
                    .arg(pcmRate_)
                    .arg(pcmChannels_ == 1 ? tr("mono") : tr("stereo"))
                    .arg(QString::fromLatin1(tag, 4).trimmed())
                    .arg(seconds, 0, 'f', 2));
        } catch (const std::exception& ex) {
            viewStack_->setCurrentWidget(placeholder_);
            placeholder_->setText(tr("Sound %1 could not be decoded:\n%2")
                                       .arg(idx)
                                       .arg(QString::fromUtf8(ex.what())));
        }
        return;
    }
    if (selectorMode_ == SelectorMode::AitdRooms) {
        bool withTriggers = textureSelect_->currentIndex() == 1;
        aitdBody_ = m2model::buildRoomBody(aitdRooms_, withTriggers);
        auto mode = m2model::AitdRenderMode::Wireframe;
        switch (lodSelect_->currentIndex()) {
        case 0: mode = m2model::AitdRenderMode::SolidMaterials; break;
        case 1: mode = m2model::AitdRenderMode::SolidFlat; break;
        case 3: mode = m2model::AitdRenderMode::Points; break;
        default: break;
        }
        modelView_->setBody(aitdBody_);
        modelView_->setRenderMode(mode);
        viewStack_->setCurrentWidget(modelView_);
        return;
    }
    if (selectorMode_ == SelectorMode::AitdBodies) {
        int idx = textureSelect_->currentIndex();
        if (aitdPak_ && idx >= 0 && size_t(idx) < aitdPak_->entryCount()) {
            aitdBody_ = m2model::parseAitdBody(aitdPak_->read(size_t(idx)));
            auto mode = m2model::AitdRenderMode::SolidMaterials;
            switch (lodSelect_->currentIndex()) {
            case 1: mode = m2model::AitdRenderMode::SolidFlat; break;
            case 2: mode = m2model::AitdRenderMode::Wireframe; break;
            case 3: mode = m2model::AitdRenderMode::Points; break;
            default: break;
            }
            modelView_->setBody(aitdBody_);
            modelView_->setRenderMode(mode);
            viewStack_->setCurrentWidget(modelView_);

            size_t textured = 0;
            for (const auto& p : aitdBody_.primitives) {
                if (p.type >= m2model::AitdPrimitive::PolyTex8) {
                    ++textured;
                }
            }
            QString name = aitdNames_.value(idx);
            infoView_->setPlainText(
                tr("File: %1\n\nAlone in the Dark 3D model (PAK entry %2 of %3)%4\n"
                   "Vertices: %5\nPrimitives: %6 (%7 textured)\nBones/groups: %8\n"
                   "Bounding box: X[%9,%10] Y[%11,%12] Z[%13,%14]\n\n"
                   "Drag to orbit, right-drag to pan, wheel to zoom, "
                   "double-click to reset. Use the render-mode box for "
                   "materials / flat / wireframe / points, and File > Export "
                   "to write an .obj + .mtl.")
                    .arg(aitdPakPath_)
                    .arg(idx)
                    .arg(aitdPak_->entryCount())
                    .arg(name.isEmpty() ? QString() : QStringLiteral("\nName: %1").arg(name))
                    .arg(aitdBody_.vertexCount())
                    .arg(aitdBody_.primitives.size())
                    .arg(textured)
                    .arg(aitdBody_.groups.size())
                    .arg(aitdBody_.bbox[0]).arg(aitdBody_.bbox[1])
                    .arg(aitdBody_.bbox[2]).arg(aitdBody_.bbox[3])
                    .arg(aitdBody_.bbox[4]).arg(aitdBody_.bbox[5]));
        }
        return;
    }
    if (selectorMode_ != SelectorMode::Textures) {
        return;
    }
    int texIndex = textureSelect_->currentIndex();
    if (texIndex < 0 || size_t(texIndex) >= textures_.size()) {
        return;
    }
    const auto& tex = textures_[size_t(texIndex)];

    if (lodSelect_->count() != int(tex.lods().size())) {
        lodSelect_->blockSignals(true);
        lodSelect_->clear();
        for (size_t i = 0; i < tex.lods().size(); ++i) {
            lodSelect_->addItem(QStringLiteral("LOD %1 (%2x%3)")
                                     .arg(i)
                                     .arg(tex.lodWidth(i))
                                     .arg(tex.lodHeight(i)));
        }
        lodSelect_->setCurrentIndex(0);
        lodSelect_->blockSignals(false);
    }
    int lodIndex = lodSelect_->currentIndex() < 0 ? 0 : lodSelect_->currentIndex();

    const auto& h = tex.header();
    QString info;
    info += QStringLiteral("Textures in file: %1 (showing #%2)\n").arg(textures_.size()).arg(texIndex);
    info += QStringLiteral("Coarsest LOD: %1x%2\n").arg(h.minXSize).arg(h.minYSize);
    info += QStringLiteral("NumLOD: %1\n").arg(h.numLOD);
    info += QStringLiteral("TexFormat: 0x%1\n").arg(h.texFormat, 4, 16, QChar('0'));
    info += QStringLiteral("  color depth: %1 bits\n").arg(m2texture::TexelFlags::colorDepth(h.texFormat));
    info += QStringLiteral("  alpha depth: %1 bits\n").arg(m2texture::TexelFlags::alphaDepth(h.texFormat));
    info += QStringLiteral("  literal RGB: %1\n")
                 .arg(m2texture::TexelFlags::isLiteral(h.texFormat) ? "yes" : "no (PIP-indexed)");
    info += QStringLiteral("HasPIP: %1 (%2 colors)\n")
                 .arg(tex.hasPip() ? "yes" : "no")
                 .arg(tex.pip().colors.size());
    info += QStringLiteral("IsCompressed: %1\n").arg(tex.isCompressed() ? "yes" : "no");
    info += QStringLiteral("LODs: %1\n").arg(tex.lods().size());

    try {
        uint32_t w = tex.lodWidth(size_t(lodIndex));
        uint32_t hgt = tex.lodHeight(size_t(lodIndex));
        auto rgba = tex.decodeLodToRgba(size_t(lodIndex));
        displayImage(rgbaToImage(rgba, w, hgt));
        info += QStringLiteral("\nLOD %1 decoded (%2x%3).\n").arg(lodIndex).arg(w).arg(hgt);
    } catch (const std::exception& e) {
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Preview not available:\n%1").arg(e.what()));
        info += QStringLiteral("\nLOD %1 decode failed: %2\n").arg(lodIndex).arg(e.what());
    }

    infoView_->setPlainText(infoView_->toPlainText().split('\n').first() + "\n\n" + info);
}

void MainWindow::showAnimFile(const QString& path) {
    try {
        m2cel::Anim anim = m2cel::Anim::loadFromFile(path.toStdWString());
        animFrames_.clear();
        animFrames_.reserve(anim.frames().size());
        for (size_t i = 0; i < anim.frames().size(); ++i) {
            const auto& ccb = anim.frames()[i].ccb;
            animFrames_.push_back(rgbaToImage(anim.decodeFrame(i), ccb.width, ccb.height));
        }

        selectorMode_ = SelectorMode::AnimFrames;
        textureSelect_->blockSignals(true);
        textureSelect_->clear();
        for (size_t i = 0; i < animFrames_.size(); ++i) {
            textureSelect_->addItem(tr("Frame %1").arg(i));
        }
        textureSelect_->setCurrentIndex(0);
        textureSelect_->blockSignals(false);
        lodSelect_->blockSignals(true);
        lodSelect_->clear();
        lodSelect_->blockSignals(false);

        if (!animFrames_.empty()) {
            displayImage(animFrames_[0]);
        }

        // Cel animations play automatically and loop, so multi-frame files
        // are immediately recognisable as animations instead of looking
        // like a still. frameRate is in 60ths of a second; files without a
        // usable rate (e.g. bare cel chains, which carry no ANIM header)
        // fall back to ~12 fps.
        if (animFrames_.size() > 1) {
            animFrame_ = 0;
            int ms = (anim.frameRate > 0) ? int(anim.frameRate * 1000 / 60) : 83;
            animTimer_->start(std::clamp(ms, 33, 1000));
            playbackKind_ = PlaybackKind::CelAnim;
            playButton_->setEnabled(true);
            stopButton_->setEnabled(true);
        }

        QString info;
        info += QStringLiteral("File: %1\n\n").arg(path);
        info += QStringLiteral("3DO ANIM cel animation\n");
        info += QStringLiteral("Frames: %1 (header says %2)\n")
                     .arg(animFrames_.size())
                     .arg(anim.numFrames);
        info += QStringLiteral("Type: %1\n")
                     .arg(anim.animType == 1 ? QStringLiteral("single-CCB")
                                              : QStringLiteral("multi-CCB"));
        info += QStringLiteral("Frame rate: %1/60 s per frame\n").arg(anim.frameRate);
        if (!anim.frames().empty()) {
            const auto& ccb = anim.frames()[0].ccb;
            info += QStringLiteral("Frame size: %1x%2, %3 bpp\n")
                         .arg(ccb.width)
                         .arg(ccb.height)
                         .arg(ccb.bitsPerPixel());
        }
        info += QStringLiteral("\n%1\n")
                     .arg(animFrames_.size() > 1
                               ? QStringLiteral("Playing (looped) — Stop to pause, or use "
                                                 "the Frame selector to step frames.")
                               : QStringLiteral("Use the Frame selector to step frames."));
        infoView_->setPlainText(info);
    } catch (const std::exception& e) {
        selectorMode_ = SelectorMode::None;
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Failed to open ANIM:\n%1").arg(e.what()));
        infoView_->setPlainText(tr("File: %1\n\nError: %2").arg(path).arg(e.what()));
    }
}

void MainWindow::showImagFile(const QString& path) {
    selectorMode_ = SelectorMode::None;
    try {
        m2cel::Imag img = m2cel::Imag::loadFromFile(path.toStdWString());
        auto rgba = img.decodeToRgba();
        textureSelect_->clear();
        lodSelect_->clear();
        displayImage(rgbaToImage(rgba, img.width, img.height));

        QString info;
        info += QStringLiteral("File: %1\n\n").arg(path);
        info += QStringLiteral("3DO IMAG image\n");
        info += QStringLiteral("Size: %1x%2\n").arg(img.width).arg(img.height);
        info += QStringLiteral("Bits/pixel: %1\n").arg(img.bitsPerPixel);
        info += QStringLiteral("Pixel order: %1\n")
                     .arg(img.pixelOrder == 0 ? QStringLiteral("linear")
                                               : QStringLiteral("LRForm (M1 VRAM)"));
        infoView_->setPlainText(info);
    } catch (const std::exception& e) {
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Failed to open IMAG:\n%1").arg(e.what()));
        infoView_->setPlainText(tr("File: %1\n\nError: %2").arg(path).arg(e.what()));
    }
}

void MainWindow::showStandardImageFile(const QString& path) {
    selectorMode_ = SelectorMode::None;
    QImage image(path);
    textureSelect_->clear();
    lodSelect_->clear();
    if (image.isNull()) {
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Qt could not load this image\n(missing image plugin?)"));
        infoView_->setPlainText(tr("File: %1\n\nQt image load failed.").arg(path));
        return;
    }
    displayImage(image);
    infoView_->setPlainText(tr("File: %1\n\nStandard image\nSize: %2x%3\n")
                                 .arg(path)
                                 .arg(image.width())
                                 .arg(image.height()));
}

void MainWindow::showAudioFile(const QString& path, FileType type) {
    selectorMode_ = SelectorMode::None;
    try {
        m2audio::Aiff aiff = m2audio::Aiff::loadFromFile(path.toStdWString());
        QString info;
        info += QStringLiteral("File: %1\n\n").arg(path);
        info += QStringLiteral("%1\n").arg(fileTypeLabel(type));
        info += QStringLiteral("Channels: %1\n").arg(aiff.channels());
        info += QStringLiteral("Sample rate: %1 Hz\n").arg(aiff.sampleRate());
        info += QStringLiteral("Bits/sample: %1\n").arg(aiff.bitsPerSample());
        info += QStringLiteral("Frames: %1\n").arg(aiff.sampleFrames());
        if (aiff.isAifc()) {
            info += QStringLiteral("AIFC compression: %1\n")
                         .arg(QString::fromStdString(aiff.compressionType()));
        }

        textureSelect_->clear();
        lodSelect_->clear();
        try {
            pcm_ = aiff.decodePcm16();
            pcmRate_ = int(aiff.sampleRate());
            pcmChannels_ = int(aiff.channels());
            waveformView_->setSamples(pcm_, pcmChannels_, double(pcmRate_));
            viewStack_->setCurrentWidget(waveformView_);
            playbackKind_ = PlaybackKind::Pcm;
            playButton_->setEnabled(true);
        } catch (const std::exception& e) {
            viewStack_->setCurrentWidget(placeholder_);
            placeholder_->setText(tr("Waveform not available:\n%1").arg(e.what()));
            info += QStringLiteral("\nPCM decode failed: %1\n").arg(e.what());
        }
        infoView_->setPlainText(info);
    } catch (const std::exception& e) {
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Failed to open audio:\n%1").arg(e.what()));
        infoView_->setPlainText(tr("File: %1\n\nError: %2").arg(path).arg(e.what()));
    }
}

void MainWindow::showWavFile(const QString& path) {
    selectorMode_ = SelectorMode::None;
    try {
        m2audio::Wav wav = m2audio::Wav::loadFromFile(path.toStdWString());
        textureSelect_->clear();
        lodSelect_->clear();
        pcm_ = wav.decodePcm16();
        pcmRate_ = int(wav.sampleRate());
        pcmChannels_ = int(wav.channels());
        waveformView_->setSamples(pcm_, pcmChannels_, double(pcmRate_));
        viewStack_->setCurrentWidget(waveformView_);
        playbackKind_ = PlaybackKind::Pcm;
        playButton_->setEnabled(true);
        infoView_->setPlainText(tr("File: %1\n\nWAV audio\nChannels: %2\nSample rate: %3 Hz\n"
                                    "Bits/sample: %4\n")
                                     .arg(path)
                                     .arg(wav.channels())
                                     .arg(wav.sampleRate())
                                     .arg(wav.bitsPerSample()));
    } catch (const std::exception& e) {
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Failed to open WAV:\n%1").arg(e.what()));
        infoView_->setPlainText(tr("File: %1\n\nError: %2").arg(path).arg(e.what()));
    }
}

void MainWindow::showCelFile(const QString& path) {
    selectorMode_ = SelectorMode::None;
    try {
        m2cel::Cel cel = m2cel::Cel::loadFromFile(path.toStdWString());
        auto rgba = cel.decodeToRgba();
        uint32_t w = cel.ccb().width;
        uint32_t h = cel.ccb().height;
        textureSelect_->clear();
        lodSelect_->clear();
        displayImage(rgbaToImage(rgba, w, h));

        QString info;
        info += QStringLiteral("File: %1\n\n").arg(path);
        info += QStringLiteral("3DO Cel image\n");
        info += QStringLiteral("Size: %1x%2\n").arg(w).arg(h);
        info += QStringLiteral("Bits/pixel: %1\n").arg(cel.ccb().bitsPerPixel());
        info += QStringLiteral("Coded (PLUT): %1\n").arg(cel.ccb().isUncoded() ? "no" : "yes");
        info += QStringLiteral("Packed (RLE): %1\n").arg(cel.ccb().isPacked() ? "yes" : "no");
        info += QStringLiteral("PLUT entries: %1\n").arg(cel.plut().size());
        infoView_->setPlainText(info);
    } catch (const std::exception& e) {
        viewStack_->setCurrentWidget(placeholder_);
        placeholder_->setText(tr("Failed to open cel:\n%1").arg(e.what()));
        infoView_->setPlainText(tr("File: %1\n\nError: %2").arg(path).arg(e.what()));
    }
}

void MainWindow::showInfoOnly(const QString& path, FileType type) {
    selectorMode_ = SelectorMode::None;
    textureSelect_->clear();
    lodSelect_->clear();
    viewStack_->setCurrentWidget(placeholder_);
    placeholder_->setText(tr("%1\n\nNo viewer for this format yet.").arg(fileTypeLabel(type)));
    QFileInfo fi(path);
    infoView_->setPlainText(tr("File: %1\nType: %2\nSize: %3 bytes\n")
                                 .arg(path)
                                 .arg(fileTypeLabel(type))
                                 .arg(fi.size()));
}

void MainWindow::displayImage(const QImage& image) {
    imageView_->setImage(image);
    viewStack_->setCurrentWidget(imageView_);
}

// --------------------------------------------------------------------------
// Export

void MainWindow::exportSelected() {
    QList<QTreeWidgetItem*> selected = fileTree_->selectedItems();
    struct Entry {
        QString path;
        FileType type;
    };
    std::vector<Entry> files;
    for (QTreeWidgetItem* item : selected) {
        QVariant typeVar = item->data(0, kTypeRole);
        if (!typeVar.isValid() || item->isHidden()) {
            continue;
        }
        FileType type = FileType(typeVar.toInt());
        if (!fileTypeHasPreview(type)) {
            continue;
        }
        if (type == FileType::StandardImage || type == FileType::Wav) {
            continue; // already standard formats
        }
        files.push_back({item->data(0, kPathRole).toString(), type});
    }
    if (files.empty()) {
        QMessageBox::information(this, tr("Export Selected"),
                                  tr("Select one or more convertible files in the browser "
                                     "first (textures, cels, anims, audio, streams)."));
        return;
    }

    // Start in the remembered export directory so repeat exports go to the
    // same place without re-navigating; the chosen folder becomes the new
    // default.
    QSettings settings;
    QString defaultDir = settings.value(QStringLiteral("exportDir")).toString();
    QString outDir =
        QFileDialog::getExistingDirectory(this, tr("Choose Output Folder"), defaultDir);
    if (outDir.isEmpty()) {
        return;
    }
    settings.setValue(QStringLiteral("exportDir"), outDir);

    qInfo() << "exporting" << files.size() << "file(s) to" << outDir;
    int written = 0;
    QStringList errors;
    for (const Entry& entry : files) {
        written += exportOneFile(entry.path, entry.type, outDir, errors);
    }
    qInfo() << "export finished:" << written << "written," << errors.size() << "failed";

    QString summary = tr("%1 output file(s) written to\n%2").arg(written).arg(outDir);
    if (!errors.isEmpty()) {
        summary += tr("\n\n%1 failure(s):\n").arg(errors.size()) + errors.join('\n');
    }
    QMessageBox box(QMessageBox::Information, tr("Export Selected"), summary,
                     QMessageBox::Ok, this);
    QPushButton* openBtn =
        box.addButton(tr("Open Destination Folder"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == openBtn) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                 {QDir::toNativeSeparators(outDir)});
    }
}

int MainWindow::exportOneFile(const QString& path, FileType type, const QString& outDir,
                                QStringList& errors) {
    QFileInfo fi(path);
    QString base = fi.completeBaseName().isEmpty() ? fi.fileName() : fi.completeBaseName();
    int written = 0;
    auto writeBlob = [&](const QString& out, const uint8_t* data, size_t n) {
        QFile f(out);
        if (f.open(QIODevice::WriteOnly) &&
            f.write(reinterpret_cast<const char*>(data), qint64(n)) == qint64(n)) {
            ++written;
        } else {
            errors << tr("%1: write failed").arg(out);
        }
    };
    try {
        switch (type) {
            case FileType::UtfTexture: {
                auto texs = m2texture::Texture::loadAllFromFile(path.toStdWString());
                for (size_t i = 0; i < texs.size(); ++i) {
                    const auto& tex = texs[i];
                    uint32_t w = tex.lodWidth(0);
                    uint32_t h = tex.lodHeight(0);
                    auto rgba = tex.decodeLodToRgba(0);
                    QString out = (texs.size() == 1)
                                       ? QStringLiteral("%1/%2.png").arg(outDir, base)
                                       : QStringLiteral("%1/%2_tex%3.png").arg(outDir, base).arg(i);
                    if (rgbaToImage(rgba, w, h).save(out, "PNG")) {
                        ++written;
                    } else {
                        errors << tr("%1: PNG write failed").arg(out);
                    }
                }
                break;
            }
            case FileType::Cel: {
                m2cel::Cel cel = m2cel::Cel::loadFromFile(path.toStdWString());
                auto rgba = cel.decodeToRgba();
                QString out = QStringLiteral("%1/%2.png").arg(outDir, base);
                if (rgbaToImage(rgba, cel.ccb().width, cel.ccb().height).save(out, "PNG")) {
                    ++written;
                } else {
                    errors << tr("%1: PNG write failed").arg(out);
                }
                break;
            }
            case FileType::Anim: {
                m2cel::Anim anim = m2cel::Anim::loadFromFile(path.toStdWString());
                for (size_t i = 0; i < anim.frames().size(); ++i) {
                    const auto& ccb = anim.frames()[i].ccb;
                    QString out = QStringLiteral("%1/%2_f%3.png").arg(outDir, base).arg(i);
                    if (rgbaToImage(anim.decodeFrame(i), ccb.width, ccb.height).save(out, "PNG")) {
                        ++written;
                    } else {
                        errors << tr("%1: PNG write failed").arg(out);
                    }
                }
                break;
            }
            case FileType::Imag: {
                m2cel::Imag img = m2cel::Imag::loadFromFile(path.toStdWString());
                QString out = QStringLiteral("%1/%2.png").arg(outDir, base);
                if (rgbaToImage(img.decodeToRgba(), img.width, img.height).save(out, "PNG")) {
                    ++written;
                } else {
                    errors << tr("%1: PNG write failed").arg(out);
                }
                break;
            }
            case FileType::Aiff:
            case FileType::Aifc: {
                m2audio::Aiff aiff = m2audio::Aiff::loadFromFile(path.toStdWString());
                auto wav = aiff.toWavBytes();
                writeBlob(QStringLiteral("%1/%2.wav").arg(outDir, base), wav.data(), wav.size());
                break;
            }
            case FileType::StreamFile: {
                m2stream::Stream s = m2stream::Stream::loadFromFile(path.toStdWString());
                if (s.hasMpegVideo()) {
                    writeBlob(QStringLiteral("%1/%2.m1v").arg(outDir, base),
                               s.mpegVideo().data(), s.mpegVideo().size());
                }
                if (s.hasMpegAudio()) {
                    writeBlob(QStringLiteral("%1/%2.mp2").arg(outDir, base),
                               s.mpegAudio().data(), s.mpegAudio().size());
                }
                QByteArray wav = makeWav(decodeSndsAudio(s), uint16_t(s.audio().channels),
                                          s.audio().sampleRate);
                if (!wav.isEmpty()) {
                    writeBlob(QStringLiteral("%1/%2.wav").arg(outDir, base),
                               reinterpret_cast<const uint8_t*>(wav.constData()),
                               size_t(wav.size()));
                }
                if (s.hasFilm()) {
                    // Prefer a single .mp4 (H.264 + AAC, audio muxed in)
                    // written through the system ffmpeg via a rawvideo
                    // pipe; fall back to numbered PNG frames without it.
                    const auto& info = s.film();
                    const auto& frames = s.filmFrames();
                    QString ffmpeg = findFfmpegPath();
                    bool mp4Done = false;
                    if (!ffmpeg.isEmpty() && frames.size() > 1) {
                        double fps = 15.0;
                        uint32_t tickSpan = frames.back().time - frames.front().time;
                        if (tickSpan > 0) {
                            fps = (frames.size() - 1) * 240.0 / tickSpan;
                        }
                        std::unique_ptr<QTemporaryFile> wavTemp;
                        if (!wav.isEmpty()) {
                            wavTemp = std::make_unique<QTemporaryFile>(
                                QDir::tempPath() + QStringLiteral("/m2suite_XXXXXX.wav"));
                            if (wavTemp->open()) {
                                wavTemp->write(wav);
                                wavTemp->flush();
                            }
                        }
                        QString mp4Path = QStringLiteral("%1/%2.mp4").arg(outDir, base);
                        QStringList args{QStringLiteral("-y"), QStringLiteral("-v"),
                                          QStringLiteral("error"), QStringLiteral("-f"),
                                          QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"),
                                          QStringLiteral("rgba"), QStringLiteral("-s"),
                                          QStringLiteral("%1x%2").arg(info.width).arg(info.height),
                                          QStringLiteral("-r"), QString::number(fps, 'f', 3),
                                          QStringLiteral("-i"), QStringLiteral("pipe:0")};
                        if (wavTemp) {
                            args << QStringLiteral("-i") << wavTemp->fileName();
                        }
                        // Pad to even dimensions: yuv420p can't encode odd
                        // sizes (182x137 films exist).
                        args << QStringLiteral("-vf")
                             << QStringLiteral("pad=ceil(iw/2)*2:ceil(ih/2)*2")
                             << QStringLiteral("-c:v") << QStringLiteral("libx264")
                             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
                        if (wavTemp) {
                            args << QStringLiteral("-c:a") << QStringLiteral("aac")
                                 << QStringLiteral("-shortest");
                        }
                        args << mp4Path;

                        QProcess proc;
                        proc.start(ffmpeg, args);
                        if (proc.waitForStarted(10000)) {
                            m2stream::CinepakDecoder dec;
                            std::vector<uint8_t> buf(size_t(info.width) * info.height * 4, 0);
                            for (const auto& fr : frames) {
                                dec.decodeFrame(fr.data.data(), fr.data.size(), buf.data(),
                                                 info.width, info.height);
                                proc.write(reinterpret_cast<const char*>(buf.data()),
                                            qint64(buf.size()));
                                if (!proc.waitForBytesWritten(30000)) {
                                    break;
                                }
                            }
                            proc.closeWriteChannel();
                            if (proc.waitForFinished(300000) && proc.exitCode() == 0 &&
                                QFileInfo::exists(mp4Path)) {
                                ++written;
                                mp4Done = true;
                            } else {
                                errors << tr("%1: ffmpeg mp4 encode failed (%2)")
                                              .arg(mp4Path,
                                                   QString::fromLocal8Bit(
                                                       proc.readAllStandardError()));
                            }
                        }
                    }
                    if (!mp4Done) {
                        // Export film frames as numbered PNGs.
                        m2stream::CinepakDecoder dec;
                        std::vector<uint8_t> buf(size_t(info.width) * info.height * 4, 0);
                        for (size_t i = 0; i < frames.size(); ++i) {
                            const auto& fr = frames[i];
                            dec.decodeFrame(fr.data.data(), fr.data.size(), buf.data(),
                                             info.width, info.height);
                            QImage image(buf.data(), int(info.width), int(info.height),
                                          int(info.width) * 4, QImage::Format_RGBA8888);
                            QString out = QStringLiteral("%1/%2_f%3.png")
                                               .arg(outDir, base)
                                               .arg(i, 4, 10, QChar('0'));
                            if (image.save(out, "PNG")) {
                                ++written;
                            } else {
                                errors << tr("%1: PNG write failed").arg(out);
                                break;
                            }
                        }
                    }
                }
                break;
            }
            case FileType::Elf: {
                m2disasm::Elf elf = m2disasm::Elf::loadFromFile(path.toStdWString());
                std::string listing = elf.disassembleAll(0); // full listing
                listing += "\n";
                listing += elf.extractStrings();
                writeBlob(QStringLiteral("%1/%2.s").arg(outDir, base),
                           reinterpret_cast<const uint8_t*>(listing.data()), listing.size());
                // Also emit the ANSI-C pseudocode reconstruction.
                std::string code = m2disasm::Pseudocode(elf).liftAll(0);
                writeBlob(QStringLiteral("%1/%2.c").arg(outDir, base),
                           reinterpret_cast<const uint8_t*>(code.data()), code.size());
                break;
            }
            case FileType::AitdPak: {
                // One Wavefront .obj (+ shared .mtl) per model in the PAK.
                // Entries that aren't models are skipped silently — every
                // body archive contains a few non-geometry entries.
                m2model::AitdPak pak = m2model::AitdPak::openFromFile(path.toStdWString());
                QString mtlName = QStringLiteral("%1.mtl").arg(base);
                std::string mtl;
                bool mtlWritten = false;
                for (size_t i = 0; i < pak.entryCount(); ++i) {
                    m2model::AitdBody body = m2model::parseAitdBody(pak.read(i));
                    if (!body.valid || body.primitives.empty()) {
                        continue;
                    }
                    std::string obj =
                        m2model::exportAitdBodyObj(body, mtlName.toStdString(), &mtl);
                    if (obj.empty()) {
                        continue;
                    }
                    QString out = QStringLiteral("%1/%2_m%3.obj")
                                       .arg(outDir, base)
                                       .arg(i, 4, 10, QChar('0'));
                    writeBlob(out, reinterpret_cast<const uint8_t*>(obj.data()), obj.size());
                    if (!mtlWritten && !mtl.empty()) {
                        writeBlob(QStringLiteral("%1/%2").arg(outDir, mtlName),
                                   reinterpret_cast<const uint8_t*>(mtl.data()), mtl.size());
                        mtlWritten = true;
                    }
                }
                break;
            }
            case FileType::AitdImage: {
                auto pages = m2model::loadAitdImages(path.toStdWString());
                for (size_t i = 0; i < pages.size(); ++i) {
                    const auto& p = pages[i];
                    QString out = (pages.size() == 1)
                                       ? QStringLiteral("%1/%2.png").arg(outDir, base)
                                       : QStringLiteral("%1/%2_p%3.png")
                                             .arg(outDir, base)
                                             .arg(i, 3, 10, QChar('0'));
                    QImage image(p.rgba.data(), int(p.width), int(p.height), int(p.width) * 4,
                                  QImage::Format_RGBA8888);
                    if (image.save(out, "PNG")) {
                        ++written;
                    } else {
                        errors << tr("%1: PNG write failed").arg(out);
                    }
                }
                break;
            }
            default:
                break;
        }
    } catch (const std::exception& e) {
        errors << QStringLiteral("%1: %2").arg(fi.fileName(), QString::fromUtf8(e.what()));
    }
    return written;
}

} // namespace m2suite
