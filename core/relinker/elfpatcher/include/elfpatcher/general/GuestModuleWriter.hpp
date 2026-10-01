#ifndef ELFPATCHER_GUESTMODULEWRITER_HPP
#define ELFPATCHER_GUESTMODULEWRITER_HPP

#include <relinker/guest/GuestImage.hpp>

namespace Elfpatcher {

class GuestModuleWriter {
public:
    std::vector<std::uint8_t> WriteWindows(const Relinker::GuestImage& image, Domain::GuestRuntime& runtime) const;
};

}

#endif
