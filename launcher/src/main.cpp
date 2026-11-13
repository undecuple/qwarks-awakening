// Qwark's Awakening launcher, phase 3 MVP (UI only). Four screens: Library, Game details, Add game (drawer), Settings.
// SDL3 + Dear ImGui (SDL3 + SDLRenderer3 backends). No game art is embedded or fetched: tiles are drawn procedurally
// unless the library entry points at the player's own PNG files.
// Build and Play run the setup engine (tools/release/catalog.py build / play, which runs firstrun.py) as a child
// process, one at a time; its LOG / PROGRESS / REASON / RESULT / STARTED / EXITED lines drive the progress bar, the
// stage words and the Details page's log. TEMP still marks the stubs left (Open saves, Remove build, the drawer's checks).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "model.h"
#include "theme_tokens.h"

// Launcher phase 4 (CMakeLists.txt, from kit.json): the build kit this launcher accepts and the GitHub addresses it may
// reach, each only after the player's click or opt-in (public/README.md "What goes online"). Empty = a developer build.
#ifndef LAUNCHER_KIT_SHA256
#define LAUNCHER_KIT_SHA256 ""
#endif
#ifndef LAUNCHER_KIT_URL
#define LAUNCHER_KIT_URL ""
#endif
#ifndef LAUNCHER_KIT_RELEASE
#define LAUNCHER_KIT_RELEASE ""
#endif
#ifndef LAUNCHER_KIT_BYTES
#define LAUNCHER_KIT_BYTES 0LL
#endif
#ifndef LAUNCHER_KIT_UNPACKED
#define LAUNCHER_KIT_UNPACKED 0LL
#endif
#ifndef LAUNCHER_REPO_API
#define LAUNCHER_REPO_API ""
#endif
#ifndef LAUNCHER_SYMBOLS_URL
#define LAUNCHER_SYMBOLS_URL ""
#endif

// ---------------------------------------------------------------------------------------------------------------
// Colours: the token table in theme_tokens.h, generated from data/theme.json by tools/release/theme_tokens.py
// (libadwaita's light and dark named colours, accent #1e70b9). T points at the set in use (Settings > Appearance:
// System / Light / Dark; System follows SDL_GetSystemTheme() and SDL_EVENT_SYSTEM_THEME_CHANGED).
static const theme::Tokens* T = &theme::kDark;
static ImU32 Col(unsigned rgba, float a = 1.f) {   // token 0xRRGGBBAA, its alpha times `a`
    return IM_COL32((rgba >> 24) & 255, (rgba >> 16) & 255, (rgba >> 8) & 255, (int)((rgba & 255) * a + 0.5f));
}
static ImVec4 ColV(unsigned rgba, float a = 1.f) { return ImGui::ColorConvertU32ToFloat4(Col(rgba, a)); }
// The procedural tiles are game art (keeps game art on the tiles): their own fixed colours, 0xRRGGBB.
static ImU32 Art(unsigned rgb, float a = 1.f) { return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (int)(a * 255.f)); }
enum : unsigned { artGold = 0xffd65c, artStar = 0xe4ebf4, artSteel = 0x96bade, artOrange = 0xff9a1e };
// Colour looks: `colour` 0 = desaturated and dimmed, 1 = full palette colour.
static ImU32 Look(ImU32 c, float colour) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    float l = v.x * 0.3f + v.y * 0.59f + v.z * 0.11f, dim = 0.55f + 0.45f * colour;
    return ImGui::ColorConvertFloat4ToU32(ImVec4((l + (v.x - l) * colour) * dim, (l + (v.y - l) * colour) * dim, (l + (v.z - l) * colour) * dim, v.w));
}

// ---------------------------------------------------------------------------------------------------------------
struct Toast { std::string text; double until = 0; };
struct Dialog { std::mutex m; int field = -1; std::string tag, path; bool has = false; };   // tag != "" = an engine action ("ov|game|slot", "loc|key|mode")
// The engine (tools/release/catalog.py) is the only writer of library.json: one command at a time, off the UI thread.
struct Engine {
    std::atomic<bool> busy{false}, done{false};
    std::mutex m; std::string key, output; int rc = 0;     // key = the row that asked ("g|id|slot" or "l|key"); written by the thread, read after done
};
// The running build or game (the engine's `build` / `play`, streamed line by line on a worker thread). One at a time.
struct Task {
    std::mutex m;                            // guards everything below except the atomics
    std::atomic<bool> running{false}, finished{false};
    std::string kind, game, title;           // kind "build" | "play"; game = the id asked for (a Collection builds its games in turn)
    std::string step, curGame, logPath;      // PROGRESS name=, GAME id= (Collection), LOG path=
    std::vector<std::string> lines, reasons; // the output without PROGRESS lines (capped), and the REASON lines
    float pct = 0.f;
    int result = -1;                         // RESULT ok=0|1, -1 = none yet
    int exitCode = 0; bool exited = false;   // play: EXITED code=
    int secs = 0, procRc = 0; bool cancelled = false, stopAsked = false;
    bool replay = false;                     // screenshots: lines fed from a file, no process
};
// Display and control settings as the engine's get-settings reports them (values as written in the files).
struct Settings {
    bool loaded = false, loading = false; std::string err, sharedFolder;
    std::map<std::string, std::string> defaults, shared;                   // "display.mode" -> "borderless"
    std::map<std::string, std::map<std::string, std::string>> games;       // game id -> its own keys only
};
// The shared online settings (catalog.py get-online / set-online, docs/ONLINE.md): one method at a time.
struct OnlinePreset { std::string name, address, player; };
struct Online {
    bool loaded = false, loading = false; std::string err, folder, method = "off", server, name, active;
    std::vector<OnlinePreset> presets;
    char serverBuf[256] = "", nameBuf[32] = "", pNameBuf[40] = "", pAddrBuf[256] = "";
};
// kit.py jobs (one at a time, off the UI thread): kit status / verify, launcher symbols, the release check. Only
// status and verify stay on this PC; the other two connect, and only after a click or the opt-in switch.
struct Job {
    std::atomic<bool> busy{false}, done{false};
    std::mutex m; std::string key, output; int rc = 0;
};
struct KitInfo {
    std::string state = "unknown", release;   // kit.py status: ready | missing | other | damaged | none (developer build)
    std::string root, url, api, symbolsUrl;   // install root (the exe's folder), and the addresses (compiled in; tests override)
    bool allowLocal = false;                  // --kit-allow-local: file:// and 127.0.0.1 for tests (kit.py --allow-local)
    bool open = false;                        // the "Download the build kit?" dialog
    std::string pendingGame;                  // the Build that asked for the kit: started once the kit is in place
};

struct App {
    SDL_Window* win = nullptr; SDL_Renderer* ren = nullptr;
    Library lib; std::string libPath, fontDir;
    float scale = 1.f;
    // fonts: Adwaita Sans Regular / SemiBold / Bold, Adwaita Mono for paths and versions, Russo One for the About wordmark
    ImFont *fBody = nullptr, *fBodySemi = nullptr, *fHead = nullptr, *fHeadBold = nullptr, *fMono = nullptr, *fMark = nullptr;
    std::string themePref = "system";       // "system", "light", "dark": library.json prefs.theme (engine set-pref), or --theme
    enum Screen { ScLibrary, ScDetails, ScSettings, ScFiles, ScAbout } screen = ScLibrary;
    // Navigation split view: sidebar of pages beside the content; below kCollapseW (logical px) one column, where
    // `showSidebar` = the sidebar is the page on screen (the content's Back leads there).
    bool collapsed = false, showSidebar = false;
    enum Focus { FocusNone, FocusSidebar, FocusContent, FocusBack } focusReq = FocusContent;   // move keyboard focus on the next frame
    int focusFrame = -1;                     // the frame the request was made in: items of a later frame take it
    bool focusShow = false;                  // ...and show the focus ring (the request came from a key or the pad)
    bool inBody = false;                     // drawing the content pane (focus requests for the content land on its first item)
    std::vector<ImGuiID> sidebarIds;         // this frame's sidebar items: is keyboard focus in the sidebar?
    std::map<ImGuiID, std::string> navLabels;   // this frame's nav items by id (screenshots print where focus is)
    int filter = 0;       // 0 All, 1 Ready, 2 Not added
    int sel = 0;
    bool addOpen = false, addJustOpened = false;
    char discBuf[512] = "", updBuf[512] = "", buildBuf[512] = "";
    // settings (the cover art switch is TEMP: kept in memory only; locations live in library.json)
    char buildFolder[512] = ""; bool artFetch = false;
    Toast toast; Dialog dlg; Task task; Engine eng;
    Settings set; std::string dataDir;       // display/control settings (engine get-settings); --data DIR for the engine
    Online online; std::string sharedDir;    // shared online settings; --shared-dir DIR for the engine (tests, screenshots)
    Job kitJob; KitInfo kit;
    std::map<std::string, std::pair<std::string, unsigned>> kitMsg;   // a job's last answer, by its key (row message, colour)
    std::string newRelease, newReleaseUrl;   // the release check found a newer release
    std::vector<std::string> engineCmd;      // argv prefix of the engine command; the verb and its arguments are appended
    std::string busyKey;                     // the row the running engine command belongs to
    std::string errKey, errText;             // last engine failure, shown on the row that asked
    float shotScroll = -1.f;                 // screenshots: scroll the body to this many px (scaled)
    bool shotMode = false; bool firstFrame = true;
    bool liveShot = false; int liveWait = 0;  // --live SECONDS: a shot whose --keys may start a real build / game; the shot waits for it (end-to-end checks)
    std::map<SDL_JoystickID, SDL_Gamepad*> pads;
    float S(float v) const { return v * scale; }
};
static App A;
static const char* kStages[5] = {"Checking files", "Preparing game files", "Recompiling", "Building", "Finishing"};
// firstrun.py's step names -> the five stage words the player sees
static int StageOf(const std::string& step) {
    if (step.empty() || step == "check" || step == "toolkit") return 0;
    if (step == "extract") return 1;
    if (step == "load" || step == "ppu-lift" || step == "spu-lift") return 2;
    if (step == "configure" || step == "compile") return 3;
    return 4;
}

static Game* Sel() { return A.sel >= 0 && A.sel < (int)A.lib.games.size() ? &A.lib.games[A.sel] : nullptr; }
static void Say(const std::string& t) { A.toast.text = t; A.toast.until = ImGui::GetTime() + 4.0; }
static int focusAge = 0;   // frames a focus request has waited; a request no item took is dropped after one frame
static void RequestFocus(App::Focus f) {
    A.focusReq = f; focusAge = 0; A.focusFrame = ImGui::GetFrameCount();
    A.focusShow = ImGui::GetCurrentContext()->NavCursorVisible;   // a key or the pad asked: keep the ring visible
}
static void GoPage(App::Screen sc) {
    A.screen = sc; A.showSidebar = false;
    RequestFocus(App::FocusContent);
}
static bool Playable(const Game& g) { return g.state == State::Built || g.state == State::PortUpdated; }
static bool InReady(const Game& g) { return g.state == State::ReadyToBuild || g.state == State::Building || Playable(g); }

static const char* PlayWord(const std::string& p, unsigned& col);
static std::string PlayDisclaimer(const Game& g);
// the engine's "Update available: rebuild (what changed)" as a line under the state word, which already says it.
static std::string ChangedWhy(const Game& g) {
    const std::string p = "Update available: rebuild (";
    if (g.reason.compare(0, p.size(), p) == 0 && g.reason.back() == ')')
        return "Changed since this build: " + g.reason.substr(p.size(), g.reason.size() - p.size() - 1) + ".";
    return g.reason;
}
static const char* StateWord(const Game& g, std::string& tmp) {
    switch (g.state) {
        case State::NotAdded: return "Not added";
        case State::WrongVersion: tmp = g.needsUpdate.empty() ? "Needs update" : "Needs update " + g.needsUpdate; return tmp.c_str();
        case State::NotPorted: return "Not ported yet";
        case State::ReadyToBuild: return "Ready to build";
        case State::Building: return "Building";
        case State::Built: return "Ready";
        case State::PortUpdated: return "Update available: rebuild";   // only when this game's build inputs changed
        case State::NeedsAttention: return "Needs attention";
    }
    return "";
}
static const char* PrimaryLabel(const Game& g) {
    switch (g.state) {
        case State::NotAdded: return "Add game";
        case State::WrongVersion: return "Locate update";
        case State::NotPorted: return nullptr;
        case State::ReadyToBuild: return "Build";
        case State::Building: return "Building";
        case State::Built: return "Play";
        case State::PortUpdated: return "Rebuild";
        case State::NeedsAttention: return "Change game files";
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// Art: the player's own PNG files only (SDL_LoadPNG). Desaturated + dimmed copy made on the CPU.
static void LoadArt(Game& g) {
    if (g.art.tried) return;
    g.art.tried = true;
    const std::string& p = !g.artBackground.empty() ? g.artBackground : g.artIcon;
    if (p.empty()) return;
    SDL_Surface* s = SDL_LoadPNG(p.c_str());
    if (!s) return;
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!c) return;
    g.art.colour = SDL_CreateTextureFromSurface(A.ren, c);
    for (int y = 0; y < c->h; y++) {
        unsigned char* row = (unsigned char*)c->pixels + y * c->pitch;
        for (int x = 0; x < c->w; x++) {
            unsigned char* px = row + x * 4;
            float l = (px[0] * 0.3f + px[1] * 0.59f + px[2] * 0.11f) * 0.55f;
            px[0] = px[1] = px[2] = (unsigned char)l;
        }
    }
    g.art.grey = SDL_CreateTextureFromSurface(A.ren, c);
    g.art.w = c->w; g.art.h = c->h;
    SDL_DestroySurface(c);
}

static unsigned Hash(unsigned x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
static void Stars(ImDrawList* dl, ImVec2 a, ImVec2 b, unsigned seed, int n, float colour, float alpha) {
    for (int i = 0; i < n; i++) {
        unsigned h = Hash(seed * 977u + i * 131u);
        float x = a.x + (b.x - a.x) * ((h & 1023) / 1023.f), y = a.y + (b.y - a.y) * (((h >> 10) & 1023) / 1023.f);
        float r = ((h >> 20) & 3) == 0 ? A.S(1.6f) : A.S(0.9f), al = alpha * (0.35f + 0.65f * (((h >> 22) & 255) / 255.f));
        dl->AddCircleFilled(ImVec2(x, y), r, Look(Art(i % 5 == 0 ? artGold : artStar, al), colour), 6);
    }
}
static void SplitTitle(const std::string& t, std::string& series, std::string& name) {
    size_t c = t.find(": ");
    if (c != std::string::npos) { series = t.substr(0, c); name = t.substr(c + 2); return; }
    const std::string pre = "Ratchet & Clank ";
    if (t.compare(0, pre.size(), pre) == 0) { series = "Ratchet & Clank"; name = t.substr(pre.size()); return; }
    series = ""; name = t;
}
// libadwaita cards have 12 px corners: the tile is drawn square, then each corner outside the rounding is filled with
// `bg` (the opaque colour behind it): a fan from the corner point over the quarter arc.
static void TileCorners(ImDrawList* dl, ImVec2 a, ImVec2 b, unsigned bg) {
    float r = A.S(theme::kRadiusCard);
    const struct { float x, y, cx, cy, a0; } k[4] = {{a.x, a.y, a.x + r, a.y + r, IM_PI}, {b.x, a.y, b.x - r, a.y + r, IM_PI * 1.5f},
                                                    {b.x, b.y, b.x - r, b.y - r, 0.f}, {a.x, b.y, a.x + r, b.y - r, IM_PI * 0.5f}};
    for (auto& c : k) {
        dl->PathLineTo(ImVec2(c.x, c.y));
        dl->PathArcTo(ImVec2(c.cx, c.cy), r, c.a0, c.a0 + IM_PI * 0.5f, 8);
        dl->PathFillConvex(Col(bg));
    }
}
// `top` composited over the opaque `under` (both 0xRRGGBBAA): a translucent token as the opaque colour it shows.
static unsigned Opaque(unsigned top, unsigned under) {
    float a = (top & 255) / 255.f; unsigned out = 255;
    for (int sh = 24; sh >= 8; sh -= 8) out |= (unsigned)(((top >> sh) & 255) * a + ((under >> sh) & 255) * (1 - a) + 0.5f) << sh;
    return out;
}
// One tile: the player's art when the entry has it, otherwise our own drawing (palette gradient, planet, stars, title).
static void DrawTile(ImDrawList* dl, ImVec2 p0, ImVec2 p1, Game& g, float brighten, float titleScale) {
    float W = p1.x - p0.x, H = p1.y - p0.y, col = g.colour;
    LoadArt(g);
    dl->PushClipRect(p0, p1, true);
    if (g.art.grey) {
        // cover-crop the landscape art into the portrait tile
        float ta = W / H, ia = (float)g.art.w / g.art.h, u0 = 0, u1 = 1, v0 = 0, v1 = 1;
        if (ia > ta) { float k = ta / ia; u0 = 0.5f - k / 2; u1 = 0.5f + k / 2; } else { float k = ia / ta; v0 = 0.5f - k / 2; v1 = 0.5f + k / 2; }
        dl->AddImage((ImTextureID)(intptr_t)g.art.grey, p0, p1, ImVec2(u0, v0), ImVec2(u1, v1));
        if (col > 0.01f) dl->AddImage((ImTextureID)(intptr_t)g.art.colour, p0, p1, ImVec2(u0, v0), ImVec2(u1, v1), IM_COL32(255, 255, 255, (int)(255 * col)));
    } else {
        static const unsigned tops[4] = {0x1d3f86, 0x6a3510, 0x1d5f80, 0x35307a}, bots[4] = {0x08102a, 0x14080a, 0x07161f, 0x0a0a1e};
        int v = (g.order + 3) % 4;
        ImU32 t = Look(Art(tops[v]), col), b = Look(Art(bots[v]), col);
        dl->AddRectFilledMultiColor(p0, p1, t, t, b, b);
        ImVec2 c(p0.x + W * 0.5f, p0.y + H * 0.36f);
        dl->AddCircleFilled(c, W * 0.30f, Look(Art(artGold, 0.10f), col), 48);
        dl->AddCircle(c, W * 0.30f, Look(Art(artGold, 0.55f), col), 48, A.S(1.5f));
        dl->AddCircle(c, W * 0.40f, Look(Art(artSteel, 0.25f), col), 48, A.S(1.f));
        dl->AddCircleFilled(ImVec2(c.x + W * 0.40f * 0.7f, c.y - W * 0.40f * 0.7f), W * 0.035f, Look(Art(artOrange), col), 12);
        Stars(dl, p0, p1, 17u + g.order, (int)(14 + W / 14), col, 0.8f);
    }
    // title block on a scrim, always full contrast
    std::string series, name; SplitTitle(g.title, series, name);
    float sz = A.S(19.f) * titleScale, ss = A.S(11.5f) * titleScale, wrap = W - A.S(16.f) * titleScale;
    ImVec2 ns = A.fHeadBold->CalcTextSizeA(sz, FLT_MAX, wrap, name.c_str());
    float sh = series.empty() ? 0 : ss + A.S(4.f);
    float top = p1.y - ns.y - sh - A.S(14.f) * titleScale;
    dl->AddRectFilledMultiColor(ImVec2(p0.x, top - A.S(26.f)), p1, 0, 0, IM_COL32(4, 7, 14, 235), IM_COL32(4, 7, 14, 235));
    ImVec2 tp(p0.x + A.S(8.f) * titleScale, top + A.S(2.f));
    if (!series.empty()) { dl->AddText(A.fBodySemi, ss, tp, Col(theme::kDark.accent), series.c_str()); tp.y += sh; }
    dl->AddText(A.fHeadBold, sz, tp, Col(theme::kDark.windowFg), name.c_str(), nullptr, wrap);
    if (brighten > 0) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, (int)(26 * brighten)));
    dl->PopClipRect();
}

// ---------------------------------------------------------------------------------------------------------------
// State icons: our own vector drawing, never colour alone (each state has its own shape).
static void StateIcon(ImDrawList* dl, ImVec2 c, float r, State st, ImU32 col) {
    float th = std::max(1.5f, r * 0.17f);
    switch (st) {
        case State::NotAdded: case State::WrongVersion: {   // lock
            dl->PathArcTo(ImVec2(c.x, c.y - r * 0.12f), r * 0.46f, IM_PI, IM_PI * 2.f, 12);
            dl->PathLineTo(ImVec2(c.x + r * 0.46f, c.y + r * 0.12f)); dl->PathLineTo(ImVec2(c.x - r * 0.46f, c.y + r * 0.12f));
            dl->PathStroke(col, 0, th);
            dl->AddRectFilled(ImVec2(c.x - r * 0.72f, c.y - r * 0.05f), ImVec2(c.x + r * 0.72f, c.y + r * 0.9f), col, r * 0.15f);
            dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.42f), r * 0.14f, Col(T->windowBg), 8);
            break; }
        case State::Built:   // check in a disc
            dl->AddCircleFilled(c, r, col, 20);
            dl->AddPolyline((ImVec2[]){ImVec2(c.x - r * 0.45f, c.y + r * 0.02f), ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), ImVec2(c.x + r * 0.5f, c.y - r * 0.32f)}, 3, Col(T->windowBg), 0, th);
            break;
        case State::ReadyToBuild:   // ring with a dot
            dl->AddCircle(c, r * 0.9f, col, 20, th); dl->AddCircleFilled(c, r * 0.32f, col, 12);
            break;
        case State::Building: {   // spinner arc
            float a = (float)ImGui::GetTime() * 4.f;
            dl->PathArcTo(c, r * 0.85f, a, a + IM_PI * 1.5f, 20); dl->PathStroke(col, 0, th * 1.2f);
            break; }
        case State::PortUpdated: {   // circular arrow
            dl->PathArcTo(c, r * 0.8f, 0.5f, 0.5f + IM_PI * 1.6f, 20); dl->PathStroke(col, 0, th);
            ImVec2 e(c.x + r * 0.8f * cosf(0.5f), c.y + r * 0.8f * sinf(0.5f));
            dl->AddTriangleFilled(ImVec2(e.x + r * 0.55f, e.y - r * 0.15f), ImVec2(e.x - r * 0.3f, e.y - r * 0.35f), ImVec2(e.x + r * 0.05f, e.y + r * 0.5f), col);
            break; }
        case State::NeedsAttention:   // triangle with a bar
            dl->AddTriangle(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y + r * 0.8f), ImVec2(c.x - r, c.y + r * 0.8f), col, th);
            dl->AddLine(ImVec2(c.x, c.y - r * 0.25f), ImVec2(c.x, c.y + r * 0.2f), col, th);
            dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.5f), th * 0.7f, col, 6);
            break;
        case State::NotPorted:   // dashed ring
            for (int i = 0; i < 8; i += 2) { float a = i * IM_PI / 4.f; dl->PathArcTo(c, r * 0.85f, a, a + IM_PI / 4.f, 4); dl->PathStroke(col, 0, th); }
            break;
    }
}
static float StateLine(ImDrawList* dl, ImVec2 pos, const Game& g, float fontPx, bool draw = true) {
    std::string tmp; const char* w = StateWord(g, tmp);
    float r = fontPx * 0.5f, gap = fontPx * 0.4f;
    ImVec2 ts = A.fHeadBold->CalcTextSizeA(fontPx, FLT_MAX, 0, w);
    if (draw) {
        ImU32 col = Playable(g) ? Col(T->success) : (g.state == State::NeedsAttention || g.state == State::PortUpdated) ? Col(T->accent) : Col(T->windowFg);
        StateIcon(dl, ImVec2(pos.x + r, pos.y + ts.y * 0.5f), r * 0.86f, g.state, col);
        dl->AddText(A.fHeadBold, fontPx, ImVec2(pos.x + r * 2 + gap, pos.y), Col(T->windowFg), w);
    }
    return r * 2 + gap + ts.x;
}

