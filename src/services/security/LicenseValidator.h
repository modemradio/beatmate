#pragma once
#include <string>
namespace BeatMate::Services::Security {
class LicenseValidator {
public:
    LicenseValidator() = default;
    bool validate(const std::string& key) const;
    static bool luhnCheck(const std::string& digits);
    static bool validateFormat(const std::string& key); // XXXXX-XXXXX-XXXXX-XXXXX
};
} // namespace BeatMate::Services::Security
