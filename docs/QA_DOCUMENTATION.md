# Labyrinth — QA Documentation

*Author: Ivan Trifonov (QA)*

## 1. Strategy

| Level | What | How |
|---|---|---|
| Automated unit tests | every game rule, generation, saving, achievements, validation, map maker | `tests/test_main.cpp` — **74 tests, 1078 checks** |
| Memory checks | leaks, out-of-bounds, undefined behaviour | tests **and the full 3D app** built with AddressSanitizer + UBSan |
| Visual smoke test | every screen and every world renders | `Labyrinth --tour <folder>`: automatic tour that saves 30 screenshots |
| End-to-end test | real key presses into the real window | `xdotool` scripts: profile → free play → views → map → pause → save → continue; admin code → fullscreen on/off → victory screen keys; map maker → test play → verified → publish |
| Platform tests | Windows build | MinGW `.exe`: tests and screenshot tour run under Wine |
| Compiler checks | no warnings | g++, clang and MinGW with `-Wall -Wextra -Wpedantic -Wshadow` |
| Manual tests | feel, sound, gamepad | checklist in section 5 |

The game rules never use `cin`/`cout` or graphics, so the tests link only the logic files and call the functions directly.

## 2. Running the tests

```bash
cmake -S . -B build && cmake --build build && ./build/labyrinth_tests
# or, without CMake / raylib:
make test
```

Memory check:
```bash
g++ -std=c++17 -g -fsanitize=address,undefined src/maze.cpp src/game.cpp src/number_trail.cpp \
    src/validation.cpp src/campaign.cpp src/profile.cpp src/savegame.cpp src/custom_map.cpp \
    tests/test_main.cpp -o tests_asan
./tests_asan
```

Screenshot tour (also a nice way to get pictures for the presentation):
```bash
./build/Labyrinth --tour screenshots
```

**Latest results:** 74/74 tests, 1078/1078 checks; no sanitizer errors in the tests or during the full app tour; Windows build passes all tests under Wine.

### The test framework

- `CHECK(condition)` counts a check and prints the failing expression with file and line.
- Each test is a function, listed with a readable name in `TEST_CASES[]`.
- `main()` prints `[PASS]`/`[FAIL]` per test and a summary, and returns 1 if anything failed (CI-friendly).

## 3. Automated test cases

### 3.1 Maze generation (T01–T11)

| ID | What it checks | Expected |
|---|---|---|
| T01 | sizes 9, 10, 33, 50, 51, −10 | only 10–50 valid |
| T02 | creating 5x5 / 100x100 | rejected, nothing allocated |
| T03 | 10x10 → 21x21 tiles; destroy twice | pointers reset, no crash |
| T04 | 4 sizes × 5 seeds | every cell reachable |
| T05 | open passages | exactly N² − 1 (perfect maze) |
| T06 | border | closed except entrance and exit |
| T07–T08 | same seed / different seeds | identical / different mazes |
| T09 | 20 mazes | correct door count; doors never make the route longer |
| T10 | keys | `(doors+1)/2`, never on start or exit |
| T11 | shortest path | starts at (0,0), ends at exit, every step to a neighbour |

### 3.2 Classic rules (T12–T23)

| ID | What it checks |
|---|---|
| T12 | new game starts at (0, 0), 0 moves, start visited |
| T13–T15 | walls and the outer edge block, corridors allow movement |
| T16–T17 | doors need keys and stay open; keys are picked up |
| T18–T19 | reaching the exit wins; walking the hint route wins in exactly the shortest number of moves |
| T20 | hint on/off, hiding it is free |
| T21 | score rules (time, hints, never negative) |
| T22 | scrolling viewport |
| T23 | leaderboard sorting, max 5 entries |

### 3.3 Input validation (T24–T27)

| ID | Valid | Rejected |
|---|---|---|
| T24 `parseInt` | `42`, `  7  `, `-3`, `+5` | empty, `abc`, `4x`, `1 2`, `-`, `3.5`, `99999999999` |
| T25 `parseUnsigned` | `0`, `4294967295` | `4294967296`, `-1`, `12a`, empty |
| T26 range / trim | 10 and 50 in [10, 50] | 9 |
| T27 names | `Maria`, `Team_4 QA` | empty, spaces only, `___`, 13+ characters, `bad!name` |

### 3.4 Number Trail (T28–T33, T59)

Creation (distinct numbers, start 0), best step is the smallest, wrong steps and game over, edge restrictions, following the best steps always finishes, digit counting, **combos** (build up, time out, reset on mistakes, bonus in the score).

