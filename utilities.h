#pragma once

#include <filesystem>

namespace veng {

bool streq(gsl::czstring left, gsl::czstring right);
std::vector<std::uint8_t> ReadFile(std::filesystem::path shader_path);

}