// ---------------------------------------------------------------------------------------------------------------
// Symbolic icons (decision 3): our own simple vector shapes in a square box of side `s` centred on `c`, one
// colour, no bundled icon theme.
enum class Ico { None, Menu, Back, Check, Folder, Open, Plus, Close, Controller, Image, Style, Info, Warning, Library, Gear };
static void Icon(ImDrawList* dl, ImVec2 c, float s, Ico k, ImU32 col) {
    float h = s * 0.5f, th = std::max(1.5f, s * 0.1f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };
    switch (k) {
        case Ico::None: break;
        case Ico::Menu: for (float y : {-0.55f, 0.f, 0.55f}) dl->AddLine(P(-0.7f, y), P(0.7f, y), col, th); break;
        case Ico::Back: { ImVec2 pts[3] = {P(0.3f, -0.65f), P(-0.35f, 0.f), P(0.3f, 0.65f)}; dl->AddPolyline(pts, 3, col, 0, th * 1.1f); break; }
        case Ico::Check: { ImVec2 pts[3] = {P(-0.65f, 0.05f), P(-0.2f, 0.5f), P(0.7f, -0.5f)}; dl->AddPolyline(pts, 3, col, 0, th * 1.1f); break; }
        case Ico::Folder:
            dl->AddRect(P(-0.8f, -0.4f), P(0.8f, 0.65f), col, h * 0.12f, 0, th);
            dl->AddRectFilled(P(-0.8f, -0.65f), P(-0.1f, -0.4f), col, h * 0.1f, ImDrawFlags_RoundCornersTop);
            break;
        case Ico::Open: {   // a box with an arrow leaving it
            ImVec2 pts[5] = {P(0.05f, -0.7f), P(-0.7f, -0.7f), P(-0.7f, 0.7f), P(0.7f, 0.7f), P(0.7f, -0.05f)};
            dl->AddPolyline(pts, 5, col, 0, th);
            dl->AddLine(P(-0.1f, 0.1f), P(0.75f, -0.75f), col, th);
            dl->AddTriangleFilled(P(0.85f, -0.85f), P(0.25f, -0.85f), P(0.85f, -0.25f), col);
            break; }
        case Ico::Plus: dl->AddLine(P(-0.7f, 0), P(0.7f, 0), col, th); dl->AddLine(P(0, -0.7f), P(0, 0.7f), col, th); break;
        case Ico::Close: dl->AddLine(P(-0.6f, -0.6f), P(0.6f, 0.6f), col, th); dl->AddLine(P(0.6f, -0.6f), P(-0.6f, 0.6f), col, th); break;
        case Ico::Controller:
            dl->AddRect(P(-0.9f, -0.45f), P(0.9f, 0.55f), col, h * 0.45f, 0, th);
            dl->AddLine(P(-0.6f, 0.05f), P(-0.2f, 0.05f), col, th); dl->AddLine(P(-0.4f, -0.15f), P(-0.4f, 0.25f), col, th);
            dl->AddCircleFilled(P(0.35f, -0.05f), h * 0.1f, col, 8); dl->AddCircleFilled(P(0.6f, 0.15f), h * 0.1f, col, 8);
            break;
        case Ico::Image:
            dl->AddRect(P(-0.85f, -0.7f), P(0.85f, 0.7f), col, h * 0.12f, 0, th);
            dl->AddTriangleFilled(P(-0.7f, 0.55f), P(-0.15f, -0.15f), P(0.4f, 0.55f), col);
            dl->AddCircleFilled(P(0.4f, -0.3f), h * 0.15f, col, 10);
            break;
        case Ico::Style:   // half-filled disc: light and dark
            dl->AddCircle(c, h * 0.8f, col, 24, th);
            dl->PathArcTo(c, h * 0.8f, IM_PI * 0.5f, IM_PI * 1.5f, 16); dl->PathFillConvex(col);
            break;
        case Ico::Info:
            dl->AddCircle(c, h * 0.85f, col, 24, th);
            dl->AddLine(P(0, -0.1f), P(0, 0.45f), col, th); dl->AddCircleFilled(P(0, -0.4f), th * 0.75f, col, 8);
            break;
        case Ico::Warning:
            dl->AddTriangle(P(0, -0.85f), P(0.9f, 0.75f), P(-0.9f, 0.75f), col, th);
            dl->AddLine(P(0, -0.3f), P(0, 0.2f), col, th); dl->AddCircleFilled(P(0, 0.48f), th * 0.7f, col, 8);
            break;
        case Ico::Library:
            dl->AddRect(P(-0.8f, -0.7f), P(-0.35f, 0.7f), col, 0, 0, th); dl->AddRect(P(-0.2f, -0.7f), P(0.25f, 0.7f), col, 0, 0, th);
            dl->AddLine(P(0.45f, -0.6f), P(0.85f, 0.65f), col, th * 1.6f);
            break;
        case Ico::Gear:   // a ring with eight teeth
            dl->AddCircle(c, h * 0.5f, col, 24, th * 1.3f);
            for (int i = 0; i < 8; i++) {
                float a = i * IM_PI / 4.f, cs = cosf(a), sn = sinf(a);
                dl->AddLine(ImVec2(c.x + cs * h * 0.55f, c.y + sn * h * 0.55f), ImVec2(c.x + cs * h * 0.85f, c.y + sn * h * 0.85f), col, th * 1.5f);
            }
            break;
    }
}

// ---------------------------------------------------------------------------------------------------------------
static std::string Fit(const std::string& t, float maxW, ImFont* f, float px);
// Widgets: libadwaita's shapes drawn with ImGui. Every interactive one is an ImGui item, so keyboard and
// controller navigation reach it (Enter / A activates); the focus ring is ImGui's nav cursor in the accent colour.
enum class Kind { Normal, Suggested, Flat, Destructive };
static void FocusRing(ImVec2 a, ImVec2 b, float rounding) {
    if (ImGui::IsItemFocused() && ImGui::GetCurrentContext()->NavCursorVisible)
        ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - A.S(3.f), a.y - A.S(3.f)), ImVec2(b.x + A.S(3.f), b.y + A.S(3.f)), Col(T->accent),
                                            rounding + A.S(3.f), 0, A.S(theme::kFocusRing));
}
// The last item takes keyboard focus now (a pending focus request): set directly, scrolled into view, the ring shown
// when a key or the pad asked. (ImGui::FocusItem() goes through a move request, which a just-failed arrow move can drop.)
static bool FocusDue(App::Focus f) { return A.focusReq == f && ImGui::GetFrameCount() > A.focusFrame; }
static void TakeFocus() {
    ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
    ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeY);
    A.focusReq = App::FocusNone;
    if (A.focusShow) ImGui::SetNavCursorVisible(true);
}
// Called right after every interactive item: remembers its label (screenshots print where focus is) and takes a
// pending "focus the content" request (the first enabled item drawn in the content pane gets it).
static void NavItemDone(const char* label) {
    ImGuiID id = ImGui::GetItemID();
    const char* end = ImGui::FindRenderedTextEnd(label);
    A.navLabels[id] = end > label ? std::string(label, end) : std::string(label);
    if (FocusDue(App::FocusContent) && A.inBody && !(ImGui::GetItemFlags() & ImGuiItemFlags_Disabled)) TakeFocus();
}
// A button: `pill` = fully round ends (libadwaita .pill), else 6 px corners. Suggested = accent fill, Destructive = red
// fill, Normal = a faint fill, Flat = no fill until hovered. A label "Text##id" keeps ImGui's id rules.
static bool PillButton(const char* label, Kind k, ImVec2 size = ImVec2(0, 0), bool pill = true, Ico icon = Ico::None, bool on = false) {
    ImFont* f = A.fBodySemi; float px = A.S(17.f);
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImVec2 ts = f->CalcTextSizeA(px, FLT_MAX, 0, label, end);
    float is = icon != Ico::None ? A.S(18.f) : 0.f, gap = (is > 0 && ts.x > 0) ? A.S(8.f) : 0.f;
    float h = size.y > 0 ? size.y : A.S(38.f);
    float w = size.x > 0 ? size.x : ts.x + is + gap + (ts.x > 0 ? A.S(pill ? 32.f : 24.f) : 0.f) + (ts.x > 0 ? 0.f : h - is);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));   // our own ring, shaped like the widget
    bool r = ImGui::InvisibleButton(label, ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    NavItemDone(label);
    bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive(), disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    float rnd = pill ? h * 0.5f : A.S(theme::kRadiusButton);
    unsigned fill = 0, fg = T->windowFg;
    switch (k) {
        case Kind::Suggested: fill = held ? T->accentBgActive : hov ? T->accentBgHover : T->accentBg; fg = T->accentFg; break;
        case Kind::Destructive: fill = held ? T->destructiveBgActive : hov ? T->destructiveBgHover : T->destructiveBg; fg = T->destructiveFg; break;
        case Kind::Normal: fill = held || on ? T->buttonActive : hov ? T->buttonHover : T->buttonBg; break;
        case Kind::Flat: fill = held || on ? T->buttonActive : hov ? T->buttonHover : 0u; break;
    }
    float alpha = disabled ? 0.5f : 1.f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(p.x + w, p.y + h);
    if (fill) dl->AddRectFilled(p, b, Col(fill, alpha), rnd);
    float cx = p.x + (w - (ts.x + is + gap)) * 0.5f;
    if (is > 0) Icon(dl, ImVec2(cx + is * 0.5f, p.y + h * 0.5f), is, icon, Col(fg, alpha));
    if (ts.x > 0) dl->AddText(f, px, ImVec2(cx + is + gap, p.y + (h - ts.y) * 0.5f), Col(fg, alpha), label, end);
    FocusRing(p, b, rnd);
    return r;
}
// The launcher's older call sites: primary = a suggested pill, else a normal button; `active` = a toggled filter.
static bool Btn(const char* label, ImVec2 size, bool primary, bool active = false) {
    return PillButton(label, primary ? Kind::Suggested : Kind::Normal, size, primary, Ico::None, active);
}
// Width of a PillButton with this label (and icon), to reserve a row's control column.
static float BtnW(const char* label, Ico icon = Ico::None, bool pill = false) {
    ImVec2 ts = A.fBodySemi->CalcTextSizeA(A.S(17.f), FLT_MAX, 0, label, ImGui::FindRenderedTextEnd(label));
    float is = icon != Ico::None ? A.S(18.f) : 0.f, gap = (is > 0 && ts.x > 0) ? A.S(8.f) : 0.f;
    return ts.x + is + gap + (ts.x > 0 ? A.S(pill ? 32.f : 24.f) : A.S(38.f) - is);
}
// Switch: libadwaita's pill switch (track 48x26, white knob); a nav item, Enter / A toggles.
static bool Switch(const char* id, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos(); float w = A.S(48.f), h = A.S(26.f);
    ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));   // our own ring, shaped like the widget
    bool r = ImGui::InvisibleButton(id, ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    NavItemDone(id);
    if (r) *v = !*v;
    bool hov = ImGui::IsItemHovered(), disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    float alpha = disabled ? 0.5f : 1.f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(p.x + w, p.y + h);
    dl->AddRectFilled(p, b, Col(*v ? (hov ? T->accentBgHover : T->accentBg) : (hov ? T->buttonActive : T->buttonHover), alpha), h * 0.5f);
    float kr = h * 0.5f - A.S(3.f);
    ImVec2 kc(*v ? b.x - h * 0.5f : p.x + h * 0.5f, p.y + h * 0.5f);
    dl->AddCircleFilled(ImVec2(kc.x, kc.y + A.S(1.f)), kr + A.S(0.5f), IM_COL32(0, 0, 6, (int)(60 * alpha)), 24);
    dl->AddCircleFilled(kc, kr, IM_COL32(255, 255, 255, (int)(255 * alpha)), 24);
    FocusRing(p, b, h * 0.5f);
    return r;
}

// Boxed list: a card (12 px corners, faint outline) holding rows separated by 1 px lines. The card is drawn behind the
// rows once their height is known (a draw-list splitter: channel 0 = card, 1 = rows).
struct BoxedListState { ImVec2 p0; float w = 0; int rows = 0; std::vector<float> seps; ImDrawListSplitter split; };
static BoxedListState gList;
static void BeginBoxedList(float w) {
    gList.p0 = ImGui::GetCursorScreenPos(); gList.w = w; gList.rows = 0; gList.seps.clear();
    gList.split.Split(ImGui::GetWindowDrawList(), 2); gList.split.SetCurrentChannel(ImGui::GetWindowDrawList(), 1);
}
static void EndBoxedList() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p1(gList.p0.x + gList.w, ImGui::GetCursorScreenPos().y);
    gList.split.SetCurrentChannel(dl, 0);
    float rnd = A.S(theme::kRadiusCard);
    dl->AddRectFilled(gList.p0, p1, Col(T->cardBg), rnd);
    dl->AddRect(gList.p0, p1, Col(T->border), rnd, 0, 1.f);
    for (float y : gList.seps) dl->AddLine(ImVec2(gList.p0.x, y), ImVec2(p1.x, y), Col(T->border), 1.f);
    gList.split.Merge(dl);
    ImGui::Dummy(ImVec2(0, A.S(4.f)));
}
// One row: title, optional dim subtitle (wrapped; `mono` = a path or id in Adwaita Mono, kept on one line with its end
// shown), an optional tag after the title (an outlined pill, e.g. where a file came from), and a control at the right
// `ctrlW` wide drawn by `control()` (vertically centred). When the row is too narrow for both, the control goes under
// the text (the collapsed one-column layout). The control reports its own clicks.
struct RowOpt { unsigned subCol = 0; bool mono = false; const char* tag = nullptr; unsigned tagCol = 0; };
template <class F>
static void RowX(const char* title, const std::string& subtitle, float ctrlW, F control, const RowOpt& o) {
    if (gList.rows++ > 0) gList.seps.push_back(ImGui::GetCursorScreenPos().y);
    float padX = A.S(14.f), padY = A.S(10.f), minH = A.S(54.f), gapC = A.S(14.f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool stacked = ctrlW > 0 && gList.w - padX * 2 - ctrlW - gapC < A.S(220.f);
    float textW = gList.w - padX * 2 - (ctrlW > 0 && !stacked ? ctrlW + gapC : 0.f);
    ImFont* sf = o.mono ? A.fMono : A.fBody; float spx = A.S(o.mono ? 14.f : 15.f);
    float tagW = 0, tagPx = A.S(13.f);
    ImVec2 tagTs = o.tag ? A.fBodySemi->CalcTextSizeA(tagPx, FLT_MAX, 0, o.tag) : ImVec2(0, 0);
    if (o.tag) tagW = tagTs.x + A.S(16.f) + A.S(10.f);
    ImVec2 tsz = title && *title ? A.fBody->CalcTextSizeA(A.S(17.f), FLT_MAX, textW - tagW, title) : ImVec2(0, 0);
    std::string sub = o.mono ? Fit(subtitle, textW, sf, spx) : subtitle;   // a path stays on one line, its end kept
    ImVec2 ssz = sub.empty() ? ImVec2(0, 0) : sf->CalcTextSizeA(spx, FLT_MAX, textW, sub.c_str());
    float textH = tsz.y + (ssz.y > 0 ? A.S(2.f) + ssz.y : 0.f);
    float ctrlH = ctrlW > 0 ? A.S(38.f) : 0.f;
    float h = stacked ? textH + A.S(8.f) + ctrlH + padY * 2 : std::max(minH, std::max(textH, ctrlH) + padY * 2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float ty = stacked ? p.y + padY : p.y + (h - textH) * 0.5f;
    if (tsz.y > 0) dl->AddText(A.fBody, A.S(17.f), ImVec2(p.x + padX, ty), Col(T->windowFg), title, nullptr, textW - tagW);
    if (o.tag) {
        float tx = p.x + padX + std::min(tsz.x, textW - tagW) + A.S(10.f), th = tagTs.y + A.S(4.f);
        ImVec2 t0(tx, ty + (A.S(17.f) - th) * 0.5f + A.S(1.f));
        dl->AddRect(t0, ImVec2(t0.x + tagTs.x + A.S(16.f), t0.y + th), Col(T->border), th * 0.5f, 0, 1.f);
        dl->AddText(A.fBodySemi, tagPx, ImVec2(t0.x + A.S(8.f), t0.y + A.S(2.f)), Col(o.tagCol ? o.tagCol : T->dimFg), o.tag);
    }
    if (ssz.y > 0) dl->AddText(sf, spx, ImVec2(p.x + padX, ty + tsz.y + A.S(2.f)), Col(o.subCol ? o.subCol : T->dimFg), sub.c_str(), nullptr, textW);
    if (ctrlW > 0) {
        ImGui::SetCursorScreenPos(stacked ? ImVec2(p.x + padX, ty + textH + A.S(8.f)) : ImVec2(p.x + gList.w - padX - ctrlW, p.y + (h - ctrlH) * 0.5f));
        ImGui::BeginGroup(); control(); ImGui::EndGroup();
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(gList.w, 0));
}
template <class F>
static void Row(const char* title, const std::string& subtitle, float ctrlW, F control, unsigned subCol = 0, bool mono = false) {
    RowOpt o; o.subCol = subCol; o.mono = mono;
    RowX(title, subtitle, ctrlW, control, o);
}
// A key/value row (libadwaita property row): the key as the title, the value as the dim subtitle (Mono for paths and ids).
static void PropRow(const char* key, const std::string& value, bool mono = false, unsigned col = 0) {
    Row(key, value, 0, [] {}, col, mono);
}
// A row whose body is free-form (drawn by `body()` inside the row's padding), e.g. the controller test.
template <class F>
static void BodyRow(F body) {
    if (gList.rows++ > 0) gList.seps.push_back(ImGui::GetCursorScreenPos().y);
    float padX = A.S(14.f), padY = A.S(12.f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x + padX, p.y + padY));
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + gList.w - padX * 2);
    ImGui::BeginGroup(); body(); ImGui::EndGroup();
    ImGui::PopTextWrapPos();
    float y = ImGui::GetItemRectMax().y + padY;
    ImGui::SetCursorScreenPos(ImVec2(p.x, y));
    ImGui::Dummy(ImVec2(gList.w, 0));
}
// A group heading over a boxed list (libadwaita preferences group): bold title, optional dim description.
static void GroupTitle(const char* title, const char* desc, float w) {
    ImGui::Dummy(ImVec2(0, A.S(10.f)));
    ImGui::PushFont(A.fHeadBold, A.S(16.f)); ImGui::TextUnformatted(title); ImGui::PopFont();
    if (desc && *desc) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + w);
        ImGui::PushFont(A.fBody, A.S(15.f)); ImGui::PushStyleColor(ImGuiCol_Text, ColV(T->dimFg)); ImGui::TextWrapped("%s", desc);
        ImGui::PopStyleColor(); ImGui::PopFont(); ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0, A.S(2.f)));
}
// Status page: a big dim icon, a title, one line and an optional button, centred in `w`. Returns the button's click.
static bool StatusPage(Ico icon, const char* title, const char* line, const char* button, float w, unsigned titleCol = 0) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float is = A.S(96.f);
    Icon(dl, ImVec2(p.x + w * 0.5f, p.y + A.S(30.f) + is * 0.5f), is, icon, Col(T->dimFg));
    float y = p.y + A.S(30.f) + is + A.S(26.f);
    ImVec2 ts = A.fHeadBold->CalcTextSizeA(A.S(28.f), FLT_MAX, w - A.S(40.f), title);
    dl->AddText(A.fHeadBold, A.S(28.f), ImVec2(p.x + (w - ts.x) * 0.5f, y), Col(titleCol ? titleCol : T->windowFg), title, nullptr, w - A.S(40.f));
    y += ts.y + A.S(10.f);
    float lw = std::min(w - A.S(40.f), A.S(560.f));
    ImVec2 ls = A.fBody->CalcTextSizeA(A.S(17.f), FLT_MAX, lw, line);
    dl->AddText(A.fBody, A.S(17.f), ImVec2(p.x + (w - std::min(ls.x, lw)) * 0.5f, y), Col(T->windowFg), line, nullptr, lw);
    y += ls.y + A.S(22.f);
    bool r = false;
    if (button) {
        ImVec2 bs = A.fBodySemi->CalcTextSizeA(A.S(17.f), FLT_MAX, 0, button, ImGui::FindRenderedTextEnd(button));
        float bw = bs.x + A.S(48.f);
        ImGui::SetCursorScreenPos(ImVec2(p.x + (w - bw) * 0.5f, y));
        r = PillButton(button, Kind::Suggested, ImVec2(bw, A.S(44.f)));
        y += A.S(44.f);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, y + A.S(10.f)));
    ImGui::Dummy(ImVec2(w, 0));
    return r;
}
// Linked toggle group (libadwaita AdwToggleGroup): one rounded trough in the button fill, the checked segment raised
// (toggle_bg, with a faint shade under it). Each segment is a nav item; Enter / A or a click checks it.
static float ToggleGroupW(const std::vector<std::string>& labels) {
    float t = A.S(3.f) * 2;
    for (auto& l : labels) t += A.fBodySemi->CalcTextSizeA(A.S(16.f), FLT_MAX, 0, l.c_str(), ImGui::FindRenderedTextEnd(l.c_str())).x + A.S(16.f) * 2;
    return t;
}
static bool ToggleGroup(const char* id, const std::vector<std::string>& labels, int* cur) {
    float pad = A.S(3.f), h = A.S(34.f), px = A.S(16.f), segPad = A.S(16.f);
    std::vector<float> ws; float total = pad * 2;
    for (auto& l : labels) { ws.push_back(A.fBodySemi->CalcTextSizeA(px, FLT_MAX, 0, l.c_str(), ImGui::FindRenderedTextEnd(l.c_str())).x + segPad * 2); total += ws.back(); }
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + total, p.y + h + pad * 2), Col(T->buttonBg), A.S(theme::kRadiusButton) + pad);
    bool changed = false; float x = p.x + pad;
    ImGui::PushID(id);
    for (size_t i = 0; i < labels.size(); i++) {
        ImVec2 a(x, p.y + pad), b(x + ws[i], p.y + pad + h);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));
        if (ImGui::InvisibleButton(labels[i].c_str(), ImVec2(ws[i], h), ImGuiButtonFlags_EnableNav)) { *cur = (int)i; changed = true; }
        ImGui::PopStyleColor();
        NavItemDone(labels[i].c_str());
        bool on = *cur == (int)i, hov = ImGui::IsItemHovered();
        float r = A.S(theme::kRadiusButton);
        if (on) { dl->AddRectFilled(ImVec2(a.x, a.y + A.S(1.f)), ImVec2(b.x, b.y + A.S(1.f)), Col(T->shade), r); dl->AddRectFilled(a, b, Col(T->toggleBg), r); }
        else if (hov) dl->AddRectFilled(a, b, Col(T->buttonBg), r);
        const char* s = labels[i].c_str(); const char* e = ImGui::FindRenderedTextEnd(s);
        ImVec2 ts = A.fBodySemi->CalcTextSizeA(px, FLT_MAX, 0, s, e);
        dl->AddText(A.fBodySemi, px, ImVec2(a.x + (ws[i] - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f), Col(T->windowFg), s, e);
        FocusRing(a, b, r);
        x += ws[i];
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + pad * 2));
    ImGui::Dummy(ImVec2(total, 0));
    return changed;
}
// One sidebar row (libadwaita .navigation-sidebar): icon and label, a 6 px rounded fill when it is the page shown.
static bool SidebarItem(const char* label, Ico icon, bool selected, float w) {
    ImVec2 p = ImGui::GetCursorScreenPos(); float h = A.S(40.f);
    ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));
    bool r = ImGui::InvisibleButton((std::string("##sb_") + label).c_str(), ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    A.sidebarIds.push_back(ImGui::GetItemID());
    A.navLabels[ImGui::GetItemID()] = std::string("sidebar ") + label;
    if (selected && FocusDue(App::FocusSidebar)) TakeFocus();
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(p.x + w, p.y + h); float rnd = A.S(theme::kRadiusButton);
    if (selected) dl->AddRectFilled(p, b, Col(T->buttonHover), rnd);
    else if (hov) dl->AddRectFilled(p, b, Col(T->buttonBg), rnd);
    float is = A.S(18.f);
    Icon(dl, ImVec2(p.x + A.S(12.f) + is * 0.5f, p.y + h * 0.5f), is, icon, Col(T->windowFg));
    ImVec2 ts = A.fBody->CalcTextSizeA(A.S(17.f), FLT_MAX, 0, label);
    dl->AddText(A.fBody, A.S(17.f), ImVec2(p.x + A.S(12.f) + is + A.S(12.f), p.y + (h - ts.y) * 0.5f), Col(T->windowFg), label);
    FocusRing(p, b, rnd);
    return r;
}
static void Heading(const char* t, float px = 24.f, unsigned col = T->windowFg) {
    ImGui::PushFont(A.fHead, A.S(px)); ImGui::PushStyleColor(ImGuiCol_Text, ColV(col)); ImGui::TextUnformatted(t); ImGui::PopStyleColor(); ImGui::PopFont();
}
static void Dim(const char* t, float px = 15.f) {
    ImGui::PushFont(A.fBody, A.S(px)); ImGui::PushStyleColor(ImGuiCol_Text, ColV(T->dimFg)); ImGui::TextUnformatted(t); ImGui::PopStyleColor(); ImGui::PopFont();
}
static void Wrapped(const char* t, unsigned col = T->windowFg, float px = 16.f) {
    ImGui::PushFont(A.fBody, A.S(px)); ImGui::PushStyleColor(ImGuiCol_Text, ColV(col)); ImGui::TextWrapped("%s", t); ImGui::PopStyleColor(); ImGui::PopFont();
}
static void Rule(float inset = 0) {
    ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + inset, p.y), ImVec2(p.x + w - inset, p.y), Col(T->border), 1.f);
    ImGui::Dummy(ImVec2(0, A.S(8.f)));
}
static void Panel(ImVec2 p0, ImVec2 p1, unsigned fill = T->cardBg) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, Col(fill), A.S(theme::kRadiusCard)); dl->AddRect(p0, p1, Col(T->border), A.S(theme::kRadiusCard), 0, 1.f);
}
static std::string Esc(const std::string& s) { return s.empty() ? "none recorded" : s; }

// ---------------------------------------------------------------------------------------------------------------
// Add drawer
static void OpenAdd(const Game& g) {
    A.addOpen = true; A.addJustOpened = true;
    snprintf(A.discBuf, sizeof A.discBuf, "%s", g.disc.c_str());
    snprintf(A.updBuf, sizeof A.updBuf, "%s", g.update.c_str());
    std::string bf = std::string(A.buildFolder) + "/" + g.id;
    snprintf(A.buildBuf, sizeof A.buildBuf, "%s", bf.c_str());
}
static std::string FolderOf(const std::string& p) { size_t i = p.find_last_of("/\\"); return i == std::string::npos ? p : p.substr(0, i); }
// Shows the folder in the system file manager (the file's folder when `p` is a file). Spaces and a few characters are escaped in the URL.
static void OpenFolderOf(const std::string& p) {
    SDL_PathInfo pi;
    if (p.empty() || !SDL_GetPathInfo(p.c_str(), &pi)) { Say(p.empty() ? "Nothing to open yet" : "That place does not exist on this PC"); return; }
    std::string dir = pi.type == SDL_PATHTYPE_DIRECTORY ? p : FolderOf(p), url = "file://";
    if (dir.empty() || dir[0] != '/') url += "/";
    for (char ch : dir) {
        if (ch == '\\') url += '/';
        else if (ch == ' ' || ch == '%' || ch == '#' || ch == '?') { char b[4]; snprintf(b, sizeof b, "%%%02X", (unsigned char)ch); url += b; }
        else url += ch;
    }
    if (!SDL_OpenURL(url.c_str())) Say(std::string("Could not open the folder: ") + SDL_GetError());
}

