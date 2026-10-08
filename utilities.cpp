#include <precomp.h>
#include <utilities.h>
#include <fstream>

namespace veng {

bool streq(gsl::czstring left, gsl::czstring right) {
  return std::strcmp(left, right) == 0;
}

std::vector<std::uint8_t> ReadFile(std::filesystem::path shader_path) {
  if(!std::filesystem::exists(shader_path)) {
    return {};
  }

  if(!std::filesystem::is_regular_file(shader_path)) {
    return {};
  }

  std::ifstream file(shader_path, std::ios::binary);
  if(!file.is_open()) {
    return {};
  }

  const std::uint32_t size = std::filesystem::file_size(shader_path);
  std::vector<std::uint8_t> buffer(size);
  file.read(reinterpret_cast<char*>(buffer.data()), size);
  return buffer;
}

}  // namespace veng
