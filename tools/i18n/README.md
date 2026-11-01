# tools/i18n (docs/21 §9.3)

- `check_texts.py [firmware/app]`: fails on a Chinese string literal in `lib/` or `modules/`
  outside a `constexpr` Txt constant (only those are checked when built, `lib/i18n/txt.h`), so
  every screen has its English. Allowed: `lib/i18n/lang_names.h`, and the key names the hint
  parser matches (`lib/theme/hint_layout.cpp`).
- `texts.py [firmware/app] [--out texts.tsv]`: every zh/en pair with its width class, limit and
  both widths in efontCN_12 px, for reviewing the English; exits 1 on a pair over its limit.

CI runs both (the firmware job). `pytest` here tests them and checks the firmware is clean.