struct DlgUser { int field; std::string tag; };
static void DialogDone(void* ud, const char* const* list, int) {
    DlgUser* u = (DlgUser*)ud;
    if (list && list[0]) { std::lock_guard<std::mutex> l(A.dlg.m); A.dlg.field = u->field; A.dlg.tag = u->tag; A.dlg.path = list[0]; A.dlg.has = true; }
    delete u;
}
static void BrowseFolder(int field, const std::string& tag = "") { SDL_ShowOpenFolderDialog(DialogDone, new DlgUser{field, tag}, A.win, nullptr, false); }
static void BrowseFile(int field, const std::string& tag = "", bool elf = false) {
    static const SDL_DialogFileFilter disc[] = {{"Disc image or package", "iso;pkg;bin"}, {"All files", "*"}};
    static const SDL_DialogFileFilter elfs[] = {{"Decrypted EBOOT", "elf"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(DialogDone, new DlgUser{field, tag}, A.win, elf ? elfs : disc, 2, nullptr, false);
}

// ---------------------------------------------------------------------------------------------------------------
// Engine: --engine "CMD" or $RC_LAUNCHER_ENGINE when given; else the bundled Python of the release zip
// (<exe dir>/python/pythonw.exe <exe dir>/tools/release/catalog.py, see DefaultEngine); else `python3 tools/release/catalog.py`
// from the working folder (a source checkout). The command is split on spaces (double quotes group); every later
// argument is its own argv entry, so paths with spaces never meet a shell.
static std::vector<std::string> SplitCommand(const std::string& c) {
    std::vector<std::string> out; std::string cur; bool q = false, any = false;
    for (char ch : c) {
        if (ch == '"') { q = !q; any = true; }
        else if (ch == ' ' && !q) { if (any) out.push_back(cur); cur.clear(); any = false; }
        else { cur += ch; any = true; }
    }
    if (any) out.push_back(cur);
    return out;
}
// The release zip's Python: paths are built from the exe's own folder exactly as SDL_GetBasePath reports it (its case
// kept, never folded), not from the working folder, so starting the launcher from elsewhere still finds them.
// pythonw.exe first: a GUI-subsystem Python, so no console window flashes up beside the launcher (its output still
// reaches the launcher through the pipe); python.exe if only that one is there.
static std::vector<std::string> DefaultEngine(const std::string& dir) {
#ifdef _WIN32   // the bundled package is a Windows Python: never picked by a Linux build that sits in an unpacked zip
    SDL_PathInfo pi;
    for (const char* exe : {"pythonw.exe", "python.exe"}) {
        std::string py = dir + "python\\" + exe;
        if (!dir.empty() && SDL_GetPathInfo(py.c_str(), &pi) && pi.type == SDL_PATHTYPE_FILE)
            return {py, dir + "tools\\release\\catalog.py"};
    }
#else
    (void)dir;
#endif
    return {"python3", "tools/release/catalog.py"};
}
static int RunCapture(const std::vector<std::string>& argv, std::string& out) {   // the whole output, then the exit code
    std::vector<const char*> a; for (auto& x : argv) a.push_back(x.c_str());
    a.push_back(nullptr);
    int rc = -1;
    SDL_PropertiesID pr = SDL_CreateProperties();
    SDL_SetPointerProperty(pr, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)a.data());
    SDL_SetNumberProperty(pr, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(pr, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(pr, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* p = SDL_CreateProcessWithProperties(pr);
    SDL_DestroyProperties(pr);
    if (!p) out = std::string("Could not start the engine (") + argv[0] + "): " + SDL_GetError();
    else {
        size_t n = 0; int code = 0;
        char* d = (char*)SDL_ReadProcess(p, &n, &code);
        if (d) { out.assign(d, n); SDL_free(d); }
        rc = code; SDL_DestroyProcess(p);
    }
    return rc;
}
static void EngineThread(std::vector<std::string> argv) {
    std::string out; int rc = RunCapture(argv, out);
    { std::lock_guard<std::mutex> l(A.eng.m); A.eng.output = out; A.eng.rc = rc; }
    A.eng.done = true;
}
// kit.py sits beside catalog.py: the engine command with its script swapped (the bundled Python, or --engine's).
static std::vector<std::string> KitCmd() {
    std::vector<std::string> a = A.engineCmd;
    for (auto it = a.rbegin(); it != a.rend(); ++it) {
        size_t p = it->rfind("catalog.py");
        if (p != std::string::npos && p + 10 == it->size()) { it->replace(p, 10, "kit.py"); return a; }
    }
    a.push_back("tools/release/kit.py");   // an --engine without catalog.py: ASSUMPTION it is a Python interpreter
    return a;
}
static void JobThread(std::vector<std::string> argv) {
    std::string out; int rc = RunCapture(argv, out);
    { std::lock_guard<std::mutex> l(A.kitJob.m); A.kitJob.output = out; A.kitJob.rc = rc; }
    A.kitJob.done = true;
}
static bool JobBusy(const std::string& key = "") { return A.kitJob.busy.load() && (key.empty() || A.kitJob.key == key); }
static bool RunJob(const std::string& key, const std::vector<std::string>& args) {
    if (A.kitJob.busy.exchange(true)) { Say("The launcher is still busy with the last check"); return false; }
    A.kitJob.done = false; A.kitJob.key = key; A.kitMsg.erase(key);
    std::vector<std::string> argv = KitCmd();
    argv.insert(argv.end(), args.begin(), args.end());
    if (A.shotMode) { A.kitJob.busy = false; return false; }   // screenshots never start a process
    std::thread(JobThread, argv).detach();
    return true;
}
static std::string LastLine(const std::string& out, const char* prefix) {   // the last "PREFIX ..." line's text
    std::string best; size_t s = 0, n = strlen(prefix);
    while (s < out.size()) {
        size_t e = out.find('\n', s); if (e == std::string::npos) e = out.size();
        std::string ln = out.substr(s, e - s); while (!ln.empty() && ln.back() == '\r') ln.pop_back();
        if (ln.compare(0, n, prefix) == 0) best = ln.substr(n);
        s = e + 1;
    }
    return best;
}
static void KitStatus() {
    if (!*LAUNCHER_KIT_SHA256) { A.kit.state = "none"; return; }
    RunJob("kit|status", {"status", "--root", A.kit.root, "--expect", LAUNCHER_KIT_SHA256});
}
static void CheckRelease(bool quiet) {
    if (A.kit.api.empty()) { A.kitMsg["upd|check"] = {"This launcher was built without a release address.", T->dimFg}; return; }
    std::vector<std::string> a = {"check-release", "--api", A.kit.api, "--current", LAUNCHER_VERSION};
    if (A.kit.allowLocal) a.push_back("--allow-local");
    RunJob(quiet ? "upd|auto" : "upd|check", a);
}
static std::string LauncherExe();
static void GetSymbols() {
    if (A.kit.symbolsUrl.empty()) { A.kitMsg["sym|get"] = {"This launcher was built without a release address.", T->dimFg}; return; }
    std::vector<std::string> a = {"symbols", "--exe", LauncherExe(), "--url", A.kit.symbolsUrl};
    if (A.kit.allowLocal) a.push_back("--allow-local");
    RunJob("sym|get", a);
}
static void ReloadLibrary();
static void StartBuild(const Game& g);
static void PumpJob() {
    if (!A.kitJob.done.exchange(false)) return;
    std::string key, out; int rc;
    { std::lock_guard<std::mutex> l(A.kitJob.m); key = A.kitJob.key; out = A.kitJob.output; rc = A.kitJob.rc; }
    A.kitJob.busy = false;
    std::string reason = LastLine(out, "REASON ");
    Json st; std::string err; bool hasSt = Json::parse(LastLine(out, "STATUS "), st, err) && !st.isNull();
    if (key == "kit|status" && A.lib.updateCheck == "on" && !A.shotMode) {   // the opt-in release check waits for the kit status
        A.kitJob.done = false; CheckRelease(true);
    }
    if (key == "kit|status" || key == "kit|verify") {
        if (hasSt) { A.kit.state = st["state"].str("missing"); A.kit.release = st["release"].str(); }
        else if (key == "kit|status") A.kit.state = "missing";
        if (key == "kit|verify")
            A.kitMsg[key] = rc == 0 ? std::make_pair(std::string("Every file of the build kit matches its list."), T->success)
                                    : std::make_pair(reason.empty() ? std::string("Files of the build kit are missing or changed.") : reason, T->destructive);
        if (A.kit.state == "ready" && A.kit.open && !A.kit.pendingGame.empty() && !A.task.running.load()) {   // the kit was there after all
            A.kit.open = false;
            for (auto& g : A.lib.games) { if (g.id == A.kit.pendingGame) StartBuild(g); for (auto& c : g.children) if (c.id == A.kit.pendingGame) StartBuild(c); }
            A.kit.pendingGame.clear();
        }
    } else if (key == "upd|check" || key == "upd|auto") {
        if (rc == 0 && hasSt) {
            std::string latest = st["latest"].str();
            if (st["newer"].boolean()) {
                A.newRelease = latest; A.newReleaseUrl = st["url"].str();
                A.kitMsg["upd|check"] = {"Release " + latest + " is out (you have " + LAUNCHER_VERSION + "). Download it from the release page.", T->accent};
                if (key == "upd|auto") Say("Release " + latest + " is out: Settings > Updates has the link");
            } else A.kitMsg["upd|check"] = {latest.empty() ? std::string("No release is listed yet.") : "You have the newest release (" + latest + ").", T->success};
        } else if (key == "upd|check") A.kitMsg[key] = {reason.empty() ? std::string("The release list could not be read.") : reason, T->destructive};
    } else if (key == "sym|get") {
        A.kitMsg[key] = rc == 0 ? std::make_pair(std::string("The launcher's debug information is next to it now."), T->success)
                                : std::make_pair(reason.empty() ? std::string("The debug information could not be fetched.") : reason, T->destructive);
    }
}
static bool EngineBusy() { return A.eng.busy.load(); }
static void RunEngine(const std::string& key, const std::vector<std::string>& args) {
    if (A.eng.busy.exchange(true)) { Say("The engine is still working on the last change"); return; }
    A.eng.done = false; A.busyKey = key;
    { std::lock_guard<std::mutex> l(A.eng.m); A.eng.key = key; }
    if (A.errKey == key) { A.errKey.clear(); A.errText.clear(); }
    std::vector<std::string> argv = A.engineCmd;
    argv.insert(argv.end(), args.begin(), args.end());
    if (A.shotMode) { A.eng.busy = false; return; }   // screenshots never start a process
    std::thread(EngineThread, argv).detach();
}
static void EngineOverride(const std::string& game, const std::string& slot, const std::string& path) {
    RunEngine("g|" + game + "|" + slot, {"override", "--library", A.libPath, "--game", game, "--slot", slot, path});
}
static void EngineClearOverride(const std::string& game, const std::string& slot) {
    RunEngine("g|" + game + "|" + slot, {"clear-override", "--library", A.libPath, "--game", game, "--slot", slot});
}
// First start: no library.json yet. The engine scans once (RPCS3's usual folders, read-only; no folder of the player's
// is known yet) and writes it; the library screen shows "Checking..." meanwhile and reloads when it is done.
static const char* kFirstScan = "l|first_scan";
static void EngineFirstScan() { RunEngine(kFirstScan, {"scan", "--library", A.libPath, "--find-rpcs3"}); }
// mode: "" set, "--add", "--remove", "--reset" (value ignored)
static void EngineLocation(const std::string& key, const std::string& value, const std::string& mode = "") {
    std::vector<std::string> a = {"set-location", "--library", A.libPath, key};
    if (mode != "--reset") a.push_back(value);
    if (!mode.empty()) a.push_back(mode);
    RunEngine("l|" + key, a);
}
static bool LitState(const Game& g) { return g.state == State::ReadyToBuild || g.state == State::Building || g.state == State::Built || g.state == State::PortUpdated || g.state == State::NeedsAttention; }
static void SyncBuildFolder() {
    const char* base = SDL_GetBasePath();
    std::string b = !A.lib.loc.build.empty() ? A.lib.loc.build : std::string(base ? base : "") + "games";
    snprintf(A.buildFolder, sizeof A.buildFolder, "%s", b.c_str());
}
// Reloads library.json after the engine rewrote it; runtime state (art) is carried over by game id.
static void ReloadLibrary() {
    Library nl = loadLibrary(A.libPath);
    if (!nl.error.empty()) { Say("Could not reload the library: " + nl.error); return; }
    std::string selId = Sel() ? Sel()->id : "";
    for (auto& g : nl.games) {
        g.colourTarget = LitState(g) ? 1.f : 0.f; g.colour = g.colourTarget;
        for (auto& o : A.lib.games) if (o.id == g.id) {
            g.art = o.art; o.art = ArtTex();
        }
    }
    A.lib = std::move(nl);
    for (size_t i = 0; i < A.lib.games.size(); i++) if (A.lib.games[i].id == selId) A.sel = (int)i;
    SyncBuildFolder();
}
static bool ParseSettings(const std::string& out);
static void LoadSettings();
// Online settings: the engine's get-online JSON. keepInputs = after a change, the text fields keep what was typed.
static bool ParseOnline(const std::string& out, bool keepInputs) {
    size_t b = out.find('{'); if (b == std::string::npos) return false;
    Json j; std::string err;
    if (!Json::parse(out.substr(b), j, err)) return false;
    Online& o = A.online;
    o.loaded = true; o.loading = false; o.err.clear();
    o.folder = j["folder"].str(); o.method = j["method"].str("off");
    o.server = j["repsn"]["server"].str(); o.name = j["repsn"]["name"].str(); o.active = j["community"]["active"].str();
    o.presets.clear();
    for (auto& p : j["community"]["presets"].a) o.presets.push_back({p["name"].str(), p["address"].str(), p["player_name"].str()});
    if (!keepInputs || !*o.serverBuf) snprintf(o.serverBuf, sizeof o.serverBuf, "%s", o.server.c_str());
    if (!keepInputs || !*o.nameBuf) snprintf(o.nameBuf, sizeof o.nameBuf, "%s", o.name.c_str());
    return true;
}
static void OnlineArgs(std::vector<std::string>& a) { if (!A.sharedDir.empty()) { a.push_back("--shared-dir"); a.push_back(A.sharedDir); } }
static void LoadOnline() {
    std::vector<std::string> a = {"get-online"}; OnlineArgs(a);
    A.online.loading = true; RunEngine("o|load", a);
}
static void SetOnline(const std::string& rowKey, std::vector<std::string> args) {
    std::vector<std::string> a = {"set-online"}; OnlineArgs(a);
    a.insert(a.end(), args.begin(), args.end());
    RunEngine(rowKey, a);
}
static void PumpEngine() {
    if (!A.eng.done.exchange(false)) return;
    std::string key, out; int rc;
    { std::lock_guard<std::mutex> l(A.eng.m); key = A.eng.key; out = A.eng.output; rc = A.eng.rc; }
    A.eng.busy = false; A.busyKey.clear();
    bool settings = key.compare(0, 2, "s|") == 0;   // get-settings / set-setting: the reply is the settings, not library.json
    bool online = key.compare(0, 2, "o|") == 0;     // get-online / set-online: the reply is the shared online settings
    if (rc == 0 && settings) {
        if (!ParseSettings(out)) { A.set.loading = false; A.set.err = "The engine's answer could not be read."; }
        return;
    }
    if (rc == 0 && online) {
        if (!ParseOnline(out, key != "o|load")) { A.online.loading = false; A.online.err = "The engine's answer could not be read."; }
        else if (key == "o|add") A.online.pNameBuf[0] = A.online.pAddrBuf[0] = 0;   // added: the fields are free again
        return;
    }
    if (rc == 0) { ReloadLibrary(); return; }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    size_t nl = out.find_last_of('\n');
    A.errKey = key;
    A.errText = out.empty() ? "The engine failed without a message." : (rc == 2 || nl == std::string::npos ? out : out.substr(nl + 1));   // exit 2 = the engine's own message
    if (key == "s|load") { A.set.loading = false; A.set.err = A.errText; }
    else if (settings) LoadSettings();   // a refused change: show the files as they are again
    else if (key == "o|load") { A.online.loading = false; A.online.err = A.errText; }
}

// Dialog results for engine actions: tag "ov|<game>|<slot>" or "loc|<key>|<mode>".
static void HandleTag(const std::string& tag, const std::string& path) {
    size_t a = tag.find('|'), b = tag.find('|', a + 1);
    if (a == std::string::npos || b == std::string::npos) return;
    std::string kind = tag.substr(0, a), x = tag.substr(a + 1, b - a - 1), y = tag.substr(b + 1);
    if (kind == "ov") EngineOverride(x, y, path);
    else if (kind == "loc") EngineLocation(x, path, y);
}
static void PumpDialog() {
    std::string tag, path; int field;
    { std::lock_guard<std::mutex> l(A.dlg.m); if (!A.dlg.has) return; A.dlg.has = false; tag = A.dlg.tag; path = A.dlg.path; field = A.dlg.field; }
    if (!tag.empty()) { HandleTag(tag, path); return; }
    char* dst = field == 0 ? A.discBuf : field == 1 ? A.updBuf : field == 2 ? A.buildBuf : nullptr;
    if (dst) snprintf(dst, 512, "%s", path.c_str());
}
// ---------------------------------------------------------------------------------------------------------------
// Build and Play: the engine's `build --library F --game ID --stop-file S` and `play --library F --game ID`, one at a
// time, streamed line by line. Cancel = the stop file firstrun.py watches (it ends the running tool's process tree and
// leaves that step unmarked, so the next Build resumes there).
static const size_t kTaskLines = 3000;
static std::string StopFile() { return FolderOf(A.libPath) + "/.build.stop"; }
static std::string Field(const std::string& line, const char* key) {   // "k=v" in a space-separated line; the last key may hold spaces (LOG path=)
    std::string k = std::string(key) + "=";
    size_t p = line.find(" " + k); if (p == std::string::npos) { if (line.compare(0, k.size(), k) != 0) return ""; p = 0; } else p += 1;
    p += k.size();
    if (std::string(key) == "path") return line.substr(p);
    size_t e = line.find(' ', p);
    return line.substr(p, e == std::string::npos ? std::string::npos : e - p);
}
static void TaskLine(const std::string& raw) {
    std::string line = raw; while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    Task& t = A.task;
    std::lock_guard<std::mutex> l(t.m);
    if (line.compare(0, 9, "PROGRESS ") == 0) { t.step = Field(line, "name"); t.pct = (float)atof(Field(line, "pct").c_str()); return; }
    if (line.compare(0, 9, "LOG path=") == 0) t.logPath = line.substr(9);
    else if (line.compare(0, 7, "REASON ") == 0) t.reasons.push_back(line.substr(7));
    else if (line.compare(0, 7, "RESULT ") == 0) { t.result = Field(line, "ok") == "1" ? 1 : 0; t.cancelled = Field(line, "step") == "cancelled"; }
    else if (line.compare(0, 5, "GAME ") == 0) { t.curGame = Field(line, "id"); t.step.clear(); t.pct = 0; }
    else if (line.compare(0, 7, "EXITED ") == 0) { t.exited = true; t.exitCode = atoi(Field(line, "code").c_str()); t.secs = atoi(Field(line, "seconds").c_str()); }
    t.lines.push_back(line);
    if (t.lines.size() > kTaskLines) t.lines.erase(t.lines.begin(), t.lines.begin() + (t.lines.size() - kTaskLines));
}
static void TaskThread(std::vector<std::string> argv) {
    std::vector<const char*> a; for (auto& x : argv) a.push_back(x.c_str());
    a.push_back(nullptr);
    SDL_PropertiesID pr = SDL_CreateProperties();
    SDL_SetPointerProperty(pr, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)a.data());
    SDL_SetNumberProperty(pr, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(pr, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(pr, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* p = SDL_CreateProcessWithProperties(pr);
    SDL_DestroyProperties(pr);
    int rc = -1;
    if (!p) TaskLine(std::string("REASON The launcher could not start its setup engine (") + SDL_GetError() + ").");
    else {
        SDL_IOStream* io = SDL_GetProcessOutput(p);
        std::string buf; char chunk[4096];
        while (io) {
            size_t n = SDL_ReadIO(io, chunk, sizeof chunk);
            if (n == 0) { if (SDL_GetIOStatus(io) == SDL_IO_STATUS_NOT_READY) { SDL_Delay(20); continue; } break; }
            buf.append(chunk, n);
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) { TaskLine(buf.substr(0, nl)); buf.erase(0, nl + 1); }
        }
        if (!buf.empty()) TaskLine(buf);
        SDL_WaitProcess(p, true, &rc);
        SDL_DestroyProcess(p);
    }
    { std::lock_guard<std::mutex> l(A.task.m); A.task.procRc = rc; }
    A.task.finished = true;
}
static bool TaskRunning() { return A.task.running.load(); }
static bool BuildingGame(const Game& g) {   // this entry (or a game inside it) is the one being built
    if (!TaskRunning() || A.task.kind != "build") return false;
    if (A.task.game == g.id) return true;
    std::lock_guard<std::mutex> l(A.task.m);
    return !A.task.curGame.empty() && A.task.curGame == g.id;
}
// A release launcher builds only with its build kit in place (kit.py status "ready"); otherwise Build opens the kit
// dialog, which downloads nothing until the player chooses Download. A developer build (no kit hash) builds as before.
static bool KitNeeded() { return *LAUNCHER_KIT_SHA256 && A.kit.state != "ready" && A.kit.state != "none"; }
static void StartTask(const std::string& kind, const Game& g) {
    if (kind == "build" && KitNeeded()) {
        A.kit.open = true; A.kit.pendingGame = g.id;
        if (A.kit.state == "unknown" && !JobBusy()) KitStatus();
        return;
    }
    if (A.task.running.exchange(true)) { Say(A.task.kind == "build" ? "A build is running; one at a time" : A.task.kind == "kit" ? "The build kit is downloading" : "A game is running"); return; }
    {
        std::lock_guard<std::mutex> l(A.task.m);
        A.task.kind = kind; A.task.game = g.id; A.task.title = g.title; A.task.step.clear(); A.task.curGame.clear();
        A.task.logPath.clear(); A.task.lines.clear(); A.task.reasons.clear(); A.task.pct = 0; A.task.result = -1;
        A.task.exited = false; A.task.exitCode = 0; A.task.secs = 0; A.task.procRc = 0; A.task.cancelled = false; A.task.stopAsked = false;
        A.task.replay = false;
    }
    A.task.finished = false;
    std::vector<std::string> argv = A.engineCmd;
    if (kind == "build") {
        for (const char* x : {"build", "--library"}) argv.push_back(x);
        argv.push_back(A.libPath); argv.push_back("--game"); argv.push_back(g.id); argv.push_back("--stop-file"); argv.push_back(StopFile());
        SDL_RemovePath(StopFile().c_str());
    } else if (kind == "kit") {   // the player chose Download in the kit dialog
        argv = KitCmd();
        for (const std::string& x : {std::string("fetch"), std::string("--root"), A.kit.root, std::string("--expect"), std::string(LAUNCHER_KIT_SHA256),
                                     std::string("--url"), A.kit.url, std::string("--stop-file"), StopFile()}) argv.push_back(x);
        if (A.kit.allowLocal) argv.push_back("--allow-local");
        SDL_RemovePath(StopFile().c_str());
    } else {
        for (const char* x : {"play", "--library"}) argv.push_back(x);
        argv.push_back(A.libPath); argv.push_back("--game"); argv.push_back(g.id);
    }
    if (A.shotMode && !A.liveShot) { A.task.running = false; return; }   // screenshots never start a process (unless --live)
    std::thread(TaskThread, argv).detach();
}
static void StartBuild(const Game& g) { StartTask("build", g); }
static void StartPlay(const Game& g) { StartTask("play", g); }
static void CancelBuild() {   // a build or the kit download: both watch the stop file
    if (!TaskRunning() || (A.task.kind != "build" && A.task.kind != "kit")) return;
    SDL_IOStream* f = SDL_IOFromFile(StopFile().c_str(), "w");
    if (f) SDL_CloseIO(f);
    std::lock_guard<std::mutex> l(A.task.m); A.task.stopAsked = true;
}
static void PumpTask() {
    if (A.task.replay || !A.task.finished.exchange(false)) return;
    A.task.running = false;
    std::string kind, title, reason; int result, code; bool exited, cancelled;
    { std::lock_guard<std::mutex> l(A.task.m); kind = A.task.kind; title = A.task.title; result = A.task.result; code = A.task.exitCode;
      exited = A.task.exited; cancelled = A.task.cancelled; reason = A.task.reasons.empty() ? "" : A.task.reasons[0]; }
    if (kind == "kit") {   // the dialog shows a failure; a kit in place starts the Build that asked for it
        if (result == 1) {
            A.kit.state = "ready"; A.kit.release = LAUNCHER_KIT_RELEASE; A.kit.open = false;
            Say("The build kit is in place");
            std::string id = A.kit.pendingGame; A.kit.pendingGame.clear();
            for (auto& g : A.lib.games) { if (g.id == id) StartBuild(g); for (auto& c : g.children) if (c.id == id) StartBuild(c); }
        } else if (cancelled) Say("Download stopped. Nothing was installed.");
        return;
    }
    if (kind == "build") {
        ReloadLibrary();   // the engine recorded the build (or nothing changed)
        if (result == 1) Say(title + " is built");
        else if (cancelled) Say("Build stopped. Build again to continue where it stopped.");
        else Say(reason.empty() ? "The build stopped. The log on the Details page says why." : reason);
    } else {
        if (exited) { char b[160]; snprintf(b, sizeof b, "%s closed (exit code %d)", title.c_str(), code); Say(b); }
        else Say(reason.empty() ? "The game could not be started." : reason);
    }
}
// Screenshots: --replay FILE feeds a recorded engine output into the task as if it streamed (no process); without a
// RESULT / EXITED line the task is shown as still running.
static void ReplayTask(const std::string& file, const Game& g, bool kit = false) {   // kit: a kit download (--screen kit)
    size_t n = 0; char* d = (char*)SDL_LoadFile(file.c_str(), &n);
    if (!d) { fprintf(stderr, "--replay: cannot read %s\n", file.c_str()); return; }
    std::string text(d, n); SDL_free(d);
    bool play = text.find("\nSTARTED ") != std::string::npos || text.compare(0, 8, "STARTED ") == 0;
    StartTask(kit ? "kit" : play ? "play" : "build", g);
    size_t s = 0, e;
    while ((e = text.find('\n', s)) != std::string::npos) { TaskLine(text.substr(s, e - s)); s = e + 1; }
    if (s < text.size()) TaskLine(text.substr(s));
    bool done = A.task.result >= 0 || A.task.exited;
    A.task.replay = true; A.task.running = !done;
}

// TEMP validation: the path exists, or it is the path the library already recorded. The real checks come from the engine.
struct Check { bool ok; std::string text; };
static Check ValidatePath(const char* path, const std::string& recorded, const char* what) {
    if (!*path) return {false, std::string("Choose the ") + what};
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info)) return {true, std::string("Found: ") + (info.type == SDL_PATHTYPE_DIRECTORY ? "a folder" : "a file") + " (TEMP: contents not checked yet)"};
    if (path == recorded) return {true, "Recorded by the library (TEMP: not checked again here)"};
    return {false, "Nothing at that path"};
}
static bool PathRow(const char* id, char* buf, int field, bool folderToo) {
    bool changed = false;
    float bw = A.S(92.f), avail = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(avail - (bw + sp) * (folderToo ? 2.f : 1.f) - A.S(2.f));
    ImGui::PushFont(A.fBody, A.S(16.f));
    changed = ImGui::InputText(id, buf, 512);
    ImGui::PopFont();
    ImGui::SameLine();
    if (Btn((std::string(folderToo ? "Folder" : "Browse") + "##f" + id).c_str(), ImVec2(bw, 0), false)) BrowseFolder(field);
    if (folderToo) { ImGui::SameLine(); if (Btn((std::string("File##g") + id).c_str(), ImVec2(bw, 0), false)) BrowseFile(field); }
    return changed;
}
static void CheckLine(const Check& c) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(); float r = A.S(7.f);
    ImU32 col = c.ok ? Col(T->success) : Col(T->destructive);
    ImVec2 m(p.x + r, p.y + A.S(10.f));
    if (c.ok) dl->AddPolyline((ImVec2[]){ImVec2(m.x - r * .6f, m.y), ImVec2(m.x - r * .1f, m.y + r * .5f), ImVec2(m.x + r * .7f, m.y - r * .5f)}, 3, col, 0, A.S(2.f));
    else { dl->AddLine(ImVec2(m.x - r * .5f, m.y - r * .5f), ImVec2(m.x + r * .5f, m.y + r * .5f), col, A.S(2.f)); dl->AddLine(ImVec2(m.x + r * .5f, m.y - r * .5f), ImVec2(m.x - r * .5f, m.y + r * .5f), col, A.S(2.f)); }
    ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + A.S(6.f), p.y));
    ImGui::PushTextWrapPos(0); Wrapped(c.text.c_str(), c.ok ? T->success : T->destructive, 15.f); ImGui::PopTextWrapPos();
}
static void StepHeader(int n, const char* title, int mode /*0 active 1 done 2 skipped*/) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(); float r = A.S(13.f);
    ImVec2 c(p.x + r, p.y + r + A.S(1.f));
    if (mode == 0) { dl->AddCircleFilled(c, r, Col(T->accentBg), 24); char b[4]; snprintf(b, 4, "%d", n); ImVec2 ts = A.fHeadBold->CalcTextSizeA(A.S(18.f), FLT_MAX, 0, b); dl->AddText(A.fHeadBold, A.S(18.f), ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), Col(T->accentFg), b); }
    else if (mode == 1) { dl->AddCircle(c, r, Col(T->success), 24, A.S(2.f)); dl->AddPolyline((ImVec2[]){ImVec2(c.x - r * .45f, c.y), ImVec2(c.x - r * .1f, c.y + r * .38f), ImVec2(c.x + r * .5f, c.y - r * .32f)}, 3, Col(T->success), 0, A.S(2.f)); }
    else { dl->AddCircle(c, r, Col(T->dimFg, .6f), 24, A.S(1.5f)); dl->AddLine(ImVec2(c.x - r * .4f, c.y), ImVec2(c.x + r * .4f, c.y), Col(T->dimFg, .6f), A.S(1.5f)); }
    ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + A.S(10.f), p.y));
    Heading(title, 22.f, mode == 2 ? T->dimFg : T->windowFg);
    if (mode == 2) { ImGui::SameLine(); Dim("  skipped, already known"); }
    if (mode == 1) { ImGui::SameLine(); Dim("  done"); }
    ImGui::Dummy(ImVec2(0, A.S(4.f)));
}

