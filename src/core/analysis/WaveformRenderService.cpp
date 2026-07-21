#include "WaveformRenderService.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

WaveformRenderService::WaveformRenderService() = default;
WaveformRenderService::~WaveformRenderService() = default;

void WaveformRenderService::drawLine(std::vector<uint32_t>& pixels, int width, int height,
                                       int x, int y1, int y2, uint32_t color) {
    if (x < 0 || x >= width) return;
    y1 = std::clamp(y1, 0, height - 1);
    y2 = std::clamp(y2, 0, height - 1);
    if (y1 > y2) std::swap(y1, y2);

    for (int y = y1; y <= y2; ++y) {
        pixels[y * width + x] = color;
    }
}

uint32_t WaveformRenderService::blendColor(uint32_t base, uint32_t overlay, float alpha) {
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

RenderedWaveform WaveformRenderService::render(const ColouredWaveformData& data,
                                                 const WaveformRenderParams& params) {
    RenderedWaveform result;
    result.width = params.width;
    result.height = params.height;
    result.pixels.resize(params.width * params.height, params.backgroundColor);

    if (data.points.empty()) return result;

    int totalPoints = static_cast<int>(data.points.size());
    int visiblePoints = static_cast<int>(totalPoints / params.zoomLevel);
    visiblePoints = std::max(1, std::min(visiblePoints, totalPoints));

    int startPoint = static_cast<int>(params.scrollPosition * (totalPoints - visiblePoints));
    startPoint = std::clamp(startPoint, 0, totalPoints - visiblePoints);
    int endPoint = startPoint + visiblePoints;

    result.startTime = static_cast<double>(startPoint) / totalPoints * data.duration;
    result.endTime = static_cast<double>(endPoint) / totalPoints * data.duration;

    int centerY = params.height / 2;

    if (params.showCenterLine) {
        for (int x = 0; x < params.width; ++x) {
            result.pixels[centerY * params.width + x] =
                blendColor(params.backgroundColor, 0xFF404060, 0.5f);
        }
    }

    float pointsPerPixel = static_cast<float>(visiblePoints) / params.width;

    for (int x = 0; x < params.width; ++x) {
        int ptStart = startPoint + static_cast<int>(x * pointsPerPixel);
        int ptEnd = startPoint + static_cast<int>((x + 1) * pointsPerPixel);
        ptEnd = std::max(ptEnd, ptStart + 1);
        ptEnd = std::min(ptEnd, totalPoints);

        float maxAmp = 0.0f;
        float avgLow = 0.0f, avgMid = 0.0f, avgHigh = 0.0f;
        uint32_t avgR = 0, avgG = 0, avgB = 0;
        int count = 0;

        for (int pt = ptStart; pt < ptEnd; ++pt) {
            maxAmp = std::max(maxAmp, data.points[pt].amplitude);
            avgLow += data.points[pt].low;
            avgMid += data.points[pt].mid;
            avgHigh += data.points[pt].high;
            avgR += data.points[pt].color.r;
            avgG += data.points[pt].color.g;
            avgB += data.points[pt].color.b;
            count++;
        }

        if (count > 0) {
            avgLow /= count;
            avgMid /= count;
            avgHigh /= count;
            avgR /= count;
            avgG /= count;
            avgB /= count;
        }

        uint32_t waveColor = 0xFF000000 | (avgR << 16) | (avgG << 8) | avgB;

        if (params.show3Band) {
            int halfHeight = centerY;
            int bassHeight = static_cast<int>(avgLow * halfHeight * params.opacity);
            int midHeight = static_cast<int>(avgMid * halfHeight * params.opacity);
            int highHeight = static_cast<int>(avgHigh * halfHeight * params.opacity);

            uint32_t bassColor = 0xFFFF2828;
            drawLine(result.pixels, params.width, params.height, x,
                     centerY - bassHeight, centerY + bassHeight, bassColor);

            uint32_t midColor = 0xFF28FF28;
            drawLine(result.pixels, params.width, params.height, x,
                     centerY - midHeight, centerY - bassHeight,
                     blendColor(params.backgroundColor, midColor, 0.7f));
            drawLine(result.pixels, params.width, params.height, x,
                     centerY + bassHeight, centerY + midHeight,
                     blendColor(params.backgroundColor, midColor, 0.7f));

            uint32_t highColor = 0xFF2864FF;
            drawLine(result.pixels, params.width, params.height, x,
                     centerY - midHeight - highHeight, centerY - midHeight,
                     blendColor(params.backgroundColor, highColor, 0.5f));
            drawLine(result.pixels, params.width, params.height, x,
                     centerY + midHeight, centerY + midHeight + highHeight,
                     blendColor(params.backgroundColor, highColor, 0.5f));
        } else {
            int waveHeight = static_cast<int>(maxAmp * centerY * params.opacity);
            drawLine(result.pixels, params.width, params.height, x,
                     centerY - waveHeight, centerY + waveHeight, waveColor);
        }
    }

    return result;
}

RenderedWaveform WaveformRenderService::renderRange(const ColouredWaveformData& data,
                                                      double startTime, double endTime,
                                                      int width, int height) {
    WaveformRenderParams params;
    params.width = width;
    params.height = height;

    if (data.duration > 0) {
        params.zoomLevel = static_cast<float>(data.duration / (endTime - startTime));
        params.scrollPosition = static_cast<float>(startTime / (data.duration - (endTime - startTime)));
    }

    return render(data, params);
}

int WaveformRenderService::screenToPoint(const ColouredWaveformData& data,
                                            const WaveformRenderParams& params, int screenX) {
    int totalPoints = static_cast<int>(data.points.size());
    int visiblePoints = static_cast<int>(totalPoints / params.zoomLevel);
    int startPoint = static_cast<int>(params.scrollPosition * (totalPoints - visiblePoints));

    float pointsPerPixel = static_cast<float>(visiblePoints) / params.width;
    return startPoint + static_cast<int>(screenX * pointsPerPixel);
}

double WaveformRenderService::screenToTime(const ColouredWaveformData& data,
                                              const WaveformRenderParams& params, int screenX) {
    int pt = screenToPoint(data, params, screenX);
    int totalPoints = static_cast<int>(data.points.size());
    if (totalPoints == 0) return 0.0;
    return static_cast<double>(pt) / totalPoints * data.duration;
}

} // namespace BeatMate::Core
