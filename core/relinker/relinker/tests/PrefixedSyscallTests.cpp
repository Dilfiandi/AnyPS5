#include <relinker/analysis/SyscallScanner.hpp>
#include <relinker/domain/Types.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(const bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void requireForbidden(const std::vector<std::uint8_t>& code, const char* message) {
    try {
        Relinker::MakeSyscallScanner()->ScanCodeSectionForSyscalls(code, 0x1000, code.size());
    } catch (const Domain::RelinkerException& error) {
        require(error.FailureOffset == 0x1000, message);
        return;
    }
    throw std::runtime_error(message);
}

}

int main() {
    try {
        requireForbidden({0x0F, 0x05}, "plain syscall was not rejected");
        requireForbidden({0xF3, 0x0F, 0x05}, "REP-prefixed syscall was not rejected");
        requireForbidden({0xF2, 0x0F, 0x05}, "REPNE-prefixed syscall was not rejected");
        requireForbidden({0x66, 0x0F, 0x05}, "operand-size-prefixed syscall was not rejected");
        requireForbidden({0x67, 0x0F, 0x05}, "address-size-prefixed syscall was not rejected");
        requireForbidden({0x48, 0x0F, 0x05}, "REX-prefixed syscall was not rejected");
        requireForbidden({0x2E, 0x0F, 0x05}, "segment-prefixed syscall was not rejected");
        Relinker::MakeSyscallScanner()->ScanCodeSectionForSyscalls({0x90, 0xC3}, 0x1000, 2);
        std::cout << "Prefixed syscall tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