static void AddDrawer(float W, float H) {
    Game* gp = Sel(); if (!gp) { A.addOpen = false; return; }
    Game& g = *gp;
    if (A.addJustOpened) { ImGui::OpenPopup("add_game"); A.addJustOpened = false; }
    float dw = std::min(A.S(580.f), W * 0.56f);
    ImGui::SetNextWindowPos(ImVec2(W - dw, 0)); ImGui::SetNextWindowSize(ImVec2(dw, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(A.S(24.f), A.S(20.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ColV(T->dialogBg)); ImGui::PushStyleColor(ImGuiCol_Border, ColV(T->border));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ColV(0x00000666u));
    bool open = ImGui::BeginPopupModal("add_game", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor(3); ImGui::PopStyleVar(2);
    if (!open) { A.addOpen = false; return; }

    bool building = BuildingGame(g);
    bool justDone = !building && Playable(g) && A.task.kind == "build" && A.task.game == g.id && A.task.result == 1;
    // header
    ImGui::PushFont(A.fHeadBold, A.S(30.f)); ImGui::TextUnformatted(g.state == State::Built || g.state == State::PortUpdated ? "Rebuild game" : "Add game"); ImGui::PopFont();
    ImGui::SameLine(dw - A.S(48.f) - A.S(24.f) - A.S(40.f));
    if (Btn("Close", ImVec2(A.S(88.f), 0), false) || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive()) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
        ImGui::CloseCurrentPopup(); A.addOpen = false; ImGui::EndPopup(); return;
    }
    if (A.firstFrame || ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
    Heading(g.title.c_str(), 20.f, T->accent);
    Rule();

    bool filesKnown = !g.disc.empty();
    bool needUpd = !g.needsUpdate.empty() || g.state == State::WrongVersion;
    ImGui::BeginChild("steps", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);

    // (1) what this game needs
    StepHeader(1, "What this game needs", filesKnown ? 2 : 0);
    if (!filesKnown) {
        ImGui::Indent(A.S(36.f));
        Wrapped(("The game's disc (a folder or an ISO) or its PSN package" + std::string(g.titleId.empty() ? "" : " (title ID " + g.titleId + ")") + "."  ).c_str(), T->windowFg, 16.f);
        if (needUpd) Wrapped(("The update " + g.needsUpdate + " (a PKG file).").c_str(), T->windowFg, 16.f);
        Wrapped("A decrypted EBOOT.elf: the build makes it from your files, you do not supply it.", T->dimFg, 15.f);
        ImGui::Unindent(A.S(36.f));
    }
    ImGui::Dummy(ImVec2(0, A.S(6.f)));

    // (2) choose files
    StepHeader(2, "Choose your files", (building || justDone) ? 1 : 0);
    ImGui::Indent(A.S(36.f));
    Check c1 = ValidatePath(A.discBuf, g.disc, "game files"), c2{true, ""};
    Dim("Game files (folder, ISO or PKG)");
    PathRow("##disc", A.discBuf, 0, true);
    CheckLine(c1);
    if (needUpd) {
        ImGui::Dummy(ImVec2(0, A.S(4.f)));
        Dim(("Update " + g.needsUpdate + " (PKG)").c_str());
        PathRow("##upd", A.updBuf, 1, false);
        c2 = ValidatePath(A.updBuf, g.update, "update file"); CheckLine(c2);
    }
    ImGui::Unindent(A.S(36.f));
    ImGui::Dummy(ImVec2(0, A.S(6.f)));

    // (3) where it goes
    StepHeader(3, "Where it goes", (building || justDone) ? 1 : 0);
    ImGui::Indent(A.S(36.f));
    Dim("Build folder");
    PathRow("##bld", A.buildBuf, 2, false);
    char est[160];
    if (g.hasEstimate) snprintf(est, sizeof est, "Needs about %.1f GB of space and about %d minutes.", g.estGb, g.estMinutes);
    else snprintf(est, sizeof est, "Space and time are shown here once the files are checked.");
    Wrapped(est, T->windowFg, 15.f);
    ImGui::Unindent(A.S(36.f));
    ImGui::Dummy(ImVec2(0, A.S(6.f)));

    // (4) build
    StepHeader(4, "Build", justDone ? 1 : 0);
    ImGui::Indent(A.S(36.f));
    bool canBuild = c1.ok && c2.ok && !building && !TaskRunning();
    if (!building && !justDone) {
        ImGui::BeginDisabled(!canBuild);
        if (Btn(g.state == State::Built || g.state == State::PortUpdated ? "Rebuild##go" : "Build##go", ImVec2(A.S(180.f), A.S(48.f)), true)) StartBuild(g);
        ImGui::EndDisabled();
        if (!canBuild && TaskRunning()) Dim(A.task.kind == "build" ? "Another build is running; start this one when it is done." : "A game is running.");
        else if (!canBuild) Dim("Fix the files above to enable Build.");
        else Dim("Builds on this PC from your files; the Details page shows its log.");
    } else {
        float p; int cur;
        { std::lock_guard<std::mutex> l(A.task.m); p = building ? std::min(1.f, A.task.pct / 100.f) : 1.f; cur = building ? StageOf(A.task.step) : 5; }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 b0 = ImGui::GetCursorScreenPos(); float bw = ImGui::GetContentRegionAvail().x, bh = A.S(14.f);
        dl->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + bh), Col(T->windowBg), bh / 2); dl->AddRect(b0, ImVec2(b0.x + bw, b0.y + bh), Col(T->border), bh / 2);
        if (p > 0.01f) dl->AddRectFilledMultiColor(b0, ImVec2(b0.x + bw * p, b0.y + bh), Col(T->accentBg), Col(T->accent), Col(T->accent), Col(T->accentBg));
        ImGui::Dummy(ImVec2(0, bh + A.S(8.f)));
        for (int i = 0; i < 5; i++) {
            ImVec2 q = ImGui::GetCursorScreenPos(); float r = A.S(6.f); ImVec2 m(q.x + r, q.y + A.S(11.f));
            bool done = justDone || i < cur, now = building && i == cur;
            if (done) dl->AddPolyline((ImVec2[]){ImVec2(m.x - r * .7f, m.y), ImVec2(m.x - r * .15f, m.y + r * .6f), ImVec2(m.x + r * .8f, m.y - r * .6f)}, 3, Col(T->success), 0, A.S(2.f));
            else if (now) dl->AddCircleFilled(m, r * .7f, Col(T->accent), 12);
            else dl->AddCircle(m, r * .7f, Col(T->dimFg, .6f), 12, A.S(1.5f));
            ImGui::SetCursorScreenPos(ImVec2(q.x + A.S(24.f), q.y));
            ImGui::PushFont(now ? A.fBodySemi : A.fBody, A.S(16.f)); ImGui::PushStyleColor(ImGuiCol_Text, now ? ColV(T->accent) : done ? ColV(T->windowFg) : ColV(T->dimFg));
            ImGui::TextUnformatted(kStages[i]); ImGui::PopStyleColor(); ImGui::PopFont();
        }
        if (justDone) {
            ImGui::Dummy(ImVec2(0, A.S(6.f)));
            Wrapped("Done. The game is ready, and its art has its colour back.", T->success, 16.f);
            ImGui::Dummy(ImVec2(0, A.S(4.f)));
            if (Btn("Play##d", ImVec2(A.S(160.f), A.S(48.f)), true)) { ImGui::CloseCurrentPopup(); A.addOpen = false; StartPlay(g); }
        }
    }
    ImGui::Unindent(A.S(36.f));
    ImGui::EndChild();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------------------------------------------
// Screens
static void Action(Game& g, int idx) {
    switch (g.state) {
        case State::Built:   // a Collection entry: its games are played from the Details page
            if (!g.children.empty()) { A.sel = idx; GoPage(App::ScDetails); } else StartPlay(g);
            break;
        case State::NotPorted: break;
        case State::Building: A.sel = idx; GoPage(App::ScDetails); break;
        case State::ReadyToBuild: case State::PortUpdated: A.sel = idx; StartBuild(g); GoPage(App::ScDetails); break;
        case State::NeedsAttention: A.sel = idx; GoPage(App::ScFiles); break;
        default: A.sel = idx; OpenAdd(g); break;
    }
}
static void ManageMenu(Game& g, int idx) {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ColV(T->popoverBg)); ImGui::PushStyleColor(ImGuiCol_Border, ColV(T->border));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(A.S(8.f), A.S(8.f)));
    if (ImGui::BeginPopup("manage")) {
        ImGui::PushFont(A.fBody, A.S(17.f));
        auto item = [&](const char* label) { return ImGui::Selectable(label, false, 0, ImVec2(A.S(240.f), A.S(30.f))); };
        if (item("Rebuild")) { if (g.state == State::NotAdded || g.state == State::NotPorted) Say("Nothing to rebuild: the game is not added"); else { A.sel = idx; OpenAdd(g); } }
        if (item("Open game folder")) {
            std::string dir = g.hasBuild ? FolderOf(g.exe) : !g.disc.empty() ? g.disc : "";
            SDL_PathInfo pi;
            if (dir.empty() || !SDL_GetPathInfo(dir.c_str(), &pi)) Say(dir.empty() ? "No folder recorded for this game yet" : "That folder does not exist on this PC");
            else { std::string url = "file://" + std::string(dir[0] == '/' ? "" : "/"); for (char ch : dir) url += ch == '\\' ? '/' : ch; if (!SDL_OpenURL(url.c_str())) Say(std::string("Could not open the folder: ") + SDL_GetError()); }
        }
        if (item("Open saves")) Say("TEMP: Open saves is not connected yet");
        if (item("Change game files")) { if (g.state == State::NotPorted) Say("Not ported yet: nothing to change"); else { A.sel = idx; GoPage(App::ScFiles); } }
        if (item("Build log")) { A.sel = idx; GoPage(App::ScDetails); }
        if (item("Remove build")) Say("TEMP: Remove build is not connected yet");
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
}

// The selected-game panel: art, title, year, state word, one primary action, Manage. `expanded` = Game details screen.
static void SelectedPanel(float h, bool expanded) {
    Game* gp = Sel(); if (!gp) return;
    Game& g = *gp;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p1(p0.x + w, p0.y + h);
    Panel(p0, p1);   // a libadwaita card: 12 px corners, faint outline
    float pad = A.S(18.f), tx = p0.x + pad;
    if (w >= A.S(620.f)) {   // the art beside the text; a narrow pane keeps the text only (the tile below shows the art)
        ImVec2 a0(p0.x + pad, p0.y + pad), a1(a0.x + (h - pad * 2) * 0.75f, p1.y - pad);
        DrawTile(dl, a0, a1, g, 0.f, 1.35f);
        TileCorners(dl, a0, a1, Opaque(T->cardBg, T->windowBg));
        dl->AddRect(a0, a1, Col(T->border), A.S(theme::kRadiusCard), 0, 1.f);
        tx = a1.x + A.S(28.f);
    }
    ImGui::SetCursorScreenPos(ImVec2(tx, p0.y + pad));
    char meta[160];
    snprintf(meta, sizeof meta, "%d%s%s", g.year, g.titleId.empty() ? "" : "   ·   ", g.titleId.c_str());
    ImGui::PushFont(A.fMono, A.S(15.f)); ImGui::PushStyleColor(ImGuiCol_Text, ColV(T->accent)); ImGui::TextUnformatted(meta); ImGui::PopStyleColor(); ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(tx, ImGui::GetCursorScreenPos().y));
    ImGui::PushFont(A.fHeadBold, A.S(expanded ? 32.f : 30.f)); ImGui::PushTextWrapPos(p1.x - pad - ImGui::GetWindowPos().x + ImGui::GetScrollX()); ImGui::TextUnformatted(g.title.c_str()); ImGui::PopTextWrapPos(); ImGui::PopFont();
    ImVec2 sp = ImGui::GetCursorScreenPos(); sp.x = tx; sp.y += A.S(6.f);
    float sw = StateLine(dl, sp, g, A.S(22.f));
    (void)sw;
    ImGui::SetCursorScreenPos(ImVec2(tx, sp.y + A.S(34.f)));
    std::string line;
    bool bld = BuildingGame(g) || (g.state == State::Building);
    if (bld) { std::lock_guard<std::mutex> l(A.task.m); char b[96]; snprintf(b, sizeof b, "Step %d of 5: %s, %d%%", StageOf(A.task.step) + 1, kStages[StageOf(A.task.step)], (int)A.task.pct); line = b; }
    else if (g.state == State::ReadyToBuild && g.hasEstimate) { char b[96]; snprintf(b, sizeof b, "Your files are in place. About %d min, %.1f GB.", g.estMinutes, g.estGb); line = b; }
    else if (g.state == State::NotAdded) line = "No game files found. This only means the launcher has not seen them; it cannot tell whether you own the game.";
    else if (g.state == State::WrongVersion) line = "Your files are version " + g.version + ". The port needs " + g.needsUpdate + ": add the update package.";
    else if (g.state == State::NotPorted) line = g.children.empty() ? "Listed as in the README table; it cannot be built yet." : "Holds Ratchet & Clank, Going Commando and Up Your Arsenal. Not ported yet.";
    else if (g.state == State::Built) line = "Built with port " + g.builtPortVersion + ". Ready to play.";
    else if (g.state == State::PortUpdated) line = ChangedWhy(g);
    else if (!g.reason.empty()) line = g.reason;
    ImGui::PushTextWrapPos(p1.x - pad - ImGui::GetWindowPos().x + ImGui::GetScrollX());
    Wrapped(line.c_str(), T->dimFg, 16.f);
    if (!bld && (g.state == State::ReadyToBuild || g.state == State::Built || g.state == State::PortUpdated)) {   // 
        std::string d = PlayDisclaimer(g); unsigned c = 0;
        if (!d.empty()) { ImGui::SetCursorScreenPos(ImVec2(tx, ImGui::GetCursorScreenPos().y)); PlayWord(g.playability, c); Wrapped(d.c_str(), c == T->accent || c == T->success ? T->dimFg : T->warning, 15.f); }
    }
    ImGui::PopTextWrapPos();

    // buttons at the bottom of the panel
    float by = p1.y - pad - A.S(50.f);
    ImGui::SetCursorScreenPos(ImVec2(tx, by));
    const char* lab = PrimaryLabel(g);
    ImVec2 bs(std::min(A.S(190.f), std::max(A.S(120.f), p1.x - pad - tx - BtnW("Manage") - BtnW("Details") - A.S(24.f))), A.S(50.f));
    if (bld) {
        float pr = std::min(1.f, A.task.pct / 100.f); ImVec2 q = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(q, ImVec2(q.x + A.S(300.f), q.y + A.S(14.f)), Col(T->windowBg), A.S(7.f)); dl->AddRect(q, ImVec2(q.x + A.S(300.f), q.y + A.S(14.f)), Col(T->border), A.S(7.f));
        dl->AddRectFilledMultiColor(q, ImVec2(q.x + A.S(300.f) * pr, q.y + A.S(14.f)), Col(T->accentBg), Col(T->accent), Col(T->accent), Col(T->accentBg));
        ImGui::Dummy(ImVec2(A.S(300.f), A.S(14.f)));
        ImGui::SameLine(0, A.S(14.f)); ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, q.y - A.S(12.f)));
        ImGui::BeginDisabled(A.task.stopAsked);
        if (Btn(A.task.stopAsked ? "Stopping...##cancel" : "Cancel##cancel", ImVec2(BtnW("Stopping..."), A.S(38.f)), false)) CancelBuild();
        ImGui::EndDisabled();
        if (!expanded) { ImGui::SameLine(0, A.S(12.f)); if (Btn("Details##d", ImVec2(BtnW("Details"), A.S(38.f)), false)) GoPage(App::ScDetails); }
    } else if (lab) {
        if (Btn((std::string(lab) + "##primary").c_str(), bs, true)) Action(g, A.sel);
        if (ImGui::IsWindowAppearing() || A.firstFrame) ImGui::SetItemDefaultFocus();
        ImGui::SameLine(0, A.S(12.f));
    } else {
        Dim("Nothing to do yet. The entry follows the README table.");
        ImGui::SetCursorScreenPos(ImVec2(tx + A.S(420.f), by));
    }
    if (!bld) {
        if (Btn("Manage##m", ImVec2(BtnW("Manage"), A.S(50.f)), false)) ImGui::OpenPopup("manage");
        ManageMenu(g, A.sel);
        if (!expanded) { ImGui::SameLine(0, A.S(12.f)); if (Btn("Details##d", ImVec2(BtnW("Details"), A.S(50.f)), false)) GoPage(App::ScDetails); }
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
    ImGui::Dummy(ImVec2(w, A.S(14.f)));
}

static void DrawCard(Game& g, int idx, ImVec2 size, bool& selectMe) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    char id[32]; snprintf(id, sizeof id, "card%d", idx);
    ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));   // our own ring, shaped like the widget
    ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    NavItemDone(("tile " + g.id).c_str());
    bool hov = ImGui::IsItemHovered(), foc = ImGui::IsItemFocused(), clicked = ImGui::IsItemClicked() || ImGui::IsItemActivated();
    if (clicked || (foc && A.sel != idx && (ImGui::GetCurrentContext()->NavCursorVisible || ImGui::IsItemActive()))) selectMe = true;
    ImVec2 a0 = p, a1(p.x + size.x, p.y + size.x * 1.333f);
    DrawTile(dl, a0, a1, g, (hov || foc) ? 1.f : 0.f, 0.75f + 0.25f * (size.x / A.S(150.f)));
    TileCorners(dl, a0, a1, T->windowBg);
    float rr = A.S(theme::kRadiusCard);
    dl->AddRect(a0, a1, Col(T->border), rr, 0, 1.f);
    if (A.sel == idx) dl->AddRect(ImVec2(a0.x - A.S(3.f), a0.y - A.S(3.f)), ImVec2(a1.x + A.S(3.f), a1.y + A.S(3.f)), Col(T->accent), rr + A.S(3.f), 0, A.S(theme::kFocusRing));
    if (foc && ImGui::GetCurrentContext()->NavCursorVisible)
        dl->AddRect(ImVec2(a0.x - A.S(6.f), a0.y - A.S(6.f)), ImVec2(a1.x + A.S(6.f), a1.y + A.S(6.f)), Col(T->accent), rr + A.S(6.f), 0, A.S(theme::kFocusRing));
    StateLine(dl, ImVec2(p.x + A.S(2.f), a1.y + A.S(8.f)), g, A.S(15.f));
    if (BuildingGame(g)) {
        float pr = std::min(1.f, A.task.pct / 100.f);
        dl->AddRectFilled(ImVec2(a0.x, a1.y - A.S(5.f)), ImVec2(a0.x + size.x * pr, a1.y), Col(T->accentBg));
    }
}

