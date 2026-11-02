// Home launcher logic (docs/13 §5.6, v0.4.0): a 3 x 3 grid of tiles, row-major
//   P Board     I Inbox     R Meet
//   N Note      Q AI queue  S Session
//   M Settings  L Lock      Tab Menu
// A tile never runs anything itself: it asks the shell to press a key for the user
// (ModuleContext::press), so it does exactly what that key does (docs/13 §3.4).
// Keys: a tile letter or Enter on the selected tile -> Opt+letter (the Tab tile -> Tab);
// 1-9 -> Opt+1-9; ; . , / move up, down, left, right, wrapping within the row or column.
// docs/20 D1 (U5): R and L (Meet, Lock) need Opt; their letters alone, like any letter that
// is no tile, press nothing, and the first such letter of a visit hints how to write a note
// (text typed on home by mistake: "remind me" no longer opens the meeting box). Their tiles
// show the Opt key (B4).
#pragma once

#include <cstdint>

#include "module.h"
#include "theme_mando.h"
#include "words.h"

namespace cpa {

constexpr KeyEvent home_chord(char k) { return KeyEvent{KeyEventKind::Chord, 0, kModOpt, k, Nav::None}; }

constexpr uint8_t kHomeCols = 3;
constexpr uint8_t kHomeTileCount = 9;

// Each tile as a menu row: the key it presses, its letter as drawn, what it opens.
// docs/21 §6.7: the tiles' names are words.h's, as the menu and the editor name them, where
// they fit: a tile has 53 px after a one-letter key, 31 after Opt+R, and a badge takes its
// width and 4 px. So in English P says "Board" (beside a count), R "Meet", S "Session"
// (the menu says "New session", 66 px). test_mod_home draws them all whole.
constexpr Txt kBoardTile{"项目", "Board", 37};
constexpr Txt kMeetTile{"开会", "Meet", 31};
constexpr TileTxt kSessionTile{"开 session", "Session"};
constexpr TileTxt kMenuTile{"菜单", "Menu"};
inline constexpr HelpRow kHomeTiles[kHomeTileCount] = {
    {home_chord('p'), "P", "", &kBoardTile},
    {home_chord('i'), "I", "", &txt::kWaiting},
    {home_chord('r'), "Opt+R", "", &kMeetTile},
    {home_chord('n'), "N", "", &txt::kNote},
    {home_chord('q'), "Q", "", &txt::kAiQueue},
    {home_chord('s'), "S", "", &kSessionTile},
    {home_chord('m'), "M", "", &txt::kSettings},
    {home_chord('l'), "Opt+L", "", &txt::kLock},
    {KeyEvent{KeyEventKind::Nav, 0, 0, 0, Nav::Menu}, "Tab", "", &kMenuTile},
};

struct HomeUi {
  uint8_t sel = 0;  // selected tile, row-major
  bool hinted = false;  // docs/20 D1: the letter hint was given on this visit
};

constexpr ToastTxt kHomeLetterHint{"主页上字母是快捷键：按 G0 记一条", "Letters are shortcuts: Note with G0"};

struct HomeStep {
  HomeUi ui;
  KeyEvent press;  // kind None: nothing to press
  bool handled = false;
  const char* hint = nullptr;  // docs/20 D1: say this (a letter that is no tile)
};

HomeStep home_next(const HomeUi& ui, const KeyEvent& ev);

// Sessions running in every project and in none (kRunningTile); 0 while the board is only
// the cached names (no counts yet).
uint32_t home_running(const AppState& s);

// The most urgent session that needs you (docs/20 U22): waiting first, the longest wait;
// then idle, the newest first; idle for a day or more is no one's most urgent (home_older
// counts it). Ages as of data_as_of(s, now). -1: none.
constexpr uint32_t kHomeOlderS = 24 * 3600;
int home_urgent(const AppState& s, uint32_t now = 0);
uint8_t home_older(const AppState& s, uint32_t now);  // "+N older sessions"

// docs/20 U16: why G0 and Opt+R would be refused now (rec::rec_refusal, as far as AppState
// tells it): the R tile is greyed and the hint says why, before a press fails.
enum class RecBlock : uint8_t { None, NoSd, Full, Pending, Battery };
RecBlock home_rec_block(const AppState& s);
// The hint bar: G0 first (D8: talk once ● REC shows; U16: why it cannot record), then Enter.
const char* home_hint(const AppState& s);

// Where the tile rows sit, from the content area's top: three rows (52, 16 px tall, 19 apart),
// or with docs/16's app tiles four (48, 14 px, 16 apart) so the last ends above the hint bar.
struct HomeGrid {
  uint8_t rows = 3;
  int16_t y = 52;
  int16_t h = 16;
  int16_t pitch = 19;
};
HomeGrid home_grid(uint8_t tiles);

// "10/21 Wed", the local date; empty while the time is unknown.
FixedStr<16> home_date(const TimeInfo& t, uint32_t now);

// docs/19 §6.3: "Morning" from 5:00, "Hi" from 11:00, "Afternoon" from 13:00, "Evening" from
// 18:00 (local time); "Hello" while the time is unknown. v0.5.0 (mando_eggs.h): "Late night"
// from 23:30 to 5:00, "Weekend" on Saturday and Sunday.
const char* home_greeting(const TimeInfo& t, uint32_t now);

// docs/19 §7.2: what Mando does on home, the first that holds: v0.5.0, a pat (Opt+W) or the
// all clear on show (mando_eggs.h) -> Hop; a recording saved under
// kHopMs ago -> Hop; one dropped as too short under kNoVoiceMs ago -> NoVoice; the Mac is on
// the cable and cpad does not answer -> Sniff; any other link that is not Ready -> Offline
// (docs/20 U19: what he knows is old, so no "!" for it); a session needs you -> NeedsYou;
// the outbox is syncing -> Sniff; otherwise Idle.
// (kHopMs and kNoVoiceMs are in app_state.h, beside Moment.)
theme::MandoAct home_mando(const AppState& s, uint32_t now);

}  // namespace cpa
