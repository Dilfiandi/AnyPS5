#ifndef ANYPS5_GUI_INPUTFORMAT_HPP
#define ANYPS5_GUI_INPUTFORMAT_HPP

#include <array>
#include <cstdint>

namespace Gui {

enum class InputFormat {
    PlainElf,
    Self,
    Unknown
};

InputFormat DetectInputFormat(const std::array<std::uint8_t, 4>& magic);

}

#endif
