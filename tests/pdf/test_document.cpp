#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pdf/context.hpp"
#include "pdf/document.hpp"
#include "pdf/render_service.hpp"
#include "pdf/tiles.hpp"
#include "support/sample_pdf.hpp"
#include "util/log.hpp"

using Catch::Approx;
using namespace pdfread;
using namespace std::chrono_literals;

namespace {

pdf::RootContext& root() {
    static pdf::RootContext r;
    return r;
}

// Gắn context MuPDF cho luồng test trong suốt một TEST_CASE.
struct MuPdfScope {
    MuPdfScope() { pdf::attach_thread(root()); }
    ~MuPdfScope() { pdf::detach_thread(); }
    MuPdfScope(const MuPdfScope&) = delete;
    MuPdfScope& operator=(const MuPdfScope&) = delete;
};

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const std::string& name) : path(std::filesystem::temp_directory_path() / name) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

// Trang 0: A4 dọc, ô vuông đen 100..200 (tọa độ PDF, gốc dưới-trái).
// Trang 1: A4 ngang. Trang 2: 200x300 với /Rotate 90 -> hiển thị 300x200.
std::vector<testing::SamplePage> three_pages() {
    return {
        {595, 842, 0, testing::pdf_rect(100, 100, 100, 100, 0.0)},
        {842, 595, 0, testing::pdf_text(72, 500, 1, 12, "Landscape")},
        {200, 300, 90, testing::pdf_text(20, 250, 1, 12, "Rotated")},
    };
}

unsigned char pixel(const pdf::Pixmap& pix, int x, int y, int channel = 0) {
    return pix.samples()[static_cast<std::ptrdiff_t>(y) * pix.stride() + x * pix.components() + channel];
}

}  // namespace

TEST_CASE("Document opens a PDF and reads page sizes including rotation", "[pdf][document]") {
    MuPdfScope scope;
    TempFile file("pdfread_test_three_pages.pdf");
    testing::write_pdf(file.path, three_pages());

    auto doc = pdf::Document::open(file.path);
    REQUIRE(doc->page_count() == 3);
    CHECK(doc->page(0).width == Approx(595));
    CHECK(doc->page(0).height == Approx(842));
    CHECK(doc->page(1).width == Approx(842));
    CHECK(doc->page(1).height == Approx(595));
    CHECK(doc->page(2).width == Approx(300));  // đã xoay
    CHECK(doc->page(2).height == Approx(200));

    auto doc2 = pdf::Document::open(file.path);
    CHECK(doc2->id() != doc->id());
}

TEST_CASE("Document caches display lists", "[pdf][document]") {
    MuPdfScope scope;
    TempFile file("pdfread_test_cache.pdf");
    testing::write_pdf(file.path, three_pages());
    auto doc = pdf::Document::open(file.path);

    auto a = doc->display_list(0);
    auto b = doc->display_list(0);
    REQUIRE(a);
    CHECK(a.get() == b.get());

    doc->set_display_list_capacity(1);
    auto c = doc->display_list(1);  // đẩy trang 0 ra khỏi cache
    auto d = doc->display_list(0);
    CHECK(d.get() != a.get());

    CHECK_THROWS_AS(doc->display_list(3), pdf::Error);
    CHECK_THROWS_AS(doc->display_list(-1), pdf::Error);
}

TEST_CASE("render draws page content at the right pixel position", "[pdf][render]") {
    MuPdfScope scope;
    TempFile file("pdfread_test_render.pdf");
    testing::write_pdf(file.path, three_pages());
    auto doc = pdf::Document::open(file.path);
    auto list = doc->display_list(0);

    // Cả trang ở tỉ lệ 1: ô đen nằm ở x 100..200, y (từ trên) 842-200 .. 842-100 = 642..742.
    auto pix = pdf::render(*list, doc->page(0), 1.0f, {0, 0, 595, 842});
    REQUIRE(pix->width() == 595);
    REQUIRE(pix->height() == 842);
    REQUIRE(pix->components() == 3);
    CHECK(pixel(*pix, 150, 692) < 10);
    CHECK(pixel(*pix, 10, 10) > 245);
    CHECK(pixel(*pix, 250, 692) > 245);

    // Một tile ở tỉ lệ 2: vùng [256, 768) x [1280, 1792) chứa ô đen (200..400, 1284..1484).
    auto tile = pdf::render(*list, doc->page(0), 2.0f, {256, 1280, 768, 1792});
    REQUIRE(tile->width() == 512);
    CHECK(pixel(*tile, 300 - 256, 1400 - 1280) < 10);
    CHECK(pixel(*tile, 450 - 256, 1400 - 1280) > 245);
}

