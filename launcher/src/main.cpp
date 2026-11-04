// Qwark's Awakening launcher, phase 3 MVP (UI only). Four screens: Library, Game details, Add game (drawer), Settings.
// SDL3 + Dear ImGui (SDL3 + SDLRenderer3 backends). No game art is embedded or fetched: tiles are drawn procedurally
// unless the library entry points at the player's own PNG files.
// TEMP marks every stub: nothing here talks to the build engine yet (the Build button runs a fake 6 s build).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
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

// ---------------------------------------------------------------------------------------------------------------
// Palette: tokens from public/site/style.css :root (dark).
static ImU32 Hex(unsigned rgb, float a = 1.f) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (int)(a * 255.f));
}
static ImVec4 HexV(unsigned rgb, float a = 1.f) { return ImGui::ColorConvertU32ToFloat4(Hex(rgb, a)); }
enum : unsigned {
    cBg = 0x070b16, cGlow = 0x132449, cPanel = 0x0d1428, cPanel2 = 0x111b36, cLine = 0x1f3460, cText = 0xe4ebf4,
    cDim = 0x93a3b8, cAcc = 0xff9a1e, cAccText = 0xffb347, cGold = 0xffd65c, cSteel = 0x96bade, cBtnText = 0x0b0e18,
    cChip = 0x1b2c52, cOk = 0x7fd6a0, cBad = 0xff8a7a
};
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
struct Job { int game = -1; float t = 0; };   // TEMP: the fake build

struct App {
    SDL_Window* win = nullptr; SDL_Renderer* ren = nullptr;
    Library lib; std::string libPath, fontDir;
    float scale = 1.f;
    ImFont *fBody = nullptr, *fBodySemi = nullptr, *fHead = nullptr, *fHeadBold = nullptr, *fHeadMed = nullptr, *fMark = nullptr;
    enum Screen { ScLibrary, ScDetails, ScSettings, ScFiles } screen = ScLibrary;
    int filter = 0;       // 0 All, 1 Ready, 2 Not added
    int sel = 0;
    bool addOpen = false, addJustOpened = false;
    char discBuf[512] = "", updBuf[512] = "", buildBuf[512] = "";
    // settings (the cover art switch is TEMP: kept in memory only; locations live in library.json)
    char buildFolder[512] = ""; bool artFetch = false;
    Toast toast; Dialog dlg; Job job; Engine eng;
    std::vector<std::string> engineCmd;      // argv prefix of the engine command; the verb and its arguments are appended
    std::string busyKey;                     // the row the running engine command belongs to
    std::string errKey, errText;             // last engine failure, shown on the row that asked
    float shotScroll = -1.f;                 // screenshots: scroll the body to this many px (scaled)
    bool shotMode = false; bool firstFrame = true;
    std::map<SDL_JoystickID, SDL_Gamepad*> pads;
    float S(float v) const { return v * scale; }
};
static App A;
static const float kBuildSeconds = 6.f;   // TEMP: fake build length
static const char* kStages[5] = {"Checking files", "Preparing game files", "Recompiling", "Building", "Finishing"};

static Game* Sel() { return A.sel >= 0 && A.sel < (int)A.lib.games.size() ? &A.lib.games[A.sel] : nullptr; }
static void Say(const std::string& t) { A.toast.text = t; A.toast.until = ImGui::GetTime() + 4.0; }
static bool Playable(const Game& g) { return g.state == State::Built || g.state == State::PortUpdated; }
static bool InReady(const Game& g) { return g.state == State::ReadyToBuild || g.state == State::Building || Playable(g); }

static const char* StateWord(const Game& g, std::string& tmp) {
    switch (g.state) {
        case State::NotAdded: return "Not added";
        case State::WrongVersion: tmp = g.needsUpdate.empty() ? "Needs update" : "Needs update " + g.needsUpdate; return tmp.c_str();
        case State::NotPorted: return "Not ported yet";
        case State::ReadyToBuild: return "Ready to build";
        case State::Building: return "Building";
        case State::Built: return "Ready";
        case State::PortUpdated: return "Port updated: rebuild";
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
        dl->AddCircleFilled(ImVec2(x, y), r, Look(Hex(i % 5 == 0 ? cGold : cText, al), colour), 6);
    }
}
static void SplitTitle(const std::string& t, std::string& series, std::string& name) {
    size_t c = t.find(": ");
    if (c != std::string::npos) { series = t.substr(0, c); name = t.substr(c + 2); return; }
    const std::string pre = "Ratchet & Clank ";
    if (t.compare(0, pre.size(), pre) == 0) { series = "Ratchet & Clank"; name = t.substr(pre.size()); return; }
    series = ""; name = t;
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
        ImU32 t = Look(Hex(tops[v]), col), b = Look(Hex(bots[v]), col);
        dl->AddRectFilledMultiColor(p0, p1, t, t, b, b);
        ImVec2 c(p0.x + W * 0.5f, p0.y + H * 0.36f);
        dl->AddCircleFilled(c, W * 0.30f, Look(Hex(cGold, 0.10f), col), 48);
        dl->AddCircle(c, W * 0.30f, Look(Hex(cGold, 0.55f), col), 48, A.S(1.5f));
        dl->AddCircle(c, W * 0.40f, Look(Hex(cSteel, 0.25f), col), 48, A.S(1.f));
        dl->AddCircleFilled(ImVec2(c.x + W * 0.40f * 0.7f, c.y - W * 0.40f * 0.7f), W * 0.035f, Look(Hex(cAcc), col), 12);
        Stars(dl, p0, p1, 17u + g.order, (int)(14 + W / 14), col, 0.8f);
    }
    // title block on a scrim, always full contrast
    std::string series, name; SplitTitle(g.title, series, name);
    float sz = A.S(23.f) * titleScale, ss = A.S(13.f) * titleScale, wrap = W - A.S(16.f) * titleScale;
    ImVec2 ns = A.fHeadBold->CalcTextSizeA(sz, FLT_MAX, wrap, name.c_str());
    float sh = series.empty() ? 0 : ss + A.S(4.f);
    float top = p1.y - ns.y - sh - A.S(14.f) * titleScale;
    dl->AddRectFilledMultiColor(ImVec2(p0.x, top - A.S(26.f)), p1, 0, 0, IM_COL32(4, 7, 14, 235), IM_COL32(4, 7, 14, 235));
    ImVec2 tp(p0.x + A.S(8.f) * titleScale, top + A.S(2.f));
    if (!series.empty()) { dl->AddText(A.fHead, ss, tp, Hex(cAccText), series.c_str()); tp.y += sh; }
    dl->AddText(A.fHeadBold, sz, tp, Hex(cText), name.c_str(), nullptr, wrap);
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
            dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.42f), r * 0.14f, Hex(cBg), 8);
            break; }
        case State::Built:   // check in a disc
            dl->AddCircleFilled(c, r, col, 20);
            dl->AddPolyline((ImVec2[]){ImVec2(c.x - r * 0.45f, c.y + r * 0.02f), ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), ImVec2(c.x + r * 0.5f, c.y - r * 0.32f)}, 3, Hex(cBg), 0, th);
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
        ImU32 col = Playable(g) ? Hex(cOk) : (g.state == State::NeedsAttention || g.state == State::PortUpdated) ? Hex(cAccText) : Hex(cText);
        StateIcon(dl, ImVec2(pos.x + r, pos.y + ts.y * 0.5f), r * 0.86f, g.state, col);
        dl->AddText(A.fHeadBold, fontPx, ImVec2(pos.x + r * 2 + gap, pos.y), Hex(cText), w);
    }
    return r * 2 + gap + ts.x;
}

