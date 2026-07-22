#include "ImageViewport.h"

#include <QWheelEvent>
#include <QtMath>

namespace m2suite {

ImageViewport::ImageViewport(QWidget* parent) : QGraphicsView(parent) {
    setScene(&scene_);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(QColor(0x67, 0x67, 0x67));
    // Nearest-neighbor: retro textures should stay crisp when zoomed in.
    setRenderHint(QPainter::SmoothPixmapTransform, false);
}

void ImageViewport::setImage(const QImage& image) {
    scene_.clear();
    item_ = scene_.addPixmap(QPixmap::fromImage(image));
    scene_.setSceneRect(item_->boundingRect());
    // Default to 1:1 pixels; the user zooms from there (wheel/slider/spinbox).
    zoomPercent_ = 100;
    applyZoom();
    emit zoomChanged(zoomPercent_);
}

void ImageViewport::updateImage(const QImage& image) {
    if (!item_) {
        setImage(image);
        return;
    }
    item_->setPixmap(QPixmap::fromImage(image));
}

void ImageViewport::clearImage() {
    scene_.clear();
    item_ = nullptr;
}

void ImageViewport::setBackgroundColor(const QColor& color) {
    setBackgroundBrush(color);
}

void ImageViewport::setZoomPercent(int percent) {
    percent = qBound(kMinZoom, percent, kMaxZoom);
    if (percent == zoomPercent_) {
        return;
    }
    zoomPercent_ = percent;
    applyZoom();
    emit zoomChanged(zoomPercent_);
}

void ImageViewport::zoomToFit() {
    if (!item_) {
        return;
    }
    QRectF rect = item_->boundingRect();
    if (rect.isEmpty()) {
        return;
    }
    QSizeF avail = viewport()->size();
    qreal scale = qMin(avail.width() / rect.width(), avail.height() / rect.height());
    // Small textures start at an integer upscale for crispness; big ones
    // fit down to the window.
    if (scale > 1.0) {
        scale = qMax(1.0, std::floor(scale));
    }
    int percent = qBound(kMinZoom, int(scale * 100.0), kMaxZoom);
    zoomPercent_ = percent;
    applyZoom();
    emit zoomChanged(zoomPercent_);
}

void ImageViewport::applyZoom() {
    QTransform t;
    qreal s = zoomPercent_ / 100.0;
    t.scale(s, s);
    setTransform(t);
}

void ImageViewport::wheelEvent(QWheelEvent* event) {
    if (!item_) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    // 10% per wheel notch, snapped to multiples of 10 so the value stays
    // aligned with the slider/spinbox steps.
    int steps = event->angleDelta().y() / 120;
    int target = zoomPercent_ + steps * 10;
    target = (target / 10) * 10;
    setZoomPercent(target);
    event->accept();
}

} // namespace m2suite
