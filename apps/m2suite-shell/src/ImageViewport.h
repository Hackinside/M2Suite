#pragma once

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>

namespace m2suite {

// Zoomable image viewport: mouse-wheel zoom (anchored under the cursor)
// plus a programmatic percent API a zoom slider can bind to.
class ImageViewport : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageViewport(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    // Replaces the displayed image without touching zoom/scroll — for
    // video frame updates during playback.
    void updateImage(const QImage& image);
    void clearImage();

    // 100 = 1:1 pixels. Clamped to [kMinZoom, kMaxZoom].
    void setZoomPercent(int percent);
    int zoomPercent() const { return zoomPercent_; }
    void zoomToFit();

    // Canvas colour behind the image (user-configurable via the View menu).
    void setBackgroundColor(const QColor& color);

    static constexpr int kMinZoom = 10;
    static constexpr int kMaxZoom = 1600;

signals:
    void zoomChanged(int percent);

protected:
    void wheelEvent(QWheelEvent* event) override;

private:
    void applyZoom();

    QGraphicsScene scene_;
    QGraphicsPixmapItem* item_ = nullptr;
    int zoomPercent_ = 100;
};

} // namespace m2suite
