#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace paint {
std::vector<std::uint8_t> read_regular_file_bounded(const std::string& path, std::size_t maximum,
                                                    const char* failure_message);
void write_file_atomic(const std::vector<std::uint8_t>& bytes, const std::string& path,
                       const char* failure_message, bool replace_existing = true);
} // namespace paint
