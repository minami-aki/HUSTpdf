// Tính năng đọc sách của App: thanh bên (mục lục, dấu trang, tìm kiếm), lịch sử điều hướng
// (Quay lại / Tiến tới), liên kết trong PDF, chế độ đọc (màu trang + theme giao diện).

#include "app/app.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include "text/search.hpp"
#include "ui/fonts.hpp"
#include "util/log.hpp"
#include "util/utf8.hpp"

namespace pdfread::app {

namespace {

constexpr std::size_t kHistoryLimit = 100;

bool is_web_uri(std::string_view uri) {
    return uri.starts_with("http://") || uri.starts_with("https://") || uri.starts_with("mailto:");
}

bool has_non_ascii_letter(std::string_view s) {
    return std::any_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80; });
}

}  // namespace

// --- điều hướng ----------------------------------------------------------------------

void App::navigate_to(ui::Location dest) {
    const ui::Location here = viewer_.location();
    if (back_.empty() || !(back_.back() == here)) back_.push_back(here);
    if (back_.size() > kHistoryLimit) back_.erase(back_.begin());
    forward_.clear();
    viewer_.go_to(dest.page, dest.y);
}

void App::go_back() {
    if (back_.empty() || !viewer_.document()) return;
    forward_.push_back(viewer_.location());
    const ui::Location dest = back_.back();
    back_.pop_back();
    viewer_.go_to(dest.page, dest.y);
}

void App::go_forward() {
    if (forward_.empty() || !viewer_.document()) return;
    back_.push_back(viewer_.location());
    const ui::Location dest = forward_.back();
    forward_.pop_back();
    viewer_.go_to(dest.page, dest.y);
}

void App::follow_link(const pdf::Link& link) {
    if (link.page >= 0) {
        log::debug("Liên kết -> trang {} (y={:.0f})", link.page + 1, link.y);
        navigate_to({link.page, link.y});
        return;
    }
    if (!is_web_uri(link.uri)) {
        set_status(std::format("Không mở được liên kết: {}", link.uri), true);
        return;
    }
#ifdef _WIN32
    const std::wstring wide = path_from_utf8(link.uri).wstring();
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
    set_status(std::format("Đã mở liên kết ngoài: {}", link.uri), false);
}

// --- thanh bên ---------------------------------------------------------------------------

void App::open_sidebar(SidebarTab tab) {
    sidebar_open_ = true;
    sidebar_select_ = tab;
    if (store_) store_->set_bool("ui.sidebar", true);
}

