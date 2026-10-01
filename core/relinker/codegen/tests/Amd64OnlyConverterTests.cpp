#include <codegen/IAmd64OnlyConverter.hpp>
#include <codegen/CodegenException.hpp>
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/x86/Sse4aLowering.hpp>
#include <codegen/x86/Sse4aOperands.hpp>
#include <codegen/x86/Sha256Operands.hpp>
#include <codegen/x86/ClzeroOperands.hpp>
#include <codegen/x86/ClzeroLowering.hpp>
#include <codegen/x86/DecodedInstruction.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <codegen/x86/X64InstructionRewriter.hpp>
#include <codegen/IInstructionScanner.hpp>
#include <array>
#include <cstring>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Bytes = std::vector<std::uint8_t>;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void requireFailure(const std::function<void()>& operation, const char* message) {
    try {
        operation();
    } catch (const Codegen::CodegenException&) {
        return;
    } catch (const Domain::RelinkerException&) {
        return;
    }
    throw std::runtime_error(message);
}

Domain::FileByteOffset failureOffset(const std::function<void()>& operation, const char* message) {
    try {
        operation();
    } catch (const Codegen::CodegenException& error) {
        return error.FailureOffset;
    }
    throw std::runtime_error(message);
}

template<typename TValue>
void write(Bytes& bytes, std::size_t offset, TValue value) {
    if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) throw std::runtime_error("Test fixture write is out of bounds");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

template<typename TValue>
TValue read(const Bytes& bytes, std::size_t offset) {
    TValue value;
    if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) throw std::runtime_error("Test fixture read is out of bounds");
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

Bytes withReturn(Bytes body, const std::size_t returnBranchOffset) {
    body.at(returnBranchOffset) = 0xE9;
    for (std::size_t index = 1; index <= 4; ++index) body.at(returnBranchOffset + index) = 0;
    return body;
}

const Bytes kExtrqSite = {0x66, 0x0F, 0x78, 0xC3, 0x08, 0x28};
const Bytes kInsertqSelfSite = {0xF2, 0x0F, 0x78, 0xDB, 0x08, 0x08};
const Bytes kInsertqCrossSite = {0xF2, 0x0F, 0x78, 0xC8, 0x08, 0x00};
const Bytes kInsertqHighSite = {0xF2, 0x44, 0x0F, 0x78, 0xCC, 0x10, 0x10};
const Bytes kInsertqWordSite = {0xF2, 0x0F, 0x78, 0xDC, 0x10, 0x10};