// ---------------------------------------------------------------------------------------------------------------
// Widgets
static bool Btn(const char* label, ImVec2 size, bool primary, bool active = false) {
    ImGui::PushFont(A.fHeadBold, A.S(primary ? 22.f : 19.f));
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, HexV(cAcc)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HexV(cGold));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, HexV(cAccText)); ImGui::PushStyleColor(ImGuiCol_Text, HexV(cBtnText));
        ImGui::PushStyleColor(ImGuiCol_Border, HexV(cAcc));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? HexV(cChip) : HexV(cPanel2)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HexV(cChip));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, HexV(cLine)); ImGui::PushStyleColor(ImGuiCol_Text, active ? HexV(cGold) : HexV(cText));
        ImGui::PushStyleColor(ImGuiCol_Border, active ? HexV(cGold) : HexV(cLine));
    }
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(5); ImGui::PopFont();
    return r;
}
static void Heading(const char* t, float px = 24.f, unsigned col = cText) {
    ImGui::PushFont(A.fHead, A.S(px)); ImGui::PushStyleColor(ImGuiCol_Text, HexV(col)); ImGui::TextUnformatted(t); ImGui::PopStyleColor(); ImGui::PopFont();
}
static void Dim(const char* t, float px = 15.f) {
    ImGui::PushFont(A.fBody, A.S(px)); ImGui::PushStyleColor(ImGuiCol_Text, HexV(cDim)); ImGui::TextUnformatted(t); ImGui::PopStyleColor(); ImGui::PopFont();
}
static void Wrapped(const char* t, unsigned col = cText, float px = 16.f) {
    ImGui::PushFont(A.fBody, A.S(px)); ImGui::PushStyleColor(ImGuiCol_Text, HexV(col)); ImGui::TextWrapped("%s", t); ImGui::PopStyleColor(); ImGui::PopFont();
}
static void Rule(float inset = 0) {
    ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + inset, p.y), ImVec2(p.x + w - inset, p.y), Hex(cLine), 1.f);
    ImGui::Dummy(ImVec2(0, A.S(8.f)));
}
static void Panel(ImVec2 p0, ImVec2 p1, unsigned fill = cPanel) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, Hex(fill), A.S(10.f)); dl->AddRect(p0, p1, Hex(cLine), A.S(10.f), 0, 1.f);
}
static std::string Esc(const std::string& s) { return s.empty() ? "none recorded" : s; }
static void KeyVal(const char* k, const std::string& v, bool mono = false) {
    (void)mono;
    ImGui::PushFont(A.fBodySemi, A.S(15.f)); ImGui::PushStyleColor(ImGuiCol_Text, HexV(cDim)); ImGui::TextUnformatted(k); ImGui::PopStyleColor(); ImGui::PopFont();
    ImGui::SameLine(A.S(150.f));
    ImGui::PushFont(A.fBody, A.S(16.f)); ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(v.c_str()); ImGui::PopTextWrapPos(); ImGui::PopFont();
}

