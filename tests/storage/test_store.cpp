#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "storage/store.hpp"
#include "support/mupdf_fixture.hpp"

using namespace pdfread::storage;
using pdfread::translate::DomainScore;
using pdfread::translate::TranslateRequest;

TEST_CASE("Store settings round trip", "[storage]") {
    auto store = Store::open_in_memory();
    CHECK_FALSE(store->get("model_path").has_value());
    store->set("model_path", "C:/mô hình/a.gguf");
    CHECK(store->get("model_path") == "C:/mô hình/a.gguf");
    store->set("model_path", "b.gguf");
    CHECK(store->get("model_path") == "b.gguf");
    store->set_int("threads", 6);
    CHECK(store->get_int("threads") == 6);
    store->set("threads", "abc");
    CHECK_FALSE(store->get_int("threads").has_value());
    store->set_bool("auto", false);
    CHECK(store->get_bool("auto") == false);
    store->erase("auto");
    CHECK_FALSE(store->get_bool("auto").has_value());
}

TEST_CASE("Store translation cache hit, miss and pruning", "[storage]") {
    auto store = Store::open_in_memory();
    CHECK_FALSE(store->find_translation("k1").has_value());
    store->put_translation("k1", "m", "The beam.", "Dầm.");
    store->put_translation("k2", "m", "The bus.", "Bus.");
    store->put_translation("k3", "m", "The cell.", "Tế bào.");
    CHECK(store->find_translation("k1") == "Dầm.");  // k1 thành dùng gần nhất
    CHECK(store->translation_stats().entries == 3);
    CHECK(store->translation_stats().hits == 1);

    store->prune_translations(2);  // bỏ k2 (lâu nhất chưa dùng)
    CHECK(store->translation_stats().entries == 2);
    CHECK_FALSE(store->find_translation("k2").has_value());
    CHECK(store->find_translation("k1").has_value());
    CHECK(store->find_translation("k3").has_value());

    store->put_translation("k3", "m", "The cell.", "Ô.");  // ghi đè
    CHECK(store->find_translation("k3") == "Ô.");
    store->erase_translation("k3");
    CHECK_FALSE(store->find_translation("k3").has_value());
    store->clear_translations();
    CHECK(store->translation_stats().entries == 0);
}

TEST_CASE("Store documents keep domains, override and last page", "[storage]") {
    auto store = Store::open_in_memory();
    DocumentInfo info;
    info.fingerprint = "abc";
    info.path = "C:/tài liệu/x.pdf";
    info.catalog = "v1";
    info.domains = {{"electrical.control", 0.8f}, {"mechanical.mechatronics", 0.35f}};
    store->put_document(info);
    store->set_domain_override("abc", "it.computer_science");
    store->set_last_page("abc", 12);
    const auto got = store->find_document("abc");
    REQUIRE(got.has_value());
    CHECK(got->path == info.path);
    CHECK(got->catalog == "v1");
    REQUIRE(got->domains.size() == 2);
    CHECK(got->domains[0].domain_id == "electrical.control");
    CHECK(got->domains[1].score > 0.34f);
    CHECK(got->domains[1].score < 0.36f);
    CHECK(got->domain_override == "it.computer_science");
    CHECK(got->last_page == 12);
    CHECK_FALSE(store->find_document("zzz").has_value());
}

TEST_CASE("Store persists to a file and reopens", "[storage]") {
    pdfread::testing::TempFile tmp("pdfread_store_test.db");
    {
        auto store = Store::open(tmp.path);
        store->set("x", "1");
        store->put_translation("k", "m", "s", "o");
    }
    {
        auto store = Store::open(tmp.path);
        CHECK(store->get("x") == "1");
        CHECK(store->find_translation("k") == "o");
        store->put_translation("k2", "m", "s", "o2");  // đồng hồ "used" tiếp tục tăng sau khi mở lại
        store->prune_translations(1);
        CHECK(store->find_translation("k2") == "o2");
        CHECK_FALSE(store->find_translation("k").has_value());
    }
    std::error_code ec;
    std::filesystem::remove(tmp.path.string() + "-wal", ec);
    std::filesystem::remove(tmp.path.string() + "-shm", ec);
}

