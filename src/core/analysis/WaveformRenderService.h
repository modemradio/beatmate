#pragma once

#include "AdvancedColouredWaveformService.h"
#include <vector>
#include <cstdint>

namespace BeatMate::Core {

struct WaveformRenderParams {
    int width = 800;
    int height = 100;
    float zoomLevel = 1.0f;
    double scrollPosition = 0.0;
    bool showRMS = true;
    bool show3Band = true;
    bool showCenterLine = true;
    uint32_t backgroundColor = 0xFF1A1A2E;
    float opacity = 1.0f;
};

struct RenderedWaveform {
    std::vector<uint32_t> pixels;
    int width = 0;
    int height = 0;
    double startTime = 0.0;
    double endTime = 0.0;
};

class WaveformRenderService {
public:
    WaveformRenderService();
    ~WaveformRenderService();

    RenderedWaveform render(const ColouredWaveformData& data, const WaveformRenderParams& params);

    RenderedWaveform renderRange(const ColouredWaveformData& data,
                                  double startTime, double endTime,
                                  int width, int height);

    int screenToPoint(const ColouredWaveformData& data, const WaveformRenderParams& params, int screenX);

    double screenToTime(const ColouredWaveformData& data, const WaveformRenderParams& params, int screenX);

private:
    void drawLine(std::vector<uint32_t>& pixels, int width, int height,
                   int x, int y1, int y2, uint32_t color);

    uint32_t blendColor(uint32_t base, uint32_t overlay, float alpha);
};

}
