# Labyrinth — Technical Documentation

## 1. The game

Labyrinth is a 3D maze game written in C++17 with the raylib library. The player starts in the top-left cell (0, 0) and must reach the exit in the bottom-right cell. The maze can be seen in third person (chase camera, the default), first person, from above in 3D, or as text.

### 1.1 Rules

1. The maze is N x N cells (10 ≤ N ≤ 50). The player moves one cell at a time; walls block movement.
2. **Special doors** are walls that shorten the way to the exit. A door opens with one **key**; the key is used up and the door stays open. There are half as many keys as doors (rounded up).
3. **Items**: gems (+50 points), Torch (see further in fog), Speed Boots (12 s), Hammer (1 charge, breaks one inner wall, −40 points; at most `1 + size/25` per maze), Hourglass (freezes Minotaurs for 8 s), Heart (+1 life; never placed in one-life runs), Compass (25 s: points to and draws a free route to the nearest gem, then the exit).
4. **Hazards**: spike traps (hurt while up; 1.2 s up, 1.6 s down, each with its own rhythm), Minotaurs (lose a life when caught, then 3 s of safety), time limits.
5. **Teleporters** come in pairs and sit in dead ends; stepping onto one puts you **on** its twin, facing the way out. Each pair has its own colour and number.
6. **Fog of war**: only cells within `1 + torchLevel` of the player are revealed.
7. Losing all lives or running out of time ends the run.
7a. **Gauntlet** (`RunConfig::minotaurChase`, used by Minotaur Hunt): every Minotaur waits at the entrance and is let loose 4 s, 13 s and 22 s into the run, then always hunts.
8. **Score** = `(size × 100 + 2 × (shortest route − moves) − seconds − 100 × hints + 25 × doors + 50 × gems + 40 × lives left − 40 × walls smashed + 3 × seconds left) × difficulty factor` (never below 0).
9. **Gem tour**: `computeTourMoves()` walks start → nearest gem → … → exit with BFS; this is the realistic length of a full run.
10. **Stars** (campaign): 1 = escaped, 2 = within par time, 3 = within par with no hints, no lives lost, every gem and no more steps than the step par. Par time = `(0.45 × gem tour + 2 × size) × 1.3 if foggy × difficulty factor`, rounded up to 5 s. Step par = `gem tour × difficulty factor`.

| Difficulty | Par time | Step par (3 stars) | Minotaur seconds/step | Score |
|---|---|---|---|---|
| Easy | × 1.4 | no limit | × 1.3 | × 0.75 |
| Normal | × 1.0 | × 1.6 | × 1.0 | × 1.0 |
| Hard | × 0.78 | × 1.3 | × 0.85 | × 1.25 |
| Expert | × 0.62 | × 1.12 | × 0.72 | × 1.5 |

### 1.2 Number Trail

From the current square the player may move right, down or diagonally; the correct step is always onto the smallest reachable number. A wrong step costs one of three lives. Correct steps less than 2 s apart build a combo worth `5 × combo` bonus points each.

## 2. Architecture

```
            ┌──────────────── the 3D app (src/app) ────────────────┐
 main.cpp ─►│ screens ─► widgets  render3d  postfx  particles audio │──► raylib (OpenGL, audio, input)
            └──────────────────────────┬───────────────────────────┘
                                       │ reads the state, calls movePlayer(), updateRun() ...
            ┌──────────────── game rules (src) ─────────────────────┐
            │ game ─► maze      campaign   profile   savegame       │  pure C++: no cin/cout,
            │ number_trail      validation   custom_map             │  no graphics, fully tested
            └───────────────────────────────────────────────────────┘
                 ▲                                    ▲
     tests/test_main.cpp (74 tests)        terminal edition (input.cpp, ui.cpp)
```

**Rule of the project:** game logic never draws and never reads input. The logic reports what happened through an **event list** (`GameState::events`: Step, Bump, KeyPickup, DoorOpened, GemPickup, Caught, Won …). The app reads the events to play sounds, spawn particles and show messages, then clears them. This keeps the rules testable and lets two very different front-ends (3D app and terminal) share one rule set.