TEST_CASE("Document::open reports errors as pdf::Error", "[pdf][document]") {
    MuPdfScope scope;
    pdfread::log::set_stderr_enabled(false);
    CHECK_THROWS_AS(pdf::Document::open(std::filesystem::temp_directory_path() / "pdfread_no_such_file.pdf"),
                    pdf::Error);

    TempFile junk("pdfread_test_junk.pdf");
    {
        std::ofstream f(junk.path, std::ios::binary);
        f << "this is not a pdf";
    }
    CHECK_THROWS_AS(pdf::Document::open(junk.path), pdf::Error);
    pdfread::log::set_stderr_enabled(true);
}

TEST_CASE("Document opens paths with Vietnamese characters", "[pdf][document]") {
    MuPdfScope scope;
    TempFile file("pdfread_tài_liệu_thử.pdf");
    testing::write_pdf(file.path, three_pages());
    auto doc = pdf::Document::open(file.path);
    CHECK(doc->page_count() == 3);
}

TEST_CASE("RenderService renders requested tiles on worker threads", "[pdf][render]") {
    MuPdfScope scope;
    TempFile file("pdfread_test_service.pdf");
    testing::write_pdf(file.path, three_pages());
    auto doc = pdf::Document::open(file.path);

    std::mutex m;
    std::condition_variable cv;
    int delivered = 0;
    std::vector<pdf::TileResult> results;
    {
        pdf::RenderService service(root(), 2, [&] {
            std::lock_guard lock(m);
            ++delivered;
            cv.notify_all();
        });
        service.set_document(doc);

        // Trang 0 ở tỉ lệ 1: lưới 2x2.
        std::vector<pdf::TileKey> keys;
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 2; ++col) keys.push_back({doc->id(), 0, pdf::kScaleDenominator, col, row});
        }
        service.request(keys);
        {
            std::unique_lock lock(m);
            REQUIRE(cv.wait_for(lock, 10s, [&] { return delivered >= 4; }));
        }
        results = service.take_results();
        CHECK_FALSE(service.busy());
    }

    REQUIRE(results.size() == 4);
    std::set<std::pair<int, int>> seen;
    for (const auto& r : results) {
        REQUIRE(r.pixmap);
        CHECK(r.error.empty());
        seen.insert({r.key.col, r.key.row});
        const pdf::IRect expect = pdf::tile_rect(pdf::tile_grid(595, 842, pdf::kScaleDenominator), r.key.col, r.key.row);
        CHECK(r.pixmap->width() == expect.width());
        CHECK(r.pixmap->height() == expect.height());
    }
    CHECK(seen.size() == 4);
}

TEST_CASE("RenderService drops pending tiles when the request list is replaced", "[pdf][render]") {
    MuPdfScope scope;
    TempFile file("pdfread_test_cancel.pdf");
    testing::write_pdf(file.path, three_pages());
    auto doc = pdf::Document::open(file.path);

    std::atomic<int> delivered{0};
    pdf::RenderService service(root(), 1, [&] { ++delivered; });
    service.set_document(doc);

    // Nhiều tile ở tỉ lệ lớn, rồi thay ngay bằng danh sách rỗng: phần lớn không được render.
    std::vector<pdf::TileKey> keys;
    const int scale_q = pdf::kScaleDenominator * 8;
    const pdf::TileGrid g = pdf::tile_grid(595, 842, scale_q);
    for (int row = 0; row < g.rows; ++row) {
        for (int col = 0; col < g.cols; ++col) keys.push_back({doc->id(), 0, scale_q, col, row});
    }
    REQUIRE(keys.size() > 20);
    service.request(keys);
    service.request({});

    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (service.busy() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(5ms);
    CHECK_FALSE(service.busy());
    CHECK(delivered.load() <= 2);  // chỉ tile đã kịp bắt đầu
}
