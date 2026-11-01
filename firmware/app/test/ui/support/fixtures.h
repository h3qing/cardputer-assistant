// Builders for UI tests: AppState slices and KeyEvents as the chord detector emits them.
#pragma once

#include <initializer_list>

#include "app_state.h"
#include "chord_detector.h"

namespace uitest {

using namespace cpa;

inline KeyEvent chr(char c) { return KeyEvent{KeyEventKind::Char, c, 0, 0, Nav::None}; }
inline KeyEvent opt(char k) { return KeyEvent{KeyEventKind::Chord, 0, kModOpt, k, Nav::None}; }
inline KeyEvent nav(Nav n) { return KeyEvent{KeyEventKind::Nav, 0, 0, 0, n}; }
inline KeyEvent enter() { return nav(Nav::Enter); }
inline KeyEvent esc() { return nav(Nav::Back); }  // Fn+`
inline KeyEvent bksp() { return nav(Nav::Bksp); }
inline KeyEvent g0tap() { return KeyEvent{KeyEventKind::G0Tap, 0, 0, 0, Nav::None}; }
inline KeyEvent g0long() { return KeyEvent{KeyEventKind::G0Long, 0, 0, 0, Nav::None}; }
inline KeyEvent g0longup() { return KeyEvent{KeyEventKind::G0LongUp, 0, 0, 0, Nav::None}; }

inline ProjectRow project(uint8_t slot, const char* key, const char* name, uint16_t run = 0, uint16_t need = 0,
                          uint16_t queued = 0) {
  ProjectRow r;
  r.slot = slot;
  r.key = ProjectKey::from(key);
  r.name = ProjectName::from(name);
  r.run = run;
  r.need = need;
  r.queued = queued;
  r.ok = true;
  return r;
}

inline SessionRow session(const char* sid, uint8_t slot, const char* name, SessionKind kind, SessionState st,
                          bool need, uint32_t age_s, const char* short_id = nullptr) {
  SessionRow r;
  r.sid = Sid::from(sid);
  r.slot = slot;
  r.name = SessionName::from(name);
  r.kind = kind;
  r.st = st;
  r.need = need;
  r.age_s = age_s;
  if (short_id) r.short_id = some(ShortId::from(short_id));
  return r;
}

inline QueueRow queue_item(const char* qid, uint8_t slot, const char* text, Intent intent = Intent::Todo) {
  QueueRow r;
  r.qid = Qid::from(qid);
  r.slot = slot;
  r.text = QueueText::from(text);
  r.intent = intent;
  return r;
}

inline void set_projects(AppState& s, std::initializer_list<ProjectRow> rows, bool live = true, uint32_t rx_ms = 0) {
  s.projects.live = live;
  s.projects.rx_ms = rx_ms;
  s.projects.data.rev = 1;
  s.projects.data.n = 0;
  for (const auto& r : rows) s.projects.data.rows[s.projects.data.n++] = r;
}

inline void set_sessions(AppState& s, std::initializer_list<SessionRow> rows, uint32_t rx_ms = 0) {
  s.sessions.live = true;
  s.sessions.rx_ms = rx_ms;
  s.sessions.data.rev = 1;
  s.sessions.data.n = 0;
  for (const auto& r : rows) s.sessions.data.rows[s.sessions.data.n++] = r;
  s.sessions.data.total = s.sessions.data.n;
}

inline void set_queue(AppState& s, std::initializer_list<QueueRow> rows) {
  s.queue.live = true;
  s.queue.data.rev = 1;
  s.queue.data.n = 0;
  for (const auto& r : rows) s.queue.data.rows[s.queue.data.n++] = r;
}

inline void set_ready(AppState& s, bool agent_ok = true, bool cap_reply = false) {
  s.link.mode = LinkMode::Ready;
  s.link.agent = agent_ok ? AgentStatus::Ok : AgentStatus::Missing;
  s.link.cap_spawn = agent_ok;
  s.link.cap_reply = cap_reply;
}

// Two projects (cpa, blog) with a waiting tty session and an idle bg session in cpa.
inline AppState demo_state() {
  AppState s;
  set_projects(s, {project(1, "cpa", "Cardputer 助手", 1, 1, 2), project(2, "blog", "博客")});
  s.projects.data.other.run = 0;
  s.projects.data.other.n = 1;
  set_sessions(s, {session("8d3e7a19", 1, "架构设计", SessionKind::Tty, SessionState::Waiting, true, 540),
                   session("5c2e8a14", 1, "cpa/chord-tests", SessionKind::Bg, SessionState::Idle, true, 75, "a1b2c3")});
  set_queue(s, {queue_item("q17", 1, "add LittleFS fault-injection test"), queue_item("q18", 1, "write docs")});
  return s;
}

}  // namespace uitest
