#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "terminology/glossary_store.hpp"
#include "util/utf8.hpp"

using namespace pdfread;
using terminology::Layer;

namespace {

void write(const std::filesystem::path& p, const std::string& s) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

struct TempDir {
    std::filesystem::path path;
    TempDir() : path(std::filesystem::temp_directory_path() / "pdfread_glossary_store_test") {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

}  // namespace

TEST_CASE("GlossaryStore layers user, document and domain glossaries", "[terminology][store]") {
    TempDir dir;
    write(dir.path / "data/glossary/mech.csv", "term_en,term_vi\nstress,ứng suất\nyield,chảy dẻo\n");
    write(dir.path / "data/glossary/fin.csv", "term_en,term_vi\nyield,lợi suất\n");
    write(dir.path / "data/domains.json", R"({"groups":[{"id":"g","name_vi":"G","name_en":"G","subdomains":[
        {"id":"mech","name_vi":"Cơ","name_en":"Mech","keywords":[],"glossary":"glossary/mech.csv"},
        {"id":"fin","name_vi":"Tài chính","name_en":"Finance","keywords":[],"glossary":"glossary/fin.csv"}]}]})");
    const auto pdf = dir.path / "docs" / path_from_utf8("bài báo.pdf");
    write(dir.path / "docs" / path_from_utf8("bài báo.glossary.csv"), "term_en,term_vi\nstress,ứng suất pháp\n");

    terminology::GlossaryStore store(terminology::DomainCatalog::load(dir.path / "data/domains.json"),
                                     dir.path / "appdata/user.csv");
    CHECK(store.errors().empty());
    CHECK(terminology::GlossaryStore::document_glossary_for(pdf).filename() == path_from_utf8("bài báo.glossary.csv"));

    store.set_domains({{"mech", 1.0f}});
    auto idx = store.index();
    CHECK(idx->lookup(U"yield")->entry->term_vi == "chảy dẻo");          // lĩnh vực của tài liệu
    CHECK(idx->lookup(U"yield")->alternatives.size() == 1);             // "lợi suất" (tài chính)
    CHECK(idx->lookup(U"yield")->alternatives[0]->layer == Layer::Unrelated);

    store.set_document(pdf);
    CHECK(store.index()->lookup(U"stress")->entry->term_vi == "ứng suất pháp");  // tài liệu > lĩnh vực

    terminology::GlossaryEntry mine;
    mine.term_en = "Stress";
    mine.term_vi = "ứng suất (của tôi)";
    store.upsert(Layer::User, mine);
    CHECK(store.index()->lookup(U"stress")->entry->term_vi == "ứng suất (của tôi)");  // người dùng > tài liệu
    CHECK(std::filesystem::exists(dir.path / "appdata/user.csv"));

    // File bị sửa bên ngoài -> refresh() đọc lại.
    write(dir.path / "data/glossary/mech.csv", "term_en,term_vi\nyield,giới hạn chảy\n");
    std::filesystem::last_write_time(dir.path / "data/glossary/mech.csv",
                                     std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));
    CHECK(store.refresh());
    CHECK(store.index()->lookup(U"yield")->entry->term_vi == "giới hạn chảy");

    store.remove(Layer::User, "STRESS");
    CHECK(store.user_entries().empty());
    CHECK_THROWS(store.upsert(Layer::PrimaryDomain, mine));
}
