#pragma once

#include "BeatGridGenerator.h"
#include "WaveformRenderService.h"
#include <vector>
#include <cstdint>

namespace BeatMate::Core {

struct BeatgridOverlayParams {
    uint32_t beatColor = 0x80FFFFFF;       // Semi-transparent white
    uint32_t downbeatColor = 0xC0FF8800;   // Orange for downbeats
    uint32_t barColor = 0xC0FFD700;        // Gold for bar lines
    uint32_t phraseColor = 0x80FF00FF;     // Magenta for phrase boundaries
    int beatLineWidth = 1;
    int downbeatLineWidth = 2;
    int barLineWidth = 2;
    bool showBeatNumbers = false;
    bool showBarNumbers = true;
    bool showPhraseBoundaries = true;
    int phraseBars = 8;                     // Show phrase every N bars
};

class BeatgridOverlayRenderer {
public:
    BeatgridOverlayRenderer();
    ~BeatgridOverlayRenderer();

    void renderOverlay(RenderedWaveform& waveform, const BeatGrid& grid,
                        double startTime, double endTime,
                        const BeatgridOverlayParams& params = {});

    RenderedWaveform renderStandalone(const BeatGrid& grid,
                                       double startTime, double endTime,
                                       int width, int height,
                                       const BeatgridOverlayParams& params = {});

    void renderWithDownbeats(RenderedWaveform& waveform, const BeatGrid& grid,
                               const std::vector<double>& downbeats,
                               double startTime, double endTime,
                               const BeatgridOverlayParams& params = {});

private:
    void drawVerticalLine(std::vector<uint32_t>& pixels, int width, int height,
                            int x, uint32_t color, int lineWidth = 1);

    void drawDashedLine(std::vector<uint32_t>& pixels, int width, int height,
                          int x, uint32_t color, int dashLength = 4);

    int timeToScreenX(double time, double startTime, double endTime, int width);

    uint32_t alphaBlend(uint32_t base, uint32_t overlay);
};

} // namespace BeatMate::Core
