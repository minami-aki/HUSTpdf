// pdfread-eval: đánh giá chất lượng dịch trên tập câu (CSV), so sánh 3 cấu hình:
//   1 = dịch câu đơn, không ngữ cảnh;
//   2 = + lĩnh vực + glossary (thuật ngữ có trong câu, mẫu "chỉ định thuật ngữ");
//   3 = + câu xung quanh (mẫu "dịch có ngữ cảnh"; câu không có ngữ cảnh thì giống cấu hình 2).
// Mọi cấu hình đều thay công thức/mã/đại lượng bằng placeholder như trong app.
//
// Đầu vào: CSV UTF-8 có tiêu đề, các cột (theo tên): en, domain, context, reference
//   (domain = id lĩnh vực con trong data/domains.json; context, reference có thể rỗng).
// Đầu ra:
//   --out      : mỗi câu × cấu hình một dòng (bản dịch, thuật ngữ đúng/sai, placeholder lỗi, độ trễ);
//   --summary  : theo nhóm lĩnh vực × cấu hình: tỉ lệ thuật ngữ đúng, placeholder lỗi, độ trễ TB và p95.
//
//   pdfread-eval --model models/HY-MT1.5-1.8B-Q4_K_M.gguf [--input tools/eval/sample.csv]
//                [--out eval_results.csv] [--summary eval_summary.csv] [--configs 123]
//                [--data data] [--threads N] [--limit N] [--quantity-placeholders]

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "terminology/domains.hpp"
#include "terminology/glossary.hpp"
#include "terminology/glossary_store.hpp"
#include "text/normalize.hpp"
#include "text/protect.hpp"
#include "translate/llama_translator.hpp"
#include "util/cmdline.hpp"
#include "util/log.hpp"
#include "util/utf8.hpp"

using namespace pdfread;

namespace {

struct Sample {
    std::string en;
    std::string domain;
    std::string context;
    std::string reference;
};

struct Result {
    std::size_t index = 0;
    int config = 0;
    std::string group;
    std::string translation;
    std::string raw;
    std::string terms;  // "en=vi; ..."
    std::string violated;
    int terms_total = 0;
    int terms_ok = 0;
    int placeholders = 0;
    int placeholder_errors = 0;
    int numbers_missing = 0;
    translate::TranslationStats stats;
};

const char* config_name(int c) {
    switch (c) {
        case 1: return "1-plain";
        case 2: return "2-domain+glossary";
        default: return "3-glossary+context";
    }
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("không đọc được " + path_to_utf8(p));
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (s.starts_with("\xEF\xBB\xBF")) s.erase(0, 3);
    return s;
}

std::vector<Sample> load_samples(const std::filesystem::path& p) {
    const auto rows = terminology::parse_csv(read_file(p));
    if (rows.empty()) throw std::runtime_error("file đầu vào rỗng");
    std::map<std::string, std::size_t> col;
    for (std::size_t i = 0; i < rows[0].size(); ++i) {
        std::string name = rows[0][i];
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        col[name] = i;
    }
    if (!col.contains("en")) throw std::runtime_error("thiếu cột 'en'");
    auto get = [&](const std::vector<std::string>& r, const char* name) {
        const auto it = col.find(name);
        return it != col.end() && it->second < r.size() ? r[it->second] : std::string();
    };
    std::vector<Sample> out;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        Sample s{get(rows[i], "en"), get(rows[i], "domain"), get(rows[i], "context"), get(rows[i], "reference")};
        if (!s.en.empty()) out.push_back(std::move(s));
    }
    return out;
}

std::string csv_field(const std::string& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + '"';
}

void write_csv(const std::filesystem::path& p, const std::vector<std::vector<std::string>>& rows) {
    std::ofstream out(p, std::ios::binary);
    if (!out) throw std::runtime_error("không ghi được " + path_to_utf8(p));
    out << "\xEF\xBB\xBF";  // BOM: Excel mở đúng tiếng Việt
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i > 0) out << ',';
            out << csv_field(row[i]);
        }
        out << "\r\n";
    }
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const auto k = static_cast<std::size_t>(std::ceil(p * static_cast<double>(v.size())));
    return v[std::min(v.size() - 1, k == 0 ? 0 : k - 1)];
}

