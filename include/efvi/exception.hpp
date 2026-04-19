#pragma once
#include <stdexcept>
#include <string>

namespace efvi {

class ViException : public std::runtime_error {
public:
    ViException(int error_code, const std::string& msg)
        : std::runtime_error("[efvi:" + std::to_string(error_code) + "] " + msg)
        , error_code_(error_code) {}

    int error_code() const noexcept { return error_code_; }

private:
    int error_code_;
};

} // namespace efvi
