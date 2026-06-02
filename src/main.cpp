#include "wal/app.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

int main()
{
    wal::App app;

    try {
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
