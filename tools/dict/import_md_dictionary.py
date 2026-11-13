#!/usr/bin/env python3
"""Nhập từ điển chuyên ngành dạng Markdown (bảng 3 cột) vào glossary CSV của Pdfread.

    python tools/dict/import_md_dictionary.py <từ_điển.md> <glossary.csv> [--source dict] [--report]

- Bảng thuật ngữ: | **Thuật ngữ tiếng Anh** | Thuật ngữ tiếng Việt | Giải thích & Công thức |
- Phụ lục viết tắt: | **Viết tắt** | Tiếng Anh đầy đủ | Nghĩa tiếng Việt |
- Phần trong ngoặc của tên thuật ngữ: ký hiệu ($\\sigma$) -> đưa vào giải thích; viết tắt / tên
  khác ((Am-241), (Clebsch–Gordan Coefficients)) -> cột aliases (ghép với phần ngoặc tương ứng
  bên tiếng Việt); mô tả có dấu phẩy -> cột notes.
- Công thức LaTeX ($...$) chuyển sang ký tự Unicode dễ đọc (T₁/₂, σ_f, ²³⁵U, a/b, √x) vì giao
  diện ImGui không hiển thị được LaTeX.
- Gộp với file CSV đích: mục của từ điển thay mục cũ cùng thuật ngữ (hoặc trùng tên khác);
  các mục cũ còn lại giữ nguyên.

Cột CSV ra: term_en, term_vi, keep_english, notes, source, aliases, explanation
(aliases: "tên=nghĩa|tên"; tên không kèm nghĩa là viết tắt -> giữ nguyên khi dịch câu).
"""

import csv
import io
import re
import sys
import unicodedata

# --- LaTeX -> Unicode --------------------------------------------------------------------

GREEK = {
    "alpha": "α", "beta": "β", "gamma": "γ", "delta": "δ", "epsilon": "ε", "varepsilon": "ε", "zeta": "ζ",
    "eta": "η", "theta": "θ", "vartheta": "ϑ", "iota": "ι", "kappa": "κ", "lambda": "λ", "mu": "μ", "nu": "ν",
    "xi": "ξ", "omicron": "ο", "pi": "π", "varpi": "ϖ", "rho": "ρ", "varrho": "ϱ", "sigma": "σ", "varsigma": "ς",
    "tau": "τ", "upsilon": "υ", "phi": "φ", "varphi": "φ", "chi": "χ", "psi": "ψ", "omega": "ω",
    "Gamma": "Γ", "Delta": "Δ", "Theta": "Θ", "Lambda": "Λ", "Xi": "Ξ", "Pi": "Π", "Sigma": "Σ",
    "Upsilon": "Υ", "Phi": "Φ", "Psi": "Ψ", "Omega": "Ω",
}

