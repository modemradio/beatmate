#pragma once

#include "BeatGridGenerator.h"

namespace BeatMate::Core {

class BeatGridEditor {
public:
    BeatGridEditor();
    ~BeatGridEditor();

    void setGrid(BeatGrid grid) { grid_ = std::move(grid); }
    const BeatGrid& getGrid() const { return grid_; }

    void shiftGrid(double offsetSeconds);
    void adjustBPM(double newBPM);

    void insertBeat(double position);
    void deleteBeat(int index);
    void warpBeat(int index, double newPosition);

    double snapToGrid(double position) const;
    int getNearestBeatIndex(double position) const;

    int getBarAt(double position) const;
    int getBeatInBarAt(double position) const;

private:
    BeatGrid grid_;
    void regenerateFromBPM();
};

}
