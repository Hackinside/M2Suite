#include "WaveformView.h"

#include <QPainter>

namespace m2suite {

WaveformView::WaveformView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(160);
}

void WaveformView::setSamples(std::vector<int16_t> samples, int channels, double sampleRate) {
    samples_ = std::move(samples);
    channels_ = channels > 0 ? channels : 1;
    sampleRate_ = sampleRate;
    update();
}

void WaveformView::clearSamples() {
    samples_.clear();
    update();
}

void WaveformView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(32, 32, 36));

    if (samples_.empty()) {
        p.setPen(QColor(140, 140, 140));
        p.drawText(rect(), Qt::AlignCenter, tr("No audio loaded"));
        return;
    }

    const int w = width();
    const int h = height();
    const int mid = h / 2;
    const size_t frames = samples_.size() / size_t(channels_);

    p.setPen(QColor(70, 70, 78));
    p.drawLine(0, mid, w, mid);

    p.setPen(QColor(95, 200, 140));
    for (int x = 0; x < w; ++x) {
        size_t begin = size_t((double(x) / w) * frames);
        size_t end = size_t((double(x + 1) / w) * frames);
        if (end <= begin) end = begin + 1;
        if (begin >= frames) break;
        if (end > frames) end = frames;

        int16_t lo = 32767, hi = -32768;
        for (size_t f = begin; f < end; ++f) {
            for (int c = 0; c < channels_; ++c) {
                int16_t s = samples_[f * size_t(channels_) + size_t(c)];
                if (s < lo) lo = s;
                if (s > hi) hi = s;
            }
        }
        int yHi = mid - int((double(hi) / 32768.0) * (mid - 2));
        int yLo = mid - int((double(lo) / 32768.0) * (mid - 2));
        p.drawLine(x, yHi, x, yLo);
    }

    // Duration/rate footer.
    if (sampleRate_ > 0) {
        double seconds = double(frames) / sampleRate_;
        p.setPen(QColor(170, 170, 170));
        p.drawText(6, h - 6,
                    tr("%1 s   %2 Hz   %3 ch")
                        .arg(seconds, 0, 'f', 2)
                        .arg(int(sampleRate_))
                        .arg(channels_));
    }
}

} // namespace m2suite