// ---------------------------------------------------------------------------------------------------------------
// Fake build and add drawer
static void StartBuild(int gi) {   // TEMP: no engine yet
    if (A.job.game >= 0) { Say("A build is already running"); return; }
    A.job.game = gi; A.job.t = 0;
    A.lib.games[gi].state = State::Building;
}
static void TickBuild(float dt) {
    if (A.job.game < 0) return;
    A.job.t += dt;
    if (A.job.t < kBuildSeconds) return;
    Game& g = A.lib.games[A.job.game];
    g.state = State::Built; g.colourTarget = 1.f; g.hasBuild = true; g.buildTemp = true;
    g.exe = "(TEMP: fake build, no exe)"; g.builtPortVersion = A.lib.portVersion; g.builtAt = "just now"; g.exeSha = "";
    Say(g.title + " is ready");
    A.job.game = -1;
}
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
// Engine: `python3 tools/release/catalog.py` by default; --engine "CMD" or $RC_LAUNCHER_ENGINE replace it. The command is split
// on spaces (double quotes group); every later argument is its own argv entry, so paths with spaces never meet a shell.
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
static void EngineThread(std::vector<std::string> argv) {
    std::vector<const char*> a; for (auto& x : argv) a.push_back(x.c_str());
    a.push_back(nullptr);
    std::string out; int rc = -1;
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
    { std::lock_guard<std::mutex> l(A.eng.m); A.eng.output = out; A.eng.rc = rc; }
    A.eng.done = true;
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
// Reloads library.json after the engine rewrote it; runtime state (art, the fake build) is carried over by game id.
static void ReloadLibrary() {
    Library nl = loadLibrary(A.libPath);
    if (!nl.error.empty()) { Say("Could not reload the library: " + nl.error); return; }
    std::string selId = Sel() ? Sel()->id : "";
    for (auto& g : nl.games) {
        g.colourTarget = LitState(g) ? 1.f : 0.f; g.colour = g.colourTarget;
        for (auto& o : A.lib.games) if (o.id == g.id) {
            g.art = o.art; o.art = ArtTex(); g.buildTemp = o.buildTemp;
            if (o.buildTemp) { g.state = o.state; g.hasBuild = true; g.exe = o.exe; g.builtAt = o.builtAt; g.builtPortVersion = o.builtPortVersion; g.colourTarget = g.colour = o.colourTarget; }
        }
    }
    A.lib = std::move(nl);
    for (size_t i = 0; i < A.lib.games.size(); i++) if (A.lib.games[i].id == selId) A.sel = (int)i;
    SyncBuildFolder();
}
static void PumpEngine() {
    if (!A.eng.done.exchange(false)) return;
    std::string key, out; int rc;
    { std::lock_guard<std::mutex> l(A.eng.m); key = A.eng.key; out = A.eng.output; rc = A.eng.rc; }
    A.eng.busy = false; A.busyKey.clear();
    if (rc == 0) { ReloadLibrary(); return; }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    size_t nl = out.find_last_of('\n');
    A.errKey = key;
    A.errText = out.empty() ? "The engine failed without a message." : (rc == 2 || nl == std::string::npos ? out : out.substr(nl + 1));   // exit 2 = the engine's own message
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
    ImU32 col = c.ok ? Hex(cOk) : Hex(cBad);
    ImVec2 m(p.x + r, p.y + A.S(10.f));
    if (c.ok) dl->AddPolyline((ImVec2[]){ImVec2(m.x - r * .6f, m.y), ImVec2(m.x - r * .1f, m.y + r * .5f), ImVec2(m.x + r * .7f, m.y - r * .5f)}, 3, col, 0, A.S(2.f));
    else { dl->AddLine(ImVec2(m.x - r * .5f, m.y - r * .5f), ImVec2(m.x + r * .5f, m.y + r * .5f), col, A.S(2.f)); dl->AddLine(ImVec2(m.x + r * .5f, m.y - r * .5f), ImVec2(m.x - r * .5f, m.y + r * .5f), col, A.S(2.f)); }
    ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + A.S(6.f), p.y));
    ImGui::PushTextWrapPos(0); Wrapped(c.text.c_str(), c.ok ? cOk : cBad, 15.f); ImGui::PopTextWrapPos();
}
static void StepHeader(int n, const char* title, int mode /*0 active 1 done 2 skipped*/) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(); float r = A.S(13.f);
    ImVec2 c(p.x + r, p.y + r + A.S(1.f));
    if (mode == 0) { dl->AddCircleFilled(c, r, Hex(cAcc), 24); char b[4]; snprintf(b, 4, "%d", n); ImVec2 ts = A.fHeadBold->CalcTextSizeA(A.S(18.f), FLT_MAX, 0, b); dl->AddText(A.fHeadBold, A.S(18.f), ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), Hex(cBtnText), b); }
    else if (mode == 1) { dl->AddCircle(c, r, Hex(cOk), 24, A.S(2.f)); dl->AddPolyline((ImVec2[]){ImVec2(c.x - r * .45f, c.y), ImVec2(c.x - r * .1f, c.y + r * .38f), ImVec2(c.x + r * .5f, c.y - r * .32f)}, 3, Hex(cOk), 0, A.S(2.f)); }
    else { dl->AddCircle(c, r, Hex(cDim, .6f), 24, A.S(1.5f)); dl->AddLine(ImVec2(c.x - r * .4f, c.y), ImVec2(c.x + r * .4f, c.y), Hex(cDim, .6f), A.S(1.5f)); }
    ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + A.S(10.f), p.y));
    Heading(title, 22.f, mode == 2 ? cDim : cText);
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, HexV(cPanel)); ImGui::PushStyleColor(ImGuiCol_Border, HexV(cAcc));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.01f, 0.02f, 0.05f, 0.72f));
    bool open = ImGui::BeginPopupModal("add_game", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor(3); ImGui::PopStyleVar(2);
    if (!open) { A.addOpen = false; return; }

    bool building = A.job.game >= 0 && A.lib.games[A.job.game].id == g.id;
    bool justDone = !building && Playable(g) && g.buildTemp;
    // header
    ImGui::PushFont(A.fHeadBold, A.S(30.f)); ImGui::TextUnformatted(g.state == State::Built || g.state == State::PortUpdated ? "Rebuild game" : "Add game"); ImGui::PopFont();
    ImGui::SameLine(dw - A.S(48.f) - A.S(24.f) - A.S(40.f));
    if (Btn("Close", ImVec2(A.S(88.f), 0), false) || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive()) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
        ImGui::CloseCurrentPopup(); A.addOpen = false; ImGui::EndPopup(); return;
    }
    if (A.firstFrame || ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
    Heading(g.title.c_str(), 20.f, cAccText);
    Rule();

    bool filesKnown = !g.disc.empty();
    bool needUpd = !g.needsUpdate.empty() || g.state == State::WrongVersion;
    ImGui::BeginChild("steps", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);

    // (1) what this game needs
    StepHeader(1, "What this game needs", filesKnown ? 2 : 0);
    if (!filesKnown) {
        ImGui::Indent(A.S(36.f));
        Wrapped(("The game's disc (a folder or an ISO) or its PSN package" + std::string(g.titleId.empty() ? "" : " (title ID " + g.titleId + ")") + "."  ).c_str(), cText, 16.f);
        if (needUpd) Wrapped(("The update " + g.needsUpdate + " (a PKG file).").c_str(), cText, 16.f);
        Wrapped("A decrypted EBOOT.elf: the build makes it from your files, you do not supply it.", cDim, 15.f);
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
    Wrapped(est, cText, 15.f);
    ImGui::Unindent(A.S(36.f));
    ImGui::Dummy(ImVec2(0, A.S(6.f)));

    // (4) build
    StepHeader(4, "Build", justDone ? 1 : 0);
    ImGui::Indent(A.S(36.f));
    bool canBuild = c1.ok && c2.ok && !building && A.job.game < 0;
    if (!building && !justDone) {
        ImGui::BeginDisabled(!canBuild);
        if (Btn(g.state == State::Built || g.state == State::PortUpdated ? "Rebuild##go" : "Build##go", ImVec2(A.S(180.f), A.S(48.f)), true)) StartBuild(A.sel);
        ImGui::EndDisabled();
        if (!canBuild && A.job.game >= 0) Dim("Another build is running; this one waits for it.");
        else if (!canBuild) Dim("Fix the files above to enable Build.");
        else Dim("TEMP: this runs a fake 6 s build until the engine is connected.");
    } else {
        float p = building ? std::min(1.f, A.job.t / kBuildSeconds) : 1.f;
        int cur = std::min(4, (int)(p * 5.f));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 b0 = ImGui::GetCursorScreenPos(); float bw = ImGui::GetContentRegionAvail().x, bh = A.S(14.f);
        dl->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + bh), Hex(cBg), bh / 2); dl->AddRect(b0, ImVec2(b0.x + bw, b0.y + bh), Hex(cLine), bh / 2);
        if (p > 0.01f) dl->AddRectFilledMultiColor(b0, ImVec2(b0.x + bw * p, b0.y + bh), Hex(cAcc), Hex(cGold), Hex(cGold), Hex(cAcc));
        ImGui::Dummy(ImVec2(0, bh + A.S(8.f)));
        for (int i = 0; i < 5; i++) {
            ImVec2 q = ImGui::GetCursorScreenPos(); float r = A.S(6.f); ImVec2 m(q.x + r, q.y + A.S(11.f));
            bool done = justDone || i < cur, now = building && i == cur;
            if (done) dl->AddPolyline((ImVec2[]){ImVec2(m.x - r * .7f, m.y), ImVec2(m.x - r * .15f, m.y + r * .6f), ImVec2(m.x + r * .8f, m.y - r * .6f)}, 3, Hex(cOk), 0, A.S(2.f));
            else if (now) dl->AddCircleFilled(m, r * .7f, Hex(cGold), 12);
            else dl->AddCircle(m, r * .7f, Hex(cDim, .6f), 12, A.S(1.5f));
            ImGui::SetCursorScreenPos(ImVec2(q.x + A.S(24.f), q.y));
            ImGui::PushFont(now ? A.fBodySemi : A.fBody, A.S(16.f)); ImGui::PushStyleColor(ImGuiCol_Text, now ? HexV(cGold) : done ? HexV(cText) : HexV(cDim));
            ImGui::TextUnformatted(kStages[i]); ImGui::PopStyleColor(); ImGui::PopFont();
        }
        if (justDone) {
            ImGui::Dummy(ImVec2(0, A.S(6.f)));
            Wrapped("Done. The game is ready, and its art has its colour back.", cOk, 16.f);
            ImGui::Dummy(ImVec2(0, A.S(4.f)));
            if (Btn("Play##d", ImVec2(A.S(160.f), A.S(48.f)), true)) Say("TEMP: Play is not connected yet");
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
        case State::Built: Say("TEMP: Play is not connected yet (no game is started from this preview)"); break;
        case State::NotPorted: break;
        case State::Building: A.addOpen = true; A.addJustOpened = true; break;
        case State::NeedsAttention: A.sel = idx; A.screen = App::ScFiles; break;
        default: A.sel = idx; OpenAdd(g); break;
    }
}
static void ManageMenu(Game& g, int idx) {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, HexV(cPanel2)); ImGui::PushStyleColor(ImGuiCol_Border, HexV(cLine));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(A.S(8.f), A.S(8.f)));
    if (ImGui::BeginPopup("manage")) {
        ImGui::PushFont(A.fBody, A.S(17.f));
        auto item = [&](const char* label) { return ImGui::Selectable(label, false, 0, ImVec2(A.S(240.f), A.S(30.f))); };
        if (item("Rebuild")) { if (g.state == State::NotAdded || g.state == State::NotPorted) Say("Nothing to rebuild: the game is not added"); else { A.sel = idx; OpenAdd(g); } }
        if (item("Open game folder")) {
            std::string dir = g.hasBuild && !g.buildTemp ? FolderOf(g.exe) : !g.disc.empty() ? g.disc : "";
            SDL_PathInfo pi;
            if (dir.empty() || !SDL_GetPathInfo(dir.c_str(), &pi)) Say(dir.empty() ? "No folder recorded for this game yet" : "That folder does not exist on this PC");
            else { std::string url = "file://" + std::string(dir[0] == '/' ? "" : "/"); for (char ch : dir) url += ch == '\\' ? '/' : ch; if (!SDL_OpenURL(url.c_str())) Say(std::string("Could not open the folder: ") + SDL_GetError()); }
        }
        if (item("Open saves")) Say("TEMP: Open saves is not connected yet");
        if (item("Change game files")) { if (g.state == State::NotPorted) Say("Not ported yet: nothing to change"); else { A.sel = idx; A.screen = App::ScFiles; } }
        if (item("Build log")) Say("TEMP: Build log is not connected yet");
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
    Panel(p0, p1);
    dl->PushClipRect(p0, p1, true);
    dl->AddRectFilledMultiColor(p0, ImVec2(p1.x, p0.y + h * 0.5f), Hex(cGlow, 0.9f), Hex(cGlow, 0.25f), Hex(cPanel, 0), Hex(cPanel, 0));
    Stars(dl, ImVec2(p0.x + h * 0.9f, p0.y), p1, 5, 40, 1.f, 0.55f);
    dl->PopClipRect();
    float pad = A.S(18.f);
    ImVec2 a0(p0.x + pad, p0.y + pad), a1(a0.x + (h - pad * 2) * 0.75f, p1.y - pad);
    DrawTile(dl, a0, a1, g, 0.f, 1.35f);
    dl->AddRect(a0, a1, Hex(cLine), 0, 0, 1.f);
    float tx = a1.x + A.S(28.f);
    ImGui::SetCursorScreenPos(ImVec2(tx, p0.y + pad));
    char meta[160];
    snprintf(meta, sizeof meta, "%d%s%s", g.year, g.titleId.empty() ? "" : "   ·   ", g.titleId.c_str());
    ImGui::PushFont(A.fHead, A.S(17.f)); ImGui::PushStyleColor(ImGuiCol_Text, HexV(cAccText)); ImGui::TextUnformatted(meta); ImGui::PopStyleColor(); ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(tx, ImGui::GetCursorScreenPos().y));
    ImGui::PushFont(A.fHeadBold, A.S(expanded ? 40.f : 36.f)); ImGui::PushTextWrapPos(p1.x - pad - ImGui::GetWindowPos().x + ImGui::GetScrollX()); ImGui::TextUnformatted(g.title.c_str()); ImGui::PopTextWrapPos(); ImGui::PopFont();
    ImVec2 sp = ImGui::GetCursorScreenPos(); sp.x = tx; sp.y += A.S(6.f);
    float sw = StateLine(dl, sp, g, A.S(22.f));
    (void)sw;
    ImGui::SetCursorScreenPos(ImVec2(tx, sp.y + A.S(34.f)));
    std::string line;
    if (g.state == State::Building) { char b[64]; snprintf(b, sizeof b, "Building: %s", kStages[std::min(4, (int)(std::min(1.f, A.job.t / kBuildSeconds) * 5))]); line = b; }
    else if (g.state == State::ReadyToBuild && g.hasEstimate) { char b[96]; snprintf(b, sizeof b, "Your files are in place. About %d min, %.1f GB.", g.estMinutes, g.estGb); line = b; }
    else if (g.state == State::NotAdded) line = "No game files found. This only means the launcher has not seen them; it cannot tell whether you own the game.";
    else if (g.state == State::WrongVersion) line = "Your files are version " + g.version + ". The port needs " + g.needsUpdate + ": add the update package.";
    else if (g.state == State::NotPorted) line = g.children.empty() ? "Listed as in the README table; it cannot be built yet." : "Holds Ratchet & Clank, Going Commando and Up Your Arsenal. Not ported yet.";
    else if (g.state == State::Built) line = "Built with port " + g.builtPortVersion + ". Ready to play.";
    else if (!g.reason.empty()) line = g.reason;
    ImGui::PushTextWrapPos(p1.x - pad - ImGui::GetWindowPos().x + ImGui::GetScrollX());
    Wrapped(line.c_str(), cDim, 16.f); ImGui::PopTextWrapPos();

    // buttons at the bottom of the panel
    float by = p1.y - pad - A.S(50.f);
    ImGui::SetCursorScreenPos(ImVec2(tx, by));
    const char* lab = PrimaryLabel(g);
    ImVec2 bs(A.S(190.f), A.S(50.f));
    if (g.state == State::Building) {
        float pr = std::min(1.f, A.job.t / kBuildSeconds); ImVec2 q = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(q, ImVec2(q.x + A.S(300.f), q.y + A.S(14.f)), Hex(cBg), A.S(7.f)); dl->AddRect(q, ImVec2(q.x + A.S(300.f), q.y + A.S(14.f)), Hex(cLine), A.S(7.f));
        dl->AddRectFilledMultiColor(q, ImVec2(q.x + A.S(300.f) * pr, q.y + A.S(14.f)), Hex(cAcc), Hex(cGold), Hex(cGold), Hex(cAcc));
        ImGui::Dummy(ImVec2(A.S(300.f), A.S(14.f)));
    } else if (lab) {
        if (Btn((std::string(lab) + "##primary").c_str(), bs, true)) Action(g, A.sel);
        if (ImGui::IsWindowAppearing() || A.firstFrame) ImGui::SetItemDefaultFocus();
        ImGui::SameLine(0, A.S(12.f));
    } else {
        Dim("Nothing to do yet. The entry follows the README table.");
        ImGui::SetCursorScreenPos(ImVec2(tx + A.S(420.f), by));
    }
    if (g.state != State::Building) {
        if (Btn("Manage##m", ImVec2(A.S(130.f), A.S(50.f)), false)) ImGui::OpenPopup("manage");
        ManageMenu(g, A.sel);
        if (!expanded) { ImGui::SameLine(0, A.S(12.f)); if (Btn("Details##d", ImVec2(A.S(120.f), A.S(50.f)), false)) A.screen = App::ScDetails; }
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
    ImGui::Dummy(ImVec2(w, A.S(14.f)));
}

