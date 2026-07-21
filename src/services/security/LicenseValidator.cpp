#include "LicenseValidator.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
namespace BeatMate::Services::Security {
bool LicenseValidator::validate(const std::string& key) const {
    if (!validateFormat(key)) return false;
    std::string digits;
    for (char c : key) if (std::isalnum(static_cast<unsigned char>(c))) digits += c;
    return luhnCheck(digits);
}
bool LicenseValidator::luhnCheck(const std::string& digits) {
    int sum = 0; bool alternate = false;
    for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i) {
        int n = 0;
        char c = digits[static_cast<size_t>(i)];
        if (std::isdigit(static_cast<unsigned char>(c))) n = c - '0';
        else if (std::isalpha(static_cast<unsigned char>(c))) n = std::toupper(c) - 'A' + 10;
        if (alternate) { n *= 2; if (n > 9) n -= 9; }
        sum += n;
        alternate = !alternate;
    }
    return sum % 10 == 0;
}
bool LicenseValidator::validateFormat(const std::string& key) {
    if (key.size() != 23) return false;
    for (size_t i = 0; i < key.size(); ++i) {
        if (i == 5 || i == 11 || i == 17) { if (key[i] != '-') return false; }
        else { if (!std::isalnum(static_cast<unsigned char>(key[i]))) return false; }
    }
    return true;
}
} // namespace BeatMate::Services::Security
