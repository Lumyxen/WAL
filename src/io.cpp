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

    const auto size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    return buffer;
}

} // namespace wal
