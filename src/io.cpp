#include "wal/io.hpp"

#include <fstream>
#include <stdexcept>

namespace wal {

std::vector<char> readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open " + path);
    }

    const std::ifstream::pos_type end = file.tellg();
    if (end < 0) {
        throw std::runtime_error("failed to determine file size for " + path);
    }

    const auto size = static_cast<size_t>(end);
    std::vector<char> buffer(size);
    file.seekg(0);
    if (!buffer.empty() && !file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()))) {
        throw std::runtime_error("failed to read " + path);
    }
    return buffer;
}

} // namespace wal