static void DrawCard(Game& g, int idx, ImVec2 size, bool& selectMe) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    char id[32]; snprintf(id, sizeof id, "card%d", idx);
    ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered(), foc = ImGui::IsItemFocused(), clicked = ImGui::IsItemClicked() || ImGui::IsItemActivated();
    if (clicked || (foc && A.sel != idx && (ImGui::GetCurrentContext()->NavCursorVisible || ImGui::IsItemActive()))) selectMe = true;
    ImVec2 a0 = p, a1(p.x + size.x, p.y + size.x * 1.333f);
    DrawTile(dl, a0, a1, g, (hov || foc) ? 1.f : 0.f, 0.75f + 0.25f * (size.x / A.S(150.f)));
    if (A.sel == idx) dl->AddRect(ImVec2(a0.x - 2, a0.y - 2), ImVec2(a1.x + 2, a1.y + 2), Hex(cSteel), 0, 0, A.S(2.f));
    if (foc) dl->AddRect(ImVec2(a0.x - A.S(4.f), a0.y - A.S(4.f)), ImVec2(a1.x + A.S(4.f), a1.y + A.S(4.f)), Hex(cGold), A.S(3.f), 0, A.S(3.f));
    StateLine(dl, ImVec2(p.x + A.S(2.f), a1.y + A.S(8.f)), g, A.S(15.f));
    if (g.state == State::Building) {
        float pr = std::min(1.f, A.job.t / kBuildSeconds);
        dl->AddRectFilled(ImVec2(a0.x, a1.y - A.S(5.f)), ImVec2(a0.x + size.x * pr, a1.y), Hex(cAcc));
    }
}

static void LibraryScreen(float W) {
    (void)W;
    float pad = A.S(24.f);
    ImGui::SetCursorPos(ImVec2(pad, A.S(14.f)));
    // filters
    int nReady = 0, nNot = 0;
    for (auto& g : A.lib.games) (InReady(g) ? nReady : nNot)++;
    int counts[3] = {(int)A.lib.games.size(), nReady, nNot};
    const char* names[3] = {"All", "Ready", "Not added"};
    for (int i = 0; i < 3; i++) {
        char b[48]; snprintf(b, sizeof b, "%s  %d##flt%d", names[i], counts[i], i);
        if (Btn(b, ImVec2(0, A.S(38.f)), false, A.filter == i)) A.filter = i;
        ImGui::SameLine(0, A.S(10.f));
    }
    ImGui::NewLine();
    ImGui::SetCursorPosX(pad);
    ImGui::Dummy(ImVec2(0, A.S(4.f)));
    ImGui::SetCursorPosX(pad);
    float availW = ImGui::GetContentRegionAvail().x - pad;
    if (!A.lib.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, HexV(cBad)); ImGui::PushFont(A.fHeadBold, A.S(24.f)); ImGui::TextWrapped("%s", A.lib.error.c_str()); ImGui::PopFont(); ImGui::PopStyleColor();
        Dim("Start the launcher with --library FILE, or put library.json next to it.");
        return;
    }
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
    if (n == 0) Dim("No games match this filter.", 17.f);
    ImGui::EndGroup();
}

