#include "action_help.h"

#include "words.h"

namespace cpa {
namespace {

struct ActionHelp {
  Action action;
  Txt text;
};

// In keymap.h's order, worded as docs/13 §5.2 and §6.3 write them, in one vocabulary
// (docs/20 U22: 记一条 · 排队给 AI · 开 session · 回复 · 项目 · 等你; words.h). Tab menu
// rows: 120 px in either language (docs/21 §6.4).
constexpr ActionHelp kHelp[] = {
    {Action::QuickNote, MenuTxt{"记一条 → 今日笔记", "Note → today's notes"}},
    {Action::RecMemo, MenuTxt{"说一段（语音便条）", "Talk (voice memo)"}},
    {Action::RecMeeting, MenuTxt{"会议录音（先确认）", "Meeting (asks first)"}},
    {Action::Home, txt::kHome},
    {Action::Inbox, txt::kWaiting},
    {Action::OpenProject, MenuTxt{"第 N 个项目页", "Project N"}},
    {Action::NewNote, txt::kNote},
    {Action::NewQueue, txt::kAiQueue},
    {Action::NewSpawn, txt::kNewSession},
    {Action::System, MenuTxt{"设置 Settings", "Settings"}},  // the way back for an English reader
    {Action::Escape, MenuTxt{"返回", "Back"}},
    {Action::Up, MenuTxt{"上移", "Up"}},
    {Action::Down, MenuTxt{"下移", "Down"}},
    {Action::TabPrev, MenuTxt{"上一个标签", "Previous tab"}},
    {Action::TabNext, MenuTxt{"下一个标签", "Next tab"}},
    {Action::Primary, MenuTxt{"打开 / 执行选中项", "Open / run this row"}},
    {Action::Dismiss, MenuTxt{"完成或忽略", "Done or dismiss"}},
    {Action::Back, MenuTxt{"返回", "Back"}},
    {Action::Submit, MenuTxt{"保存 / 发送", "Save / send"}},
    {Action::Cancel, MenuTxt{"取消", "Cancel"}},
    {Action::Target, MenuTxt{"改目标项目", "Change project"}},
    {Action::TypeNote, MenuTxt{"改成记一条", "Make it a Note"}},
    {Action::TypeQueue, MenuTxt{"改成排队给 AI", "Make it AI queue"}},
    {Action::TypeSpawn, MenuTxt{"改成开 session", "Make it New session"}},
    {Action::DeleteChar, MenuTxt{"删一个字（空时退出）", "Delete (empty: exit)"}},
    {Action::Confirm, MenuTxt{"确认", "Confirm"}},
    {Action::RecTag, MenuTxt{"归到项目，0 不归属", "To project, 0: none"}},
    {Action::RecStar, MenuTxt{"加 ★", "Add ★"}},
    {Action::RecEnd, MenuTxt{"结束会议…", "End meeting…"}},
    {Action::Menu, MenuTxt{"菜单", "Menu"}},
    {Action::Lock, txt::kLock},
    {Action::Unlock, MenuTxt{"解锁", "Unlock"}},
    {Action::Projects, MenuTxt{"项目看板", "Project board"}},
    {Action::Backtick, MenuTxt{"打出 `", "Type `"}},
    {Action::RecHandsFree, MenuTxt{"免提说一段", "Hands-free memo"}},  // gh #51
    {Action::RecStop, MenuTxt{"结束并保存", "Stop and save"}},
    {Action::PatMando, MenuTxt{"摸摸 Mando", "Pat Mando"}},  // an egg: no menu lists it
};

}  // namespace

const char* action_help(Action a) {
  for (const ActionHelp& h : kHelp) {
    if (h.action == a) return tr(h.text);
  }
  return "";
}

}  // namespace cpa