void App::draw_sidebar() {
    if (ImGui::BeginTabBar("##sidebar_tabs")) {
        auto flags = [&](SidebarTab t) {
            return sidebar_select_ && *sidebar_select_ == t ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        };
        if (ImGui::BeginTabItem("Mục lục", nullptr, flags(SidebarTab::Outline))) {
            draw_outline_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Dấu trang", nullptr, flags(SidebarTab::Bookmarks))) {
            draw_bookmarks_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Tìm kiếm", nullptr, flags(SidebarTab::Search))) {
            draw_search_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    sidebar_select_.reset();
}

int App::current_outline_item() const {
    const ui::Location here = viewer_.location();
    int best = -1;
    for (std::size_t i = 0; i < outline_.size(); ++i) {
        const pdf::OutlineItem& it = outline_[i];
        if (it.page < 0) continue;
        if (it.page < here.page || (it.page == here.page && std::max(0.0f, it.y) <= here.y + 40.0f)) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::string App::section_title(int page) const {
    std::string title;
    for (const pdf::OutlineItem& it : outline_) {
        if (it.page >= 0 && it.page <= page && !it.title.empty()) title = it.title;
    }
    return title;
}

void App::draw_outline_tab() {
    if (outline_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Tài liệu không có mục lục.");
        ImGui::PopTextWrapPos();
        return;
    }
    const int current = current_outline_item();
    const bool small = outline_.size() <= 40;
    ImGui::BeginChild("##outline", ImVec2(0, 0));
    const std::size_t n = outline_.size();
    std::function<std::size_t(std::size_t)> node = [&](std::size_t i) -> std::size_t {
        const pdf::OutlineItem& it = outline_[i];
        const bool has_children = i + 1 < n && outline_[i + 1].level > it.level;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (static_cast<int>(i) == current) flags |= ImGuiTreeNodeFlags_Selected;
        if (small && it.level == 0) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        const std::string label = it.title.empty() ? std::string("(không tên)") : it.title;
        const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::intptr_t>(i)), flags, "%s", label.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
            if (it.page >= 0) navigate_to({it.page, it.y});
            else if (!it.uri.empty()) follow_link(pdf::Link{.uri = it.uri});
        }
        if (it.page >= 0) ImGui::SetItemTooltip("Trang %d", it.page + 1);
        std::size_t j = i + 1;
        if (has_children) {
            if (open) {
                while (j < n && outline_[j].level > it.level) j = node(j);
                ImGui::TreePop();
            } else {
                while (j < n && outline_[j].level > it.level) ++j;
            }
        }
        return j;
    };
    for (std::size_t i = 0; i < n;) i = node(i);
    ImGui::EndChild();
}

void App::reload_bookmarks() {
    bookmarks_.clear();
    if (!store_ || doc_fingerprint_.empty()) return;
    try {
        bookmarks_ = store_->bookmarks(doc_fingerprint_);
    } catch (const std::exception& e) {
        log::warn("Không đọc được dấu trang: {}", e.what());
    }
}

void App::add_bookmark() {
    if (!store_ || doc_fingerprint_.empty() || !viewer_.document()) {
        set_status("Không lưu được dấu trang (không có cơ sở dữ liệu)", true);
        return;
    }
    const ui::Location here = viewer_.location();
    std::string title = section_title(here.page);
    title = title.empty() ? std::format("Trang {}", here.page + 1) : std::format("{} — tr. {}", title, here.page + 1);
    try {
        store_->add_bookmark(doc_fingerprint_, here.page, here.y, title);
        reload_bookmarks();
        set_status(std::format("Đã thêm dấu trang: {}", title), false);
    } catch (const std::exception& e) {
        set_status(std::format("Không lưu được dấu trang: {}", e.what()), true);
    }
}

void App::draw_bookmarks_tab() {
    const bool can = store_ && !doc_fingerprint_.empty();
    ImGui::BeginDisabled(!can);
    if (ImGui::Button("+ Thêm dấu trang")) add_bookmark();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Đánh dấu vị trí đang đọc (Ctrl+D)");
    if (bookmarks_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Chưa có dấu trang. Bấm Ctrl+D để đánh dấu vị trí đang đọc.");
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::BeginChild("##bookmarks", ImVec2(0, 0));
    std::optional<std::int64_t> remove;
    const float close_w = ImGui::CalcTextSize("×").x + ImGui::GetStyle().FramePadding.x * 2;
    for (const storage::Bookmark& b : bookmarks_) {
        ImGui::PushID(static_cast<int>(b.id));
        const float w = ImGui::GetContentRegionAvail().x - close_w - ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::Selectable(b.title.c_str(), false, ImGuiSelectableFlags_None, ImVec2(w, 0))) navigate_to({b.page, b.y});
        ImGui::SetItemTooltip("%s", b.title.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("×")) remove = b.id;
        ImGui::SetItemTooltip("Xóa dấu trang");
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (remove && store_) {
        try {
            store_->remove_bookmark(*remove);
        } catch (const std::exception& e) {
            log::warn("Không xóa được dấu trang: {}", e.what());
        }
        reload_bookmarks();
    }
}

// --- tìm kiếm -----------------------------------------------------------------------

void App::start_search() {
    const std::string q = search_query_;
    searched_query_ = q;
    search_hits_.clear();
    search_current_ = -1;
    search_terms_.clear();
    search_pages_done_ = 0;
    search_pages_total_ = viewer_.page_count();
    viewer_.clear_search_highlights();
    const std::u32string needle = text::normalize_query(to_u32(q));
    if (needle.empty() || !viewer_.document()) {
        search_.cancel();
        search_running_ = false;
        return;
    }
    std::vector<std::u32string> needles{needle};
    // Gõ tiếng Việt: tìm thêm các thuật ngữ tiếng Anh tương ứng trong glossary / từ điển.
    if (has_non_ascii_letter(q) && glossary_) {
        search_terms_ = glossary_->index()->english_for(to_u32(q));
        for (const std::string& t : search_terms_) needles.push_back(text::normalize_query(to_u32(t)));
    }
    search_start_page_ = viewer_.current_page();
    search_ticket_ = search_.start(std::move(needles));
    search_running_ = true;
    log::debug("Tìm \"{}\"{}", q, search_terms_.empty() ? "" : std::format(" (+{} thuật ngữ)", search_terms_.size()));
}

void App::poll_search() {
    for (pdf::SearchUpdate& u : search_.take_updates()) {
        if (u.ticket != search_ticket_) continue;
        const bool had = !search_hits_.empty();
        for (pdf::SearchHit& h : u.hits) search_hits_.push_back(std::move(h));
        search_pages_done_ = u.pages_done;
        search_pages_total_ = u.pages_total;
        if (u.finished) {
            search_running_ = false;
            log::info("SEARCH<<{}>> {} kết quả", searched_query_, search_hits_.size());
        }
        // Chọn kết quả đầu tiên từ trang đang đọc trở đi (tìm từ đầu tài liệu nếu không có).
        if (search_current_ < 0 && !search_hits_.empty()) {
            for (std::size_t i = 0; i < search_hits_.size(); ++i) {
                if (search_hits_[i].page >= search_start_page_) {
                    select_search_hit(static_cast<int>(i), true);
                    break;
                }
            }
            if (search_current_ < 0 && !search_running_) select_search_hit(0, true);
        }
        if (search_hits_.size() != (had ? search_hits_.size() : 0) || u.finished) update_search_highlights();
    }
}

void App::update_search_highlights() {
    std::unordered_map<int, std::vector<text::Box>> by_page;
    for (const pdf::SearchHit& h : search_hits_) {
        auto& v = by_page[h.page];
        v.insert(v.end(), h.rects.begin(), h.rects.end());
    }
    if (search_current_ >= 0 && search_current_ < static_cast<int>(search_hits_.size())) {
        const pdf::SearchHit& c = search_hits_[static_cast<std::size_t>(search_current_)];
        viewer_.set_search_highlights(std::move(by_page), c.page, c.rects);
    } else {
        viewer_.set_search_highlights(std::move(by_page), -1, {});
    }
}

void App::select_search_hit(int index, bool record_history) {
    if (search_hits_.empty()) return;
    const int n = static_cast<int>(search_hits_.size());
    search_current_ = ((index % n) + n) % n;
    const pdf::SearchHit& h = search_hits_[static_cast<std::size_t>(search_current_)];
    const float y = h.rects.empty() ? -1.0f : std::max(0.0f, h.rects.front().y0 - 60.0f);
    if (record_history) navigate_to({h.page, y});
    else viewer_.go_to(h.page, y);
    update_search_highlights();
}

void App::draw_search_tab() {
    const ImGuiStyle& style = ImGui::GetStyle();
    if (focus_search_) {
        ImGui::SetKeyboardFocusHere();
        focus_search_ = false;
    }
    const float arrows_w = (ImGui::GetFrameHeight() + style.ItemSpacing.x) * 2;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - arrows_w);
    const bool enter = ImGui::InputTextWithHint("##search", "Tìm trong tài liệu (Enter)", &search_query_,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    if (enter) {
        if (search_query_ == searched_query_ && !search_hits_.empty()) {
            select_search_hit(search_current_ + (ImGui::GetIO().KeyShift ? -1 : 1), false);
        } else {
            start_search();
        }
        focus_search_ = true;  // giữ con trỏ trong ô tìm
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(search_hits_.empty());
    if (ImGui::ArrowButton("##prev_hit", ImGuiDir_Up)) select_search_hit(search_current_ - 1, false);
    ImGui::SetItemTooltip("Kết quả trước (Shift+F3)");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next_hit", ImGuiDir_Down)) select_search_hit(search_current_ + 1, false);
    ImGui::SetItemTooltip("Kết quả tiếp (F3)");
    ImGui::EndDisabled();

    ImGui::PushTextWrapPos(0.0f);
    if (!search_terms_.empty()) {
        std::string terms;
        for (const std::string& t : search_terms_) terms += (terms.empty() ? "" : ", ") + t;
        ImGui::TextDisabled("Tìm cả thuật ngữ tiếng Anh: %s", terms.c_str());
    }
    if (search_running_) {
        ImGui::TextDisabled("Đang tìm… %d/%d trang · %zu kết quả", search_pages_done_, search_pages_total_,
                            search_hits_.size());
    } else if (!searched_query_.empty()) {
        if (search_hits_.empty()) ImGui::TextDisabled("Không tìm thấy.");
        else ImGui::TextDisabled("Kết quả %d / %zu", search_current_ + 1, search_hits_.size());
    } else {
        ImGui::TextDisabled("Gõ từ khóa tiếng Anh, hoặc tiếng Việt để tìm theo thuật ngữ trong glossary/từ điển. "
                            "Ctrl+F: tìm · F3 / Shift+F3: kết quả tiếp / trước.");
    }
    ImGui::PopTextWrapPos();

    ImGui::BeginChild("##search_results", ImVec2(0, 0));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(search_hits_.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const pdf::SearchHit& h = search_hits_[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            const std::string label = std::format("tr. {}  {}", h.page + 1, h.snippet);
            if (ImGui::Selectable(label.c_str(), i == search_current_)) select_search_hit(i, true);
            ImGui::SetItemTooltip("%s", label.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

// --- chế độ đọc ------------------------------------------------------------------------

void App::set_reading_mode(ui::ReadingMode mode, bool save) {
    reading_mode_ = mode;
    viewer_.set_page_colors(ui::page_colors(mode));
    ui::apply_style(ui_scale_, ui::is_dark(mode));
    if (save && store_) store_->set("ui.reading_mode", ui::reading_mode_key(mode));
}

// --- phím tắt --------------------------------------------------------------------------

void App::handle_reader_shortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    // Chuột: nút Back / Forward (nút bên hông).
    if (ImGui::IsMouseClicked(3)) go_back();
    if (ImGui::IsMouseClicked(4)) go_forward();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_LeftArrow)) go_back();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_RightArrow)) go_forward();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
        open_sidebar(SidebarTab::Search);
        focus_search_ = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F3) && !search_hits_.empty()) {
        select_search_hit(search_current_ + (io.KeyShift ? -1 : 1), false);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F4)) {
        sidebar_open_ = !sidebar_open_;
        if (store_) store_->set_bool("ui.sidebar", sidebar_open_);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F9)) {
        set_reading_mode(static_cast<ui::ReadingMode>((static_cast<int>(reading_mode_) + 1) % ui::kReadingModeCount), true);
        set_status(std::format("Chế độ đọc: {}", ui::reading_mode_name(reading_mode_)), false);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) settings_window_.open_at(ui::SettingsWindow::Tab::Display);
    if (io.WantTextInput) return;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D) && viewer_.document()) {
        add_bookmark();
        open_sidebar(SidebarTab::Bookmarks);
    }
}

}  // namespace pdfread::app
