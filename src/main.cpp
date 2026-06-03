#include "wal/app.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace {

std::filesystem::path singleInstanceLockPath()
{
    if (const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR"); runtimeDir != nullptr && *runtimeDir != '\0') {
        return std::filesystem::path(runtimeDir) / "wal.lock";
    }

    return std::filesystem::temp_directory_path() / ("wal-" + std::to_string(getuid()) + ".lock");
}

class SingleInstanceLock {
public:
    SingleInstanceLock()
    {
        const std::filesystem::path path = singleInstanceLockPath();
        lockFd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (lockFd == -1) {
            throw std::runtime_error("failed to open single-instance lock: " + std::string(std::strerror(errno)));
        }

        if (flock(lockFd, LOCK_EX | LOCK_NB) == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                alreadyRunning = true;
                return;
            }

            throw std::runtime_error("failed to acquire single-instance lock: " + std::string(std::strerror(errno)));
        }

        if (ftruncate(lockFd, 0) == 0) {
            dprintf(lockFd, "%ld\n", static_cast<long>(getpid()));
        }
    }

    SingleInstanceLock(const SingleInstanceLock&) = delete;
    SingleInstanceLock& operator=(const SingleInstanceLock&) = delete;

    ~SingleInstanceLock()
    {
        if (lockFd != -1) {
            close(lockFd);
        }
    }

    [[nodiscard]] bool isAlreadyRunning() const
    {
        return alreadyRunning;
    }

private:
    int lockFd = -1;
    bool alreadyRunning = false;
};

} // namespace

int main()
{
    try {
        SingleInstanceLock lock;
        if (lock.isAlreadyRunning()) {
            return EXIT_SUCCESS;
        }

        wal::App app;
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
