#include "translate/prompt.hpp"

namespace pdfread::translate {
namespace {

constexpr std::string_view kTargetEn = "Vietnamese";
constexpr std::string_view kTargetZh = "越南语";

std::string terminology_block(const TranslateRequest& r) {
    std::string out = "参考下面的翻译：\n";
    for (const GlossaryEntry& t : r.terms) {
        out += t.term_en;
        out += " 翻译成 ";
        out += t.keep_english ? t.term_en : t.term_vi;
        out += '\n';
    }
    return out;
}

}  // namespace

std::string build_user_prompt(const TranslateRequest& r) {
    if (!r.raw_user_message.empty()) return r.raw_user_message;
    const bool has_terms = !r.terms.empty();
    const bool has_context = !r.context.empty();
    std::string p;

    if (!has_terms && !has_context) {
        p += "Translate the following segment into ";
        p += kTargetEn;
        p += ", without additional explanation.\n\n";
        p += r.text;
        return p;
    }
    if (has_terms && !has_context) {
        p += terminology_block(r);
        p += "\n将以下文本翻译为";
        p += kTargetZh;
        p += "，注意只需要输出翻译后的结果，不要额外解释：\n";
        p += r.text;
        return p;
    }
    if (has_terms) {
        p += terminology_block(r);
        p += '\n';
    }
    p += r.context;
    p += "\n参考上面的信息，把下面的文本翻译成";
    p += kTargetZh;
    p += "，注意不需要翻译上文，也不要额外解释：\n";
    p += r.text;
    return p;
}

std::string format_chat(std::string_view user_message, bool add_generation_prompt) {
    std::string out = "<｜hy_begin▁of▁sentence｜><｜hy_User｜>";
    out += user_message;
    out += add_generation_prompt ? "<｜hy_Assistant｜>" : "<｜hy_place▁holder▁no▁8｜>";
    return out;
}

}  // namespace pdfread::translate
