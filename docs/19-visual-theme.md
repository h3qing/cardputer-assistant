**English** · [中文](19-visual-theme.zh-CN.md)

# 19 · Visual theme: Mando companion (direction one)

> Role: drafted by the UI owner, finalized and committed by the Lead. Date 2026-10-21. You chose direction one. This doc is the implementation plan: tokens and palette, canvas and display interfaces, the public theme interface, the asset pipeline, the layout of every screen, Mando's state machine, files per stage, tests, budgets, risks and what we will not do. Only code, docs and mockups were read; **no device was connected and no code was changed**.
> Based on: the direction-one spec, the comments of the two reviewers and three research reports (`scratchpad/visual/companion_spec.json`); mockups `scratchpad/visual/companion/01_home … 07_states.png`, `frames/anim_listen_1-4.png`, `frames/anim_regions@2x.png`, and the scripts that generated them `companion/src/{lib,s_home,s_board,s_menu,s_rec,s_lock,s_states,sprite,build_all}.py`; firmware `lib/hal/m5_display.*`, `lib/module_api/{canvas,module}.h`, `lib/shell/{platform.h,shell*.cpp,status_bar.*,rec_overlay.*,lock_screen.*,toast.*,confirm.*,runtime*.cpp,power.*}`, `lib/help/menu_view.*`, `modules/{home,board,inbox,system}`; simulator `firmware/sim/lib/{simfb,simpng,simdev}`, `firmware/sim/README.md`; `ci.yml`, `scripts/check_budget.py`, `scripts/stack_budget.py`, `tools/repo/check_sizes.py`; docs/16 and `AppTile` on the `feat/app-sdk` branch (not merged, read-only); WhisperWoof (read-only) `website/mando/hd/{wait,think,review,hop}.webp`, `src/assets/fonts/{Nunito-normal-latin.woff2,OFL.txt}`, `LICENSE`.
> Tags as in docs/13: **[src]** code or doc that was read · **[meas]** already measured · **[calc]** calculated · **[asm]** design assumption, can change · **[todo]** to verify with the simulator, the oracle or the real device · **👤** needs you.
> Coordinates are pixels on the 240×135 screen, (x, y, w, h) from the top-left corner. Character widths follow efontCN_12: ASCII 6 px, Chinese characters 12 px, `● … ← ↑ ★ ·` 6 px [src sim/README]. Contrast follows the WCAG formula, computed on the RGB565 values the panel actually shows [calc `scratchpad/theme_plan/contrast.py`].

## 0. One-page summary

**Goal**: replace the black-and-cyan "industrial" look with WhisperWoof's: a warm brown dark background, caramel focus, rounded cards and keycaps, and bring in Mando. Mando only moves when the state changes: nodding means listening, chin on paw means syncing, a head tilt with "!" means waiting for you, a hop means saved, eyes closed with z Z means dozing on the lock screen. The recording ● is always pinned to the top-left corner.

**How**:

1. **8-bit palette canvas**: still 240×135, 32,400 B; only M5GFX's RGB332 is swapped for `palette_8bit`, plus a 768 B palette (on the heap). About 130 of the 256 entries are used: 24 color tokens, Mando's 15 colors, dimmed copies, transition colors for rounded corners and the ●, two digit ramps, 6 levels of warm glow; the rest is kept for later (§2).
2. **Tokens only**: `Color` grows from 8 to 24 values. The old 8 keep their names and meanings and only become warm colors, so v0.4 modules and docs/16 apps change color without code changes. The canvas only accepts `Color`; passing an integer does not compile. Palette entries 0x00, 0x10, 0xE0 and 0xFF are magenta "sentinels": the low bytes of the 8 RGB565 constants of v0.4.0 fall exactly on these four positions, so if anyone sneaks a constant past the type, it draws magenta, and the simulator tests catch it at once (§3.2).
3. **One drawing code, shared by both sides**: rounded corners, the ●, sprites, large digits, dimming and the warm glow are all pure functions (`lib/theme/raster.*`); device and simulator write to the 8-bit buffer with the same code. Only Chinese text still goes through each side's own efont, and that part already has M5GFX oracle cross-checks. So simulator screenshots match the device pixel for pixel, and screenshots remain the review tool (§3.6).
4. **Partial pushes and an animation tick**: add `Display::present(Rect)` and a 200 ms animation tick. Each frame the recording page only pushes three areas, the Mando box, the waveform band and the large ●, about 2.8 ms; the whole-second refresh stays as it is. The screen's SPI time while recording drops from about 104 ms/s to about 27 ms/s [calc]. **Animation only runs while the backlight is On**: when dimmed or off nothing moves, and the ● is fixed at full brightness (§3.5, §7).
5. **Assets**: WhisperWoof's original Mando, 4-bit color, 17 frames at 46 px and 12 frames at 64 px, about 24 KB; Nunito 800 digits at 30/34 px, about 4 KB; with the palette tables, about 30 KB in all (limit 60 KB). The generator lives in `tools/theme/`, only reads WhisperWoof, and the headers it outputs are committed to the repo; the firmware build does not depend on WhisperWoof (§5).
6. **Public theme interface** `lib/theme/theme.h`: tokens, cards, keycaps, pills, Mando's sign, tiles, the hint bar, and Mando himself. Third-party apps (docs/16) use it directly; `Module::animate()` lets app pages animate too, likewise only while the backlight is On (§4).
7. **Four stages, each one shippable on its own**: S0 palette and tokens (no layout change) → S1 cards, keycaps, pills → S2 Mando, large digits, animation (recording, lock screen, home) → S3 Tab menu drawer and motion (§8).

