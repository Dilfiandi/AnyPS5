#include <relinker/analysis/CallSiteResolver.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void write32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    for (std::size_t i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>((raw >> (8 * i)) & 0xFF);
}

void require(const bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

}

int main() {
    try {
        constexpr std::uint64_t base = 0x400000;
        constexpr std::int32_t displacement = 0x20;
        const std::uint64_t expectedTarget = base + 8 + displacement;

        std::vector<std::uint8_t> mov = {0x2E, 0x48, 0x8B, 0x05, 0, 0, 0, 0};
        write32(mov, 4, displacement);
        const auto movSites = Relinker::MakeCallSiteResolver()->ResolveCallSites(mov, base, expectedTarget, 1);
        require(movSites.size() == 1 && movSites[0] == base, "prefixed RIP-relative MOV was not resolved");

        std::vector<std::uint8_t> call = {0x2E, 0xFF, 0x15, 0, 0, 0, 0};
        write32(call, 3, displacement);
        const auto callSites = Relinker::MakeCallSiteResolver()->ResolveCallSites(call, base, base + 7 + displacement, 1);
        require(callSites.size() == 1 && callSites[0] == base, "prefixed RIP-relative FF call was not resolved");

        std::cout << "Call-site prefix tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
