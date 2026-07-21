#pragma once

#include <string>
#include <vector>
#include <memory>

#include "../../../models/VirtualDJTrack.h"

namespace BeatMate::Services::VirtualDJ {

class VirtualDJDatabase;
class VirtualDJRemote;

class VirtualDJService {
public:
    VirtualDJService();
    ~VirtualDJService();

    bool initialize();
    bool isAvailable() const;

    std::vector<Models::VirtualDJTrack> readDatabase();
    bool connectRemote(const std::string& ip, int port = 80);
    bool isRemoteConnected() const;

private:
    std::unique_ptr<VirtualDJDatabase> database_;
    std::unique_ptr<VirtualDJRemote> remote_;
    bool initialized_ = false;
};

} // namespace BeatMate::Services::VirtualDJ