**Review comments** were adopted only within direction one: the ● is fixed at full brightness when dimmed; the darkest breathing step is at least 4.5:1 on the background (finally #cb574c, 4.66:1); every card has a brighter 1 px top rim; no animation when the backlight is not On. Layouts from the other directions (big-title card home, 44/48 px digits, a solid red REC pill, icons) are not brought in (§1, §12).

**👤 Two things we need from you**: after S1, look at the warm dark background, ink3 text, mustard and caramel on the real device at 43% and 10.5% backlight (about 10 minutes); after S2 and after S3, run `test_runtime_budget` and `rec_soak` once each (HIL, the Lead will schedule it).

## 1. How direction one and the review comments are applied

| Source | Content | Handling |
|---|---|---|
| Direction one | Mockups 01–07 and animation frames | **Target**: layout, coordinates, colors, Mando's poses and sign all follow the mockups (§6, §7) |
| Reviews 1, 2 | The ● must freeze at full brightness under a dimmed backlight | **Adopted**. When the backlight leaves On, `breathe(0)`, and breathing stops at step 0 |
| Review 1 | The breathing low point #a2463d is only 3.2:1; raise it to about 70%, no lower than 4.5:1 | **Adopted and tightened**. To reach 4.5:1 on bg, the darkest it can go is about 83% brightness: #cb574c, 4.66:1 [calc]. 70% (#b04c43) is only 3.6:1, not enough |
| Spec | The status bar ● breathes together with the large ● | **Changed**: the status bar ● does not breathe. It sits on the rec_bg pill, where the darkest step is only 3.47:1; the ● in its fixed slot is always full-brightness Rec (4.81:1 on rec_bg). Only the large ● on the recording page breathes, which also means one area fewer to push |
| Reviews 1, 2 | Cards with only a fill (1.15:1 on bg) disappear on the panel; they need a top rim, a color bar or an outline | **Adopted**. Normal cards get a 1 px `CardRim` top rim (1.59:1 on card, 1.84:1 on bg); the drawer and the lock card use `Rim` (2.25:1 on sheet); the selected card has a `Live` outline; waiting cards also get a mustard bar |
| Reviews 1, 2 | No animation when the backlight is not On; do not mark dirty when the screen is off | **Adopted** (§3.5, §7.4) |
| Review 1 | Text meant to be read is no lower than ink3, about 4.5:1 | **Adopted and corrected**: WhisperWoof's ink3 #8f8276 is only 4.47:1 on a card, so it goes up to **#96897b** (4.97:1 on card, 5.73:1 on bg). ink3 is not used on selected backgrounds |
| Reviews 1, 2 | The 11×9 dog head might be read as a fox or a cat | Use it first; if anyone misreads it, switch to a 5×5 dot image (not the "●" character, to avoid confusion with the REC ●). A one-line change |
| Reviews 1, 2 | Mustard (waiting) and caramel (focus) differ by only 1.17:1 | Follow direction one's rule: waiting always carries "!", a left color bar, or the word "waiting", never color alone |
| Review 1 | The home page is too crowded (five areas) | Keep the mockup layout and only subtract within direction one: remove the "Home · date" title (the weekday moves into the status bar, as in the mockup), move "Hold G0 to speak" into the hint bar, do not show `!0` |
| Graft suggestions from review | Big-title card home, 44/48 px digits, solid red REC pill, 12×12 icons, waveform fade-out, sliding on every cursor move, battery icon, a palette just for Dim | **Not brought in**: these come from the layouts of directions two and three (§12) |

## 2. Tokens and palette

### 2.1 Color tokens (`Color`)

The colors come from WhisperWoof's dark theme (DESIGN.md, `.dark` in `src/index.css`) plus REC [src research A]. The old 8 come first, with unchanged values and meanings. "Index" is the position in the palette (§2.2), mapped by the generated table; it is not the enum value.

| Value | Token | Color | Index | Use | Contrast (on bg / card) |
|---|---|---|---|---|---|
| 0 | `Bg` | #120e0b | 0x01 | Page background | — |
| 1 | `Fg` (alias `Ink`) | #f3ede6 | 0x02 | Body text | 17.1 / 14.8 |
| 2 | `Dim` (alias `Ink3`) | #96897b | 0x03 | Secondary text, hints | 5.73 / 4.97 |
| 3 | `Accent` | #e2a867 | 0x04 | Focus, G0, "listening", waveform | 9.58 / 8.30 |
| 4 | `Ok` | #72c291 | 0x05 | runN, saved | 9.24 |
| 5 | `Warn` | #dcc15a | 0x06 | Waiting, !N, ★ | 11.2 / 9.69 |
| 6 | `Error` (alias `Rec`) | #f4675b | 0x07 | ● and recording timer, errors | 6.46; 4.81 on rec_bg |
| 7 | `Select` | #463525 | 0x08 | Background of the selected card and of the not-landed pill | (background) |
| 8 | `Sheet` | #1a1512 | 0x09 | Lock card; the background premixed into Mando's edges | 1.06 on bg |
| 9 | `Card` | #221c18 | 0x0A | Cards, Tab drawer | 1.15 on bg |
| 10 | `Raised` | #2c241e | 0x0B | Neutral pill | 1.26 on bg |
| 11 | `Line` | #3a2f27 | 0x0C | Track, scroll bar | 1.44 on bg |
| 12 | `Cap` | #43372d | 0x0D | Keycap | 1.63 on bg |
| 13 | `Rim` | #5a4e44 | 0x0E | Top rim of the drawer and the lock card | 2.25 on sheet |
| 14 | `CardRim` | #4a3e35 | 0x0F | Top rim of normal cards [asm] | 1.59 on card |
| 15 | `Ink2` | #bfb2a6 | 0x11 | Second-level text, text on keycaps | 9.35 / 8.10; 5.74 on cap |
| 16 | `Accent2` | #c48a4c | 0x12 | Fur color: second waveform color, keycap of the selected item | — |
| 17 | `OnAccent` | #1d130b | 0x13 | Text on caramel | 9.25 on accent |
| 18 | `Live` | #926d45 | 0x14 | Selected card outline, not-landed pill outline, timeline | — |
| 19 | `OkBg` | #272f25 | 0x15 | Green pill background | ok on it 6.89 |
| 20 | `WarnBg` | #372f1d | 0x16 | Mustard pill background | warn on it 8.02 |
| 21 | `RecBg` | #3a2520 | 0x17 | Status bar REC pill background | rec on it 4.81 |
| 22 | `InfoBg` | #36291c | 0x18 | Mando's sign (here / listening) | accent on it 7.14 |
| 23 | `RecDot` | breathing (§2.3) | 0x19 | The large ● on the recording page, used only there | darkest step 4.66 |

- There is also an internal entry, `RecDotEdge` (0x1A): the anti-aliased edge of the large ●, which breathes along with it; it is not exposed to modules.
- All of the old 8 changed value: `Fg` is warm white, `Dim` is ink3, `Accent` goes from cyan to caramel, `Select` goes from dark blue to #463525. Text drawn on `Select` today is `Fg` (10.6:1) or `Dim`; `Dim` on Select is only 3.5:1, so S0 changes those places to `Ink2` (5.77:1). Exception: grey text that means "not available now" (queue items while offline, `test_offline_items_are_drawn_grey`) still uses `Dim`, as in WCAG's exception for disabled controls.
- The dark surfaces stay distinct in RGB565: bg/sheet/card/raised/line display as #100c08/#181410/#211c18/#292418/#392c21 [calc].

### 2.2 Palette layout (256 entries, `palette_8bit`)

| Index | Content | Entries |
|---|---|---|
| 0x00 | **Sentinel** (magenta #ff00ff): v0.4.0's `0x0000` and `0xF800` land here | 1 |
| 0x01–0x0F | Tokens 0–14 | 15 |
| 0x10 | **Sentinel**: `0x8410`, `0x0010` | 1 |
| 0x11–0x19 | Tokens 15–23 | 9 |
| 0x1A | `RecDotEdge` (breathing) | 1 |
| 0x1B–0x1F | Reserved for future tokens | 5 |
| 0x20–0x2E | Mando m1–m15 (the 4-bit value n in a sprite draws as 0x1F+n) | 15 |
| 0x30–0x3E | Mando's dimmed copies (dimmed drawing: 0x2F+n) | 15 |
| 0x40–0x5F | Dimmed copies of the tokens | up to 32 |
| 0x60–0x9F | Transition colors for rounded corners and the ● (about 35 after deduplication in RGB565) | up to 64 |
| 0xA0–0xA3 | Digit ramp: Fg on Bg, 20/40/60/80% | 4 |
| 0xA4–0xA7 | Digit ramp: Fg on Sheet | 4 |
| 0xA8–0xAD | Warm glow: bg→glow(#6e4521), 6 levels | 6 |
| 0xAE–0xDF | Reserved (future tokens, apps) | 50 |
| 0xE0 | **Sentinel**: `0x07E0`, `0xFFE0` | 1 |
| 0xE1–0xFE | Reserved | 30 |
| 0xFF | **Sentinel**: `0xFFFF`, `0x07FF` | 1 |

About 130 entries are used [calc] (the mockups registered 115 names, 92 RGB565 colors after deduplication [meas build_all.py]). Every unused entry is magenta, so any "wrong index" draws magenta.

### 2.3 Derived entries

- **Transition colors for rounded corners and the ●**: one 50% mix for each pair (fill color, color underneath), table `kAa[24][24]` (576 B). The pairs the theme actually uses (about 29 in the mockups) each get an exact color; other pairs map to the nearest existing entry, never a sentinel. So third-party apps can combine anything without wrong colors; the edges are just less round.
- **Digit ramps**: the Nunito digits have 6 grey levels (0–5): 0 is not drawn, 5 draws `Fg`, 1–4 use the four ramp entries. Only two pairs are baked: Fg on Bg (recording timer) and Fg on Sheet (lock-screen clock). The four indexes are contiguous, so a VLW font could use them directly if we ever switch [src research B §1]. On other backgrounds, digits fall back to "draw Fg at grey ≥3, otherwise nothing", with no wrong colors.
- **Dimming** (the page under the Tab menu): each token and Mando color has a dimmed copy, the original color mixed 62% toward bg (the same value as the mockups). Lookup table `kDim[256]` (256 B); other entries (transition colors, ramps) map to the nearest dimmed copy, and `dim(bg) = bg`. No checkerboard dithering.
- **Warm glow** (one corner outside the lock card): 6 levels from bg→glow plus a 4×4 Bayer ordered dither, drawn only on pixels outside the lock card that are still bg; static.
- **REC breathing**: the two entries `RecDot` and `RecDotEdge`, 4 steps × 200 ms:

| Step | RecDot | On bg | RecDotEdge |
|---|---|---|---|
| 0 | #f4675b (rec, 100%) | 6.46:1 | half and half with bg |
| 1 | #e05f54 | 5.64:1 | same |
| 2 | #cb574c (about 83%) | 4.66:1 | same |
| 3 | same as step 1 | 5.64:1 | same |

  Each cycle is 0.8 s (about 1.25 Hz), and it never dims to invisible. When the backlight is dimmed, the screen is off or the menu is open, it stops at step 0. Only these two palette entries change, and then the 12×12 area of the large ● is pushed.

### 2.4 Sizes

| Name | Value | Used in |
|---|---|---|
| `kGutter` | 4 px | Both sides of the screen |
| `kCapH` | 12 px, radius 2–3 | Keycaps, status bar pills, board count pills |
| `kSignH` | 13 px, radius = half the height | Mando's sign, the small pill on the lock card |
| Tile | height 16, row pitch 19, radius 5 | Home |
| Card | radius 5 (home waiting card) / 6 (board) | Cards |
| Drawer | radius 8, only the two left corners rounded | Tab menu |
| Lock card | radius 10 | Lock screen |
| live-words pill | height 14, radius 7 | "Save to Unassigned" (存到 未归属) |

- All text is efontCN_12, no extra Chinese font (flash +0 B). Page titles use faux bold: drawn once at x and once at x+1.
- Large digits are Nunito ExtraBold 800 in **fixed-width cells** (WhisperWoof's rule: digits are tabular), so the timer does not jump as it ticks: timer 30 px, cell 17, colon 7, height 21; lock-screen clock 34 px, cell 20, colon 8, height 24 [calc from glyphs measured in the mockups, 16×21 / 19×24]. Only `0-9` and `:`.

### 2.5 Contrast rules (the tests are the acceptance)

- Text meant to be read ≥ 4.5:1, body text (ink) ≥ 7:1. `test_theme_palette` lists every (text color, background) pair the theme actually uses and asserts each one.
- On selected backgrounds (Select, Accent) only `Fg`, `Ink2`, `OnAccent`; the only exception is grey text meaning not available (`Dim`).
- Every breathing step ≥ 4.5:1 on bg; the status bar ● ≥ 4.5:1 on RecBg.
- Waiting never relies on color alone: it always carries "!", a color bar, or "waiting".
- These are computed on-screen values. Whether they are actually readable at 43% and 10.5% backlight has to be seen on the real device (👤, §11 item 1).

## 3. Canvas and display interface changes

### 3.1 Device: palette canvas

`M5Display::begin()` only changes these lines [src m5_display.cpp:46-55; research B §1]:

```cpp
sprite_.setColorDepth(lgfx::palette_8bit);
if (!sprite_.createSprite(kScreenW, kScreenH)) return false;   // 32,400 B，和今天一样
sprite_.createPalette(theme::kPalette888, 256);                // +768 B 堆
raster_ = theme::Raster{static_cast<uint8_t*>(sprite_.getBuffer()), kScreenW, kScreenH};
```

- On a palette sprite, M5GFX truncates color arguments to `c & 0xFF` as an index, and also stores text colors as raw values [src research B, colortype.hpp:775, LGFXBase.hpp:692-703]. So text uses `setTextColor(uint32_t(fg_index), uint32_t(bg_index))`. [todo oracle, S0 adds a case]
- M5GFX's runtime anti-aliasing (`fillSmooth*`, `drawWideLine`, `pushAlphaImage`) draws wrong colors on a palette canvas [src research B], so none of it is used. Rounded corners, the ● and digits all use pre-baked palette entries.
- Non-text drawing writes directly into the 8-bit buffer that `getBuffer()` points to (one byte per pixel, stride 240). S0 asserts `bufferLength() == 240×135` in `begin()`. [todo]
- When pushing, each pixel is looked up in the palette and converted to RGB565; the time is unchanged, about 13 ms for the full screen, limited by the bus speed [src research B §4].

### 3.2 Tokens only: RGB565 constants cannot get in

- **Compile time**: every `Canvas` call only accepts `enum class Color`, so integers cannot be passed; `canvas.h` has `static_assert(!std::is_convertible<uint16_t, Color>::value)`. On the device, `M5Canvas` is hidden inside the hal as a private member of a `PaletteSprite` wrapper; the wrapper only has methods that take `PalIndex`, and `PalIndex` can only be built by `theme::index_of(Color)` or by the theme's internal tables. `M5Display` itself cannot reach a bare `fillRect(…, 0xF800)` either.
- **Run time**: the sentinels of §2.2. `test_theme_palette` asserts that every token lands on a non-sentinel index and that the color matches the table; the simulator's `test_theme_sim` asserts that no frame of `v04_tour` contains a sentinel index. For values outside the token range (`static_cast<Color>(24)` and up), `index_of` returns sentinel 0x00, which draws magenta and cannot pass as Bg (review 2026-10-22).
- **Explicit casts are blocked too**: `static_cast<Color>(0xF800)` truncates to 0x00, which is exactly Bg, and `0x8410` truncates to 0x10, which is exactly Accent2, so the sentinels would not catch them. So the include-rule step in ci.yml also greps: outside `lib/theme`, `static_cast<Color>`, `(Color)x` and `Color{…}` must not appear in `lib`, `modules` or `src`.
- **One table**: today there are two color tables, the hal's `rgb565()` and the sim's `rgb565_of()`, kept in line by `-Werror=switch` [src sim/README "Code the simulator repeats"]. From now on there is only one, `theme::kPalette888`, written by the generator.

### 3.3 New `Canvas` primitives (`lib/module_api/canvas.h`)

The original four calls are unchanged. The new calls all have default implementations, so a v0.4 `Canvas` (for example someone's test double) still compiles; it just falls back to square corners and no images:

```cpp
constexpr uint8_t kCornerTL = 1, kCornerTR = 2, kCornerBL = 4, kCornerBR = 8, kCornersAll = 15;
enum class ArtId : uint8_t;                       // lib/theme/art.h（生成的帧列表）
enum class DigitFont : uint8_t { Timer, Clock };  // Nunito 800，30 px / 34 px，等宽

class Canvas {
 public:
  // v0.1：fill、text、textWidth、lineHeight（不变）
  // v0.5（docs/19 §3.3）。默认实现：方角、文字代替数字、不画图、不压暗。
  virtual void fillRound(const Rect& r, uint8_t radius, Color paint, Color under,
                         uint8_t corners = kCornersAll);       // 圆角：一级过渡色
  virtual void dot(int16_t cx, int16_t cy, uint8_t diameter, Color paint, Color under);
  virtual void art(int16_t x, int16_t y, ArtId id, bool dimmed = false);  // 4 位图，0 透明
  virtual int16_t digits(int16_t x, int16_t y, const char* text, DigitFont f, Color ink,
                         Color under);                          // 只画 0-9 和 :，返回宽度
  virtual int16_t digitsWidth(const char* text, DigitFont f) const;
  virtual void dim(const Rect& r);    // 把 r 里每个像素换成它的压暗副本（Tab 菜单下面）
  virtual void glow(const Rect& r);   // 锁屏的暖光：只改 r 里仍是 Bg 的像素
};
```

- There is only one implementation: `theme::RasterCanvas` (`lib/theme/raster_canvas.*`) implements all of the above and `fill()` on a `Raster` (pointer, width, height), leaving `text()` / `textWidth()` to subclasses. `M5Display` and the simulator's `FbCanvas` both inherit from it and each only implements text.
- `FakeCanvas` (native tests) records the new calls as `ShapeOp` (kind, rect, color, `ArtId`, text), so tests can assert "a card was drawn" or "Mando is at (7,14)". `digits()` also records a `TextOp`, so `has("0:12")` still finds the timer.

### 3.4 `Display` (`lib/shell/platform.h`)

```cpp
class Display {
 public:
  virtual Canvas& canvas() = 0;
  virtual void present() = 0;                                    // 整帧（不变）
  // v0.5：只推 r（裁到屏内）。默认推整帧。
  virtual void present(const Rect& r) { (void)r; present(); }
  // v0.5：REC 呼吸到第 step 步（0-3，0 = 全亮）。只改 RecDot / RecDotEdge 两项色板；
  // 调用方接着推显示大 ● 的区域。默认什么都不做。
  virtual void breathe(uint8_t step) { (void)step; }
};
```

- Device: `present(r)` is `Display.setClipRect(r); sprite_.pushSprite(0, 0); Display.clearClipRect();`; pushImage clips on the target side and only sends the visible part [src research B §4, LGFXBase.cpp:1435-1453]. `breathe()` uses `setPaletteColor()`.
- A subclass that overrides only one `present` needs `using Display::present;`; the Runtime always calls through `Display&`, so it is not affected.

### 3.5 Render loop and animation tick

Today: when dirty, redraw and push the whole screen, at most 20 fps, plus once per second [src runtime.cpp:220-230]. The recording level arrives every 128 ms per block, and `mirror_rec` marks dirty on every block, so while recording there are about 8 full-screen pushes per second, about 104 ms/s [src research B §0].

From now on there are two paths:

```cpp
void Runtime::render(uint32_t now) {
  const bool due = shell_.dirty() && now - last_render_ >= kRenderMinMs;
  const bool stale = !rendered_ || now - last_render_ >= kRenderEveryMs;
  if (due || stale) return render_full(now);       // 和 v0.4.0 一样：整屏画、整屏推
  if (level_ != Backlight::On) return;             // 调暗、熄屏：什么都不动
  const AnimFrame f = shell_.animate(disp_.canvas(), now);  // 只重画到点的区域
  if (f.breath >= 0) disp_.breathe(static_cast<uint8_t>(f.breath));
  for (uint8_t i = 0; i < f.n; ++i) disp_.present(f.rects[i]);
}
```

Rules:

1. **Animation only runs at `Backlight::On`**. `sync_backlight()` tells the Shell the level (`shell_.set_backlight(lv)`); when it leaves On, `disp_.breathe(0)`, and the ● returns to full brightness.
2. **Animation does not mark dirty and does not touch power**: `animate()` only redraws a few small areas and returns the rectangles to push (at most 4). It never keeps the screen on a second longer, and never draws while the screen is off.
3. **A full redraw only draws the "frozen" look**: Mando's pose, the breathing step, the waveform and the timeline all live in the animation state of the Shell (or module), and only `animate()` advances them. So while dimmed, the once-per-second full refresh only updates the timer digits; pose and waveform do not move.
4. **The level no longer triggers a full redraw**: when `Runtime::rec_event` receives `EvtKind::Level`, it calls `shell_.on_level(level)` to record it in the level history (pure function, does not mark dirty); when `mirror_rec` sees that only `level` changed, it updates AppState but does not mark dirty. The history keeps recording while dimmed or off, so the meeting timeline has no gaps.
5. **Rendering still runs in the loop task**; the audio task's priority and timing do not change. The longest step is still a full redraw (about 3–5 ms to draw + about 13 ms to push), far below the 50 ms per-step limit [calc].
6. Pace: Mando, waveform and breathing every 200 ms (`kAnimTickMs`); the lock screen's z every 600 ms; the drawer and the segment slider 50 ms per frame, 3 frames in all. A key press in the middle (full redraw) jumps straight to the end.

New in the Shell (`lib/shell/shell.h`):

```cpp
struct AnimFrame { uint8_t n = 0; Rect rects[4]; int8_t breath = -1; };  // breath -1：不变
void set_backlight(Backlight lv);              // Runtime::sync_backlight
void on_level(uint16_t rms);                   // 每个音频块（Runtime::rec_event）
void moment(Moment m, uint32_t now);           // 存好了 / 没听到（§4.3）
AnimFrame animate(Canvas& c, uint32_t now);    // 只在 On 时由 Runtime 调用
```

`stack_budget.py` adds one chain: `loop → Runtime::render → Shell::animate`.

### 3.6 Simulator (`firmware/sim/lib/simfb`)

- `Frame` is still 8-bit, but it stores palette indexes, no longer RGB332. `rgb565_of`, `rgb332_of` and `panel_rgb` are deleted and replaced by the RGB565 display values of `theme::kPalette888`.
- `FbCanvas` inherits from `theme::RasterCanvas` and only keeps efont text (`walk` unchanged; it passes indexes).
- `SimDisplay` holds a copy of the palette: `breathe()` changes the copy, `present(r)` only copies r from the sprite to the panel, `present()` copies the whole frame. PNGs are written from "panel frame + the palette at that moment", still as 8-bit indexed PNGs (`simpng` keeps its format).
- Partial pushes plus palette changes are faithful in the simulator too, provided `RecDot` pixels only appear inside the large ● area that gets pushed. `test_theme_sim` checks this.
- The oracle (`oracle/oracle.sh`) adds palette cases: an M5GFX `palette_8bit` sprite plus the same `RasterCanvas` writing the buffer, compared byte by byte with `FbCanvas`. `test_efont/oracle_golden.h` is only regenerated from the oracle. [todo: needs SDL2 on this machine]
- The backlight still does not simulate brightness: screenshots while dimmed or off show full-brightness pixels, and `shots.json` records the level (as today).

## 4. Public theme interface (for third-party apps)

### 4.1 `lib/theme/theme.h`

Pure code; does not include Arduino / M5 / esp (added to the include rules in ci.yml). Everything draws on `Canvas&`, so apps, the shell and `FakeCanvas` can all use it.

```cpp
#pragma once
#include "canvas.h"

namespace cpa {
namespace theme {

// 尺寸（docs/19 §2.4）
constexpr int16_t kGutter = 4;
constexpr int16_t kCapH = 12;
constexpr int16_t kSignH = 13;
constexpr uint8_t kRadiusTile = 5;
constexpr uint8_t kRadiusCard = 6;
constexpr uint8_t kRadiusSheet = 8;

// 一种语气 = 一对（底色, 字色）。每对都 ≥ 4.5:1（test_theme_tokens）。
enum class Tone : uint8_t { Neutral, Info, Ok, Warn, Rec, Accent };
struct ToneInk { Color fill; Color ink; };
ToneInk tone_ink(Tone t);  // Neutral Raised/Ink2 · Info InfoBg/Accent · Ok OkBg/Ok ·
                           // Warn WarnBg/Warn · Rec RecBg/Rec · Accent Accent/OnAccent

// 卡片：圆角填充加一条更亮的 1 px 顶边（光靠填充在面板上看不见）。
enum class CardKind : uint8_t { Plain, Selected, Sheet };  // Card+CardRim · Select+Live 描边 · Card+Rim
Color card_fill(CardKind k);                               // 卡里的字画在这个颜色上
void card(Canvas& c, const Rect& r, CardKind k = CardKind::Plain, Color under = Color::Bg,
          uint8_t radius = kRadiusCard);

// 键帽 [Enter]：Cap 底、Ink2 字（G0 用 Accent）。返回右边的 x。
int16_t keycap_width(const Canvas& c, const char* key);
int16_t keycap(Canvas& c, int16_t x, int16_t y, const char* key, Color under = Color::Bg,
               Color ink = Color::Ink2, Color cap = Color::Cap);

// 胶囊：跑1、!2、等你 1。返回宽度。数量为 0 时调用方画普通 Ink3 字。
int16_t pill(Canvas& c, int16_t x, int16_t y, const char* text, Tone t, Color under,
             int16_t h = kCapH);

// Mando 的牌子：以 xc 为中心的胶囊，高 13。返回它的矩形。
Rect sign(Canvas& c, int16_t xc, int16_t y, const char* text, Tone t, Color under = Color::Bg);

// 等你的卡左边的竖色条（3 px 宽的胶囊）。
void stripe(Canvas& c, int16_t x, int16_t y, int16_t h, Tone t, Color under);

// 页面标题：假粗体（x 和 x+1 各画一次）。返回宽度。
int16_t title(Canvas& c, int16_t x, int16_t y, const char* utf8, Color under = Color::Bg);

// WhisperWoof 的 live-words：还没落地的东西（存到 未归属）。Select 底、Live 描边、Accent 字。
int16_t live_capsule(Canvas& c, int16_t x, int16_t y, const char* text, Color under);

// Mando 头上的 "!"（等你）或 "?"（没听到）气泡；step 0 低 2 px，1 到位（两步弹出）。
void bubble(Canvas& c, int16_t x, int16_t y, char mark, Tone t, Color under, uint8_t step);

// 主页格子（kHomeTiles 和 docs/16 的 AppTile 都用它）。
struct TileSpec { const char* key; const char* label; const char* badge; Color badge_ink; };
void tile(Canvas& c, const Rect& r, const TileSpec& t, bool selected);

// 提示栏：把模块的 hint() 字符串拆成"键帽 + 说明"（§4.2）；放不下时退回 v0.4 的一行字。
// hint_fits 就是这条规则（docs/16 的 check_apps 用它查应用的提示）。
void hint_bar(Canvas& c, const char* hint);
bool hint_fits(const Canvas& c, const char* hint);
// 同样的写法画在别处（确认卡里的 "Enter 开始    ` 取消"）：键是键帽，说明用 ink。返回宽度。
int16_t keys_text(Canvas& c, int16_t x, int16_t y, const char* text, Color ink, Color under);
// 提示行上的 toast：整行胶囊，语气色。
void toast_bar(Canvas& c, const char* text, Tone t);

// Mando（§7）
enum class MandoSize : uint8_t { M46, M64 };           // 26×46 / 36×64
enum class MandoAct : uint8_t { Idle, Listen, Tilt, Sniff, NeedsYou, Hop, NoVoice, Doze };
Rect mando_box(MandoSize s, int16_t x, int16_t y);      // 含气泡
Rect mando(Canvas& c, int16_t x, int16_t y, MandoSize s, MandoAct a, uint8_t pose,
           Color under = Color::Bg);                    // 画 sprite（和气泡），返回画到的区域
const char* mando_sign(MandoAct a);   // 在呢 在听 在听 嗅一嗅… 等你 存好了 没听到 打盹中
Tone mando_sign_tone(MandoAct a);     // Info Info Info Neutral Warn Ok Neutral Neutral

}  // namespace theme
}  // namespace cpa
```

### 4.2 Keycap rules for the hint bar (`lib/theme/hint.*`, pure functions)

The modules' `hint()` strings **do not change by a single character** (docs/16's `expect_app_contract` requires them to end with "Tab菜单" (Tab menu), and tests also check the original text). The theme splits them up for drawing by these rules:

1. Split into words by spaces; runs of spaces only group.
2. The visible ASCII (`!` to `~`) at the start of a word is the key, the rest is the label: `Enter打开` → [Enter] 打开 (open); `;.选择` → [;.] 选择 (select); `Opt+1-9项目` → [Opt+1-9] 项目 (project); `Enter开session` → [Enter] 开session (start session) (a space would split the label into the next word, so the hint bar writes `开session`; elsewhere, per docs/20 U22, it is written "开 session").
3. When a key has no label and the next word starts with a Chinese character, the next word is the label: `Enter 确认` → [Enter] 确认 (confirm); `` ` 取消`` → keycap `` ` `` plus the label "取消" (cancel).
4. Consecutive pure-ASCII words merge into one key: `Opt N/Q/S 类型` → [Opt N/Q/S] 类型 (type).
5. A word starting with a Chinese character is a plain label (Ink3), except the two Chinese key names `退格` (Backspace) and `空格` (Space).
6. The `G0` keycap text is Accent, the others are Ink2, labels are Ink3. Single-character keys (`` ` ``, `0`) get a keycap at least 12 px wide, as in the menu; otherwise `` ` `` looks like a smudge.
7. Layout: start at x=4, item spacing 8; if it does not fit, spacing 4; if it still does not fit, x=1 and spacing 3; if it still does not fit, fall back to the v0.4 drawing (one line of Ink3, `draw_fit`). `hint_fits` tells whether one of the first three tiers fits. Shell and module hints should all fit in the first tier (`test_mod_board` checks this); hints squeezed into the last two tiers look crowded and should drop an item (they are all still in the Tab menu).

### 4.3 Additions to Module and AppState (all with defaults)

```cpp
// module.h
// v0.5（docs/19 §3.5、§7）：上次调用以来这个页面上动了什么，画进 c，返回要推的矩形
// （w == 0：没有）。Shell 每 kAnimTickMs 调一次，只在背光 On、页面上面没有浮层和
// 菜单时调。不能置脏。默认什么都不动。
virtual Rect animate(Canvas& c, const Rect& area, uint32_t now) {
  (void)c; (void)area; (void)now;
  return Rect{};
}

// app_state.h：UiFlags 多两个字段（+8 B），Shell 在存好了 / 丢弃时写入
enum class Moment : uint8_t { None, Saved, NoVoice };
struct UiFlags { bool menu = false; bool locked = false; Moment moment = Moment::None; uint32_t moment_ms = 0; };
```

### 4.4 Rules for apps

- Use only tokens and `theme.h`; do not pick your own colors. Every (text color, background) pair must be in the table of §2.5; `check_apps.py` can check this later.
- Text meant to be read uses `Fg`, `Ink2` or `Dim` (ink3); `Dim` does not go on `Select` or `Accent` (except grey text meaning not available).
- Only move when the state changes (WhisperWoof's rule). The area `animate()` returns should be small (about the size of the Mando box); do not animate the whole page.
- The REC colors (`Error` / `RecDot`) are only for recording and real errors; `Warn` is only for "waiting for you".
- When merging `feat/app-sdk`: `AppTile.badge_fg` is already a `Color`, so it just takes the warm colors; the extra fourth row on home is drawn with `theme::tile()`, laid out as the "four-row version" in §6.3 (`home_grid(tile count)`); add `theme` to the includes allowed by `tools/apps/app_rules.py`, or `check_apps` will reject apps that include `theme.h`; switch `check_apps`'s hint width check to `theme::hint_fits` (a hint that fits in 240 px as plain text may not fit once drawn as keycaps, and then falls back to grey text). See §13.

## 5. Asset pipeline (`tools/theme/`)

### 5.1 Files

| File | What it does |
|---|---|
| `tokens.py` | The single token table: names, enum order, colors, palette layout, sentinels, transition pairs to bake exactly, dimming factor 0.62, the four breathing steps |
| `gen_palette.py` | From `tokens.py` and `mando_colours.json`, generates the 256-entry palette, the token index table, `kAa[24][24]`, `kDim[256]`, the two digit ramps, the warm glow and the breathing table → `lib/theme/gen/palette_gen.h` |
| `gen_mando.py` | **Only reads** WhisperWoof's `website/mando/hd/{wait,think,review,hop}.webp`: crops every frame to the same box (the union of all frames of every action, recorded in the spec as (82,12,302,404)), scales to 46 and 64 px, premixes edges onto the Sheet color, treats pixels with under 30% coverage as transparent, runs one 15-color median cut over all 64 px frames, shared by both sizes. Picks the key frames of §7.1 and packs them as 4-bit (high nibble first) → `gen/mando46.h`, `gen/mando64.h`, `gen/art_ids.h`, and writes `tools/theme/mando_colours.json` (committed, so redoing only the palette does not need WhisperWoof) |
| `gen_icons.py` | Hand-drawn small images: the 11×9 head in the status bar (Mando colors only), the dozing eyelids (6 pixels changed on wait frame 0) → `gen/icons.h` |
| `gen_digits.py` | **Only reads** `src/assets/fonts/Nunito-normal-latin.woff2`: weight 800, 30 and 34 px, `0-9` and `:`, each glyph placed in a fixed-width cell, grey quantized to 0–5, 4 bits per pixel → `gen/digits_gen.h` |
| `gen_all.py` | Runs the four above in order. `--whisperwoof PATH`; `--check` only compares and does not write, exiting 1 on any difference; `--palette-only` does not need WhisperWoof |
| `sources.json` | Path and sha256 of every input and the Pillow version, also written into the header comment of every generated header |
| `tests/` | pytest: palette ≤256, sentinels not taken, every token has an entry, every allow-listed pair has an exact entry, `kDim` complete, pack and unpack are inverses, two generations are byte-identical, `--check` detects changes |
| `README.md` | How to regenerate; licenses |

- Each generated header is at most 400 lines (48 bytes per line, split into files by size), and `check_sizes` checks them as usual. Arrays are `const`, live in flash, and use no static RAM.
- No PNG or WebP decoding on the device (pngle needs at least 32 KB each time) [src research B §3], and no images or fonts read from the SD card (while recording, the card belongs to the audio task).

### 5.2 Regenerating

```sh
uv run --with pillow --with fonttools --with brotli python tools/theme/gen_all.py \
    --whisperwoof <WhisperWoof checkout>
git diff --stat firmware/app/lib/theme/gen tools/theme/mando_colours.json
(cd firmware/app && pio test -e native -f 'core/test_theme_*')
```

- Pillow reads animated WebP with its bundled libwebp; reading woff2 needs FreeType with brotli, otherwise `gen_digits.py` first unpacks it to TTF with fontTools (in a temporary directory, never writing to WhisperWoof). [todo: confirm on the Lead's machine]
- CI has no WhisperWoof: the `repo` job only runs `pytest tools/theme`, `gen_all.py --check --palette-only`, ruff, `check_sizes`, `lizard -L 50`. Sprites and digits are taken from the committed headers.
- These files are byte-identical in the two WhisperWoof directories (the main repo and the `cardputer-inbox` worktree) [meas cmp]; either one works.

### 5.3 Sources and licenses

- **WhisperWoof** (MIT, Copyright (c) 2026 Heqing Huang, the same author as this repo): Mando sprite frames and the dark design tokens. NOTICE gets a section: source files, sha256, how they were converted (crop, scale, 15 colors, 4-bit).
- **Nunito** (SIL OFL 1.1, Copyright 2014 The Nunito Project Authors, no Reserved Font Name): only bitmaps of 11 glyphs are baked. The full OFL text is copied from WhisperWoof's `src/assets/fonts/OFL.txt` to `LICENSES/OFL-1.1.txt`, and NOTICE states the source and use.
- Each generated header starts with its source, license and "do not edit by hand; regenerate with tools/theme/gen_all.py".

## 6. Layout of every screen (matching the mockups)

Coordinates come from the mockup scripts [src companion/src/s_*.py, lib.py]. "S1/S2/S3" is the stage in which that part is finished. This doc is about pixel layout, so device UI strings are given as the Chinese literal (whose glyph widths matter) with the English meaning in parentheses.

### 6.1 Status bar (y 0–11, on every page except the lock screen)

| Position | Content | Color |
|---|---|---|
| x=1 | **While recording**: REC pill, width = text width + 6, height 12, `RecBg`; text at x=4: `● 12:48` (one space between ● and the timer, timer starting at x=16, as in mockups 04–05; v0.4.0 is `●12:48`, drawn as one string) | Text `Rec`, **does not breathe** |
| Next (+4, or x=3) | Link: online → Mando head 11×9 (y=1); syncing → head + `◎` (Accent); `◇ 无响应` (not responding, Warn); `◇ 版本不兼容` (version mismatch, Error); `○ 离线` (offline, Ink3) | |
| Next +6 | `↑N` | Ink3 when N=0, otherwise Ink2 |
| Next +6 | `!N`, **only when N>0**: mustard pill, width = text width + 6 | Warn / WarnBg |
| Next +4 | `录N` (recordings N), only when N>0 | Ink2 |
| Right-aligned at 236 | Battery `94%` | Ink3; Error below 10% |
| 5 to the left | Time `09:41` | Ink2 |
| 4 to the left | Weekday `周三` (Wed) (from `lock_date_text`; not drawn when the time is unknown or it does not fit) | Ink3 |
| 6 further left | `※kN` (keyboard warning, when not 0) | Warn |

- The online "● 在线" (● online) is replaced by the small dog head. So when not recording, the status bar no longer has a "●", and any ● that `test_rec_visible` finds can only be REC's, which is stricter.
- When it does not fit, the weekday goes first (pure layout function; the test uses the longest combination: version mismatch, ↑200, !2, ※k9).

### 6.2 Hint bar and toast (y 123–134)

- Hint bar: the keycap drawing of §4.2. Keycaps 12 high, radius 2, `Cap` background; the label sits 3 px to the right of the keycap.
- Toast: a full-row pill (2, 123, 236, 12), Info → InfoBg/Accent, Ok → OkBg/Ok, Warn → WarnBg/Warn, Error → RecBg/Rec; text starts at x=8, width 224 px (`kToastTextW`), `draw_fit`; fixed messages are all written to this width, and `test_text_width` draws them with `toast_bar` to check (docs/20 B6). Rules unchanged: one at a time, 2.5 s, not shown on the lock screen [src toast.h, shell_render.cpp].

### 6.3 Home (mockup 01; S1 restyles, S2 finishes)

```
[头] ↑0 [!1]                    周三 09:41 94%
 ▟▙  ! 早上好，1 件事在等你
 ▛▜  ┌─────────────────────────────┐
 ▐▌  ┃ 支付回调重试        9 分钟  │
 ▐▌  ┃ acmeshop · 等你回复         │
     └─────────────────────────────┘
[P]项目 跑2   [I]等你   !1   [R]开会
[N]记一条     [Q]排队给 AI   [S]开 session
[M]系统       [L]锁屏        [Tab]菜单
[G0]按住说一段  [;.,/]移动  [Enter]打开
```

In English the mockup reads: head ↑0 [!1], Wed 09:41 94%; "Good morning, 1 thing is waiting for you"; a waiting card "Payment callback retry · 9 min", "acmeshop · waiting for your reply"; tiles [P] Projects run2, [I] Waiting !1, [R] Meeting, [N] Note, [Q] Queue for AI, [S] Start session, [M] System, [L] Lock, [Tab] Menu; hint [G0] hold to speak, [;.,/] move, [Enter] open.

| Element | Position | Notes |
|---|---|---|
| Mando | 46 px, (7,14), 26×46 | State per §7; when waiting, the bubble is at (31,14), 9×11, radius 4, plus a two-pixel tail |
| Greeting | (44,15) | `早上好，` (Good morning,) Ink + `1 件事` (1 thing) Warn + `在等你` (is waiting for you) Ink. 5–11 h good morning, 11–13 good noon, 13–18 good afternoon, otherwise good evening; "你好" (Hello) when the time is unknown. Nothing waiting: "早上好，都处理完了" (Good morning, all done). The Mac has not yet sent a session snapshot (`sessions.live` is false, Mando is sniffing): `你好，` (Hello,) followed by `还没连上 Mac` (not connected to the Mac yet, Ink3); never "all done" |
| Waiting card | (42,30,194,28), Plain, radius 5 | Mustard bar (46,35,3,18); title (session name) (54,32) Ink, truncated at the age's left edge; age right-aligned to 230, Warn, written by the device's single age function `age_text` (docs/20 U22, rounded): `9 分钟` / `2 小时` / `3 天` (9 min / 2 h / 3 days) (the mockup writes `9 分`; list rows on the right use the compact `9分钟`); second line (54,45) Ink3: `项目键 · 等你回复` (project key · waiting for your reply) [asm]. With nothing waiting, the card only holds `没有等你的 session` (no session waiting for you, Ink3), with no bar; with no snapshot yet it is `嗅一嗅… Mac 没响应` (Sniffing… Mac not responding) |
| Tiles | Three columns x = 4 / 83 / 161, widths 75 / 74 / 75; y0 = 64, height 16, row pitch 19 | Selected: Accent fill, radius 5, keycap Accent2, text OnAccent. Not selected: no fill, keycap Cap / Ink2, text Ink. Keycap at x+2, text 4 px after the keycap, truncated at x+w−4. Badge right-aligned to x+w−4: `跑N` (runN) Ink3, `!N` Warn. When there is no badge and the text does not fit (`开 session`, 60 px), the text moves 3 px left and its right edge goes to x+w−2, 1 px from the keycap and 2 px from the rounded corner, drawn in full |
| Four-row version | docs/16 app tiles | `home_grid(tile count)`: with more than 9 tiles, y0 = 60 (48 inside the content area), tile height 14, row pitch 16, four rows ending at 122, the waiting card (to 58) unchanged; the keycap inside a tile sits at y+1 (y+2 in a 16 px tile) |
| Hint | `G0按住说一段 ;.,/移动 Enter打开` (G0 hold to speak, ;.,/ move, Enter open) | Changed: the Tab menu is already the 9th tile, so "按住 G0 说一段" (hold G0 to speak) under the greeting moves here. The home hint does not end with "Tab菜单" (Tab menu): adding it needs about 246 px even at the tightest, which does not fit; home's Tab reminder is the 9th tile (already noted in docs/13 §5.6) |

- Remove the "主页 · 10/21 周三" (Home · 10/21 Wed) title: the weekday is in the status bar. `home_date()` and its tests stay; if it has no caller, delete it (the Lead decides).

### 6.4 Project board (mockup 02, S1)

| Element | Position | Notes |
|---|---|---|
| Title row | (5,14) | `项目` (Projects) in faux bold, followed after 6 px by an Ink3 sentence: `2 个在跑，1 个在等你` (2 running, 1 waiting for you). When there is a banner, the sentence is replaced by it: version mismatch (Error), Claude Code not found on the Mac (Warn), `更新于 5 分钟前` (updated 5 min ago, Ink3) |
| Cards | x=4, w=232, h=28, y = 29 + 31k, radius 6; 3 per screen, the cursor always visible | Selected: Selected (Select fill + Live outline); others: Plain |
| Number keycap | (x+5, y+8) | Selected Accent / OnAccent, otherwise Cap / Ink2 |
| Key name | (x+23, y+2) | Ink; Warn when the project is unavailable |
| Name | (x+23, y+14) | Ink3, Ink2 when selected |
| Counts | From x+w−5 leftward, y+8, spacing 3 | `跑N` (runN) Ok/OkBg, `等N` (waitN) Warn/WarnBg, `队N` (queueN) Ink2/Raised; plain Ink3 text at 0; `跑--` when offline |
| Other | Same card, no number | `其他` (Other), second line `N 个 session 没归项目` (N sessions not in a project), counts `跑N 共N` (runN totalN) |

- Hint: `Enter打开 Opt+1-9项目  Tab菜单` (Enter open, Opt+1-9 project, Tab menu). `;.选择` (;. select) was removed (it is on this page of the Tab menu); otherwise it could only squeeze into the third spacing tier, running all the way to x=238.
- Project pages (sessions / queue) and the waiting list (review 2026-10-22): row height 14, text at y+1 within the row, indented 8 on both sides (`kListX`), the selected row is a 14 px Select pill (previously 12 px, with the text touching the pill's top and bottom). The tab reads `Sessions(N)` (changed back from `会话(N)` when merging docs/20: session has only one name across the device, and the start-session confirmation also says "先看 Sessions" (check Sessions first), U22, U25). The queue hint is `Enter开session `返回  Tab菜单` (Enter start session, ` back, Tab menu) (`Opt+D完成` (Opt+D done) is in the Tab menu; put in the hint bar it could only squeeze into the third tier).
- When offline (or with no Claude on the Mac), queue items are grey text (Ink3); the selected item uses Ink2 instead (Ink3 on Select is only 3.5:1), with a neutral `离线` (offline) pill on the right, explaining why pressing does nothing.
- Waiting: a session waiting for you no longer has a plain `!` before its name, but the 3 px mustard bar of the home card (in-row (8, y+3, 3, 8)), with the name starting at x=15. When empty, draw Mando (46 px, Idle) and the `在呢` (here) sign, with `都处理完了` (all done, Ink) and `没有等你的 session` (no session waiting for you, Ink3) on the right.

### 6.5 Tab menu (mockup 03, S3; S1 only changes rows to keycaps and selection pills)

| Element | Position | Notes |
|---|---|---|
| Status bar | y 0–11 | Not dimmed, not covered; while recording the ● is always there |
| Page underneath | (0,12,240,111) | Drawn as usual, then `dim()`; Mando stays in his pose of the moment, dimmed too |
| Drawer | x=sx, y=13, w=240−sx, h=109, `Card`, radius 8, only the two left corners rounded, with dimmed bg below | sx defaults to 66 (mockup). When the widest row of this page does not fit, it widens leftward, at most to sx=6, see below |
| Top rim | (sx+7, 13, w−7, 1) | `Rim` |
| Segments | (sx+5, 17, w−9, 15) Bg pill; slider Accent pill, width = one segment, height 13, y=18 | 本页 / 去哪 / 控制 (This page / Go to / Controls); text inside the slider OnAccent, the rest Ink2. Overlays only have two segments |
| Rows | y = 36 + 14k, 6 rows per screen (previously 7) | Selected: Accent pill, height 14. Keycap (x+2, y+1), height 12, radius 3, at least 12 wide (selected Accent2 / OnAccent, otherwise Cap / Ink2); the label starts at 2 + the keycap width of **most rows** on this page + 5 (19 for a one-letter key, as in the mockup); a wider keycap only pushes its own row (no more than 54), Ink / OnAccent. Badges right-aligned to row end −6: the row that opens the board `跑N` (runN, Ink3), the row that opens waiting `!N` (Warn), not drawn at 0; on the selected caramel row they are always OnAccent (mustard on caramel is only 1.17:1). Rows like `1-9` that only explain and do not execute: keycap Raised, text Ink3 (4.55:1; Ink3 on Cap is only 3.5:1). The facts on the Controls page (sound, battery, SD, Mac) are not keys, so they have no keycap, just Ink3 text (4.97:1 on Card) |
| Scroll bar | x=237, width 2, track `Line`, thumb `Ink3` | Replaces the old ↑ ↓ markers |
| Hint | Every page: `;.选 Enter做 ,/换页 Tab关` (;. select, Enter do, ,/ switch page, Tab close), drawn as keycaps | `字母直达` (letters jump directly) was removed (not in the mockup; with it the hint bar could only fit the second spacing tier). Letters still jump directly (`menu_key`) |

- **Drawer width**: `sheet_x = clamp(240 − 8 − widest row's need, 6, 66)` (pure function; need = that row's label start + label + badge or 4 px). Labels no longer repeat the key already written on the keycap: tails such as `（按 1-9）` (press 1-9), `（按 Opt+1-9）`, `（按 0-9）` are removed (docs/13 §6.1's "row end says press 1-9" is replaced by keycaps in v0.5), and the Controls page's lock row just says `锁屏` (Lock) (how to unlock is written on the lock card). So the drawers of This page, Go to and Controls are all around 66 (at most a few px further left when the Controls page's SD row is long), and no longer cover almost the whole page so that you lose track of where you are.
- **Slide-in**: on open, 3 frames of 50 ms each, the drawer x goes from 240 to sx (t = .45 / .85 / 1). Frame 1 pushes the full screen (dimming happens in this frame), frames 2 and 3 only push the drawer rectangle. Closing does not slide; it just closes.
- **Slider**: when `,` `/` switches page, redraw the full screen once (the rows changed), then 3 slider frames of 50 ms each, pushing only the segment control.
- When the backlight is not On, nothing slides; the end state is drawn directly. A key press during a slide jumps straight to the end.
- Remove the "菜单 · 会议录音" (Menu · Meeting recording) title and the `[本页]` ([This page]) brackets: the segment slider shows which page you are on, and the original page is visible behind the drawer.

### 6.6 Voice memo (mockup 04, S2)

```
[●0:07] [头] ↑0 [!1]           周三 09:41 94%
  ▟▙      语音便条                 第 1 段
  ▛▜      ● 0:07          （Nunito 30 px）
  ▐▌      ┃┃·┃┃┃·┃┃┃┃┃·┃┃┃┃·┃┃┃┃┃·┃┃┃
  ▐▌
 (在听)
[1]acmeshop  ([2]cardputer)  [0]不归属       ← gh #53：1–9 和 0 各是什么，选中的亮着
[G0]松开结束  [1-9]选项目                    ← 0 不归属只在图例里写（评审：两处叠着像画错了）
```

In English the mockup reads: "Voice memo", "Segment 1", the large ● and timer, the waveform, the "Listening" sign; the legend [1]acmeshop ([2]cardputer) [0]Unassigned (gh #53: what 1–9 and 0 are, the selected one lit); hint [G0] release to end, [1-9] choose project (0 Unassigned only appears in the legend; review: the two overlapping looked like a drawing bug).

| Element | Position | Notes |
|---|---|---|
| Mando | 64 px, (10,19), 36×64 | Sound: Listen (nodding); quiet: Tilt (head tilt); §7 |
| Sign | Center x=28, y=87 | While recording `在听` (Listening, Info); no sign during Cue; Finishing `保存中…` (Saving…, Neutral, Mando with chin on paw) |
| Labels | (60,18) `语音便条` (Voice memo) Ink3; `第 N 段` (Segment N) Ink3 right-aligned to 236, y=18 | |
| Large ● | Center (66,44), diameter 9, `RecDot` | Push area (60,38,12,12), breathing |
| Timer | (76,33), Timer font, Ink on Bg | `m:ss` (`rec_clock_text`) |
| Waveform | 35 bars, x = 62 + 5k, 3 px wide, center y=75, half height ≤11 | Newest on the right; alternating Accent / Accent2 every 3 bars; when height ≥5, the four corners are rounded with transition colors. Area (62,63,174,25). When the legend takes three rows (about 5 or more projects), center y=70, half height ≤7, area (62,63,173,15), still 35 bars |
| 1–9 legend (gh #53) | One row: (7,103), below the sign; two rows: (60,94) and (7,108); three rows: add (60,80), and the waveform gets shorter. Rows starting from the left begin at x=7, so a lit pill starts at x=4, aligned with the hint bar | In the order of the number key row: `[1] project key` … `[9] project key`, then `[0] 不归属` (Unassigned). The digit is a 10 px wide keycap, text Ink (the digit is what you match against, so it is brightest), 2 px gap then the project key, Ink2; 8 px between projects (6 px when it does not fit). **The selected one** is drawn as the former "存到" (save to) live-words pill: Select background, Live outline, keycap Accent / OnAccent, text Accent; it switches the moment a digit is pressed (at the next full redraw). **Pressing a digit never moves the legend** (review 2026-10-22: with 6 projects, pressing 8 made the legend jump from two rows to three, the waveform shorter and every project move): as long as any digit in 1–9 is free, a `[9] ?` slot is kept after `[0]` and counted in the layout, drawn and lit only when a digit with no project is chosen; the row count, truncation and each item's position do not depend on what is chosen (`test_rec_legend` checks every digit for 0–9 projects). Shows the project key (ASCII, 6 px per letter, at most 12 by protocol), not the name; when it does not fit, all keys are truncated to the same width, one letter at a time, removing a trailing `-` if the cut lands on it; `不归属` is never truncated; **a truncated key gets two 1×1 dots after it** (on the baseline, 3 px, counted in the layout), so `tax-202` does not look like another complete key ("…" would take a Chinese character width). **Rows: first find the fewest rows that need no truncation** (the review asked to choose: recognizing which project matters more than waveform height; two projects fit in one row; three 12-letter keys in two rows, shown in full; 5–6 projects in three rows, shown in full); only if three rows do not fit either, fall back to the fewest rows that leave each key at least 6 letters, and failing that three rows (9 keys of 12 letters: three rows, 6 letters plus two dots each). The old "存到 #project key" (save to #project key) row was removed: the lit one is the destination; **when the selected key is truncated**, `语音便条` (Voice memo) in the title row is replaced by `存到 #full key` (save to: 存到 Ink3, key Accent); during grace and hands-free that row still shows their hints. Pure functions in `rec_legend.h` (`legend_list`, `legend_fit`, `legend_plan`, `memo_legend`) |
| Hint | `G0松开结束 1-9选项目`, drawn as keycaps [G0] 松开结束 (release to end) [1-9] 选项目 (choose project) (mockup 04); `0 不归属` (0 Unassigned) only in the legend | Previously `松开 G0 结束  1-9 项目  0 不归属`; after the gh #53 legend arrived, review found the hint bar's `[0] 不归属` and the legend's `0 不归属` stacked one above the other at almost the same x, looking like a drawing bug, so it was removed from all three hint bars (hands-free went back to the full "选项目") |
| REC | 5 px right of the timer, y=38, `REC`, Rec | Only while recording (not while saving). The prompt you chose in v0.3.1 is "看到 ● REC 再说话" (speak when you see ● REC), so once the timer runs, there must really be a ● and REC on screen (review 2026-10-22; the alternative was to change that sentence, which needs your OK, and was not done) |

- **Cue** (the mic is not up yet): no large ● and timer; instead two lines of the original text: (60,36) `● 准备` (● Getting ready, Warn), (60,52) `看到 ● REC 再说话` (speak when you see ● REC, Ink3); Mando is Idle, with no sign; the status bar shows `●0:00`.
- **Finishing**: the timer stops at the last time; the waveform keeps its last moment, all drawn in `Live` (the color of "words not yet landed" in the mockup generator), leaving no hole; sign `保存中…` (Saving…), the hint bar empty (no need to say "Saving…" twice).
- **Level**: `level_q` is on a dB scale: the block's RMS (after the high-pass, see below) from −60 dBFS (RMS 33, the room's noise floor, drawn as a dot) to −24 dBFS (RMS 2,068, full height); 36 dB in 11 levels, about 3.3 dB each. `q = round(255 × clamp((20·log10(rms / 32768) + 60) / 36, 0, 1))`, one `log10f` per block, 8 per second.
  - It used to be linear (`255 × min(rms, 8192) / 8192`), and did not move on the real device. In your four memos of 2026-10-22, speech blocks had an RMS of 155–1,083 (median 454, about −37 dBFS), all drawn as 3 px dots [meas].
  - **High-pass** (second real-device check on 2026-10-22): the room's low-frequency hum (peaks at 35 and 68 Hz, 70% of the energy below 100 Hz) was at −52 to −57 dBFS, above the "has sound" hold line (−56 dBFS, §7.1); Mando kept nodding in pauses, and 5 px bars were drawn with nobody speaking [meas]. Now the block RMS is computed after a high-pass: `rec::level_of` (`lib/rec/level_hpf.*`), called once per block by the recording task's `RecEngine::write`.
    - 4th-order Butterworth, 130 Hz: two RBJ high-pass biquads (Q 0.5412 and 1.3066), float, transposed direct form II. State carries over between blocks of the same recording (also across segments), and `RecEngine::start` clears it. 35 Hz −45.6 dB, 50 / 60 Hz −33 / −27 dB, 68 Hz −22.5 dB, 100 Hz −9.6 dB, 150 Hz −1.2 dB, above 300 Hz less than 0.01 dB off [calc; `test_rec_level` checks 35–68 Hz ≥ 20 dB, 300–3,400 Hz ±1 dB].
    - The originally planned single 2nd-order 120 Hz stage gives only −10.3 dB at 68 Hz (35 Hz −21.4 dB), short of 20 dB; a single 2nd-order stage with 20 dB at 68 Hz would have to sit at about 215 Hz, losing 1 dB at 300 Hz. So two stages [calc].
    - Only the level is affected: not one byte of the WAV changes (`test_rec_level` compares byte by byte).
    - A synthetic recording (`test_rec_level`): speaking 2 s and pausing 2 s, three times, inside −52 dBFS hum (35 + 68 Hz half each, or all at 68 Hz). Without the high-pass, pause blocks are 5 px and keep the sound on; with the high-pass, the sound ends at the 2nd full block of a pause, every pause block is a dot, and the sound stays on while speaking [meas].
    - Cost: two biquads (10 multiply-adds) plus one squared accumulation per sample. On the host (Apple silicon Mac, -O2) about 7 µs per block of 2,048 samples [meas]. On the ESP32-S3 (-Os, disassembled with `xtensa-esp32s3-elf-g++`) the inner loop is about 69 instructions, estimated at 70–110 cycles / sample: 1.1–1.8 M cycles per second, 0.5–0.75% of one 240 MHz core, 0.6–0.9 ms per block (128 ms) [calc, todo on device]. Recording task stack: `level_of` 48 B, called only after the card-write chain returns, not stacked on top of it; `RecEngine::write` 112 → 128 B (`.su`).
  - The four memos through the high-pass (the same WAVs run through `level_of` on the host, `level` in `test/core/support/memo_levels.h`): speech blocks 136–1,056 (median 390, about −38.5 dBFS), 0–3.3 dB lower than before (median 0.9 dB, the voice's fundamental around 150 Hz); noise floor 5–33 [meas]. Classification unchanged: all 180 speech blocks are bars of ≥ 9 px and count as sound, the noise floor is all dots and the blocks far from speech do not count, 14 sound switches in all; only the fading last block of memo 0 (RMS 55, 52 after the high-pass, exactly −56 dBFS) ends the sound one block (128 ms) earlier [meas].
  - Speech now spans levels 4–9: 5th percentile, median, 95th percentile and maximum are 4, 7, 8, 9; neighboring blocks differ by about 1.3 levels on average. The noise floor is still dots; full height takes 5 dB more than the loudest block [meas].
  - The first block of each recording counts as quiet (q = 0). It is the codec ringing as the mic starts up (RMS 575–600 in all four, 265–277 after the high-pass, decaying in about 10 ms), not your voice; if it counted, a 13 px bar would pop up at the start and Mando would nod once.
  - Bars are not smoothed in rise or fall: each one is already a 128 ms RMS. The meeting timeline uses the same scale (half height ≤8), and merging still takes the maximum.
- Remove v0.4's two lines "● REC 0:07 → 未归属" (→ Unassigned) and "第 1 段 · 电 94%" (Segment 1 · battery 94%): the ● and timer become large text, the destination becomes a pill, the battery is in the status bar.
- **1–9 legend** (gh #53; requirement from a memo of yours: while speaking, the screen should show which project each of 1 to 9 is): always present during hold-to-speak, the grace period after release, hands-free, Cue and saving; not drawn when the card shown after holding G0 to end covers the page. Only full redraws draw it, and the animation tick never touches it: the tick only asks how much room it leaves (`rec_room`: with three rows the waveform gets shorter), so the waveform area the tick pushes is always above the legend. Three things tried and rejected: a strip that pages (on another page the selected one is not visible), a 3×3 key grid (lots of empty cells with few projects, and no room for the meeting page's timeline), a card lit for 2 seconds after starting and after pressing a digit (covers the waveform just as it starts, and seeing it again needs another press).

### 6.7 Meeting recording (mockup 05, S2)

| Element | Position | Notes |
|---|---|---|
| Title | (6,15) `开会` (Meeting) in faux bold, followed after 5 px by `destination · 第 N 段` (segment N), Ink3 (`未归属 · 第 1 段`, Unassigned · segment 1), truncated 4 px left of the ★ pill | The segment number used to be centered under the timeline; the mockup has nothing there, so it moved to the title row |
| ★ pill | Height 14, right-aligned to 236, y=14, Warn/WarnBg | Normally `★N`; for 600 ms right after starring (reusing `kStarFlashMs`) it is `★ 已标记 12:31` (★ marked 12:31, the timer at the moment of starring) |
| Mando | 46 px, (10,31) | Listen / Tilt |
| Sign | Center x=23, y=79 | `在听` (Listening) |
| Large ● | Center (52,44), diameter 9 | Push area (46,38,12,12) |
| Timer | (62,33), Timer font | `mm:ss` |
| Timeline | x 48–236, center y=98 | 94 cells for the whole meeting, half height ≤8, `Live`, the newest 7 cells Accent. **Before the first merge**, each cell is 1 px wide with 2 px pitch, growing rightward from x=48; the `Rec` end dot (diameter 5) sits 2 px after the newest cell, and the 1 px `Live` baseline only runs to the dot, with no empty track after it; **after merging**, the cells fill the whole 188 px: pitch = 188 / cell count, bar width = pitch − 1, the dot at (237,98) (review: before, after a merge the bars shrank to half width, followed by a flat track to a dot pinned at the far right, like a progress bar). ★ sits in the middle of the cell where it was added: a 1×4 Warn vertical line at (sx, 85), `★` Warn at (sx−2, 73); a ★ less than 8 px from the previous drawn ★ only gets the line (after merging, stars close together would blur into a blob). Below at y=109: `0:00` on the left, `现在` (now) right-aligned to the dot's outer edge (+3) but not overlapping `0:00`; both Ink3 |
| 1–9 legend (gh #53) | One row: (48,59), below the timer and above the ★; if it does not fit (fewer than 5 letters per key), add (7,109), and then `0:00` and `现在` under the timeline are not drawn; its pills are 1 px from the loudest bar and 1 px from the hint bar | Same drawing and order as the memo page, the selected one lit; the title row still shows the destination. The meeting page **keeps no** `?` slot: the title already says `#8`, and a slot would truncate your two projects to 5 letters; pressing a digit with no project lights nothing in the legend, and nothing else moves. With 9 long keys, also fitting `[0] 不归属` would leave each key fewer than 4 letters, so 0 is drawn as a bare keycap `[0]` (the title says "未归属" (Unassigned), and it still lights when 0 is chosen), with 3 letters plus two dots per key, the tightest case. Not drawn during Cue and the end confirmation (the confirmation would cover it). The tick does not touch it: with two rows, the timeline's redraw area only reaches below the bars (46,73,194,34), and `现在` does not move |
| Hint | `G0加★ 1-9项目 Opt+R结束 Tab菜单` (G0 add ★, 1-9 project, Opt+R end, Tab menu) | Changed: the old `G0/空格★` (G0/Space ★) could not be split into keycaps; Space still adds a star, and it is on this page of the menu |

- **EndConfirm**: `结束录音？` (End recording?, Warn) and ``Enter 结束    ` 继续录`` (Enter end, ` keep recording), original text unchanged, placed in a Plain card (44,58,192,62), with the keys of that line drawn as keycaps (`keys_text`, the same string and drawing as the hint bar); Mando stays still, the ● and timer as usual.
- **Meeting confirmation** (before recording): the text **does not change by a single character**, and the row positions do not change (`开始会议录音？` (Start meeting recording?) Warn on row 1 at y=24, `请先告知在场的人` (Tell the people present first) on row 3 at y=48, ``Enter 开始    ` 取消`` (Enter start, ` cancel) on row 6 at y=84), with a Plain card (4,14,232,106) underneath. On row 6, `Enter` and `` ` `` are drawn as keycaps with not one character changed (string constant `kMeetingConfirmKeys`, also used by the hint bar, and the tests check the original text); Mando sits on the right of the card at (190,40) (46 px, Idle, no sign), well away from the text, so the card no longer looks half empty. The hint is unchanged.
- The timeline covers the whole meeting: the level history keeps recording while the screen is off or dimmed (§3.5 item 4); when the cells are full they merge in pairs, and each cell then covers twice the time.

### 6.8 Lock screen (mockup 06; S1 card shape, S2 Mando, clock, warm glow)

| Element | Position | Notes |
|---|---|---|
| Dark screen (card not lit) | All Bg | While recording, the top-left shows v0.4.0's line with the text unchanged: `●42:10 ★2` (meeting) / `● REC 0:12` (memo) / `● 准备` (● Getting ready); placed in a pill like the status bar's REC slot (1,0,text width+6,12), text at x=4: Rec on RecBg (4.81:1), Getting ready is Warn on WarnBg. While the card is lit, the warm glow stays at least 2 px from this pill (before, a square black hole was cut out of the glow, like screen corruption; when Dim it was still bare text at x=0) |
| Card | (12,8,216,94), Sheet, radius 10; while recording it moves down to y=14 so it does not cover the ● line | Top rim `Rim` (21,8,198,1) |
| Warm glow | Pixels outside the card that are still Bg | Center (−6,−10), radius 170, 6 levels plus Bayer, static |
| Mando | 46 px, (25,17), dozing frame | z at (47,22) Ink3, (53,15) Ink2, (59,7) `Z` Ink; one more lights every 600 ms, and after all three are lit it starts over; only moves while the card is lit |
| Sign | Center x=38, y=67, `打盹中` (Dozing), Neutral | |
| Locked | (77,14) Ink3 | `已锁屏` (Locked) |
| Clock | (76,29), Clock font, Ink on Sheet | When the time is unknown, plain text `--:--` (Ink2) |
| Date line | (78,58) Ink2 | `10/21 周三 · 电 94%` (10/21 Wed · battery 94%) (v0.4 format) |
| Small pills | y=77, height 13, spacing 4 | `等你 N` (waiting N) Warn, `待传 N` (waiting N, the number of recording segments waiting to transfer) Neutral; `↑N` Neutral, only when N>0 |
| Hint | Centered, y=108: [Opt+L] then [Enter] to unlock; y=122: tap [G0] first, then hold to speak | During a meeting the second line is `开会中：[G0] 加 ★` (In a meeting: [G0] add ★). While recording the card moves down, and the two lines move to y=110 and 123 |

- The card still shows only counts: no project names, session names or any body text (docs/13 §7.1).

### 6.9 Pages without mockups

S0 changes colors, S1 adds cards and selection pills. Review (2026-10-22) said these pages still looked "industrial", so they were rearranged with the same parts, text unchanged:

- **System**: the title `系统` (System) is drawn in the label position of the firmware row (faux bold, x=4), followed by the firmware version; every other row is "label / value": the label is the row's first word, Ink3, x=4; the value is the rest, Ink, x=46 (after the widest label `outbox`), multiple spaces merged into one; warning rows (a sentence, not label plus value) are drawn whole from x=4, Warn. The three rows `固件 / dev / 启动` (firmware / dev / boot) are separated from the rest by a 1 px `Line` divider with 1 px space above and below. 9 rows plus the divider end exactly at y=122. `SystemPage` gains a `key_len` (the label's byte count, 0 for warning rows); the row text does not change.
- **Edit page (Compose)**: the title `笔记 → acmeshop` (Note → acmeshop) goes into a Raised neutral pill at (4,13); the body goes into a Plain card (4,28,232,92), padding 6, 36 characters per line, 5 lines; the count `22/500` at the card's bottom right, Ink3; "开头不能是 '-'" (cannot start with '-') at the card's bottom left, Warn.
- **Discard confirmation** (Fn+`): the same kind of card as the meeting confirmation (4,28,232,92), `放弃已输入的内容？` (Discard what you typed?) Warn at (12,40), ``Enter 放弃    ` 返回编辑`` (Enter discard, ` back to editing) at (12,72) drawn as keycaps, text unchanged.
- **Waiting and project pages**: see the last items of §6.4.
- **Run confirmation and migration progress pages**: colors only, no new layout.

## 7. Mando state machine

### 7.1 Actions, frames and signs

Frame numbers are the frame indexes of the WhisperWoof sprites (24 frames per action, 15 for hop, originally 80 ms per frame). On the device only 4 key poses are taken, 200 ms each, which is the pace written in the Mando asset pack as your preference [src research A §4].

| Action | Pose (46 px / 64 px) | Pace | Sign | Bubble |
|---|---|---|---|---|
| Idle, here | wait 0 | Still | 在呢 (here, Info) | — |
| Listen, listening (sound) | review 0/6/12/18 | Loop | 在听 (listening, Info) | — |
| Tilt, listening (no sound) | wait 0/6/12/18 | Loop; no phase change when switching with Listen | 在听 (listening, Info) | — |
| Sniff, sniffing… | think 0/6/12/18 | Loop, at most 8 s, then stops at pose 0 | 嗅一嗅… (sniffing…, Neutral); while saving it says "保存中…" (Saving…) | — |
| NeedsYou, waiting | wait 0 (sitting quietly, mockup 01) | Still; the bubble pops up in two steps | 等你 (waiting, Warn) | `!` Warn |
| Hop, saved | hop 0/4/8/12 | Plays once (0.8 s), then stops at wait 0 | 存好了 (saved, Ok) | — |
| NoVoice, didn't hear | wait 12 (most tilted) | Still; the bubble pops up in two steps, back to the previous state after 2.5 s | 没听到 (didn't hear, Neutral) | `?` Neutral |
| Doze, dozing | dozing frame (wait 0 with eyes closed) | Still; one z every 600 ms | 打盹中 (dozing, Neutral) | — |

- 46 px has 17 frames in all: wait 4 + review 4 + think 4 + hop 4 + doze 1, 598 B per frame, 10,166 B in all. 64 px has 12 frames: review 4 + wait 4 + think 4, 1,152 B per frame, 13,824 B in all (used only on the voice memo).
- Hop and NoVoice count from the end of recording; when a memo hits the 5-minute limit while G0 is still held (docs/20 U15), the screen first stays on the limit page, and counting starts only when G0 is released and home returns (`Runtime::rec_apply`), so the hop does not finish playing behind the limit page.
- "Has sound" (`LevelHistory::voice`, using the same q as the bars): it starts on one block ≥ −50 dBFS (q ≥ 71, RMS ≥ 104, bar height 7 px); after that, blocks above −56 dBFS (q > 28, RMS ≥ 53) keep it going; it ends only after 2 consecutive blocks (256 ms) at or below −56 dBFS. So he keeps nodding in the pauses between words, and a tap on the table only gets one or two nods.
  - In the four memos, 186 / 186 speech blocks count as sound, with no delay at the start; none of the 18 quiet blocks far from speech count; 14 switches in all, exactly the start and end of each stretch of speech [meas].
  - It used to be "RMS of the last 2 blocks ≥ 1024 (about −30 dBFS)": only 2 of 180 speech blocks reached it, and Mando tilted his head almost all the time [meas]. `kVoiceLevel` in `rec_overlay.h` (which `rec_mando` compares with `RecInfo::level`) changes to 104 as well.
- Mockup 07_states has no 64 px hop and NoVoice: after a memo is saved or discarded the page closes, so these two actions only appear on home (46 px).

### 7.2 Which action each page uses (`theme::mando_pick`, pure function)

| Page | Condition (top to bottom, the first that holds) | Action | Where the sign is drawn |
|---|---|---|---|
| Home | `ui.moment == Saved` and less than 0.8 s | Hop | Home has no sign |
| Home | `ui.moment == NoVoice` and less than 2.5 s | NoVoice | |
| Home | A session is waiting for you | NeedsYou | |
| Home | Link Ready and `outbox.pending > 0`, or the Mac is plugged in but cpad is not responding | Sniff | |
| Home | Otherwise | Idle | |
| Voice memo | Cue / Recording with sound / Recording without sound / Finishing | Idle (no sign) / Listen / Tilt / Sniff (Saving…) | (28,87) |
| Meeting | Recording with sound / without sound / EndConfirm / Finishing | Listen / Tilt / Idle (listening) / Sniff | (23,79) |
| Lock card lit | — | Doze | (38,67) |
| Tab menu open | — | The action of the page below, frozen and dimmed | — |
| Board, waiting, system, edit, confirmations | — | No Mando | — |

- `Moment` is written to AppState by the Shell: `Saved` on the recording's `EvtKind::Saved` and on a successful note save, `NoVoice` on `Discarded` (too short) (§4.3). Home's Mando reads it from AppState and does not look at toast text.
- Hop / NoVoice are only visible when returning to home after a memo is saved or discarded; on other pages there is no such action.

### 7.3 Timing

- `kAnimTickMs = 200`: Mando's pose change, the waveform shifting left and one breathing step all happen in the same tick.
- Bubble pop: for the two ticks after entering NeedsYou or NoVoice, step 0 is 2 px below the end position, step 1 is in place (WhisperWoof's "?" pops upward).
- Hop: plays over 4 ticks, and on the 5th tick stops at wait 0.
- Sniff: stops after 8 s, so Mando does not keep his chin on his paw when the Mac is plugged in but cpad is not running (WhisperWoof also switches to "Still working…" after 8 s).
- z: one more every 3 ticks (600 ms); after all three are lit, the next step clears them and starts over.
- Waveform: each tick takes a snapshot of the last 35 blocks from the level history (about 1.5 blocks / tick), so it appears to shift left about 2 bars per frame.
- Breathing: one step per tick, 4 steps per cycle.

### 7.4 Animation only while the backlight is On

| Backlight | When | Animation |
|---|---|---|
| On | On power; on battery with a key pressed in the last 60 s; lock card lit | As in §7.3 |
| Dim | On battery and recording for more than 60 s; a meeting while locked | **Everything stops**: pose, waveform and z do not move, the ● is fixed at step 0 (full brightness). The once-per-second full refresh only advances the timer digits |
| Off | Screen off, locked with the card not lit | Nothing drawn (v0.4's rule: not dirty, not drawn) |

- When the menu is open or an overlay is on top of the page, the Shell does not call the module's `animate()`.
- Animation never delays the 60 s screen-off: it does not go through `power_key` and does not change `last_key_ms`.

### 7.5 Which areas each frame pushes

Push time = pixel count × 16 bit ÷ 40 MHz + about 30 µs [src research B §4].

| Scene | Areas pushed per tick | Pixels | Time |
|---|---|---|---|
| Voice memo | Mando (10,19,36,64); waveform (62,63,174,25) ((62,63,173,15) with a three-row legend); large ● (60,38,12,12) | 2,304 + 4,350 (2,595) + 144 | about 2.8 ms / 200 ms |
| Meeting | Mando (10,31,26,46); large ● (46,38,12,12); when a cell is added: before merging, push the strip "from the recolored newest cells to the dot" (height 17) and the small area where `现在` (now) moves (height 12); after merging, every cell has moved, so push the bar and ★ band (46,73,194,34); on a merge or a star, push the whole timeline (46,73,194,49) | 1,196 + 144 (+about 600; after merging +6,596) | about 0.6 ms (+0.3 ms; after merging +2.7 ms) |
| Home | Mando plus bubble (7,12,34,48), only while moving | 1,632 | about 0.7 ms |
| Lock card | z (45,5,22,22), every 600 ms | 484 | about 0.2 ms |
| Drawer slide-in | Frame 1 full screen; frames 2 and 3 the drawer | 32,400; ≤19,314 | 13 ms; ≤7.7 ms |
| Segment slider | (sx+5,17,≤165,15) × 3 frames | ≤2,475 | about 1 ms |

While recording (On): 5 ticks per second × 2.8 ms + 1 full screen 13 ms ≈ 27 ms/s; v0.4.0 is about 8 full screens ≈ 104 ms/s [calc]. Measured in the simulator by `test_anim_cost` (same runtime code): memo 29.5 ms/s, the first 5 s of a meeting 20.5 ms/s, a meeting past 12 s (after merging, one more cell every 256 ms, with every cell moving) 29.2 ms/s, all within 32 ms/s [meas simulator].

The gh #53 1–9 legend (2026-10-22, `test_anim_cost`, host Apple silicon Mac, median of three runs): SPI unchanged or less: memo 25.9 ms/s, memo with 9 keys of 12 letters 23.3 ms/s (shorter waveform area), meeting with 9 keys past 12 s 26.0 ms/s [meas simulator]. CPU per full screen: memo with 2 projects about 495 → 538 µs (+40 µs), meeting about 543 → 605 µs, memo with 9 keys about 502 → 613 µs (+110 µs, the worst case), meeting about 514 → 596 µs; one tick costs 7–8 µs more (asking how many rows the legend takes: measure the key widths once and compute, no truncation), and the meeting's tick actually got cheaper (no redraw area is computed when the timeline has not changed) [meas simulator]. Scaling by the device's full meeting page of 21.8 ms (about 13 ms of it pushing), the CPU part is about 16 times the host: the legend adds about 2 ms to a full screen (only 4.4 ms even at 40 times), and about 0.1–0.3 ms per tick, all far from 50 ms [calc]. `test_runtime_budget` adds `memo_us` (full page plus push of a memo with 9 keys of 12 letters) and `memo_tick_us`, both required < 50 ms [todo on device].

## 8. Stages and files

At the end of each stage, run all the checks of §9.4 and look at the simulator screenshots before the Lead commits. Within a stage, write the tests first (RED), then the implementation (GREEN), then tidy up.

### S0 Palette canvas and tokens (no layout change)

New:
- `tools/theme/{tokens.py,gen_palette.py,gen_all.py,sources.json,README.md}`, `tools/theme/tests/test_gen_palette.py`
- `firmware/app/lib/theme/{palette.h,palette.cpp,raster.h,raster.cpp,raster_canvas.h,raster_canvas.cpp}`, `lib/theme/gen/palette_gen.h` (generated)
- `test/core/test_theme_palette/`, `test/core/test_theme_raster/` (S0 only has fill and clipping)
- `firmware/sim/test/test_theme_sim/` (no sentinels, partial pushes, the palette copy)

Changed:
- `lib/module_api/canvas.h`: `Color` grows to 24 values and three aliases, `static_assert`
- `lib/shell/platform.h`: `present(Rect)`, `breathe()`
- `lib/hal/m5_display.{h,cpp}`: `palette_8bit`, `PaletteSprite`, inherits from `RasterCanvas`, `present(Rect)`
- `firmware/sim/lib/simfb/{fb.h,fb.cpp,fb_canvas.h,fb_canvas.cpp,sim_display.h}`, `lib/simpng/png.{h,cpp}` (palette comes from the theme)
- `firmware/sim/oracle/*`, `test/test_efont/oracle_golden.h` (regenerated from the oracle), `test/test_fb`, `test/test_shots`
- Only `Dim` text that carries information on `Select` changes to `Ink2` (board names and session states, inbox, menu_view markers): this is a contrast requirement, not a layout change; grey text meaning unavailable stays
- `ci.yml`: include rules add `lib/theme`; the `repo` job adds `tools/theme` (pytest, ruff, check_sizes, lizard, `--check --palette-only`)
- `scripts/stack_budget.py`: `APP_DIRS` adds `theme`; `platformio.ini`'s `cardputer-adv-test` adds `theme` to `lib_deps` (the device suite must list local libraries explicitly)
- `NOTICE` (WhisperWoof's design tokens), `firmware/sim/README.md` (the "Colors" section)

Result: every page is in warm colors, with the same layout as v0.4.0.

### S1 Shapes: cards, keycaps, pills

New: `lib/theme/{theme.h,theme.cpp,hint.h,hint.cpp}`; raster adds `fill_round`, `dot`, `remap`; `test/core/test_theme_hint/`, `test/ui/test_theme_widgets/`.

Changed:
- `canvas.h`: the new primitives of §3.3 (the default implementations of `art` and `digits` stay for now)
- `test/ui/support/fake_canvas.h`: records `ShapeOp`
- `lib/shell/status_bar.cpp` (pills, !N only when not 0, weekday), `shell_render.cpp` (hint bar, toast, cards for the edit page and confirmations)
- `lib/shell/lock_screen.cpp` (card, top rim, small pills, keycap hints; the clock uses efont text for now)
- `lib/help/menu_view.cpp` (rows become keycaps and selection pills; still full width)
- `modules/board/board_module.cpp`, `board_logic.*` (cards, count pills, the opening sentence)
- `modules/home/home_module.cpp` (tile style; layout changes in S2)
- `modules/inbox`, `modules/system` (selection pills, titles)

Result: mockup 02 done; the other pages have cards and keycaps. 👤 At this point, look once on the real device at the warm dark background, ink3, mustard and caramel (§11 item 1).

### S2 Mando, large digits, animation

New:
- `tools/theme/{gen_mando.py,gen_icons.py,gen_digits.py,mando_colours.json}` and their tests
- `lib/theme/gen/{mando46.h,mando64.h,icons.h,art_ids.h,digits_gen.h}` (generated)
- `lib/theme/{art.h,art.cpp,digits.h,digits.cpp,mando.h,mando.cpp,anim.h,anim.cpp,level_history.h,level_history.cpp}`
- `test/core/test_theme_art/`, `test_theme_digits/`, `test_theme_anim/`, `test_level_history/`; `test/ui/test_runtime_anim/`
- `LICENSES/OFL-1.1.txt`

Changed:
- `canvas.h`: `art`, `digits`, `glow`
- `module.h`: `Module::animate()`
- `app_state.h`: `Moment`, `UiFlags.moment(_ms)`
- `lib/shell/runtime.{h,cpp}` (`render` splits into `render_full` and animation; `set_backlight` and `breathe(0)` in `sync_backlight`)
- `runtime_rec.cpp` (`on_level`; no dirty mark when only the level changed; `Saved` / `Discarded` write `Moment`)
- `shell.{h,cpp}` (`animate`, `set_backlight`, `on_level`, `moment`, animation state and level history)
- `rec_overlay.cpp`, plus a new `rec_anim.cpp` (to keep `rec_overlay` at or under 400 lines)
- `lock_screen.cpp` (Mando, clock, warm glow, z), `status_bar.cpp` (dog head)
- `modules/home/{home_module.*,home_logic.*}` (final layout, greeting, waiting card, `animate`)
- `firmware/sim/scripts/theme_states.cpas` (new, one screenshot per state of §7.2)
- `NOTICE` (Mando and Nunito)

Result: mockups 01, 04, 05, 06, 07 and the animation frames done. 👤 HIL: `test_runtime_budget`, `rec_soak`.

### S3 Tab menu drawer and motion

Changed: `lib/help/menu_view.{h,cpp}` (drawer, segments, scroll bar, drawer width, 6 rows per screen); `lib/shell/shell_menu.cpp` (draw the page below first, then `dim`, then the drawer; slide-in and slider state); `lib/theme/anim.*` (the two slides); `test/ui/test_menu_view`; `test/core/test_theme_anim`.

Result: mockup 03 done. 👤 HIL: `test_runtime_budget`, `rec_soak`, recording with the menu open.

## 9. Tests

### 9.1 New pure-function tests (written first, RED → GREEN)

| Suite | What it tests |
|---|---|
| `core/test_theme_palette` | Every token maps to a non-sentinel index, with the color matching `tokens.py`; the four sentinels are magenta, and the low bytes of v0.4.0's 8 constants all land on sentinels; the dark surfaces are distinct in RGB565; every pair listed in §2.5 ≥4.5:1, ink ≥7:1; `kAa` symmetric, allow-listed pairs have exact entries, other pairs are not sentinels; `kDim` only lowers brightness, `dim(bg)=bg`; every breathing step ≥4.5:1 on bg, step 0 equals Rec; `aa_of(RecDot, Bg) == RecDotEdge` |
| `core/test_theme_raster` | fill clipping; corner pixels and transition colors of rounded corners (r=2…10, any corners); pixel maps of dots of diameter 5 and 9; `remap` only changes inside the rectangle; warm glow monotonic, only changes Bg pixels, Bayer stable |
| `core/test_theme_hint` | The seven rules of §4.2, checked against every current `hint()` string; the three fallback tiers; the longest string fits or falls back unchanged |
| `core/test_theme_art` | Width and height of each frame; 4-bit unpacking (high nibble first); transparency; clipping at the edges; dimmed drawing uses 0x2F+n; checksum of the generated data unchanged |
| `core/test_theme_digits` | Fixed-width cells: `12:48` is 75 wide (Timer), `09:41` is 88 wide (Clock); ramp indexes contiguous; unbaked pairs fall back to a threshold; non-digit characters skipped |
| `core/test_theme_anim` | The whole `mando_pick` table (§7.2); 4 poses × 200 ms; no phase change switching Listen and Tilt; Hop plays only once; two-step bubble; Sniff stops after 8 s; z every 600 ms; 3-frame slides, a key press jumps to the end; `anim_due` true only at On; breathing is 0 at Dim |
| `core/test_level_history` | Push, take the last 35 blocks, 94 cells merging in pairs when full (taking the max), ★ positions following merges, the dB curve from RMS to bar height (noise floor, speech, capped at −24 dBFS), the first block counting as quiet, the hysteresis of "has sound", the levels of the four real-device memos |
| `core/test_rec_level` | The level high-pass (§6.6): 35, 50, 60, 68 Hz down ≥ 20 dB, 300–3,400 Hz within ±1 dB, an 8 kHz square wave passes unchanged; state carries across blocks, and where blocks are cut makes no difference; through `RecEngine`: WAV byte-identical, the level is post-high-pass, the next recording starts fresh; speaking and pausing 2 s inside −52 dBFS hum: from the 2nd full block of a pause there is no sound, and the pause is all dots |
| `ui/test_theme_widgets` | On `FakeCanvas`: a card always draws a top rim or outline; keycap widths; signs centered; a pill at 0 draws plain text; tile colors selected and not selected; toast tone colors |
| `ui/test_runtime_anim` | Runtime with a `FakeDisplay`: at On, recording pushes three areas every 200 ms and no full-screen push; at Dim, zero partial pushes, `breathe(0)`, the waveform snapshot unchanged; at Off, nothing pushed; animation never marks dirty; a level-only change never causes a full redraw; the 1 s refresh as before |
| `tools/theme/tests` | §5.1 |

### 9.2 `test_rec_visible` unchanged, plus the palette

- The original four tests do not change by a line and still pass: the status bar's `●mm:ss`, the lock-screen line's format and color (`Color::Error`) all keep the v0.4.0 look.
- New `test_dot_has_contrast_on_its_background`: for {memo, meeting} × {lock screen, menu, toast} × the four phases, in the text draw that contains the ●, the contrast of its text color and background in the palette is ≥4.5:1, and the text color is `Error` (`Warn` for Cue).
- New `test_dot_frozen_when_dimmed`: at Dim, the Runtime only ever calls `breathe` with 0, and the status bar ● never uses `RecDot`.
- The simulator's `test_theme_sim` adds one check: in every recording screenshot, the left of the status bar (0,0,80,12) has at least 20 pixels of the `Rec` index, at both On and Dim.

### 9.3 Existing tests to change (on purpose, each one listed in the commit message)

| Stage | Test | Change | Why |
|---|---|---|---|
| S0 | sim `test_fb` | RGB332 replaced by palette indexes | The canvas format changed |
| S0 | sim `test_shots` line 93 | Count red pixels by the `Rec` index, no longer by 0xE0 | Same |
| S0 | sim `test_efont/oracle_golden.h` | Regenerated from the oracle | Frames store indexes |
| S1 | `test_mod_board` | `跑1 等1 队2` drawn in three draws; selection fill at y=29 (card), not y=12 | Counts become pills; the board becomes cards |
| S1 | `test_mod_home` | `P 项目` split into keycap `P` and `项目`; the selected tile background is `Accent` | Tile style |
| S1 | `test_menu_view` | The selected row's fill color is `Accent` | Row style |
| S1 | `test_rec_overlay` line 173 | `松开 G0` drawn as `松开` and keycap `G0`, two draws | Hint bar keycaps (the string itself is unchanged) |
| S2 | `test_status_bar_states` | Online: assert the dog head was drawn (`ArtId::Head`), no longer "●" | Mockup |
| S2 | `test_mod_home` | Remove the asserts on `主页 · 10/21 周三` and `按住 G0 说一段`; the assert on the hint's ending becomes "ends with `Enter打开`, and the Tab menu is the 9th tile"; the assert on the most urgent item becomes the card's title, age and `项目键 · 等你回复` | Home layout |
| S2 | `test_rec_overlay` lines 171, 193, 204 | `● REC 0:12` / `● REC 12:31` become: large ● drawn, `digits("0:12")`, sign `在听`; status bar `●0:12`. Line 165 (`● 准备`), `#cpa`, `未归属`, `保存中`, `★2`, `第 3 段`, `★ 已标记`, and the original text of the meeting confirmation and end confirmation are unchanged | Memo and meeting layout |
| S2 | `test_runtime_rec` line 305 | `● REC` becomes the status bar's `●` plus timer | Same |
| S3 | `test_menu_view` | Remove the `菜单 · 会议录音` title and `[本页]`; 6 rows per screen; ↑ ↓ markers become a scroll bar; `row_y` by 14 px | Drawer layout |

`test_lock_power` (the lock-screen memo line `● REC 0:12`, `已锁屏`), `test_rec_visible`, `test_shell_dispatch_order`, `test_exit_keys` and all core tests are unchanged.

### 9.4 Checks run at every stage

In `firmware/app`:
```sh
pio test -e native
uvx gcovr -r . --filter 'lib/|modules/' --exclude 'lib/(hal|usb_transport)/' --fail-under-line 80
pio run -e cardputer-adv && python3 scripts/check_budget.py .pio/build/cardputer-adv \
  && python3 scripts/check_partitions.py .pio/build/cardputer-adv/partitions.bin
! grep -a -q -E 'debug\.(key|state|reboot|rec|mic|codec)' .pio/build/cardputer-adv/firmware.bin
pio run -e cardputer-adv-test
pio test -e cardputer-adv-test -f 'device/*' --without-uploading --without-testing
python3 ../../tools/repo/check_sizes.py --warn 400 --fail 800 lib test modules src
uvx lizard==1.24.1 -l cpp -L 50 -w lib modules src
# include 规则（ci.yml，目录里加上 lib/theme）
uv run --no-project --with pytest pytest scripts test/core/support -q
```

At the repo root:
```sh
pio test -d firmware/sim && pio run -d firmware/sim
firmware/sim/.pio/build/sim/cpa-sim shots firmware/sim/scripts/v04_tour.cpas --out firmware/sim/out/theme_tour
firmware/sim/.pio/build/sim/cpa-sim shots firmware/sim/scripts/theme_states.cpas --out firmware/sim/out/theme_states  # S2 起
uv run --with pillow --with pytest pytest tools/theme -q
python3 tools/theme/gen_all.py --check --palette-only
```

**Look at the PNGs**: at every stage, put `theme_tour/contact.png` side by side with the matching mockups: truncation, overlap, colors, Mando's position. `missing_glyphs` in `shots.json` is all 0. The new text (在呢, 嗅一嗅…, 打盹中, 存好了, 没听到, 早上好 and so on) was checked with the mockups' efont decoder, and every glyph is in the font [meas]; the device test `test_runtime_budget` asserts it once more.

## 10. Budgets

| Item | Now (v0.4.0) | After the theme | Limit | Basis |
|---|---|---|---|---|
| Canvas | 32,400 B, RGB332 | 32,400 B, `palette_8bit` | — | [src] |
| Palette | 0 | +768 B (heap) | — | [src LGFX_Sprite.hpp:398-416] |
| Free heap | 182,944 B | about 182,100 B | ≥ 153,600 B (150 KB) | [meas docs/07 line 848], [calc] |
| Largest free block | 163,828 B | about 163,000 B (the palette is allocated right after the sprite) | ≥ 65,536 B | [meas], [calc] |
| Static RAM | 65,968 B | about 66,300 B (animation state ~24 B, level history ~190 B, waveform and timeline snapshots ~130 B) | 96 KB | [meas docs/07 line 898], [calc] |
| AppState | 3,488 B | +8 B (`Moment`) | 4 KB | [src docs/13 §10.3] |
| Loop stack headroom | 5,788 B | Should not shrink: theme functions have small stack frames and no large arrays | ≥ 4 KB; one frame ≤ 4 KB | [meas], [todo stack_budget.py] |
| New asset flash | 0 | Mando 46 px 10,166 B + 64 px 13,824 B + digits about 4.1 KB + small images about 0.2 KB + palette and tables about 1.7 KB (palette 768, `kAa` 576, `kDim` 256, others) ≈ 30 KB | ≤ 60 KB | [calc] |
| App image | 969,824 B | about 1,020,000 B (assets about 30 KB + code about 20 KB) | Warning line 1,835,008 B; hard limit 2,621,440 B | [meas], [calc] |
| Palette entries | — | about 130 / 256 | 256 | [calc] |
| Screen SPI while recording (On) | about 104 ms/s | about 27 ms/s | — | [calc] §7.5 |
| Longest loop step | Full screen about 15–18 ms | Unchanged (animation frame about 3–4 ms); review estimates full screen 35–50 ms, a drawer frame 25–30 ms | ≤ 50 ms, reply p95 ≤ 100 ms | [calc]; in the test build, `Runtime::render_times()` (`Platform::micros()`, `esp_timer` on the device) records the longest full screen and the longest animation frame, and `test_runtime_budget` prints them on the `CPA_BUDGET` line (`max_render_us`, `max_tick_us`, plus a fully loaded meeting page `meeting_us`; gh #53 added the memo page with the fullest legend `memo_us` and one of its ticks `memo_tick_us`), all required < 50 ms [todo on device] |
| Chinese font | efontCN_12 213,444 B | Unchanged | — | [src research B §5] |

## 11. Risks

| # | Risk | Response |
|---|---|---|
| 1 | Panel contrast has not been measured: bg, sheet and card differ by only 1–2 steps in RGB565 and may blend together at 43% backlight; at 10.5% probably only ink, accent and REC are readable | Every card has a top rim or outline; key information never relies on telling dark surfaces apart. 👤 Look once on the real device after S1 (43% and 10.5%); if it fails, adjust one place in `tokens.py` and regenerate, with no code change |
| 2 | Whether M5GFX really uses "raw value as index" for text color on a palette sprite has not been verified on the device | The first task of S0 is the oracle case (M5GFX itself, running on the Mac); if it is wrong, change how `PaletteSprite` writes it, and modules are not affected |
| 3 | Writing directly into the `getBuffer()` buffer, with a layout different from what M5GFX assumes | `begin()` asserts `bufferLength`; the oracle uses the same `RasterCanvas` to write M5GFX's sprite buffer and compares byte by byte with the simulator |
| 4 | Mustard and caramel differ by only 1.17:1 | Waiting always carries "!", a color bar or text; look at this pair specifically on the device |
| 5 | The status bar dog head is read as a fox or a cat | Switch to a 5×5 dot image, a one-line change |
| 6 | Mando is too cute and becomes annoying over time (the Pebble Time lesson) | Still when idle; Sniff at most 8 s; Hop only once; everything stops at Dim and when the screen is off |
| 7 | Dozing is wait frame 0 with changed eyes; the asset pack has no real sleeping pose | At 46 px the z Z carries the meaning; a real sleeping pose would need new art from WhisperWoof (out of scope this time) |
| 8 | Large scope: every module uses `Canvas`, plus the simulator, `FakeCanvas` and 6 screens rearranged | Split into S0–S3, each shippable on its own; new calls all have default implementations; changed tests are listed one by one (§9.3) |
| 9 | Conflict with `feat/app-sdk`: both sides change `canvas.h`, `module.h` and home | Either can merge first; conflicts are only in added declarations; home's fourth row uses `theme::tile()` per the four-row version of §6.3; the Lead runs `test_app_tiles` when merging |
| 10 | A level-only change no longer causes a full redraw; some test or HIL may depend on "redraw whenever the level changes" | S2 runs `test_runtime_rec`, `rec_flows`; HIL's `rec_memo` and `rec_meeting` look at `debug.state`, not pixels |
| 11 | The full-screen push during the drawer slide-in may tear (the Cardputer does not wire the TE pin) | Only frame 1 is full screen, after that only the drawer is pushed; slides only happen on open |
| 12 | The warm glow has to be computed on every full redraw (about 12,000 pixels outside the card) | Use squared distance and a threshold table, no square roots; S2 measures with `esp_timer` in the test build [todo]; if too slow, draw it only when the card first lights up |
| 13 | The generator depends on Pillow's WebP and woff2 support | `sources.json` records versions; the fallback is fontTools to unpack woff2; generated output is committed, so CI does not need to regenerate |
| 14 | Heap and timing are estimates, not measured on v0.4.0 | Run `test_runtime_budget` and `rec_soak` once after S2 and once after S3 (👤) |
| 15 | With a linear level, speech on the real device (RMS mostly 1,000–4,000) may only reach 15–50% of bar height, and the waveform looks short | **Changed (2026-10-22)**: on the real device, handheld normal speech blocks had RMS mostly 224–847 (−43 to −32 dBFS), 7–19 dB lower than expected, and all dots on a linear scale. Changed to a dB scale from −60 to −24 dBFS (§6.6), and "has sound" to −50 / −56 dBFS with hysteresis (§7.1); the simulator's `mic … speech` also uses real-device levels (words 225–1,100, pauses 16). Still to check on the device: a noisy room (with the noise floor at −50 dBFS, Mando would nod all the time); voices 1–2 m away in a meeting (perhaps 10–16 dB below handheld, near the threshold); when the mic gain changes later, both ends of the scale and the thresholds must move by the same dB. **Changed again (2026-10-22, second real-device check)**: the room's low-frequency hum (peaks at 35 / 68 Hz, 70% of the energy below 100 Hz, −52 to −57 dBFS) was above the −56 dBFS hold line, so Mando kept nodding in pauses and 5 px bars were drawn. The level is now the RMS after a 130 Hz 4th-order high-pass (§6.6, `lib/rec/level_hpf.*`), only affecting the level, with the WAV unchanged; in the synthetic test, −52 dBFS hum no longer counts as sound from the 2nd full block of a pause, and the bars are all dots; speech and noise-floor classification of the four memos is unchanged. Still to check on the device: if the 30% of the hum above 100 Hz is harmonics above 200 Hz, the high-pass cannot block it, at about −57 dBFS, just under the hold line; broadband noise (fan or air-conditioner hiss) also gets through the high-pass |

## 12. What we will not do

- **No 16-bit canvas**, no M5GFX runtime anti-aliasing, no VLW fonts, no efontCN_16 or bold: titles stay faux bold at 12 px.
- **No layouts from the other directions**: a home with "one big title card", 44/48 px digits, a solid red REC pill, 12×12 monochrome icons, fading old waveform bars, a selection block that slides on every cursor move, a battery icon, a separate high-contrast palette at Dim.
- **The status bar ● does not breathe** (§1).
- **No new Mando art**: the sleeping pose is changed eyes; 64 px has no hop and NoVoice.
- **The run confirmation and migration progress pages are not rearranged**: no mockups, colors only. Project pages, waiting, system, edit page and discard confirmation were rearranged with the same parts per §6.9 (review 2026-10-22), text unchanged.
- **Hint strings** only changed where needed to match the mockups or where they did not fit: home, meeting page, memo (`G0松开结束 1-9选项目`, no 0 after gh #53), board (`;.选择` removed), queue (`Opt+D完成` removed), menu (`字母直达` removed); every removed key is still in the Tab menu. The age on the home card uses `age_text` from docs/20 U22 (`9 分钟`, changed from the mockup's `9 分` when merging v0.4.1).
- **The simulator does not simulate backlight brightness**; PNGs while dimmed are still full-brightness pixels.
- **Apps cannot pick their own colors**: no per-app accent color; the palette keeps 0xAE–0xDF for later.
- **Recording while locked with the screen off** looks the same as v0.4.0's text (one line of red text at the top left), just placed in a pill like the status bar's REC slot (§6.8).
- **The real-device panel check, `test_runtime_budget` and `rec_soak`** are not done in this plan; once each stage is done, the Lead schedules HIL (👤).

## 13. Changes after review (2026-10-22)

The issues raised by the two reviews (screen by screen against the mockups, contrast computed in RGB565; code and checks) were all fixed in the sections above. This section only lists what changed, what did not, and what to watch for when merging.

**Contrast (three places below 4.5:1)**: keycaps in the menu that only explain become Raised background with Ink3, and the Controls page facts become plain Ink3 text (§6.5); the selected queue item while offline uses Ink2 plus an `离线` (offline) pill (§6.4); the ● line on the lock screen goes into a RecBg pill, with the warm glow 2 px away from it (§6.8). The new edge pair `Warn`/`Select` (the color bar on a selected waiting row) was added to `AA_PAIRS` in `tokens.py`.

**Matching the mockups**: meeting timeline fills the width, ★ no longer blurs, segment number moves to the title row (§6.7); memo waveform linear, the waveform kept while saving (§6.6); menu label position, badges, hints (§6.5); the space between the status bar ● and the timer (§6.1); several hint bars (§12); home age (`age_text`, docs/20 U22), Mando sitting when waiting, no "all done" when offline (§6.3, §7.1); the pages without mockups rearranged (§6.9).

**Code**: `index_of` returns a sentinel for values beyond the tokens, and ci.yml forbids casting numbers to `Color` outside `lib/theme` (§3.2); new public interfaces `theme::hint_fits`, `theme::keys_text` (§4.1); `home_grid` for the four-row home (§6.3); the regeneration tests of `tools/theme` only run when `CPA_WHISPERWOOF` points to a checkout and the installed Pillow is the version written in `mando_colours.json`, and the hard-coded `/Users/…` path was removed; `test_rec_visible` no longer checks a dimmed ● as if it were the original color, and requires at least one undimmed ● ≥ 4.5:1; the test build records the longest full screen and animation frame (§10).

**"看到 ● REC 再说话" (speak when you see ● REC)**: the sentence you chose in v0.3.1; once the timer started, the screen used to show only the large ● and the timer, with no "REC". This time we chose the option that does not change your sentence: while recording, a small `REC` is drawn to the right of the timer (Rec color, §6.6). If you would rather change the sentence (for example "speak when you see the timer running"), that touches one line of `rec_pages.cpp`, `test_rec_overlay`, `test_theme_sim` and docs/04, 11, 19 (👤).

**Simulator scripts**: the meeting part of `v04_tour` used to have no `mic` after 0:05, so the timeline did not grow and ★ had nowhere to land; now every part is fed `mic … speech` (one block per 128 ms on the device). `theme_states`'s `home-hop` waits 300 ms before the shot (before, it captured the memo page's "Saving…").

**Not done yet (needs the real device, 👤)**: the warm colors on the panel, and `● mm:ss` on RecBg at Dim; `test_runtime_budget` (free heap, largest block, `max_render_us`, `max_tick_us`, `meeting_us` < 50 ms); `rec_soak` with the menu open; the M5GFX oracle for `push(Rect)` and `breathe`; how the dB level looks in a noisy room and at meeting distance (§11 item 15). Simulator screenshots of the four-row home can only be taken once `feat/app-sdk` is merged and there are app tiles; for now there are only unit tests of `home_grid`.

**When merging `feat/app-sdk`**: home lays out four rows with `home_grid(tile count)`; add `theme` to the allow list in `tools/apps/app_rules.py`; switch `check_apps`'s hint width check to `theme::hint_fits` (§4.4). These three cannot be done on this branch: those two files only exist on `feat/app-sdk`.

**When merging #43 (49e5b99)**: both sides changed `Runtime::update` to `bool redraw = true` (`runtime.h`). Keep one `update()`, using #43's `same_link` and `sync_link_slice`, and keep this branch's split of `render` into `render_full` and `animate` (plus the test build's timing). After merging, run `test_a_pull_does_not_redraw_per_chunk` again: it only counts full screens and should still pass.

## Appendix: evidence index

- **Direction one and reviews**: `direction` in `scratchpad/visual/companion_spec.json` (palette, fonts, motion, cost, risks), `judges[0..1]` (scores, recommendation, synthesis_ideas), `research[0..2]`.
- **Mockups and scripts**: `scratchpad/visual/companion/{01_home,02_board,03_menu,04_memo,05_meeting,06_lock,07_states}.png`, `frames/anim_listen_{1..4}.png`, `frames/anim_regions@2x.png`; `companion/src/lib.py` (palette, rounded corners, pills, status bar, keycaps, signs), `s_home.py`, `s_board.py`, `s_menu.py`, `s_rec.py`, `s_lock.py`, `s_states.py`, `sprite.py` (crop, scale, 15 colors), `build_all.py` (only uses colors from the palette). The mockup palette has 92 colors after deduplication [meas: the author of this doc reran `lib.PAL` without writing PNGs].
- **Firmware**: `lib/hal/m5_display.cpp:14-25` (`rgb565()`), `:46-55` (`begin`, `setColorDepth(8)`), `:57` (full-screen `pushSprite`); `lib/module_api/canvas.h` (four calls, 8 colors, layout constants); `lib/shell/runtime.cpp:220-230` (`render`), `runtime_rec.cpp` (`same_rec` in `mirror_rec` includes `level`); `rec_core.cpp:173` (`EvtKind::Level`); `status_bar.cpp`, `rec_overlay.cpp`, `lock_screen.cpp`, `shell_render.cpp`, `toast.h`, `power.cpp` (`backlight_of`); `lib/help/menu_view.cpp` (`kMenuVisibleRows`, `kMenuKeyColW`); `modules/home/home_module.cpp`, `home_logic.h` (`kHomeTiles`), `modules/board/board_module.cpp`.
- **Simulator**: `firmware/sim/lib/simfb/{fb.h,fb.cpp,fb_canvas.*,sim_display.h}`, `lib/simpng/png.h`, `README.md` ("How close it is to the device", "Code the simulator repeats").
- **Tests**: `test/ui/test_rec_visible/test_main.cpp`, `test_status_bar_states`, `test_mod_home`, `test_mod_board`, `test_menu_view`, `test_rec_overlay`, `test_runtime_rec`, `test_lock_power`; `test/ui/support/fake_canvas.h`; sim `test_fb`, `test_shots`.
- **CI and budgets**: `.github/workflows/ci.yml` (include rules, gcovr, budget, sizes, lizard, sim); `scripts/check_budget.py` (static RAM ≤96 KB, image ≤2.5 MB, 1.75 MiB warning); `scripts/stack_budget.py` (`APP_DIRS`, 4 KB per frame, loop 12 KB); `test/device/test_runtime_budget` (stack ≥4 KB, heap ≥150 KB, largest block ≥64 KB); `docs/07-hil-checklist.md` lines 848, 898, 902 (v0.4.0 measurements).
- **App SDK**: on `feat/app-sdk`, `module.h` (`AppTile`, `Module::tile`, `ModuleContext::appTile`), `modules/home/home_logic.h` (`kHomeAppsMax = 3`, the fourth row), `docs/16-writing-apps.md` §3.4, §4, §6.
- **WhisperWoof** (read-only): `website/mando/hd/{wait,think,review,hop}.webp` (sha256 prefixes 4cb38c44…, 91489609…, 9924f586…, ee4dc0db…); `src/assets/fonts/Nunito-normal-latin.woff2` (20fc9b6f…) and `OFL.txt`; `LICENSE` (MIT, Copyright (c) 2026 Heqing Huang). These files are byte-identical in both directories.
- **Research**: research A (WhisperWoof's design language, `pickMandoAction`, RGB332 ruined the warm colors); research B (palette canvas, `c & 0xFF`, runtime anti-aliasing drawing wrong colors, push time table, flash headroom); research C (similar products: Flipper, Pebble, claude-desktop-buddy, Pwnagotchi).
