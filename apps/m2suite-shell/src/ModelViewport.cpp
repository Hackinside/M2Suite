#include "ModelViewport.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace m2suite {

ModelViewport::ModelViewport(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 200);
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
}

void ModelViewport::setBody(const m2model::AitdBody& body) {
    body_ = body;
    resetView();
}

void ModelViewport::setRenderMode(m2model::AitdRenderMode mode) {
    mode_ = mode;
    rerender();
    update();
}

void ModelViewport::setSpinning(bool spinning) {
    spinning_ = spinning;
}

void ModelViewport::resetView() {
    camera_ = m2model::AitdCamera{};
    // A slight default tilt reads more like a 3D object than a flat
    // silhouette when the model first appears.
    camera_.pitch = -0.15;
    rerender();
    update();
}

void ModelViewport::stepSpin(double deltaYaw) {
    if (!spinning_ || orbiting_ || panning_) {
        return;
    }
    camera_.yaw += deltaYaw;
    rerender();
    update();
}

void ModelViewport::rerender() {
    int w = std::max(64, width());
    int h = std::max(64, height());
    size_t need = size_t(w) * size_t(h) * 4;
    if (buffer_.size() != need) {
        buffer_.assign(need, 0);
    }
    if (!m2model::renderAitdBody(body_, buffer_.data(), uint32_t(w), uint32_t(h), camera_,
                                  mode_)) {
        image_ = QImage();
        return;
    }
    image_ = QImage(buffer_.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
}

void ModelViewport::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if (image_.isNull() || image_.size() != size()) {
        rerender(); // size changed (or first paint)
    }
    if (image_.isNull()) {
        p.fillRect(rect(), QColor(0x10, 0x12, 0x18));
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, tr("No model geometry"));
        return;
    }
    p.drawImage(0, 0, image_);

    // Small on-canvas hint so the controls are discoverable.
    p.setPen(QColor(150, 150, 160));
    p.drawText(8, height() - 8,
                tr("drag: orbit   right-drag: pan   wheel: zoom   double-click: reset"));
}

void ModelViewport::mousePressEvent(QMouseEvent* event) {
    lastPos_ = event->pos();
    if (event->button() == Qt::LeftButton) {
        orbiting_ = true;
        setCursor(Qt::ClosedHandCursor);
    } else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        panning_ = true;
        setCursor(Qt::SizeAllCursor);
    }
}

void ModelViewport::mouseMoveEvent(QMouseEvent* event) {
    QPoint delta = event->pos() - lastPos_;
    lastPos_ = event->pos();
    if (orbiting_) {
        camera_.yaw += delta.x() * 0.01;
        camera_.pitch += delta.y() * 0.01;
        camera_.pitch = std::clamp(camera_.pitch, -1.55, 1.55);
        rerender();
        update();
    } else if (panning_) {
        camera_.panX += double(delta.x()) / std::max(1, width());
        camera_.panY += double(delta.y()) / std::max(1, height());
        rerender();
        update();
    }
}

void ModelViewport::mouseReleaseEvent(QMouseEvent*) {
    orbiting_ = false;
    panning_ = false;
    setCursor(Qt::OpenHandCursor);
}

void ModelViewport::mouseDoubleClickEvent(QMouseEvent*) {
    resetView();
}

void ModelViewport::wheelEvent(QWheelEvent* event) {
    double steps = event->angleDelta().y() / 120.0;
    camera_.zoom *= std::pow(1.15, steps);
    camera_.zoom = std::clamp(camera_.zoom, 0.05, 40.0);
    rerender();
    update();
    event->accept();
}

} // namespace m2suite