### 3.5 Adventure rules (T34–T48)

| ID | Test | What it proves |
|---|---|---|
| T34 | run settings are clamped | 99x99, 9 lives, 7 Minotaurs… are fixed; an invalid size is rejected |
| T35 | items are placed | 12 gems, 4 teleporter pads with matching links, nothing on start/exit/keys, memory freed |
| T36 | power-up effects | torch, boots, hammer, hourglass, compass, hearts capped at the maximum |
| T37 | gems and sealed exit | exit refuses to open until every gem is collected |
| T38 | hammer | needs charges, outer wall unbreakable, inner wall becomes a corridor |
| T39 | teleporter | moves the player to the twin pad |
| T40 | spikes | hurt only when up; invulnerability after a hit; they go down again |
| T41 | losing all lives | game over, no more moves, score 0 |
| T42 | time limit | countdown, warning event, loss at 0 |
| T43 | Minotaur hunts | a waiting player is caught; −1 life; the Minotaur returns to its lair |
| T44 | Minotaurs and walls | over 400 updates a Minotaur never walks through a wall or jumps |
| T45 | hourglass | frozen Minotaurs do not move |
| T46 | fog of war | 2x2 revealed at start, torch widens it, no fog reveals everything |
| T47 | hints disabled | Iron Runner rule |
| T48 | events | reported, cleared, never overflow — and **Won/Lost are never lost even when the list is full** |

### 3.6 Campaign, profiles and saving (T49–T58)

| ID | Test | What it proves |
|---|---|---|
| T49 | campaign levels | all 15 start, are solvable, get bigger, end at 50x50 |
| T50 | challenges | all 6 start; daily seed is the same all day and different the next day |
| T51 | stars | 0/1/2/3 stars for each rule |
| T52 | statistics and achievements | stats update correctly; each achievement is earned once |
| T53 | campaign unlocking | a level opens when the previous one has a star |
| T54 | profile round trip | everything saved is loaded back identically |
| T55 | bad profile files | rejected; out-of-range numbers clamped; unknown lines ignored |
| T56 | leaderboards round trip | names with spaces, all boards |
| T57 | save game round trip | load == original, saving again gives the same file, and **both games continue identically for 200 updates** (Minotaur randomness included) |
| T58 | corrupt save files | rejected, nothing half-loaded, no leaks |

### 3.7 Play-test round 2: balance and difficulty (T60–T68)

| ID | Test | What it proves |
|---|---|---|
| T60 | difficulty levels | same maze on every difficulty; harder = less par time, fewer steps for 3 stars, faster Minotaurs, more points; Easy has no step limit |
| T61 | step par and stars | too many steps = 2 stars, within the step par = 3 |
| T62 | gem tour | no gems: tour = shortest route; with gems it is longer and reproducible |
| T63 | hammers | at most `1 + size/25` per maze; every smashed wall costs 40 points |
| T64 | one-life runs | Frostbite and Iron Runner never get hearts; Crystal Rush needs 6 crystals |
| T65 | teleporters | pads sit in dead ends; teleporting puts you ON the twin pad |
| T66 | compass | free route (no hint counted) to the nearest gem, gone when it runs out |
| T67 | gauntlet | Minotaurs wait at the entrance, are released one by one, hunt and catch a player who stands still |
| T68 | admin and Star Collector | admin code, everything unlocked, Star Collector needs all 45 stars, admin flag saved |

### 3.8 Map maker (T69–T74)

| ID | Test | What it proves |
|---|---|---|
| T69 | editing rules | outer wall, pillars, start and exit protected; keys and items replace each other; max 3 Minotaurs |
| T70 | portal pairing | pads pair up automatically, removing one leaves its twin waiting, max 3 pairs |
| T71 | problems | walled-in exit, door without a key, lonely pad, sealed exit with no/unreachable gems all block the test play |
| T72 | verification | only a won run verifies a map; renaming keeps it verified, changing the maze does not |
| T73 | .lmap round trip | everything saved loads back identically, and the run built from it has the same contents |
| T74 | bad map files | wrong header, open outer wall, bad size, fake links rejected; a hand-edited file loses its verified mark |

## 4. Visual and end-to-end tests

### 4.1 Screenshot tour