SYMBOLS = {
    "times": "×", "cdot": "·", "div": "÷", "pm": "±", "mp": "∓", "approx": "≈", "simeq": "≃", "sim": "~",
    "propto": "∝", "equiv": "≡", "neq": "≠", "ne": "≠", "le": "≤", "leq": "≤", "ge": "≥", "geq": "≥",
    "lesssim": "≲", "gtrsim": "≳", "ll": "≪", "gg": "≫", "infty": "∞", "partial": "∂", "nabla": "∇",
    "int": "∫", "iint": "∬", "iiint": "∭", "oint": "∮", "sum": "∑", "prod": "∏", "to": "→",
    "rightarrow": "→", "longrightarrow": "⟶", "leftarrow": "←", "longleftarrow": "⟵", "Rightarrow": "⇒",
    "Leftarrow": "⇐", "leftrightarrow": "↔", "Leftrightarrow": "⇔", "uparrow": "↑", "downarrow": "↓",
    "rightleftharpoons": "⇌", "mapsto": "↦", "hbar": "ħ", "ell": "ℓ", "circ": "∘", "degree": "°",
    "perp": "⊥", "parallel": "∥", "in": "∈", "notin": "∉", "subset": "⊂", "subseteq": "⊆", "cup": "∪",
    "cap": "∩", "forall": "∀", "exists": "∃", "angle": "∠", "prime": "′", "dagger": "†", "otimes": "⊗",
    "oplus": "⊕", "star": "⋆", "ast": "∗", "bullet": "•", "triangle": "△", "checkmark": "✓",
    "cdots": "⋯", "ldots": "…", "dots": "…", "vdots": "⋮", "ddots": "⋱", "lvert": "|", "rvert": "|",
    "vert": "|", "mid": "|", "lVert": "‖", "rVert": "‖", "Vert": "‖", "langle": "⟨", "rangle": "⟩",
    "lfloor": "⌊", "rfloor": "⌋", "lceil": "⌈", "rceil": "⌉", "AA": "Å", "aleph": "ℵ", "Re": "ℜ",
    "Im": "ℑ", "emptyset": "∅", "varnothing": "∅", "neg": "¬", "land": "∧", "lor": "∨", "wedge": "∧",
    "vee": "∨", "setminus": "∖", "therefore": "∴", "because": "∵", "odot": "⊙", "quad": " ", "qquad": " ",
    "percent": "%", "textperthousand": "‰", "textdegree": "°", "dag": "†", "S": "§", "backslash": "\\",
    "lbrace": "{", "rbrace": "}", "colon": ":", "approxeq": "≊", "cong": "≅", "nsim": "≁", "preceq": "⪯",
    "succeq": "⪰", "prec": "≺", "succ": "≻", "ni": "∋", "bot": "⊥", "top": "⊤", "square": "□",
    "Box": "□", "diamond": "⋄", "flat": "♭", "sharp": "♯", "natural": "♮", "hookrightarrow": "↪",
    "nearrow": "↗", "searrow": "↘", "leftrightharpoons": "⇋", "upharpoonright": "↾", "imath": "ı",
}

FUNCTIONS = {"log", "ln", "lg", "exp", "sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan",
             "sinh", "cosh", "tanh", "coth", "max", "min", "sup", "inf", "lim", "det", "arg", "deg", "dim",
             "ker", "Pr", "gcd", "erf", "erfc", "sgn", "Tr", "tr", "Var", "Cov", "mod", "bmod", "pmod"}

PASS_ARG = {"text", "mathrm", "mathbf", "mathit", "mathsf", "mathtt", "textbf", "textit", "textrm", "textsf",
            "texttt", "mathcal", "mathscr", "mathfrak", "boldsymbol", "bm", "operatorname", "boxed", "mbox",
            "hbox", "emph", "underline", "overbrace", "underbrace", "textup", "textnormal", "mathop", "ensuremath",
            "mathbin", "mathrel", "mathord", "displaystyle"}

IGNORE = {"left", "right", "big", "Big", "bigg", "Bigg", "bigl", "bigr", "Bigl", "Bigr", "biggl", "biggr",
          "Biggl", "Biggr", "displaystyle", "textstyle", "scriptstyle", "limits", "nolimits", "middle",
          "nonumber", "notag", "phantom", "hphantom", "vphantom", "label", "rm", "bf", "it", "cal", "tiny",
          "small", "large", "Large", "normalsize"}

# Dấu mở: bỏ khoảng trắng ngay sau ("\lvert JM\rangle" -> "|JM⟩").
DELIMS = {"lvert", "lVert", "langle", "left", "bigl", "Bigl", "biggl", "Biggl", "lfloor", "lceil"}

BLACKBOARD = {"R": "ℝ", "N": "ℕ", "Z": "ℤ", "Q": "ℚ", "C": "ℂ", "P": "ℙ", "E": "𝔼"}