static void RowStatus(const std::string& key);
static void LibraryScreen(float W) {
    (void)W;
    float pad = A.S(24.f);
    ImGui::SetCursorPos(ImVec2(pad, A.S(14.f)));
    float availW = ImGui::GetContentRegionAvail().x - pad;
    if (A.lib.empty) {   // the engine's library lists no game at all: libadwaita's empty state
        ImGui::Dummy(ImVec2(0, A.S(30.f))); ImGui::SetCursorPosX(pad);
        ImGui::BeginDisabled(EngineBusy());
        if (StatusPage(Ico::Library, "No games added yet", "Add the folder that holds your game files. The launcher looks in it and in the folders inside it.",
                       "Add folder##empty", availW))
            BrowseFolder(-1, "loc|library_folders|--add");
        ImGui::EndDisabled();
        ImGui::SetCursorPosX(pad);
        RowStatus("l|library_folders");
        return;
    }
    if (!A.lib.error.empty()) {
        if (EngineBusy() && A.busyKey == kFirstScan) {     // first start: the engine is writing library.json
            Dim("First start: looking for your games and writing library.json next to the launcher.");
            RowStatus(kFirstScan);
            return;
        }
        StatusPage(Ico::Warning, "The library could not be read", (A.lib.error + ". Start the launcher with --library FILE, or put library.json next to it.").c_str(), nullptr, availW);
        ImGui::SetCursorPosX(pad);
        RowStatus(kFirstScan);                              // the first-start scan's message when it failed
        return;
    }
    // filters: a linked toggle group
    int nReady = 0, nNot = 0;
    for (auto& g : A.lib.games) (InReady(g) ? nReady : nNot)++;
    int counts[3] = {(int)A.lib.games.size(), nReady, nNot};
    const char* names[3] = {"All", "Ready", "Not added"};
    std::vector<std::string> labels;
    for (int i = 0; i < 3; i++) { char b[48]; snprintf(b, sizeof b, "%s  %d##flt%d", names[i], counts[i], i); labels.push_back(b); }
    A.inBody = false;   // a request to focus the content skips the filters and lands on the selected game's action
    ToggleGroup("filters", labels, &A.filter);
    A.inBody = true;
    ImGui::Dummy(ImVec2(0, A.S(6.f)));
    ImGui::SetCursorPosX(pad);
    ImGui::BeginGroup();
    float ph = std::clamp(ImGui::GetIO().DisplaySize.y * 0.36f, A.S(250.f), A.S(340.f));
    ImGui::PushItemWidth(availW);
    ImGui::BeginChild("selpanel", ImVec2(availW, ph + A.S(14.f)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    SelectedPanel(ph, false);
    ImGui::EndChild();
    ImGui::PopItemWidth();
    // grid of all games, series order, filtered only by hiding
    float gap = A.S(14.f), minW = A.S(128.f);
    int cols = std::max(1, std::min(8, (int)((availW + gap) / (minW + gap))));
    float cw = std::min(A.S(240.f), (availW - gap * (cols - 1)) / cols);
    float ch = cw * 1.333f + A.S(40.f);
    int n = 0;
    for (int i = 0; i < (int)A.lib.games.size(); i++) {
        Game& g = A.lib.games[i];
        if ((A.filter == 1 && !InReady(g)) || (A.filter == 2 && InReady(g))) continue;
        if (n % cols) ImGui::SameLine(0, gap);
        else ImGui::SetCursorPosX(pad);
        bool sm = false;
        DrawCard(g, i, ImVec2(cw, ch), sm);
        if (sm) A.sel = i;
        n++;
    }
    if (n == 0) StatusPage(Ico::Library, "No games here", A.filter == 1 ? "No game is ready yet. Add a game to build it." : "Every game has been added.", nullptr, availW);
    ImGui::EndGroup();
}

// A screen's column: `w` wide (at most `maxW`), centred in the pane with at least 24 px each side; the cursor is put at
// its top. Returns the left edge (window-local x) so later lines can come back to it.
static float Column(float maxW, float& w) {
    float side = A.S(24.f), avail = ImGui::GetContentRegionAvail().x;
    w = std::min(avail - side * 2, A.S(maxW));
    float x = std::max(side, (avail - w) / 2);
    ImGui::SetCursorPos(ImVec2(x, A.S(14.f)));
    return x;
}
// Nothing selected (an empty library): the inner pages say so instead of drawing nothing.
static bool NoGame() {
    if (Sel()) return false;
    float w; float x = Column(760.f, w);
    ImGui::Dummy(ImVec2(0, A.S(30.f))); ImGui::SetCursorPosX(x);
    StatusPage(Ico::Library, "No game selected", "Choose a game in the Library first.", nullptr, w);
    return true;
}

// Details > Build and play: the running build's stage, progress and Cancel; the last build's result (its REASON lines
// when it failed); Build / Rebuild / Play; the last game run's exit code; the engine's log lines. A Collection entry
// lists its games, each with its own Build or Play.
static void ProgressBar(float w, float frac) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b0 = ImGui::GetCursorScreenPos(); float bh = A.S(12.f);
    dl->AddRectFilled(b0, ImVec2(b0.x + w, b0.y + bh), Col(T->windowBg), bh / 2); dl->AddRect(b0, ImVec2(b0.x + w, b0.y + bh), Col(T->border), bh / 2);
    if (frac > 0.005f) dl->AddRectFilled(b0, ImVec2(b0.x + std::max(bh, w * std::min(1.f, frac)), b0.y + bh), Col(T->accentBg), bh / 2);
    ImGui::Dummy(ImVec2(w, bh));
}
static bool TaskOf(const Game& g) {   // the current or last task belongs to this entry (or a game inside it)
    if (A.task.kind.empty()) return false;
    if (A.task.game == g.id) return true;
    for (auto& c : g.children) if (c.id == A.task.game) return true;
    return false;
}
static void BuildAndPlay(Game& g, float x, float w) {
    if (g.state == State::NotAdded || g.state == State::NotPorted || g.state == State::WrongVersion) {
        if (!TaskOf(g)) return;
    }
    std::string step, cur, logPath; std::vector<std::string> lines, reasons; float pct; int result, code, secs; bool exited, stopAsked, cancelled;
    { std::lock_guard<std::mutex> l(A.task.m); step = A.task.step; cur = A.task.curGame; logPath = A.task.logPath; lines = A.task.lines; reasons = A.task.reasons;
      pct = A.task.pct; result = A.task.result; code = A.task.exitCode; secs = A.task.secs; exited = A.task.exited; stopAsked = A.task.stopAsked; cancelled = A.task.cancelled; }
    bool mine = TaskOf(g), running = mine && TaskRunning(), building = running && A.task.kind == "build";
    std::string desc = g.children.empty() ? "The build runs on this PC from your own files; the game starts from here when it is built."
                                          : "Each game of the collection is built and played on its own.";
    if (g.hasEstimate && g.children.empty()) { char b[96]; snprintf(b, sizeof b, " A build takes about %d min and %.1f GB.", g.estMinutes, g.estGb); desc += b; }
    ImGui::SetCursorPosX(x);
    ImGui::BeginGroup();
    GroupTitle("Build and play", desc.c_str(), w);
    BeginBoxedList(w);
    if (building) {
        BodyRow([&] {
            const Game* cg = nullptr; for (auto& c : g.children) if (c.id == cur) cg = &c;
            char b[200]; snprintf(b, sizeof b, "%s%sStep %d of 5: %s", cg ? cg->title.c_str() : "", cg ? " · " : "", StageOf(step) + 1, kStages[StageOf(step)]);
            ImGui::PushFont(A.fBodySemi, A.S(17.f)); ImGui::TextUnformatted(b); ImGui::PopFont();
            float bw = w - A.S(28.f) - BtnW("Stopping...") - A.S(16.f);
            ImVec2 r0 = ImGui::GetCursorPos();
            ImGui::Dummy(ImVec2(0, A.S(4.f))); ProgressBar(bw, pct / 100.f);
            char pc[64]; snprintf(pc, sizeof pc, "%d%%   ·   step %s", (int)pct, step.empty() ? "starting" : step.c_str());
            Dim(pc, 14.f);
            ImGui::SetCursorPos(ImVec2(r0.x + bw + A.S(16.f), r0.y - A.S(6.f)));
            ImGui::BeginDisabled(stopAsked);
            if (Btn(stopAsked ? "Stopping...##cb" : "Cancel##cb", ImVec2(BtnW("Stopping..."), A.S(38.f)), false)) CancelBuild();
            ImGui::EndDisabled();
            std::string st; for (int i = 0; i < 5; i++) { st += (i ? "   ›   " : ""); st += kStages[i]; }
            ImGui::SetCursorPosX(r0.x);
            ImGui::Dummy(ImVec2(0, A.S(2.f)));
            ImGui::PushFont(A.fBody, A.S(14.f));
            for (int i = 0; i < 5; i++) {
                if (i) { ImGui::SameLine(0, A.S(6.f)); ImGui::TextColored(ColV(T->dimFg), "›"); ImGui::SameLine(0, A.S(6.f)); }
                int si = StageOf(step);
                ImGui::TextColored(i < si ? ColV(T->success) : i == si ? ColV(T->accent) : ColV(T->dimFg), "%s", kStages[i]);
            }
            ImGui::PopFont();
        });
    } else if (mine && A.task.kind == "build" && result >= 0) {
        if (result == 1) Row("Last build", "Finished: " + A.task.title + " is built and ready to play.", 0, [] {}, T->success);
        else if (cancelled) Row("Last build", "Stopped. Build again to continue where it stopped.", 0, [] {});
        else {
            std::string why; for (auto& r : reasons) why += (why.empty() ? "" : "\n") + r;
            Row("Last build did not finish", why.empty() ? "The build stopped. The log below says why." : why, 0, [] {}, T->destructive);
        }
    } else if (mine && A.task.kind == "play") {
        if (running) Row("Playing", "The game is running. The launcher waits until it closes.", 0, [] {}, T->accent);
        else if (exited) { char b[160]; snprintf(b, sizeof b, "Closed with exit code %d after %d s.", code, secs); Row("Last game run", b, 0, [] {}, code == 0 ? T->success : T->destructive); }
        else if (!reasons.empty()) Row("Could not start the game", reasons[0], 0, [] {}, T->destructive);
    }
    auto actions = [&](Game& e) {   // Build / Rebuild and Play for one game
        bool can = !TaskRunning();
        std::string bl = (e.state == State::Built || e.state == State::PortUpdated) ? "Rebuild" : "Build";
        bool canBuild = e.state == State::ReadyToBuild || e.state == State::Built || e.state == State::PortUpdated || e.state == State::NeedsAttention;
        bool canPlay = Playable(e);
        float cw = (canBuild ? BtnW(bl.c_str()) : 0.f) + (canPlay ? BtnW("Play", Ico::None, true) + A.S(10.f) : 0.f);
        std::string tmp, sub = StateWord(e, tmp);
        if (e.hasBuild) sub += "  ·  built " + (e.builtAt.empty() ? std::string("?") : e.builtAt.substr(0, 16)) + " with port " + e.builtPortVersion;
        else if (!e.reason.empty() && e.state != State::ReadyToBuild) sub += ": " + e.reason;
        if (e.state == State::PortUpdated && e.hasBuild && !e.reason.empty()) sub += "\n" + ChangedWhy(e);   // what changed
        if (!e.updMsg.empty()) sub += "\n" + e.updMsg;                                               // a newer update than tested
        std::string disc = PlayDisclaimer(e);                                                        // beside Build
        if (canBuild && !disc.empty()) sub += "\n" + disc;
        RowOpt ro; unsigned tc = 0; const char* tw = canBuild ? PlayWord(e.playability, tc) : nullptr;
        ro.tag = tw; ro.tagCol = tc;
        RowX(e.title.c_str(), sub, cw, [&] {
            ImGui::BeginDisabled(!can);
            if (canBuild && Btn((bl + "##b_" + e.id).c_str(), ImVec2(BtnW(bl.c_str()), A.S(38.f)), !canPlay)) StartBuild(e);
            if (canBuild && canPlay) ImGui::SameLine(0, A.S(10.f));
            if (canPlay && PillButton(("Play##p_" + e.id).c_str(), Kind::Suggested, ImVec2(BtnW("Play", Ico::None, true), A.S(38.f)))) StartPlay(e);
            ImGui::EndDisabled();
        }, ro);
    };
    if (g.children.empty()) actions(g);
    else for (auto& c : g.children) actions(c);
    EndBoxedList();
    if (mine && !lines.empty()) {
        GroupTitle("Log", "The setup engine's output; the full build log stays in the build folder.", w);
        BeginBoxedList(w);
        BodyRow([&] {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::BeginChild("tasklog", ImVec2(w - A.S(28.f), A.S(230.f)), 0, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::PushFont(A.fMono, A.S(13.f));
            size_t from = lines.size() > 400 ? lines.size() - 400 : 0;
            for (size_t i = from; i < lines.size(); i++) {
                const std::string& ln = lines[i];
                unsigned c = ln.compare(0, 7, "REASON ") == 0 || ln.compare(0, 5, "FAIL ") == 0 ? T->destructive
                           : ln.compare(0, 7, "RESULT ") == 0 || ln.compare(0, 7, "STAGED ") == 0 || ln.compare(0, 7, "EXITED ") == 0 ? T->accent
                           : ln.compare(0, 5, "WARN ") == 0 ? T->warning : T->windowFg;
                ImGui::PushStyleColor(ImGuiCol_Text, ColV(c)); ImGui::TextUnformatted(ln.c_str()); ImGui::PopStyleColor();
            }
            ImGui::PopFont();
            if (running || A.shotMode) ImGui::SetScrollHereY(1.f);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            if (!logPath.empty()) { if (Btn("Open log folder##lf", ImVec2(BtnW("Open log folder"), A.S(36.f)), false)) OpenFolderOf(logPath); }
        });
        EndBoxedList();
    }
    ImGui::EndGroup();
}

// ---------------------------------------------------------------------------------------------------------------
// The build kit dialog (launcher phase 4): opened by Build when the kit is not next to the launcher. It says what would
// be downloaded, how big, from where, and how it is checked, and downloads only when the player chooses Download.
static std::string HostOf(const std::string& url) {
    size_t s = url.find("://"); if (s == std::string::npos) return url;
    s += 3; size_t e = url.find('/', s);
    return url.substr(s, e == std::string::npos ? std::string::npos : e - s);
}
static std::string Megabytes(long long b) {
    char t[48];
    if (b >= 1000LL * 1000 * 1000) snprintf(t, sizeof t, "%.1f GB", b / 1e9); else snprintf(t, sizeof t, "%lld MB", (b + 500000) / 1000000);
    return t;
}
static void KitDialog(float W, float H) {
    if (!ImGui::IsPopupOpen("kit")) ImGui::OpenPopup("kit");
    float dw = std::min(A.S(640.f), W - A.S(32.f));
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dw, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(A.S(24.f), A.S(20.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, A.S(theme::kRadiusCard));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ColV(T->dialogBg)); ImGui::PushStyleColor(ImGuiCol_Border, ColV(T->border));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ColV(0x00000666u));
    bool open = ImGui::BeginPopupModal("kit", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor(3); ImGui::PopStyleVar(2);
    if (!open) { A.kit.open = false; return; }
    float w = dw - A.S(48.f);
    bool mine = A.task.kind == "kit", running = mine && TaskRunning();
    int result; float pct; bool stopAsked, cancelled; std::string step; std::vector<std::string> reasons;
    { std::lock_guard<std::mutex> l(A.task.m); result = A.task.result; pct = A.task.pct; stopAsked = A.task.stopAsked; cancelled = A.task.cancelled; step = A.task.step; reasons = A.task.reasons; }
    bool failed = mine && !running && result == 0 && !cancelled;
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + w);
    ImGui::PushFont(A.fHeadBold, A.S(26.f)); ImGui::TextUnformatted(running ? "Downloading the build kit" : "Download the build kit?"); ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, A.S(4.f)));
    const std::string& st = A.kit.state;
    std::string why = st == "other" ? "The build kit next to the launcher belongs to another release (" + (A.kit.release.empty() ? std::string("unknown") : A.kit.release) +
                                      "). This launcher needs the one of release " LAUNCHER_KIT_RELEASE "."
                    : st == "damaged" ? std::string("Files of the build kit next to the launcher are missing. Downloading it again puts them back.")
                    : st == "unknown" ? std::string("Checking the launcher's folder for the build kit...")
                    : std::string("Building a game on this PC needs the build kit: the port's source code, the recompiler, a compiler (llvm-mingw), CMake "
                                  "and Ninja. It is not next to the launcher yet.");
    Wrapped(why.c_str(), T->windowFg, 16.f);
    ImGui::Dummy(ImVec2(0, A.S(8.f)));
    BeginBoxedList(w);
    PropRow("Download", Megabytes(LAUNCHER_KIT_BYTES) + " (about), once; " + Megabytes(LAUNCHER_KIT_UNPACKED) + " on disk after unpacking");
    PropRow("From", HostOf(A.kit.url) + (A.kit.allowLocal ? "  (test address)" : "  (this project's release page, GitHub)"));
    PropRow("Address", A.kit.url, true);
    PropRow("Checked", "Against a fingerprint built into this launcher. A download that does not match is refused and deleted; nothing of it is kept.");
    EndBoxedList();
    ImGui::Dummy(ImVec2(0, A.S(6.f)));
    Wrapped("Nothing is downloaded unless you choose Download, and nothing about you or your games is sent. You can also get the "
            "build download from the release page yourself and unpack it into the launcher's folder.", T->dimFg, 14.f);
    ImGui::Dummy(ImVec2(0, A.S(8.f)));
    if (running) {
        ProgressBar(w, pct / 100.f);
        char b[96]; snprintf(b, sizeof b, "%d%%   ·   %s", (int)pct, step.empty() ? "starting" : step == "download" ? "downloading" : step == "verify" ? "checking" : step == "install" ? "installing" : step.c_str());
        Dim(b, 14.f);
        ImGui::Dummy(ImVec2(0, A.S(6.f)));
        ImGui::BeginDisabled(stopAsked);
        if (Btn(stopAsked ? "Stopping...##kc" : "Cancel##kc", ImVec2(BtnW("Stopping..."), A.S(42.f)), false)) CancelBuild();
        ImGui::EndDisabled();
    } else {
        if (failed) {
            std::string r; for (auto& x : reasons) r += (r.empty() ? "" : "\n") + x;
            Wrapped(r.empty() ? "The download did not finish. Nothing was installed." : r.c_str(), T->destructive, 15.f);
            ImGui::Dummy(ImVec2(0, A.S(6.f)));
        } else if (mine && cancelled) { Wrapped("Download stopped. Nothing was installed.", T->dimFg, 15.f); ImGui::Dummy(ImVec2(0, A.S(6.f))); }
        const char* go = failed ? "Try again##kd" : "Download##kd";
        ImGui::BeginDisabled(TaskRunning() || A.kit.url.empty() || st == "unknown");
        if (Btn(go, ImVec2(BtnW("Try again") + A.S(20.f), A.S(42.f)), true)) {
            Game* gp = nullptr;
            for (auto& g : A.lib.games) { if (g.id == A.kit.pendingGame) gp = &g; for (auto& c : g.children) if (c.id == A.kit.pendingGame) gp = &c; }
            if (gp) StartTask("kit", *gp);
        }
        ImGui::EndDisabled();
        if (A.firstFrame || ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
        ImGui::SameLine(0, A.S(10.f));
        if (Btn("Not now##kn", ImVec2(BtnW("Not now") + A.S(12.f), A.S(42.f)), false) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
            A.kit.open = false; A.kit.pendingGame.clear(); ImGui::CloseCurrentPopup();
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
}

// the catalogue's playability in this release, shown beside every Build (badges.py's words and colours).
static const char* PlayWord(const std::string& p, unsigned& col) {
    col = T->warning;
    if (p == "boots") return "Boots";
    if (p == "menus") return "Menus";
    if (p == "in_game") { col = T->accent; return "In game"; }
    if (p == "playable") { col = T->success; return "Playable"; }
    if (p == "completed") { col = T->success; return "Completed"; }
    if (p == "not_built") { col = T->dimFg; return "Not built"; }
    return nullptr;
}
static std::string PlayDisclaimer(const Game& g) {
    unsigned c; const char* w = PlayWord(g.playability, c);
    if (!w) return "";
    std::string s = std::string("In this release: ") + w;
    if (!g.playabilityNote.empty()) s += ", " + g.playabilityNote;
    if (!g.playabilityChecked.empty()) s += " (checked " + g.playabilityChecked + ")";
    return s + ".";
}

static void SettingGroups(const std::string& scope, float w);
static void DetailsScreen() {
    if (NoGame()) return;
    Game& g = *Sel();
    float w; float x = Column(1100.f, w);
    ImGui::BeginChild("detpanel", ImVec2(w, A.S(250.f)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    SelectedPanel(A.S(236.f), true);
    ImGui::EndChild();
    BuildAndPlay(g, x, w);
    // boxed lists of key/value rows: two columns when the pane is wide, one when it is narrow
    bool two = w >= A.S(820.f);
    float gap = A.S(18.f), cw = two ? (w - gap) / 2 : w;
    std::string tmp;
    auto group = [&](const char* title, float cx, auto body) {
        ImGui::SetCursorPosX(cx);
        ImGui::BeginGroup();
        GroupTitle(title, nullptr, cw);
        BeginBoxedList(cw); body(); EndBoxedList();
        ImGui::EndGroup();
    };
    auto pair = [&](const char* t1, auto b1, const char* t2, auto b2) {
        float y = ImGui::GetCursorPosY();
        group(t1, x, b1);
        float y1 = ImGui::GetCursorPosY();
        if (two) ImGui::SetCursorPosY(y);
        group(t2, two ? x + cw + gap : x, b2);
        if (two) ImGui::SetCursorPosY(std::max(y1, ImGui::GetCursorPosY()));
    };
    pair("Readme state", [&] {
        PropRow("State", g.readme.empty() ? "unknown" : g.readme);
        PropRow("Port supports it", g.supported ? "yes, it can be built" : "not yet");
        PropRow("Launcher state", StateWord(g, tmp));
        if (!g.reason.empty()) PropRow("Reason", g.reason);
        if (!g.children.empty()) { std::string c; for (auto& ch : g.children) c += (c.empty() ? "" : ", ") + ch.title; PropRow("Contains", c); }
    }, "What this game needs", [&] {
        PropRow("Title ID", Esc(g.titleId), !g.titleId.empty());
        PropRow("Your version", g.version.empty() ? "not known (no files)" : g.version, !g.version.empty());
        PropRow("Update needed", g.needsUpdate.empty() ? "none" : g.needsUpdate, !g.needsUpdate.empty());
        PropRow("EBOOT.elf", g.eboot.empty() ? "not made yet (the build decrypts it)" : "decrypted: " + g.eboot);
    });
    pair("Recorded files", [&] {
        PropRow("Game files", Esc(g.disc), !g.disc.empty());
        PropRow("Update", Esc(g.update), !g.update.empty());
        PropRow("EBOOT.elf", Esc(g.eboot), !g.eboot.empty());
    }, "Build info", [&] {
        if (!g.hasBuild) {
            PropRow("Built", "no");
            if (g.hasEstimate) { char b[64]; snprintf(b, sizeof b, "about %d min, %.1f GB", g.estMinutes, g.estGb); PropRow("Estimate", b); }
        } else {
            PropRow("Executable", g.exe, true);
            PropRow("Built with port", g.builtPortVersion.empty() ? "?" : g.builtPortVersion, !g.builtPortVersion.empty());
            PropRow("Built at", g.builtAt);
            if (!g.exeSha.empty()) PropRow("Exe SHA-256", g.exeSha.substr(0, 16) + "...", true);
        }
        if (g.igdbGame) { char b[64]; snprintf(b, sizeof b, "game %lld, cover %s", g.igdbGame, g.igdbCover.empty() ? "-" : g.igdbCover.c_str()); PropRow("IGDB id", b, true); }
    });
    // this game's display and controls: each row follows the shared value or keeps the game's own
    ImGui::SetCursorPosX(x);
    ImGui::BeginGroup();
    SettingGroups(g.id, w);
    ImGui::EndGroup();
}

// ---------------------------------------------------------------------------------------------------------------
// Locations: Settings > Locations (global) and Manage > Change game files (per game). Every change goes through the engine.
static std::string Fit(const std::string& t, float maxW, ImFont* f, float px) {   // keeps the end of a long path ("...\folder\file")
    if (f->CalcTextSizeA(px, FLT_MAX, 0, t.c_str()).x <= maxW) return t;
    size_t cut = 1;
    while (cut < t.size() && f->CalcTextSizeA(px, FLT_MAX, 0, ("..." + t.substr(cut)).c_str()).x > maxW) cut++;
    return "..." + t.substr(cut);
}
// "Checking..." while the engine works on this row, its message when it refused (exit 2), nothing otherwise. Always one line high.
static void RowStatus(const std::string& key) {
    if (EngineBusy() && A.busyKey == key) { Wrapped("Checking...", T->accent, 15.f); return; }
    if (A.errKey == key) {
        Wrapped(A.errText.c_str(), T->destructive, 15.f);
        return;
    }
    ImGui::Dummy(ImVec2(0, A.S(22.f)));
}
static const char* kSlotLabel[4] = {"Game files (disc folder, ISO or PKG)", "Update (PKG or installed folder)", "EBOOT.elf (decrypted)", "Saves"};
static const char* kSlotKey[4] = {"disc", "update", "eboot", "saves"};
static std::string SlotPath(const Game& g, int i) { return i == 0 ? g.disc : i == 1 ? g.update : i == 2 ? g.eboot : g.saves; }
static const char* SourceWord(const Game& g, const std::string& slot, const std::string& path, unsigned& col) {
    auto it = g.sources.find(slot);
    std::string s = it == g.sources.end() ? "" : it->second;
    if (path.empty()) { col = T->dimFg; return "Not found"; }
    if (s == "override") { col = T->accent; return "Chosen by you"; }
    if (s == "rpcs3") { col = T->accent; return "RPCS3"; }
    col = T->success; return "Found";
}
// Which override does the engine's reason belong to? It does not name the slot: ASSUMPTION = an override with a suggestion,
// else one whose path is missing, else the first override.
static std::string BadSlot(const Game& g) {
    if (g.state != State::NeedsAttention) return "";
    for (auto k : kSlotKey) if (g.overrides.count(k) && g.suggested.count(k)) return k;
    SDL_PathInfo pi;
    for (auto k : kSlotKey) if (g.overrides.count(k) && !SDL_GetPathInfo(g.overrides.at(k).c_str(), &pi)) return k;
    for (auto k : kSlotKey) if (g.overrides.count(k)) return k;
    return "";
}

// The engine's state for one settings row: "Checking..." while it works on it, its message when it refused.
static bool RowMsg(const std::string& key, std::string& msg, unsigned& col) {
    if (EngineBusy() && A.busyKey == key) { msg = "Checking..."; col = T->accent; return true; }
    if (A.errKey == key) { msg = A.errText; col = T->destructive; return true; }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// Display and control settings (launcher-plan.md "Shared display and control settings"): the shared files every game
// reads first (<data>/shared/display.ini, keybinds.ini) and each game's own (<data>/<game id>/). Read and written only
// through the engine (catalog.py get-settings / set-setting); a game's own value wins, "Use the shared value" removes it.
enum class SK { Choice, OnOff, Number, Keys };
struct SDef { const char* key; const char* title; const char* desc; SK kind; std::vector<std::string> labels, values; float step = 0, lo = 0, hi = 0; };
static const std::vector<SDef>& DisplayDefs() {
    static const std::vector<SDef> d = {
        {"display.mode", "Window mode", "Alt+Enter switches in the game.", SK::Choice, {"Windowed", "Borderless", "Fullscreen"}, {"windowed", "borderless", "fullscreen"}},
        {"display.size", "Window size", "The first start's size. Each game then remembers its own window.", SK::Choice, {"720p", "1080p", "1440p", "4K"}, {"720p", "1080p", "1440p", "2160p"}},
        {"display.scaling", "Scaling", "Fit keeps the picture's shape (black bars); Stretch fills the window.", SK::Choice, {"Fit", "Stretch"}, {"fit", "stretch"}},
        {"display.filter", "Filter", "Nearest with Fit scales by whole numbers (sharp pixels).", SK::Choice, {"Bilinear", "Nearest"}, {"bilinear", "nearest"}},
        {"display.vsync", "Vertical sync", "Waits for the screen: no tearing.", SK::OnOff, {}, {}},
        {"display.upscale", "Upscaler", "FSR 1 sharpens the picture when the window is larger than 720p.", SK::Choice, {"Off", "FSR 1"}, {"none", "fsr1"}},
        {"display.upscale_sharpness", "FSR 1 sharpness", "0 is the sharpest, 2 the softest.", SK::Number, {}, {}, 0.1f, 0.f, 2.f},
    };
    return d;
}
static const std::vector<SDef>& ControlDefs() {
    static const std::vector<SDef> d = {
        {"mouse.look", "Mouse look", "The mouse turns the camera.", SK::OnOff, {}, {}},
        {"mouse.sensitivity", "Mouse sensitivity", "", SK::Number, {}, {}, 0.25f, 0.25f, 20.f},
        {"mouse.zoom_sensitivity", "Zoomed sensitivity", "A factor while zoomed in.", SK::Number, {}, {}, 0.25f, 0.25f, 5.f},
        {"mouse.invert_y", "Invert mouse Y", "", SK::OnOff, {}, {}},
        {"keyboard.ls_up", "Move forward", "", SK::Keys, {}, {}}, {"keyboard.ls_down", "Move back", "", SK::Keys, {}, {}},
        {"keyboard.ls_left", "Move left", "", SK::Keys, {}, {}}, {"keyboard.ls_right", "Move right", "", SK::Keys, {}, {}},
        {"keyboard.rs_up", "Camera up", "", SK::Keys, {}, {}}, {"keyboard.rs_down", "Camera down", "", SK::Keys, {}, {}},
        {"keyboard.rs_left", "Camera left", "", SK::Keys, {}, {}}, {"keyboard.rs_right", "Camera right", "", SK::Keys, {}, {}},
        {"keyboard.cross", "Jump, accept", "", SK::Keys, {}, {}}, {"keyboard.start", "Pause menu", "", SK::Keys, {}, {}},
    };
    return d;
}
static std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
// The value a scope ("shared" or a game id) uses: the game's own, else the shared one, else the default. own/set: where it came from.
static std::string SetVal(const std::string& scope, const std::string& key, bool* own = nullptr, bool* shared = nullptr) {
    const Settings& s = A.set;
    if (own) *own = false;
    if (shared) *shared = false;
    if (scope != "shared") {
        auto g = s.games.find(scope);
        if (g != s.games.end()) { auto v = g->second.find(key); if (v != g->second.end()) { if (own) *own = true; return v->second; } }
    }
    auto v = s.shared.find(key);
    if (v != s.shared.end()) { if (shared) *shared = true; return v->second; }
    auto d = s.defaults.find(key);
    return d == s.defaults.end() ? "" : d->second;
}
static int ChoiceIndex(const SDef& d, std::string v) {
    v = Lower(v);
    if (d.key == std::string("display.size")) {   // WxH of a preset reads as the preset
        static const char* wh[4] = {"1280x720", "1920x1080", "2560x1440", "3840x2160"};
        for (int i = 0; i < 4; i++) if (v == wh[i]) return i;
        if (v == "4k") return 3;
    }
    for (size_t i = 0; i < d.values.size(); i++) if (d.values[i] == v) return (int)i;
    return -1;
}
static std::string ValueWords(const SDef& d, const std::string& v) {   // the value as the row shows it
    if (d.kind == SK::Choice) { int i = ChoiceIndex(d, v); return i >= 0 ? d.labels[i] : v; }
    if (d.kind == SK::OnOff) return Lower(v) == "on" ? "On" : "Off";
    return v;
}
static void SettingsArgs(std::vector<std::string>& a) { if (!A.dataDir.empty()) { a.push_back("--data"); a.push_back(A.dataDir); } }
static void LoadSettings() {
    std::vector<std::string> a = {"get-settings"}; SettingsArgs(a);
    A.set.loading = true;
    RunEngine("s|load", a);
}
static void SetSetting(const std::string& scope, const std::string& key, const std::string& value, bool unset = false) {
    std::vector<std::string> a = {"set-setting"}; SettingsArgs(a);
    if (scope == "shared") a.push_back("--shared"); else { a.push_back("--game"); a.push_back(scope); }
    a.push_back(key);
    if (unset) a.push_back("--unset"); else a.push_back(value);
    RunEngine("s|" + scope + "|" + key, a);
    if (A.shotMode) return;
    // shown at once; the engine's answer (the files as written) replaces it
    if (scope == "shared") { if (unset) A.set.shared.erase(key); else A.set.shared[key] = value; }
    else { if (unset) A.set.games[scope].erase(key); else A.set.games[scope][key] = value; }
}
// get-settings / set-setting output -> A.set
static bool ParseSettings(const std::string& out) {
    Json j; std::string err;
    size_t b = out.find('{');
    if (b == std::string::npos || !Json::parse(out.substr(b), j, err)) return false;
    Settings s; s.loaded = true;
    s.sharedFolder = j["shared_folder"].str();
    for (auto& kv : j["defaults"].o) s.defaults[kv.first] = kv.second.str();
    for (auto& kv : j["shared"].o) s.shared[kv.first] = kv.second.str();
    for (auto& g : j["games"].o) for (auto& kv : g.second.o) s.games[g.first][kv.first] = kv.second.str();
    A.set = std::move(s);
    return true;
}
// Not read yet (or the engine failed): one row that says so. Returns true when the rows can be drawn.
static bool SettingsReady(float w) {
    if (A.set.loaded) return true;
    if (!A.set.loading && !EngineBusy() && A.set.err.empty()) LoadSettings();
    BeginBoxedList(w);
    if (!A.set.err.empty()) Row("The settings files could not be read", A.set.err, 0, [] {}, T->destructive);
    else Row("Reading the settings files...", "", 0, [] {});
    EndBoxedList();
    return false;
}
// One setting row. scope "shared": the value control (and Reset when the shared file has it). A game id: the value
// control, disabled while the row uses the shared value, then "Shared" and its switch.
static void SettingRow(const std::string& scope, const SDef& d) {
    bool own = false, inShared = false;
    std::string v = SetVal(scope, d.key, &own, &inShared);
    bool perGame = scope != "shared", useShared = perGame && !own;
    std::string rowKey = "s|" + scope + "|" + d.key, msg; unsigned col = 0;
    bool has = RowMsg(rowKey, msg, col);
    ImGui::PushID(d.key);
    float gap = A.S(10.f), swW = A.S(48.f);
    ImVec2 lblSz = A.fBody->CalcTextSizeA(A.S(15.f), FLT_MAX, 0, "Shared");
    float valW = d.kind == SK::Choice ? ToggleGroupW(d.labels) : d.kind == SK::OnOff ? swW
               : d.kind == SK::Number ? A.S(38.f) * 2 + A.S(64.f) + gap * 2 : 0.f;
    float resetW = BtnW("Reset");
    float ctrlW = valW + (perGame ? (valW > 0 ? gap * 2 : 0.f) + lblSz.x + gap + swW : (valW > 0 ? gap : 0.f) + resetW);
    std::string sub;
    std::string shownShared = ValueWords(d, SetVal("shared", d.key));
    if (has) sub = msg;
    else if (d.kind == SK::Keys) sub = perGame ? (own ? v + "   (this game; shared: " + shownShared + ")" : v) : v;
    else if (perGame) sub = own ? "This game only. Shared: " + shownShared : "Shared value";
    else sub = d.desc;
    if (!has && !perGame && !inShared && d.kind != SK::Keys) sub += std::string(sub.empty() ? "" : " ") + "(default)";
    RowOpt o; o.subCol = has ? col : 0; o.mono = d.kind == SK::Keys && !has;
    RowX(d.title, sub, ctrlW, [&] {
        bool busy = EngineBusy();
        float y0 = ImGui::GetCursorPosY();
        ImGui::BeginDisabled(busy || useShared);
        if (d.kind == SK::Choice) {
            ImGui::SetCursorPosY(y0 - A.S(1.f));
            int cur = ChoiceIndex(d, v);
            if (ToggleGroup("v", d.labels, &cur) && cur >= 0) SetSetting(scope, d.key, d.values[cur]);
        } else if (d.kind == SK::OnOff) {
            ImGui::SetCursorPosY(y0 + A.S(6.f));
            bool on = Lower(v) == "on";
            if (Switch("##v", &on)) SetSetting(scope, d.key, on ? "on" : "off");
        } else if (d.kind == SK::Number) {
            float f = (float)atof(v.c_str());
            char b[32];
            if (PillButton("-##dec", Kind::Normal, ImVec2(A.S(38.f), 0), false) && f - d.step >= d.lo - 1e-4f) { snprintf(b, sizeof b, "%g", f - d.step); SetSetting(scope, d.key, b); }
            ImGui::SameLine(0, gap);
            snprintf(b, sizeof b, "%g", f);
            ImVec2 ts = A.fBodySemi->CalcTextSizeA(A.S(17.f), FLT_MAX, 0, b);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddText(A.fBodySemi, A.S(17.f), ImVec2(p.x + (A.S(64.f) - ts.x) / 2, p.y + (A.S(38.f) - ts.y) / 2),
                                                Col(T->windowFg, useShared ? 0.5f : 1.f), b);
            ImGui::Dummy(ImVec2(A.S(64.f), A.S(38.f)));
            ImGui::SameLine(0, gap);
            if (PillButton("+##inc", Kind::Normal, ImVec2(A.S(38.f), 0), false) && f + d.step <= d.hi + 1e-4f) { snprintf(b, sizeof b, "%g", f + d.step); SetSetting(scope, d.key, b); }
        }
        ImGui::EndDisabled();
        if (perGame) {
            if (valW > 0) ImGui::SameLine(0, gap * 2);
            ImGui::SetCursorPosY(y0);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddText(A.fBody, A.S(15.f), ImVec2(p.x, p.y + (A.S(38.f) - lblSz.y) / 2), Col(T->dimFg), "Shared");
            ImGui::Dummy(ImVec2(lblSz.x, A.S(38.f)));
            ImGui::SameLine(0, gap);
            ImGui::SetCursorPosY(y0 + A.S(6.f));
            ImGui::BeginDisabled(busy);
            bool sh = useShared;
            if (Switch(("##shared_" + std::string(d.key)).c_str(), &sh)) {
                if (sh) SetSetting(scope, d.key, "", true);   // back to the shared value
                else SetSetting(scope, d.key, v);           // this game keeps the value it uses now
            }
            ImGui::EndDisabled();
        } else {
            if (valW > 0) ImGui::SameLine(0, gap);
            ImGui::SetCursorPosY(y0);
            ImGui::BeginDisabled(busy || !inShared);   // Reset: back to the default (the shared file drops the key)
            if (PillButton("Reset##r", Kind::Flat, ImVec2(resetW, 0), false)) SetSetting(scope, d.key, "", true);
            ImGui::EndDisabled();
        }
    }, o);
    ImGui::PopID();
}
static void SettingGroups(const std::string& scope, float w) {
    bool perGame = scope != "shared";
    GroupTitle(perGame ? "Display for this game" : "All games: display",
               perGame ? "Turn \"Shared\" off to give this game its own value; on, it follows Settings > All games."
                       : "Every game uses these unless its own page says otherwise. Read when a game starts; the window's place stays with each game.", w);
    if (!SettingsReady(w)) return;
    BeginBoxedList(w);
    for (auto& d : DisplayDefs()) SettingRow(scope, d);
    EndBoxedList();
    GroupTitle(perGame ? "Controls for this game" : "All games: controls",
               perGame ? "Keys are changed in the game's keybinds.ini for now; here a key can follow the shared one or keep this game's."
                       : "The mouse and the keys every game has (move, camera, jump, pause). Other keys stay with each game. Keys are changed in keybinds.ini for now.", w);
    BeginBoxedList(w);
    for (auto& d : ControlDefs()) SettingRow(scope, d);
    if (!perGame) {
        float bwO = BtnW("Open", Ico::Open);
        Row("Shared settings folder", A.set.sharedFolder, bwO, [&] {
            if (PillButton("Open##sf", Kind::Normal, ImVec2(bwO, 0), false, Ico::Open)) OpenFolderOf(A.set.sharedFolder);
        }, 0, true);
    }
    EndBoxedList();
}

// Files: the selected game's four places as one boxed list. Each row: the slot as its title, where it came from as a
// tag, the path in use as a Mono subtitle, and Browse / Open / Use found at the right. A row the engine flags gets a
// second row under it with the reason (and "Use found" when the engine suggests a place).
static void FilesScreen() {
    if (NoGame()) return;
    Game& g = *Sel();
    float w; float x = Column(1000.f, w);
    ImGui::BeginGroup();
    char meta[160]; snprintf(meta, sizeof meta, "%d%s%s", g.year, g.titleId.empty() ? "" : "   ·   ", g.titleId.c_str());
    ImGui::PushFont(A.fMono, A.S(15.f)); ImGui::PushStyleColor(ImGuiCol_Text, ColV(T->accent)); ImGui::TextUnformatted(meta); ImGui::PopStyleColor(); ImGui::PopFont();
    ImGui::PushFont(A.fHeadBold, A.S(30.f)); ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + w); ImGui::TextUnformatted(g.title.c_str()); ImGui::PopTextWrapPos(); ImGui::PopFont();
    { ImVec2 sp = ImGui::GetCursorScreenPos(); StateLine(ImGui::GetWindowDrawList(), sp, g, A.S(19.f)); ImGui::Dummy(ImVec2(0, A.S(28.f))); }
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + w);
    Wrapped("The launcher finds these by itself. Choose a place by hand when it is somewhere else; what you choose always wins, and is checked like a found file.", T->dimFg, 15.f);
    ImGui::PopTextWrapPos();
    GroupTitle("Game files", nullptr, w);
    BeginBoxedList(w);
    std::string bad = BadSlot(g);
    for (int i = 0; i < 4; i++) {
        std::string slot = kSlotKey[i], path = SlotPath(g, i);
        bool overridden = g.overrides.count(slot) > 0, isBad = bad == slot, hasSug = g.suggested.count(slot) > 0;
        std::string key = "g|" + g.id + "|" + slot, msg; unsigned msgCol = 0;
        bool has = RowMsg(key, msg, msgCol);
        unsigned tc; const char* tag = SourceWord(g, slot, path, tc);
        // EBOOT row: the engine's check (which build the file is) replaces the source word; its message goes below
        bool ebChk = i == 2 && !path.empty() && !g.ebootTag.empty();
        if (ebChk) { tag = g.ebootTag.c_str(); tc = g.ebootLevel == "ok" ? T->success : g.ebootLevel == "warn" ? T->warning : T->destructive; }
        bool folderSlot = i == 3, fileSlot = i == 2, both = i < 2;
        const char* lbF = both ? "Folder##bf" : "Browse##bf";
        const char* lbZ = both ? "File##bz" : "Browse##bz";
        float gap = A.S(6.f), wF = BtnW(lbF, Ico::Folder), wZ = BtnW(lbZ), wO = BtnW("Open##of"), wU = BtnW("Use found##uf");
        float ctrlW = ((folderSlot || both) ? wF + gap : 0) + ((fileSlot || both) ? wZ + gap : 0) + wO + (overridden ? gap + wU : 0);
        RowOpt o; o.mono = !has && !path.empty(); o.tag = tag; o.tagCol = tc;
        o.subCol = has ? msgCol : path.empty() ? T->dimFg : isBad ? T->destructive : T->windowFg;
        ImGui::PushID(slot.c_str());
        std::string tagv = "ov|" + g.id + "|" + slot;
        RowX(kSlotLabel[i], has ? msg : path.empty() ? "Nothing found yet" : path, ctrlW, [&] {
            ImGui::BeginDisabled(EngineBusy());
            if (folderSlot || both) { if (PillButton(lbF, Kind::Normal, ImVec2(wF, 0), false, Ico::Folder)) BrowseFolder(-1, tagv); ImGui::SameLine(0, gap); }
            if (fileSlot || both) { if (PillButton(lbZ, Kind::Normal, ImVec2(wZ, 0), false)) BrowseFile(-1, tagv, fileSlot); ImGui::SameLine(0, gap); }
            ImGui::EndDisabled();
            if (PillButton("Open##of", Kind::Flat, ImVec2(wO, 0), false)) OpenFolderOf(path);
            if (overridden) {
                ImGui::SameLine(0, gap);
                ImGui::BeginDisabled(EngineBusy());
                if (PillButton("Use found##uf", Kind::Flat, ImVec2(wU, 0), false)) EngineClearOverride(g.id, slot);
                ImGui::EndDisabled();
            }
        }, o);
        if (isBad) {
            BodyRow([&] {
                Wrapped(g.reason.c_str(), T->destructive, 15.f);
                if (hasSug) {
                    std::string sug = g.suggested.at(slot);
                    std::string lab = "Use found: " + Fit(sug, w - A.S(180.f), A.fBodySemi, A.S(17.f));
                    ImGui::Dummy(ImVec2(0, A.S(2.f)));
                    ImGui::BeginDisabled(EngineBusy());
                    if (PillButton((lab + "##usefound").c_str(), Kind::Suggested, ImVec2(0, 0), false)) EngineClearOverride(g.id, slot);
                    ImGui::EndDisabled();
                }
            });
        } else if (ebChk && !has) {
            BodyRow([&] { Wrapped(g.ebootMsg.c_str(), g.ebootLevel == "ok" ? T->dimFg : tc, 15.f); });
        }
        ImGui::PopID();
    }
    EndBoxedList();
    ImGui::EndGroup();
    (void)x;
}

static std::string BaseDir() { const char* b = SDL_GetBasePath(); return b ? b : ""; }
// ASSUMPTION: the defaults sit next to the launcher until the installer decides (launcher-plan.md "Locations").
static std::string DefaultLocation(const std::string& key) {
    return BaseDir() + (key == "build" ? "games" : key == "saves" ? "saves" : key == "logs" ? "logs" : "art-cache");
}
static std::string LastPart(const std::string& p) {
    std::string t = p; while (t.size() > 1 && (t.back() == '/' || t.back() == '\\')) t.pop_back();
    size_t i = t.find_last_of("/\\"); return i == std::string::npos ? t : t.substr(i + 1);
}

// A list location (library folders, excluded subfolders, RPCS3 folders): one row per entry (folder name, full path in
// Adwaita Mono) with Remove, then the row that adds one.
static void LocList(const char* key, const std::vector<std::string>& items, const char* emptyTitle, const char* emptyText,
                    const char* addTitle, const char* addLabel, const char* addTag) {
    std::string rm;
    float rmW = BtnW("Remove");
    if (items.empty()) Row(emptyTitle, emptyText, 0, [] {});
    for (size_t i = 0; i < items.size(); i++) {
        ImGui::PushID((int)i);
        Row(LastPart(items[i]).c_str(), items[i], rmW, [&] {
            ImGui::BeginDisabled(EngineBusy());
            if (PillButton("Remove##rm", Kind::Flat, ImVec2(rmW, 0), false)) rm = items[i];
            ImGui::EndDisabled();
        }, 0, true);
        ImGui::PopID();
    }
    std::string msg; unsigned col = 0;
    bool has = RowMsg(std::string("l|") + key, msg, col);
    float aw = BtnW(addLabel, Ico::Plus);
    Row(addTitle, has ? msg : "", aw, [&] {
        ImGui::BeginDisabled(EngineBusy());
        if (PillButton(addLabel, Kind::Normal, ImVec2(aw, 0), false, Ico::Plus)) BrowseFolder(-1, addTag);
        ImGui::EndDisabled();
    }, col);
    if (!rm.empty()) EngineLocation(key, rm, "--remove");
}

static void PadTest() {
    int n = 0; SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (int i = 0; i < n; i++) if (!A.pads.count(ids[i])) { SDL_Gamepad* g = SDL_OpenGamepad(ids[i]); if (g) A.pads[ids[i]] = g; }
    for (auto it = A.pads.begin(); it != A.pads.end();) { if (!SDL_GamepadConnected(it->second)) { SDL_CloseGamepad(it->second); it = A.pads.erase(it); } else ++it; }
    SDL_free(ids);
    if (A.pads.empty()) { Wrapped("No controller found. Connect one and press any button: this list updates by itself.", T->dimFg, 15.f); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    static const struct { SDL_GamepadButton b; const char* n; } bt[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, "A"}, {SDL_GAMEPAD_BUTTON_EAST, "B"}, {SDL_GAMEPAD_BUTTON_WEST, "X"}, {SDL_GAMEPAD_BUTTON_NORTH, "Y"},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "LB"}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "RB"}, {SDL_GAMEPAD_BUTTON_BACK, "Back"}, {SDL_GAMEPAD_BUTTON_START, "Start"},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, "Up"}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, "Down"}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, "Left"}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "Right"}};
    for (auto& kv : A.pads) {
        ImGui::PushFont(A.fBodySemi, A.S(17.f)); ImGui::TextUnformatted(SDL_GetGamepadName(kv.second) ? SDL_GetGamepadName(kv.second) : "Controller"); ImGui::PopFont();
        ImVec2 q = ImGui::GetCursorScreenPos(); float x = 0;
        for (auto& b : bt) {
            bool on = SDL_GetGamepadButton(kv.second, b.b);
            ImVec2 ts = A.fBodySemi->CalcTextSizeA(A.S(15.f), FLT_MAX, 0, b.n); float w = ts.x + A.S(20.f);
            dl->AddRectFilled(ImVec2(q.x + x, q.y), ImVec2(q.x + x + w, q.y + A.S(28.f)), on ? Col(T->accentBg) : Col(T->buttonBg), A.S(14.f));
            dl->AddText(A.fBodySemi, A.S(15.f), ImVec2(q.x + x + A.S(10.f), q.y + (A.S(28.f) - ts.y) / 2), on ? Col(T->accentFg) : Col(T->windowFg), b.n);
            x += w + A.S(6.f);
        }
        ImGui::Dummy(ImVec2(x, A.S(34.f)));
        float lx = SDL_GetGamepadAxis(kv.second, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f, ly = SDL_GetGamepadAxis(kv.second, SDL_GAMEPAD_AXIS_LEFTY) / 32767.f;
        float rx = SDL_GetGamepadAxis(kv.second, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.f, ry = SDL_GetGamepadAxis(kv.second, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.f;
        char b[96]; snprintf(b, sizeof b, "Left stick %+.2f %+.2f      Right stick %+.2f %+.2f", lx, ly, rx, ry);
        ImGui::PushFont(A.fMono, A.S(14.f)); ImGui::PushStyleColor(ImGuiCol_Text, ColV(T->dimFg)); ImGui::TextUnformatted(b); ImGui::PopStyleColor(); ImGui::PopFont();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Light / dark: "system" follows SDL_GetSystemTheme() (re-read on SDL_EVENT_SYSTEM_THEME_CHANGED); an unknown
// system theme counts as light, libadwaita's default. The choice is stored by the engine (set-pref theme).
static void ApplyStyle();
static void ApplyTheme() {
    bool dark = A.themePref == "dark" || (A.themePref != "light" && SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
    T = dark ? &theme::kDark : &theme::kLight;
    if (A.scale > 0) ApplyStyle();
}
static void SetThemePref(const std::string& v) {
    if (v == A.themePref) return;
    A.themePref = v; ApplyTheme();
    RunEngine("l|theme", {"set-pref", "--library", A.libPath, "theme", v});
}

static std::string LauncherExe() {   // the running launcher: the release zip's name, else the build's own
    SDL_PathInfo pi; std::string b = BaseDir();
    for (const char* n : {"Qwarks-Awakening-launcher.exe", "launcher.exe", "launcher"})
        if (SDL_GetPathInfo((b + n).c_str(), &pi) && pi.type == SDL_PATHTYPE_FILE) return b + n;
    return b + "launcher.exe";
}
static bool JobMsg(const std::string& key, std::string& msg, unsigned& col) {
    if (JobBusy(key)) { msg = key == "kit|verify" ? "Checking every file..." : key == "kit|status" ? "Checking..." : "Asking GitHub..."; col = T->accent; return true; }
    auto it = A.kitMsg.find(key);
    if (it == A.kitMsg.end()) return false;
    msg = it->second.first; col = it->second.second; return true;
}
static void SetPref(const char* key, bool on) { RunEngine(std::string("l|") + key, {"set-pref", "--library", A.libPath, key, on ? "on" : "off"}); }

// Building (and the build kit): Release builds by default; "Debug information" (off by default) makes each build
// also write <game>.exe.debug beside the game, for the player's own crash reports, never uploaded. The launcher's own
// symbols are an optional release asset fetched only on request.
static void BuildingGroup(float w) {
    GroupTitle("Building", "How the launcher builds your games on this PC.", w);
    BeginBoxedList(w);
    {
        std::string msg; unsigned col = 0;
        bool has = RowMsg("l|debug_info", msg, col);
        Row("Debug information", has ? msg : "Off: games are built without debug information (smaller). On: each build also writes "
                                              "the game's debug information to a .debug file beside it, for your own crash reports. It "
                                              "stays on this PC and is never uploaded. Takes effect at the next build.",
            A.S(48.f), [&] {
            bool on = A.lib.debugInfo == "on";
            ImGui::BeginDisabled(EngineBusy());
            if (Switch("##debuginfo", &on)) SetPref("debug_info", on);
            ImGui::EndDisabled();
        }, col);
    }
    if (*LAUNCHER_KIT_SHA256) {
        const std::string& st = A.kit.state;
        std::string msg; unsigned col = 0;
        bool has = JobMsg("kit|verify", msg, col);
        std::string sub = st == "ready" ? (A.kit.release.empty() ? std::string("In place.") : "In place (release " + A.kit.release + ").") : st == "other" ? "Next to the launcher, but of another release: Build offers the right one."
                        : st == "damaged" ? "Files are missing: Build offers to download it again." : st == "unknown" ? "Checking..." : "Not next to the launcher: Build offers to download it.";
        Row("Build kit", has ? msg : sub, BtnW("Check files"), [&] {
            ImGui::BeginDisabled(JobBusy() || st == "missing" || st == "unknown");
            if (PillButton("Check files##kv", Kind::Normal, ImVec2(BtnW("Check files"), 0), false))
                RunJob("kit|verify", {"verify", "--root", A.kit.root, "--expect", LAUNCHER_KIT_SHA256});
            ImGui::EndDisabled();
        }, col);
    }
    {
        std::string msg; unsigned col = 0;
        bool has = JobMsg("sym|get", msg, col);
        Row("Launcher debug information", has ? msg : "For reporting a launcher crash: downloads the launcher's own symbols from this project's "
                                                      "GitHub release, only when you choose Download. Not needed to play.",
            BtnW("Download"), [&] {
            ImGui::BeginDisabled(JobBusy() || A.kit.symbolsUrl.empty());
            if (PillButton("Download##sym", Kind::Normal, ImVec2(BtnW("Download"), 0), false)) GetSymbols();
            ImGui::EndDisabled();
        }, col);
    }
    EndBoxedList();
}

// Updates: the opt-in release check (one anonymous request to GitHub's release list at start, off by default). The
// launcher never downloads a release or a game update by itself; title updates come from the player's own copy.
static void UpdatesGroup(float w) {
    GroupTitle("Updates", "Game updates come from your own copy (Files). The launcher never downloads an update by itself.", w);
    BeginBoxedList(w);
    {
        std::string msg; unsigned col = 0;
        bool has = RowMsg("l|update_check", msg, col);
        Row("Check for new releases", has ? msg : "Off by default. When on, the launcher asks GitHub once at start which release of the port "
                                                   "is newest (one request, no account; nothing about you or your games is sent) and tells you here.",
            A.S(48.f), [&] {
            bool on = A.lib.updateCheck == "on";
            ImGui::BeginDisabled(EngineBusy());
            if (Switch("##updcheck", &on)) SetPref("update_check", on);
            ImGui::EndDisabled();
        }, col);
    }
    {
        std::string msg; unsigned col = 0;
        bool has = JobMsg("upd|check", msg, col);
        bool newer = !A.newReleaseUrl.empty();
        float cw = BtnW("Check now") + (newer ? BtnW("Open release page") + A.S(6.f) : 0.f);
        Row("Launcher " LAUNCHER_VERSION, has ? msg : "Check once now, without turning the switch on.", cw, [&] {
            ImGui::BeginDisabled(JobBusy());
            if (PillButton("Check now##uc", Kind::Normal, ImVec2(BtnW("Check now"), 0), false)) CheckRelease(false);
            ImGui::EndDisabled();
            if (newer) {
                ImGui::SameLine(0, A.S(6.f));
                if (PillButton("Open release page##ur", Kind::Suggested, ImVec2(BtnW("Open release page"), 0), false)) {
                    if (A.newReleaseUrl.rfind("https://github.com/", 0) == 0) SDL_OpenURL(A.newReleaseUrl.c_str());
                }
            }
        }, col);
    }
    EndBoxedList();
}

// Online (docs/ONLINE.md): the shared repsn.ini / community.ini every game of this port reads. Off by default;
// one method at a time; the launcher only writes the files (no game code changes) and connects to nothing itself.
static void OnlineGroup(float w) {
    GroupTitle("Online", "Shared by every game, in a folder other ports read too. Off by default. The games of this port do not go online "
                         "yet: these settings are kept for when they do.", w);
    Online& o = A.online;
    if (!o.loaded) {
        if (!o.loading && !EngineBusy() && o.err.empty()) LoadOnline();
        BeginBoxedList(w);
        if (!o.err.empty()) Row("The online settings could not be read", o.err, 0, [] {}, T->destructive);
        else Row("Reading the online settings...", "", 0, [] {});
        EndBoxedList();
        return;
    }
    BeginBoxedList(w);
    {
        static const std::vector<std::string> kLabels = {"Off", "re-psn", "Community server"};
        static const char* kVals[3] = {"off", "repsn", "community"};
        int cur = o.method == "repsn" ? 1 : o.method == "community" ? 2 : 0;
        std::string msg; unsigned col = 0;
        bool has = RowMsg("o|method", msg, col);
        std::string sub = o.method == "both" ? "Both files turn a method on; choose one." : cur == 0 ? "No game connects to any server."
                        : cur == 1 ? "Games use the re-psn server below." : "Games use the chosen community server. Port-exclusive features do not work there.";
        Row("Online play", has ? msg : sub, ToggleGroupW(kLabels), [&] {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - A.S(1.f));
            ImGui::BeginDisabled(EngineBusy());
            if (ToggleGroup("onlinemethod", kLabels, &cur)) SetOnline("o|method", {"--method", kVals[cur]});
            ImGui::EndDisabled();
        }, col);
    }
    {
        std::string msg; unsigned col = 0;
        bool has = RowMsg("o|repsn", msg, col);
        float iw = std::min(A.S(240.f), w * 0.3f), bw = BtnW("Save");
        Row("re-psn", has ? msg : "Server (host or host:port) and your online name (3 to 16 letters, digits, - or _).", iw * 2 + bw + A.S(12.f), [&] {
            ImGui::SetNextItemWidth(iw); ImGui::InputTextWithHint("##rserver", "server", o.serverBuf, sizeof o.serverBuf);
            ImGui::SameLine(0, A.S(6.f));
            ImGui::SetNextItemWidth(iw); ImGui::InputTextWithHint("##rname", "online name", o.nameBuf, sizeof o.nameBuf);
            ImGui::SameLine(0, A.S(6.f));
            ImGui::BeginDisabled(EngineBusy() || (!*o.serverBuf && !*o.nameBuf));
            if (PillButton("Save##rs", Kind::Normal, ImVec2(bw, 0), false)) {
                std::vector<std::string> a;
                if (*o.serverBuf) { a.push_back("--server"); a.push_back(o.serverBuf); }
                if (*o.nameBuf) { a.push_back("--name"); a.push_back(o.nameBuf); }
                SetOnline("o|repsn", a);
            }
            ImGui::EndDisabled();
        }, col);
    }
    for (auto& p : o.presets) {
        ImGui::PushID(p.name.c_str());
        bool act = p.name == o.active;
        std::string key = "o|p|" + p.name, msg; unsigned col = 0;
        bool has = RowMsg(key, msg, col);
        RowOpt ro; ro.tag = act ? "In use" : nullptr; ro.tagCol = T->accent; ro.mono = !has;
        if (has) ro.subCol = col;
        float cw = (act ? 0.f : BtnW("Use") + A.S(6.f)) + BtnW("Remove");
        RowX(p.name.c_str(), has ? msg : p.address + (p.player.empty() ? "" : "  ·  " + p.player), cw, [&] {
            ImGui::BeginDisabled(EngineBusy());
            if (!act) { if (PillButton("Use##pu", Kind::Normal, ImVec2(BtnW("Use"), 0), false)) SetOnline(key, {"--active", p.name}); ImGui::SameLine(0, A.S(6.f)); }
            if (PillButton("Remove##pr", Kind::Flat, ImVec2(BtnW("Remove"), 0), false)) SetOnline(key, {"--delete-preset", p.name});
            ImGui::EndDisabled();
        }, ro);
        ImGui::PopID();
    }
    {
        std::string msg; unsigned col = 0;
        bool has = RowMsg("o|add", msg, col);
        float iw = std::min(A.S(200.f), w * 0.26f), bw = BtnW("Add");
        Row(o.presets.empty() ? "Community server" : "Add a community server", has ? msg : "A short name (a-z, 0-9, - or _) and the server's address.",
            iw * 2 + bw + A.S(12.f), [&] {
            ImGui::SetNextItemWidth(iw); ImGui::InputTextWithHint("##pname", "name", o.pNameBuf, sizeof o.pNameBuf);
            ImGui::SameLine(0, A.S(6.f));
            ImGui::SetNextItemWidth(iw); ImGui::InputTextWithHint("##paddr", "address", o.pAddrBuf, sizeof o.pAddrBuf);
            ImGui::SameLine(0, A.S(6.f));
            ImGui::BeginDisabled(EngineBusy() || !*o.pNameBuf || !*o.pAddrBuf);
            if (PillButton("Add##pa", Kind::Normal, ImVec2(bw, 0), false)) {
                std::vector<std::string> a = {"--preset", o.pNameBuf, "--address", o.pAddrBuf};
                if (o.presets.empty()) { a.push_back("--active"); a.push_back(o.pNameBuf); }   // the first one is the one in use
                SetOnline("o|add", a);
            }
            ImGui::EndDisabled();
        }, col);
    }
    {
        float bw = BtnW("Open", Ico::Open);
        Row("Folder", o.folder, bw, [&] { if (PillButton("Open##of", Kind::Normal, ImVec2(bw, 0), false, Ico::Open)) OpenFolderOf(o.folder + "/repsn.ini"); }, 0, true);
    }
    EndBoxedList();
}

static void SettingsScreen() {
    float side = A.S(24.f), avail = ImGui::GetContentRegionAvail().x;
    float w = std::min(avail - side * 2, A.S(760.f)), x = std::max(side, (avail - w) / 2);
    ImGui::SetCursorPos(ImVec2(x, A.S(10.f)));
    ImGui::BeginGroup();
    ImGui::PushID("settings");

    GroupTitle("Appearance", nullptr, w);
    BeginBoxedList(w);
    {
        static const char* kModes[3] = {"system", "light", "dark"};
        static const std::vector<std::string> kLabels = {"System", "Light", "Dark"};
        int cur = A.themePref == "light" ? 1 : A.themePref == "dark" ? 2 : 0;
        std::string msg; unsigned col = 0;
        bool has = RowMsg("l|theme", msg, col);
        Row("Style", has ? msg : std::string(A.themePref == "system" ? "Follows your system's light or dark setting." : "Always this style, whatever the system uses."),
            ToggleGroupW(kLabels), [&] {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - A.S(1.f));   // the group is 40 px high, the row's control slot 38
            if (ToggleGroup("style", kLabels, &cur)) SetThemePref(kModes[cur]);
        }, col);
    }
    EndBoxedList();

    SettingGroups("shared", w);   // All games: display and controls

    const Locations& L = A.lib.loc;
    GroupTitle("Game library folders", "The launcher looks in these folders, and the folders inside them, for your game files.", w);
    BeginBoxedList(w);
    LocList("library_folders", L.libraryFolders, "No folders yet", "The launcher only looks in RPCS3's folders.", "Add a game folder", "Add folder##lf", "loc|library_folders|--add");
    EndBoxedList();
    GroupTitle("Left out", "Subfolders of the game folders that the launcher skips.", w);
    BeginBoxedList(w);
    LocList("excluded", L.excluded, "Nothing is left out", "", "Leave out a subfolder", "Exclude##ex", "loc|excluded|--add");
    EndBoxedList();

    GroupTitle("RPCS3", "Folders of an RPCS3 install to read: games, installed updates and saves, read only. Licence files are never touched.", w);
    BeginBoxedList(w);
    LocList("rpcs3_folders", L.rpcs3Folders, "No RPCS3 folder chosen", "", "Add an RPCS3 folder", "Add folder##rf", "loc|rpcs3_folders|--add");
    {
        std::string msg; unsigned col = 0;
        bool has = RowMsg("l|rpcs3_auto", msg, col);
        Row("Look in the usual places", has ? msg : "Beside the launcher, Downloads, Desktop, Documents and the usual install folders.", A.S(48.f), [&] {
            bool on = L.rpcs3Auto;
            ImGui::BeginDisabled(EngineBusy());
            if (Switch("##rpcs3auto", &on)) EngineLocation("rpcs3_auto", on ? "true" : "false");
            ImGui::EndDisabled();
        }, col);
    }
    EndBoxedList();

    GroupTitle("Folders", "Where the launcher puts what it makes. Each one can go back to its default.", w);
    BeginBoxedList(w);
    struct One { const char* key; const char* title; const std::string* value; };
    const One ones[4] = {{"build", "Built games", &L.build}, {"saves", "Saves and settings", &L.saves}, {"logs", "Logs", &L.logs}, {"art_cache", "Art cache", &L.artCache}};
    float bwB = BtnW("Browse", Ico::Folder), bwO = BtnW("Open", Ico::Open), bwR = BtnW("Reset"), gap = A.S(6.f);
    for (const One& o : ones) {
        ImGui::PushID(o.key);
        bool set = !o.value->empty();
        std::string def = DefaultLocation(o.key), msg; unsigned col = 0;
        bool has = RowMsg(std::string("l|") + o.key, msg, col);
        Row(o.title, has ? msg : set ? *o.value : "Default: " + def, bwB + bwO + bwR + gap * 2, [&] {
            ImGui::BeginDisabled(EngineBusy());
            if (PillButton("Browse##b", Kind::Normal, ImVec2(bwB, 0), false, Ico::Folder)) BrowseFolder(-1, std::string("loc|") + o.key + "|");
            ImGui::EndDisabled();
            ImGui::SameLine(0, gap);
            if (PillButton("Open##o", Kind::Normal, ImVec2(bwO, 0), false, Ico::Open)) OpenFolderOf(set ? *o.value : def);
            ImGui::SameLine(0, gap);
            ImGui::BeginDisabled(EngineBusy() || !set);
            if (PillButton("Reset##r", Kind::Flat, ImVec2(bwR, 0), false)) EngineLocation(o.key, "", "--reset");
            ImGui::EndDisabled();
        }, col, !has);
        ImGui::PopID();
    }
    EndBoxedList();

    BuildingGroup(w);
    OnlineGroup(w);
    UpdatesGroup(w);

    GroupTitle("Cover art", nullptr, w);
    BeginBoxedList(w);
    Row("Fetch cover art from the internet",
        "Off by default. When on, cover images are downloaded from IGDB.com by their id and kept in the art cache; no account is "
        "needed. Until then no art is fetched and the launcher uses your own disc art. Preview: not wired yet, nothing is fetched.",
        A.S(48.f), [&] { Switch("##artfetch", &A.artFetch); });
    EndBoxedList();

    GroupTitle("Controller", "Press buttons and move the sticks to check your controller.", w);
    BeginBoxedList(w);
    BodyRow([] { PadTest(); });
    EndBoxedList();

    ImGui::PopID();
    ImGui::EndGroup();
}

// About (libadwaita about page): the Russo One wordmark, the versions, the licence and the credits. Only what the
// launcher already said elsewhere (pass 1's About section, NOTICE.md); no new claims.
static void AboutScreen() {
    float w; float x = Column(640.f, w);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0, A.S(18.f)));
    const char* mark = "QWARK'S AWAKENING";
    float mpx = std::min(A.S(40.f), w / 11.f);
    ImVec2 ms = A.fMark->CalcTextSizeA(mpx, FLT_MAX, 0, mark);
    ImGui::SetCursorPosX(x + (w - ms.x) / 2);
    ImGui::PushFont(A.fMark, mpx); ImGui::TextUnformatted(mark); ImGui::PopFont();
    auto centred = [&](const std::string& t, ImFont* f, float px, unsigned col) {
        ImVec2 ts = f->CalcTextSizeA(A.S(px), FLT_MAX, 0, t.c_str());
        ImGui::SetCursorPosX(x + std::max(0.f, (w - ts.x) / 2));
        ImGui::PushFont(f, A.S(px)); ImGui::PushStyleColor(ImGuiCol_Text, ColV(col)); ImGui::TextUnformatted(t.c_str()); ImGui::PopStyleColor(); ImGui::PopFont();
    };
    centred("Ratchet & Clank (PS3) // Unofficial PC Port", A.fBody, 17.f, T->windowFg);   // the banner's line
    ImGui::Dummy(ImVec2(0, A.S(4.f)));
    char b[160];
    snprintf(b, sizeof b, "Launcher %s (preview)   ·   port %s", LAUNCHER_VERSION, A.lib.portVersion.empty() ? "unknown" : A.lib.portVersion.c_str());
    centred(b, A.fMono, 14.f, T->accent);
    ImGui::SetCursorPosX(x);
    GroupTitle("Licence", nullptr, w);
    BeginBoxedList(w);
    PropRow("This launcher and the port's own code", "MIT licence, Copyright (c) 2026 undecuple. It covers the port's own source code, tools and documentation only.");
    EndBoxedList();
    ImGui::SetCursorPosX(x);
    GroupTitle("Credits", nullptr, w);
    BeginBoxedList(w);
    PropRow("Dear ImGui", "MIT, Copyright (c) 2014-2025 Omar Cornut");
    PropRow("SDL3", "zlib, Copyright (c) 1997-2025 Sam Lantinga and SDL contributors");
    PropRow("Fonts", "Adwaita Sans and Adwaita Mono (GNOME; from Inter and Iosevka), Russo One (Jovanny Lemonad): SIL Open Font License 1.1");
    PropRow("Cover art", "IGDB.com, only when you turn the cover art switch on");
    EndBoxedList();
    ImGui::SetCursorPosX(x);
    ImGui::Dummy(ImVec2(0, A.S(6.f)));
    ImGui::SetCursorPosX(x);
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + w);
    Wrapped("Game titles and art belong to their owners. This project is not affiliated with them; it needs your own game files.", T->dimFg, 14.f);
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
}