`Labyrinth --tour <folder>` drives the app by itself at a fixed 30 fps and saves screenshots of: splash, main menu, campaign, challenges, free play, every world in first person and top-down, an item showcase, the text view, the map, a Minotaur face-to-face, the victory screen with stars, pause, Number Trail, achievements, leaderboards, settings, how-to-play and profiles. The tour is used to check every screen after each change (see `docs/screenshots/`).

### 4.2 Real key presses

A script starts the game in a virtual display and sends real key presses with `xdotool` (held for 250 ms like a person would): splash → **types a profile name** → main menu → Free Play → START → walks forward → `V` top-down → walks → `T` text panel → `T` full-screen text → `T` off → `H` hint → `M` map → `M` close → `Esc` pause → *Save & Quit* → *Continue* → *Quit to menu*. Screenshots after every step are checked.

## 5. Manual checklist

| ID | Steps | Expected | ✓ |
|---|---|---|---|
| M01 | Start the game | animated splash, music; the taskbar and .exe icons are the same | ✅ |
| M02 | New profile `!!` then `Ana` | first refused with a message, second created | ✅ |
| M03 | Main menu with arrows, Enter, mouse, gamepad | focus moves, sounds play | ✅ |
| M04 | Free Play seed `99999999999` | error message, game does not start | ✅ |
| M05 | Free Play seed `7` twice | the same maze twice | ✅ |
| M06 | Third person (default) and first person: ↑ ↓ ← → Q E | walk, step back, turn, strafe; minimap arrow always points where you walk | ✅ |
| M07 | Walk into a wall | bump sound, small shake, no move | ✅ |
| M08 | Locked door without / with key | message / door sinks into the floor | ✅ |
| M09 | `V` three times, mouse wheel | third → first → top-down → third, zoom | ✅ |
| M10 | `T` three times | side panel → full-screen text → off | ✅ |
| M11 | `H` | glowing orbs show the route, −100 | ✅ |
| M12 | Hammer + Space at a wall | wall breaks with particles | ✅ |
| M13 | Minotaur level | eyes turn red when hunting, heartbeat, red edges, caught = −1 life | ✅ |
| M14 | Hourglass | Minotaurs freeze, icy edges | ✅ |
| M15 | Time Attack | countdown turns red at 30 s, ticks at 10 s | ✅ |
| M16 | Win a campaign level | stars appear one by one, score counts up, next level unlocks | ✅ |
| M17 | Pause → Save & Quit → Continue | exact same game restored | ✅ |
| M18 | Number Trail: wrong step, clicking a square, diagonal with E | heart lost / step taken / diagonal | ✅ |
| M19 | Settings: volume, CRT, bloom, reduce motion, FOV | applied immediately and remembered | ✅ |
| M20 | Resize window, F11 twice, Settings fullscreen switch on/off, F12 | layout scales, fullscreen turns on AND off every time, screenshot saved | ✅ |
| M21 | Windows `Labyrinth.exe` on a school PC | starts without installing anything | ✅ |
| M22 | No sound card | game runs silently, settings shows a note | ✅ |
| M23 | Win a level, then ← → Enter / N / R / Esc on the results | buttons react at once | ✅ |
| M23c | Switch to another window while music plays | game fades to silence, comes back when you return (Settings: mute in background) | ✅ |
| M23d | Map Maker → TUTORIAL: YOUR FIRST MAP, follow all steps | each step ticks DONE and moves on; test play verifies the map | ✅ |
| M23f | Change a setting, end the game from Task Manager, start it again | the change is still there | ✅ |
| M23e | F3 in any screen | FPS chip appears / disappears | ☐ |
| M23b | Press ← firmly (about half a second), then tap ↑ firmly | exactly one 90° turn, exactly one step; holding ↑ keeps walking | ✅ |
| M24 | Step onto a portal while holding ↑ | you land on the twin pad (same colour) and stay there until you let go | ✅ |
| M25 | Minotaur Hunt | Minotaurs appear behind you one by one and chase you | ✅ |
| M26 | Settings → admin code `MINOTAUR` | every level, star and achievement unlocked; F9 warps to the exit | ✅ |
| M27 | Map maker: new map, carve, items, name, TEST PLAY, win | map becomes VERIFIED and playable from the list | ✅ |
| M28 | Copy a `.lmap` to another PC and drop it onto the window | map appears in the list | ✅ |

## 6. Bugs found and fixed

