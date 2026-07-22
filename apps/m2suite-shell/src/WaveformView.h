#pragma once

#include <cstdint>
#include <vector>

#include <QWidget>

namespace m2suite {

// Simple audio waveform display: min/max amplitude envelope per pixel
// column over the full clip, all channels merged.
class WaveformView : public QWidget {
    Q_OBJECT

public:
    explicit WaveformView(QWidget* parent = nullptr);

    void setSamples(std::vector<int16_t> samples, int channels, double sampleRate);
    void clearSamples();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<int16_t> samples_;
    int channels_ = 1;
    double sampleRate_ = 0.0;
};

} // namespace m2suite
