# Feature parity

How much of each game's console version works in the port. The source after version 0.2.1, checked on 2026-11-10;
each row of the summary says when its game was last checked.

**What "feature parity" means here.** Each table lists features of the PS3 version of a game and says whether each
one works in the port. The list is ours: it names what a player sees and does, not every detail of the game.

**These numbers are estimates.** They count rows; they do not weigh how big or how hard a feature is.

**The formula.** Every row counts the same. Works = 1, partial = 0.5, not tried, not reached yet or missing = 0.
Parity = the sum divided by the number of rows. A feature we have not tried counts as 0 even if it may already work,
so the estimate is on the low side. Trophies and the games' online features are not counted: the original online
services are closed, and the port answers the games' network calls as a console with no connection does.

**How it was checked.** Every run was the Windows program under Proton on Linux; none was on Windows itself.
"Scripted" means a run with no player: a script presses the pad's buttons and moves its sticks at set times.
"Hand play" means a person played it. Frame rates are frames shown per second. The scripted rates of 2026-11-04 are
the mean over a run's seconds 100 to 280 (*Tools of Destruction*: its last 20 seconds); those of 2026-11-10 are over
the part of the run the row names.

## Summary

| Game | Title ID tested | State reached | Parity (estimate) | Last checked |
|---|---|---|---|---|
| Ratchet & Clank Future: Tools of Destruction | BCUS98127 | plays by hand from the first level; scripted, a new game from an empty install and about 11 minutes in Metropolis; not played through | 48 % | 2026-11-10, scripted; hand play 2026-11-03 (keyboard) and October 2026 (earlier build) |
| Ratchet & Clank Future: Quest for Booty | BCES00301 | installs its data; a new game reaches the ship deck, with Ratchet under scripted control | 33 % | 2026-11-10, scripted |
| Ratchet & Clank Future: A Crack in Time | BCUS98124, 1.20 update | a new game reaches its first save; then black in most runs | 23 % | 2026-11-10, scripted |
| Ratchet & Clank: All 4 One | BCUS98175, 1.03 update | the menus reach the first level, then the game stops | 23 % | 2026-11-10, scripted |
| Ratchet & Clank: Full Frontal Assault | BCUS98380, 1.05 update | installs its data; the screen stays black | 8 % | 2026-11-04, scripted |
| Ratchet & Clank: Into the Nexus | BCUS99245, 1.01 update | installs its data and shows its intro movie; then the lobby stays black | 11 % | 2026-11-10, scripted |
| Ratchet: Deadlocked HD | NPUA80646 | not built yet | 0 % | - |
| Ratchet & Clank Collection: Ratchet & Clank 1 | BCUS98282 | a new game reaches Veldin, the first level, with Ratchet under scripted control | 30 % | 2026-11-10, scripted |
| Ratchet & Clank Collection: Ratchet & Clank 2 | BCUS98282 | a new game reaches Aranos, the first level, with Ratchet under scripted control | 23 % | 2026-11-10, scripted |
| Ratchet & Clank Collection: Ratchet & Clank 3 | BCUS98282 | a new game reaches Veldin, the first level, with Ratchet under scripted control | 27 % | 2026-11-10, scripted |

## Ratchet & Clank Future: Tools of Destruction

27 rows: 7 work, 12 partial, 8 not tried. **Estimate: 48 %.**

