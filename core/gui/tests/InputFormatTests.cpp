#include <InputFormat.hpp>

#include <array>
#include <cstdint>

int main() {
    using Gui::DetectInputFormat;
    using Gui::InputFormat;

    if (DetectInputFormat({0x7f, 'E', 'L', 'F'}) != InputFormat::PlainElf)
        return 1;

    if (DetectInputFormat({0x4f, 0x15, 0x3d, 0x1d}) != InputFormat::Self)
        return 2;

    if (DetectInputFormat({0x7f, 'C', 'N', 'T'}) != InputFormat::Unknown)
        return 3;

    if (DetectInputFormat({0x00, 0x00, 0x00, 0x00}) != InputFormat::Unknown)
        return 4;

    return 0;
}