static void DetailsScreen() {
    Game* gp = Sel(); if (!gp) return;
    Game& g = *gp;
    float pad = A.S(24.f);
    ImGui::SetCursorPos(ImVec2(pad, A.S(14.f)));
    float availW = ImGui::GetContentRegionAvail().x - pad;
    ImGui::BeginChild("detpanel", ImVec2(availW, A.S(250.f)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    SelectedPanel(A.S(236.f), true);
    ImGui::EndChild();
    ImGui::SetCursorPosX(pad);
    float gap = A.S(14.f), cw = (availW - gap) / 2;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    auto card = [&](const char* title, float x, float h, auto body) {
        ImGui::SetCursorScreenPos(ImVec2(p0.x + x, p0.y));
        ImVec2 q = ImGui::GetCursorScreenPos();
        Panel(q, ImVec2(q.x + cw, q.y + h));
        ImGui::SetCursorScreenPos(ImVec2(q.x + A.S(18.f), q.y + A.S(14.f)));
        ImGui::BeginChild(title, ImVec2(cw - A.S(36.f), h - A.S(24.f)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        Heading(title, 22.f, cAccText); ImGui::Dummy(ImVec2(0, A.S(4.f)));
        body();
        ImGui::EndChild();
    };
    float rh = A.S(172.f);
    card("Readme state", 0, rh, [&]() {
        KeyVal("State", g.readme.empty() ? "unknown" : g.readme);
        KeyVal("Port supports it", g.supported ? "yes, it can be built" : "not yet");
        KeyVal("Launcher state", [&]() { std::string t; return std::string(StateWord(g, t)); }());
        if (!g.reason.empty()) KeyVal("Reason", g.reason);
        if (!g.children.empty()) { std::string s; for (auto& c : g.children) s += (s.empty() ? "" : ", ") + c.title; KeyVal("Contains", s); }
    });
    card("What this game needs", cw + gap, rh, [&]() {
        KeyVal("Title ID", Esc(g.titleId));
        KeyVal("Your version", g.version.empty() ? "not known (no files)" : g.version);
        KeyVal("Update needed", g.needsUpdate.empty() ? "none" : g.needsUpdate);
        KeyVal("EBOOT.elf", g.eboot.empty() ? "not made yet (the build decrypts it)" : "decrypted: " + g.eboot);
    });
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + rh + gap));
    p0 = ImGui::GetCursorScreenPos();
    float rh2 = A.S(192.f);
    card("Recorded files", 0, rh2, [&]() {
        KeyVal("Game files", Esc(g.disc)); KeyVal("Update", Esc(g.update)); KeyVal("EBOOT.elf", Esc(g.eboot));
    });
    card("Build info", cw + gap, rh2, [&]() {
        if (!g.hasBuild) { KeyVal("Built", "no"); if (g.hasEstimate) { char b[64]; snprintf(b, sizeof b, "about %d min, %.1f GB", g.estMinutes, g.estGb); KeyVal("Estimate", b); } }
        else {
            KeyVal("Executable", g.exe); KeyVal("Built with port", g.builtPortVersion.empty() ? "?" : g.builtPortVersion);
            KeyVal("Built at", g.builtAt); if (!g.exeSha.empty()) KeyVal("Exe SHA-256", g.exeSha.substr(0, 16) + "...");
        }
        if (g.igdbGame) { char b[64]; snprintf(b, sizeof b, "game %lld, cover %s", g.igdbGame, g.igdbCover.empty() ? "-" : g.igdbCover.c_str()); KeyVal("IGDB id", b); }
    });
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + rh2 + gap));
}

// ---------------------------------------------------------------------------------------------------------------
// Locations: Settings > Locations (global) and Manage > Change game files (per game). Every change goes through the engine.
static std::string Fit(const std::string& t, float maxW, ImFont* f, float px) {   // keeps the end of a long path ("...\folder\file")
    if (f->CalcTextSizeA(px, FLT_MAX, 0, t.c_str()).x <= maxW) return t;
    size_t cut = 1;
    while (cut < t.size() && f->CalcTextSizeA(px, FLT_MAX, 0, ("..." + t.substr(cut)).c_str()).x > maxW) cut++;
    return "..." + t.substr(cut);
}
static void PathLine(const std::string& t, unsigned col, float px = 16.f) {
    ImGui::PushFont(A.fBody, A.S(px)); ImGui::PushStyleColor(ImGuiCol_Text, HexV(col));
    ImGui::TextUnformatted(Fit(t, ImGui::GetContentRegionAvail().x, A.fBody, A.S(px)).c_str());
    ImGui::PopStyleColor(); ImGui::PopFont();
}
// "Checking..." while the engine works on this row, its message when it refused (exit 2), nothing otherwise. Always one line high.
static void RowStatus(const std::string& key) {
    if (EngineBusy() && A.busyKey == key) { Wrapped("Checking...", cGold, 15.f); return; }
    if (A.errKey == key) {
        Wrapped(A.errText.c_str(), cBad, 15.f);
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
    if (path.empty()) { col = cDim; return "Not found"; }
    if (s == "override") { col = cGold; return "Chosen by you"; }
    if (s == "rpcs3") { col = cSteel; return "RPCS3"; }
    col = cOk; return "Found";
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

static void FilesScreen() {
    Game* gp = Sel(); if (!gp) return;
    Game& g = *gp;
    float pad = A.S(24.f);
    ImGui::SetCursorPos(ImVec2(pad, A.S(14.f)));
    float availW = std::min(ImGui::GetContentRegionAvail().x - pad * 2, A.S(1000.f));
    pad = std::max(pad, (ImGui::GetContentRegionAvail().x - availW) / 2);
    ImGui::SetCursorPos(ImVec2(pad, A.S(14.f)));
    char meta[160]; snprintf(meta, sizeof meta, "%d%s%s", g.year, g.titleId.empty() ? "" : "   ·   ", g.titleId.c_str());
    Heading(meta, 17.f, cAccText);
    ImGui::SetCursorPosX(pad);
    ImGui::PushFont(A.fHeadBold, A.S(34.f)); ImGui::TextUnformatted(g.title.c_str()); ImGui::PopFont();
    ImGui::SetCursorPosX(pad);
    { std::string t; ImVec2 sp = ImGui::GetCursorScreenPos(); StateLine(ImGui::GetWindowDrawList(), sp, g, A.S(20.f));
      ImGui::Dummy(ImVec2(0, A.S(30.f))); (void)t; }
    ImGui::SetCursorPosX(pad);
    Dim("The launcher finds these by itself. Choose a place by hand when it is somewhere else; what you choose always wins, and is checked like a found file.");
    ImGui::Dummy(ImVec2(0, A.S(6.f)));
    std::string bad = BadSlot(g);
    for (int i = 0; i < 4; i++) {
        std::string slot = kSlotKey[i], path = SlotPath(g, i);
        bool overridden = g.overrides.count(slot) > 0, isBad = bad == slot, hasSug = g.suggested.count(slot) > 0;
        std::string key = "g|" + g.id + "|" + slot;
        float rh = A.S(134.f) + (isBad ? A.S(hasSug ? 62.f : 30.f) : 0.f) + (A.errKey == key ? A.S(24.f) : 0.f);
        ImGui::SetCursorPosX(pad);
        ImVec2 q = ImGui::GetCursorScreenPos();
        Panel(q, ImVec2(q.x + availW, q.y + rh), cPanel);
        if (isBad) ImGui::GetWindowDrawList()->AddRect(q, ImVec2(q.x + availW, q.y + rh), Hex(cBad, 0.8f), A.S(10.f), 0, A.S(1.5f));
        ImGui::SetCursorScreenPos(ImVec2(q.x + A.S(20.f), q.y + A.S(12.f)));
        ImGui::PushID(slot.c_str());
        float innerW = availW - A.S(40.f);
        ImGui::BeginGroup();
        // label + where it came from
        ImGui::PushFont(A.fHead, A.S(22.f)); ImGui::PushStyleColor(ImGuiCol_Text, HexV(cAccText)); ImGui::TextUnformatted(kSlotLabel[i]); ImGui::PopStyleColor(); ImGui::PopFont();
        unsigned tc; const char* tag = SourceWord(g, slot, path, tc);
        ImVec2 ts = A.fHeadBold->CalcTextSizeA(A.S(17.f), FLT_MAX, 0, tag);
        ImVec2 tp(q.x + availW - A.S(20.f) - ts.x - A.S(16.f), q.y + A.S(12.f) + A.S(3.f));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(tp, ImVec2(tp.x + ts.x + A.S(16.f), tp.y + ts.y + A.S(4.f)), Hex(cChip), A.S(6.f));
        dl->AddText(A.fHeadBold, A.S(17.f), ImVec2(tp.x + A.S(8.f), tp.y + A.S(2.f)), Hex(tc), tag);
        // the path in use
        ImGui::PushItemWidth(innerW);
        PathLine(path.empty() ? "Nothing found yet" : path, path.empty() ? cDim : isBad ? cBad : cText);
        ImGui::PopItemWidth();
        if (isBad) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + innerW);
            Wrapped(g.reason.c_str(), cBad, 16.f);
            ImGui::PopTextWrapPos();
            if (hasSug) {
                std::string sug = g.suggested.at(slot);
                std::string lab = "Use found: " + Fit(sug, innerW - A.S(120.f), A.fHeadBold, A.S(19.f));
                ImGui::BeginDisabled(EngineBusy());
                if (Btn((lab + "##usefound").c_str(), ImVec2(0, 0), true)) EngineClearOverride(g.id, slot);
                ImGui::EndDisabled();
            }
        }
        ImGui::Dummy(ImVec2(0, A.S(2.f)));
        // actions
        ImGui::BeginDisabled(EngineBusy());
        bool folderSlot = i == 3, fileSlot = i == 2, both = i < 2;
        std::string tagv = "ov|" + g.id + "|" + slot;
        if (folderSlot || both) { if (Btn(both ? "Browse folder##bf" : "Browse##bf", ImVec2(0, 0), false)) BrowseFolder(-1, tagv); ImGui::SameLine(); }
        if (fileSlot || both) { if (Btn(both ? "Browse file##bz" : "Browse##bz", ImVec2(0, 0), false)) BrowseFile(-1, tagv, fileSlot); ImGui::SameLine(); }
        ImGui::EndDisabled();
        if (Btn("Open folder##of", ImVec2(0, 0), false)) OpenFolderOf(path);
        if (overridden) {
            ImGui::SameLine();
            ImGui::BeginDisabled(EngineBusy());
            if (Btn("Use found location##uf", ImVec2(0, 0), false)) EngineClearOverride(g.id, slot);
            ImGui::EndDisabled();
        }
        RowStatus(key);
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(q.x, q.y + rh + A.S(12.f)));
    }
}

