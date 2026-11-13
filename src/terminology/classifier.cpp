#include "terminology/classifier.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "terminology/words.hpp"
#include "util/utf8.hpp"

namespace pdfread::terminology {
namespace {

struct Keyword {
    std::vector<std::u32string> words;
    double weight = 1;
};

// Số lần (có trọng số theo đoạn) cụm `kw` xuất hiện trong dãy từ.
double count_matches(const std::vector<Word>& words, const Keyword& kw) {
    double n = 0;
    if (kw.words.empty() || words.size() < kw.words.size()) return 0;
    for (std::size_t i = 0; i + kw.words.size() <= words.size(); ++i) {
        bool ok = true;
        for (std::size_t k = 0; k < kw.words.size() && ok; ++k) {
            // Chỉ từ cuối của cụm được phép ở dạng số nhiều.
            ok = k + 1 == kw.words.size() ? same_or_plural(words[i + k].text, kw.words[k])
                                          : words[i + k].text == kw.words[k];
        }
        if (ok) n += 1;
    }
    return n;
}

}  // namespace

std::vector<translate::DomainScore> classify(const std::vector<WeightedText>& inputs, const DomainCatalog& catalog,
                                             const ClassifyOptions& options) {
    std::vector<std::pair<std::vector<Word>, double>> docs;
    for (const WeightedText& t : inputs) docs.emplace_back(split_words(t.text), t.weight);

    std::vector<translate::DomainScore> scores;
    for (const Subdomain& sub : catalog.subdomains()) {
        double score = 0;
        for (const std::string& k : sub.keywords) {
            Keyword kw;
            for (Word& w : split_words(to_u32(k))) kw.words.push_back(std::move(w.text));
            kw.weight = 1.0 + 0.5 * (static_cast<double>(kw.words.size()) - 1.0);
            double n = 0;
            for (const auto& [words, weight] : docs) n += weight * count_matches(words, kw);
            score += kw.weight * std::log2(1.0 + n);
        }
        if (score > 0) scores.push_back({sub.id, static_cast<float>(score)});
    }
    std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (scores.empty() || scores.front().score < options.min_score) return {};

    const double top = scores.front().score;
    std::vector<translate::DomainScore> out;
    for (const auto& s : scores) {
        if (out.size() >= options.max_results) break;
        if (s.score < options.relative_threshold * top || s.score < options.min_score) break;
        out.push_back(s);
    }
    double total = 0;
    for (const auto& s : out) total += s.score;
    for (auto& s : out) s.score = static_cast<float>(s.score / total);
    return out;
}

}  // namespace pdfread::terminology