ACCENT = {"bar": "̄", "overline": "̅", "hat": "̂", "widehat": "̂", "vec": "⃗",
          "dot": "̇", "ddot": "̈", "tilde": "̃", "widetilde": "̃", "check": "̌",
          "acute": "́", "grave": "̀", "breve": "̆", "overrightarrow": "⃗"}

SUP = dict(zip("0123456789+-−=()niabcdefghijklmoprstuvwxyzABDEGHIJKLMNOPRTUVW∘°′βγδθφχιn",
               "⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻⁻⁼⁽⁾ⁿⁱᵃᵇᶜᵈᵉᶠᵍʰⁱʲᵏˡᵐᵒᵖʳˢᵗᵘᵛʷˣʸᶻᴬᴮᴰᴱᴳᴴᴵᴶᴷᴸᴹᴺᴼᴾᴿᵀᵁⱽᵂ°°′ᵝᵞᵟᶿᵠᵡᶥⁿ"))
SUB = dict(zip("0123456789+-−=()aeoxhklmnpstijruvβγρφχə",
               "₀₁₂₃₄₅₆₇₈₉₊₋₋₌₍₎ₐₑₒₓₕₖₗₘₙₚₛₜᵢⱼᵣᵤᵥᵦᵧᵨᵩᵪₔ"))


TEXT_CMDS = {"text", "textrm", "textit", "textbf", "textsf", "texttt", "mbox", "hbox", "textup", "textnormal",
             "emph"}


