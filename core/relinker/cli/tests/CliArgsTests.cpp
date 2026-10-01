#include <Cli.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

template<typename TCall>
void requireFailure(const TCall& call, const char* message) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

Cli::Args parse(const std::initializer_list<std::string>& arguments) {
    std::vector<std::string> values(arguments);
    std::vector<std::vector<char>> storage;
    storage.reserve(values.size());
    for (const auto& value : values)
        storage.push_back(std::vector<char>(value.begin(), value.end()));
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& value : storage) {
        value.push_back('\0');
        argv.push_back(value.data());
    }
    return Cli::ParseArgs(static_cast<int>(argv.size()), argv.data());
}

}

int main() {
    try {
        require(parse({"relinker", "--help"}).showHelp, "--help must set showHelp");
        require(parse({"relinker", "-h"}).showHelp, "-h must set showHelp");
        const auto normal = parse({"relinker", "--to-intel", "input.elf", "output.elf"});
        require(!normal.showHelp && normal.toIntel && normal.inputPath == "input.elf" && normal.outputPath == "output.elf", "normal arguments were parsed incorrectly");
        requireFailure([] { (void)parse({"relinker", "--windows-gui"}); }, "--windows-gui without --windows was accepted");
        requireFailure([] { (void)parse({"relinker", "--unknown"}); }, "unknown option was accepted");
        std::cout << "CLI argument tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
