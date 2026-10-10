#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <iterator>
#include <cwctype>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace fs = std::filesystem;
using namespace Gdiplus;

namespace {
constexpr int SIDEBAR_W = 228;
constexpr int HEADER_H = 92;
constexpr int IDI_ANIMA_STUDIO = 101;
enum Page { Home, Learn, Examples, GraphicsLab, Tools, About };
enum class HitKind { Navigate, Action };
struct Hit { RECT rect{}; HitKind kind{}; int page{}; std::wstring action; };

HWND g_hwnd{};
ULONG_PTR g_gdiplusToken{};
fs::path g_appDir;
std::unique_ptr<Image> g_logo;
std::vector<Hit> g_hits;
Page g_page = Home;
bool g_dark = false;
POINT g_mouse{-1,-1};
std::wstring g_status = L"Ready to build.";

Color RGBc(BYTE r, BYTE g, BYTE b) { return Color(255, r, g, b); }
Color MainBg() { return g_dark ? RGBc(17,19,24) : RGBc(246,247,250); }
Color CardBg() { return g_dark ? RGBc(28,31,39) : RGBc(255,255,255); }
Color TextMain() { return g_dark ? RGBc(241,243,247) : RGBc(25,27,32); }
Color TextMuted() { return g_dark ? RGBc(160,167,179) : RGBc(106,113,125); }
Color LineColor() { return g_dark ? RGBc(50,54,64) : RGBc(229,232,238); }
Color Accent() { return RGBc(74,108,245); }
Color Sidebar() { return RGBc(18,21,28); }

std::wstring QuoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\""; unsigned slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') { out.append(slashes * 2 + 1, L'\\'); out += L'"'; slashes = 0; continue; }
        out.append(slashes, L'\\'); slashes = 0; out += c;
    }
    out.append(slashes * 2, L'\\'); out += L'"'; return out;
}
fs::path AppPath(const wchar_t* rel) { return g_appDir / rel; }
bool Exists(const wchar_t* rel) { std::error_code ec; return fs::is_regular_file(AppPath(rel), ec); }

