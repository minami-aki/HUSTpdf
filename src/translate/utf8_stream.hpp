#pragma once
// Ghép các mảnh byte do mô hình sinh ra (một token có thể chỉ chứa nửa ký tự UTF-8,
// ví dụ một phần của chữ "ệ") thành các đoạn UTF-8 trọn vẹn để hiển thị dần.

#include <string>
#include <string_view>

namespace pdfread::translate {

class Utf8Stream {
public:
    // Thêm byte; trả về phần đầu dài nhất kết thúc ở biên ký tự, giữ lại phần dở dang.
    [[nodiscard]] std::string push(std::string_view bytes) {
        pending_.append(bytes);
        const std::size_t cut = complete_prefix(pending_);
        std::string out = pending_.substr(0, cut);
        pending_.erase(0, cut);
        return out;
    }

    // Trả phần còn lại (có thể không hợp lệ nếu mô hình dừng giữa ký tự).
    [[nodiscard]] std::string flush() {
        std::string out;
        out.swap(pending_);
        return out;
    }

private:
    static std::size_t complete_prefix(std::string_view s) {
        // Tìm byte dẫn đầu của ký tự cuối (tối đa lùi 3 byte tiếp nối).
        std::size_t i = s.size();
        int back = 0;
        while (i > 0 && back < 4) {
            const auto c = static_cast<unsigned char>(s[i - 1]);
            if ((c & 0xC0) != 0x80) {  // byte dẫn đầu hoặc ASCII
                const std::size_t need = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
                const std::size_t have = s.size() - (i - 1);
                return have >= need ? s.size() : i - 1;
            }
            --i;
            ++back;
        }
        return s.size();  // toàn byte tiếp nối lạc: trả nguyên trạng
    }

    std::string pending_;
};

}  // namespace pdfread::translate
