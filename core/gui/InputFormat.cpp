#include <InputFormat.hpp>

namespace Gui {

InputFormat DetectInputFormat(const std::array<std::uint8_t, 4>& magic) {
    if (magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F')
        return InputFormat::PlainElf;

    if (magic[0] == 0x4f && magic[1] == 0x15 && magic[2] == 0x3d && magic[3] == 0x1d)
        return InputFormat::Self;

    return InputFormat::Unknown;
}

}