// ---------------------------------------------------------------------------------------------------------------
static void DrawBackdrop(float W, float H) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), Col(T->windowBg));
}

static const float kHeaderH = 56.f, kFooterH = 40.f;
static const float kCollapseW = 600.f;   // below this window width (logical px) the split view is one column
// Navigation split view (libadwaita AdwNavigationSplitView): the sidebar's pages. Game Details is a page under Library.
static const struct { const char* label; Ico icon; App::Screen screen; } kPages[4] = {
    {"Library", Ico::Library, App::ScLibrary}, {"Files", Ico::Folder, App::ScFiles}, {"Settings", Ico::Gear, App::ScSettings}, {"About", Ico::Info, App::ScAbout}};
// Back (the header's Back button, Esc, controller B): Game Details goes back to the Library; any other page goes to
// the sidebar (in one column, the sidebar becomes the page; side by side, focus moves to the page's sidebar row).
static void GoBack() {
    if (A.screen == App::ScDetails) { A.screen = App::ScLibrary; RequestFocus(App::FocusContent); return; }
    if (A.collapsed) A.showSidebar = true;
    RequestFocus(App::FocusSidebar);
}
static const char* PageTitle() {
    if (A.collapsed && A.showSidebar) return "Qwark's Awakening";
    switch (A.screen) {
        case App::ScLibrary: return "Library";
        case App::ScDetails: return "Game Details";
        case App::ScFiles: return "Game Files";
        case App::ScSettings: return "Settings";
        case App::ScAbout: return "About";
    }
    return "";
}
// The sidebar pane's rows, `w` wide, in the current window (a child the caller opened).
static void SidebarItems(float w) {
    ImGui::SetCursorPos(ImVec2(A.S(8.f), A.S(8.f)));
    App::Screen cur = A.screen == App::ScDetails ? App::ScLibrary : A.screen;
    for (auto& pg : kPages) {
        ImGui::SetCursorPosX(A.S(8.f));
        if (SidebarItem(pg.label, pg.icon, cur == pg.screen, w - A.S(16.f))) GoPage(pg.screen);
        ImGui::Dummy(ImVec2(0, A.S(2.f)));
    }
}
// Header bar (libadwaita) over the content pane, from x0 to W: the page title centred in Adwaita Sans Bold, Back at the
// left where there is somewhere to go back to, and the main menu (three lines) at the right: Library, Settings, About.
// F10 or the controller's Start opens it.
static void HeaderBar(float x0, float W) {
    float hh = A.S(kHeaderH);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    dl->AddRectFilled(ImVec2(wp.x + x0, wp.y), ImVec2(wp.x + W, wp.y + hh), Col(T->headerbarBg));
    dl->AddLine(ImVec2(wp.x + x0, wp.y + hh - 1), ImVec2(wp.x + W, wp.y + hh - 1), Col(T->border), 1.f);
    const char* t = PageTitle();
    ImVec2 ts = A.fHeadBold->CalcTextSizeA(A.S(18.f), FLT_MAX, 0, t);
    dl->AddText(A.fHeadBold, A.S(18.f), ImVec2(wp.x + x0 + (W - x0 - ts.x) / 2, wp.y + (hh - ts.y) / 2), Col(T->windowFg), t);
    float bh = A.S(38.f), by = (hh - bh) / 2;
    bool back = A.screen == App::ScDetails || (A.collapsed && !A.showSidebar);
    if (back) {
        ImGui::SetCursorPos(ImVec2(x0 + A.S(8.f), by));
        bool toLib = A.screen == App::ScDetails;
        if (PillButton(toLib && !A.collapsed ? "Library##back" : "##back", Kind::Flat, ImVec2(toLib && !A.collapsed ? 0 : A.S(42.f), bh), false, Ico::Back)) GoBack();
        if (FocusDue(App::FocusBack)) TakeFocus();
    }
    ImGui::SetCursorPos(ImVec2(W - A.S(8.f) - A.S(42.f), by));
    if (PillButton("##mainmenu", Kind::Flat, ImVec2(A.S(42.f), bh), false, Ico::Menu) ||
        ImGui::IsKeyPressed(ImGuiKey_F10) || ImGui::IsKeyPressed(ImGuiKey_GamepadStart)) ImGui::OpenPopup("mainmenu");
    ImGui::SetNextWindowPos(ImVec2(wp.x + W - A.S(8.f), wp.y + hh - A.S(4.f)), ImGuiCond_Always, ImVec2(1.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(A.S(6.f), A.S(6.f)));
    if (ImGui::BeginPopup("mainmenu")) {
        ImGui::PushFont(A.fBody, A.S(17.f));
        auto item = [&](const char* label) { return ImGui::Selectable(label, false, 0, ImVec2(A.S(240.f), A.S(34.f))); };
        if (item("Library")) GoPage(App::ScLibrary);
        if (item("Settings")) GoPage(App::ScSettings);
        if (item("About Qwark's Awakening")) GoPage(App::ScAbout);
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

// Footer under the content pane: games ready at the left, the keys in the middle (dropped when the pane is too narrow),
// the build queue at the right.
static void Footer(float x0, float W, float H) {
    float fh = A.S(kFooterH);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    dl->AddRectFilled(ImVec2(wp.x + x0, wp.y + H - fh), ImVec2(wp.x + W, wp.y + H), Col(T->headerbarBg));
    dl->AddLine(ImVec2(wp.x + x0, wp.y + H - fh), ImVec2(wp.x + W, wp.y + H - fh), Col(T->border), 1.f);
    int ready = 0; for (auto& g : A.lib.games) if (Playable(g)) ready++;
    char b[96]; snprintf(b, sizeof b, "%d of %d games ready", ready, (int)A.lib.games.size());
    float px = A.S(15.f), y = wp.y + H - fh;
    ImVec2 ls = A.fBodySemi->CalcTextSizeA(px, FLT_MAX, 0, b);
    dl->AddText(A.fBodySemi, px, ImVec2(wp.x + x0 + A.S(20.f), y + (fh - ls.y) / 2), Col(T->windowFg), b);
    std::string q = "Build queue: empty";
    bool busy = TaskRunning();
    if (busy) {
        std::lock_guard<std::mutex> l(A.task.m); char c[200];
        if (A.task.kind == "build") snprintf(c, sizeof c, "Building %s · step %d of 5 · %d%%", (A.task.curGame.empty() ? A.task.game : A.task.curGame).c_str(), StageOf(A.task.step) + 1, (int)A.task.pct);
        else if (A.task.kind == "kit") snprintf(c, sizeof c, "Downloading the build kit · %d%%", (int)A.task.pct);
        else snprintf(c, sizeof c, "Playing %s", A.task.game.c_str());
        q = c;
    }
    ImVec2 qs = A.fBodySemi->CalcTextSizeA(px, FLT_MAX, 0, q.c_str());
    float room = W - x0 - A.S(40.f) - ls.x;
    if (qs.x + A.S(20.f) <= room)
        dl->AddText(A.fBodySemi, px, ImVec2(wp.x + W - A.S(20.f) - qs.x, y + (fh - qs.y) / 2), busy ? Col(T->accent) : Col(T->dimFg), q.c_str());
    const char* mid = "Enter / A: choose     Esc / B: back     arrows / D-pad: move     F10 / Start: menu";
    ImVec2 ms = A.fBody->CalcTextSizeA(px, FLT_MAX, 0, mid);
    float cx = wp.x + x0 + (W - x0 - ms.x) / 2;
    if (cx > wp.x + x0 + A.S(40.f) + ls.x && cx + ms.x < wp.x + W - A.S(40.f) - qs.x)
        dl->AddText(A.fBody, px, ImVec2(cx, y + (fh - ms.y) / 2), Col(T->dimFg), mid);
}
// Toast (libadwaita): a dark pill at the bottom centre, above the footer, that fades out. Fed by Say().
static void DrawToast(float W, float H) {
    double left = A.toast.until - ImGui::GetTime();
    if (left <= 0 || A.toast.text.empty()) return;
    float a = (float)std::min(1.0, left / 0.3);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float px = A.S(16.f), maxW = std::min(W - A.S(80.f), A.S(640.f));
    ImVec2 ts = A.fBody->CalcTextSizeA(px, FLT_MAX, maxW, A.toast.text.c_str());
    float w = ts.x + A.S(40.f), h = ts.y + A.S(20.f);
    ImVec2 p((W - w) / 2, H - A.S(kFooterH) - A.S(16.f) - h);
    dl->AddRectFilled(ImVec2(p.x, p.y + A.S(2.f)), ImVec2(p.x + w, p.y + h + A.S(2.f)), Col(T->shade, a), h / 2);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(T->toastBg, a), std::min(h / 2, A.S(20.f)));
    dl->AddText(A.fBody, px, ImVec2(p.x + A.S(20.f), p.y + A.S(10.f)), Col(T->toastFg, a), A.toast.text.c_str(), nullptr, maxW);
}

static void ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.ScaleAllSizes(A.scale);
    s.FontSizeBase = 17.f * A.scale;
    s.WindowBorderSize = 0; s.FrameBorderSize = 1; s.PopupBorderSize = 1; s.FrameRounding = A.S(theme::kRadiusButton); s.ChildRounding = A.S(theme::kRadiusCard);
    s.PopupRounding = A.S(theme::kRadiusCard); s.GrabRounding = A.S(theme::kRadiusButton); s.ScrollbarRounding = A.S(8.f);
    s.FramePadding = ImVec2(A.S(14.f), A.S(9.f)); s.ItemSpacing = ImVec2(A.S(10.f), A.S(8.f)); s.ScrollbarSize = A.S(10.f); s.ItemInnerSpacing = ImVec2(A.S(10.f), A.S(4.f));
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ColV(T->windowFg); c[ImGuiCol_TextDisabled] = ColV(T->dimFg); c[ImGuiCol_WindowBg] = ColV(T->windowBg, 0); c[ImGuiCol_ChildBg] = ColV(T->windowBg, 0);
    c[ImGuiCol_PopupBg] = ColV(T->popoverBg); c[ImGuiCol_Border] = ColV(T->border); c[ImGuiCol_FrameBg] = ColV(T->viewBg); c[ImGuiCol_FrameBgHovered] = ColV(T->viewBg);
    c[ImGuiCol_FrameBgActive] = ColV(T->viewBg); c[ImGuiCol_NavCursor] = ColV(T->accent); c[ImGuiCol_ScrollbarBg] = ColV(T->windowBg, 0);
    c[ImGuiCol_ScrollbarGrab] = ColV(T->buttonActive); c[ImGuiCol_ScrollbarGrabHovered] = ColV(T->dimFg); c[ImGuiCol_ScrollbarGrabActive] = ColV(T->windowFg);
    c[ImGuiCol_Header] = ColV(T->buttonBg); c[ImGuiCol_HeaderHovered] = ColV(T->buttonHover); c[ImGuiCol_HeaderActive] = ColV(T->buttonActive);
    c[ImGuiCol_Button] = ColV(T->buttonBg); c[ImGuiCol_ButtonHovered] = ColV(T->buttonHover); c[ImGuiCol_ButtonActive] = ColV(T->buttonActive);
    c[ImGuiCol_CheckMark] = ColV(T->accentBg); c[ImGuiCol_Separator] = ColV(T->border); c[ImGuiCol_TextSelectedBg] = ColV(T->accentBg, 0.35f);
    c[ImGuiCol_ModalWindowDimBg] = ColV(T->shade); c[ImGuiCol_InputTextCursor] = ColV(T->windowFg);
}

static void Frame(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float ns = std::clamp(H / 720.f, 0.75f, 3.f);
    if (std::fabs(ns - A.scale) > 0.01f) { A.scale = ns; ApplyStyle(); }
    PumpDialog();
    PumpEngine();
    PumpTask();
    PumpJob();
    for (auto& g : A.lib.games) {   // art regains colour: a short fade, the one motion that matters
        float d = g.colourTarget - g.colour, step = dt / 0.9f;
        g.colour = A.shotMode && A.firstFrame ? g.colourTarget : (std::fabs(d) < step ? g.colourTarget : g.colour + (d > 0 ? step : -step));
    }
    A.collapsed = W < A.S(kCollapseW);
    static int frameNo = 0;   // the first two frames: ImGui's own focus of a new window (its first item, the sidebar) runs too, so ask again
    if (frameNo++ < 2) { A.focusReq = App::FocusContent; focusAge = 0; A.focusShow = true; A.focusFrame = -1; }
    if (A.kit.open) A.focusReq = App::FocusNone;   // the kit dialog is modal: nothing behind it takes keyboard focus
    A.sidebarIds.clear(); A.navLabels.clear();
    DrawBackdrop(W, H);
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    float top = A.S(kHeaderH), fh = A.S(kFooterH);
    bool sidebarPage = A.collapsed && A.showSidebar;
    float sw = A.collapsed ? 0.f : std::clamp(W * 0.25f, A.S(200.f), A.S(260.f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!A.collapsed) {   // the sidebar pane: its own flat header with the app's name, then the pages, full height
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(sw, H), Col(T->sidebarBg));
        dl->AddLine(ImVec2(sw - 1, 0), ImVec2(sw - 1, H), Col(T->border), 1.f);
        const char* app = "Qwark's Awakening";
        ImVec2 ts = A.fHeadBold->CalcTextSizeA(A.S(18.f), FLT_MAX, 0, app);
        dl->AddText(A.fHeadBold, A.S(18.f), ImVec2((sw - ts.x) / 2, (top - ts.y) / 2), Col(T->windowFg), app);
        ImGui::SetCursorPos(ImVec2(0, top));
        ImGui::BeginChild("sidebar", ImVec2(sw - 1, H - top), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        SidebarItems(sw - 1);
        ImGui::EndChild();
    }
    HeaderBar(sw, W);
    ImGui::SetCursorPos(ImVec2(sw, top));
    if (sidebarPage) {   // one column: the sidebar is the page
        dl->AddRectFilled(ImVec2(0, top), ImVec2(W, H - fh), Col(T->sidebarBg));
        ImGui::BeginChild("sidebar", ImVec2(W, H - top - fh), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        SidebarItems(W);
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("body", ImVec2(W - sw, H - top - fh), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        A.inBody = true;
        if (A.screen == App::ScLibrary) LibraryScreen(W - sw);
        else if (A.screen == App::ScDetails) DetailsScreen();
        else if (A.screen == App::ScFiles) FilesScreen();
        else if (A.screen == App::ScAbout) AboutScreen();
        else SettingsScreen();
        A.inBody = false;
        if (A.shotScroll >= 0 && A.shotMode) ImGui::SetScrollY(A.S(A.shotScroll));
        ImGui::Dummy(ImVec2(0, A.S(16.f)));
        ImGui::EndChild();
    }
    Footer(sw, W, H);
    // Esc / controller B: back (see GoBack) when nothing else wants the key; in the sidebar it does nothing (the top)
    bool inSidebar = std::find(A.sidebarIds.begin(), A.sidebarIds.end(), ImGui::GetFocusID()) != A.sidebarIds.end();
    bool free = !A.addOpen && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !ImGui::IsAnyItemActive();
    if (free && !inSidebar && !sidebarPage && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))) GoBack();
    // Side by side, Left at the content's left edge goes to the sidebar and Right from the sidebar goes to the content.
    // ImGui's own arrows only reach items that line up with the focused one, so an arrow that moved nothing within two
    // frames (the focused id unchanged) is taken as "at the edge".
    static struct { ImGuiID id = 0; int dir = 0, age = 0; } edge;
    if (edge.dir && ++edge.age >= 2) {
        if (ImGui::GetFocusID() == edge.id) RequestFocus(edge.dir < 0 ? App::FocusSidebar : App::FocusContent);
        edge.dir = 0;
    }
    if (free && !A.collapsed && ImGui::GetFocusID()) {
        bool left = ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft);
        bool right = ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight);
        if ((left && !inSidebar) || (right && inSidebar)) { edge.id = ImGui::GetFocusID(); edge.dir = left ? -1 : 1; edge.age = 0; }
    }
    ImGui::End();
    if (A.addOpen) AddDrawer(W, H);
    if (A.kit.open) KitDialog(W, H);
    DrawToast(W, H);
    if (A.firstFrame) ImGui::SetNavCursorVisible(true);
    A.firstFrame = false;
    // a content page with nothing to focus (About): the page's sidebar row takes it, or in one column the Back button;
    // any other request nothing took is dropped
    if (A.focusReq == App::FocusContent && focusAge >= 1) { A.focusReq = A.collapsed ? App::FocusBack : App::FocusSidebar; focusAge = 0; }
    else if (A.focusReq != App::FocusNone && focusAge++ >= 1) A.focusReq = App::FocusNone;
}

