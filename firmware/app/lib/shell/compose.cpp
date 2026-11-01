#include "compose.h"

#include <cstdio>
#include <cstring>

#include "keymap.h"
#include "text_fit.h"
#include "text_rules.h"
#include "theme.h"
#include "words.h"

namespace cpa {

// docs/21: the editor's words; its toasts fit the 224 px capsule (docs/20 B6).
constexpr ToastTxt kEmptyText{"空文本不保存", "Empty: not saved"};
constexpr ToastTxt kNeedProject{"先选项目（Opt+1-9）", "Pick a project first (Opt+1-9)"};
constexpr ToastTxt kDashText{"开头不能是 '-'", "Can't start with '-'"};
constexpr ToastTxt kFull{"已到 500 字节上限", "500-byte limit reached"};
constexpr ToastTxt kNoProjects{"还没有项目：Mac 上 cpa project add", "No projects: cpa project add on Mac"};
constexpr ToastTxt kNoProjectsYet{"还没收到项目：连上 Mac 再试", "No projects yet: connect the Mac"};
constexpr Txt kMoreProjects{" … 共 %u 个", " … %u in all"};
constexpr Txt kOptDigit{"Opt+数字：", "Opt+digit: "};
constexpr Txt kNoSlot{"没有第 %u 个项目（共 %u 个）", "No project %u (only %u)"};
namespace {

constexpr char kFirstPrintable = 0x20;
constexpr char kLastPrintable = 0x7E;

ComposeStep step(const ComposeState& s, ComposeResult r) { return ComposeStep{s, r}; }

bool blank(const UserText& t) {
  for (size_t i = 0; i < t.size(); ++i) {
    if (t.c_str()[i] != ' ') return false;
  }
  return true;
}

bool needs_project(ComposeKind k) { return k == ComposeKind::Queue || k == ComposeKind::Spawn; }

bool is_agent_kind(ComposeKind k) { return k == ComposeKind::Spawn || k == ComposeKind::Feedback; }

ComposeStep with_kind(const ComposeState& s, ComposeKind k) {
  ComposeState out = s;
  out.spec.kind = k;
  out.spec.sid = Opt<Sid>{};  // only Feedback targets a session
  return step(out, ComposeResult::Edited);
}

ComposeStep with_target(const ComposeState& s, uint8_t slot) {
  if (s.spec.kind == ComposeKind::Feedback) return step(s, ComposeResult::None);
  ComposeState out = s;
  out.spec.slot = slot;
  return step(out, ComposeResult::Edited);
}

ComposeStep submit(const ComposeState& s) {
  if (blank(s.text)) return step(s, ComposeResult::EmptyText);
  if (needs_project(s.spec.kind) && s.spec.slot == 0) return step(s, ComposeResult::NeedProject);
  if (compose_dash_blocked(s)) return step(s, ComposeResult::DashText);
  return step(s, ComposeResult::Submit);
}

ComposeStep cancel(const ComposeState& s) {
  if (s.text.empty()) return step(s, ComposeResult::Cancelled);
  ComposeState out = s;
  out.discard = true;
  return step(out, ComposeResult::DiscardAsked);
}

// Nothing to delete means nothing to lose: a fresh Backspace exits (issue #15). A held
// Backspace that keeps repeating after clearing the text does not.
ComposeStep delete_char(const ComposeState& s, const KeyEvent& ev) {
  if (s.text.empty()) return step(s, ev.repeat ? ComposeResult::None : ComposeResult::Cancelled);
  ComposeState out = s;
  out.text = UserText::from(s.text.c_str(), s.text.size() - 1);
  return step(out, ComposeResult::Edited);
}

ComposeStep type_char(const ComposeState& s, char c) {
  if (c < kFirstPrintable || c > kLastPrintable) return step(s, ComposeResult::None);
  if (s.text.size() >= UserText::capacity()) return step(s, ComposeResult::Full);
  ComposeState out = s;
  out.text = s.text.append(&c, 1);
  return step(out, ComposeResult::Edited);
}

// "Discard?": Enter discards; Fn+` or the top-left key alone goes back to editing.
ComposeStep discard_prompt(const ComposeState& s, const KeyEvent& ev) {
  if (ev.kind == KeyEventKind::Nav && ev.nav == Nav::Enter) return step(s, ComposeResult::Cancelled);
  const bool fn_grave = ev.kind == KeyEventKind::Nav && ev.nav == Nav::Back;
  const bool back = fn_grave || (ev.kind == KeyEventKind::Char && ev.ch == '`');
  if (!back) return step(s, ComposeResult::None);
  ComposeState out = s;
  out.discard = false;
  return step(out, ComposeResult::Edited);
}

}  // namespace

ComposeState compose_open(const ComposeSpec& spec) {
  ComposeState s;
  s.spec = spec;
  return s;
}

ComposeStep compose_key(const ComposeState& s, const KeyEvent& ev) {
  if (s.discard) return discard_prompt(s, ev);
  const Resolved r = keymap_lookup(Layer::Compose, ev);
  switch (r.action) {
    case Action::DeleteChar: return delete_char(s, ev);
    case Action::Submit: return submit(s);
    case Action::Cancel: return cancel(s);
    case Action::Target: return with_target(s, r.arg);
    case Action::TypeNote: return with_kind(s, ComposeKind::Note);
    case Action::TypeQueue: return with_kind(s, ComposeKind::Queue);
    case Action::TypeSpawn: return with_kind(s, ComposeKind::Spawn);
    case Action::Backtick: return type_char(s, '`');  // docs/20 D5
    default: break;
  }
  if (ev.kind == KeyEventKind::Char) return type_char(s, ev.ch);
  return step(s, ComposeResult::None);
}

bool compose_dash_blocked(const ComposeState& s) {
  if (!is_agent_kind(s.spec.kind) || blank(s.text)) return false;
  return !proto::is_agent_text(s.text.c_str(), s.text.size());
}

const char* compose_kind_label(ComposeKind k) {
  switch (k) {
    case ComposeKind::Queue: return tr(txt::kAiQueue);  // docs/20 U22: the names of the tiles and menu
    case ComposeKind::Spawn: return tr(txt::kNewSession);
    case ComposeKind::Feedback: return tr(txt::kReply);
    default: return tr(txt::kNote);
  }
}

const char* compose_result_text(ComposeResult r) {
  switch (r) {
    case ComposeResult::EmptyText: return tr(kEmptyText);
    case ComposeResult::NeedProject: return tr(kNeedProject);
    case ComposeResult::DashText: return tr(kDashText);
    case ComposeResult::Full: return tr(kFull);
    default: return "";
  }
}

}  // namespace cpa