class Latex:
    def __init__(self, s, text_mode=False):
        self.s = s
        self.i = 0
        self.text_mode = text_mode  # 	ext{...}: giữ khoảng trắng và dấu '-' như chữ thường

    def raw_group(self):
        """Nội dung {nhóm} chưa xử lý (để đọc lại ở chế độ chữ)."""
        self.skip_spaces()
        if self.peek() != "{":
            return self.arg()
        depth, j = 0, self.i
        while j < len(self.s):
            if self.s[j] == "{":
                depth += 1
            elif self.s[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            elif self.s[j] == "\\":
                j += 1
            j += 1
        content = self.s[self.i + 1:j]
        self.i = j + 1
        return Latex(content, text_mode=True).seq()

    def peek(self):
        return self.s[self.i] if self.i < len(self.s) else ""

    def command(self):
        # sau dấu '\'
        j = self.i
        while j < len(self.s) and self.s[j].isalpha():
            j += 1
        if j == self.i:  # \, \; \{ ...
            c = self.s[self.i] if self.i < len(self.s) else ""
            self.i += 1
            return {",": " ", ";": " ", ":": " ", "!": "", " ": " ", "{": "{", "}": "}", "%": "%", "|": "‖",
                    "#": "#", "&": "&", "_": "_", "$": "$", "\\": " "}.get(c, c)
        name = self.s[self.i:j]
        self.i = j
        out = self.apply(name)
        if name in DELIMS:
            self.skip_spaces()  # "\lvert JM\rangle" -> "|JM⟩"
        return out

    def skip_spaces(self):
        while self.peek() == " ":
            self.i += 1

    def arg(self):
        """Đối số: {nhóm} hoặc một token."""
        self.skip_spaces()
        c = self.peek()
        if c == "":
            return ""
        if c == "{":
            self.i += 1
            return self.seq("}")
        if c == "\\":
            self.i += 1
            return self.command()
        self.i += 1
        return c

    def optional(self):
        self.skip_spaces()
        if self.peek() != "[":
            return None
        self.i += 1
        out = self.seq("]")
        return out

    def apply(self, name):
        if name in GREEK:
            return GREEK[name]
        if name in SYMBOLS:
            return SYMBOLS[name]
        if name in FUNCTIONS:
            return name + (" " if self.peek().isalnum() else "")
        if name in ("frac", "dfrac", "tfrac", "cfrac"):
            a, b = self.arg(), self.arg()
            nxt = self.peek()
            gap = " " if (nxt.isalnum() or nxt == "\\") else ""
            return wrap(a) + "/" + wrap(b) + gap
        if name == "sqrt":
            n = self.optional()
            a = self.arg()
            root = {"3": "∛", "4": "∜"}.get(n, "√") if n else "√"
            return root + (a if is_atom(a) else "(" + a + ")")
        if name == "binom":
            a, b = self.arg(), self.arg()
            return f"C({a}, {b})"
        if name == "mathbb":
            a = self.arg()
            return "".join(BLACKBOARD.get(ch, ch) for ch in a)
        if name in ACCENT:
            a = self.arg()
            if len(a) == 1:
                return a + ACCENT[name]
            if name in ("overline", "bar"):
                return "".join(ch + "̅" for ch in a)
            return a
        if name in ("overset", "stackrel", "underset"):
            self.arg()
            return self.arg()
        if name in TEXT_CMDS:
            return self.raw_group()
        if name in PASS_ARG:
            return self.arg()
        if name in ("textsuperscript",):
            return sup(self.arg())
        if name in ("textsubscript",):
            return sub(self.arg())
        if name in IGNORE:
            if name in ("phantom", "hphantom", "vphantom", "label"):
                self.arg()
            return ""
        if name in ("mathring",):
            return self.arg() + "̊"
        if name in ("not",):
            nxt = self.arg()
            return nxt + "̸"
        if name in ("begin", "end"):
            self.arg()
            return ""
        if name in ("cases",):
            return ""
        return name  # lệnh lạ: giữ tên

    def seq(self, until=""):
        out = []
        while self.i < len(self.s):
            c = self.s[self.i]
            if until and c == until:
                self.i += 1
                break
            self.i += 1
            if c == "\\":
                out.append(self.command())
            elif c == "{":
                out.append(self.seq("}"))
            elif c == "}":
                pass
            elif c == "^":
                out.append(sup(self.arg()))
            elif c == "_":
                out.append(sub(self.arg()))
            elif c == "~":
                out.append(" ")
            elif c in " \t\n" and not self.text_mode:
                pass  # trong công thức, khoảng trắng không có nghĩa (thêm lại quanh dấu =, + … sau)
            elif c == "&":
                out.append(" ")
            elif c == "-" and not self.text_mode:
                # "500-700": khoảng giá trị (gạch nối) chứ không phải phép trừ.
                before = "".join(out).rstrip()[-1:]
                after = self.s[self.i:].lstrip()[:1]
                out.append("–" if before.isdigit() and after.isdigit() else "−")
            else:
                out.append(c)
        return "".join(out)


def is_atom(s):
    s = s.strip()
    return len(s) <= 1 or re.fullmatch(r"[\w.ʹ′]+", s) is not None


def wrap(s):
    s = s.strip()
    return s if is_atom(s) else "(" + s + ")"


def sup(a):
    a = a.replace(" ", "")
    if a in ("∘", "°", "o"):
        return "°"
    if a and all(ch in SUP or ch == "/" for ch in a) and a.strip("/"):
        return "".join(SUP.get(ch, ch) for ch in a)
    if a in ("′", "′′"):
        return a
    return "^" + (a if len(a) == 1 else "(" + a + ")")


def sub(a):
    a = a.replace(" ", "")
    if a and all(ch in SUB or ch == "/" for ch in a) and a.strip("/"):
        return "".join(SUB.get(ch, ch) for ch in a)
    if len(a) == 1 or re.fullmatch(r"[\w]+", a):
        return "_" + a
    return "_(" + a + ")"


REL = set("=≈≤≥≠≡∝→⇒⇔↔<>≃≪≫⇌≲≳⟶")
BIN = set("+−×·±∓÷")


def space_math(t):
    """Khoảng trắng quanh quan hệ (=, ≈, →…) và phép toán hai ngôi (+, −, ×…); dấu trừ một ngôi giữ liền."""
    out = []
    prev = ""  # ký tự khác khoảng trắng gần nhất
    for ch in t:
        if ch in REL:
            out.append(" " + ch + " ")
        elif ch in BIN:
            unary = prev == "" or prev in "([{,;|⟨" or prev in REL or prev in BIN
            out.append(ch if unary else " " + ch + " ")
        else:
            out.append(ch)
        if not ch.isspace():
            prev = ch
    t = re.sub(r" {2,}", " ", "".join(out)).strip()
    t = re.sub(r",(?=[\w(−])", ", ", t)          # "2,8,20" -> "2, 8, 20" (số thập phân dùng dấu chấm)
    t = re.sub(r"\(\s+", "(", re.sub(r"\s+\)", ")", t))
    return t


def latex_to_unicode(s):
    return space_math(Latex(s).seq())


def convert_text(s):
    """Ô bảng Markdown: thay $...$ bằng Unicode, bỏ định dạng Markdown."""
    # "\$" = ký hiệu dollar (đơn vị độ phản ứng): tạm thay để không lệch cặp $...$.
    s = s.replace("\\|", "|").replace("\\$", "\x00")
    out = []
    pos = 0
    for m in re.finditer(r"\$\$(.+?)\$\$|\$(.+?)\$", s):
        out.append(s[pos:m.start()])
        if m.group(1):  # công thức khối: dòng riêng
            out.append("\n" + latex_to_unicode(m.group(1)) + "\n")
        else:
            out.append(latex_to_unicode(m.group(2)))
        pos = m.end()
    out.append(s[pos:])
    t = "".join(out).replace("\x00", "$")
    t = re.sub(r"\*\*(.+?)\*\*", r"\1", t)
    t = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"\1", t)
    t = re.sub(r"`([^`]*)`", r"\1", t)
    t = re.sub(r"<br\s*/?>", "\n", t, flags=re.I)
    t = re.sub(r"[ \t]{2,}", " ", t)
    t = re.sub(r"[ \t]*\n[ \t]*", "\n", t)  # quanh công thức khối
    return unicodedata.normalize("NFC", t.strip())


