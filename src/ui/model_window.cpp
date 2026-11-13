#include "ui/model_window.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <thread>

#include <imgui.h>

#include "util/log.hpp"
#include "util/utf8.hpp"

namespace pdfread::ui {
namespace {

constexpr ImVec4 kErrorColor{0.75f, 0.1f, 0.1f, 1.0f};
constexpr ImVec4 kOkColor{0.1f, 0.5f, 0.15f, 1.0f};

std::string gb(std::uint64_t n) { return std::format("{:.2f} GB", static_cast<double>(n) / 1e9); }
std::string mb(std::uint64_t n) { return std::format("{:.0f} MB", static_cast<double>(n) / 1e6); }

std::string eta(double seconds) {
    if (seconds <= 0 || seconds > 48 * 3600) return "–";
    const int s = static_cast<int>(seconds);
    if (s >= 3600) return std::format("{} giờ {} phút", s / 3600, s % 3600 / 60);
    if (s >= 60) return std::format("{} phút {} giây", s / 60, s % 60);
    return std::format("{} giây", s);
}

std::uint64_t size_or_zero(const std::filesystem::path& p) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(p, ec);
    return ec ? 0 : static_cast<std::uint64_t>(n);
}

}  // namespace

ModelWindow::ModelWindow(std::function<void()> wake) : wake_(wake), downloader_(std::move(wake)) {}

ModelWindow::~ModelWindow() {
    if (license_) license_->cancel = true;
    if (license_thread_.joinable()) license_thread_.join();
}

void ModelWindow::fetch_license() {
    if (license_thread_.joinable()) {
        if (license_ && !license_->done) return;  // đang tải
        license_thread_.join();
    }
    license_ = std::make_shared<LicenseFetch>();
    license_text_.clear();
    license_thread_ = std::thread([state = license_, wake = wake_] {
        std::string error;
        auto text = download::http_get(download::kDefaultModel.license_url, &error, 1u << 20, &state->cancel);
        {
            std::lock_guard lock(state->mutex);
            state->text = std::move(text);
            state->error = std::move(error);
            state->done = true;
        }
        if (wake) wake();
    });
}

