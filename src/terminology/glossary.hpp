#pragma once
// Glossary: file CSV (cột term_en, term_vi, keep_english, notes, source; tùy chọn aliases,
// explanation) và chỉ mục tra cứu nhiều tầng.
//
// aliases: tên gọi khác, cách nhau bằng '|', dạng "tên=nghĩa" hoặc "tên" (viết tắt / ký hiệu
// đồng vị: giữ nguyên khi dịch câu). explanation: giải thích & công thức (chỉ hiện khi tra
// một thuật ngữ đơn lẻ, không đưa vào prompt).
//
// Thứ tự ưu tiên khi cùng một thuật ngữ có ở nhiều nơi (CLAUDE.md mục 5.1):
//   glossary người dùng > glossary của tài liệu > lĩnh vực điểm cao nhất > các lĩnh vực khác.
// Thuật ngữ chưa có bản dịch (term_vi rỗng, không keep_english) là "chưa xác nhận": không
// đưa vào prompt. Thuật ngữ có nghĩa khác ở các lĩnh vực khác -> trả kèm nghĩa thay thế.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "text/types.hpp"

namespace pdfread::terminology {

// Unrelated: glossary của lĩnh vực không thuộc tài liệu — chỉ dùng làm "nghĩa khác" khi
// tra từ, KHÔNG đưa vào prompt.
enum class Layer : int { User = 0, Document = 1, PrimaryDomain = 2, OtherDomain = 3, Unrelated = 4 };

struct GlossaryEntry {
    std::string term_en;
    std::string term_vi;
    bool keep_english = false;
    std::string notes;
    std::string source;     // "draft", "verified", "user", "extracted"…
    std::string domain_id;  // lĩnh vực con (rỗng với glossary người dùng / tài liệu)
    Layer layer = Layer::OtherDomain;
    int rank = 0;           // thứ tự trong cùng tầng (lĩnh vực điểm cao hơn -> nhỏ hơn)
    std::vector<std::string> aliases;  // "tên=nghĩa" hoặc "tên"
    std::string explanation;           // giải thích & công thức (Unicode)
    std::string alias_of;              // khác rỗng: mục ảo do chỉ mục tạo từ tên gọi khác của thuật ngữ này

    // Bản dịch dùng trong prompt / hậu kiểm.
    [[nodiscard]] const std::string& target() const noexcept { return keep_english ? term_en : term_vi; }
    [[nodiscard]] bool confirmed() const noexcept { return keep_english || !term_vi.empty(); }
};

// --- CSV ---------------------------------------------------------------------

// Tách CSV (RFC 4180: dấu phẩy, ngoặc kép, "" trong ngoặc kép, xuống dòng trong ô).
[[nodiscard]] std::vector<std::vector<std::string>> parse_csv(std::string_view data);
[[nodiscard]] std::string to_csv(const std::vector<std::vector<std::string>>& rows);

// Đọc file glossary; dòng đầu là tiêu đề (bắt buộc có term_en, term_vi; các cột khác tùy
// chọn, theo tên). Ném std::runtime_error khi không đọc được file.
[[nodiscard]] std::vector<GlossaryEntry> load_glossary_csv(const std::filesystem::path& path);
[[nodiscard]] std::vector<GlossaryEntry> parse_glossary_csv(std::string_view data);
void save_glossary_csv(const std::filesystem::path& path, const std::vector<GlossaryEntry>& entries);

// Tách một tên gọi khác "tên=nghĩa" -> (tên, nghĩa); không có '=' -> nghĩa rỗng.
[[nodiscard]] std::pair<std::string, std::string> split_alias(const std::string& alias);

// --- Chỉ mục -------------------------------------------------------------------

struct TermMatch {
    text::Span span;                             // vị trí trong text đã khớp
    const GlossaryEntry* entry = nullptr;        // bản được chọn theo thứ tự ưu tiên
    std::vector<const GlossaryEntry*> alternatives;  // nghĩa khác (lĩnh vực khác) có term_vi khác
};

class GlossaryIndex {
public:
    GlossaryIndex() = default;
    // `entries` đã gán layer/rank. Giữ bản sao; mỗi tên gọi khác thành một mục ảo (alias_of
    // = term_en gốc) để tra và khớp trong câu như thuật ngữ thường.
    explicit GlossaryIndex(std::vector<GlossaryEntry> entries);

    // Mọi thuật ngữ xuất hiện trong `text` (ưu tiên cụm dài nhất, không chồng lấn),
    // bỏ qua các vị trí nằm trong `excluded` (công thức, mã…).
    [[nodiscard]] std::vector<TermMatch> find_in(std::u32string_view text,
                                                 const std::vector<text::Span>& excluded = {}) const;

    // Tra đúng một thuật ngữ (cả chuỗi `term`, không phân biệt hoa/thường, chấp nhận số nhiều).
    [[nodiscard]] std::optional<TermMatch> lookup(std::u32string_view term) const;

    [[nodiscard]] const std::vector<GlossaryEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Key {
        std::vector<std::u32string> words;
        std::vector<std::size_t> entries;  // chỉ số trong entries_, đã sắp theo ưu tiên
        // Viết tắt toàn chữ hoa ("IT", "USE", "CM", "LOCA"): chỉ khớp đúng chữ hoa trong văn bản,
        // để "it", "use", "cm" không bị coi là thuật ngữ.
        bool case_sensitive = false;
        std::u32string exact;
    };
    [[nodiscard]] TermMatch make_match(const Key& key, text::Span span) const;

    std::vector<GlossaryEntry> entries_;
    std::vector<Key> keys_;                                         // mỗi term_en (đã chuẩn hóa) một key
    std::unordered_map<std::u32string, std::vector<std::size_t>> by_first_word_;  // từ đầu -> keys (dài trước)
};

// Hậu kiểm: các thuật ngữ đã chỉ định mà bản dịch không dùng đúng (so sánh không phân
// biệt hoa/thường, bỏ khác biệt khoảng trắng).
[[nodiscard]] std::vector<GlossaryEntry> check_terms(std::string_view translation,
                                                     const std::vector<GlossaryEntry>& required);

}  // namespace pdfread::terminology
