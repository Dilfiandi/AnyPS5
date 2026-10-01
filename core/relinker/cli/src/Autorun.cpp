#include <Cli.hpp>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <cstdlib>

namespace Cli {

int Autorun(const std::string& absPath, bool) {
    const std::string cmd = "\"" + absPath + "\"";
    const int exitCode = std::system(cmd.c_str());

    std::cout << "\nExit code: " << exitCode << '\n';
    std::cout << "\nPress Enter to exit...\n";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    return exitCode;
}

}
