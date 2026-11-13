#include "ui/translation_popup.hpp"

#include <algorithm>
#include <format>

namespace pdfread::ui {
namespace {

constexpr ImVec4 kWarnColor{0.72f, 0.38f, 0.0f, 1.0f};
constexpr ImVec4 kErrorColor{0.75f, 0.1f, 0.1f, 1.0f};
constexpr ImVec4 kTermColor{0.10f, 0.35f, 0.65f, 1.0f};

}  // namespace

void TranslationPopup::show(std::uint64_t request_id, bool truncated, Scope scope, std::vector<TermHint> terms) {
    state_ = State::Waiting;
    glossary_mode_ = false;
    request_id_ = request_id;
    truncated_ = truncated;
    scope_ = scope;
    terms_ = std::move(terms);
    issues_.clear();
    text_.clear();
    error_.clear();
    stats_ = {};
}

void TranslationPopup::show_glossary(GlossaryAnswer answer) {
    state_ = State::Done;
    glossary_mode_ = true;
    request_id_ = 0;
    truncated_ = false;
    scope_ = Scope::None;
    terms_.clear();
    issues_.clear();
    text_ = answer.term_vi;
    error_.clear();
    stats_ = {};
    answer_ = std::move(answer);
}

void TranslationPopup::on_event(const translate::TranslationEvent& e) {
    using Kind = translate::TranslationEvent::Kind;
    if (state_ == State::Hidden || glossary_mode_ || e.request_id != request_id_) return;
    switch (e.kind) {
        case Kind::Started:
        case Kind::Token: state_ = State::Streaming; break;
        case Kind::Done:
            state_ = State::Done;
            stats_ = e.stats;
            break;
        case Kind::Cancelled: state_ = State::Cancelled; break;
        case Kind::Failed:
            state_ = State::Error;
            error_ = e.text;
            break;
        default: break;
    }
}

void TranslationPopup::draw_glossary_answer() {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", answer_.term_en.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, kTermColor);
    ImGui::TextUnformatted(answer_.term_vi.c_str());
    ImGui::PopStyleColor();
    if (!answer_.domain.empty()) ImGui::TextDisabled("%s", answer_.domain.c_str());
    if (!answer_.notes.empty()) ImGui::TextUnformatted(answer_.notes.c_str());
    if (!answer_.alternatives.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Nghĩa khác:");
        for (const auto& [vi, domain] : answer_.alternatives) {
            ImGui::BulletText("%s", domain.empty() ? vi.c_str() : std::format("{}  ({})", vi, domain).c_str());
        }
    }
    ImGui::PopTextWrapPos();
}

TranslationPopup::Action TranslationPopup::draw(const std::pair<ImVec2, ImVec2>& anchor,
                                               const std::pair<ImVec2, ImVec2>& bounds, float ui_scale,
                                               translate::ModelState model) {
    if (state_ == State::Hidden) return Action::None;
    Action action = Action::None;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float margin = 8.0f * ui_scale;
    const float gap = 6.0f * ui_scale;
    const float bounds_w = bounds.second.x - bounds.first.x;
    const float width = std::max(160.0f * ui_scale, std::min(460.0f * ui_scale, bounds_w - 2 * margin));
    const float max_h = std::max(120.0f * ui_scale, (bounds.second.y - bounds.first.y) * 0.6f);
    const float h = last_height_ > 0 ? last_height_ : 80.0f * ui_scale;

    // Dưới vùng chọn nếu đủ chỗ, không thì phía trên; cả hai đều không đủ thì chọn bên rộng hơn.
    const float below = anchor.second.y + gap;
    const float above = anchor.first.y - gap - h;
    float y = below;
    if (below + h > bounds.second.y - margin) {
        if (above >= bounds.first.y + margin) {
            y = above;
        } else {
            const float room_below = bounds.second.y - anchor.second.y;
            const float room_above = anchor.first.y - bounds.first.y;
            y = room_below >= room_above ? std::max(bounds.first.y + margin, bounds.second.y - margin - h)
                                         : bounds.first.y + margin;
        }
    }
    const float x = std::clamp(anchor.first.x, bounds.first.x + margin,
                               std::max(bounds.first.x + margin, bounds.second.x - margin - width));

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, max_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * ui_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * ui_scale, 8.0f * ui_scale));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 0.995f, 0.96f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.18f));
    bool open = true;
    ImGui::Begin("##translation", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_AlwaysAutoResize);

    // Hàng tiêu đề: trạng thái + nút sao chép + nút đóng.
    std::string status;
    switch (state_) {
        case State::Waiting:
            status = model == translate::ModelState::Loading ? "đang nạp mô hình…" : "đang chờ…";
            break;
        case State::Streaming: status = "đang dịch…"; break;
        case State::Cancelled: status = "đã hủy"; break;
        case State::Error: status = "lỗi"; break;
        default: break;
    }
    const char* title = glossary_mode_ ? "Glossary" : "Bản dịch";
    ImGui::TextDisabled("%s", status.empty() ? title : std::format("{} · {}", title, status).c_str());
    const float close_w = ImGui::CalcTextSize("×").x + style.FramePadding.x * 2;
    const float copy_w = ImGui::CalcTextSize("Sao chép").x + style.FramePadding.x * 2;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - close_w - copy_w - style.ItemSpacing.x);
    ImGui::BeginDisabled(text_.empty());
    if (ImGui::SmallButton("Sao chép")) ImGui::SetClipboardText(text_.c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("×")) open = false;
    ImGui::Separator();

    if (glossary_mode_) {
        draw_glossary_answer();
        ImGui::Spacing();
        if (ImGui::SmallButton("Dịch bằng mô hình")) action = Action::TranslateWithModel;
        ImGui::SameLine();
        if (ImGui::SmallButton("Sửa thuật ngữ…")) action = Action::EditTerm;
    } else {
        ImGui::PushTextWrapPos(0.0f);
        if (state_ == State::Error) {
            ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
            ImGui::TextUnformatted(error_.c_str());
            ImGui::PopStyleColor();
        } else if (text_.empty()) {
            ImGui::TextDisabled("%s", state_ == State::Cancelled ? "(không có bản dịch)" : "…");
        } else {
            ImGui::TextUnformatted(text_.c_str());
        }
        if (truncated_) ImGui::TextDisabled("(Đoạn chọn quá dài, chỉ dịch phần đầu.)");

        // Hậu kiểm: thuật ngữ không được dịch như chỉ định, ký hiệu bị mất/đổi.
        if (!issues_.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, kWarnColor);
            for (const std::string& issue : issues_) ImGui::TextUnformatted(("• " + issue).c_str());
            ImGui::PopStyleColor();
        }
        if (!terms_.empty()) {
            std::string line = "Thuật ngữ: ";
            for (std::size_t i = 0; i < terms_.size(); ++i) {
                if (i > 0) line += " · ";
                line += terms_[i].en + " → " + terms_[i].vi;
            }
            ImGui::TextDisabled("%s", line.c_str());
        }
        if (state_ == State::Done && stats_.output_tokens > 0) {
            ImGui::TextDisabled("%s", std::format("{:.2f} s · token đầu {:.2f} s · {:.0f} token/s",
                                                  stats_.total_ms / 1000.0, stats_.first_token_ms / 1000.0,
                                                  stats_.tokens_per_second())
                                          .c_str());
        }
        ImGui::PopTextWrapPos();

        // Chọn phạm vi dịch khi vùng bôi đen chỉ là một phần câu.
        if (scope_ != Scope::None) {
            ImGui::Spacing();
            ImGui::TextDisabled("Dịch:");
            ImGui::SameLine();
            // Nền ô radio mặc định (FrameBg) gần trùng nền popup: làm đậm lên cho thấy rõ.
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.84f, 0.84f, 0.84f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.76f, 0.80f, 0.90f, 1.0f));
            if (ImGui::RadioButton("cả câu", scope_ == Scope::WholeSentence) && scope_ != Scope::WholeSentence) {
                action = Action::UseWholeSentence;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("chỉ phần chọn", scope_ == Scope::SelectionOnly) && scope_ != Scope::SelectionOnly) {
                action = Action::UseSelectionOnly;
            }
            ImGui::PopStyleColor(2);
        }
    }

    last_height_ = ImGui::GetWindowSize().y;
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (!open) {
        state_ = State::Hidden;
        action = Action::Close;
    }
    return action;
}

}  // namespace pdfread::ui
