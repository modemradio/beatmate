#pragma once

namespace BeatMate::UI {

struct IRetranslatable {
    virtual void retranslateUi() = 0;

protected:
    ~IRetranslatable() = default;
};

} // namespace BeatMate::UI