# --- Bảng Markdown -----------------------------------------------------------------------

def split_row(line):
    cells, cur, esc = [], [], False
    body = line.strip()
    if body.startswith("|"):
        body = body[1:]
    if body.endswith("|"):
        body = body[:-1]
    i = 0
    while i < len(body):
        c = body[i]
        if c == "\\" and i + 1 < len(body) and body[i + 1] == "|":
            cur.append("\\|")
            i += 2
            continue
        if c == "|":
            cells.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    cells.append("".join(cur).strip())
    return cells


def top_level_parens(s):
    """[(start, end, content)] các cặp ngoặc tròn cấp ngoài cùng (bỏ qua ngoặc trong $...$)."""
    res, depth, start, in_math = [], 0, -1, False
    for i, c in enumerate(s):
        if c == "$":
            in_math = not in_math
        if in_math:
            continue
        if c == "(":
            if depth == 0:
                start = i
            depth += 1
        elif c == ")" and depth > 0:
            depth -= 1
            if depth == 0:
                res.append((start, i + 1, s[start + 1:i]))
    return res


def strip_md(s):
    s = s.strip()
    s = re.sub(r"^\*\*(.*)\*\*$", r"\1", s)
    return s.replace("**", "").strip()


def classify_paren(p):
    p = p.strip()
    if "$" in p or "\\" in p:
        return "symbol"
    if "," in p or ";" in p or ":" in p or len(p.split()) > 5 or p.lower().startswith(("e.g", "i.e", "ví dụ")):
        return "descr"
    return "alias"