TEST_CASE("Translation cache key depends on everything sent to the model", "[storage]") {
    TranslateRequest a;
    a.text = "The stress in the beam is ⟦F1⟧.";
    const std::string k = translation_cache_key(a, "HY-MT1.5-1.8B-Q4_K_M.gguf", false);
    CHECK(k.size() == 64);
    CHECK(k == translation_cache_key(a, "HY-MT1.5-1.8B-Q4_K_M.gguf", false));
    CHECK(k != translation_cache_key(a, "other.gguf", false));
    CHECK(k != translation_cache_key(a, "HY-MT1.5-1.8B-Q4_K_M.gguf", true));

    TranslateRequest b = a;
    b.terms.push_back({"stress", "ứng suất", false});
    CHECK(translation_cache_key(b, "HY-MT1.5-1.8B-Q4_K_M.gguf", false) != k);
    TranslateRequest c = a;
    c.context = "Consider a cantilever.";
    CHECK(translation_cache_key(c, "HY-MT1.5-1.8B-Q4_K_M.gguf", false) != k);
    // Lĩnh vực không vào prompt -> không đổi khóa (thuật ngữ đã phản ánh lĩnh vực).
    TranslateRequest d = a;
    d.domains.push_back({"mechanical.engineering_mechanics", 1.0f});
    CHECK(translation_cache_key(d, "HY-MT1.5-1.8B-Q4_K_M.gguf", false) == k);
}

TEST_CASE("Document fingerprint uses size, head and tail", "[storage]") {
    pdfread::testing::TempFile f1("pdfread_fp1.bin");
    pdfread::testing::TempFile f2("pdfread_fp2.bin");
    std::string content(3 * 1024 * 1024, 'a');
    { std::ofstream(f1.path, std::ios::binary) << content; }
    { std::ofstream(f2.path, std::ios::binary) << content; }
    const std::string fp = document_fingerprint(f1.path);
    CHECK(fp.size() == 64);
    CHECK(fp == document_fingerprint(f2.path));

    content.back() = 'b';  // khác ở cuối
    { std::ofstream(f2.path, std::ios::binary) << content; }
    CHECK(fp != document_fingerprint(f2.path));
    { std::ofstream(f2.path, std::ios::binary) << content.substr(1); }  // khác kích thước
    CHECK(fp != document_fingerprint(f2.path));
    CHECK(document_fingerprint(f1.path.string() + ".missing").empty());

    pdfread::testing::TempFile small("pdfread_fp3.bin");
    { std::ofstream(small.path, std::ios::binary) << "tiny"; }
    CHECK(document_fingerprint(small.path).size() == 64);
}

TEST_CASE("Domain list encoding", "[storage]") {
    const std::vector<DomainScore> d = {{"a.b", 0.5f}, {"c.d", 1.0f}};
    const auto back = decode_domains(encode_domains(d));
    REQUIRE(back.size() == 2);
    CHECK(back[0].domain_id == "a.b");
    CHECK(back[1].score == 1.0f);
    CHECK(decode_domains("").empty());
    CHECK(decode_domains("bad;x:1").size() == 1);
}

TEST_CASE("Store keeps bookmarks per document in page order", "[storage]") {
    auto store = Store::open_in_memory();
    const auto b1 = store->add_bookmark("docA", 10, 120.5f, "Chương 2 — tr. 11");
    const auto b2 = store->add_bookmark("docA", 3, 0.0f, "Trang 4");
    store->add_bookmark("docB", 1, 0.0f, "khác");
    auto list = store->bookmarks("docA");
    REQUIRE(list.size() == 2);
    CHECK(list[0].page == 3);
    CHECK(list[1].page == 10);
    CHECK(list[1].y > 120.0f);
    CHECK(list[1].title == "Chương 2 — tr. 11");
    CHECK(list[1].id == b1);
    store->rename_bookmark(b2, "Mở đầu");
    store->remove_bookmark(b1);
    list = store->bookmarks("docA");
    REQUIRE(list.size() == 1);
    CHECK(list[0].title == "Mở đầu");
    CHECK(store->bookmarks("docB").size() == 1);
    CHECK(store->bookmarks("none").empty());
}