std::filesystem::path default_data_dir(const std::string& argv0) {
    std::error_code ec;
    const auto beside = std::filesystem::absolute(path_from_utf8(argv0), ec).parent_path() / "data";
    if (std::filesystem::exists(beside / "domains.json", ec)) return beside;
    return "data";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const std::vector<std::string> args = utf8_args(argc, argv);
    translate::ModelConfig config;
    std::filesystem::path input = "tools/eval/sample.csv";
    std::filesystem::path out_path = "eval_results.csv";
    std::filesystem::path summary_path = "eval_summary.csv";
    std::filesystem::path data_dir = default_data_dir(args[0]);
    std::string configs = "123";
    std::size_t limit = 0;
    text::ProtectOptions protect_options{.quantities = false};  // như app (PlanOptions::protect_quantities)
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--model") config.model_path = path_from_utf8(next());
        else if (a == "--input") input = path_from_utf8(next());
        else if (a == "--out") out_path = path_from_utf8(next());
        else if (a == "--summary") summary_path = path_from_utf8(next());
        else if (a == "--data") data_dir = path_from_utf8(next());
        else if (a == "--configs") configs = next();
        else if (a == "--threads") config.threads = std::atoi(next().c_str());
        else if (a == "--backend") config.backend = translate::backend_from_key(next()).value_or(translate::Backend::Auto);
        else if (a == "--device") config.device = next();
        else if (a == "--limit") limit = static_cast<std::size_t>(std::atoi(next().c_str()));
        else if (a == "--quantity-placeholders") protect_options.quantities = true;
        else if (a == "--verbose") log::set_level(log::Level::Debug);
        else {
            std::fprintf(stderr, "Tham số không rõ: %s\n", a.c_str());
            return 2;
        }
    }
    if (config.model_path.empty()) {
        std::fprintf(stderr, "Cần --model file.gguf\n");
        return 2;
    }
    std::vector<int> config_ids;
    for (char c : configs) {
        if (c >= '1' && c <= '3') config_ids.push_back(c - '0');
    }

    try {
        std::vector<Sample> samples = load_samples(input);
        if (limit > 0 && samples.size() > limit) samples.resize(limit);
        terminology::DomainCatalog catalog = terminology::DomainCatalog::load(data_dir / "domains.json");
        const terminology::DomainCatalog catalog_copy = catalog;
        // Chỉ glossary lĩnh vực (không dùng glossary người dùng) để kết quả lặp lại được.
        terminology::GlossaryStore store(std::move(catalog),
                                         std::filesystem::temp_directory_path() / "pdfread-eval-no-user-glossary.csv");

        std::fprintf(stderr, "%zu câu × %zu cấu hình, mô hình %s\n", samples.size(), config_ids.size(),
                     path_to_utf8(config.model_path.filename()).c_str());
        translate::LlamaTranslator translator(config);
        const std::atomic<bool> cancel{false};
        {
            translate::TranslateRequest warm;  // khởi động (không tính vào độ trễ)
            warm.text = "Hello.";
            translator.translate(warm, [](std::string_view) {}, cancel, nullptr);
        }

        std::vector<Result> results;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const Sample& s = samples[i];
            const terminology::Subdomain* sub = catalog_copy.find(s.domain);
            const terminology::DomainGroup* group = sub != nullptr ? catalog_copy.group_of(*sub) : nullptr;
            if (sub == nullptr) std::fprintf(stderr, "Cảnh báo: lĩnh vực không rõ '%s' (dòng %zu)\n", s.domain.c_str(), i + 2);

            const std::u32string u = to_u32(s.en);
            const auto spans = text::find_protected(u, protect_options);
            std::vector<text::Span> excluded;
            for (const auto& p : spans) excluded.push_back(p.span);
            std::vector<translate::DomainScore> domains;
            if (sub != nullptr) domains.push_back({sub->id, 1.0f});
            store.set_domains(domains);
            const auto index = store.index();
            std::vector<terminology::GlossaryEntry> terms;
            for (const auto& m : index->find_in(u, excluded)) {
                const auto& e = *m.entry;
                if (!e.confirmed() || e.layer == terminology::Layer::Unrelated) continue;
                if (std::any_of(terms.begin(), terms.end(), [&](const auto& t) { return t.term_en == e.term_en; })) continue;
                terms.push_back(e);
            }
            const text::Placeholdered ph = text::apply_placeholders(u, spans);
            std::string term_list;
            for (const auto& t : terms) term_list += (term_list.empty() ? "" : "; ") + t.term_en + "=" + t.target();

            for (int c : config_ids) {
                translate::TranslateRequest req;
                req.text = ph.text;
                if (c >= 2) {
                    for (const auto& t : terms) req.terms.push_back({t.term_en, t.term_vi, t.keep_english});
                    req.domains = domains;
                }
                if (c == 3) req.context = s.context;

                Result r;
                r.index = i;
                r.config = c;
                r.group = group != nullptr ? group->id : "unknown";
                r.terms = term_list;
                translator.translate(req, [&](std::string_view piece) { r.raw += piece; }, cancel, &r.stats);
                const text::RestoreResult rr = text::restore_placeholders(r.raw, ph.placeholders);
                r.translation = rr.text;
                while (!r.translation.empty() && (r.translation.back() == '\n' || r.translation.back() == ' ')) r.translation.pop_back();
                r.translation = text::match_initial_case(r.translation, s.en);
                r.numbers_missing = static_cast<int>(text::missing_numbers(s.en, r.translation).size());
                r.placeholders = static_cast<int>(ph.placeholders.size());
                r.placeholder_errors = static_cast<int>(rr.missing.size() + rr.duplicated.size() + rr.unknown.size());
                const auto bad = terminology::check_terms(rr.text, terms);
                r.terms_total = static_cast<int>(terms.size());
                r.terms_ok = r.terms_total - static_cast<int>(bad.size());
                for (const auto& b : bad) r.violated += (r.violated.empty() ? "" : "; ") + b.term_en;
                std::fprintf(stderr, "[%zu/%zu] cấu hình %d: %.0f ms, thuật ngữ %d/%d, placeholder lỗi %d\n", i + 1,
                             samples.size(), c, r.stats.total_ms, r.terms_ok, r.terms_total, r.placeholder_errors);
                results.push_back(std::move(r));
            }
        }

        // Kết quả từng câu.
        std::vector<std::vector<std::string>> rows = {
            {"id", "group", "domain", "config", "en", "context", "reference", "translation", "terms", "terms_total",
             "terms_ok", "terms_violated", "placeholders", "placeholder_errors", "numbers_missing", "first_token_ms", "total_ms",
             "prompt_tokens", "output_tokens", "raw"}};
        for (const Result& r : results) {
            const Sample& s = samples[r.index];
            rows.push_back({std::to_string(r.index + 1), r.group, s.domain, config_name(r.config), s.en, s.context,
                            s.reference, r.translation, r.terms, std::to_string(r.terms_total), std::to_string(r.terms_ok),
                            r.violated, std::to_string(r.placeholders), std::to_string(r.placeholder_errors),
                            std::to_string(r.numbers_missing), std::format("{:.0f}", r.stats.first_token_ms), std::format("{:.0f}", r.stats.total_ms),
                            std::to_string(r.stats.prompt_tokens), std::to_string(r.stats.output_tokens), r.raw});
        }
        write_csv(out_path, rows);

        // Tổng hợp theo nhóm lĩnh vực × cấu hình (+ dòng "ALL").
        struct Agg {
            int sentences = 0, terms_total = 0, terms_ok = 0, placeholders = 0, placeholder_errors = 0, numbers_missing = 0;
            std::vector<double> latency, first_token;
        };
        std::map<std::pair<std::string, int>, Agg> agg;
        for (const Result& r : results) {
            for (const std::string& g : {r.group, std::string("ALL")}) {
                Agg& a = agg[{g, r.config}];
                ++a.sentences;
                a.terms_total += r.terms_total;
                a.terms_ok += r.terms_ok;
                a.placeholders += r.placeholders;
                a.placeholder_errors += r.placeholder_errors;
                a.numbers_missing += r.numbers_missing;
                a.latency.push_back(r.stats.total_ms);
                a.first_token.push_back(r.stats.first_token_ms);
            }
        }
        std::vector<std::vector<std::string>> summary = {
            {"group", "group_name", "config", "sentences", "terms_total", "terms_ok", "term_accuracy_pct", "placeholders",
             "placeholder_errors", "numbers_missing", "latency_mean_ms", "latency_p95_ms", "first_token_mean_ms"}};
        std::printf("\n%-12s %-20s %5s %12s %10s %10s %10s %10s %10s\n", "nhóm", "cấu hình", "câu", "thuật ngữ", "đúng %",
                    "ph lỗi", "mất số", "TB ms", "p95 ms");
        for (const auto& [key, a] : agg) {
            const auto& [g, c] = key;
            const auto grp = std::find_if(catalog_copy.groups().begin(), catalog_copy.groups().end(),
                                           [&](const auto& x) { return x.id == g; });
            const std::string gname = grp != catalog_copy.groups().end() ? grp->name_vi : g;
            double mean = 0, ft = 0;
            for (double v : a.latency) mean += v;
            for (double v : a.first_token) ft += v;
            mean /= std::max<std::size_t>(1, a.latency.size());
            ft /= std::max<std::size_t>(1, a.first_token.size());
            const double acc = a.terms_total > 0 ? 100.0 * a.terms_ok / a.terms_total : 100.0;
            const double p95 = percentile(a.latency, 0.95);
            summary.push_back({g, gname, config_name(c), std::to_string(a.sentences), std::to_string(a.terms_total),
                               std::to_string(a.terms_ok), std::format("{:.1f}", acc), std::to_string(a.placeholders),
                               std::to_string(a.placeholder_errors), std::to_string(a.numbers_missing),
                               std::format("{:.0f}", mean),
                               std::format("{:.0f}", p95), std::format("{:.0f}", ft)});
            std::printf("%-12s %-20s %5d %6d/%-5d %10.1f %10d %10d %10.0f %10.0f\n", g.c_str(), config_name(c), a.sentences,
                        a.terms_ok, a.terms_total, acc, a.placeholder_errors, a.numbers_missing, mean, p95);
        }
        write_csv(summary_path, summary);
        std::printf("\nĐã ghi %s và %s\n", path_to_utf8(out_path).c_str(), path_to_utf8(summary_path).c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Lỗi: %s\n", e.what());
        return 1;
    }
    return 0;
}
