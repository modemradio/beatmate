#include "BeatGridEditor.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

BeatGridEditor::BeatGridEditor() = default;
BeatGridEditor::~BeatGridEditor() = default;

void BeatGridEditor::shiftGrid(double offsetSeconds) {
    grid_.firstBeatOffset += offsetSeconds;
    for (auto& b : grid_.beatPositions) b += offsetSeconds;
    for (auto& b : grid_.barPositions) b += offsetSeconds;
    spdlog::info("BeatGridEditor: shifted by {:.3f}s", offsetSeconds);
}

void BeatGridEditor::adjustBPM(double newBPM) {
    if (newBPM <= 0) return;
    double ratio = grid_.bpm / newBPM;
    grid_.bpm = newBPM;

    double offset = grid_.firstBeatOffset;
    grid_.beatPositions.clear();
    grid_.barPositions.clear();

    double beatInterval = 60.0 / newBPM;
    double pos = offset;
    int beatCount = 0;

    double maxPos = (grid_.beatPositions.empty()) ? 600.0 :
                    grid_.beatPositions.back() * ratio + 10.0;

    while (pos < maxPos) {
        grid_.beatPositions.push_back(pos);
        if (beatCount % grid_.beatsPerBar == 0) {
            grid_.barPositions.push_back(pos);
        }
        pos += beatInterval;
        beatCount++;
    }

    spdlog::info("BeatGridEditor: BPM adjusted to {:.1f}", newBPM);
}

void BeatGridEditor::insertBeat(double position) {
    auto it = std::lower_bound(grid_.beatPositions.begin(), grid_.beatPositions.end(), position);
    grid_.beatPositions.insert(it, position);
}

void BeatGridEditor::deleteBeat(int index) {
    if (index >= 0 && index < static_cast<int>(grid_.beatPositions.size())) {
        grid_.beatPositions.erase(grid_.beatPositions.begin() + index);
    }
}

void BeatGridEditor::warpBeat(int index, double newPosition) {
    if (index >= 0 && index < static_cast<int>(grid_.beatPositions.size())) {
        grid_.beatPositions[index] = newPosition;
    }
}

double BeatGridEditor::snapToGrid(double position) const {
    int idx = getNearestBeatIndex(position);
    if (idx >= 0 && idx < static_cast<int>(grid_.beatPositions.size())) {
        return grid_.beatPositions[idx];
    }
    return position;
}

int BeatGridEditor::getNearestBeatIndex(double position) const {
    if (grid_.beatPositions.empty()) return -1;

    auto it = std::lower_bound(grid_.beatPositions.begin(), grid_.beatPositions.end(), position);
    if (it == grid_.beatPositions.end()) return static_cast<int>(grid_.beatPositions.size()) - 1;
    if (it == grid_.beatPositions.begin()) return 0;

    auto prev = it - 1;
    return (std::fabs(*it - position) < std::fabs(*prev - position)) ?
        static_cast<int>(it - grid_.beatPositions.begin()) :
        static_cast<int>(prev - grid_.beatPositions.begin());
}

int BeatGridEditor::getBarAt(double position) const {
    int beatIdx = getNearestBeatIndex(position);
    if (beatIdx < 0) return 0;
    return beatIdx / grid_.beatsPerBar + 1;
}

int BeatGridEditor::getBeatInBarAt(double position) const {
    int beatIdx = getNearestBeatIndex(position);
    if (beatIdx < 0) return 1;
    return (beatIdx % grid_.beatsPerBar) + 1;
}

}
