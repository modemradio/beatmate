#pragma once
#include <string>
namespace BeatMate::Services::Security {
class HardwareId {
public:
    HardwareId() = default;
    std::string getHardwareId();
    // Exposed so the WordPress license client can transmit a real MAC to /activate.
    std::string getMACAddress();
private:
    std::string getCPUID(); std::string getMotherboardId(); std::string getDiskSerial();
    std::string queryWMI(const std::string& wmiClass, const std::string& property);
};
} // namespace BeatMate::Services::Security
