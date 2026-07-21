#include "BeatgridOverlayRenderer.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

BeatgridOverlayRenderer::BeatgridOverlayRenderer() = default;
BeatgridOverlayRenderer::~BeatgridOverlayRenderer() = default;

uint32_t BeatgridOverlayRenderer::alphaBlend(uint32_t base, uint32_t overlay) {
    float alpha = ((overlay >> 24) & 0xFF) / 255.0f;

    uint8_t bR = (base >> 16) & 0xFF;
    uint8_t bG = (base >> 8) & 0xFF;
    uint8_t bB = base & 0xFF;

    uint8_t oR = (overlay >> 16) & 0xFF;
    uint8_t oG = (overlay >> 8) & 0xFF;
    uint8_t oB = overlay & 0xFF;

    uint8_t r = static_cast<uint8_t>(bR * (1.0f - alpha) + oR * alpha);
    uint8_t g = static_cast<uint8_t>(bG * (1.0f - alpha) + oG * alpha);
    uint8_t b = static_cast<uint8_t>(bB * (1.0f - alpha) + oB * alpha);

    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

void BeatgridOverlayRenderer::drawVerticalLine(std::vector<uint32_t>& pixels, int width, int height,
                                                  int x, uint32_t color, int lineWidth) {
    for (int dx = 0; dx < lineWidth; ++dx) {
        int px = x + dx - lineWidth / 2;
        if (px < 0 || px >= width) continue;

        for (int y = 0; y < height; ++y) {
            int idx = y * width + px;
            pixels[idx] = alphaBlend(pixels[idx], color);
        }
    }
}

void BeatgridOverlayRenderer::drawDashedLine(std::vector<uint32_t>& pixels, int width, int height,
                                                int x, uint32_t color, int dashLength) {
    if (x < 0 || x >= width) return;

    for (int y = 0; y < height; ++y) {
        if ((y / dashLength) % 2 == 0) {
            int idx = y * width + x;
            pixels[idx] = alphaBlend(pixels[idx], color);
        }
    }
}

int BeatgridOverlayRenderer::timeToScreenX(double time, double startTime, double endTime, int width) {
    if (endTime <= startTime) return 0;
    double normalized = (time - startTime) / (endTime - startTime);
    return static_cast<int>(normalized * width);
}

void BeatgridOverlayRenderer::renderOverlay(RenderedWaveform& waveform, const BeatGrid& grid,
                                               double startTime, double endTime,
                                               const BeatgridOverlayParams& params) {
    if (waveform.pixels.empty() || grid.beatPositions.empty()) return;

    int width = waveform.width;
    int height = waveform.height;

    int beatIndex = 0;
    for (double beatPos : grid.beatPositions) {
        if (beatPos < startTime) { beatIndex++; continue; }
        if (beatPos > endTime) break;

        int x = timeToScreenX(beatPos, startTime, endTime, width);

        bool isDownbeat = (beatIndex % grid.beatsPerBar == 0);
        bool isPhraseBoundary = (beatIndex % (grid.beatsPerBar * params.phraseBars) == 0);

        if (isPhraseBoundary && params.showPhraseBoundaries) {
            drawVerticalLine(waveform.pixels, width, height, x,
                              params.phraseColor, params.downbeatLineWidth + 1);
        } else if (isDownbeat) {
            drawVerticalLine(waveform.pixels, width, height, x,
                              params.downbeatColor, params.downbeatLineWidth);
        } else {
            drawDashedLine(waveform.pixels, width, height, x, params.beatColor, 3);
        }

        beatIndex++;
    }

    if (params.showBarNumbers) {
        for (double barPos : grid.barPositions) {
            if (barPos < startTime || barPos > endTime) continue;
            int x = timeToScreenX(barPos, startTime, endTime, width);
            drawVerticalLine(waveform.pixels, width, height, x,
                              params.barColor, params.barLineWidth);
        }
    }
}

RenderedWaveform BeatgridOverlayRenderer::renderStandalone(const BeatGrid& grid,
                                                             double startTime, double endTime,
                                                             int width, int height,
                                                             const BeatgridOverlayParams& params) {
    RenderedWaveform result;
    result.width = width;
    result.height = height;
    result.startTime = startTime;
    result.endTime = endTime;
    result.pixels.resize(width * height, 0x00000000);

    renderOverlay(result, grid, startTime, endTime, params);
    return result;
}

void BeatgridOverlayRenderer::renderWithDownbeats(RenderedWaveform& waveform, const BeatGrid& grid,
                                                     const std::vector<double>& downbeats,
                                                     double startTime, double endTime,
                                                     const BeatgridOverlayParams& params) {
    renderOverlay(waveform, grid, startTime, endTime, params);

    int width = waveform.width;
    int height = waveform.height;

    for (double db : downbeats) {
        if (db < startTime || db > endTime) continue;

        int x = timeToScreenX(db, startTime, endTime, width);

        int markerSize = 6;
        for (int dy = 0; dy < markerSize; ++dy) {
            int halfWidth = markerSize - dy;
            for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
                int px = x + dx;
                int py = dy;
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    waveform.pixels[py * width + px] = alphaBlend(
                        waveform.pixels[py * width + px], params.downbeatColor);
                }
            }
        }
    }
}

} // namespace BeatMate::Core