const Bytes kExtrqBody = {
    0x66, 0x0F, 0x38, 0x00, 0x1D, 0x07, 0x00, 0x00, 0x00, 0xE9, 0x00, 0x00, 0x00, 0x00, 0xCC, 0xCC,
    0x05, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
const Bytes kInsertqSelfBody = {
    0x66, 0x0F, 0x38, 0x00, 0x1D, 0x07, 0x00, 0x00, 0x00, 0xE9, 0x00, 0x00, 0x00, 0x00, 0xCC, 0xCC,
    0x00, 0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
const Bytes kInsertqCrossBody = {
    0x66, 0x0F, 0x6C, 0xC8, 0x66, 0x0F, 0x38, 0x00, 0x0D, 0x13, 0x00, 0x00, 0x00, 0xE9, 0x00, 0x00,
    0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0x08, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
const Bytes kInsertqHighBody = {
    0x66, 0x44, 0x0F, 0x6C, 0xCC, 0x66, 0x44, 0x0F, 0x38, 0x00, 0x0D, 0x11, 0x00, 0x00, 0x00, 0xE9,
    0x00, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0x00, 0x01, 0x08, 0x09, 0x04, 0x05, 0x06, 0x07, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
const Bytes kInsertqWordBody = {
    0x66, 0x0F, 0x6C, 0xDC, 0x66, 0x0F, 0x38, 0x00, 0x1D, 0x13, 0x00, 0x00, 0x00, 0xE9, 0x00, 0x00,
    0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0x00, 0x01, 0x08, 0x09, 0x04, 0x05, 0x06, 0x07, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};

const Bytes kClzeroBody = {
    0x48, 0x8D, 0xA4, 0x24, 0x70, 0xFF, 0xFF, 0xFF, 0xF3, 0x0F, 0x7F, 0x04, 0x24, 0x51, 0x66, 0x48,
    0x0F, 0x6E, 0xC0, 0x66, 0x0F, 0xDB, 0x05, 0x35, 0x00, 0x00, 0x00, 0x66, 0x48, 0x0F, 0x7E, 0xC1,
    0x66, 0x0F, 0xEF, 0xC0, 0x66, 0x0F, 0xE7, 0x01, 0x66, 0x0F, 0xE7, 0x41, 0x10, 0x66, 0x0F, 0xE7,
    0x41, 0x20, 0x66, 0x0F, 0xE7, 0x41, 0x30, 0x59, 0xF3, 0x0F, 0x6F, 0x04, 0x24, 0x48, 0x8D, 0xA4,
    0x24, 0x90, 0x00, 0x00, 0x00, 0xE9, 0x00, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void decoderLengths() {
    const Codegen::X64InstructionDecoder decoder;
    const std::vector<Bytes> instructions = {
        kExtrqSite, kInsertqSelfSite, kInsertqCrossSite, kInsertqHighSite, kInsertqWordSite,
        {0x66, 0x0F, 0x79, 0xCA}, {0xF2, 0x0F, 0x79, 0xCA}, {0x66, 0x45, 0x0F, 0x79, 0xCA},
        {0xF3, 0x0F, 0xB8, 0xC0}, {0xCD, 0x41}, {0x0F, 0x0D, 0x08}, {0x0F, 0xC0, 0xC1}, {0x0F, 0xC3, 0x07},
        {0x66, 0x0F, 0xC4, 0xC0, 0x01}, {0xC2, 0x08, 0x00}, {0xC8, 0x10, 0x00, 0x00}, {0xF3, 0x0F, 0x2B, 0x07},
        {0xF2, 0x44, 0x0F, 0x2B, 0x4C, 0x24, 0x10}, {0x0F, 0x01, 0xFA}, {0x0F, 0xB9, 0x00},
        {0x41, 0x0F, 0xBB, 0xF7}, {0x0F, 0xBB, 0x47, 0x08},
        {0x0F, 0x38, 0xCB, 0xCA}, {0x45, 0x0F, 0x38, 0xCC, 0xE1}, {0x0F, 0x38, 0xCD, 0x08}, {0x0F, 0x38, 0xCB, 0x0D, 0x10, 0x00, 0x00, 0x00},
        {0x48, 0x66, 0xB8, 0x34, 0x12}, {0x66, 0x48, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8}, {0x41, 0x48, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8},
        {0x48, 0x64, 0x8B, 0x00}, {0x48, 0xF3, 0x0F, 0x2B, 0x00}, {0x48, 0x67, 0x0F, 0x01, 0xFC}, {0x48, 0x48, 0x0F, 0x01, 0xFC}};
    Bytes padded;
    for (const auto& instruction : instructions) {
        padded = instruction;
        padded.insert(padded.end(), 8, 0x90);
        require(decoder.Decode(padded.data(), padded.size()) == instruction.size(), "AMD-only or repaired two-byte opcode was decoded with the wrong length");
    }
    requireFailure([&] { const Bytes bare = {0x0F, 0x78, 0xC3, 0x08, 0x28}; (void)decoder.Decode(bare.data(), bare.size()); }, "0F 78 without an SSE4a prefix was accepted");
    const Bytes strayRex = {0x48, 0x64, 0x8B, 0x00, 0x90};
    const auto stray = decoder.DecodeInstruction(strayRex.data(), strayRex.size());
    require(stray.Length == 4 && stray.OpcodeOffset == 2 && stray.RexPrefix == 0 && stray.SegmentPrefix == 0x64, "A REX before a legacy prefix was kept");
    const Bytes lastRex = {0x41, 0x48, 0x8B, 0x00, 0x90};
    const auto last = decoder.DecodeInstruction(lastRex.data(), lastRex.size());
    require(last.Length == 4 && last.OpcodeOffset == 2 && last.RexPrefix == 0x48, "The REX before the opcode was not the one kept");
}

void sse4aOperands() {
    const auto check = [](const Bytes& site, const bool insertq, const int dst, const int src, const int length, const int index) {
        const auto operands = Codegen::DecodeSse4a(site.data(), site.size());
        require(operands.Insertq == insertq && !operands.RegisterForm && operands.Destination == dst && operands.Source == src && operands.Length == length && operands.Index == index, "SSE4a operands were decoded incorrectly");
    };
    check(kExtrqSite, false, 3, 3, 8, 40);
    check(kInsertqSelfSite, true, 3, 3, 8, 8);
    check(kInsertqCrossSite, true, 1, 0, 8, 0);
    check(kInsertqHighSite, true, 9, 4, 16, 16);
    check(kInsertqWordSite, true, 3, 4, 16, 16);
    const Bytes fullField = {0xF2, 0x0F, 0x78, 0xC8, 0x00, 0x00};
    require(Codegen::DecodeSse4a(fullField.data(), fullField.size()).Length == 64, "Zero length does not mean 64");
    const Bytes registerForm = {0x66, 0x45, 0x0F, 0x79, 0xCA};
    const auto decoded = Codegen::DecodeSse4a(registerForm.data(), registerForm.size());
    require(decoded.RegisterForm && !decoded.Insertq && decoded.Destination == 9 && decoded.Source == 10, "Register form operands were decoded incorrectly");
    const Bytes strayRex = {0x41, 0xF2, 0x0F, 0x79, 0xCA};
    const auto ignored = Codegen::DecodeSse4a(strayRex.data(), strayRex.size());
    require(ignored.RegisterForm && ignored.Insertq && ignored.Destination == 1 && ignored.Source == 2, "A REX before a legacy prefix was applied");
    requireFailure([] { const Bytes bytes = {0x66, 0x0F, 0x78, 0xCB, 0x08, 0x28}; (void)Codegen::DecodeSse4a(bytes.data(), bytes.size()); }, "EXTRQ with a non-zero reg field was accepted");
    requireFailure([] { const Bytes bytes = {0xF2, 0x0F, 0x78, 0x1B, 0x08, 0x08}; (void)Codegen::DecodeSse4a(bytes.data(), bytes.size()); }, "SSE4a memory operand was accepted");
    requireFailure([] { const Bytes bytes = {0xF2, 0x0F, 0x78, 0xC8, 0x20, 0x30}; (void)Codegen::DecodeSse4a(bytes.data(), bytes.size()); }, "Field beyond bit 64 was accepted");
}

void sha256Operands() {
    const auto check = [](const Bytes& site, const Codegen::Sha256Operation operation, const int dst, const int src) {
        const auto operands = Codegen::DecodeSha256(site.data(), site.size());
        require(operands.Operation == operation && operands.Destination == dst && operands.Source == src, "SHA-256 operands were decoded incorrectly");
    };
    check({0x0F, 0x38, 0xCB, 0xCA}, Codegen::Sha256Operation::Rnds2, 1, 2);
    check({0x45, 0x0F, 0x38, 0xCC, 0xE1}, Codegen::Sha256Operation::Msg1, 12, 9);
    check({0x44, 0x0F, 0x38, 0xCD, 0xC0}, Codegen::Sha256Operation::Msg2, 8, 0);
    requireFailure([] { const Bytes bytes = {0x0F, 0x38, 0xCC, 0x08}; (void)Codegen::DecodeSha256(bytes.data(), bytes.size()); }, "SHA-256 memory operand was accepted");
    requireFailure([] { const Bytes bytes = {0x66, 0x0F, 0x38, 0xCB, 0xCA}; (void)Codegen::DecodeSha256(bytes.data(), bytes.size()); }, "Prefixed 0F 38 CB was decoded as SHA-256");
    requireFailure([] { const Bytes bytes = {0x0F, 0x38, 0xC9, 0xCA}; (void)Codegen::DecodeSha256(bytes.data(), bytes.size()); }, "SHA-1 was decoded as SHA-256");
    check({0x41, 0x2E, 0x0F, 0x38, 0xCC, 0xCA}, Codegen::Sha256Operation::Msg1, 1, 2);
    check({0x2E, 0x41, 0x0F, 0x38, 0xCC, 0xCA}, Codegen::Sha256Operation::Msg1, 1, 10);
    const Bytes rounds = {0x0F, 0x38, 0xCB, 0xCA};
    require(Codegen::DecodedInstruction{rounds.data(), rounds.size()}.IsShaNi(), "SHA256RNDS2 is not recognised as SHA-NI");
}

void clzeroOperands() {
    const auto decode = [](const Bytes& bytes) { return Codegen::DecodeClzero(bytes.data(), bytes.size()); };
    require(!decode({0x0F, 0x01, 0xFC}).AddressSize32 && decode({0x67, 0x0F, 0x01, 0xFC}).AddressSize32, "CLZERO address size was decoded incorrectly");
    require(!decode({0x3E, 0x48, 0x0F, 0x01, 0xFC}).AddressSize32, "CLZERO with a DS override and REX was not decoded");
    requireFailure([&] { (void)decode({0xF0, 0x0F, 0x01, 0xFC}); }, "LOCK CLZERO was accepted");
    requireFailure([&] { (void)decode({0x64, 0x0F, 0x01, 0xFC}); }, "FS-relative CLZERO was accepted");
    requireFailure([&] { (void)decode({0x65, 0x0F, 0x01, 0xFC}); }, "GS-relative CLZERO was accepted");
    Bytes overlong(13, 0x2E);
    overlong.insert(overlong.end(), {0x0F, 0x01, 0xFC});
    requireFailure([&] { (void)decode(overlong); }, "CLZERO longer than 15 bytes was accepted");
}

void matcherSubstitutions() {
    const auto matcher = Codegen::MakeAmd64OnlyInstructionMatcher();
    const auto match = [&](const Bytes& bytes) { return matcher->Match(bytes.data(), bytes.size()); };
    const auto movntss = match({0xF3, 0x0F, 0x2B, 0x07});
    require(movntss && movntss->Lowering == Codegen::Amd64OnlyLowering::InPlace && movntss->ReplacementBytes == Bytes{0xF3, 0x0F, 0x11, 0x07} && movntss->InstructionName == "MOVNTSS", "MOVNTSS was not rewritten to MOVSS");
    const auto movntsd = match({0xF2, 0x44, 0x0F, 0x2B, 0x4C, 0x24, 0x10});
    require(movntsd && movntsd->Lowering == Codegen::Amd64OnlyLowering::InPlace && movntsd->ReplacementBytes == Bytes{0xF2, 0x44, 0x0F, 0x11, 0x4C, 0x24, 0x10} && movntsd->InstructionName == "MOVNTSD", "MOVNTSD was not rewritten to MOVSD");
    requireFailure([&] { (void)match({0xF3, 0x0F, 0x2B, 0xC1}); }, "MOVNTSS with a register operand was accepted");
    const auto monitorx = match({0x0F, 0x01, 0xFA});
    require(monitorx && monitorx->Lowering == Codegen::Amd64OnlyLowering::InPlace && monitorx->ReplacementBytes == Bytes{0x0F, 0x1F, 0x00} && monitorx->InstructionName == "MONITORX", "MONITORX was not replaced by a NOP");
    const auto mwaitx = match({0x0F, 0x01, 0xFB});
    require(mwaitx && mwaitx->Lowering == Codegen::Amd64OnlyLowering::InPlace && mwaitx->ReplacementBytes == Bytes{0xF3, 0x90, 0x90} && mwaitx->InstructionName == "MWAITX", "MWAITX was not replaced by PAUSE");
    const auto prefixedMonitorx = match({0x67, 0x0F, 0x01, 0xFA});
    require(prefixedMonitorx && prefixedMonitorx->ReplacementBytes == Bytes{0x0F, 0x1F, 0x40, 0x00}, "Prefixed MONITORX was not padded to its length");
    const auto prefixedMwaitx = match({0x2E, 0x41, 0x0F, 0x01, 0xFB});
    require(prefixedMwaitx && prefixedMwaitx->ReplacementBytes == Bytes{0xF3, 0x90, 0x0F, 0x1F, 0x00}, "Prefixed MWAITX was not padded to its length");
    Bytes longMonitorx(5, 0x2E);
    longMonitorx.insert(longMonitorx.end(), {0x0F, 0x01, 0xFA});
    const auto paddedMonitorx = match(longMonitorx);
    require(paddedMonitorx && paddedMonitorx->ReplacementBytes == Bytes{0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x90}, "8-byte MONITORX was not padded with two NOPs");
    Bytes longestMwaitx(12, 0x2E);
    longestMwaitx.insert(longestMwaitx.end(), {0x0F, 0x01, 0xFB});
    const auto paddedMwaitx = match(longestMwaitx);
    require(paddedMwaitx && paddedMwaitx->ReplacementBytes == Bytes{0xF3, 0x90, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00}, "15-byte MWAITX was not padded to its length");
    const auto lockedMwaitx = match({0xF0, 0x0F, 0x01, 0xFB});
    require(lockedMwaitx && lockedMwaitx->Lowering == Codegen::Amd64OnlyLowering::Unsupported, "LOCK MWAITX was replaced instead of failing");
    Bytes overlongMonitorx(13, 0x2E);
    overlongMonitorx.insert(overlongMonitorx.end(), {0x0F, 0x01, 0xFA});
    const auto overlong = match(overlongMonitorx);
    require(overlong && overlong->Lowering == Codegen::Amd64OnlyLowering::Unsupported, "MONITORX longer than 15 bytes was replaced instead of failing");
    const auto clzero = match({0x0F, 0x01, 0xFC});
    require(clzero && clzero->Lowering == Codegen::Amd64OnlyLowering::Trampoline && clzero->InstructionName == "CLZERO", "CLZERO was not lowered through a stub");
    for (const std::uint8_t prefix : {std::uint8_t{0x66}, std::uint8_t{0xF2}, std::uint8_t{0xF3}}) {
        const auto prefixedClzero = match({prefix, 0x0F, 0x01, 0xFC});
        require(prefixedClzero && prefixedClzero->Lowering == Codegen::Amd64OnlyLowering::Unsupported, "CLZERO with a 66, F2 or F3 prefix was not reported as unsupported");
    }
    const auto rdpru = match({0x0F, 0x01, 0xFD});
    require(rdpru && rdpru->Lowering == Codegen::Amd64OnlyLowering::Unsupported && rdpru->InstructionName == "RDPRU", "RDPRU was not reported as unsupported");
    const auto registerForm = match({0x66, 0x0F, 0x79, 0xCA});
    require(registerForm && registerForm->Lowering == Codegen::Amd64OnlyLowering::Trampoline && registerForm->InstructionName == "EXTRQ register form", "EXTRQ register form was not lowered through a stub");
    const auto insertqRegisterForm = match({0xF2, 0x0F, 0x79, 0xCA});
    require(insertqRegisterForm && insertqRegisterForm->Lowering == Codegen::Amd64OnlyLowering::Trampoline && insertqRegisterForm->InstructionName == "INSERTQ register form", "INSERTQ register form was not lowered through a stub");
    require(!match({0x66, 0x0F, 0x2B, 0x07}) && !match({0x0F, 0x2B, 0x07}) && !match({0x48, 0x8B, 0x05, 0, 0, 0, 0}), "Ordinary instruction was matched");
    for (const auto& [bytes, name] : {std::pair{Bytes{0x0F, 0x38, 0xCB, 0xCA}, "SHA256RNDS2"}, {Bytes{0x0F, 0x38, 0xCC, 0xCA}, "SHA256MSG1"}, {Bytes{0x45, 0x0F, 0x38, 0xCD, 0xE1}, "SHA256MSG2"}}) {
        const auto sha256 = match(bytes);
        require(sha256 && sha256->Lowering == Codegen::Amd64OnlyLowering::Trampoline && sha256->InstructionName == name, "SHA-256 instruction was not lowered through a stub");
    }
    require(!match({0x0F, 0x38, 0xC9, 0xCA}), "SHA-1 instruction was matched");
    const auto stub = match(kInsertqHighSite);
    require(stub && stub->Lowering == Codegen::Amd64OnlyLowering::Trampoline && stub->StubBody == kInsertqHighBody && stub->ReturnBranchOffset == 15 && stub->InstructionName == "INSERTQ", "INSERTQ was not lowered through a stub");
    const auto shiftInPlace = match({0x66, 0x0F, 0x78, 0xC3, 0x18, 0x28});
    require(shiftInPlace && shiftInPlace->Lowering == Codegen::Amd64OnlyLowering::InPlace && shiftInPlace->ReplacementBytes == Bytes{0x66, 0x0F, 0x73, 0xD3, 0x28, 0x90}, "Top-aligned EXTRQ was not lowered in place");
}

void goldenBodies() {
    const Codegen::Sse4aLowering lowering;
    const auto outOfLine = [&](const Bytes& site, const Bytes& expected, const std::size_t returnBranchOffset) {
        const auto operands = Codegen::DecodeSse4a(site.data(), site.size());
        require(!lowering.LowerInPlace(operands, site.size()).has_value(), "Demon's Souls site unexpectedly qualified for an in-place lowering");
        const auto body = lowering.LowerOutOfLine(operands);
        require(body.ReturnBranchOffset == returnBranchOffset, "Stub return branch is at the wrong offset");
        require(body.Bytes == expected, "Stub body differs from the golden encoding");
    };
    outOfLine(kExtrqSite, kExtrqBody, 9);
    outOfLine(kInsertqSelfSite, kInsertqSelfBody, 9);
    outOfLine(kInsertqCrossSite, kInsertqCrossBody, 13);
    outOfLine(kInsertqHighSite, kInsertqHighBody, 15);
    outOfLine(kInsertqWordSite, kInsertqWordBody, 13);
    const auto inPlace = [&](const Bytes& site, const Bytes& expected) {
        const auto operands = Codegen::DecodeSse4a(site.data(), site.size());
        const auto sequence = lowering.LowerInPlace(operands, site.size());
        require(sequence.has_value() && *sequence == expected, "In-place lowering differs from the golden encoding");
    };
    inPlace({0xF2, 0x0F, 0x78, 0xC8, 0x00, 0x00}, {0xF3, 0x0F, 0x7E, 0xC8, 0x66, 0x90});
    inPlace({0x66, 0x0F, 0x78, 0xC3, 0x18, 0x28}, {0x66, 0x0F, 0x73, 0xD3, 0x28, 0x90});
    inPlace({0x66, 0x0F, 0x78, 0xC3, 0x08, 0x00}, {0x66, 0x0F, 0x38, 0x32, 0xDB, 0x90});
    inPlace({0xF2, 0x0F, 0x78, 0xDB, 0x08, 0x00}, {0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00});
    inPlace({0xF2, 0x0F, 0x78, 0xC8, 0x20, 0x00}, {0x66, 0x0F, 0x3A, 0x0E, 0xC8, 0x03});
    inPlace({0xF2, 0x45, 0x0F, 0x78, 0xC8, 0x10, 0x00}, {0x66, 0x45, 0x0F, 0x3A, 0x0E, 0xC8, 0x01});
    const Bytes clzeroSite = {0x0F, 0x01, 0xFC};
    const auto clzero = Codegen::ClzeroLowering{}.LowerOutOfLine(Codegen::DecodeClzero(clzeroSite.data(), clzeroSite.size()));
    require(clzero.Bytes == kClzeroBody && clzero.ReturnBranchOffset == 69, "CLZERO stub differs from the golden encoding");
    const auto highRegisters = Codegen::DecodeSse4a(kInsertqHighSite.data(), kInsertqHighSite.size());
    const auto generic = lowering.LowerOutOfLine(Codegen::Sse4aOperands{true, false, 9, 4, 5, 3});
    require(generic.Bytes[0] == 0x48 && generic.Bytes.size() % 16 == 0 && generic.ReturnBranchOffset < generic.Bytes.size(), "Generic INSERTQ body does not start with the red-zone skip");
    (void)highRegisters;
    const auto insertqRegisterForm = lowering.LowerOutOfLine(Codegen::Sse4aOperands{true, true, 1, 2, 0, 0});
    require(insertqRegisterForm.Bytes[0] == 0x48 && insertqRegisterForm.Bytes.size() % 16 == 0 && insertqRegisterForm.ReturnBranchOffset < insertqRegisterForm.Bytes.size(), "INSERTQ register form body does not start with the red-zone skip");
}

Bytes segmentFixture() {
    Bytes file(0x300, 0xCC);
    const Bytes text = {
        0xF3, 0x0F, 0xB8, 0xC0,
        0xCD, 0x41,
        0xEB, 0x07,
        0xF2, 0x44, 0x0F, 0x78, 0xCC, 0x10, 0x10,
        0xF3, 0x0F, 0x2B, 0x07,
        0xC3};
    std::copy(text.begin(), text.end(), file.begin() + 0x200);
    return file;
}

Domain::ProgramHeader segmentHeader(const std::uint64_t size) {
    return {1, 5, 0x200, 0x1000, 0, size, size, 16};
}

void converterSegment() {
    const auto converter = Codegen::MakeAmd64OnlyConverter();
    const auto file = segmentFixture();
    const auto result = converter->Convert(file, {segmentHeader(20)});
    require(result.ReplacedCount == 1 && result.Reports.size() == 2 && result.Trampolines.size() == 1, "Converter did not classify the segment's AMD-only instructions");
    const auto& site = result.Trampolines[0];
    require(site.Offset == 0x208 && site.Address == 0x1008 && site.Length == 7 && site.OriginalBytes == kInsertqHighSite && site.Body == kInsertqHighBody && site.ReturnBranchOffset == 15, "Trampoline site was recorded incorrectly");
    require(result.Reports[0].InstructionName == "INSERTQ" && result.Reports[0].Offset == 0x208 && result.Reports[0].Lowering == Codegen::Amd64OnlyLowering::Trampoline && result.Reports[0].ReplacementLength == 48, "Trampoline report is wrong");
    require(result.Reports[1].InstructionName == "MOVNTSS" && result.Reports[1].Offset == 0x20F && result.Reports[1].Lowering == Codegen::Amd64OnlyLowering::InPlace && result.Reports[1].ReplacementLength == 4, "In-place report is wrong");
    auto expected = file;
    expected[0x211] = 0x11;
    require(result.Bytes == expected, "Converter changed bytes other than the MOVNTSS opcode");
    const auto untouched = converter->Convert(Bytes(0x300, 0x90), {segmentHeader(0x100)});
    require(untouched.ReplacedCount == 0 && untouched.Trampolines.empty() && untouched.Reports.empty() && untouched.Bytes == Bytes(0x300, 0x90), "Segment without AMD-only instructions was changed");
    auto branchInside = file;
    branchInside[0x207] = 0x02;
    requireFailure([&] { (void)converter->Convert(branchInside, {segmentHeader(20)}); }, "Branch into an AMD-only instruction was accepted");
    auto rdpru = file;
    rdpru[0x20F] = 0x0F;
    rdpru[0x210] = 0x01;
    rdpru[0x211] = 0xFD;
    rdpru[0x212] = 0x90;
    requireFailure([&] { (void)converter->Convert(rdpru, {segmentHeader(20)}); }, "RDPRU was silently kept");
    auto registerForm = file;
    const Bytes extrqRegister = {0x66, 0x0F, 0x79, 0xCA};
    std::copy(extrqRegister.begin(), extrqRegister.end(), registerForm.begin() + 0x20F);
    requireFailure([&] { (void)converter->Convert(registerForm, {segmentHeader(20)}); }, "Short EXTRQ followed by a return was relocated");
    registerForm[0x213] = 0x90;
    const auto relocated = converter->Convert(registerForm, {segmentHeader(20)});
    require(relocated.Trampolines.size() == 2, "Short EXTRQ register form was not lowered through a stub");
    const auto& shortSite = relocated.Trampolines[1];
    const Bytes shortOriginal = {0x66, 0x0F, 0x79, 0xCA, 0x90};
    require(shortSite.Offset == 0x20F && shortSite.Length == 5 && shortSite.OriginalBytes == shortOriginal, "Short EXTRQ site did not absorb the following instruction");
    require(shortSite.Body[shortSite.ReturnBranchOffset - 1] == 0x90 && shortSite.Body[shortSite.ReturnBranchOffset] == 0xE9, "Absorbed instruction does not run before the return jump");
    requireFailure([&] { (void)converter->Convert(file, {segmentHeader(0x200)}); }, "Segment exceeding the file was accepted");
}

void converterSha256() {
    const auto converter = Codegen::MakeAmd64OnlyConverter();
    Bytes file(0x300, 0xCC);
    const Bytes text = {
        0x0F, 0x38, 0xCB, 0xCA,
        0x0F, 0x38, 0xCC, 0xD3,
        0x66, 0x0F, 0xFE, 0xC1,
        0x0F, 0x38, 0xCD, 0xE5,
        0x90,
        0xC3};
    std::copy(text.begin(), text.end(), file.begin() + 0x200);
    const auto result = converter->Convert(file, {segmentHeader(text.size())});
    require(result.Trampolines.size() == 2 && result.Reports.size() == 2 && result.Bytes == file, "SHA-256 sites were not lowered through stubs");
    const auto& rounds = result.Trampolines[0];
    require(rounds.Offset == 0x200 && rounds.Length == 8 && result.Reports[0].InstructionName == "SHA256RNDS2", "SHA256RNDS2 did not absorb the following SHA256MSG1");
    const auto& message = result.Trampolines[1];
    require(message.Offset == 0x20C && message.Length == 5 && result.Reports[1].InstructionName == "SHA256MSG2", "SHA256MSG2 did not absorb the following instruction");
    require(message.Body[message.ReturnBranchOffset - 1] == 0x90 && message.Body[message.ReturnBranchOffset] == 0xE9, "Absorbed instruction does not run before the return jump");
    auto beforeReturn = file;
    beforeReturn[0x210] = 0xC3;
    requireFailure([&] { (void)converter->Convert(beforeReturn, {segmentHeader(text.size())}); }, "Short SHA-256 instruction followed by a return was relocated");
    auto memoryForm = file;
    memoryForm[0x20F] = 0x28;
    require(failureOffset([&] { (void)converter->Convert(memoryForm, {segmentHeader(text.size())}); }, "SHA-256 memory form was accepted") == 0x20C, "SHA-256 operand failure does not carry the file offset");
}

void converterMonitorWait() {
    const auto converter = Codegen::MakeAmd64OnlyConverter();
    Bytes file(0x300, 0xCC);
    const Bytes text = {0x0F, 0x01, 0xFA, 0x0F, 0x01, 0xFB, 0x2E, 0x0F, 0x01, 0xFB, 0xC3};
    std::copy(text.begin(), text.end(), file.begin() + 0x200);
    const auto result = converter->Convert(file, {segmentHeader(text.size())});
    require(result.ReplacedCount == 3 && result.Trampolines.empty() && result.Reports.size() == 3, "MONITORX/MWAITX were not replaced in place");
    require(result.Reports[0].InstructionName == "MONITORX" && result.Reports[1].InstructionName == "MWAITX" && result.Reports[2].Offset == 0x206 && result.Reports[2].ReplacementLength == 4 && result.Reports[2].Lowering == Codegen::Amd64OnlyLowering::InPlace, "MONITORX/MWAITX reports are wrong");
    auto expected = file;
    const Bytes replaced = {0x0F, 0x1F, 0x00, 0xF3, 0x90, 0x90, 0xF3, 0x90, 0x66, 0x90, 0xC3};
    std::copy(replaced.begin(), replaced.end(), expected.begin() + 0x200);
    require(result.Bytes == expected, "MONITORX/MWAITX were replaced with the wrong bytes");
}

void converterClzero() {
    const auto converter = Codegen::MakeAmd64OnlyConverter();
    Bytes file(0x300, 0xCC);
    const Bytes text = {
        0x0F, 0x01, 0xFC,
        0x48, 0x83, 0xC0, 0x40,
        0x0F, 0x01, 0xFC,
        0x0F, 0x01, 0xFC,
        0x90,
        0xC3};
    std::copy(text.begin(), text.end(), file.begin() + 0x200);
    const auto result = converter->Convert(file, {segmentHeader(text.size())});
    require(result.Trampolines.size() == 2 && result.Reports.size() == 2 && result.Bytes == file, "CLZERO sites were not lowered through stubs");
    const auto& advance = result.Trampolines[0];
    require(advance.Offset == 0x200 && advance.Length == 7 && result.Reports[0].InstructionName == "CLZERO", "CLZERO did not absorb the following instruction");
    const Bytes add = {0x48, 0x83, 0xC0, 0x40};
    require(Bytes(advance.Body.begin() + static_cast<std::ptrdiff_t>(advance.ReturnBranchOffset - add.size()), advance.Body.begin() + static_cast<std::ptrdiff_t>(advance.ReturnBranchOffset)) == add, "Absorbed instruction does not run before the return jump");
    const auto& pair = result.Trampolines[1];
    require(pair.Offset == 0x207 && pair.Length == 6 && result.Reports[1].InstructionName == "CLZERO", "Consecutive CLZERO sites were not lowered into one stub");
    auto beforeReturn = file;
    beforeReturn[0x203] = 0xC3;
    requireFailure([&] { (void)converter->Convert(beforeReturn, {segmentHeader(text.size())}); }, "Short CLZERO followed by a return was relocated");
    auto segmentRelative = file;
    const Bytes fsClzero = {0x64, 0x0F, 0x01, 0xFC};
    std::copy(fsClzero.begin(), fsClzero.end(), segmentRelative.begin() + 0x20A);
    require(failureOffset([&] { (void)converter->Convert(segmentRelative, {segmentHeader(text.size())}); }, "FS-relative CLZERO was accepted") == 0x20A, "CLZERO operand failure does not carry the file offset");
}

void converterStrayRex() {
    const auto converter = Codegen::MakeAmd64OnlyConverter();
    const auto convert = [&](const Bytes& text) {
        Bytes file(0x300, 0xCC);
        std::copy(text.begin(), text.end(), file.begin() + 0x200);
        return std::pair{file, converter->Convert(file, {segmentHeader(text.size())})};
    };
    for (const Bytes& instruction : {Bytes{0x48, 0x64, 0x0F, 0x01, 0xFC, 0x90, 0x90, 0xC3}, Bytes{0x48, 0xF0, 0x0F, 0x01, 0xFC, 0x90, 0x90, 0xC3}})
        require(failureOffset([&] { (void)convert(instruction); }, "CLZERO with a FS or LOCK prefix after a stray REX was accepted") == 0x200, "Stray REX failure does not carry the file offset");
    const auto [addressFile, addressSize32] = convert({0x48, 0x67, 0x0F, 0x01, 0xFC, 0xC3});
    require(addressSize32.Trampolines.size() == 1 && addressSize32.Trampolines[0].Length == 5 && addressSize32.Reports[0].InstructionName == "CLZERO", "67h CLZERO after a stray REX was not lowered as one instruction");
    const auto [movntsFile, movnts] = convert({0x48, 0xF3, 0x0F, 0x2B, 0x00, 0xC3});
    auto expected = movntsFile;
    expected[0x203] = 0x11;
    require(movnts.ReplacedCount == 1 && movnts.Reports[0].InstructionName == "MOVNTSS" && movnts.Bytes == expected, "MOVNTSS after a stray REX was not rewritten");
    for (const Bytes& following : {Bytes{0x48, 0x64, 0x8B, 0x00}, Bytes{0x48, 0x67, 0x8B, 0x00}, Bytes{0x48, 0xF0, 0x01, 0x00}}) {
        Bytes text = {0x0F, 0x01, 0xFC};
        text.insert(text.end(), following.begin(), following.end());
        text.push_back(0xC3);
        const auto [file, result] = convert(text);
        require(result.Trampolines.size() == 1 && result.Trampolines[0].Length == 7, "Instruction with a stray REX was not absorbed whole");
        const auto& body = result.Trampolines[0].Body;
        const auto returnBranch = result.Trampolines[0].ReturnBranchOffset;
        require(Bytes(body.begin() + static_cast<std::ptrdiff_t>(returnBranch - following.size()), body.begin() + static_cast<std::ptrdiff_t>(returnBranch)) == following, "Absorbed instruction lost its prefixes");
    }
}

void rewriterStrayRex() {
    const Bytes code = {0x48, 0x2E, 0xE9, 0x01, 0x00, 0x00, 0x00, 0x90, 0xC3};
    const auto rewritten = Codegen::X64InstructionRewriter{}.Rewrite(code, {7, {0x66, 0x90}});
    const Bytes expected = {0x48, 0x2E, 0xE9, 0x02, 0x00, 0x00, 0x00, 0x66, 0x90, 0xC3};
    require(rewritten.Bytes == expected, "Branch with a stray REX was not adjusted by a length-changing rewrite");
}

void converterFailureOffsets() {
    const auto converter = Codegen::MakeAmd64OnlyConverter();
    const auto file = segmentFixture();
    auto undecodable = file;
    const Bytes bareSse4a = {0x0F, 0x78, 0xC0, 0x00};
    std::copy(bareSse4a.begin(), bareSse4a.end(), undecodable.begin() + 0x20F);
    require(failureOffset([&] { (void)converter->Convert(undecodable, {segmentHeader(20)}); }, "Undecodable instruction was accepted") == 0x20F, "Decoder failure does not carry the file offset");
    auto memoryForm = file;
    memoryForm[0x20C] = 0x08;
    require(failureOffset([&] { (void)converter->Convert(memoryForm, {segmentHeader(20)}); }, "INSERTQ memory form was accepted") == 0x208, "SSE4a operand failure does not carry the file offset");
    auto movntsRegister = file;
    movntsRegister[0x212] = 0xC1;
    require(failureOffset([&] { (void)converter->Convert(movntsRegister, {segmentHeader(20)}); }, "MOVNTSS register form was accepted") == 0x20F, "MOVNTSS failure does not carry the file offset");
    auto rdpru = file;
    const Bytes rdpruBytes = {0x0F, 0x01, 0xFD, 0x90};
    std::copy(rdpruBytes.begin(), rdpruBytes.end(), rdpru.begin() + 0x20F);
    require(failureOffset([&] { (void)converter->Convert(rdpru, {segmentHeader(20)}); }, "RDPRU was accepted") == 0x20F, "Unsupported instruction failure does not carry the file offset");
}




void scannerZeroTail() {
    const auto scanner = Codegen::MakeInstructionScanner();
    const Bytes code{0xC3, 0x00, 0x00, 0x00};
    require(scanner->ScanCodeSection(code, 0, code.size()).size() == 2, "Odd zero padding at segment tail must end the scan");
    const Bytes truncated{0xC3, 0x0F};
    requireFailure([&] { (void)scanner->ScanCodeSection(truncated, 0, truncated.size()); }, "Truncated non-zero tail must still fail");
}

} // namespace

int main() {
    try {
        decoderLengths();
        sse4aOperands();
        sha256Operands();
        clzeroOperands();
        matcherSubstitutions();
        goldenBodies();
        converterSegment();
        converterSha256();
        converterMonitorWait();
        converterClzero();
        converterStrayRex();
        rewriterStrayRex();
        converterFailureOffsets();
        scannerZeroTail();
        std::cout << "AMD64-only converter tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
