#include "util/thread_name.hpp"

#include "util/log.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace pdfread {

void name_current_thread(std::string name) {
#ifdef _WIN32
    const int len = MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
    if (len > 0) {
        std::wstring wide(static_cast<std::size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wide.data(), len);
        SetThreadDescription(GetCurrentThread(), wide.c_str());
    }
#endif
    log::set_thread_name(std::move(name));
}

}  // namespace pdfread