// A list location (library folders, excluded subfolders, RPCS3 folders): one row per entry with Remove.
static void LocList(const char* key, const std::vector<std::string>& items, const char* emptyText) {
    std::string rm;
    float btnW = A.S(100.f);
    if (items.empty()) { ImGui::AlignTextToFramePadding(); Wrapped(emptyText, cDim, 16.f); }
    for (size_t i = 0; i < items.size(); i++) {
        ImGui::PushID((int)i);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(A.fBody, A.S(16.f));
        ImGui::TextUnformatted(Fit(items[i], ImGui::GetContentRegionMax().x - btnW - A.S(30.f), A.fBody, A.S(16.f)).c_str());
        ImGui::PopFont();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - btnW);
        ImGui::BeginDisabled(EngineBusy());
        if (Btn("Remove##rm", ImVec2(btnW, 0), false)) rm = items[i];
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (!rm.empty()) EngineLocation(key, rm, "--remove");
}
static float ListH(size_t n) { return A.S(172.f) + A.S(40.f) * std::max<size_t>(1, n); }
static std::string BaseDir() { const char* b = SDL_GetBasePath(); return b ? b : ""; }
// ASSUMPTION: the defaults sit next to the launcher until the installer decides (launcher-plan.md "Locations").
static std::string DefaultLocation(const std::string& key) {
    return BaseDir() + (key == "build" ? "games" : key == "saves" ? "saves" : key == "logs" ? "logs" : "art-cache");
}

static void PadTest() {
    int n = 0; SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (int i = 0; i < n; i++) if (!A.pads.count(ids[i])) { SDL_Gamepad* g = SDL_OpenGamepad(ids[i]); if (g) A.pads[ids[i]] = g; }
    for (auto it = A.pads.begin(); it != A.pads.end();) { if (!SDL_GamepadConnected(it->second)) { SDL_CloseGamepad(it->second); it = A.pads.erase(it); } else ++it; }
    SDL_free(ids);
    if (A.pads.empty()) { Wrapped("No controller found. Connect one and press any button: this list updates by itself.", cDim, 16.f); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    static const struct { SDL_GamepadButton b; const char* n; } bt[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, "A"}, {SDL_GAMEPAD_BUTTON_EAST, "B"}, {SDL_GAMEPAD_BUTTON_WEST, "X"}, {SDL_GAMEPAD_BUTTON_NORTH, "Y"},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "LB"}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "RB"}, {SDL_GAMEPAD_BUTTON_BACK, "Back"}, {SDL_GAMEPAD_BUTTON_START, "Start"},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, "Up"}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, "Down"}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, "Left"}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "Right"}};
    for (auto& kv : A.pads) {
        Heading(SDL_GetGamepadName(kv.second) ? SDL_GetGamepadName(kv.second) : "Controller", 20.f);
        ImVec2 q = ImGui::GetCursorScreenPos(); float x = 0;
        for (auto& b : bt) {
            bool on = SDL_GetGamepadButton(kv.second, b.b);
            ImVec2 ts = A.fHeadBold->CalcTextSizeA(A.S(16.f), FLT_MAX, 0, b.n); float w = ts.x + A.S(20.f);
            dl->AddRectFilled(ImVec2(q.x + x, q.y), ImVec2(q.x + x + w, q.y + A.S(28.f)), on ? Hex(cAcc) : Hex(cPanel2), A.S(6.f));
            dl->AddRect(ImVec2(q.x + x, q.y), ImVec2(q.x + x + w, q.y + A.S(28.f)), on ? Hex(cGold) : Hex(cLine), A.S(6.f));
            dl->AddText(A.fHeadBold, A.S(16.f), ImVec2(q.x + x + A.S(10.f), q.y + A.S(4.f)), on ? Hex(cBtnText) : Hex(cText), b.n);
            x += w + A.S(6.f);
        }
        ImGui::Dummy(ImVec2(0, A.S(34.f)));
        float lx = SDL_GetGamepadAxis(kv.second, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f, ly = SDL_GetGamepadAxis(kv.second, SDL_GAMEPAD_AXIS_LEFTY) / 32767.f;
        float rx = SDL_GetGamepadAxis(kv.second, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.f, ry = SDL_GetGamepadAxis(kv.second, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.f;
        char b[96]; snprintf(b, sizeof b, "Left stick %+.2f %+.2f      Right stick %+.2f %+.2f", lx, ly, rx, ry); Dim(b, 15.f);
    }
}

static void SettingsScreen() {
    float pad = A.S(24.f);
    ImGui::SetCursorPos(ImVec2(pad, A.S(14.f)));
    float availW = std::min(ImGui::GetContentRegionAvail().x - pad * 2, A.S(1000.f));
    pad = std::max(pad, (ImGui::GetContentRegionAvail().x - availW) / 2);
    ImGui::SetCursorPos(ImVec2(pad, A.S(14.f)));
    auto section = [&](const char* title, float h, auto body) {
        ImGui::SetCursorPosX(pad);
        ImVec2 q = ImGui::GetCursorScreenPos();
        Panel(q, ImVec2(q.x + availW, q.y + h));
        ImGui::SetCursorScreenPos(ImVec2(q.x + A.S(20.f), q.y + A.S(14.f)));
        ImGui::BeginChild(title, ImVec2(availW - A.S(40.f), h - A.S(24.f)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        Heading(title, 24.f, cAccText); ImGui::Dummy(ImVec2(0, A.S(4.f)));
        body();
        ImGui::EndChild();
        ImGui::SetCursorScreenPos(ImVec2(q.x, q.y + h + A.S(14.f)));
    };
    ImGui::SetCursorPosX(pad);
    Heading("Locations", 30.f);
    ImGui::SetCursorPosX(pad);
    Dim("The launcher finds everything by itself. Change a place here when yours is somewhere else; each change is made by the setup engine.");
    ImGui::Dummy(ImVec2(0, A.S(6.f)));
    const Locations& L = A.lib.loc;
    auto extra = [&](const char* key) { return A.errKey == std::string("l|") + key ? A.S(26.f) : 0.f; };
    section("Game library folders", ListH(L.libraryFolders.size()) + A.S(38.f) + A.S(40.f) * std::max<size_t>(1, L.excluded.size()) + A.S(52.f) + extra("library_folders") + extra("excluded"), [&]() {
        Dim("The launcher looks in these folders for your game files, including the folders inside them.");
        LocList("library_folders", L.libraryFolders, "No folders yet: the launcher only looks in RPCS3's folders.");
        ImGui::BeginDisabled(EngineBusy());
        if (Btn("Add folder##lf", ImVec2(0, 0), false)) BrowseFolder(-1, "loc|library_folders|--add");
        ImGui::EndDisabled();
        RowStatus("l|library_folders");
        Heading("Left out", 18.f, cDim);
        LocList("excluded", L.excluded, "Nothing is left out.");
        ImGui::BeginDisabled(EngineBusy());
        if (Btn("Exclude subfolder##ex", ImVec2(0, 0), false)) BrowseFolder(-1, "loc|excluded|--add");
        ImGui::EndDisabled();
        RowStatus("l|excluded");
    });
    section("RPCS3 folders", ListH(L.rpcs3Folders.size()) - A.S(8.f) + extra("rpcs3_folders") + extra("rpcs3_auto"), [&]() {
        Dim("Folders of an RPCS3 install to read (read only: games, installed updates and saves; licence files are never touched).");
        LocList("rpcs3_folders", L.rpcs3Folders, "None chosen.");
        ImGui::BeginDisabled(EngineBusy());
        if (Btn("Add folder##rf", ImVec2(0, 0), false)) BrowseFolder(-1, "loc|rpcs3_folders|--add");
        ImGui::SameLine(0, A.S(24.f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, HexV(cPanel2)); ImGui::PushStyleColor(ImGuiCol_CheckMark, HexV(cGold)); ImGui::PushStyleColor(ImGuiCol_Border, HexV(cLine));
        ImGui::PushFont(A.fBodySemi, A.S(18.f));
        bool autoOn = L.rpcs3Auto;
        if (ImGui::Checkbox("Look in the usual places", &autoOn)) EngineLocation("rpcs3_auto", autoOn ? "true" : "false");
        ImGui::PopFont(); ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        RowStatus("l|rpcs3_folders");
        RowStatus("l|rpcs3_auto");
    });
    struct One { const char* key; const char* title; const char* desc; const std::string* value; };
    const One ones[4] = {
        {"build", "Build folder", "Built games go here, one folder per game.", &L.build},
        {"saves", "Saves and settings folder", "Where the games keep saves and settings.", &L.saves},
        {"logs", "Log folder", "Build logs and game logs.", &L.logs},
        {"art_cache", "Art cache folder", "Cover art kept for the library (only filled when cover art is on).", &L.artCache}};
    for (const One& o : ones) {
        section(o.title, A.S(174.f) + extra(o.key), [&]() {
            Dim(o.desc);
            bool set = !o.value->empty();
            std::string def = DefaultLocation(o.key);
            PathLine(set ? *o.value : "Default (" + def + ")", set ? cText : cDim);
            ImGui::BeginDisabled(EngineBusy());
            if (Btn("Browse##b", ImVec2(0, 0), false)) BrowseFolder(-1, std::string("loc|") + o.key + "|");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (Btn("Open##o", ImVec2(0, 0), false)) OpenFolderOf(set ? *o.value : def);
            ImGui::SameLine();
            ImGui::BeginDisabled(EngineBusy() || !set);
            if (Btn("Reset to default##r", ImVec2(0, 0), false)) EngineLocation(o.key, "", "--reset");
            ImGui::EndDisabled();
            RowStatus(std::string("l|") + o.key);
        });
    }
    section("Cover art", A.S(200.f), [&]() {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, HexV(cPanel2)); ImGui::PushStyleColor(ImGuiCol_CheckMark, HexV(cGold)); ImGui::PushStyleColor(ImGuiCol_Border, HexV(cLine));
        ImGui::PushFont(A.fBodySemi, A.S(18.f));
        ImGui::Checkbox("Fetch cover art from the internet", &A.artFetch);
        ImGui::PopFont(); ImGui::PopStyleColor(3);
        Wrapped("Off by default. When it is on, the launcher downloads cover images from IGDB.com by their id and keeps them in a local cache. "
                "No account is needed. Until you turn it on, the launcher makes no network connections, and your own disc art is used when your files have it.", cText, 16.f);
        Dim("Preview: this switch is not wired yet; nothing is fetched in this version.");
    });
    section("Controller test", A.S(200.f), [&]() { PadTest(); });
    section("About", A.S(290.f), [&]() {
        char b[128]; snprintf(b, sizeof b, "Qwark's Awakening launcher %s (preview). Port version %s.", LAUNCHER_VERSION, A.lib.portVersion.empty() ? "unknown" : A.lib.portVersion.c_str());
        Wrapped(b, cText, 16.f);
        ImGui::Dummy(ImVec2(0, A.S(4.f)));
        Heading("Licences", 20.f);
        Wrapped("Dear ImGui: MIT, Copyright (c) 2014-2025 Omar Cornut", cText, 15.f);
        Wrapped("SDL3: zlib, Copyright (c) 1997-2025 Sam Lantinga and SDL contributors", cText, 15.f);
        Wrapped("Fonts: Rajdhani (Indian Type Foundry), Barlow (Jeremy Tribby), Russo One (Jovanny Lemonad), all SIL Open Font License 1.1", cText, 15.f);
        Wrapped("Cover art: IGDB.com (only when you turn the cover art switch on)", cText, 15.f);
        Dim("Game titles and art belong to their owners. This project is not affiliated with them; it needs your own game files.", 14.f);
    });
}

// ---------------------------------------------------------------------------------------------------------------
static void DrawBackdrop(float W, float H) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), Hex(cBg));
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H * 0.6f), Hex(cGlow, 0.85f), Hex(cGlow, 0.85f), Hex(cBg, 0), Hex(cBg, 0));
    Stars(dl, ImVec2(0, 0), ImVec2(W, H * 0.7f), 99, (int)(W / 22), 1.f, 0.35f);
}

