#include <relinker/analysis/CallSiteResolver.hpp>
#include <codegen/IInstructionScanner.hpp>
#include <codegen/x86/DecodedInstruction.hpp>
#include <codegen/x86/X64OpcodeConstants.hpp>
#include <cstring>
#include <memory>
#include <map>

namespace Relinker {

namespace {
using namespace Codegen::X64OpcodeConstants;

std::int32_t readDisp32(const std::vector<std::uint8_t>& text, std::size_t offset) {
    std::uint32_t v = 0;
    std::memcpy(&v, text.data() + offset, 4);
    return static_cast<std::int32_t>(v);
}

bool isRipRelativeFF(const std::vector<std::uint8_t>& text, const Codegen::InstructionMatch& m) {
    const Codegen::DecodedInstruction instruction{text.data() + m.Offset, m.Length};
    const auto opcode = static_cast<std::size_t>(instruction.OpcodeOffset());
    const auto modrmOffset = opcode + 1;
    if (opcode >= m.Length || text[m.Offset + opcode] != OneByteGrp5Rm || modrmOffset >= m.Length)
        return false;
    const std::uint8_t modrm = text[m.Offset + modrmOffset];
    const std::uint8_t mod = (modrm >> ModRmModShift) & ModRmModMask;
    const std::uint8_t rm = modrm & ModRmRmMask;
    return mod == ModRmModIndirect && rm == ModRmRmRipRelative;
}

std::size_t ffDispOffset(const std::vector<std::uint8_t>& text, const Codegen::InstructionMatch& m) {
    const Codegen::DecodedInstruction instruction{text.data() + m.Offset, m.Length};
    return static_cast<std::size_t>(m.Offset) + instruction.OpcodeOffset() + 2;
}

bool hasRexW(const std::vector<std::uint8_t>& text, const Codegen::InstructionMatch& m) {
    const auto instruction = Codegen::DecodedInstruction{text.data() + m.Offset, m.Length};
    const auto opcodeOffset = instruction.OpcodeOffset();
    std::uint8_t rex = 0;
    for (std::size_t i = 0; i < opcodeOffset; ++i) {
        const auto b = text[m.Offset + i];
        if (b >= RexMin && b <= RexMax)
            rex = b;
    }
    return (rex & RexWBit) != 0;
}

bool isRipRelativeMov64(const std::vector<std::uint8_t>& text, const Codegen::InstructionMatch& m) {
    const Codegen::DecodedInstruction instruction{text.data() + m.Offset, m.Length};
    const auto opcodeOffset = instruction.OpcodeOffset();
    if (opcodeOffset >= m.Length || text[m.Offset + opcodeOffset] < OneByteModRmRangeJMin ||
        text[m.Offset + opcodeOffset] > OneByteModRmRangeJMax || !hasRexW(text, m))
        return false;
    const auto modrmOffset = opcodeOffset + 1;
    if (modrmOffset >= m.Length)
        return false;
    const std::uint8_t modrm = text[m.Offset + modrmOffset];
    const std::uint8_t mod = (modrm >> ModRmModShift) & ModRmModMask;
    const std::uint8_t rm = modrm & ModRmRmMask;
    return mod == ModRmModIndirect && rm == ModRmRmRipRelative;
}

std::size_t mov64DispOffset(const std::vector<std::uint8_t>& text, const Codegen::InstructionMatch& m) {
    const Codegen::DecodedInstruction instruction{text.data() + m.Offset, m.Length};
    return static_cast<std::size_t>(m.Offset) + instruction.OpcodeOffset() + 2;
}
}

class CallSiteResolver : public ICallSiteResolver {
public:
    std::vector<FileByteOffset> ResolveCallSites(const std::vector<std::uint8_t>& textSection, FileByteOffset textSectionVAddr, VirtualAddress targetGotOrPltAddress, ByteCount targetGotOrPltSize) override;
private:
    const std::vector<std::uint8_t>* _cachedTextPtr = nullptr;
    std::map<VirtualAddress, FileByteOffset> _cachedTargetToInstr;
};

std::vector<FileByteOffset> CallSiteResolver::ResolveCallSites(const std::vector<std::uint8_t>& textSection, const FileByteOffset textSectionVAddr, const VirtualAddress targetGotOrPltAddress, const ByteCount targetGotOrPltSize) {
    if (textSection.empty()) return {};
    if (_cachedTextPtr != &textSection) {
        auto scanner = Codegen::MakeInstructionScanner();
        auto instructions = scanner->ScanCodeSection(textSection, textSectionVAddr, textSection.size());
        _cachedTargetToInstr.clear();
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
            VirtualAddress instrEnd = m.Offset + static_cast<VirtualAddress>(m.Length);
            VirtualAddress target = static_cast<VirtualAddress>(static_cast<std::int64_t>(instrEnd) + disp);
            _cachedTargetToInstr.emplace(target, m.Offset);
        }
        _cachedTextPtr = &textSection;
    }

    std::vector<FileByteOffset> sites;
    auto lower = _cachedTargetToInstr.lower_bound(targetGotOrPltAddress);
    auto upper = _cachedTargetToInstr.lower_bound(targetGotOrPltAddress + targetGotOrPltSize);
    for (auto it = lower; it != upper; ++it)
        sites.push_back(it->second);
    return sites;
}

std::shared_ptr<ICallSiteResolver> MakeCallSiteResolver() { return std::make_shared<CallSiteResolver>(); }

}