// ---------------------------------------------------------------------------------------------------------------
static ImFont* LoadFont(const char* file) {
    std::string p = A.fontDir + "/" + file;
    ImFont* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(p.c_str(), 18.f);
    if (!f) { fprintf(stderr, "launcher: cannot load font %s\n", p.c_str()); }
    return f;
}

// Arguments are UTF-8 on every platform. On Windows SDL_main.h renames this function to SDL_main and supplies the
// program's entry point (WinMain for the -mwindows release build), which calls SDL_RunApp(0, NULL, ...): SDL then reads
// the wide command line (GetCommandLineW) and converts each argument to UTF-8, so a --library or --engine path with
// non-ASCII characters arrives intact (no ANSI code page argv; the same effect as -municode with wmain). Every path
// is then opened through UTF-8 APIs (SDL_LoadFile, SDL_GetPathInfo, SDL_CreateProcess, Dear ImGui's file open), and
// the environment is read with SDL_getenv (UTF-8) rather than getenv (ANSI on Windows).
// test_launcher_src.py keeps SDL_main.h included, SDL_MAIN_HANDLED undefined and getenv unused.
int main(int argc, char** argv) {
    std::string shot, screenName = "library", selectId, engine, themeArg, keyList, replay, kitUrl, relApi, symUrl, kitState; int W = 1280, H = 720; float progress = -1.f;
    const char* base = SDL_GetBasePath();
    A.libPath = std::string(base ? base : "") + "library.json";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i]; auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--library") A.libPath = next();
        else if (a == "--screenshot") shot = next();
        else if (a == "--screen") screenName = next();
        else if (a == "--select") selectId = next();
        else if (a == "--size") sscanf(next().c_str(), "%dx%d", &W, &H);
        else if (a == "--progress") progress = (float)atof(next().c_str());   // screenshots (--screen add): a build at this fraction
        else if (a == "--live") { A.liveShot = true; A.liveWait = atoi(next().c_str()); }
        else if (a == "--replay") replay = next();   // screenshots: feed a recorded engine output (build or play) into the selected game's task
        else if (a == "--fonts") A.fontDir = next();
        else if (a == "--engine") engine = next();
        else if (a == "--scroll") A.shotScroll = (float)atof(next().c_str());   // screenshots: scroll the screen down by this many px (at 720 high)
        else if (a == "--keys") keyList = next();
        else if (a == "--shared-dir") A.sharedDir = next();   // the shared online folder for the engine (tests, screenshots)
        else if (a == "--kit-root") A.kit.root = next();      // where the build kit goes (default: the exe's folder)
        else if (a == "--kit-url") kitUrl = next();           // tests: another address for the full build zip (needs --kit-allow-local unless GitHub)
        else if (a == "--release-api") relApi = next();      // tests: another release list address
        else if (a == "--symbols-url") symUrl = next();       // tests: another launcher symbols address
        else if (a == "--kit-allow-local") A.kit.allowLocal = true;   // tests: file:// and 127.0.0.1 (kit.py --allow-local)
        else if (a == "--kit-state") kitState = next();       // screenshots: ready|missing|other|damaged (no kit.py status run)
        else if (a == "--data") A.dataDir = next();   // the games' data folder for the engine's settings commands (default: the engine's rule)   // screenshots: press these keys first (comma list), then print where focus is
        else if (a == "--theme") { themeArg = next(); if (themeArg != "system" && themeArg != "light" && themeArg != "dark") { fprintf(stderr, "--theme: system, light or dark\n"); return 2; } }
        else { fprintf(stderr, "usage: launcher [--library FILE] [--engine \"CMD\"] [--data DIR] [--shared-dir DIR] [--kit-root DIR] [--kit-url URL] [--release-api URL] [--symbols-url URL] [--kit-allow-local] [--screenshot OUT.png --screen library|details|add|settings|locations|files|about|sidebar|kit [--kit-state S] [--select ID] [--size WxH] [--progress F] [--replay FILE] [--live SECONDS] [--scroll PX] [--keys up,down,left,right,enter,esc,tab,f10,...]] [--theme system|light|dark]\n"); return 2; }
    }
    if (engine.empty()) { const char* e = SDL_getenv("RC_LAUNCHER_ENGINE"); if (e && *e) engine = e; }
    A.engineCmd = engine.empty() ? DefaultEngine(base ? base : "") : SplitCommand(engine);
    A.shotMode = !shot.empty();
    if (A.kit.root.empty()) A.kit.root = base ? base : ".";
    A.kit.url = !kitUrl.empty() ? kitUrl : LAUNCHER_KIT_URL;
    A.kit.api = !relApi.empty() ? relApi : LAUNCHER_REPO_API;
    A.kit.symbolsUrl = !symUrl.empty() ? symUrl : LAUNCHER_SYMBOLS_URL;
    if (A.fontDir.empty()) {
        std::string d = std::string(base ? base : "") + "fonts"; SDL_PathInfo pi;
        A.fontDir = SDL_GetPathInfo((d + "/AdwaitaSans-Regular.ttf").c_str(), &pi) ? d : LAUNCHER_FONT_DIR;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    A.win = SDL_CreateWindow("Qwark's Awakening", W, H, SDL_WINDOW_RESIZABLE | (A.shotMode ? 0 : SDL_WINDOW_HIGH_PIXEL_DENSITY));
    if (!A.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    A.ren = SDL_CreateRenderer(A.win, nullptr);
    if (!A.ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderVSync(A.ren, A.shotMode ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL3_InitForSDLRenderer(A.win, A.ren);
    ImGui_ImplSDLRenderer3_Init(A.ren);
    A.fBody = LoadFont("AdwaitaSans-Regular.ttf"); A.fBodySemi = LoadFont("AdwaitaSans-SemiBold.ttf");
    A.fHeadBold = LoadFont("AdwaitaSans-Bold.ttf"); A.fHead = A.fHeadBold; A.fMono = LoadFont("AdwaitaMono-Regular.ttf");
    A.fMark = LoadFont("RussoOne-Regular.ttf");
    if (!A.fBody || !A.fBodySemi || !A.fHeadBold || !A.fMono || !A.fMark) return 1;
    A.scale = 0.f;

    A.lib = loadLibrary(A.libPath);
    // light / dark: --theme (not stored) > library.json prefs.theme > system
    A.themePref = !themeArg.empty() ? themeArg : (A.lib.theme == "light" || A.lib.theme == "dark") ? A.lib.theme : "system";
    ApplyTheme();
    {   // first start: no library.json at all (an unreadable or broken one is shown as it is, never overwritten)
        SDL_PathInfo lpi;
        if (!A.shotMode && !SDL_GetPathInfo(A.libPath.c_str(), &lpi)) EngineFirstScan();
    }
    for (auto& g : A.lib.games) {
        bool lit = LitState(g);
        g.colourTarget = lit ? 1.f : 0.f; g.colour = g.colourTarget;
    }
    // TEMP defaults for settings: folders the library already knows, build folder next to the exe
    SyncBuildFolder();
    // initial selection
    A.sel = 0;
    for (size_t i = 0; i < A.lib.games.size(); i++) if (A.lib.games[i].state == State::Built) { A.sel = (int)i; break; }
    if (!selectId.empty()) for (size_t i = 0; i < A.lib.games.size(); i++) if (A.lib.games[i].id == selectId) A.sel = (int)i;
    if (A.shotMode) {
        if (screenName == "details") A.screen = App::ScDetails;
        else if (screenName == "settings" || screenName == "locations") A.screen = App::ScSettings;
        else if (screenName == "files") A.screen = App::ScFiles;
        else if (screenName == "about") A.screen = App::ScAbout;
        else if (screenName == "sidebar") A.showSidebar = true;   // one column: the sidebar as the page (wide: Library)
        else if (screenName == "add") {
            if (selectId.empty()) for (size_t i = 0; i < A.lib.games.size(); i++) if (A.lib.games[i].state == State::ReadyToBuild) { A.sel = (int)i; break; }
            if (!A.lib.games.empty()) OpenAdd(A.lib.games[A.sel]);
            if (progress >= 0 && !A.lib.games.empty()) {   // a build at this fraction, no process (the stage from the fraction)
                static const char* kStep[5] = {"check", "extract", "ppu-lift", "compile", "stage"};
                StartBuild(A.lib.games[A.sel]);
                A.task.replay = true; A.task.running = true; A.task.pct = progress * 100.f; A.task.step = kStep[std::min(4, (int)(progress * 5.f))];
            }
        }
    }

    // the build kit: kit.py status at start (read only, this PC); screenshots take --kit-state instead
    if (A.shotMode) A.kit.release = LAUNCHER_KIT_RELEASE;
    if (A.shotMode) A.kit.state = !kitState.empty() ? kitState : !*LAUNCHER_KIT_SHA256 ? "none" : screenName == "kit" ? "missing" : "ready";
    else {
        KitStatus();
        if (A.kit.state == "none" && A.lib.updateCheck == "on") CheckRelease(true);   // opt-in (Settings > Updates), off by default
    }
    if (A.shotMode && screenName == "kit" && !A.lib.games.empty()) {   // the dialog as Build opens it (--progress F: downloading)
        A.kit.open = true; A.kit.pendingGame = A.lib.games[A.sel].id;
        if (progress >= 0) { StartTask("kit", A.lib.games[A.sel]); A.task.replay = true; A.task.running = true; A.task.pct = progress * 100.f; A.task.step = progress < 0.9f ? "download" : "install"; }
    }
    if (A.shotMode && !replay.empty() && !A.lib.games.empty()) ReplayTask(replay, A.lib.games[A.sel], screenName == "kit");
    // A shot of a page with settings reads them first: get-settings is read only, the one engine command a screenshot runs.
    if (A.shotMode && (A.screen == App::ScSettings || A.screen == App::ScDetails)) {
        std::vector<std::string> argv = A.engineCmd; argv.push_back("get-settings"); SettingsArgs(argv);
        A.eng.busy = true; A.busyKey = "s|load"; A.set.loading = true;
        { std::lock_guard<std::mutex> l(A.eng.m); A.eng.key = "s|load"; }
        EngineThread(argv);
        if (A.screen == App::ScSettings) {   // and the shared online settings (get-online, read only)
            PumpEngine();
            std::vector<std::string> o = A.engineCmd; o.push_back("get-online"); OnlineArgs(o);
            std::string out; if (RunCapture(o, out) == 0) ParseOnline(out, false); else A.online.err = out;
        }
    }

    // --keys: one key every 6 frames from frame 8 (down on the first, up on the next), the shot 8 frames after the last
    std::vector<ImGuiKey> keys;
    {
        static const struct { const char* n; ImGuiKey k; } kNames[] = {
            {"up", ImGuiKey_UpArrow}, {"down", ImGuiKey_DownArrow}, {"left", ImGuiKey_LeftArrow}, {"right", ImGuiKey_RightArrow},
            {"enter", ImGuiKey_Enter}, {"space", ImGuiKey_Space}, {"esc", ImGuiKey_Escape}, {"tab", ImGuiKey_Tab}, {"f10", ImGuiKey_F10}};
        std::string k;
        for (size_t i = 0; i <= keyList.size(); i++) {
            if (i < keyList.size() && keyList[i] != ',') { k += keyList[i]; continue; }
            if (k.empty()) continue;
            ImGuiKey found = ImGuiKey_None;
            for (auto& kn : kNames) if (k == kn.n) found = kn.k;
            if (found == ImGuiKey_None) { fprintf(stderr, "--keys: unknown key %s\n", k.c_str()); return 2; }
            keys.push_back(found); k.clear();
        }
    }
    const int shotFrame = keys.empty() ? 14 : 8 + 6 * (int)keys.size() + 8;
    bool run = true; int frames = 0; Uint64 last = SDL_GetTicks(); int rc = 0;
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) run = false;
            else if (e.type == SDL_EVENT_SYSTEM_THEME_CHANGED && A.themePref == "system") ApplyTheme();
        }
        ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame();
        if (A.shotMode && frames >= 8 && (frames - 8) / 6 < (int)keys.size() && (frames - 8) % 6 < 2)
            ImGui::GetIO().AddKeyEvent(keys[(frames - 8) / 6], (frames - 8) % 6 == 0);
        ImGui::NewFrame();
        Uint64 now = SDL_GetTicks(); float dt = A.shotMode ? 1.f / 60.f : std::min(0.1f, (now - last) / 1000.f); last = now;
        Frame(dt);
        ImGui::Render();
        SDL_SetRenderDrawColor(A.ren, (T->windowBg >> 24) & 255, (T->windowBg >> 16) & 255, (T->windowBg >> 8) & 255, 255); SDL_RenderClear(A.ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), A.ren);
        frames++;
        // --live: after the keys, wait (up to liveWait s) for a task they started to end, then 30 frames more
        static Uint64 liveT0 = 0; static int liveDone = -1;
        bool liveHold = false;
        if (A.shotMode && A.liveShot && frames >= shotFrame) {
            if (!liveT0) liveT0 = SDL_GetTicks();
            if (TaskRunning() && SDL_GetTicks() - liveT0 < (Uint64)A.liveWait * 1000) { liveHold = true; SDL_Delay(50); }
            else if (liveDone < 0) liveDone = frames;
            if (liveDone >= 0 && frames < liveDone + 30) liveHold = true;
        }
        if (A.shotMode && frames >= shotFrame && !liveHold) {
            if (!keys.empty()) {
                auto it = A.navLabels.find(ImGui::GetFocusID());
                printf("nav: screen=%s sidebar_page=%d collapsed=%d focus=%s\n", PageTitle(), A.showSidebar ? 1 : 0, A.collapsed ? 1 : 0,
                       it == A.navLabels.end() ? "(none)" : it->second.c_str());
            }
            if (A.liveShot) {   // --live: what the task did, for the end-to-end check's log
                std::lock_guard<std::mutex> l(A.task.m);
                printf("live: task=%s result=%d proc=%d lines=%zu\n", A.task.kind.empty() ? "(none)" : A.task.kind.c_str(), A.task.result, A.task.procRc, A.task.lines.size());
                for (size_t i = A.task.lines.size() > 8 ? A.task.lines.size() - 8 : 0; i < A.task.lines.size(); i++) printf("live: %s\n", A.task.lines[i].c_str());
            }
            SDL_Surface* s = SDL_RenderReadPixels(A.ren, nullptr);
            if (!s || !SDL_SavePNG(s, shot.c_str())) { fprintf(stderr, "screenshot failed: %s\n", SDL_GetError()); rc = 1; }
            else printf("wrote %s (%dx%d)\n", shot.c_str(), s->w, s->h);
            if (s) SDL_DestroySurface(s);
            break;
        }
        SDL_RenderPresent(A.ren);
    }
    for (auto& kv : A.pads) SDL_CloseGamepad(kv.second);
    ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    SDL_DestroyRenderer(A.ren); SDL_DestroyWindow(A.win); SDL_Quit();
    return rc;
}