static void Header(float W) {
    float hh = A.S(64.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    dl->AddRectFilled(wp, ImVec2(wp.x + W, wp.y + hh), Hex(cBg));
    dl->AddLine(ImVec2(wp.x, wp.y + hh), ImVec2(wp.x + W, wp.y + hh), Hex(cAcc), A.S(2.f));
    // wordmark: Russo One, gold to orange
    const char* mark = "QWARK'S AWAKENING"; float sz = A.S(30.f);
    ImVec2 ts = A.fMark->CalcTextSizeA(sz, FLT_MAX, 0, mark), pos(wp.x + A.S(24.f), wp.y + (hh - ts.y) / 2);
    int v0 = dl->VtxBuffer.Size;
    dl->AddText(A.fMark, sz, pos, IM_COL32_WHITE, mark);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, pos, ImVec2(pos.x + ts.x, pos.y), Hex(cGold), Hex(cAcc));
    ImGui::SetCursorPos(ImVec2(W - A.S(24.f) - A.S(150.f), (hh - A.S(40.f)) / 2));
    if (A.screen == App::ScLibrary) { if (Btn("Settings##hdr", ImVec2(A.S(150.f), A.S(40.f)), false)) A.screen = App::ScSettings; }
    else if (Btn(A.screen == App::ScDetails ? "Back to library##hdr" : "Back##hdr", ImVec2(A.S(150.f), A.S(40.f)), false)) A.screen = App::ScLibrary;
    if (A.screen != App::ScLibrary) {
        ImGui::SetCursorPos(ImVec2(W - A.S(24.f) - A.S(150.f) - A.S(220.f), 0));
        const char* t = A.screen == App::ScSettings ? "SETTINGS" : A.screen == App::ScFiles ? "CHANGE GAME FILES" : "GAME DETAILS";
        ImVec2 s = A.fHead->CalcTextSizeA(A.S(22.f), FLT_MAX, 0, t);
        dl->AddText(A.fHead, A.S(22.f), ImVec2(wp.x + W - A.S(24.f) - A.S(150.f) - A.S(20.f) - s.x, wp.y + (hh - s.y) / 2), Hex(cDim), t);
    }
}

