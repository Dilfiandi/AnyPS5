#include <relinker/analysis/CallSiteResolver.hpp>
#include <codegen/IInstructionScanner.hpp>
#include <codegen/x86/DecodedInstruction.hpp>
#include <codegen/x86/X64OpcodeConstants.hpp>
#include <cstring>
#include <limits>
#include <memory>
#include <algorithm>
#include <utility>

namespace Relinker {

namespace {
using namespace Codegen::X64OpcodeConstants;

std::int32_t readDisp32(const std::vector<std::uint8_t>& text, std::size_t offset) {
    std::uint32_t v = 0;
    std::memcpy(&v, text.data() + offset, 4);
    return static_cast<std::int32_t>(v);
}

std::size_t opcodeOffset(const std::vector<std::uint8_t>& text, const Codegen::InstructionMatch& m) {
    return Codegen::DecodedInstruction{text.data() + m.Offset, m.Length}.OpcodeOffset();
}

bool hasAddressOverride(const std::vector<std::uint8_t>& text, const Codegen::InstructionMatch& m, std::size_t opcode) {
    return std::find(text.begin() + m.Offset, text.begin() + m.Offset + opcode, PrefixAddressSize) != text.begin() + m.Offset + opcode;
}

bool isRipRelativeFF(const std::vector<std::uint8_t>& text, const Codegen::InstructionMatch& m) {
    const auto opcode = opcodeOffset(text, m);
    if (opcode + 6 > m.Length || hasAddressOverride(text, m, opcode) || text[m.Offset + opcode] != OneByteGrp5Rm)
        return false;
    const auto modrm = text[m.Offset + opcode + 1];
    const auto operation = (modrm >> ModRmRegShift) & ModRmRegMask;
    return (modrm >> ModRmModShift) == ModRmModIndirect && (modrm & ModRmRmMask) == ModRmRmRipRelative && operation >= 2 && operation <= 6;
}

std::size_t ffDispOffset(const std::vector<std::uint8_t>& text, const Codegen::InstructionMatch& m) {
    return static_cast<std::size_t>(m.Offset) + opcodeOffset(text, m) + 2;
}

bool isRipRelativeMov64(const std::vector<std::uint8_t>& text, const Codegen::InstructionMatch& m) {
    const auto opcode = opcodeOffset(text, m);
    if (opcode + 6 > m.Length || opcode == 0 || hasAddressOverride(text, m, opcode) || text[m.Offset + opcode] != OneByteModRmRangeJMax)
        return false;
    const auto rex = text[m.Offset + opcode - 1];
    if (rex < RexMin || rex > RexMax || !(rex & RexWBit))
        return false;
    const auto modrm = text[m.Offset + opcode + 1];
    return (modrm >> ModRmModShift) == ModRmModIndirect && (modrm & ModRmRmMask) == ModRmRmRipRelative;
}

std::size_t mov64DispOffset(const std::vector<std::uint8_t>& text, const Codegen::InstructionMatch& m) {
    return static_cast<std::size_t>(m.Offset) + opcodeOffset(text, m) + 2;
}
}

class CallSiteResolver : public ICallSiteResolver {
public:
    std::vector<FileByteOffset> ResolveCallSites(const std::vector<std::uint8_t>& textSection, FileByteOffset textSectionVAddr, VirtualAddress targetGotOrPltAddress, ByteCount targetGotOrPltSize) override;
private:
    const std::vector<std::uint8_t>* _cachedTextPtr = nullptr;
    std::vector<std::pair<VirtualAddress, FileByteOffset>> _cachedTargetToInstr;
    FileByteOffset _cachedTextVAddr = 0;
};

std::vector<FileByteOffset> CallSiteResolver::ResolveCallSites(const std::vector<std::uint8_t>& textSection, const FileByteOffset textSectionVAddr, const VirtualAddress targetGotOrPltAddress, const ByteCount targetGotOrPltSize) {
    if (targetGotOrPltSize > std::numeric_limits<VirtualAddress>::max() - targetGotOrPltAddress)
        throw RelinkerException("Relocation target range exceeds the address space");
    if (textSection.empty()) return {};
    if (_cachedTextPtr != &textSection || _cachedTextVAddr != textSectionVAddr) {
        auto scanner = Codegen::MakeInstructionScanner();
        auto instructions = scanner->ScanCodeSection(textSection, 0, textSection.size());
        _cachedTargetToInstr.clear();
        const auto referenceCount = std::count_if(instructions.begin(), instructions.end(), [&](const auto& match) {
            return isRipRelativeFF(textSection, match) || isRipRelativeMov64(textSection, match);
        });
        _cachedTargetToInstr.reserve(static_cast<std::size_t>(referenceCount));
        for (const auto& m : instructions) {
            std::size_t dispOff = 0;
            if (isRipRelativeFF(textSection, m))
                dispOff = ffDispOffset(textSection, m);
            else if (isRipRelativeMov64(textSection, m))
                dispOff = mov64DispOffset(textSection, m);
            else
                continue;
            if (dispOff + 4 > textSection.size()) continue;
            std::int32_t disp = readDisp32(textSection, dispOff);
            VirtualAddress instrEnd = textSectionVAddr + m.Offset + static_cast<VirtualAddress>(m.Length);
            VirtualAddress target = static_cast<VirtualAddress>(static_cast<std::int64_t>(instrEnd) + disp);
            _cachedTargetToInstr.emplace_back(target, m.Offset);
        }
        std::sort(_cachedTargetToInstr.begin(), _cachedTargetToInstr.end());
        _cachedTextPtr = &textSection;
        _cachedTextVAddr = textSectionVAddr;
    }

    std::vector<FileByteOffset> sites;
    const auto findTarget = [&](VirtualAddress target) {
        return std::lower_bound(_cachedTargetToInstr.begin(), _cachedTargetToInstr.end(),
            std::pair<VirtualAddress, FileByteOffset>{target, 0});
    };
    auto lower = findTarget(targetGotOrPltAddress);
    auto upper = findTarget(targetGotOrPltAddress + targetGotOrPltSize);
    for (auto it = lower; it != upper; ++it)
        sites.push_back(textSectionVAddr + it->second);
    return sites;
}

std::shared_ptr<ICallSiteResolver> MakeCallSiteResolver() { return std::make_shared<CallSiteResolver>(); }

}
