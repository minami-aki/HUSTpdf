#include "ui/glossary_window.hpp"

#include <algorithm>
#include <exception>
#include <format>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <nfd.h>

#include "terminology/words.hpp"
#include "util/utf8.hpp"

namespace pdfread::ui {
namespace {

using terminology::GlossaryEntry;
using terminology::Layer;

const char* layer_label(Layer l) {
    switch (l) {
        case Layer::User: return "Của tôi";
        case Layer::Document: return "Tài liệu";
        case Layer::PrimaryDomain: return "Lĩnh vực chính";
        case Layer::OtherDomain: return "Lĩnh vực phụ";
        case Layer::Unrelated: return "Lĩnh vực khác";
    }
    return "";
}

bool contains_ci(const std::string& hay, const std::u32string& needle_lower) {
    if (needle_lower.empty()) return true;
    return terminology::lower(to_u32(hay)).find(needle_lower) != std::u32string::npos;
}

std::string trim(std::string s) {
    const auto b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}

}  // namespace

void GlossaryWindow::load_form(const GlossaryEntry& e) {
    form_en_ = e.term_en;
    form_vi_ = e.term_vi;
    form_notes_ = e.notes;
    form_keep_ = e.keep_english;
    form_aliases_ = e.alias_of.empty() ? e.aliases : std::vector<std::string>{};
    form_explanation_ = e.explanation;
    form_target_ = e.layer == Layer::Document ? 1 : 0;
    message_.clear();
}

void GlossaryWindow::edit(const terminology::GlossaryStore& store, const std::string& term_en) {
    (void)store;
    open = true;
    select_terms_tab_ = true;
    search_ = term_en;
    GlossaryEntry e;
    e.term_en = term_en;
    load_form(e);
}

bool GlossaryWindow::draw(terminology::GlossaryStore& store, const std::vector<terminology::TermCandidate>& extracted,
                          bool extracting, float ui_scale) {
    if (!open) return false;
    bool changed = false;
    ImGui::SetNextWindowSize(ImVec2(780 * ui_scale, 560 * ui_scale), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Glossary", &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return false;
    }

    // Thanh lệnh.
    if (ImGui::Button("Nạp lại")) changed |= store.refresh();
    ImGui::SetItemTooltip("Đọc lại các file CSV đã sửa bên ngoài");
    ImGui::SameLine();
    if (ImGui::Button("Nhập CSV…")) {
        nfdu8char_t* out = nullptr;
        const nfdu8filteritem_t filters[] = {{"CSV", "csv"}};
        if (NFD_OpenDialogU8(&out, filters, 1, nullptr) == NFD_OKAY) {
            try {
                const std::size_t n = store.import_into_user(path_from_utf8(out));
                message_ = std::format("Đã nhập {} mục vào glossary của tôi", n);
                message_error_ = false;
                changed = true;
            } catch (const std::exception& e) {
                message_ = e.what();
                message_error_ = true;
            }
            NFD_FreePathU8(out);
        }
    }
    ImGui::SetItemTooltip("Gộp file CSV (cột term_en, term_vi, keep_english, notes, source) vào glossary của tôi");
    ImGui::SameLine();
    if (ImGui::Button("Xuất CSV…")) {
        nfdu8char_t* out = nullptr;
        const nfdu8filteritem_t filters[] = {{"CSV", "csv"}};
        if (NFD_SaveDialogU8(&out, filters, 1, nullptr, "glossary.csv") == NFD_OKAY) {
            try {
                const auto idx = store.index();
                const std::u32string needle = terminology::lower(to_u32(search_));
                std::vector<GlossaryEntry> rows;
                for (const auto& e : idx->entries()) {
                    if (!e.alias_of.empty()) continue;  // tên gọi khác: đã nằm trong cột aliases của mục gốc
                    if (contains_ci(e.term_en, needle) || contains_ci(e.term_vi, needle)) rows.push_back(e);
                }
                terminology::save_glossary_csv(path_from_utf8(out), rows);
                message_ = std::format("Đã xuất {} mục", rows.size());
                message_error_ = false;
            } catch (const std::exception& e) {
                message_ = e.what();
                message_error_ = true;
            }
            NFD_FreePathU8(out);
        }
    }
    ImGui::SetItemTooltip("Xuất các mục đang hiển thị (theo ô tìm kiếm) ra file CSV");
    ImGui::SameLine();
    ImGui::TextDisabled("Glossary của tôi: %s", path_to_utf8(store.user_glossary_path()).c_str());
    for (const std::string& err : store.errors()) ImGui::TextColored(ImVec4(0.75f, 0.1f, 0.1f, 1.0f), "%s", err.c_str());

    if (ImGui::BeginTabBar("##gl_tabs")) {
        const ImGuiTabItemFlags terms_flags = select_terms_tab_ ? ImGuiTabItemFlags_SetSelected : 0;
        select_terms_tab_ = false;
        if (ImGui::BeginTabItem("Thuật ngữ", nullptr, terms_flags)) {
            changed |= draw_terms_tab(store, ui_scale);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(std::format("Trích từ tài liệu ({})###extracted", extracted.size()).c_str())) {
            draw_extracted_tab(store, extracted, extracting);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    return changed;
}

bool GlossaryWindow::draw_terms_tab(terminology::GlossaryStore& store, float ui_scale) {
    bool changed = false;
    ImGui::SetNextItemWidth(260 * ui_scale);
    ImGui::InputTextWithHint("##search", "Tìm (tiếng Anh hoặc tiếng Việt)", &search_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180 * ui_scale);
    const char* filters[] = {"Tất cả", "Của tôi", "Tài liệu", "Lĩnh vực của tài liệu", "Lĩnh vực khác"};
    ImGui::Combo("##filter", &filter_, filters, IM_ARRAYSIZE(filters));

    const auto idx = store.index();
    const std::u32string needle = terminology::lower(to_u32(trim(search_)));
    std::vector<const GlossaryEntry*> rows;
    for (const auto& e : idx->entries()) {
        if (!e.alias_of.empty()) continue;  // tên gọi khác hiển thị cùng mục gốc
        const bool pass_filter = filter_ == 0 || (filter_ == 1 && e.layer == Layer::User) ||
                                 (filter_ == 2 && e.layer == Layer::Document) ||
                                 (filter_ == 3 && (e.layer == Layer::PrimaryDomain || e.layer == Layer::OtherDomain)) ||
                                 (filter_ == 4 && e.layer == Layer::Unrelated);
        if (pass_filter && (contains_ci(e.term_en, needle) || contains_ci(e.term_vi, needle))) rows.push_back(&e);
    }
    std::sort(rows.begin(), rows.end(), [](const GlossaryEntry* a, const GlossaryEntry* b) {
        const auto la = terminology::lower(to_u32(a->term_en));
        const auto lb = terminology::lower(to_u32(b->term_en));
        return la != lb ? la < lb : a->layer < b->layer;
    });
    ImGui::SameLine();
    ImGui::TextDisabled("%zu mục", rows.size());

    const float form_h = ImGui::GetFrameHeightWithSpacing() * 5.5f;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##terms", 5, flags, ImVec2(0, ImGui::GetContentRegionAvail().y - form_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Thuật ngữ (EN)", ImGuiTableColumnFlags_None, 2.0f);
        ImGui::TableSetupColumn("Tiếng Việt", ImGuiTableColumnFlags_None, 2.0f);
        ImGui::TableSetupColumn("Giữ EN", ImGuiTableColumnFlags_None, 0.6f);
        ImGui::TableSetupColumn("Nguồn", ImGuiTableColumnFlags_None, 1.8f);
        ImGui::TableSetupColumn("Ghi chú", ImGuiTableColumnFlags_None, 2.0f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const GlossaryEntry& e = *rows[static_cast<std::size_t>(r)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(r);
                if (ImGui::Selectable(e.term_en.c_str(), form_en_ == e.term_en, ImGuiSelectableFlags_SpanAllColumns)) {
                    load_form(e);
                }
                ImGui::PopID();
                ImGui::TableNextColumn();
                if (e.confirmed()) ImGui::TextUnformatted(e.term_vi.c_str());
                else ImGui::TextDisabled("(chưa xác nhận)");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(e.keep_english ? "có" : "");
                ImGui::TableNextColumn();
                std::string src = layer_label(e.layer);
                if (const auto* d = store.catalog().find(e.domain_id)) src += " · " + d->name_vi;
                if (e.source == "draft") src += " · nháp";
                ImGui::TextUnformatted(src.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(e.notes.c_str());
            }
        }
        ImGui::EndTable();
    }
    changed |= draw_form(store);
    return changed;
}

bool GlossaryWindow::draw_form(terminology::GlossaryStore& store) {
    bool changed = false;
    ImGui::SeparatorText("Sửa / thêm thuật ngữ");
    const float half = ImGui::GetContentRegionAvail().x * 0.5f - ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(half);
    ImGui::InputTextWithHint("##en", "Thuật ngữ tiếng Anh", &form_en_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    ImGui::InputTextWithHint("##vi", "Tiếng Việt", &form_vi_);
    ImGui::Checkbox("Giữ nguyên tiếng Anh", &form_keep_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##notes", "Ghi chú", &form_notes_);
    // Tên gọi khác và giải thích & công thức: chỉ xem ở đây, sửa trong file CSV (cột aliases, explanation).
    if (!form_aliases_.empty()) {
        std::string list;
        for (const auto& a : form_aliases_) list += (list.empty() ? "" : " · ") + a;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Tên gọi khác: %s", list.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!form_explanation_.empty()) {
        ImGui::BeginChild("##form_explanation", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 4.5f),
                          ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(form_explanation_.c_str(), form_explanation_.c_str() + form_explanation_.size());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }

    ImGui::RadioButton("Glossary của tôi", &form_target_, 0);
    ImGui::SameLine();
    const bool has_doc = !store.document_glossary_path().empty();
    ImGui::BeginDisabled(!has_doc);
    ImGui::RadioButton("Glossary của tài liệu này", &form_target_, 1);
    ImGui::EndDisabled();
    if (!has_doc && form_target_ == 1) form_target_ = 0;
    ImGui::SameLine();

    const Layer target = form_target_ == 1 ? Layer::Document : Layer::User;
    const std::string en = trim(form_en_);
    ImGui::BeginDisabled(en.empty() || (trim(form_vi_).empty() && !form_keep_));
    if (ImGui::Button("Lưu")) {
        try {
            GlossaryEntry e;
            e.term_en = en;
            e.term_vi = trim(form_vi_);
            e.keep_english = form_keep_;
            e.notes = trim(form_notes_);
            e.aliases = form_aliases_;
            e.explanation = form_explanation_;
            e.source = "user";
            store.upsert(target, e);
            message_ = std::format("Đã lưu \"{}\" vào {}", en, form_target_ == 1 ? "glossary của tài liệu" : "glossary của tôi");
            message_error_ = false;
            changed = true;
        } catch (const std::exception& ex) {
            message_ = ex.what();
            message_error_ = true;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const auto& list = target == Layer::Document ? store.document_entries() : store.user_entries();
    const bool exists = std::any_of(list.begin(), list.end(), [&](const auto& e) {
        return terminology::lower(to_u32(e.term_en)) == terminology::lower(to_u32(en));
    });
    ImGui::BeginDisabled(!exists);
    if (ImGui::Button("Xóa")) {
        try {
            store.remove(target, en);
            message_ = std::format("Đã xóa \"{}\"", en);
            message_error_ = false;
            changed = true;
        } catch (const std::exception& ex) {
            message_ = ex.what();
            message_error_ = true;
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Xóa khỏi glossary đang chọn (glossary lĩnh vực chỉ đọc)");
    ImGui::SameLine();
    if (ImGui::Button("Mới")) load_form({});

    if (!message_.empty()) {
        ImGui::SameLine();
        if (message_error_) ImGui::TextColored(ImVec4(0.75f, 0.1f, 0.1f, 1.0f), "%s", message_.c_str());
        else ImGui::TextDisabled("%s", message_.c_str());
    }
    return changed;
}

void GlossaryWindow::draw_extracted_tab(terminology::GlossaryStore& store,
                                        const std::vector<terminology::TermCandidate>& extracted, bool extracting) {
    ImGui::TextDisabled("%s", extracting ? "Đang trích thuật ngữ từ tài liệu…"
                                         : "Thuật ngữ lặp lại và từ viết tắt tìm thấy trong tài liệu (chưa xác nhận, "
                                           "không dùng khi dịch cho tới khi bạn thêm bản dịch).");
    const auto idx = store.index();
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##extracted", 5, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Thuật ngữ", ImGuiTableColumnFlags_None, 2.5f);
        ImGui::TableSetupColumn("Dạng đầy đủ", ImGuiTableColumnFlags_None, 2.5f);
        ImGui::TableSetupColumn("Số lần", ImGuiTableColumnFlags_None, 0.6f);
        ImGui::TableSetupColumn("Trong glossary", ImGuiTableColumnFlags_None, 2.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_None, 0.7f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(extracted.size()));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const auto& c = extracted[static_cast<std::size_t>(r)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.term.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.expansion.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d", c.count);
                ImGui::TableNextColumn();
                const auto hit = idx->lookup(to_u32(c.term));
                if (hit && hit->entry->confirmed()) ImGui::TextUnformatted(hit->entry->target().c_str());
                else ImGui::TextDisabled("—");
                ImGui::TableNextColumn();
                ImGui::PushID(r);
                if (ImGui::SmallButton("Thêm…")) {
                    GlossaryEntry e;
                    e.term_en = c.term;
                    e.notes = c.expansion;
                    e.layer = Layer::Document;
                    load_form(e);
                    if (store.document_glossary_path().empty()) form_target_ = 0;
                    select_terms_tab_ = true;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

}  // namespace pdfread::ui
