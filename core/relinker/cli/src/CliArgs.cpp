#include <Cli.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace Cli {

Args ParseArgs(int argc, char* argv[]) {
    Args args;
    args.toWindows = true;
    bool unusedFilterSpecified = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            args.showHelp = true;
        } else if (arg == "--skip-syscall-check") {
            args.skipSyscallCheck = true;
        } else if (arg == "--skip-sce-module") {
            args.skipSceModule = true;
        } else if (arg == "--exclude-sce-module") {
            if (i + 1 >= argc)
                throw std::runtime_error("--exclude-sce-module requires a file name");
            args.excludedSceModules.insert(argv[++i]);
        } else if (arg == "--to-intel") {
            args.toIntel = true;
        } else if (arg.rfind("unused-filter=", 0) == 0) {
            const std::string value = arg.substr(14);
            if (unusedFilterSpecified || value.size() != 1 || value[0] < '0' || value[0] > '2')
                throw std::runtime_error("unused-filter must be specified once with a value of 0, 1 or 2");
            args.unusedFilterLevel = static_cast<std::uint32_t>(value[0] - '0');
            unusedFilterSpecified = true;
        } else if (arg == "--registry") {
            args.writeRegistry = true;
        } else if (arg == "--rpath") {
            if (i + 1 >= argc)
                throw std::runtime_error("--rpath requires a value");
            args.runPath = argv[++i];
        } else if (arg == "--windows") {
            args.toWindows = true;
        } else if (arg == "--lazy-binding") {
            args.lazyBinding = true;
        } else if (arg == "--autorun") {
            args.autorun = true;
        } else if (arg == "--windows-diagnostics") {
            args.windowsDiagnostics = true;
        } else if (arg == "--windows-gui") {
            args.windowsGui = true;
        } else if (arg.rfind("--", 0) == 0 || arg == "unused-filter") {
            throw std::runtime_error("unknown option: " + arg);
        } else if (args.inputPath.empty()) {
            args.inputPath = arg;
        } else if (args.outputPath.empty()) {
            args.outputPath = arg;
        } else {
            throw std::runtime_error("unexpected argument: " + arg);
        }
    }

    if (args.showHelp)
        return args;

    if (args.skipSceModule && !args.excludedSceModules.empty())
        throw std::runtime_error("--exclude-sce-module conflicts with --skip-sce-module");

    if (args.inputPath.empty() || args.outputPath.empty())
        throw std::runtime_error(
            "Usage: relinker [--windows-diagnostics] [--windows-gui] [--skip-syscall-check] [--skip-sce-module] [--exclude-sce-module <file>]... [--to-intel] [unused-filter=0|1|2] [--registry] [--rpath <path>] [--lazy-binding] [--autorun] <input.elf> <output.exe>\n"
            "Example: relinker input.elf output.exe"
        );

    return args;
}

void PrintUsage(std::ostream& output) {
    output << "Usage: relinker [options] <input.elf> <output.exe>\n"
           << "\n"
           << "Options:\n"
           << "  -h, --help                              Show this help message\n"
           << "      --windows                           Compatibility alias; Windows output is always used\n"
           << "      --windows-diagnostics               Enable Windows dependency diagnostics\n"
           << "      --windows-gui                       Use the Windows GUI subsystem\n"
           << "      --skip-syscall-check                Skip forbidden syscall validation\n"
           << "      --skip-sce-module                   Skip sce_module/sce_modules processing\n"
           << "      --exclude-sce-module <file>         Exclude a sce_module file (repeatable)\n"
           << "      --to-intel                          Lower supported AMD-only instructions\n"
           << "      unused-filter=0|1|2                 Select unused NID filtering level\n"
           << "      --registry                          Write the call registry JSON\n"
           << "      --rpath <path>                      Set the runtime library search path\n"
           << "      --lazy-binding                      Enable lazy symbol binding\n"
           << "      --autorun                           Run the generated executable\n"
           << "\n"
           << "Example: relinker --to-intel input.elf output.exe\n";
}

}