void Text(Graphics& g, const std::wstring& value, float x, float y, float w, float h,
          float size, Color color, bool bold = false,
          StringAlignment align = StringAlignmentNear,
          StringAlignment lineAlign = StringAlignmentNear) {
    FontFamily family(L"Segoe UI");
    Font font(&family, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(color);
    StringFormat fmt; fmt.SetAlignment(align); fmt.SetLineAlignment(lineAlign);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter); fmt.SetFormatFlags(StringFormatFlagsLineLimit);
    g.DrawString(value.c_str(), static_cast<INT>(value.size()), &font,
                 RectF(x, y, w, h), &fmt, &brush);
}
void RoundFill(Graphics& g, RectF r, float radius, Color color) {
    GraphicsPath p;
    const float d = radius * 2.0f;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush b(color); g.FillPath(&b, &p);
}
void RoundBorder(Graphics& g, RectF r, float radius, Color color, float width = 1.0f) {
    GraphicsPath p;
    const float d = radius * 2.0f;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
    Pen pen(color, width); g.DrawPath(&pen, &p);
}
void AddHit(RectF r, std::wstring action) {
    g_hits.push_back({ RECT{static_cast<LONG>(r.X), static_cast<LONG>(r.Y), static_cast<LONG>(r.X+r.Width), static_cast<LONG>(r.Y+r.Height)}, HitKind::Action, 0, std::move(action) });
}
void AddNavHit(RectF r, Page p) {
    g_hits.push_back({ RECT{static_cast<LONG>(r.X), static_cast<LONG>(r.Y), static_cast<LONG>(r.X+r.Width), static_cast<LONG>(r.Y+r.Height)}, HitKind::Navigate, static_cast<int>(p), {} });
}
bool Hovered(const RECT& r) { return g_mouse.x >= r.left && g_mouse.x < r.right && g_mouse.y >= r.top && g_mouse.y < r.bottom; }
void Button(Graphics& g, RectF r, const std::wstring& label, const std::wstring& action,
            bool primary = false, bool compact = false) {
    Color fill = primary ? Accent() : (g_dark ? RGBc(39,43,53) : RGBc(244,246,249));
    Color ink = primary ? RGBc(255,255,255) : TextMain();
    RectF rect = r;
    if (g_mouse.x >= rect.X && g_mouse.x < rect.X + rect.Width && g_mouse.y >= rect.Y && g_mouse.y < rect.Y + rect.Height)
        fill = primary ? RGBc(56,91,228) : (g_dark ? RGBc(52,57,69) : RGBc(234,238,245));
    RoundFill(g, rect, compact ? 9.0f : 12.0f, fill);
    Text(g, label, rect.X + 10, rect.Y + 1, rect.Width - 20, rect.Height - 2,
         compact ? 12.5f : 13.5f, ink, true, StringAlignmentCenter, StringAlignmentCenter);
    AddHit(rect, action);
}
void Card(Graphics& g, RectF r, const std::wstring& eyebrow, const std::wstring& title,
          const std::wstring& detail) {
    RoundFill(g, r, 18.0f, CardBg());
    RoundBorder(g, r, 18.0f, LineColor(), 1.0f);
    Text(g, eyebrow, r.X + 20, r.Y + 16, r.Width - 40, 20, 10.5f, Accent(), true);
    Text(g, title, r.X + 20, r.Y + 40, r.Width - 40, 28, 18.0f, TextMain(), true);
    Text(g, detail, r.X + 20, r.Y + 70, r.Width - 40, 28, 12.0f, TextMuted());
}
void Heading(Graphics& g, const std::wstring& title, const std::wstring& subtitle, float x, float y, float w) {
    Text(g, title, x, y, w, 38, 27.0f, TextMain(), true);
    Text(g, subtitle, x, y + 42, w, 32, 13.0f, TextMuted());
}
void NavItem(Graphics& g, float y, Page p, const std::wstring& glyph, const std::wstring& label) {
    RectF r(14, y, SIDEBAR_W - 28, 42);
    const bool active = g_page == p;
    const bool hover = g_mouse.x >= r.X && g_mouse.x < r.X+r.Width && g_mouse.y >= r.Y && g_mouse.y < r.Y+r.Height;
    if (active) RoundFill(g, r, 11, RGBc(49,54,66));
    else if (hover) RoundFill(g, r, 11, RGBc(35,39,48));
    Text(g, glyph, r.X+13, r.Y+2, 24, 38, 17, active ? RGBc(255,255,255) : RGBc(158,168,186), true, StringAlignmentCenter, StringAlignmentCenter);
    Text(g, label, r.X+47, r.Y+2, r.Width-55, 38, 13.5f, active ? RGBc(255,255,255) : RGBc(184,190,201), active, StringAlignmentNear, StringAlignmentCenter);
    AddNavHit(r, p);
}
void DrawHeader(Graphics& g, int width) {
    const float x = SIDEBAR_W + 32.0f;
    const wchar_t* titles[] = {L"Your workspace", L"Learn ANIMA", L"Examples", L"Graphics lab", L"Developer tools", L"About this preview"};
    const wchar_t* subtitles[] = {L"One native place to build, explore, and test.", L"Explore the language, runtime and engine boundary.", L"Run real ANIMA source files with the 0.3 runtime.", L"Open the native renderer and launch media previews.", L"Run diagnostics, choose a file, or create a project.", L"A shareable beta build from Unrecorded Labs."};
    Text(g, titles[g_page], x, 26, 590, 38, 27, TextMain(), true);
    Text(g, subtitles[g_page], x, 62, 650, 22, 12.5f, TextMuted());
    Button(g, RectF(static_cast<float>(width)-160.0f, 30, 128, 34), g_dark ? L"Light appearance" : L"Dark appearance", L"toggle-theme", false, true);
}
void DrawHome(Graphics& g, int width) {
    const float x=SIDEBAR_W+32.0f, w=static_cast<float>(width)-x-32.0f;
    RectF hero(x, 104, w, 190);
    LinearGradientBrush grad(hero, RGBc(18,22,34), RGBc(43,52,83), LinearGradientModeHorizontal);
    GraphicsPath hp; float d=34;
    hp.AddArc(hero.X,hero.Y,d,d,180,90); hp.AddArc(hero.X+hero.Width-d,hero.Y,d,d,270,90);
    hp.AddArc(hero.X+hero.Width-d,hero.Y+hero.Height-d,d,d,0,90); hp.AddArc(hero.X,hero.Y+hero.Height-d,d,d,90,90); hp.CloseFigure();
    g.FillPath(&grad,&hp);
    RoundBorder(g,hero,17,RGBc(66,75,103));
    RoundFill(g,RectF(x+24,hero.Y+15,116,25),12,RGBc(41,48,68));
    Text(g,L"ANIMA NATIVE 0.3 BETA",x+29,hero.Y+17,107,21,9.5f,RGBc(208,216,237),true,StringAlignmentCenter,StringAlignmentCenter);
    Text(g,L"A language with\na native soul.",x+26,hero.Y+43, w-270,71,27,RGBc(255,255,255),true);
    Text(g,L"Write ANIMA. Run it natively. Explore the engine.",x+28,hero.Y+115,w-300,22,12.5f,RGBc(207,214,231));
    Button(g,RectF(x+26,hero.Y+150,158,28),L"Open documentation",L"docs",true,true);
    Button(g,RectF(x+194,hero.Y+150,135,28),L"Run first example",L"run|fibonacci.ani",false,true);
    if(g_logo && g_logo->GetLastStatus()==Ok) g.DrawImage(g_logo.get(),RectF(x+w-174,hero.Y+42,112,94));
    Text(g,L"QUICK LAUNCH",x,310,500,22,11,TextMuted(),true);
    const float gap=12, cw=(w-gap)/2, ch=135, y1=340, y2=340+ch+12;
    Card(g,RectF(x,y1,cw,ch),L"LEARN",L"Language & docs",L"Syntax, native runtime, capabilities and roadmap.");
    Button(g,RectF(x+20,y1+99,126,28),L"Browse guide",L"docs",false,true);
    Card(g,RectF(x+cw+gap,y1,cw,ch),L"EXAMPLES",L"Run an ANIMA program",L"Try recursion, collections, simulation and GPU queries.");
    Button(g,RectF(x+cw+gap+20,y1+99,122,28),L"Explore examples",L"nav-examples",false,true);
    Card(g,RectF(x,y2,cw,ch),L"NATIVE GRAPHICS",L"Room & lighting demo",L"Launch the Direct3D 12 room prototype on Windows.");
    Button(g,RectF(x+20,y2+99,136,28),L"Launch room",L"room",true,true);
    Card(g,RectF(x+cw+gap,y2,cw,ch),L"SHAREABLE BUILD",L"Tester-ready output",L"The Release package can run without Visual Studio installed.");
    Button(g,RectF(x+cw+gap+20,y2+99,142,28),L"Open app folder",L"open-release",false,true);
}
struct ExampleItem { const wchar_t* title; const wchar_t* description; const wchar_t* path; bool legacy; bool browser; };
const ExampleItem examples[] = {
 {L"Fibonacci & functions",L"Recursive calls, parameters, return values.",L"fibonacci.ani",false,false},
 {L"Machine inspector",L"CPU information and native capability queries.",L"machine-inspector.ani",false,false},
 {L"Fixed-step simulation",L"A deterministic update loop in ANIMA code.",L"fixed-step-simulation.ani",false,false},
 {L"Records & enums",L"Typed records, defaults and enum values.",L"enums-and-records.ani",false,false},
 {L"Map iteration",L"Iterate ordered keys and values.",L"map-iteration.ani",false,false},
 {L"Local modules",L"Functions imported from a sibling module.",L"Modules\\module-use.ani",false,false},
 {L"0.2 compatibility",L"The preserved reference-language runtime.",L"native-power.ani",true,false},
 {L"Legacy UI host",L"Open a browser-based legacy example.",L"hello-host.html",false,true},
 {L"WebGPU reference",L"Open the browser GPU demo, if available.",L"webgpu-host.html",false,true},
};
void DrawExamples(Graphics& g, int width) {
    const float x=SIDEBAR_W+32.0f, w=static_cast<float>(width)-x-32.0f;
    Heading(g,L"Example gallery",L"Every listed .ani program ships with this build. Run opens a visible console window.",x,112,w);
    const float gap=10, cw=(w-gap*2)/3, ch=126, top=188;
    for(size_t i=0;i<std::size(examples);++i){
        const int row=static_cast<int>(i/3), col=static_cast<int>(i%3);
        float cx=x+col*(cw+gap), cy=top+row*(ch+gap);
        Card(g,RectF(cx,cy,cw,ch),examples[i].legacy?L"LEGACY RUNTIME":L"ANIMA SOURCE",examples[i].title,examples[i].description);
        if (examples[i].browser) {
            Button(g,RectF(cx+16,cy+98,140,25),L"Open preview",std::wstring(L"browser|")+examples[i].path,false,true);
        } else {
            Button(g,RectF(cx+16,cy+98,80,25),examples[i].legacy?L"Guide":L"Check",examples[i].legacy?L"guide-02":std::wstring(L"check|")+examples[i].path,false,true);
            Button(g,RectF(cx+104,cy+98,74,25),L"Run",std::wstring(L"run|")+examples[i].path+(examples[i].legacy?L"|legacy":L""),true,true);
        }
    }
    const float bottom=top+3*(ch+gap)+2;
    Button(g,RectF(x,bottom,w*0.28f,32),L"Choose an .ani file to run",L"browse-run",false,true);
    Button(g,RectF(x+w*0.30f,bottom,w*0.28f,32),L"Choose an .ani file to check",L"browse-check",false,true);
    Button(g,RectF(x+w*0.60f,bottom,w*0.30f,32),L"Open all examples",L"open-examples",false,true);
}
void DrawLearn(Graphics& g, int width) {
    const float x=SIDEBAR_W+32.0f, w=static_cast<float>(width)-x-32.0f;
    Heading(g,L"Learn the language",L"The guide distinguishes working preview features from the full 0.3 specification.",x,112,w);
    const float gap=14,cw=(w-gap)/2,ch=154,top=198;
    Card(g,RectF(x,top,cw,ch),L"START HERE",L"ANIMA documentation",L"Read the native docs: language syntax, examples, GPU bridge and native roadmap.");
    Button(g,RectF(x+20,top+112,130,28),L"Open docs app",L"docs",true,true);
    Card(g,RectF(x+cw+gap,top,cw,ch),L"LANGUAGE 0.3",L"Specification & guide",L"Grammar, type system, bytecode, VM and known gaps.");
    Button(g,RectF(x+cw+gap+20,top+112,138,28),L"Read 0.3 guide",L"guide-03",false,true);
    Card(g,RectF(x,top+ch+gap,cw,ch),L"LEGACY 0.2",L"Compatibility reference",L"Learn the earlier syntax and use the reference runner.");
    Button(g,RectF(x+20,top+ch+gap+112,154,28),L"Open 0.2 reference",L"guide-02",false,true);
    Card(g,RectF(x+cw+gap,top+ch+gap,cw,ch),L"BUILD SYSTEM",L"Compile and share",L"Follow the build instructions and create a portable Release folder for testers.");
    Button(g,RectF(x+cw+gap+20,top+ch+gap+112,143,28),L"Build instructions",L"build-guide",false,true);
}
void DrawGraphics(Graphics& g, int width) {
    const float x=SIDEBAR_W+32.0f, w=static_cast<float>(width)-x-32.0f;
    Heading(g,L"Graphics lab",L"Native room rendering plus the earlier GPU and teaser experiments.",x,112,w);
    const float gap=10,cw=(w-gap)/2,ch=133,top=190;
    Card(g,RectF(x,top,cw,ch),L"DIRECT3D 12",L"Room & lighting",L"Launch the native room demo. The current renderer is a prototype, not a full DXR engine.");
    Button(g,RectF(x+20,top+99,138,26),L"Launch native room",L"room",true,true);
    Card(g,RectF(x+cw+gap,top,cw,ch),L"RAY-TRACING STUDY",L"ON / OFF comparison",L"View the reference comparison image and notes about the ray-marched prototype.");
    Button(g,RectF(x+cw+gap+20,top+99,135,26),L"View comparison",L"ray-compare",false,true);
    Card(g,RectF(x,top+ch+gap,cw,ch),L"LAUNCH MEDIA",L"20-second teaser",L"ANIMA identity reveal and Coming Soon card.");
    Button(g,RectF(x+20,top+ch+gap+99,116,26),L"Play teaser",L"teaser20",false,true);
    Card(g,RectF(x+cw+gap,top+ch+gap,cw,ch),L"LAUNCH MEDIA",L"45-second teaser",L"The original cinematic language and motion reveal.");
    Button(g,RectF(x+cw+gap+20,top+ch+gap+99,116,26),L"Play teaser",L"teaser45",false,true);
    Card(g,RectF(x,top+2*(ch+gap),cw,144),L"WEBGPU",L"Browser reference host",L"This older preview may require a compatible browser and secure context.");
    Button(g,RectF(x+20,top+2*(ch+gap)+99,152,26),L"Open GPU preview",L"webgpu",false,true);
    Card(g,RectF(x+cw+gap,top+2*(ch+gap),cw,144),L"ASSETS",L"Explore graphics files",L"Browse shader source, room source, icons and launch media.");
    Button(g,RectF(x+cw+gap+20,top+2*(ch+gap)+99,139,26),L"Open asset folder",L"open-assets",false,true);
}
void DrawTools(Graphics& g, int width) {
    const float x=SIDEBAR_W+32.0f, w=static_cast<float>(width)-x-32.0f;
    Heading(g,L"Developer tools",L"Check your package, run tests, select source files and scaffold a new project.",x,112,w);
    const float gap=10,cw=(w-gap)/2,ch=130,top=190;
    Card(g,RectF(x,top,cw,ch),L"DIAGNOSTICS",L"Check installation",L"Verify companion applications in the current folder.");
    Button(g,RectF(x+20,top+98,127,26),L"Run doctor",L"doctor",false,true);
    Card(g,RectF(x+cw+gap,top,cw,ch),L"TESTING",L"ANIMA self-tests",L"Run the 0.3 VM's integrated self-test suite in a console.");
    Button(g,RectF(x+cw+gap+20,top+98,118,26),L"Run self-tests",L"self-test",true,true);
    Card(g,RectF(x,top+ch+gap,cw,ch),L"NEW PROJECT",L"Starter generator",L"Create a small .ani project in a folder you choose.");
    Button(g,RectF(x+20,top+ch+gap+98,152,26),L"Create project",L"create-project",false,true);
    Card(g,RectF(x+cw+gap,top+ch+gap,cw,ch),L"SOURCE",L"Open example folder",L"Browse all bundled .ani, .anima and host examples.");
    Button(g,RectF(x+cw+gap+20,top+ch+gap+98,141,26),L"Browse examples",L"open-examples",false,true);
    Card(g,RectF(x,top+2*(ch+gap),cw,ch),L"RELEASE",L"Portable app folder",L"This folder is the intended runtime location for a packaged Beta build.");
    Button(g,RectF(x+20,top+2*(ch+gap)+98,130,26),L"Open app folder",L"open-release",false,true);
    Card(g,RectF(x+cw+gap,top+2*(ch+gap),cw,ch),L"SOURCE PICKER",L"Run any .ani file",L"Select an ANIMA program outside the bundled samples.");
    Button(g,RectF(x+cw+gap+20,top+2*(ch+gap)+98,112,26),L"Select file",L"browse-run",false,true);
}
void DrawAbout(Graphics& g, int width) {
    const float x=SIDEBAR_W+32.0f, w=static_cast<float>(width)-x-32.0f;
    Heading(g,L"About ANIMA Studio",L"A native, portable app for the ANIMA Native 0.3 Beta preview.",x,112,w);
    RectF hero(x,190,w,145); RoundFill(g,hero,18,CardBg()); RoundBorder(g,hero,18,LineColor());
    if(g_logo && g_logo->GetLastStatus()==Ok) g.DrawImage(g_logo.get(),RectF(x+24,hero.Y+28,100,84));
    Text(g,L"ANIMA NATIVE 0.3 BETA",x+146,hero.Y+22,w-175,25,11,Accent(),true);
    Text(g,L"Code × Motion × Interfaces",x+146,hero.Y+50,w-175,34,24,TextMain(),true);
    Text(g,L"Unrecorded Labs · Native C++ runtime · Visual Studio source included",x+146,hero.Y+88,w-175,24,12,TextMuted());
    Card(g,RectF(x,378,w,144),L"PORTABLE DISTRIBUTION",L"Compile once, share the folder",L"The Release packaging script stages static-CRT binaries, docs, examples, shaders and teaser media. Test on a clean Windows PC before sharing widely.");
    Button(g,RectF(x+20,449,148,26),L"Open build guide",L"build-guide",true,true);
    Card(g,RectF(x,494,w,139),L"KNOWN BETA LIMITATIONS",L"Not all 0.3 spec items are finished",L"The bytecode VM is a preview. Some class/trait/closure and exception semantics, full migration coverage, native UI rendering, dedicated DXR and AOT remain unfinished.");
    Button(g,RectF(x+20,594,151,26),L"Read full spec",L"spec",false,true);
}
void DrawSidebar(Graphics& g, int height) {
    SolidBrush bg(Sidebar()); g.FillRectangle(&bg,0,0,SIDEBAR_W,height);
    if(g_logo && g_logo->GetLastStatus()==Ok) g.DrawImage(g_logo.get(),RectF(24,23,40,35));
    Text(g,L"ANIMA",72,21,130,25,20,RGBc(250,251,253),true);
    Text(g,L"NATIVE STUDIO",73,46,130,15,9.2f,RGBc(146,155,172),true);
    RoundFill(g,RectF(22,83,151,25),12,RGBc(38,43,55));
    {
        SolidBrush dot(RGBc(92,210,153)); g.FillEllipse(&dot,RectF(33,92,7,7));
    }
    Text(g,L"BETA PREVIEW",46,85,118,21,9.5f,RGBc(194,201,214),true,StringAlignmentNear,StringAlignmentCenter);
    Text(g,L"WORKSPACE",24,138,160,22,10,RGBc(129,139,157),true);
    NavItem(g,164,Home,L"⌂",L"Home");
    NavItem(g,211,Learn,L"◫",L"Learn");
    NavItem(g,258,Examples,L"</>",L"Examples");
    NavItem(g,305,GraphicsLab,L"◈",L"Graphics lab");
    NavItem(g,352,Tools,L"⚙",L"Developer tools");
    NavItem(g,399,About,L"ⓘ",L"About Beta");
    Pen divider(RGBc(45,49,60),1); g.DrawLine(&divider,20,height-76,SIDEBAR_W-20,height-76);
    Text(g,L"UNRECORDED LABS",24,height-63,SIDEBAR_W-48,20,9.2f,RGBc(133,144,163),true);
    Text(g,L"ANIMA Native 0.3",24,height-42,SIDEBAR_W-48,22,12,RGBc(217,222,231),true);
}
void DrawWindow(HDC hdc, const RECT& rc) {
    const int width=rc.right-rc.left, height=rc.bottom-rc.top;
    g_hits.clear();
    Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    SolidBrush main(MainBg()); g.FillRectangle(&main,0,0,width,height);
    DrawSidebar(g,height);
    DrawHeader(g,width);
    switch(g_page){case Home:DrawHome(g,width);break;case Learn:DrawLearn(g,width);break;case Examples:DrawExamples(g,width);break;case GraphicsLab:DrawGraphics(g,width);break;case Tools:DrawTools(g,width);break;case About:DrawAbout(g,width);break;}
    // persistent status bar
    Pen line(LineColor(),1); g.DrawLine(&line,SIDEBAR_W,height-37,width,height-37);
    SolidBrush dot(RGBc(64,182,124)); g.FillEllipse(&dot,RectF(SIDEBAR_W+34.0f,height-24.0f,7,7));
    Text(g,g_status,SIDEBAR_W+49.0f,height-31.0f,width-SIDEBAR_W-255.0f,25.0f,10.5f,TextMuted(),false,StringAlignmentNear,StringAlignmentCenter);
    Text(g,L"ANIMA NATIVE 0.3 BETA",static_cast<float>(width)-205.0f,height-31.0f,180.0f,25.0f,9.5f,TextMuted(),true,StringAlignmentFar,StringAlignmentCenter);
}