void ModelWindow::draw_download(const Info& info, float ui_scale, Action& action) {
    using download::DownloadState;
    const auto& src = download::kDefaultModel;
    const std::filesystem::path dest = info.download_dir / src.file_name;
    const download::DownloadProgress p = downloader_.progress();

    if (downloader_.running()) {
        const float frac = p.total > 0 ? static_cast<float>(static_cast<double>(p.done) / static_cast<double>(p.total)) : 0.0f;
        switch (p.state) {
            case DownloadState::Verifying:
                ImGui::ProgressBar(frac, ImVec2(-1, 0), std::format("Kiểm tra SHA-256… {:.0f}%", frac * 100).c_str());
                break;
            case DownloadState::Retrying:
                ImGui::ProgressBar(frac, ImVec2(-1, 0), std::format("{} / {}", gb(p.done), gb(p.total)).c_str());
                ImGui::TextDisabled("%s", std::format("Lỗi mạng ({}), đang thử lại (lần {})…", p.error, p.attempt + 2).c_str());
                break;
            case DownloadState::Downloading: {
                ImGui::ProgressBar(frac, ImVec2(-1, 0),
                                   std::format("{} / {} ({:.1f}%)", gb(p.done), gb(p.total), frac * 100).c_str());
                const double left = p.bytes_per_second > 0
                                        ? static_cast<double>(p.total - std::min(p.total, p.done)) / p.bytes_per_second
                                        : 0;
                ImGui::TextDisabled("%s", std::format("{:.1f} MB/s · còn khoảng {}", p.bytes_per_second / 1e6, eta(left)).c_str());
                break;
            }
            default:
                ImGui::ProgressBar(frac, ImVec2(-1, 0), "Đang kết nối…");
                break;
        }
        if (ImGui::Button("Tạm dừng")) downloader_.cancel();
        ImGui::SetItemTooltip("Phần đã tải được giữ lại; bấm tải lần sau sẽ tải tiếp");
        return;
    }

    // Vừa tải xong.
    if (p.state == DownloadState::Done && !download_reported_) {
        download_reported_ = true;
        action = Action::Downloaded;
        message_ = "Đã tải xong và kiểm tra SHA-256. Đang nạp mô hình…";
        message_error_ = false;
        show_license_ = false;
    } else if (p.state == DownloadState::Failed && !download_reported_) {
        download_reported_ = true;
        message_ = p.error;
        message_error_ = true;
    } else if (p.state == DownloadState::Cancelled && !download_reported_) {
        download_reported_ = true;
        message_ = "Đã tạm dừng. Phần đã tải được giữ lại.";
        message_error_ = false;
    }
    if (!message_.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::PushStyleColor(ImGuiCol_Text, message_error_ ? kErrorColor : kOkColor);
        ImGui::TextUnformatted(message_.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }

    const std::uint64_t have = size_or_zero(download::part_path(dest));
    if (!show_license_) {
        const std::string label = have > 0 ? std::format("Tải tiếp (đã có {} / {})…", mb(have), gb(src.size))
                                           : std::string("Xem giấy phép và tải về…");
        if (ImGui::Button(label.c_str())) {
            show_license_ = true;
            agreed_ = false;
            fetch_license();
        }
        return;
    }

    // Giấy phép mô hình: hiển thị, người dùng đồng ý rồi mới tải.
    if (license_) {
        std::lock_guard lock(license_->mutex);
        if (license_->done && license_text_.empty()) {
            if (license_->text) license_text_ = *license_->text;
        }
    }
    const bool fetching = license_ && license_text_.empty() && !(license_->done);
    if (fetching) {
        ImGui::TextDisabled("Đang tải văn bản giấy phép…");
    } else if (license_text_.empty()) {
        std::string err;
        if (license_) {
            std::lock_guard lock(license_->mutex);
            err = license_->error;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextWrapped("Không tải được giấy phép (%s). Kiểm tra kết nối mạng.", err.c_str());
        ImGui::PopStyleColor();
        if (ImGui::Button("Thử lại")) fetch_license();
        ImGui::SameLine();
        if (ImGui::Button("Thôi")) show_license_ = false;
    } else {
        ImGui::TextUnformatted("Giấy phép của mô hình (Tencent HY Community License):");
        ImGui::BeginChild("##license", ImVec2(0, 200 * ui_scale), ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(license_text_.c_str(), license_text_.c_str() + license_text_.size());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled(
            "Lưu ý: giấy phép không áp dụng tại EU, Vương quốc Anh, Hàn Quốc; có chính sách sử dụng được chấp nhận; "
            "không dùng đầu ra để cải thiện mô hình AI khác. Văn bản được lưu cạnh file mô hình.");
        ImGui::PopTextWrapPos();
        ImGui::Checkbox("Tôi đã đọc và đồng ý với giấy phép trên", &agreed_);
        ImGui::BeginDisabled(!agreed_);
        if (ImGui::Button(have > 0 ? "Tải tiếp" : "Tải về")) {
            std::error_code ec;
            std::filesystem::create_directories(info.download_dir, ec);
            std::ofstream(info.download_dir / src.license_file_name, std::ios::binary) << license_text_;
            message_.clear();
            download_reported_ = false;
            downloader_.start({.url = src.url, .dest = dest, .expected_size = src.size, .sha256 = src.sha256});
            log::info("Bắt đầu tải mô hình vào {}", path_to_utf8(dest));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Thôi")) show_license_ = false;
    }
}

ModelWindow::Action ModelWindow::draw(const Info& info, float ui_scale) {
    if (threads_ < 0) threads_ = info.threads;
    if (!open) return Action::None;
    Action action = Action::None;

    ImGui::SetNextWindowSize(ImVec2(640 * ui_scale, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Mô hình dịch", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return action;
    }

    ImGui::SeparatorText("Mô hình đang dùng");
    ImGui::PushTextWrapPos(0);
    if (info.model_path.empty()) {
        ImGui::TextUnformatted("Chưa có mô hình dịch. Tải mô hình mặc định bên dưới (cần mạng một lần), hoặc chọn file "
                               "GGUF có sẵn trên máy.");
    } else {
        ImGui::TextUnformatted(path_to_utf8(std::filesystem::path(info.model_path).make_preferred()).c_str());
        ImGui::TextDisabled("Trạng thái: %s", info.model_status.c_str());
    }
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Chọn file GGUF có sẵn…")) action = Action::ChooseFile;

    const auto& src = download::kDefaultModel;
    ImGui::SeparatorText("Tải mô hình từ Hugging Face");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(std::format("Tencent HY-MT1.5-1.8B, bản GGUF Q4_K_M ({}). Lưu vào {}. Sau khi tải, app "
                                       "chạy hoàn toàn offline.",
                                       gb(src.size), path_to_utf8(std::filesystem::path(info.download_dir).make_preferred()))
                               .c_str());
    ImGui::PopTextWrapPos();
    draw_download(info, ui_scale, action);

    ImGui::SeparatorText("Suy luận");
    const int hw = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    const std::string fmt = threads_ == 0 ? std::format("mặc định ({})", info.default_threads) : std::string("%d luồng");
    ImGui::SetNextItemWidth(260 * ui_scale);
    ImGui::SliderInt("Số luồng CPU", &threads_, 0, hw, fmt.c_str(), ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    ImGui::BeginDisabled(threads_ == info.threads || info.model_path.empty());
    if (ImGui::Button("Áp dụng")) action = Action::ApplyThreads;
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Nạp lại mô hình với số luồng mới");

    ImGui::SeparatorText("Cache bản dịch");
    if (info.cache) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(std::format("{} bản dịch đã lưu · đã dùng lại {} lần", info.cache->entries, info.cache->hits).c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(info.cache->entries == 0);
        if (ImGui::Button("Xóa cache")) action = Action::ClearCache;
        ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled("Không dùng được cơ sở dữ liệu cache (xem log).");
    }

    ImGui::End();
    return action;
}

}  // namespace pdfread::ui