def is_acronym(s):
    """Viết tắt / ký hiệu đồng vị: giữ nguyên khi dịch câu (AME, LOCA, Am-241, keff…)."""
    if " " in s.strip():
        return False
    letters = [c for c in s if c.isalpha()]
    if not letters:
        return True
    upper = sum(1 for c in letters if c.isupper())
    return upper >= max(2, len(letters) // 2) or any(c.isdigit() for c in s) or (upper >= 1 and len(letters) <= 4)


def is_notation(p):
    """Ký hiệu phản ứng hạt nhân trong ngoặc: "n,2n", "$\alpha$,n", "n,γ"."""
    p = p.strip()
    return "," in p and " " not in p and len(p) <= 24


def parse_term(en_raw, vi_raw):
    en = strip_md(en_raw)
    vi = strip_md(vi_raw)
    symbols, aliases_en, descr = [], [], []
    # Ngoặc là một phần của tên: ở đầu ("(α,n) Reaction") hoặc ký hiệu phản ứng ("(n,2n)", "(n,γ)").
    parens = [x for x in top_level_parens(en) if x[0] > 0 and not is_notation(x[2])]
    for (_, _, p) in parens:
        kind = classify_paren(p)
        if kind == "symbol":
            symbols.append(convert_text("$" + p.strip().strip("$") + "$") if "$" not in p else convert_text(p))
        elif kind == "alias":
            aliases_en.append(convert_text(p))
        else:
            descr.append(convert_text(p))
    en_main = en
    for (a, b, _) in reversed(parens):
        en_main = en_main[:a] + en_main[b:]
    en_main = re.sub(r"\s+", " ", en_main).strip(" –-")
    vi_paren_list = [x for x in top_level_parens(vi) if x[0] > 0 and not is_notation(x[2])]
    vi_parens = [convert_text(p) for (_, _, p) in vi_paren_list if "$" not in p]
    vi_main = vi
    for (a, b, _) in reversed(vi_paren_list):
        vi_main = vi_main[:a] + vi_main[b:]
    vi_main = convert_text(re.sub(r"\s+", " ", vi_main).strip(" –-"))
    en_main = convert_text(en_main)

    # "A & B": thuật ngữ chính = A, B là tên gọi khác (ghép với vế tương ứng bên tiếng Việt).
    sep = r"\s+&\s+|\s+/\s+|,\s+"
    en_parts = [x.strip() for x in re.split(sep, en_main) if x.strip()]
    vi_parts = [x.strip() for x in re.split(sep, vi_main) if x.strip()]
    aliases = []  # (alias, vi | None)
    if len(en_parts) > 1:
        # Chỉ tách khi vế đầu là một cụm từ (>= 2 từ): "Toroidal & Poloidal Magnetic Fields",
        # "Exoergic / Endoergic Reaction" (chung danh từ) hay "Time, Distance, Shielding" (một khái
        # niệm) giữ nguyên cả cụm. Vế sau chỉ thành tên gọi khác nếu là cụm từ hoặc viết tắt
        # (bỏ "Nuclear" trong "Electricity Market Design & Nuclear").
        if len(en_parts[0].split()) >= 2 and len(vi_parts) == len(en_parts):
            en_main, vi_main = en_parts[0], vi_parts[0]
            for e, v in zip(en_parts[1:], vi_parts[1:]):
                if is_acronym(e):
                    aliases.append((e, None))
                elif len(e.split()) >= 2:
                    aliases.append((e, v))
        else:
            # Không ghép được từng vế với tiếng Việt: giữ nguyên cả cụm, chỉ thêm các vế viết tắt.
            for e in en_parts[1:]:
                if is_acronym(e):
                    aliases.append((e, None))
    # Ngoặc bên tiếng Anh: ghép với ngoặc bên tiếng Việt theo thứ tự nếu cùng số lượng.
    pair = len(vi_parens) == len(aliases_en)
    for k, a in enumerate(aliases_en):
        if is_acronym(a):
            aliases.append((a, None))
        elif pair and len(a.split()) >= 2 and vi_parens[k] and vi_parens[k].lower() != a.lower():
            aliases.append((a, vi_parens[k]))
    return en_main, vi_main, aliases, symbols, descr, convert_text(re.sub(r"\s+", " ", en)) if (
        aliases and any(v is not None for (_, v) in aliases)) else ""


def lower_first(vi, en):
    """Bỏ viết hoa đầu cụm tiếng Việt (để dùng giữa câu), trừ tên riêng / viết tắt."""
    if not vi:
        return vi
    w = vi.split()[0]
    first_en = en.split()[0] if en.split() else ""
    if w == first_en or not w[0].isupper():
        return vi
    if len(w) > 1 and (w[1:].lower() != w[1:] or any(c.isdigit() for c in w)):
        return vi  # viết tắt, ký hiệu
    return vi[0].lower() + vi[1:]


def fold(s):
    return re.sub(r"\s+", " ", unicodedata.normalize("NFC", s).lower()).strip()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        print(__doc__)
        return 2
    source = "dict"
    if "--source" in sys.argv:
        source = sys.argv[sys.argv.index("--source") + 1]
        args = [a for a in args if a != source]
    md_path, csv_path = args[0], args[1]
    report = "--report" in sys.argv

    lines = open(md_path, encoding="utf-8").read().splitlines()
    known_abbr = set()
    for line in lines:  # chữ viết tắt (phụ lục) để giữ nguyên khi viết thường tên mục
        if line.startswith("| **"):
            first = strip_md(split_row(line)[0])
            if re.fullmatch(r"[A-Z0-9-]{2,}", first):
                known_abbr.add(first)
    entries = {}  # fold(term_en) -> dict
    order = []
    section = ""
    pending_section = False
    mode = None  # "terms" | "abbr" | None
    dup = 0
    abbr_rows = []
    for line in lines:
        if line.startswith("## "):
            title = line[3:].strip()
            if title.upper().startswith("PHẦN"):
                mode = "terms"
                section = re.sub(r"^PHẦN\s+\d+:\s*", "", title).strip()
                pending_section = True
            elif "VIẾT TẮT" in title.upper():
                mode = "abbr"
            else:
                mode = None
            continue
        if mode == "terms" and pending_section and line.startswith("*("):
            pending_section = False
            keep_words = set(re.findall(r"[A-Z0-9]{2,}", line)) | known_abbr
            title_words = set(re.findall(r"[A-Z][a-z]+", line))
            words = []
            for w in section.split():
                core = re.sub(r"[^\w]", "", w)
                if core in keep_words or core.rstrip("S") in keep_words or any(c.isdigit() for c in core):
                    words.append(w)
                elif core.capitalize() in title_words and core.isascii() and len(core) > 3:
                    words.append(w.capitalize())  # tên riêng: Monte Carlo
                else:
                    words.append(w.lower())
            section = " ".join(words)
            section = section[0].upper() + section[1:]
            continue
        if not line.startswith("| **") or mode is None:
            continue
        cells = split_row(line)
        if len(cells) < 3:
            continue
        if mode == "abbr":
            abbr_rows.append(cells)
            continue
        en, vi, aliases, symbols, descr, full_title = parse_term(cells[0], cells[1])
        if not en:
            continue
        expl = convert_text(cells[2])
        if symbols:
            expl = "Ký hiệu: " + ", ".join(symbols) + ". " + expl
        key = fold(en)
        if key in entries:
            dup += 1
            continue
        keep = fold(vi) == fold(en)
        if keep and re.fullmatch(r"[A-Z][a-z]+", en):
            # "Proton" = "Proton": nghĩa viết thường để mô hình không viết hoa giữa câu.
            keep, vi = False, en.lower()
        notes = "; ".join(descr)
        if full_title:
            notes = (notes + " · " if notes else "") + "Mục từ: " + full_title
        notes = (notes + " · " if notes else "") + "Mục: " + section
        entries[key] = {
            "term_en": en,
            "term_vi": "" if keep else lower_first(vi, en),
            "keep_english": "1" if keep else "0",
            "notes": notes,
            "source": source,
            "aliases": aliases,
            "explanation": expl,
        }
        order.append(key)

    # Một viết tắt dùng chung cho nhiều mục ("Am-241" ở "Americium-241" và trong ngoặc của
    # "Ionization Smoke Detector (Am-241)"): chỉ giữ ở mục mà nó thực sự là viết tắt.
    def abbr_score(alias, term):
        digits = re.findall(r"\d+", alias)
        if digits and all(d in term for d in digits):
            return 2
        initials = "".join(w[0] for w in re.findall(r"[A-Za-z]+", term)).upper()
        letters = re.sub(r"[^A-Za-z]", "", alias).upper()
        return 1 if letters and letters in initials else 0

    owners = {}
    for k in order:
        for (a, v) in entries[k]["aliases"]:
            if v is None:
                owners.setdefault(fold(a), []).append(k)
    for a, ks in owners.items():
        if len(ks) < 2:
            continue
        best = max(ks, key=lambda k: abbr_score(a, entries[k]["term_en"]))
        for k in ks:
            if k != best:
                entries[k]["aliases"] = [(x, v) for (x, v) in entries[k]["aliases"] if not (v is None and fold(x) == a)]

    # Viết tắt (phụ lục): bỏ qua nếu đã là tên gọi khác của một thuật ngữ.
    alias_keys = {fold(a) for k in order for (a, _) in entries[k]["aliases"]}
    abbr_added = 0
    for cells in abbr_rows:
        abbr = convert_text(strip_md(cells[0]))
        full = convert_text(cells[1])
        vi = convert_text(cells[2])
        k = fold(abbr)
        if not abbr or k in entries or k in alias_keys:
            continue
        entries[k] = {
            "term_en": abbr,
            "term_vi": lower_first(vi, full),
            "keep_english": "1",
            "notes": full,
            "source": source,
            "aliases": [],
            "explanation": "",
        }
        order.append(k)
        abbr_added += 1

    # Gộp với glossary hiện có: mục của từ điển thắng.
    known = set(order) | {fold(a) for k in order for (a, _) in entries[k]["aliases"]}
    try:
        old = list(csv.DictReader(io.StringIO(open(csv_path, encoding="utf-8-sig").read())))
    except FileNotFoundError:
        old = []
    kept_old, replaced = [], 0
    for r in old:
        if (r.get("source") or "") == source:
            continue  # lần nhập trước của chính từ điển này
        if fold(r["term_en"]) in known:
            replaced += 1
            continue
        kept_old.append(r)

    out = io.StringIO()
    w = csv.writer(out, lineterminator="\n")
    w.writerow(["term_en", "term_vi", "keep_english", "notes", "source", "aliases", "explanation"])
    for k in order:
        e = entries[k]
        al = "|".join(a if v is None else f"{a}={lower_first(v, a)}" for (a, v) in e["aliases"] if fold(a) != k)
        w.writerow([e["term_en"], e["term_vi"], e["keep_english"], e["notes"], e["source"], al, e["explanation"]])
    for r in kept_old:
        w.writerow([r.get("term_en", ""), r.get("term_vi", ""), r.get("keep_english", "0"), r.get("notes", ""),
                    r.get("source", ""), r.get("aliases", ""), r.get("explanation", "")])
    open(csv_path, "w", encoding="utf-8", newline="").write(out.getvalue())

    print(f"từ điển: {len(order) - abbr_added} thuật ngữ, {abbr_added} viết tắt, {dup} mục trùng bỏ qua")
    print(f"glossary cũ: giữ {len(kept_old)}, thay {replaced}")
    if report:
        left = [e["explanation"] for e in entries.values() if "\\" in e["explanation"]]
        print(f"giải thích còn ký tự '\\': {len(left)}")
        for x in left[:15]:
            print("  ", x[:200])
    return 0


if __name__ == "__main__":
    sys.exit(main())
