# Online play

**Online play does not work yet in any game.** The network and PlayStation Network functions the games call answer as a
console with no connection does, and no game connects to anything. This page says what the console versions offer
online, what the port plans to do about it, and the rules it follows.

## Which games had online modes on the console

| Game | Online on the console | In the port |
|---|---|---|
| Ratchet & Clank: All 4 One | online co-op for up to four players | does not work yet |
| Ratchet & Clank: Full Frontal Assault | online multiplayer | does not work yet |
| Ratchet: Deadlocked HD | online multiplayer | does not work yet |

The console's own online services for these games are closed, so a replacement server is needed. This table lists the
games whose online modes the port plans to cover first; other games of the catalogue are not covered here.

## re-psn

[re-psn](https://github.com/undecuple/re-psn) is an open-source replacement for the PlayStation Network services that
games need to find each other and play together: sign-in, matching and the paths between players. It has a server and a
desktop app; its own README and docs folder explain how to run a server and how a game connects to it. This port plans
to use it as its online method.

### Quick start (for when a game connects)

1. Get a re-psn server address: run your own server from the re-psn repository, or use one a friend runs.
2. Put the address and an online name in the shared settings file (below), or in the launcher's Online settings once
   they are there. The name is 3 to 16 letters, digits, `-` or `_`; one player has one name on a server.
3. Switch re-psn on. It is off until you do.

Until a game's connection to re-psn is finished, switching it on changes nothing in that game. This page will say which
game is the first to work, with the date and how it was checked, when one does.

## The rules

- **Off by default.** Nothing connects unless you choose it.
- **One method at a time.** Only one online method is active; if a second one were also switched on, the game would
  refuse to start online play and say why, instead of picking one.
- **All or nothing.** When re-psn is on, all of a game's online traffic goes to the re-psn server you named, not to
  any other address.
- **No account, no keys.** The port needs no PlayStation Network account. It ships no server address and no game files.
- Your online name and the server address are stored only in your settings file on your PC.

## Settings file

Player-wide values go in one shared file for every game: `repsn.ini`, in the folder `PS3Recomp` of your Windows
profile's application data, that is `%APPDATA%\PS3Recomp\repsn.ini`. (A portable install keeps it in a `shared` folder
beside the program; on Linux it is `$XDG_CONFIG_HOME/ps3recomp/`, by default `.config/ps3recomp/` in the home folder.) It is plain text; the
launcher and an in-game menu write the same file, and a hand edit works.

| Section | Key | Meaning |
|---|---|---|
| `[repsn]` | `enabled` | `1` to use re-psn, `0` (the default) for off |
| `[repsn]` | `server` | the server's `host` or `host:port`; the default port is 36100 |
| `[repsn]` | `name` | your online name: 3 to 16 letters, digits, `-` or `_` |

Per-game values (the title's network communication ID, the game's peer-to-peer port, whether it reports round-trip
times) belong to the game: each game keeps them in its own `repsn.ini` in its data folder and you do not set them. A
game reads the shared file first, then its own; a later key wins.

**Today no game reads this file yet.** The layout is what the port is built towards; this page is corrected when a game
does read it, and the keys above change here first if they change.

## The launcher's Online settings

The launcher has an Online page that writes the same shared file: switch re-psn on or off, enter the server and your
online name. It checks the name and refuses a value the file cannot hold. It makes no connection itself; it only edits
the file.

## What the port does not do

- It does not connect to any server before you switch a method on.
- It does not collect or send telemetry, whatever the online method.
- It does not name or recommend any other server.

See also [What goes online](../README.md#settings-and-known-issues) in the README and the plan in the
[ROADMAP](ROADMAP.md).
