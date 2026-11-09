# Labyrinth — Project Plan (GitHub)

*Author: ______ (Scrum Trainer)*

Project period: **21 Oct – 15 Nov 2026**. Defence: **16–17 Nov** (check the exact date with the teacher; the assignment PDF says 2025).

## 1. Sprints

| Sprint | Dates | Goal | Result |
|---|---|---|---|
| 1 | 21 – 27 Oct | Repository, maze data structure, generation, drawing | v1 terminal maze |
| 2 | 28 Oct – 3 Nov | Arrow keys, menu, validation, tests, Number Trail | v1 complete, play-tested |
| 3 | 4 – 8 Nov | **3D app**: renderer, 5 worlds, items, Minotaur, campaign, profiles, saving | v2 playable |
| 4 | 9 – 12 Nov | Play-testing, bug fixing, balancing, Windows build, docs | v2 release |
| 5 | 13 – 15 Nov | Every member studies their code (`CODE_TOUR.md`), presentation, two rehearsals | ready to defend |

## 2. GitHub setup

- **Project board**: *Backlog → To do → In progress → Review → Done*.
- **Labels**: `backend`, `frontend`, `qa`, `docs`, `bug`, `enhancement`, `graphics`, `audio`.
- **Milestones**: one per sprint.
- **Branches**: `main` always builds; one branch per issue (`feature/23-minotaur-ai`), merged by pull request after one review.
- **Releases**: tag `v1.0` (terminal edition) and `v2.0` (3D app) with the Windows `.exe` attached.

## 3. Backlog (GitHub Issues)

| # | Issue | Label | Owner | Sprint |
|---|---|---|---|---|
| 1 | Repository, folders, .gitignore, CMake | docs | Scrum | 1 |
| 2 | Maze data structure (dynamic 2D array) | backend | Backend | 1 |
| 3 | Maze generation (randomised DFS) | backend | Backend | 1 |
| 4 | Draw the maze in the terminal | frontend | Frontend | 1 |
| 5 | Arrow keys on Windows and Linux | frontend | Frontend | 2 |
| 6 | Movement rules and winning | backend | Backend | 2 |
| 7 | Menu and help screen | frontend | Frontend | 2 |
| 8 | Input validation | qa | QA | 2 |
| 9 | Test framework + first tests | qa | QA | 2 |
| 10 | BFS shortest path, doors and keys, hints | backend | Backend | 2 |
| 11 | Number Trail mode | backend | Backend | 2 |
| 12 | raylib window, 3D maze meshes, cameras | frontend | Frontend | 3 |
| 13 | Lighting + fog shader | frontend | Frontend | 3 |
| 14 | Bloom / CRT post-processing | frontend | Frontend | 3 |
| 15 | 5 worlds: generated textures, skies, particles | frontend | Frontend | 3 |
| 16 | Synthesized sound effects and music | frontend | Frontend | 3 |
| 17 | Items: gems, torch, boots, hammer, hourglass, heart, compass | backend | Backend | 3 |
| 18 | Spike traps, teleporters, fog of war, time limits | backend | Backend | 3 |
| 19 | Minotaur AI (wander + BFS hunting) | backend | Backend | 3 |
| 20 | Campaign (15 levels), stars, par times | backend | Backend | 3 |
| 21 | Challenges + daily maze | backend | Backend | 3 |
| 22 | Profiles, statistics, 20 achievements, leaderboards | qa | QA | 3 |
| 23 | Save & Continue | backend | Backend | 3 |
| 24 | Menus, HUD, minimap, text view, map, pause, results | frontend | Frontend | 3 |
| 25 | Tests for every new rule (59 total) | qa | QA | 3 |
| 26 | Screenshot tour + end-to-end key test | qa | QA | 4 |
| 27 | Fix: overlays closing on the opening key | bug | Frontend | 4 |
| 28 | Fix: Ice/Candy over-exposed | bug | Frontend | 4 |
| 29 | Fix: Win event lost when event list full | bug | Backend | 4 |
| 30 | Balance: par times from real routes | enhancement | Backend | 4 |
| 31 | Windows .exe build | docs | Scrum | 4 |
| 32 | Documentation + code tour | docs | All | 4 |
| 33 | Presentation + rehearsals | docs | Scrum | 5 |
| 34 | Fix: minimap arrow points backwards | bug | Frontend | 4 |
| 35 | Third-person chase camera as the default view | enhancement | Frontend | 4 |
| 36 | Difficulty levels (par time, step par, Minotaur speed, score) | enhancement | Backend | 4 |
| 37 | Spikes visible from above; calmer fog-of-war colour; dimmer Neon Grid | graphics | Frontend | 4 |
| 38 | Fix: results screen ignores ←/→, buttons appear late | bug | Frontend | 4 |
| 39 | Balance: compass route to gems, rare hammer, no hearts with 1 life, Crystal Rush 6 gems | enhancement | Backend | 4 |
| 40 | Fix: teleporter soft lock | bug | Backend + Frontend | 4 |
| 41 | Fix: fullscreen cannot be turned off | bug | Frontend | 4 |
| 42 | .exe icon = window icon | art | Frontend | 4 |
| 43 | Admin code + Admin profile | qa | QA | 4 |
| 44 | Map maker: logic, .lmap files, verification by test play | backend | Backend | 4 |
| 45 | Map maker: list + editor screens, drag & drop import | frontend | Frontend | 4 |
| 46 | Minotaur Hunt as a gauntlet | enhancement | Backend | 4 |
| 47 | Portal pairs with their own colours | graphics | Frontend | 4 |
| 48 | Fix: Minotaur clips through wall corners | bug | Frontend | 4 |
| 49 | Star Collector needs all 45 stars | enhancement | QA | 4 |
| 50 | Fix: panel borders and title colours (QA) | bug | Frontend | 4 |
| 51 | Tests for everything above (74 total) | qa | QA | 4 |

## 4. Commit messages

Format: `<type>: <short summary>` + a body that says *what* and *why*.

| Type | Use for |
|---|---|
| `feat` | new feature |
| `fix` | bug fix |
| `test` | tests |
| `docs` | documentation |
| `refactor` | change without new behaviour |
| `build` | CMake, Makefile |
| `art` | textures, shaders, sounds |

Examples:
```
feat: Minotaur hunts the player with BFS

Within 10 corridor steps the Minotaur follows the distance field
towards the player; otherwise it wanders without turning back.
Closes #19.
```
```
fix: Esc no longer closes the pause menu it just opened

The same key press was read by the game and by the menu in one
frame. Close keys are now ignored in the frame an overlay opens.
Closes #27.
```

## 5. Definition of done

- Builds with no warnings; all 74 tests pass.
- New rules have tests; new screens appear in the screenshot tour.
- Reviewed by one teammate, merged into `main`, issue closed.
- The author can explain the code in their own words (see `CODE_TOUR.md`).