| Module | Part | Purpose |
|---|---|---|
| `maze.h/.cpp` | logic | storage, generation (DFS), special doors, keys, BFS path-finding |
| `game.h/.cpp` | logic | runs: items, traps, teleporters, Minotaur AI, fog, timers, score, events |
| `campaign.h/.cpp` | logic | 15 levels, 6 challenges, daily seed, par time, stars |
| `profile.h/.cpp` | logic | statistics, 20 achievements, settings, leaderboards, text-file saving |
| `savegame.h/.cpp` | logic | save/load a running game |
| `custom_map.h/.cpp` | logic | hand-made maps: editing rules, problem checks, verification checksum, `.lmap` files, starting a run from a map |
| `number_trail.h/.cpp` | logic | the "smallest numbers" puzzle and combos |
| `validation.h/.cpp` | logic | parsing and checking user input |
| `app/*` | app | everything you see and hear |

## 3. Backend — game rules

*Author: ______ (Backend Developer)*

### 3.1 Storing the maze (dynamic 2D arrays)

```cpp
enum class Tile : unsigned char { Wall, Path, Door, Key };
struct Maze { int size; int tileCount; Tile** tiles; unsigned int seed; int doorCount, keyCount; };
```

An N x N maze is stored as a `(2N+1) × (2N+1)` grid of tiles: cell `(r, c)` is tile `(2r+1, 2c+1)`, and the tile between two cells is the wall, passage or door between them. A 50x50 maze is 101 × 101 tiles (10 KB).

| Dynamic array | Created by | Freed by |
|---|---|---|
| `Tile** tiles` | `allocateMaze()` | `destroyMaze()` |
| `bool** visitedCells, hintTiles, discovered` | `createFlagGrid()` | `destroyFlagGrid()` |
| `Item** items` | `createItemGrid()` | `destroyItemGrid()` |
| `int* teleportLinks` (size²) | `startRun()` | `endGame()` |
| `Minotaur* minotaurs` | `spawnMinotaurs()` | `endGame()` |
| BFS queue, DFS stack, distance arrays | inside each algorithm | same function |

Every destroy function resets pointers to `nullptr`, so calling it twice is safe. Tests and the full app run cleanly under AddressSanitizer.

### 3.2 Generating the maze — randomised depth-first search

`carvePassages()` builds a *perfect* maze (exactly one route between any two cells): start at (0, 0), repeatedly move to a random unvisited neighbour while knocking down the wall between, and backtrack when stuck. An explicit dynamic stack replaces recursion, so a 50x50 maze (2,500 cells) cannot overflow the call stack. `std::mt19937` seeded with the maze seed makes every maze reproducible.

### 3.3 Special doors and keys

`addDoors()` runs a BFS from the start, then looks at every wall between two cells. If the corridor distances on its two sides differ by more than N, opening it is a real shortcut, so it becomes a candidate. Candidates are shuffled (Fisher–Yates) and N/5 become doors. `placeKeys()` then places `(doors + 1) / 2` keys.

### 3.4 Shortest paths — breadth-first search

`computeDistances()` fills an array with the number of steps from one cell to every other cell. `findShortestPath()` runs it **from the exit** and walks "downhill" from the player, so no parent pointers are needed. It is used for the par route, hints, door placement, item placement and the Minotaur.

### 3.5 A run: `RunConfig` and `startRun()`

Campaign levels, challenges and free play all describe a run with one `RunConfig` (size, seed, lives, Minotaurs, fog, power-ups, traps, teleporter pairs, gems, sealed exit, time limit, hints, difficulty, gauntlet). `startRun()` is built from three steps that custom maps reuse: `initRunGrids()`, placing things, `finishRunSetup()`. It:
1. clamps the config (`clampConfig()`), rejects invalid sizes;
2. creates the maze and all grids;
3. computes the shortest route (for score and par);
4. places gems, power-ups (only useful ones: torches only in fog, hourglasses only with a Minotaur, hearts only with more than one life, rare hammers …), spike traps (never next to the start) and teleporter pairs (in dead ends, far apart along the corridors);
5. spawns Minotaurs in the far half of the maze (or at the entrance, waiting, in a gauntlet);
6. reveals the map (all of it, or only around the player in fog).

Everything is driven by one seeded random generator, so **the same seed always gives the same maze and the same items** — this is how the Daily Maze is identical for everyone.

### 3.6 Movement — `movePlayer()`

1. Edge of the maze or wall → blocked (`Bump` event).
2. Door: blocked without a key; with a key it opens for good.
3. Move; pick up a key, a gem or a power-up; spikes hurt if they are up; teleporters move you to the twin pad.
4. Reveal fog; check the exit (sealed exits need every gem); check Minotaur collision; refresh the hint.