bool Launch(const fs::path& target, const std::wstring& params = L"") {
    std::error_code ec;
    if (!fs::exists(target,ec)) {
        MessageBoxW(g_hwnd,(L"This component isn't present in the current folder:\n\n"+target.wstring()+L"\n\nBuild the full solution or use the complete portable package.").c_str(),L"ANIMA component not found",MB_OK|MB_ICONWARNING);
        g_status = L"Missing component: " + target.filename().wstring(); InvalidateRect(g_hwnd,nullptr,FALSE); return false;
    }
    HINSTANCE result=ShellExecuteW(g_hwnd,L"open",target.c_str(),params.empty()?nullptr:params.c_str(),g_appDir.c_str(),SW_SHOWNORMAL);
    if(reinterpret_cast<INT_PTR>(result)<=32){
        MessageBoxW(g_hwnd,(L"Windows could not open:\n"+target.wstring()+L"\n\nError code: "+std::to_wstring(reinterpret_cast<INT_PTR>(result))).c_str(),L"ANIMA launch error",MB_OK|MB_ICONERROR);
        return false;
    }
    g_status=L"Launched "+target.filename().wstring(); InvalidateRect(g_hwnd,nullptr,FALSE); return true;
}
void RunCli(const std::wstring& args) { Launch(AppPath(L"anima.exe"),args); }
void OpenFolder(const fs::path& path) {
    std::error_code ec; fs::create_directories(path,ec);
    HINSTANCE result = ShellExecuteW(g_hwnd, L"open", L"explorer.exe", QuoteArg(path.wstring()).c_str(), g_appDir.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) MessageBoxW(g_hwnd, L"Could not open File Explorer for that folder.", L"ANIMA Studio", MB_OK | MB_ICONWARNING);
}
void OpenMarkdown(const wchar_t* relative) { Launch(AppPath(relative)); }
void RunExample(const std::wstring& rel, bool check, bool legacy=false) {
    fs::path file = legacy ? AppPath(L"AnimaLangExamples") / rel : AppPath(L"Anima03Examples") / rel;
    std::error_code ec;
    if(!fs::is_regular_file(file,ec)) {
        // Documentation examples are also copied under Examples in the portable package.
        file = AppPath(L"Examples") / rel;
    }
    if(!fs::is_regular_file(file,ec)) {
        MessageBoxW(g_hwnd,(L"Example source was not found:\n"+file.wstring()).c_str(),L"Example not found",MB_OK|MB_ICONWARNING); return;
    }
    std::wstring args;
    if(legacy) args=L"legacy "+QuoteArg(file.wstring());
    else args=(check?L"check ":L"run ")+QuoteArg(file.wstring());
    RunCli(args);
}
void BrowseAni(bool check) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_hwnd; ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
    ofn.lpstrFilter=L"ANIMA source (*.ani)\0*.ani\0All files (*.*)\0*.*\0\0";
    ofn.nFilterIndex=1; ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER;
    const fs::path initialDir=AppPath(L"Anima03Examples"); ofn.lpstrInitialDir=initialDir.c_str(); ofn.lpstrTitle=check?L"Select ANIMA source to check":L"Select ANIMA source to run";
    if(GetOpenFileNameW(&ofn)) {
        std::ifstream source(fs::path(file), std::ios::binary);
        std::string firstLine; std::getline(source, firstLine);
        const bool isLegacy02 = firstLine.find("ANIMA 0.2") != std::string::npos;
        std::wstring args = (check?L"check ":L"run ") + QuoteArg(file);
        if(isLegacy02) args += L" --compat 0.2";
        RunCli(args);
    }
}
void CreateStarter() {
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitCom = SUCCEEDED(comHr);
    BROWSEINFOW bi{}; bi.hwndOwner=g_hwnd; bi.lpszTitle=L"Choose a parent folder for your new ANIMA project"; bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi); if(!pid){ if(uninitCom) CoUninitialize(); return; }
    wchar_t folder[MAX_PATH]{}; const BOOL ok=SHGetPathFromIDListW(pid,folder); CoTaskMemFree(pid); if(!ok){ if(uninitCom) CoUninitialize(); return; }
    fs::path base(folder), project=base/L"ANIMAProject"; int suffix=2;
    while(fs::exists(project) && suffix<1000) project=base/(L"ANIMAProject"+std::to_wstring(suffix++));
    RunCli(L"new "+QuoteArg(project.wstring()));
    if(uninitCom) CoUninitialize();
}
void ExecuteAction(const std::wstring& action) {
    if(action==L"toggle-theme"){g_dark=!g_dark;BOOL dark=g_dark?TRUE:FALSE;DwmSetWindowAttribute(g_hwnd,20,&dark,sizeof(dark));InvalidateRect(g_hwnd,nullptr,FALSE);return;}
    if(action==L"docs"){Launch(AppPath(L"AnimaDocs.exe"));return;}
    if(action==L"room"){Launch(AppPath(L"AnimaNative.exe"));return;}
    if(action==L"doctor"){RunCli(L"doctor");return;}
    if(action==L"self-test"){RunCli(L"self-test");return;}
    if(action==L"create-project"){CreateStarter();return;}
    if(action==L"browse-run"){BrowseAni(false);return;}
    if(action==L"browse-check"){BrowseAni(true);return;}
    if(action==L"open-examples"){OpenFolder(AppPath(L"Anima03Examples"));return;}
    if(action==L"open-release"){OpenFolder(g_appDir);return;}
    if(action==L"open-assets"){OpenFolder(AppPath(L"Assets"));return;}
    if(action==L"teaser20"){Launch(AppPath(L"Assets\\anima-teaser-20s.mp4"));return;}
    if(action==L"teaser45"){Launch(AppPath(L"Assets\\anima-teaser-45s.mp4"));return;}
    if(action==L"ray-compare"){Launch(AppPath(L"Assets\\raytrace-room-fallback-comparison.png"));return;}
    if(action==L"webgpu"){Launch(AppPath(L"Examples\\webgpu-host.html"));return;}
    if(action.rfind(L"browser|",0)==0){Launch(AppPath(L"Examples")/action.substr(8));return;}
    if(action==L"guide-03"){OpenMarkdown(L"LanguageDocs\\ANIMA_03_LANGUAGE_GUIDE.md");return;}
    if(action==L"guide-02"){OpenMarkdown(L"LanguageDocs\\ANIMA_02_LANGUAGE_SPEC.md");return;}
    if(action==L"build-guide"){OpenMarkdown(L"LanguageDocs\\VISUAL_STUDIO_BUILD_AND_RUN.md");return;}
    if(action==L"spec"){OpenMarkdown(L"Reference\\ANIMA_0.3_Engineering_Specification.docx");return;}
    if(action==L"nav-examples"){g_page=Examples;InvalidateRect(g_hwnd,nullptr,FALSE);return;}
    if(action.rfind(L"run|",0)==0 || action.rfind(L"check|",0)==0){
        const bool check=action.rfind(L"check|",0)==0;
        std::wstring payload=action.substr(check?6:4);
        bool legacy=false;
        const auto pos=payload.rfind(L"|legacy");
        if(pos!=std::wstring::npos){legacy=true;payload.resize(pos);}
        RunExample(payload,check,legacy);return;
    }
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg){
    case WM_PAINT:{PAINTSTRUCT ps{};HDC hdc=BeginPaint(hwnd,&ps);RECT rc{};GetClientRect(hwnd,&rc);HDC mem=CreateCompatibleDC(hdc);HBITMAP bm=CreateCompatibleBitmap(hdc,rc.right,rc.bottom);HGDIOBJ old=SelectObject(mem,bm);DrawWindow(mem,rc);BitBlt(hdc,0,0,rc.right,rc.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bm);DeleteDC(mem);EndPaint(hwnd,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_MOUSEMOVE:{POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};if(p.x!=g_mouse.x||p.y!=g_mouse.y){g_mouse=p;TRACKMOUSEEVENT tme{sizeof(tme),TME_LEAVE,hwnd,0};TrackMouseEvent(&tme);InvalidateRect(hwnd,nullptr,FALSE);}return 0;}
    case WM_MOUSELEAVE:g_mouse={-1,-1};InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_LBUTTONUP:{POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};for(auto it=g_hits.rbegin();it!=g_hits.rend();++it){if(PtInRect(&it->rect,p)){if(it->kind==HitKind::Navigate){g_page=static_cast<Page>(it->page);InvalidateRect(hwnd,nullptr,FALSE);}else ExecuteAction(it->action);break;}}return 0;}
    case WM_KEYDOWN:if(wParam==VK_F5){if(g_page==Examples)RunExample(L"fibonacci.ani",false);else if(g_page==GraphicsLab)ExecuteAction(L"room");else ExecuteAction(L"docs");return 0;}if(wParam>=VK_F1&&wParam<=VK_F6){g_page=static_cast<Page>(wParam-VK_F1);InvalidateRect(hwnd,nullptr,FALSE);return 0;}return 0;
    case WM_GETMINMAXINFO:{auto* m=reinterpret_cast<MINMAXINFO*>(lParam);m->ptMinTrackSize.x=1050;m->ptMinTrackSize.y=720;return 0;}
    case WM_DESTROY:g_logo.reset();PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GdiplusStartupInput startup{}; if(GdiplusStartup(&g_gdiplusToken,&startup,nullptr)!=Ok)return 1;
    wchar_t exe[MAX_PATH]{};DWORD n=GetModuleFileNameW(nullptr,exe,MAX_PATH);g_appDir=fs::path(std::wstring(exe,n)).parent_path();
    if(fs::exists(AppPath(L"Assets\\anima-monochrome.png"))) g_logo=std::make_unique<Image>(AppPath(L"Assets\\anima-monochrome.png").c_str());
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=WndProc;wc.hInstance=instance;
    wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_ANIMA_STUDIO));wc.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_ANIMA_STUDIO),IMAGE_ICON,32,32,LR_DEFAULTCOLOR));
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=nullptr;wc.lpszClassName=L"ANIMANativeStudioBetaWindow";
    if(!RegisterClassExW(&wc)){g_logo.reset();GdiplusShutdown(g_gdiplusToken);return 2;}
    RECT r{0,0,1280,720};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"ANIMA Studio · Native 0.3 Beta",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
    if(!hwnd){g_logo.reset();GdiplusShutdown(g_gdiplusToken);return 3;}
    g_hwnd=hwnd;
    BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
    g_status=(Exists(L"anima.exe")&&Exists(L"AnimaStudio.exe")&&Exists(L"AnimaDocs.exe")&&Exists(L"AnimaNative.exe")&&Exists(L"anima-core.exe")&&fs::exists(AppPath(L"Content\\00-overview.md"))&&fs::exists(AppPath(L"Assets\\anima-monochrome.png")))
        ? L"All primary components detected · F5 launches the current page action"
        : L"Some components are missing · Build the full Release solution or use the portable package";
    ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    GdiplusShutdown(g_gdiplusToken);return static_cast<int>(msg.wParam);
}
