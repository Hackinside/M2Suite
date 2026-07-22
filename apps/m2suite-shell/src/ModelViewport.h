#pragma once

#include <QImage>
#include <QPoint>
#include <QWidget>

#include "m2model/AitdPak.h"

namespace m2suite {

// Interactive 3D viewport for Alone in the Dark bodies. Software-rendered
// (no GPU/QtQuick3D dependency, which keeps the portable package small and
// avoids a QML runtime), with the usual orbit / pan / zoom controls:
//   left-drag   orbit (yaw + pitch)
//   right-drag or middle-drag  pan
//   wheel       zoom
//   double-click  reset the view
class ModelViewport : public QWidget {
    Q_OBJECT

public:
    explicit ModelViewport(QWidget* parent = nullptr);

    void setBody(const m2model::AitdBody& body);
    void setRenderMode(m2model::AitdRenderMode mode);
    void setSpinning(bool spinning);
    bool isSpinning() const { return spinning_; }
    void resetView();
    // Advances the idle spin; call from a timer.
    void stepSpin(double deltaYaw);

    const m2model::AitdCamera& camera() const { return camera_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void rerender();

    m2model::AitdBody body_;
    m2model::AitdCamera camera_;
    m2model::AitdRenderMode mode_ = m2model::AitdRenderMode::SolidMaterials;
    QImage image_;
    std::vector<uint8_t> buffer_;
    QPoint lastPos_;
    bool orbiting_ = false;
    bool panning_ = false;
    bool spinning_ = true;
};

} // namespace m2suite