### 3.7 Time — `updateRun(state, seconds)`

Called every frame by the app. Advances the clock, counts down power-ups and invulnerability, checks the time limit (warning events at 30 s and the last 10 s), hurts the player standing on rising spikes, and moves the Minotaurs.

### 3.8 The Minotaur AI

Each Minotaur has a step timer. When it fires:
- A BFS **from the player** gives every cell's distance to the player (computed once per frame, shared by all Minotaurs).
- If the Minotaur is within 10 corridor steps it **hunts**: it steps to the neighbour whose distance is one smaller — always along a shortest path. Hunting Minotaurs move 15 % faster; a `MinotaurAlert` event fires when the hunt starts.
- Otherwise it **wanders**: a random open neighbour, never turning back unless it is in a dead end.
- Minotaurs never use doors or teleporters. An Hourglass stops all of them.
- Catching the player costs a life; the Minotaur returns to its lair and rests for 2 s.
- Gauntlet: Minotaurs start un-released at the entrance (invisible, harmless), are released by time (`MinotaurReleased` event) and then always hunt.
- Difficulty scales the step time (`getMinotaurStepSeconds()`).

### 3.9 Campaign, challenges, achievements

- `campaign.cpp` holds a table of 15 `LevelDef`s (name, description, world, `RunConfig`) and 6 challenges. The Daily seed hashes the date (`makeDailySeed(2026, 11, 6)`).
- `profile.cpp`: `recordRun()` updates statistics after every run; achievements are pure functions of statistics (`getAchievementProgress()`), so `updateAchievements()` just compares and returns the newly earned ones.

### 3.10 Save files

All files are plain text, written and read with `<fstream>`:

| File | Content |
|---|---|
| `saves/<name>.profile` | `key=value` lines: statistics, settings, achievements (`0101…`), stars, best scores |
| `saves/leaderboards.txt` | `e <board> <score> <size> <moves> <seconds> <name>` |
| `saves/<name>.run` | the running game (`LABYRINTH_SAVE 2`): config incl. difficulty and gauntlet, player, every tile (`#`, `.`, `D`, `K`), items, visited and discovered grids, teleporter links, Minotaurs (with release times) and the random generator's state |
| `maps/<name>.lmap` | a hand-made map (`LABYRINTH_MAP 1`): name, author, world, rules, tiles, items, portal links, Minotaur lairs, and `verified <0/1> <checksum> <moves> <seconds>` |

### 3.11 Hand-made maps and verification

`custom_map.cpp` keeps editing legal: the outer wall, pillars and the entrance/exit openings can't change; items, keys and Minotaur lairs never go on the start or exit; a key and an item never share a cell; portals pair up automatically (max 3 pairs, max 3 Minotaurs, max 60 gems). `findMapProblems()` blocks a test play when the exit can't be reached, the only way is behind a door with no reachable key, a portal has no twin, or a sealed exit has missing/unreachable gems.

A map is **verified** only when its maker wins a test play: `markMapVerified()` stores an FNV-1a checksum of everything that affects play (rules, tiles, items, links, lairs — not the name). `isMapVerified()` recomputes it, so any later change, in the editor or by hand in the file, turns the map back into a draft. The map list only lets you play verified maps.

Loading never trusts the file: every number is range-checked, every character must be valid, and a broken file is rejected without leaving half-built data (`deserializeRun()` frees everything on failure). Because the random generator's state is saved too, a loaded game continues **exactly** as the original would have — a test checks this.

## 4. Frontend — the 3D app

*Author: ______ (Frontend Developer)*

### 4.1 Main loop and screens

`main.cpp` opens a resizable 1280x720 window (MSAA, v-sync), loads everything and runs the loop: update audio → update and draw the current screen → page transition → toasts. Each screen has `enter…()` and `update…()` functions; `goTo(screen)` starts an animated diagonal-stripe transition and swaps screens when the screen is fully covered.

### 4.2 Building the 3D maze

`buildWorld()` turns the tile grid into three meshes, uploaded to the GPU once per maze:
- **walls**: for every wall tile, a side face only where the neighbour is *not* a wall (hidden faces are skipped, so a 50x50 maze is a few thousand quads);
- **wall tops** and **floor**;
- a large **ground** plane around the maze (the neon grid, the lava sea, the snow field…).

