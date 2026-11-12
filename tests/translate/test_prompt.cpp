#include <string>

#include <catch2/catch_test_macros.hpp>

#include "translate/prompt.hpp"
#include "translate/utf8_stream.hpp"

using namespace pdfread::translate;

TEST_CASE("Plain request uses the official XX<=>XX template", "[translate][prompt]") {
    TranslateRequest r;
    r.text = "The PID controller reduces overshoot.";
    CHECK(build_user_prompt(r) ==
          "Translate the following segment into Vietnamese, without additional explanation.\n\n"
          "The PID controller reduces overshoot.");
}

TEST_CASE("Terminology request uses the official terminology template", "[translate][prompt]") {
    TranslateRequest r;
    r.text = "The firmware drives the stepper motor.";
    r.terms = {{"stepper motor", "động cơ bước", false}, {"firmware", "", true}};
    CHECK(build_user_prompt(r) ==
          "参考下面的翻译：\n"
          "stepper motor 翻译成 động cơ bước\n"
          "firmware 翻译成 firmware\n"
          "\n将以下文本翻译为越南语，注意只需要输出翻译后的结果，不要额外解释：\n"
          "The firmware drives the stepper motor.");
}

TEST_CASE("Context request uses the official contextual template", "[translate][prompt]") {
    TranslateRequest r;
    r.text = "It reduces overshoot.";
    r.context = "A PID controller is tuned for the motor.";
    CHECK(build_user_prompt(r) ==
          "A PID controller is tuned for the motor.\n"
          "参考上面的信息，把下面的文本翻译成越南语，注意不需要翻译上文，也不要额外解释：\n"
          "It reduces overshoot.");

    r.terms = {{"overshoot", "độ quá điều chỉnh", false}};
    const std::string both = build_user_prompt(r);
    CHECK(both.starts_with("参考下面的翻译：\novershoot 翻译成 độ quá điều chỉnh\n\nA PID controller"));
    CHECK(both.ends_with("也不要额外解释：\nIt reduces overshoot."));
}

TEST_CASE("format_chat wraps the message with the model's chat template", "[translate][prompt]") {
    CHECK(format_chat("hello", false) == "<｜hy_begin▁of▁sentence｜><｜hy_User｜>hello<｜hy_place▁holder▁no▁8｜>");
    CHECK(format_chat("hello", true) == "<｜hy_begin▁of▁sentence｜><｜hy_User｜>hello<｜hy_Assistant｜>");
}

TEST_CASE("Utf8Stream only releases whole characters", "[translate][utf8]") {
    Utf8Stream s;
    const std::string viet = "điều";  // đ (2 byte), i, ề (3 byte), u
    CHECK(s.push(viet.substr(0, 1)).empty());       // nửa chữ "đ"
    CHECK(s.push(viet.substr(1, 2)) == "đi");      // xong "đ" + "i"
    CHECK(s.push(viet.substr(3, 2)).empty());       // 2/3 byte của "ề"
    CHECK(s.push(viet.substr(5)) == "ều");
    CHECK(s.flush().empty());

    CHECK(s.push("ab") == "ab");
    CHECK(s.push("\xE1\xBB").empty());
    CHECK(s.flush() == "\xE1\xBB");  // mô hình dừng giữa ký tự: trả nguyên phần còn lại
}