static void Footer(float W, float H) {
    float fh = A.S(40.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    dl->AddRectFilled(ImVec2(wp.x, wp.y + H - fh), ImVec2(wp.x + W, wp.y + H), Hex(cBg));
    dl->AddLine(ImVec2(wp.x, wp.y + H - fh), ImVec2(wp.x + W, wp.y + H - fh), Hex(cLine), 1.f);
    int ready = 0; for (auto& g : A.lib.games) if (Playable(g)) ready++;
    char b[96]; snprintf(b, sizeof b, "%d of %d games ready", ready, (int)A.lib.games.size());
    float ty = wp.y + H - fh + (fh - A.S(18.f)) / 2;
    dl->AddText(A.fHeadBold, A.S(18.f), ImVec2(wp.x + A.S(24.f), ty), Hex(cText), b);
    // middle: toast or hint
    bool toast = ImGui::GetTime() < A.toast.until;
    const char* mid = toast ? A.toast.text.c_str() : "Enter / A: choose     Esc / B: back     arrows / D-pad: move";
    ImVec2 ms = A.fBody->CalcTextSizeA(A.S(15.f), FLT_MAX, 0, mid);
    dl->AddText(A.fBody, A.S(15.f), ImVec2(wp.x + (W - ms.x) / 2, wp.y + H - fh + (fh - ms.y) / 2), toast ? Hex(cGold) : Hex(cDim), mid);
    // right: build queue
    std::string q = "Build queue: empty";
    if (A.job.game >= 0) { char c[160]; snprintf(c, sizeof c, "Building %s: %s, %d%%", A.lib.games[A.job.game].id.c_str(), kStages[std::min(4, (int)(A.job.t / kBuildSeconds * 5))], (int)(std::min(1.f, A.job.t / kBuildSeconds) * 100)); q = c; }
    ImVec2 qs = A.fBodySemi->CalcTextSizeA(A.S(15.f), FLT_MAX, 0, q.c_str());
    dl->AddText(A.fBodySemi, A.S(15.f), ImVec2(wp.x + W - A.S(24.f) - qs.x, wp.y + H - fh + (fh - qs.y) / 2), A.job.game >= 0 ? Hex(cAccText) : Hex(cDim), q.c_str());
}

static void ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.ScaleAllSizes(A.scale);
    s.FontSizeBase = 17.f * A.scale;
    s.WindowBorderSize = 0; s.FrameBorderSize = 1; s.FrameRounding = A.S(7.f); s.ChildRounding = A.S(8.f); s.PopupRounding = A.S(8.f);
    s.FramePadding = ImVec2(A.S(18.f), A.S(9.f)); s.ItemSpacing = ImVec2(A.S(10.f), A.S(8.f)); s.ScrollbarSize = A.S(12.f); s.ItemInnerSpacing = ImVec2(A.S(10.f), A.S(4.f));
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = HexV(cText); c[ImGuiCol_TextDisabled] = HexV(cDim); c[ImGuiCol_WindowBg] = HexV(cBg, 0); c[ImGuiCol_ChildBg] = HexV(cBg, 0);
    c[ImGuiCol_PopupBg] = HexV(cPanel2); c[ImGuiCol_Border] = HexV(cLine); c[ImGuiCol_FrameBg] = HexV(cBg); c[ImGuiCol_FrameBgHovered] = HexV(cPanel2);
    c[ImGuiCol_FrameBgActive] = HexV(cPanel2); c[ImGuiCol_NavCursor] = HexV(cGold); c[ImGuiCol_ScrollbarBg] = HexV(cBg, 0);
    c[ImGuiCol_ScrollbarGrab] = HexV(cLine); c[ImGuiCol_ScrollbarGrabHovered] = HexV(cSteel); c[ImGuiCol_ScrollbarGrabActive] = HexV(cGold);
    c[ImGuiCol_Header] = HexV(cChip); c[ImGuiCol_HeaderHovered] = HexV(cChip); c[ImGuiCol_HeaderActive] = HexV(cLine);
    c[ImGuiCol_Separator] = HexV(cLine); c[ImGuiCol_TextSelectedBg] = HexV(cAcc, 0.35f);
}

static void Frame(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float ns = std::clamp(H / 720.f, 0.75f, 3.f);
    if (std::fabs(ns - A.scale) > 0.01f) { A.scale = ns; ApplyStyle(); }
    PumpDialog();
    PumpEngine();
    TickBuild(dt);
    for (auto& g : A.lib.games) {   // art regains colour: a short fade, the one motion that matters
        float d = g.colourTarget - g.colour, step = dt / 0.9f;
        g.colour = A.shotMode && A.firstFrame ? g.colourTarget : (std::fabs(d) < step ? g.colourTarget : g.colour + (d > 0 ? step : -step));
    }
    DrawBackdrop(W, H);
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    Header(W);
    float top = A.S(64.f) + A.S(2.f), fh = A.S(40.f);
    ImGui::SetCursorPos(ImVec2(0, top));
    ImGui::BeginChild("body", ImVec2(W, H - top - fh), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
    if (A.screen == App::ScLibrary) LibraryScreen(W);
    else if (A.screen == App::ScDetails) DetailsScreen();
    else if (A.screen == App::ScFiles) FilesScreen();
    else SettingsScreen();
    if (A.shotScroll >= 0 && A.shotMode) ImGui::SetScrollY(A.S(A.shotScroll));
    ImGui::Dummy(ImVec2(0, A.S(16.f)));
    ImGui::EndChild();
    Footer(W, H);
    // Escape / controller B: back out of a screen when nothing else wants the key
    if (!A.addOpen && A.screen != App::ScLibrary && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !ImGui::IsAnyItemActive() &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))) A.screen = App::ScLibrary;
    ImGui::End();
    if (A.addOpen) AddDrawer(W, H);
    if (A.firstFrame) ImGui::SetNavCursorVisible(true);
    A.firstFrame = false;
}

// ---------------------------------------------------------------------------------------------------------------
static ImFont* LoadFont(const char* file) {
    std::string p = A.fontDir + "/" + file;
    ImFont* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(p.c_str(), 18.f);
    if (!f) { fprintf(stderr, "launcher: cannot load font %s\n", p.c_str()); }
    return f;
}

int main(int argc, char** argv) {
    std::string shot, screenName = "library", selectId, engine; int W = 1280, H = 720; float progress = -1.f;
    const char* base = SDL_GetBasePath();
    A.libPath = std::string(base ? base : "") + "library.json";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i]; auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--library") A.libPath = next();
        else if (a == "--screenshot") shot = next();
        else if (a == "--screen") screenName = next();
        else if (a == "--select") selectId = next();
        else if (a == "--size") sscanf(next().c_str(), "%dx%d", &W, &H);
        else if (a == "--progress") progress = (float)atof(next().c_str());   // screenshots: show the fake build at this fraction
        else if (a == "--fonts") A.fontDir = next();
        else if (a == "--engine") engine = next();
        else if (a == "--scroll") A.shotScroll = (float)atof(next().c_str());   // screenshots: scroll the screen down by this many px (at 720 high)
        else { fprintf(stderr, "usage: launcher [--library FILE] [--engine \"CMD\"] [--screenshot OUT.png --screen library|details|add|settings|locations|files [--select ID] [--size WxH] [--progress F] [--scroll PX]]\n"); return 2; }
    }
    if (engine.empty()) { const char* e = getenv("RC_LAUNCHER_ENGINE"); engine = e && *e ? e : "python3 tools/release/catalog.py"; }
    A.engineCmd = SplitCommand(engine);
    A.shotMode = !shot.empty();
    if (A.fontDir.empty()) {
        std::string d = std::string(base ? base : "") + "fonts"; SDL_PathInfo pi;
        A.fontDir = SDL_GetPathInfo((d + "/Barlow-Regular.ttf").c_str(), &pi) ? d : LAUNCHER_FONT_DIR;
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
    A.fBody = LoadFont("Barlow-Regular.ttf"); A.fBodySemi = LoadFont("Barlow-SemiBold.ttf");
    A.fHead = LoadFont("Rajdhani-SemiBold.ttf"); A.fHeadBold = LoadFont("Rajdhani-Bold.ttf"); A.fHeadMed = LoadFont("Rajdhani-Medium.ttf");
    A.fMark = LoadFont("RussoOne-Regular.ttf");
    if (!A.fBody || !A.fBodySemi || !A.fHead || !A.fHeadBold || !A.fMark) return 1;
    A.scale = 0.f;

    A.lib = loadLibrary(A.libPath);
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
        else if (screenName == "add") {
            if (selectId.empty()) for (size_t i = 0; i < A.lib.games.size(); i++) if (A.lib.games[i].state == State::ReadyToBuild) { A.sel = (int)i; break; }
            if (!A.lib.games.empty()) OpenAdd(A.lib.games[A.sel]);
            if (progress >= 0 && !A.lib.games.empty()) { StartBuild(A.sel); A.job.t = progress * kBuildSeconds; }
        }
    }

    bool run = true; int frames = 0; Uint64 last = SDL_GetTicks(); int rc = 0;
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) { ImGui_ImplSDL3_ProcessEvent(&e); if (e.type == SDL_EVENT_QUIT) run = false; }
        ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
        Uint64 now = SDL_GetTicks(); float dt = A.shotMode ? 1.f / 60.f : std::min(0.1f, (now - last) / 1000.f); last = now;
        Frame(dt);
        ImGui::Render();
        SDL_SetRenderDrawColor(A.ren, 7, 11, 22, 255); SDL_RenderClear(A.ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), A.ren);
        frames++;
        if (A.shotMode && frames >= 14) {
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