The mesh is rebuilt only when the hammer breaks a wall. Props (keys, gems, power-ups, doors, the exit beacon, decorations, the Minotaur) are drawn from a few shared meshes (cube, sphere, cone, cylinder, torus, knot, a hand-made gem) with per-object transforms.

### 4.3 Lighting shader

A GLSL 330 shader (`render3d.cpp`) computes, per pixel:
- ambient light + up to **16 coloured point lights**: the player's torch (grows with Torch pickups, flickers), the exit beacon, and the nearest wall torches, items and Minotaur eyes, sorted by distance each frame;
- **emission**: the alpha channel of the wall/floor textures marks glowing pixels (neon lines, lava cracks), so they shine in the dark;
- **fog**: exponential-squared fog in the world's colour (much thicker in fog-of-war levels).

### 4.4 Post-processing

The scene is drawn into an off-screen texture, then (`postfx.cpp`):
1. a **bright-pass** keeps only bright pixels at quarter resolution;
2. two **Gaussian blur** passes (horizontal + vertical, twice) make the **bloom**;
3. a **composite** shader adds bloom, chromatic aberration and scanlines (CRT option), vignette, saturation, red **danger** pulse when a Minotaur is close, icy edges while frozen, and coloured screen **flashes** (damage, teleport, victory).

### 4.5 Procedural content

- **Textures** (`theme.cpp`): generated pixel by pixel at start-up from value noise, fractal noise, Voronoi cracks and brick patterns: neon circuits, dungeon bricks with moss, basalt with lava cracks, ice bricks with sparkles, candy-cane stripes.
- **Skies**: striped synthwave sun and wireframe mountains, volcanoes, aurora ribbons, clouds and a rainbow.
- **Audio** (`audio.cpp`): sound effects are built from oscillators (sine, square, saw, triangle, noise) with envelopes. Each world's **music** is composed by code: a chord progression, a scale, a drum style and a melody random walk become an 8-bar loop with echo, streamed seamlessly and cross-faded between tracks.
- **Fonts** are embedded into the executable (`tools/embed_fonts.py` → `embedded_fonts.h`).

### 4.6 Cameras and movement

The logic moves the player instantly; the app animates: the visible position glides between cells (0.17 s, 0.09 s with boots), turns ease over 0.15 s, the camera bobs while walking and shakes on hits. Three cameras: a **chase camera** 3.4 units behind and 3.3 above the player (default), **first person**, and **top-down**. Third and first person use relative controls (forward/back/turn/strafe), top-down and text view use absolute ones.

Minotaurs walk a queue of **cell centres** (waypoints), always along the corridor, so they never cut a corner through a wall; the model is drawn at 0.9 scale so arms and horns fit the 1-unit corridors.

The minimap arrow uses the same yaw as the 3D camera: the player faces `(sin yaw, cos yaw)` in world `(x, z)`, and the map draws `x` right and `z` down, so the arrow tip is `(sin yaw, cos yaw)` on screen. After a teleport, movement pauses until the keys are released, so you never walk straight off the pad.

### 4.7 User interface

Panels are drawn as a rounded fill plus a border band that shares the fill's exact corner radius (`drawRoundedBand()`), so borders never float inside or outside the panel. Titles use a clean circular outline and a drop shadow.

`widgets.cpp` is an *immediate-mode* toolkit: a screen calls `button("PLAY", rect)` every frame and gets `true` when it is clicked. Widgets are numbered in drawing order so the arrow keys / gamepad move a focus highlight and Enter activates. Text fields only accept allowed characters (digits for seeds; letters, digits, space, `-`, `_` for names) and the final value is checked with `validation.cpp` before use.

## 5. QA

*Author: ______ (QA)* — see [QA_DOCUMENTATION.md](QA_DOCUMENTATION.md).

## 6. Planning

*Author: ______ (Scrum Trainer)* — see [PROJECT_PLAN.md](PROJECT_PLAN.md).

## 7. Conventions

- Types `PascalCase`, functions and variables `camelCase`, constants `UPPER_SNAKE_CASE`, files `snake_case`.
- One job per function; every file starts with a comment that explains it.
- No warnings with `-Wall -Wextra -Wpedantic -Wshadow` (g++, clang, MinGW); MSVC builds with `/W4`.