| Bug | Found by | Cause | Fix |
|---|---|---|---|
| 20x20 Number Trail ended showing position 19, 19 | play test (v1) | 0-based positions printed as-is | `Row 20/20 Col 20/20` display |
| Old pages visible above new ones | play test (v1) | screen clear left the scroll-back | full console clear |
| Pressing `Esc`/`M` opened and closed the menu at once | end-to-end script | the same key press was read by the opener and the overlay | ignore close keys in the frame an overlay opens |
| Keyboard focus reset every frame during the game | screenshot tour | the HUD's drawing pass reset the focus | separate drawing-only UI style (`setUiStyle`) |
| Ice and Candy worlds completely white | screenshot tour | too much light and self-glow | lower ambient/emission, light clamp in the shader |
| Neon stripe smeared across the view | screenshot tour | glowing line exactly at eye height | stripes moved near floor and top |
| Win not reported after many moves in one frame | screenshot tour | event list full (64) | Won/Lost always replace the last event (test T48) |
| Gem mesh missing | log warnings | raylib cannot make a 2-ring sphere | hand-made six-sided gem mesh |
| Meshes uploaded twice | log warnings | raylib already uploads generated meshes | only upload meshes that are not on the GPU |
| 2nd-star par times unfair (0.49 s/step) | balance check | hand-picked numbers | par computed from each maze's real route |
| Minimap arrow pointed backwards when walking up/down | play test | the arrow used `π − yaw`, which flips the vertical | arrow uses the camera's yaw directly; it also follows the smooth walk |
| Compass arrow mirrored left/right in first person | code review | angle was `target − yaw` | `yaw − target` (the camera's right is world −x when facing +z) |
| Fullscreen could not be turned off | play test (QA) | the app switches to *borderless* fullscreen but asked raylib about *exclusive* fullscreen, which was always "off" | check the borderless state; the switch shows the real window state; Alt+Enter added |
| Results screen "locked" the keyboard | play test | buttons in a row but focus only moved with ↑/↓; buttons appeared after 2 s | ←/→ move focus, buttons at once, N / R / Esc shortcuts |
| Teleporter "soft lock" | play test | a held key walked you straight off the twin pad, sometimes back onto the first pad | movement pauses after a teleport until keys are released; pads only in dead ends; you face the way out |
| Minotaur clipped through wall corners | play test | it glided straight towards its new cell, cutting corners | waypoint queue of cell centres, model at 0.9 scale |
| Hammer let players flatten the maze | play test | 2 charges per pickup, many pickups | 1 charge, at most `1 + size/25` hammers, −40 points per wall |
| Crystal Rush took too long | play test | 12 crystals required | 6 crystals |
| Extra hearts in Frostbite / Iron Runner | play test | hearts placed whenever there were traps | hearts only when the run has more than one life |
| Panel borders stopped short of the edge | play test (QA) | three outline rings with different corner radii and gaps | one border band sharing the fill's exact corners |
| Titles looked smeared | play test (QA) | glow copies offset diagonally only | circular outline + drop shadow; one smooth gradient for the logo |
| Neon Grid hurt the eyes | play test | strong bloom and self-lit lines | bloom 1.15 → 0.7, dimmer lines and wall tops |
| A firm key press turned twice (180° instead of 90°) or walked two cells | play test | keys were read every frame while held; a turn only takes 0.15 s, so a press held a bit longer started a second one | keyboard-repeat behaviour: one press = one action; holding repeats only after 0.28 s (walking) / 0.55 s (turning, then every 0.4 s) |
| Map maker: a fast drag skipped cells | tutorial test | the mouse can cross several cells between two frames | the editor walks from the last cell to the new one, one neighbour at a time |
| Settings sometimes "forgotten" after closing | play test | saves lived next to the .exe; started from inside a zip, Windows runs it from a temp folder that gets wiped; a crash skipped the save on exit | per-device user folder (`%LOCALAPPDATA%\Labyrinth`), files next to the program copied in once, autosave within 1 s of any change, warning if a save fails |
| Levels 14 and 15 showed "LEVEL 14" instead of their names | play test | long names were replaced when they didn't fit | button labels shrink to fit |
| Taskbar showed the generic Windows icon | play test | GLFW's window class only uses an .exe icon named `GLFW_ICON` | icon resource renamed; window icon given at 6 sizes |

## 7. Bug report template

```
Title:        short description
Steps:        1. ... 2. ... 3. ...
Expected:     what should happen
Actual:       what happened
Mode / seed:  e.g. Free Play 25x25 seed 4242 (mazes are reproducible from the seed)
Platform:     Windows / Linux / macOS, GPU
Severity:     critical / major / minor
```
