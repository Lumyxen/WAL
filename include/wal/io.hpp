#pragma once

#include <string>
#include <vector>

namespace wal {

[[nodiscard]] std::vector<char> readFile(const std::string& path);

} // namespace wal
