#include <cstddef>
#include <cstring>
#include <cerrno>

extern "C" int memset_s(void* dest, std::size_t destsz, int ch, std::size_t count) {
    if (dest == nullptr) {
        return EINVAL;
    }
    if (count > destsz) {
        std::memset(dest, ch, destsz);
        return EINVAL;
    }
    std::memset(dest, ch, count);
    return 0;
}
