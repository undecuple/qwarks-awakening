// Library model: what library.json says (schema 1, written by tools/release/catalog.py) plus a little runtime state.
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include "json.h"
#include <SDL3/SDL.h>

enum class State { NotAdded, WrongVersion, NotPorted, ReadyToBuild, Building, Built, PortUpdated, NeedsAttention };

inline State parseState(const std::string& s) {
    if (s == "wrong_version") return State::WrongVersion;
    if (s == "not_ported") return State::NotPorted;
    if (s == "ready_to_build") return State::ReadyToBuild;
    if (s == "building") return State::Building;
    if (s == "built") return State::Built;
    if (s == "port_updated") return State::PortUpdated;
    if (s == "needs_attention") return State::NeedsAttention;
    return State::NotAdded;
}

struct ArtTex {  // loaded from the library entry's art paths (the player's own files); never fetched, never shipped
    SDL_Texture* colour = nullptr;
    SDL_Texture* grey = nullptr;  // desaturated and dimmed on the CPU at load
    int w = 0, h = 0;
    bool tried = false;
};

struct Game {
    std::string id, title, readme, reason, titleId, version, needsUpdate;
    int year = 0, order = 0;
    bool supported = false;
    State state = State::NotAdded;
    std::string disc, update, eboot, saves; // files (empty = none recorded)
    std::map<std::string, std::string> overrides, sources, suggested;   // per slot (disc|update|eboot|saves), from the engine
    std::string artIcon, artBackground;     // paths (empty = none)
    long long igdbGame = 0; std::string igdbCover, igdbArtwork;   // parsed and kept; nothing is fetched yet
    bool hasBuild = false;
    std::string exe, builtPortVersion, exeSha, builtAt;
    bool hasEstimate = false; int estMinutes = 0; double estGb = 0;
    std::vector<Game> children;
    // runtime
    float colour = 0, colourTarget = 0;     // 0 = desaturated "not added" look, 1 = full colour
    ArtTex art;
    bool buildTemp = false;                 // TEMP: set by the fake build
};

struct Locations {   // library.json "locations"; empty/absent = default (the engine owns the file, the launcher only reads it)
    std::vector<std::string> libraryFolders, excluded, rpcs3Folders;
    bool rpcs3Auto = true;
    std::string build, saves, logs, artCache;
};

struct Library {
    std::string portVersion, generated, error;
    Locations loc;
    std::vector<Game> games;
};

inline Game parseGame(const Json& j) {
    Game g;
    g.id = j["id"].str(); g.title = j["title"].str(); g.year = j["year"].num(); g.order = j["order"].num();
    g.readme = j["readme_state"].str(); g.supported = j["supported"].boolean();
    g.state = parseState(j["state"].str()); g.reason = j["reason"].str();
    g.titleId = j["title_id"].str(); g.version = j["version"].str(); g.needsUpdate = j["needs_update"].str();
    g.disc = j["files"]["disc"].str(); g.update = j["files"]["update"].str(); g.eboot = j["files"]["eboot"].str();
    g.saves = j["files"]["saves"].str();
    for (const char* slot : {"disc", "update", "eboot", "saves"}) {
        if (!j["overrides"][slot].isNull()) g.overrides[slot] = j["overrides"][slot].str();
        if (!j["sources"][slot].isNull()) g.sources[slot] = j["sources"][slot].str();
        if (!j["suggested"][slot].isNull()) g.suggested[slot] = j["suggested"][slot].str();
    }
    g.artIcon = j["art"]["icon"].str(); g.artBackground = j["art"]["background"].str();
    g.igdbGame = (long long)j["igdb"]["game"].dbl(0); g.igdbCover = j["igdb"]["cover"].str(); g.igdbArtwork = j["igdb"]["artwork"].str();
    if (!j["build"].isNull()) {
        g.hasBuild = true; g.exe = j["build"]["exe"].str(); g.builtPortVersion = j["build"]["port_version"].str();
        g.exeSha = j["build"]["exe_sha256"].str(); g.builtAt = j["build"]["built_at"].str();
    }
    if (!j["estimate"].isNull()) {
        g.hasEstimate = true; g.estMinutes = j["estimate"]["minutes"].num(); g.estGb = j["estimate"]["disk_gb"].dbl();
    }
    for (auto& c : j["children"].a) g.children.push_back(parseGame(c));
    std::sort(g.children.begin(), g.children.end(), [](const Game& a, const Game& b) { return a.order < b.order; });
    return g;
}

inline Library loadLibrary(const std::string& path) {
    Library lib;
    size_t n = 0;
    char* data = (char*)SDL_LoadFile(path.c_str(), &n);
    if (!data) { lib.error = "Could not read " + path; return lib; }
    std::string text(data, n); SDL_free(data);
    Json root; std::string err;
    if (!Json::parse(text, root, err)) { lib.error = "library.json is not valid JSON (" + err + ")"; return lib; }
    lib.portVersion = root["port_version"].str(); lib.generated = root["generated"].str();
    const Json& l = root["locations"];
    for (auto& x : l["library_folders"].a) lib.loc.libraryFolders.push_back(x.str());
    for (auto& x : l["excluded"].a) lib.loc.excluded.push_back(x.str());
    for (auto& x : l["rpcs3_folders"].a) lib.loc.rpcs3Folders.push_back(x.str());
    lib.loc.rpcs3Auto = l["rpcs3_auto"].boolean(true);
    lib.loc.build = l["build"].str(); lib.loc.saves = l["saves"].str(); lib.loc.logs = l["logs"].str(); lib.loc.artCache = l["art_cache"].str();
    for (auto& g : root["games"].a) lib.games.push_back(parseGame(g));
    std::stable_sort(lib.games.begin(), lib.games.end(), [](const Game& a, const Game& b) { return a.order < b.order; });
    if (lib.games.empty()) lib.error = "library.json lists no games";
    return lib;
}