namespace cpa {
namespace {

// The widest efontCN_12 draws s in: ASCII 6 px, anything else 12 px.
int widest_px(const char* s) {
  const size_t n = std::strlen(s);
  int w = 0;
  for (size_t i = 0; i < n;) {
    const Utf8Char ch = utf8_at(s, n, i);
    w += ch.cp < 0x80 ? 6 : 12;
    i += ch.len;
  }
  return w;
}

}  // namespace

// In the editor a plain digit is typed, so the hint names the chord; projects that do
// not fit the toast capsule (224 px, docs/20 B6) are counted instead of cut.
FixedStr<96> need_project_text(const ProjectsView& p) {
  using Text = FixedStr<96>;
  if (p.data.n == 0) return Text::from(tr(p.live ? kNoProjects : kNoProjectsYet));
  char more[32];
  std::snprintf(more, sizeof more, tr(kMoreProjects), p.data.n);
  Text out = Text::from(tr(kOptDigit));
  for (uint8_t i = 0; i < p.data.n; ++i) {
    char one[40];
    std::snprintf(one, sizeof one, "%s%u %s", i ? " · " : "", p.data.rows[i].slot, p.data.rows[i].key.c_str());
    const Text with = out.append(one);
    const int tail = i + 1 < p.data.n ? widest_px(more) : 0;
    if (widest_px(with.c_str()) + tail > theme::kToastTextW) return out.append(more);
    out = with;
  }
  return out;
}

FixedStr<96> no_slot_text(const ProjectsView& p, uint8_t slot) {
  if (p.data.n == 0) return need_project_text(p);
  char buf[64];
  std::snprintf(buf, sizeof buf, tr(kNoSlot), slot, p.data.n);
  return FixedStr<96>::from(buf);
}

}  // namespace cpa
