// Điểm vào: khởi tạo GLFW, OpenGL 3.3 core, Dear ImGui, NFD và MuPDF rồi chạy App.
//
// Tham số dòng lệnh:
//   pdfread [file.pdf] [--page N] [--zoom 150 | --fit-page] [--timing] [--verbose]
//   pdfread file.pdf --snapshot out.png   (kiểm thử: chờ render xong, chụp cửa sổ, thoát)

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nfd.h>

#include "app/app.hpp"
#include "pdf/context.hpp"
#include "ui/fonts.hpp"
#include "util/log.hpp"
#include "util/thread_name.hpp"
#include "util/utf8.hpp"

#ifndef PDFREAD_VERSION
#define PDFREAD_VERSION "0.0.0"
#endif

namespace {

using namespace pdfread;

// Tham số dòng lệnh dạng UTF-8 (trên Windows argv là mã ANSI, mất dấu tiếng Việt).
std::vector<std::string> utf8_args(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int n = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) out.push_back(path_to_utf8(std::filesystem::path(wargv[i])));
    LocalFree(wargv);
#else
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

std::filesystem::path executable_dir() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path find_data_dir() {
    std::error_code ec;
    const auto beside_exe = executable_dir() / "data";
    if (std::filesystem::exists(beside_exe / "fonts", ec)) return beside_exe;
#ifdef PDFREAD_SOURCE_DATA_DIR
    return path_from_utf8(PDFREAD_SOURCE_DATA_DIR);
#else
    return beside_exe;
#endif
}

void on_glfw_error(int code, const char* message) { log::error("GLFW {}: {}", code, message); }

// --select "P:x,y:x,y[:c|w|l]" hoặc "P:x,y:Q:x,y[:c|w|l]" — trang đánh số từ 1, tọa độ
// theo point từ góc trên-trái trang; đơn vị: ký tự (mặc định), từ, dòng.
std::optional<app::SelectSpec> parse_select(std::string_view s) {
    std::vector<std::string_view> parts;
    for (std::size_t start = 0;;) {
        const std::size_t colon = s.find(':', start);
        parts.push_back(s.substr(start, colon == std::string_view::npos ? std::string_view::npos : colon - start));
        if (colon == std::string_view::npos) break;
        start = colon + 1;
    }
    auto parse_int = [](std::string_view v, int& out) {
        return std::from_chars(v.data(), v.data() + v.size(), out).ec == std::errc{};
    };
    auto parse_point = [](std::string_view v, pdf::PointF& out) {
        const std::size_t comma = v.find(',');
        if (comma == std::string_view::npos) return false;
        return std::from_chars(v.data(), v.data() + comma, out.x).ec == std::errc{} &&
               std::from_chars(v.data() + comma + 1, v.data() + v.size(), out.y).ec == std::errc{};
    };

    app::SelectSpec spec;
    std::size_t i = 0;
    int page = 0;
    if (parts.size() < 3 || !parse_int(parts[i++], page) || !parse_point(parts[i++], spec.a.pt)) return std::nullopt;
    spec.a.page = page - 1;
    spec.b.page = spec.a.page;
    if (parts[i].find(',') == std::string_view::npos) {
        if (!parse_int(parts[i++], page)) return std::nullopt;
        spec.b.page = page - 1;
    }
    if (i >= parts.size() || !parse_point(parts[i++], spec.b.pt)) return std::nullopt;
    if (i < parts.size()) {
        if (parts[i] == "w") spec.unit = pdf::SelectUnit::Words;
        else if (parts[i] == "l") spec.unit = pdf::SelectUnit::Lines;
    }
    return spec;
}

int run(const std::vector<std::string>& args) {
    app::AppOptions options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&]() -> const std::string& {
            static const std::string empty;
            return i + 1 < args.size() ? args[++i] : empty;
        };
        if (a == "--version") {
            std::printf("pdfread %s\n", PDFREAD_VERSION);
            return 0;
        } else if (a == "--verbose") {
            log::set_level(log::Level::Debug);
        } else if (a == "--timing") {
            log::set_timing_enabled(true);
        } else if (a == "--snapshot") {
            options.snapshot_path = path_from_utf8(next());
        } else if (a == "--page") {
            options.start_page = std::max(0, std::atoi(next().c_str()) - 1);
        } else if (a == "--zoom") {
            options.zoom = std::atof(next().c_str()) / 100.0;
        } else if (a == "--fit-page") {
            options.fit_page = true;
        } else if (a == "--select") {
            const std::string& spec = next();
            options.select = parse_select(spec);
            if (!options.select) log::warn("--select không hợp lệ: {}", spec);
        } else if (!a.starts_with("--")) {
            options.open_path = path_from_utf8(a);
        } else {
            log::warn("Bỏ qua tham số không rõ: {}", a);
        }
    }
    options.font_dir = find_data_dir() / "fonts";

    glfwSetErrorCallback(on_glfw_error);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1200, 860, "Pdfread", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // không ghi imgui.ini
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    if (!ui::load_fonts(options.font_dir)) {
        log::warn("Dùng font mặc định của ImGui (không hiển thị được tiếng Việt có dấu)");
    }

    NFD_Init();
    int code = 0;
    {
        pdf::RootContext root;
        pdf::ThreadContextScope main_ctx(root);
        app::App app(window, root, std::move(options));
        code = app.run();
    }
    NFD_Quit();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return code;
}

}  // namespace

int main(int argc, char** argv) {
    name_current_thread("main");
    try {
        return run(utf8_args(argc, argv));
    } catch (const std::exception& e) {
        log::error("Lỗi không xử lý được: {}", e.what());
        return 1;
    }
}