| Feature | On the console | Port | Notes |
|---|---|---|---|
| Start-up screens | boot logos and legal text, then the title flow | partial | the start-up runs; the logos are not shown yet |
| Game-data install | the first start installs about 400 MB, then "Setup complete" | works | 417 MB installed, "Setup complete" shown |
| First-start text | language data notice on the first start | works | shown |
| Main menu | NEW GAME, LOAD, DELETE and OPTIONS over a movie | works | used with a DualShock 4 in hand play, October 2026 |
| Menu background movie | a movie plays behind the menu | partial | plays; one colour is wrong (Ratchet's brown legs show green) |
| Menu text reflection | menu labels with a reflection below | partial | the reflection is drawn; its fade is not checked against the console |
| Options menu | audio, display and control settings | not tried | |
| Pad input in menus | D-pad, sticks and buttons move through menus | works | hand play with a DualShock 4, October 2026: title menu, LOAD, pause menu |
| Pause menu | pause menu with its sub-menus | partial | the menu and its sub-menus work in hand play; not every entry checked |
| Saving | saves to the console's hard disk | partial | a new game writes its save and the game saves in play; there is no save-slot screen yet, and one automatic save call is not handled yet |
| Load and delete a save | LOAD and DELETE in the main menu | partial | a saved game loads into Fastoon; DELETE does nothing yet |
| Prerendered cutscenes | movies between levels | partial | the new-game intro plays to its end, with the same colour error as the menu movie |
| In-engine cutscenes | real-time scenes in levels | not tried | |
| Planets and levels | the campaign's planets in order | partial | Metropolis (new game), Fastoon (saved game), Cobalia and further planets reached by hand; none played to its end |
| Ratchet on foot | walk, jump, wrench, jetpack | works | hand play, October 2026; the ground holds after every level load. Scripted, 2026-11-10: about 11 minutes of moving, jumping, wrench and firing in Metropolis after a new game from an empty install, twice, no crash |
| Combat | enemies fight back and can be beaten | works | fights in Fastoon won by hand, October 2026 |
| Weapons and upgrades | buy, level up and upgrade weapons | not tried | |
| Gadgets and Clank sections | gadgets and Clank's own sections | not tried | |
| Ship travel | pick a planet on the map and fly there | works | from Fastoon to Cobalia and on, by hand, October 2026 |
| Space combat | on-rails ship sections | not tried | |
| Motion control | Sixaxis tilt in some sections | not tried | pads without motion sensors will need a mapping |
| Audio | music, effects and voices | not tried | test runs have sound turned off |
| Video output | 720p | partial | a 1280x704 picture; window, borderless and fullscreen work; no scaling options yet |
| Scene rendering | levels drawn whole and stable | partial | full scenes; the too-strong reflections and the mirrored HUD prompt at Fastoon's spaceport are fixed in a scripted run on 2026-11-04; the picture still flickers in one cave on Fastoon, and glow looks washed out (scripted runs, 2026-11-10) |
| Level loading | levels load without visible stalls | partial | levels load, but loads stall while graphics pipelines are built, and the game's own stall notice can show |
| Frame rate | the console's own frame rate (not measured by us) | partial | hand play, October 2026, earlier build: about 37 per second in Fastoon over 147 seconds, fights included. Scripted, 2026-11-04: about 47 at a Fastoon save, but about 15 in Metropolis after a new game. Scripted, 2026-11-10: 39.9 and 27.6 over two runs' time in Metropolis (the machine's load was not the same in both) |
| Challenge mode and extras | replay with upgrades, skill points, extras | not tried | |

## Ratchet & Clank Future: Quest for Booty

The game builds and starts. On a first start it installs its game data, then a scripted run chooses NEW GAME, the
game writes its save, plays its intro and the prologue's ship cutscene, and hands control to the player on the ship
deck. The script then moves Ratchet for about 90 seconds: crates broken, bolts collected. One run, 2026-11-10, no
crash. 18 rows: 1 works, 10 partial, 7 not reached or not tried. **Estimate: 33 %.**

| Feature | On the console | Port | Notes |
|---|---|---|---|
| Start-up and front end | start-up screens, then the front end | partial | the front end is shown |
| Game-data install | the first start installs the game data | works | about 4 GB installed in a scripted run on 2026-11-04, then the game goes on |
| Main menu | new game, load, options | partial | NEW GAME starts the game; the other entries were not tried |
| Options menu | controls, display and sound | not tried | |
| Pad input in menus | D-pad and buttons navigate | partial | scripted button presses only |
| Pause menu | pause menu and its pages | not reached yet | |
| Saving | the game saves to the HDD | partial | NEW GAME writes its save |
| Loading a save | LOAD in the menu | not tried | |
| Cutscenes | story scenes | partial | the intro and the prologue's ship cutscene are drawn |
| Levels | the game's levels in order | partial | the prologue's ship deck is reached (scripted, 2026-11-10); no level finished |
| Ratchet on foot | move, jump, wrench | partial | moves under the script's stick input, breaks crates and collects bolts; the rest not checked |
| Combat | enemies fight back and die | not tried | pirates are on the ship deck; fights not checked |
| Weapons and upgrades | buy and upgrade weapons | not reached yet | |
| Gadgets | gadgets and their sections | not reached yet | |
| Audio | music, effects and voices | not tried | test runs have sound turned off |
| Picture | 720p, full scenes | partial | the front end, the cutscenes and the ship deck are drawn |
| Level loading | loads without visible stalls | partial | the cutscene comes after a load of about 30 seconds with no new frames |
| Frame rate | the console's own frame rate | partial | scripted, 2026-11-04: about 80 per second in the front end, 12 to 14 in the cutscene; scripted, 2026-11-10: 11.2 on the ship deck |

## Ratchet & Clank Future: A Crack in Time

1.20 update. The game builds and starts. In scripted runs on 2026-11-10 it does its first-start setup, plays the
install movie, reaches the title screen, and a new game (NEW GAME, difficulty, brightness) writes its first save.
After the save, one run in five shows the next dialog; the others stay black and do not react to the pad. Not
playable. 15 rows: 7 partial, 8 not reached or not tried. **Estimate: 23 %.**

| Feature | On the console | Port | Notes |
|---|---|---|---|
| Start-up and intro | start-up screens, then the intro | partial | the setup, the install movie and the title screen are shown |
| Main menu | new game, load, options, extras | partial | NEW GAME chosen by the script; the other entries not tried |
| Options menu | controls, brightness, motion and sound | not reached yet | the brightness screen of a new game is passed |
| Input | pad in menus and in play, four control schemes | partial | scripted presses work in the menus; after the save the game ignores them in most runs |
| Saving and loading | save and load from the menu | partial | a new game writes its first save; loading not tried |
| Cutscenes | movies and real-time story scenes | partial | the intro cinematic is drawn |
| Planets and levels | the campaign's planets, moons and stations | not reached yet | |
| Clank's time puzzles | Clank's sections with time recordings | not reached yet | |
| Hoverboots and gadgets | hoverboots and other gadgets | not reached yet | |
| Weapons and upgrades | weapons, Constructo mods, Groovitron | not reached yet | |
| Space flight | the Aphelion between planets and moons, ship upgrades | not reached yet | |
| Challenge mode and extras | challenge mode, skill points, arena, extras | not reached yet | |
| Audio | music, effects and voices | not tried | test runs have sound turned off |
| Picture | 720p, full scenes | partial | the intro, the install movie, the title and the new-game screens are drawn; black after the save in most runs |
| Frame rate | the console's own frame rate | partial | scripted, 2026-11-04: 16.7 per second; scripted, 2026-11-10: 8 to 20 during the install movie, 60 at the title screen |

## Ratchet & Clank: Into the Nexus

1.01 update. The game builds and starts. It installs its data and shows its intro, a news broadcast with subtitles;
then the lobby stays black (2026-11-10). It ran at 27.6 frames per second in a scripted run on 2026-11-04, on a black
screen. The main menu is not reached yet. 14 rows: 3 partial, 11 not reached or not tried. **Estimate: 11 %.**

| Feature | On the console | Port | Notes |
|---|---|---|---|
| Start-up and intro | start-up screens, then the intro | partial | the install and the intro are shown; then the lobby stays black |
| Main menu | new game, load, options, extras | not reached yet | |
| Options menu | controls, display and sound | not reached yet | |
| Input | pad in menus and in play | not tried | |
| Saving and loading | save and load from the menu | not reached yet | |
| Cutscenes | movies and real-time story scenes | partial | the intro is drawn |
| Levels | the campaign's levels | not reached yet | |
| Battle arena | arena challenges | not reached yet | |
| Gravity and Netherverse sections | gravity gadgets and Clank's Netherverse sections | not reached yet | |
| Weapons and upgrades | weapons that level up with use | not reached yet | |
| Skill points and extras | skill points, extras | not reached yet | |
| Audio | music, effects and voices | not tried | test runs have sound turned off |
| Picture | 720p, full scenes | partial | the intro is drawn; the lobby after it is black |
| Frame rate | the console's own frame rate | not tried | 27.6 per second in a scripted run, but on a black screen, so it does not count yet |

## Ratchet & Clank: All 4 One

1.03 update. The game builds and starts. In scripted runs on 2026-11-10 it installs its data, plays the install
movie and reaches the title screen; the script goes through the main menu, the game options and the character select
and launches the first level, Luminopolis. The level's intro is shown, then the HUD over a white 3D view, and after
about 100 seconds the game stops drawing. Not playable. 13 rows: 6 partial, 6 not reached or not tried, 1 missing.
**Estimate: 23 %.**

| Feature | On the console | Port | Notes |
|---|---|---|---|
| Start-up and title screen | start-up screens, then the title screen | partial | the install notice, the install movie and the title screen are shown |
| Main menu | play, options, extras | partial | the offline game options, the character select and the level launch work with scripted presses |
| Options menu | controls, display and sound | not tried | |
| Input | pad in menus and in play | partial | scripted presses work in the menus; in the level the game stops before it reads them |
| Local co-op | up to four players on one console | not reached yet | |
| Saving and loading | save and load | not reached yet | |
| Cutscenes | story scenes | partial | the first level's intro is shown |
| Levels | the campaign's levels | missing | Luminopolis is reached, but its 3D view is white and the game stops after about 100 seconds |
| Weapons and upgrades | weapons and their upgrades | not reached yet | |
| Combat | enemies and bosses | not reached yet | |
| Audio | music, effects and voices | not tried | test runs have sound turned off |
| Picture | 720p, full scenes | partial | the menus and the level intro are drawn; the level's 3D view is white |
| Frame rate | the console's own frame rate | partial | scripted, 2026-11-04: 12.6 per second on the title screen; scripted, 2026-11-10: 23.3 in the menus, about 8 in the level before it stops |

## Ratchet & Clank: Full Frontal Assault

1.05 update. The game builds and starts, installs its data and runs at 30 frames per second in a five-minute
scripted run, but the screen stays black. Nothing is visible yet. 13 rows: 2 partial, 11 not reached, not tried
or missing. **Estimate: 8 %.**

| Feature | On the console | Port | Notes |
|---|---|---|---|
| Start-up | start-up screens, then the title flow | partial | the game starts and keeps running; nothing is shown |
| Game-data install | data installed on the first start | partial | the install runs; its end is not seen on screen |
| Main menu | campaign, versus, options | not reached yet | |
| Options menu | controls, display and sound | not reached yet | |
| Input | pad in menus and in play | not tried | |
| Campaign levels | the campaign's planets | not reached yet | |
| Base defence | build and defend a base with turrets and barriers | not reached yet | |
| Local co-op | two players on one console | not reached yet | |
| Saving and loading | save and load | not reached yet | |
| Weapons and upgrades | weapons and their upgrades | not reached yet | |
| Audio | music, effects and voices | not tried | test runs have sound turned off |
| Picture | 720p, full scenes | missing | the screen stays black |
| Frame rate | the console's own frame rate | not tried | 30 per second in a scripted run, but on a black screen, so it does not count yet |

## Ratchet & Clank Collection: Ratchet & Clank 1, 2 and 3

PS2 remasters. Each of the three games builds into its own program (`rc1.exe`, `rc2.exe`, `rc3.exe`). In scripted
runs on 2026-11-10, five and a half minutes each, a new game reaches the first level with Ratchet under scripted
control: *Ratchet & Clank 1* about three minutes on Veldin, *Ratchet & Clank 2* about a minute on Aranos (mostly
against a window), *Ratchet & Clank 3* about three minutes of running, jumping and fighting on Veldin. None crashed.
Not playable. 15 rows each. **Estimates: *Ratchet & Clank 1* 30 % (9 partial), *Ratchet & Clank 2* 23 % (7
partial), *Ratchet & Clank 3* 27 % (8 partial).**

| Feature | On the console | RC1 | RC2 | RC3 | Notes |
|---|---|---|---|---|---|
| Start-up and front end | start-up screens, then the main menu | partial | partial | partial | the main menu is reached in each |
| Main menu | new game, load, options | partial | partial | partial | NEW GAME chosen by the script; in RC3, LOAD GAME on an empty save list stays in the menu |
| Options menu | controls, display and sound | not tried | not tried | not tried | |
| Input | pad in menus and in play | partial | partial | partial | scripted presses and stick moves only |
| Saving | the game saves to the HDD | partial | not tried | not tried | RC1: a new game writes its save |
| Loading a save | load from the menu | not tried | not tried | not tried | |
| Cutscenes | story movies | partial | not tried | not tried | RC1: the opening movies play before the first level |
| Levels | the game's planets in order | partial | partial | partial | the first level is reached (Veldin, Aranos, Veldin); none finished |
| Ratchet on foot | move, jump, wrench | partial | partial | partial | RC1 moves and jumps; RC2 runs, then stands against a window; RC3 runs and jumps |
| Combat | enemies fight back and die | not tried | not tried | partial | RC3: fights under scripted input |
| Weapons and upgrades | buy and upgrade weapons | not tried | not tried | not tried | |
| Ship travel | fly to the next planet | not reached yet | not reached yet | not reached yet | |
| Audio | music, effects and voices | not tried | not tried | not tried | test runs have sound turned off |
| Picture | full scenes | partial | partial | partial | the first level is drawn; not compared with the console |
| Frame rate | the console's own frame rate | partial | partial | partial | scripted, 2026-11-10, in the level: RC1 about 8, RC2 41 to 65, RC3 about 23 |

## Ratchet: Deadlocked HD

**Not built yet.** A PS2 remaster. Its main-CPU (PPU) code is lifted to C; no program has been built from it. Every
feature counts 0 until it runs. **Estimate: 0 %.**

## Port additions

What the port adds beyond the console version. These are not counted in the estimates. Gameplay changes stay
optional.

| Addition | State | Notes |
|---|---|---|
| Window, borderless and fullscreen | works | Alt+Enter switches; `display.ini` remembers the choice and the window's place and size; used in hand play, October 2026; place and size checked in scripted runs of *Tools of Destruction*, 2026-11-10 |
| FSR 1 upscaling | opt-in | `upscale = fsr1` in `display.ini`; checked in a scripted run of *Tools of Destruction*, 2026-11-10 |
| Shared settings | works in host tests | display and control keys in `shared/display.ini` and `shared/keybinds.ini` apply to every game; a game's own file wins; not yet tried in a game run |
| PC pads | works | Xbox-style pads and Sony pads through SDL3; a DualShock 4 was used in hand play, October 2026 |
| Keyboard | works | default keys for every pad button, changeable in `keybinds.ini`; *Tools of Destruction* played by hand on the keyboard on 2026-11-03 |
| Mouse look | opt-in, not tried by hand | off unless `look = on` is set in the `[mouse]` section of `keybinds.ini`; turning and looking up and down checked in scripted runs on 2026-11-04 only |
| QUIT TO MAIN MENU and QUIT TO DESKTOP in the pause menu | opt-in, known problems | off unless `RC_PAUSE_QUIT=1` is set; QUIT TO DESKTOP works in a scripted run. The two entries are drawn below the menu's box, the label shown can name the other entry, and QUIT TO MAIN MENU loads the last save again instead of stopping at the title screen |
| Frame rate above 30 | partial | the port is not held to 30: about 37 per second in hand play (October 2026); no frame pacing or frame cap options yet |

The other games' readiness is also in the [README](../README.md#compatibility) and the [roadmap](ROADMAP.md).
