#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <memory>
#include <cwctype>
#include <shlwapi.h>
#include <cstring>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace Gdiplus;
namespace fs = std::filesystem;

#define IDI_APP 101

namespace {
constexpr int kSidebarW = 258;
constexpr int kHeaderH = 78;
constexpr int kDockH = 74;
constexpr int kPageBodyTop = 250;
constexpr int kContentX = 292;

ULONG_PTR g_gdiplusToken = 0;
HWND g_hwnd = nullptr;
HWND g_search = nullptr;
HFONT g_searchFont = nullptr;
WNDPROC g_originalSearchProc = nullptr;
HBRUSH g_editBrush = nullptr;
fs::path g_exeDir;
fs::path g_projectRoot;
std::map<std::wstring, std::unique_ptr<Image>> g_images;
bool g_dark = false;
int g_currentPage = 0;
int g_scrollY = 0;
int g_contentHeight = 0;
std::wstring g_status = L"Ready · Native documentation · C++20";
std::wstring g_searchText;

struct ColorSet {
    Color background, header, sidebar, surface, surfaceAlt, border, text, muted, accent, accentSoft,
          codeBg, codeText, selected, selectedText, shadow;
};
ColorSet Colors() {
    if (g_dark) return {
        Color(255, 13, 15, 19), Color(255, 17, 19, 24), Color(255, 15, 17, 22),
        Color(255, 23, 26, 32), Color(255, 29, 32, 39), Color(255, 46, 50, 59),
        Color(255, 241, 243, 247), Color(255, 157, 164, 176), Color(255, 242, 244, 248),
        Color(255, 44, 48, 57), Color(255, 10, 12, 15), Color(255, 225, 232, 242),
        Color(255, 245, 246, 248), Color(255, 20, 22, 26), Color(70, 0, 0, 0)
    };
    return {
        Color(255, 245, 246, 248), Color(255, 252, 252, 253), Color(255, 250, 250, 252),
        Color(255, 255, 255, 255), Color(255, 248, 249, 251), Color(255, 226, 228, 233),
        Color(255, 28, 30, 35), Color(255, 105, 111, 122), Color(255, 26, 28, 32),
        Color(255, 238, 239, 242), Color(255, 247, 248, 250), Color(255, 41, 44, 51),
        Color(255, 32, 34, 39), Color(255, 255, 255, 255), Color(35, 30, 35, 45)
    };
}

enum class BlockType { H2, H3, Paragraph, Bullet, Code, Quote, Rule, Image, Table, Cards };
struct DocBlock { BlockType type; std::wstring text; std::wstring key; };
struct Page {
    std::wstring id, title, subtitle, filename;
    std::wstring raw, searchable;
    std::vector<DocBlock> blocks;
};
std::vector<Page> g_pages;

enum class Action {
    Navigate, Theme, CopyCode, OpenSolution, RunRoom, OpenExample, Play20, Play45,
    OpenBrandBoard, OpenRoomComparison, OpenWebGPUHost, OpenBrowserRoomHost, RunLanguageExample, ScrollTop, OpenReference
};
struct HitTarget { RECT rect{}; Action action{}; int index = -1; std::wstring payload; };
std::vector<HitTarget> g_hits;
bool g_clipHits = false;
RECT g_hitClip{};

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}
std::wstring ReadUtf8(const fs::path& p) {
    if (p.empty()) return {};
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss; ss << f.rdbuf();
    std::string data = ss.str();
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) data.erase(0, 3);
    return Utf8ToWide(data);
}
std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}
std::wstring Trim(std::wstring s) {
    auto b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return L"";
    auto e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}
std::wstring CleanInline(const std::wstring& input) {
    std::wstring out; out.reserve(input.size());
    bool inCode=false;
    for (size_t i=0;i<input.size();++i) {
        if (input[i]==L'`') { inCode=!inCode; continue; }
        if (!inCode && input[i]==L'*') continue; // strip Markdown emphasis markers, preserve operators inside inline code
        out+=input[i];
    }
    return out;
}

bool Starts(const std::wstring& s, const std::wstring& p) { return s.rfind(p, 0) == 0; }
fs::path ExistingPath(const std::vector<fs::path>& candidates) {
    for (const auto& p : candidates) { std::error_code ec; if (fs::exists(p, ec)) return fs::weakly_canonical(p, ec); }
    return {};
}
fs::path ContentPath(const std::wstring& file) {
    return ExistingPath({g_exeDir / L"Content" / file, g_projectRoot / L"AnimaDocs" / L"Content" / file,
        g_exeDir / L".." / L".." / L".." / L"AnimaDocs" / L"Content" / file});
}
fs::path AssetPath(const std::wstring& file) {
    return ExistingPath({g_exeDir / L"Assets" / file, g_projectRoot / L"AnimaDocs" / L"Assets" / file,
        g_exeDir / L".." / L".." / L".." / L"AnimaDocs" / L"Assets" / file});
}
fs::path ExamplePath(const std::wstring& file) {
    return ExistingPath({g_exeDir / L"Examples" / file, g_projectRoot / L"AnimaDocs" / L"Examples" / file,
        g_projectRoot / L"AnimaNative" / L"Examples" / file});
}
fs::path SolutionPath() {
    return ExistingPath({g_projectRoot / L"ANIMA_Studio.sln", g_exeDir / L".." / L".." / L".." / L"ANIMA_Studio.sln"});
}
fs::path RoomExePath() {
    return ExistingPath({g_exeDir / L"AnimaNative.exe", g_projectRoot / L"x64" / L"Debug" / L"AnimaNative.exe",
        g_projectRoot / L"x64" / L"Release" / L"AnimaNative.exe",
        g_projectRoot / L"AnimaNative" / L"x64" / L"Debug" / L"AnimaNative.exe",
        g_projectRoot / L"AnimaNative" / L"x64" / L"Release" / L"AnimaNative.exe",
        g_projectRoot / L"AnimaNative" / L"Debug" / L"AnimaNative.exe",
        g_projectRoot / L"AnimaNative.exe"});
}

std::vector<DocBlock> ParseMarkdown(const std::wstring& markdown) {
    std::vector<DocBlock> out;
    std::wistringstream stream(markdown);
    std::wstring line, paragraph, code;
    bool inCode = false;
    bool skipTitle = true;
    auto flushParagraph = [&]() {
        auto t = Trim(paragraph);
        if (!t.empty()) out.push_back({BlockType::Paragraph, t, L""});
        paragraph.clear();
    };
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        auto t = Trim(line);
        if (Starts(t, L"```")) {
            if (!inCode) { flushParagraph(); inCode = true; code.clear(); }
            else { out.push_back({BlockType::Code, code, L""}); code.clear(); inCode = false; }
            continue;
        }
        if (inCode) { if (!code.empty()) code += L"\n"; code += line; continue; }
        if (t.empty()) { flushParagraph(); continue; }
        if (Starts(t, L"# ")) {
            flushParagraph();
            if (skipTitle) { skipTitle = false; continue; }
            out.push_back({BlockType::H2, CleanInline(Trim(t.substr(2))), L""}); continue;
        }
        skipTitle = false;
        if (Starts(t, L"## ")) { flushParagraph(); out.push_back({BlockType::H2, CleanInline(Trim(t.substr(3))), L""}); continue; }
        if (Starts(t, L"### ")) { flushParagraph(); out.push_back({BlockType::H3, CleanInline(Trim(t.substr(4))), L""}); continue; }
        if (t == L"---") { flushParagraph(); out.push_back({BlockType::Rule, L"", L""}); continue; }
        if (Starts(t, L"[image:") && t.back() == L']') { flushParagraph(); out.push_back({BlockType::Image, L"", Trim(t.substr(7, t.size()-8))}); continue; }
        if (t==L"[cards:overview]") { flushParagraph(); out.push_back({BlockType::Cards,L"",L"overview"}); continue; }
        if (Starts(t, L"> ")) { flushParagraph(); out.push_back({BlockType::Quote, CleanInline(Trim(t.substr(2))), L""}); continue; }
        if (Starts(t, L"- ") || Starts(t, L"* ")) { flushParagraph(); out.push_back({BlockType::Bullet, CleanInline(Trim(t.substr(2))), L""}); continue; }
        if (Starts(t, L"|")) { flushParagraph(); out.push_back({BlockType::Table, CleanInline(t), L""}); continue; }
        if (!paragraph.empty()) paragraph += L" ";
        paragraph += CleanInline(t);
    }
    if (inCode && !code.empty()) out.push_back({BlockType::Code, code, L""});
    flushParagraph();
    return out;
}

void LoadPages() {
    struct Meta { const wchar_t* id; const wchar_t* title; const wchar_t* subtitle; const wchar_t* file; };
    const Meta metas[] = {
        {L"overview", L"ANIMA", L"Code × Motion × Interfaces", L"00-overview.md"},
        {L"start", L"Getting started", L"From your first scene to the native Windows application.", L"01-getting-started.md"},
        {L"language", L"Language reference", L"Program structure, nodes, properties, and expressions.", L"02-language.md"},
        {L"layout", L"Layout & styling", L"Describe structure and visual hierarchy with properties.", L"03-layout-style.md"},
        {L"motion", L"Motion & events", L"Use named motions and bind them to interface signals.", L"04-motion-events.md"},
        {L"scenes", L"Scenes & audio", L"Organize separate views and produce synthesized sound.", L"05-scenes-audio.md"},
        {L"gpu", L"GPU & WebGPU", L"Shader-based graphics and compute in the browser runtime.", L"06-gpu-webgpu.md"},
        {L"native", L"Native runtime", L"A browser-free C++ application for Windows.", L"07-native-runtime.md"},
        {L"room", L"Ray-traced room", L"A moody interior with a tall window and lighting comparison.", L"08-raytrace-room.md"},
        {L"examples", L"Examples library", L"Small source files you can open, copy, and adapt.", L"09-examples.md"},
        {L"media", L"Brand & launch media", L"ANIMA — Code × Motion × Interfaces.", L"10-media-brand.md"},
        {L"roadmap", L"Roadmap & status", L"A clear boundary between what runs today and what comes next.", L"11-roadmap.md"},
        {L"native02", L"ANIMA Native 0.2 Preview", L"The language’s own source form, native CPU capabilities, and Direct3D 12 compute.", L"12-native-language-v02.md"},
        {L"native03", L"ANIMA Native 0.3 Integrated", L"Static checking, typed bytecode, records, enums, and local modules in one native pipeline.", L"13-native-language-v03.md"},
        {L"studio", L"Unified Studio", L"One launcher for docs, the room demo, language tools, legacy compatibility, and project scaffolding.", L"14-integrated-native-studio.md"}
    };
    g_pages.clear();
    for (const auto& m : metas) {
        Page p; p.id=m.id; p.title=m.title; p.subtitle=m.subtitle; p.filename=m.file;
        p.raw = ReadUtf8(ContentPath(p.filename));
        p.blocks = ParseMarkdown(p.raw);
        p.searchable = Lower(p.title + L" " + p.subtitle + L" " + p.raw);
        g_pages.push_back(std::move(p));
    }
}

void DrawRoundedRect(Graphics& g, const RectF& r, float radius, const Color& fill, const Color* stroke=nullptr, float strokeWidth=1.0f) {
    GraphicsPath p;
    float d = radius * 2.0f;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X+r.Width-d, r.Y, d, d, 270, 90);
    p.AddArc(r.X+r.Width-d, r.Y+r.Height-d, d, d, 0, 90);
    p.AddArc(r.X, r.Y+r.Height-d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush b(fill); g.FillPath(&b, &p);
    if (stroke) { Pen pen(*stroke, strokeWidth); g.DrawPath(&pen, &p); }
}
void Line(Graphics& g, float x1, float y1, float x2, float y2, Color c, float width=1.0f) {
    Pen p(c,width); g.DrawLine(&p,x1,y1,x2,y2);
}
float TextHeight(Graphics& g, const std::wstring& text, Font& font, float width, float extra=5) {
    if (text.empty()) return 8;
    StringFormat fmt; fmt.SetTrimming(StringTrimmingWord); fmt.SetFormatFlags(StringFormatFlagsLineLimit);
    RectF layout(0,0,width,10000), bounds;
    g.MeasureString(text.c_str(), (INT)text.size(), &font, layout, &fmt, &bounds);
    return std::max(19.0f, bounds.Height + extra);
}
void DrawTextFit(Graphics& g, const std::wstring& text, const RectF& rect, float size, Color color, int style=FontStyleRegular,
                 const wchar_t* family=L"Segoe UI", StringAlignment align=StringAlignmentNear) {
    Font font(family,size,style,UnitPixel);
    SolidBrush brush(color);
    StringFormat fmt; fmt.SetTrimming(StringTrimmingWord); fmt.SetAlignment(align); fmt.SetLineAlignment(StringAlignmentNear);
    g.DrawString(text.c_str(), (INT)text.size(), &font, rect, &fmt, &brush);
}
void DrawCentered(Graphics& g, const std::wstring& text, const RectF& rect, float size, Color color, int style=FontStyleRegular,
                  const wchar_t* family=L"Segoe UI") {
    Font font(family,size,style,UnitPixel); SolidBrush brush(color);
    StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter); fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(text.c_str(), (INT)text.size(), &font, rect, &fmt, &brush);
}
RECT ToRECT(float x,float y,float w,float h) { return RECT{(LONG)x,(LONG)y,(LONG)(x+w),(LONG)(y+h)}; }
void AddHit(float x,float y,float w,float h, Action action, int index=-1, const std::wstring& payload=L"") {
    RECT r=ToRECT(x,y,w,h);
    if (g_clipHits) {
        RECT clipped{};
        if (!IntersectRect(&clipped,&r,&g_hitClip)) return;
        r=clipped;
    }
    g_hits.push_back({r,action,index,payload});
}
void DrawButton(Graphics& g, const std::wstring& label, float x, float y, float w, float h,
                Action act, int idx=-1, const std::wstring& payload=L"", bool primary=false) {
    auto c=Colors();
    Color fill = primary ? c.selected : c.surface;
    Color txt = primary ? c.selectedText : c.text;
    DrawRoundedRect(g,RectF(x,y,w,h),12,fill,&c.border,1.0f);
    DrawCentered(g,label,RectF(x+10,y,w-20,h),13.5f,txt,FontStyleBold);
    AddHit(x,y,w,h,act,idx,payload);
}
std::unique_ptr<Image>& GetImage(const std::wstring& key) {
    auto it = g_images.find(key);
    if (it != g_images.end()) return it->second;
    std::wstring fn;
    if (key==L"brand-board") fn=L"anima-brand-board.png";
    else if (key==L"room-comparison") fn=L"raytrace-room-fallback-comparison.png";
    else if (key==L"logo") fn=L"anima-monochrome.png";
    else if (key==L"icon") fn=L"anima-docs-icon.png";
    auto path=AssetPath(fn);
    if (path.empty()) { g_images[key]=nullptr; return g_images[key]; }
    auto image=std::make_unique<Image>(path.c_str());
    if (image->GetLastStatus()!=Ok) image.reset();
    g_images[key]=std::move(image);
    return g_images[key];
}
void DrawBrandIcon(Graphics& g,float x,float y,float size) {
    auto c=Colors();
    Color tile = g_dark ? Color(255,246,247,249) : Color(255,16,18,22);
    DrawRoundedRect(g,RectF(x,y,size,size),size*0.26f,tile);
    auto& logo=GetImage(L"logo");
    if (logo) {
        // active mark is white on transparent; invert tile treatment for visibility by drawing on dark logo tile.
        DrawRoundedRect(g,RectF(x,y,size,size),size*0.26f,Color(255,16,18,22));
        float pad=size*0.18f;
        g.DrawImage(logo.get(),RectF(x+pad,y+pad,size-2*pad,size-2*pad));
    } else {
        Pen p(Color(255,255,255,255),size*0.11f); p.SetLineJoin(LineJoinRound);
        PointF pts[]={PointF(x+size*.22f,y+size*.76f),PointF(x+size*.48f,y+size*.23f),PointF(x+size*.78f,y+size*.76f)};
        g.DrawLines(&p,pts,3);
    }
}
void DrawImageBlock(Graphics& g,const std::wstring& key,float x,float y,float w,float maxH) {
    auto& img=GetImage(key);
    auto c=Colors();
    if (!img) {
        DrawRoundedRect(g,RectF(x,y,w,110),15,c.surfaceAlt,&c.border);
        DrawTextFit(g,L"Preview asset not found",RectF(x+20,y+20,w-40,50),14,c.muted);
        return;
    }
    float iw=(float)img->GetWidth(), ih=(float)img->GetHeight();
    float scale=std::min(w/iw,maxH/ih);
    float dw=iw*scale, dh=ih*scale;
    RectF frame(x,y,dw,dh);
    DrawRoundedRect(g,RectF(x-1,y-1,dw+2,dh+2),15,c.surface,&c.border,1);
    GraphicsState s=g.Save();
    GraphicsPath clip; float rad=12,d=rad*2;
    clip.AddArc(frame.X,frame.Y,d,d,180,90); clip.AddArc(frame.X+frame.Width-d,frame.Y,d,d,270,90);
    clip.AddArc(frame.X+frame.Width-d,frame.Y+frame.Height-d,d,d,0,90); clip.AddArc(frame.X,frame.Y+frame.Height-d,d,d,90,90); clip.CloseFigure();
    g.SetClip(&clip); g.DrawImage(img.get(),frame); g.Restore(s);
}

void DrawSidebar(Graphics& g,int width,int height) {
    auto c=Colors();
    SolidBrush sb(c.sidebar); g.FillRectangle(&sb,0,kHeaderH,kSidebarW,height-kHeaderH);
    Line(g,(float)kSidebarW-1,(float)kHeaderH,(float)kSidebarW-1,(float)height,c.border,1);
    DrawTextFit(g,L"DOCUMENTATION",RectF(26,99,200,22),10.5f,c.muted,FontStyleBold);
    float y=129;
    for (int i=0;i<(int)g_pages.size();++i) {
        float h=37;
        bool selected=(i==g_currentPage && g_searchText.empty());
        if (selected) DrawRoundedRect(g,RectF(13,y,kSidebarW-26,h),10,c.selected);
        Color tc=selected?c.selectedText:c.text;
        // slim monochrome icon tile
        if (selected) DrawRoundedRect(g,RectF(24,y+8,20,20),6,g_dark?Color(255,55,59,67):Color(255,238,239,242));
        else DrawRoundedRect(g,RectF(24,y+8,20,20),6,c.surfaceAlt,&c.border,.8f);
        const wchar_t* glyph=L"•";
        switch(i) { case 0:glyph=L"A";break; case 1:glyph=L"→";break; case 2:glyph=L"{}";break; case 3:glyph=L"▦";break; case 4:glyph=L"↗";break; case 5:glyph=L"◖";break; case 6:glyph=L"GPU";break; case 7:glyph=L"C+";break; case 8:glyph=L"◫";break; case 9:glyph=L"<>";break; case 10:glyph=L"▶";break; case 11:glyph=L"✓";break; case 12:glyph=L"0.2";break; }
        DrawCentered(g,glyph,RectF(24,y+7,20,21),i==6?7.5f:11.5f,selected?tc:c.muted,FontStyleBold);
        DrawTextFit(g,g_pages[i].title,RectF(55,y+4,kSidebarW-77,h-8),13.2f,tc,selected?FontStyleBold:FontStyleRegular);
        AddHit(13,y,kSidebarW-26,h,Action::Navigate,i);
        y+=42;
    }
    float fy=(float)height-120;
    Line(g,20,fy,kSidebarW-20,fy,c.border,1);
    DrawTextFit(g,L"ANIMA NATIVE",RectF(24,fy+14,180,20),11,c.text,FontStyleBold);
    DrawTextFit(g,L"C++20  ·  Direct3D 12  ·  Windows",RectF(24,fy+37,215,36),10.5f,c.muted);
}

void DrawHeader(Graphics& g,int width,int height) {
    auto c=Colors();
    SolidBrush h(c.header); g.FillRectangle(&h,0,0,width,kHeaderH);
    Line(g,0,kHeaderH-1,width,kHeaderH-1,c.border,1);
    DrawBrandIcon(g,22,17,44);
    DrawTextFit(g,L"ANIMA",RectF(78,15,150,25),18,c.text,FontStyleBold,L"Segoe UI");
    DrawTextFit(g,L"NATIVE DOCUMENTATION",RectF(79,39,220,18),9.5f,c.muted,FontStyleBold);
    DrawButton(g,g_dark?L"Light appearance":L"Dark appearance",(float)width-164,18,140,39,Action::Theme);
    DrawRoundedRect(g,RectF((float)width-306,24,124,27),13,c.surfaceAlt,&c.border,.8f);
    DrawCentered(g,L"NATIVE EDITION",RectF((float)width-306,24,124,27),9.5f,c.muted,FontStyleBold);
}

void DrawSidebarAndHeader(Graphics& g,int width,int height) {
    auto c=Colors();
    SolidBrush bg(c.background); g.FillRectangle(&bg,0,0,width,height);
    DrawSidebar(g,width,height);
    DrawHeader(g,width,height);
}

void DrawCode(Graphics& g,const DocBlock& block,float x,float& y,float width) {
    auto c=Colors();
    Font codeFont(L"Consolas",13.2f,FontStyleRegular,UnitPixel);
    std::wstring code=block.text;
    float textH=TextHeight(g,code,codeFont,width-44,0);
    float panelH=std::max(60.0f,textH+51.0f);
    DrawRoundedRect(g,RectF(x,y,width,panelH),14,c.codeBg,&c.border,1);
    DrawTextFit(g,L"ANIMA SOURCE",RectF(x+17,y+11,160,16),9.5f,c.muted,FontStyleBold);
    float btnW=59;
    DrawRoundedRect(g,RectF(x+width-btnW-12,y+8,btnW,26),8,c.surfaceAlt,&c.border,.8f);
    DrawCentered(g,L"Copy",RectF(x+width-btnW-12,y+8,btnW,26),10.5f,c.text,FontStyleBold);
    AddHit(x+width-btnW-12,y+8,btnW,26,Action::CopyCode,-1,code);
    SolidBrush cb(c.codeText); StringFormat fmt; fmt.SetTrimming(StringTrimmingWord); fmt.SetFormatFlags(StringFormatFlagsLineLimit);
    RectF codeRect(x+18,y+42,width-36,panelH-50);
    g.DrawString(code.c_str(),(INT)code.size(),&codeFont,codeRect,&fmt,&cb);
    y += panelH+17;
}

void DrawMarkdown(Graphics& g,int width,int height) {
    auto c=Colors();
    if (g_pages.empty()) return;
    const auto& page=g_pages[g_currentPage];
    float contentW=(float)width-kContentX-46;
    contentW=std::max(450.0f,contentW);
    DrawTextFit(g,page.title,RectF((float)kContentX,101,contentW,44),31,c.text,FontStyleBold,L"Segoe UI");
    DrawTextFit(g,page.subtitle,RectF((float)kContentX,151,contentW,36),14,c.muted);
    float actionY=194;
    if (g_currentPage==0) {
        DrawButton(g,L"Get started",(float)kContentX,actionY,120,35,Action::Navigate,1,L"",true);
        DrawButton(g,L"Language reference",(float)kContentX+130,actionY,160,35,Action::Navigate,2);
    } else if (g_currentPage==1 || g_currentPage==7) {
        DrawButton(g,L"Open Visual Studio solution",(float)kContentX,actionY,210,35,Action::OpenSolution,-1,L"",true);
        DrawButton(g,L"Run room demo",(float)kContentX+220,actionY,139,35,Action::RunRoom);
    } else if (g_currentPage==6) {
        DrawButton(g,L"Open WGSL source",(float)kContentX,actionY,145,35,Action::OpenExample,-1,L"webgpu.anima",true);
        DrawButton(g,L"Open WebGPU host",(float)kContentX+155,actionY,150,35,Action::OpenWebGPUHost);
        DrawButton(g,L"Open room source",(float)kContentX+315,actionY,140,35,Action::OpenExample,-1,L"raytrace-room.anima");
        DrawButton(g,L"Run native room",(float)kContentX+465,actionY,135,35,Action::RunRoom);
        DrawButton(g,L"Open browser room host",(float)kContentX,actionY+41,190,35,Action::OpenBrowserRoomHost);
    } else if (g_currentPage==8) {
        DrawButton(g,L"Run native room",(float)kContentX,actionY,146,35,Action::RunRoom,-1,L"",true);
        DrawButton(g,L"Open room source",(float)kContentX+156,actionY,150,35,Action::OpenExample,-1,L"raytrace-room.anima");
        DrawButton(g,L"View comparison image",(float)kContentX+316,actionY,182,35,Action::OpenRoomComparison);
    } else if (g_currentPage==9) {
        DrawButton(g,L"Hello source",(float)kContentX,actionY,114,35,Action::OpenExample,-1,L"hello.anima",true);
        DrawButton(g,L"Glass card",(float)kContentX+124,actionY,112,35,Action::OpenExample,-1,L"glass-card.anima");
        DrawButton(g,L"Motion example",(float)kContentX+246,actionY,142,35,Action::OpenExample,-1,L"motion-composition.anima");
        DrawButton(g,L"Scene navigation",(float)kContentX+398,actionY,145,35,Action::OpenExample,-1,L"scene-navigation.anima");
        DrawButton(g,L"Run room",(float)kContentX+553,actionY,116,35,Action::RunRoom);
        DrawButton(g,L"Chime source",(float)kContentX,actionY+41,124,35,Action::OpenExample,-1,L"chime.anima");
        DrawButton(g,L"WebGPU source",(float)kContentX+134,actionY+41,140,35,Action::OpenExample,-1,L"webgpu.anima");
        DrawButton(g,L"Room source",(float)kContentX+284,actionY+41,124,35,Action::OpenExample,-1,L"raytrace-room.anima");
    } else if (g_currentPage==10) {
        DrawButton(g,L"Play 20s teaser",(float)kContentX,actionY,135,35,Action::Play20,-1,L"",true);
        DrawButton(g,L"Play 45s teaser",(float)kContentX+145,actionY,135,35,Action::Play45);
        DrawButton(g,L"Open brand board",(float)kContentX+290,actionY,155,35,Action::OpenBrandBoard);
    } else if (g_currentPage==12) {
        DrawButton(g,L"Run ANIMA 0.2 example",(float)kContentX,actionY,190,35,Action::RunLanguageExample,-1,L"native-power.ani",true);
        DrawButton(g,L"Open .ani source",(float)kContentX+200,actionY,145,35,Action::OpenExample,-1,L"native-power.ani");
        DrawButton(g,L"Open Visual Studio solution",(float)kContentX+355,actionY,205,35,Action::OpenSolution);
    } else if (g_currentPage==11) {
        DrawButton(g,L"Open native solution",(float)kContentX,actionY,170,35,Action::OpenSolution,-1,L"",true);
        DrawButton(g,L"Run room demo",(float)kContentX+180,actionY,139,35,Action::RunRoom);
        DrawButton(g,L"Archived docs",(float)kContentX+329,actionY,130,35,Action::OpenReference);
    }

    float bodyTop=(g_currentPage==6 || g_currentPage==9 || g_currentPage==12) ? 292.0f : (float)kPageBodyTop;
    float bodyBottom=(float)height-kDockH-22;
    float viewH=std::max(120.0f,bodyBottom-bodyTop);
    // Scroll viewport with native GDI+ clipping.
    GraphicsState clipState=g.Save();
    g.SetClip(RectF((float)kContentX,bodyTop,contentW,viewH));
    g_hitClip=ToRECT((float)kContentX,bodyTop,contentW,viewH);
    g_clipHits=true;
    float y=bodyTop-(float)g_scrollY;
    for (const auto& b:page.blocks) {
        if (b.type==BlockType::H2) {
            y+=14;
            Font f(L"Segoe UI",21.5f,FontStyleBold,UnitPixel);
            float h=TextHeight(g,b.text,f,contentW-10,7);
            DrawTextFit(g,b.text,RectF((float)kContentX,y,contentW-8,h+4),21.5f,c.text,FontStyleBold,L"Segoe UI");
            y+=h+5;
            Line(g,(float)kContentX,y,(float)kContentX+contentW,y,c.border,.8f);
            y+=12;
        } else if (b.type==BlockType::H3) {
            y+=7;
            Font f(L"Segoe UI",16,FontStyleBold,UnitPixel);
            float h=TextHeight(g,b.text,f,contentW-12,5);
            DrawTextFit(g,b.text,RectF((float)kContentX,y,contentW-12,h),16,c.text,FontStyleBold);
            y+=h+5;
        } else if (b.type==BlockType::Paragraph) {
            Font f(L"Segoe UI",14.2f,FontStyleRegular,UnitPixel);
            float h=TextHeight(g,b.text,f,contentW-7,8);
            DrawTextFit(g,b.text,RectF((float)kContentX,y,contentW-7,h),14.2f,c.text);
            y+=h+8;
        } else if (b.type==BlockType::Bullet) {
            SolidBrush dot(c.accent);
            g.FillEllipse(&dot, (REAL)kContentX + 3.0f, y + 8.0f, 5.0f, 5.0f);
            Font f(L"Segoe UI",14,FontStyleRegular,UnitPixel);
            float h=TextHeight(g,b.text,f,contentW-33,8);
            DrawTextFit(g,b.text,RectF((float)kContentX+19,y,contentW-27,h),14,c.text);
            y+=h+4;
        } else if (b.type==BlockType::Code) {
            DrawCode(g,b,(float)kContentX,y,contentW-8);
        } else if (b.type==BlockType::Quote) {
            Font f(L"Segoe UI",13.5f,FontStyleItalic,UnitPixel);
            float h=TextHeight(g,b.text,f,contentW-54,8);
            DrawRoundedRect(g,RectF((float)kContentX,y,contentW-8,h+24),10,c.surfaceAlt,&c.border,.8f);
            DrawRoundedRect(g,RectF((float)kContentX,y,4,h+24),2,c.accent);
            DrawTextFit(g,b.text,RectF((float)kContentX+17,y+11,contentW-44,h+2),13.5f,c.muted,FontStyleItalic);
            y+=h+34;
        } else if (b.type==BlockType::Rule) {
            Line(g,(float)kContentX,y,(float)kContentX+contentW-8,y,c.border,1); y+=20;
        } else if (b.type==BlockType::Image) {
            float maxH=300;
            if (b.key==L"brand-board") maxH=390;
            if (b.key==L"room-comparison") maxH=390;
            DrawImageBlock(g,b.key,(float)kContentX,y,contentW-8,maxH);
            auto& img=GetImage(b.key);
            if (img) {
                float scale=std::min((contentW-8)/(float)img->GetWidth(),maxH/(float)img->GetHeight());
                y+=(float)img->GetHeight()*scale+16;
            } else y+=130;
        } else if (b.type==BlockType::Cards) {
            const float gap=12.0f;
            const float cardW=(contentW-8-gap*2)/3.0f;
            const wchar_t* labels[]={L"LANGUAGE",L"GPU GRAPHICS",L"NATIVE WINDOWS"};
            const wchar_t* bodies[]={L"Declarative app, scene, node and property syntax.",L"WebGPU bridge, HLSL compute, and a future DXR backend.",L"C++20, Win32, Direct3D 12 and Visual Studio."};
            for (int ci=0;ci<3;++ci) {
                float cx=(float)kContentX+ci*(cardW+gap);
                DrawRoundedRect(g,RectF(cx,y,cardW,112),14,c.surface,&c.border,1);
                DrawTextFit(g,labels[ci],RectF(cx+16,y+15,cardW-30,17),9.5f,c.muted,FontStyleBold);
                Font f(L"Segoe UI",12.2f,FontStyleRegular,UnitPixel);
                float th=TextHeight(g,bodies[ci],f,cardW-30,5);
                DrawTextFit(g,bodies[ci],RectF(cx+16,y+40,cardW-30,std::min(65.0f,th+24)),12.2f,c.text);
            }
            y+=128;
        } else if (b.type==BlockType::Table) {
            DrawRoundedRect(g,RectF((float)kContentX,y,contentW-8,34),7,c.surfaceAlt,&c.border,.7f);
            DrawTextFit(g,b.text,RectF((float)kContentX+12,y+8,contentW-32,22),11.5f,c.text,FontStyleRegular,L"Consolas");
            y+=40;
        }
    }
    g_clipHits=false;
    g.Restore(clipState);
    g_contentHeight=(int)std::max(0.0f,y+g_scrollY-bodyTop+10);
    float maxScroll=std::max(0.0f,(float)g_contentHeight-viewH);
    if (g_scrollY>(int)maxScroll) g_scrollY=(int)maxScroll;
    // Minimal custom scrollbar.
    if (g_contentHeight>(int)viewH && viewH>0) {
        float trackX=(float)width-15, trackY=bodyTop+2, trackH=viewH-4;
        DrawRoundedRect(g,RectF(trackX,trackY,4,trackH),2,c.border);
        float thumbH=std::max(40.0f,trackH*viewH/(float)g_contentHeight);
        float thumbY=trackY+(trackH-thumbH)*((float)g_scrollY/std::max(1.0f,maxScroll));
        DrawRoundedRect(g,RectF(trackX-1,thumbY,6,thumbH),3,c.muted);
    }
}

void DrawSearchResults(Graphics& g,int width,int height) {
    auto c=Colors();
    float contentW=(float)width-kContentX-46;
    DrawTextFit(g,L"Search results",RectF((float)kContentX,105,contentW,43),30,c.text,FontStyleBold,L"Segoe UI");
    DrawTextFit(g,L"Matches across the ANIMA native documentation",RectF((float)kContentX,151,contentW,25),14,c.muted);
    float y=207-(float)g_scrollY;
    g_hitClip=ToRECT((float)kContentX,190,contentW-8,(float)height-kDockH-22-190);
    g_clipHits=true;
    int count=0;
    for (int i=0;i<(int)g_pages.size();++i) {
        bool match=g_pages[i].searchable.find(Lower(g_searchText))!=std::wstring::npos;
        if (!match) continue;
        count++;
        float h=94;
        auto rr=RectF((float)kContentX,y,contentW-16,h);
        auto fill=(i==g_currentPage)?c.surfaceAlt:c.surface;
        DrawRoundedRect(g,rr,16,fill,&c.border,1);
        DrawTextFit(g,g_pages[i].title,RectF((float)kContentX+20,y+16,contentW-60,27),16,c.text,FontStyleBold);
        DrawTextFit(g,g_pages[i].subtitle,RectF((float)kContentX+20,y+47,contentW-60,35),12.5f,c.muted);
        DrawCentered(g,L"Open  ›",RectF((float)kContentX+contentW-90,y+28,67,35),11.5f,c.text,FontStyleBold);
        AddHit((float)kContentX,y,contentW-16,h,Action::Navigate,i);
        y+=h+12;
    }
    g_clipHits=false;
    if (!count) {
        DrawRoundedRect(g,RectF((float)kContentX,207,contentW-16,116),16,c.surface,&c.border);
        DrawTextFit(g,L"No matches found. Try a term such as 'motion', 'WebGPU', 'native', or 'ray tracing'.",RectF((float)kContentX+20,231,contentW-60,64),14,c.muted);
    }
    g_contentHeight=(int)(y+g_scrollY-207);
}

void DrawDock(Graphics& g,int width,int height) {
    auto c=Colors();
    float dockW=680, dockH=46;
    float dockX=(float)kSidebarW+((float)width-kSidebarW-dockW)/2;
    float dockY=(float)height-74;
    DrawRoundedRect(g,RectF(dockX+1,dockY+3,dockW,dockH),18,c.shadow);
    DrawRoundedRect(g,RectF(dockX,dockY,dockW,dockH),18,c.surface,&c.border,1);
    struct D { const wchar_t* label; int page; } items[]={{L"Overview",0},{L"Language",2},{L"GPU",6},{L"Native",7},{L"Examples",9},{L"ANIMA 0.2",12}};
    float gap=6, itemW=(dockW-14-gap*5)/6;
    for (int i=0;i<6;i++) {
        float x=dockX+7+i*(itemW+gap);
        bool active=(g_currentPage==items[i].page && g_searchText.empty());
        if (active) DrawRoundedRect(g,RectF(x,dockY+5,itemW,dockH-10),12,c.selected);
        DrawCentered(g,items[i].label,RectF(x+2,dockY+5,itemW-4,dockH-10),12.2f,active?c.selectedText:c.muted,active?FontStyleBold:FontStyleRegular);
        AddHit(x,dockY+3,itemW,dockH-6,Action::Navigate,items[i].page);
    }
    DrawTextFit(g,g_status,RectF(22,(float)height-22,(float)width-44,16),9.5f,c.muted);
}

void SetStatus(const std::wstring& s) { g_status=s; if(g_hwnd) InvalidateRect(g_hwnd,nullptr,FALSE); }
void CopyText(const std::wstring& text) {
    if (!OpenClipboard(g_hwnd)) { SetStatus(L"Clipboard is unavailable right now."); return; }
    EmptyClipboard();
    SIZE_T bytes=(text.size()+1)*sizeof(wchar_t);
    HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,bytes);
    if (!h) { CloseClipboard(); SetStatus(L"Could not allocate clipboard text."); return; }
    void* p=GlobalLock(h);
    if (!p) { GlobalFree(h); CloseClipboard(); SetStatus(L"Could not lock clipboard text."); return; }
    memcpy(p,text.c_str(),bytes); GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT,h); CloseClipboard();
    SetStatus(L"Code copied to the clipboard.");
}
void OpenExternal(const fs::path& path, const std::wstring& verb=L"open") {
    if (path.empty() || !fs::exists(path)) { SetStatus(L"File not found. Extract the complete package and keep the folder structure intact."); return; }
    HINSTANCE r=ShellExecuteW(g_hwnd,verb.c_str(),path.c_str(),nullptr,path.parent_path().c_str(),SW_SHOWNORMAL);
    if ((INT_PTR)r<=32) SetStatus(L"Windows could not open the selected file.");
    else SetStatus(L"Opened: "+path.filename().wstring());
}
void OpenWithNotepad(const fs::path& path) {
    if (path.empty() || !fs::exists(path)) { SetStatus(L"Example source not found in the Examples folder."); return; }
    HINSTANCE r=ShellExecuteW(g_hwnd,L"open",L"notepad.exe",(L"\""+path.wstring()+L"\"").c_str(),nullptr,SW_SHOWNORMAL);
    if ((INT_PTR)r<=32) SetStatus(L"Could not launch Notepad for this example.");
    else SetStatus(L"Opened source: "+path.filename().wstring());
}
void RunRoom() {
    auto exe=RoomExePath();
    if (exe.empty()) {
        SetStatus(L"Native room renderer not found. Build the full ANIMA_Studio solution first, then try again.");
        MessageBoxW(g_hwnd,L"The native room renderer has not been built yet. In Visual Studio, choose Build → Build Solution, then click Run room demo again.",L"Build the native renderer first",MB_OK|MB_ICONINFORMATION);
        return;
    }
    HINSTANCE r=ShellExecuteW(g_hwnd,L"open",exe.c_str(),nullptr,exe.parent_path().c_str(),SW_SHOWNORMAL);
    if ((INT_PTR)r<=32) SetStatus(L"The native renderer could not be launched.");
    else SetStatus(L"Launching the native Direct3D 12 room renderer...");
}
void OpenSolution() { auto p=SolutionPath(); OpenExternal(p); }
void RunLanguage(const std::wstring& exampleName) {
    const auto source = ExamplePath(exampleName);
    if (source.empty() || !fs::exists(source)) {
        SetStatus(L"ANIMA 0.2 example source was not found. Re-extract the complete package.");
        return;
    }
    const auto exe = ExistingPath({
        g_exeDir / L"AnimaLang.exe",
        g_projectRoot / L"x64" / L"Debug" / L"AnimaLang.exe",
        g_projectRoot / L"x64" / L"Release" / L"AnimaLang.exe",
        g_projectRoot / L"AnimaLang" / L"x64" / L"Debug" / L"AnimaLang.exe",
        g_projectRoot / L"AnimaLang" / L"x64" / L"Release" / L"AnimaLang.exe"});
    if (exe.empty()) {
        SetStatus(L"AnimaLang.exe not found. Build the full ANIMA_Studio solution first.");
        MessageBoxW(g_hwnd, L"Build the AnimaLang project (or the full solution) in Visual Studio, then try again.", L"ANIMA Native 0.2", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::wstring args = L"\"" + source.wstring() + L"\"";
    HINSTANCE r = ShellExecuteW(g_hwnd, L"open", exe.c_str(), args.c_str(), exe.parent_path().c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) SetStatus(L"Could not launch AnimaLang.exe.");
    else SetStatus(L"Running ANIMA Native 0.2 example: " + source.filename().wstring());
}
void Navigate(int idx) {
    if (idx<0 || idx>=(int)g_pages.size()) return;
    g_currentPage=idx; g_scrollY=0; g_searchText.clear();
    if (g_search) SetWindowTextW(g_search,L"");
    SetStatus(L"Opened section: "+g_pages[idx].title);
    InvalidateRect(g_hwnd,nullptr,FALSE);
}
void HandleAction(const HitTarget& h) {
    switch(h.action) {
    case Action::Navigate: Navigate(h.index); break;
    case Action::Theme:
        g_dark=!g_dark;
        if(g_editBrush) DeleteObject(g_editBrush);
        g_editBrush=CreateSolidBrush(g_dark?RGB(28,30,36):RGB(255,255,255));
        if(g_search) InvalidateRect(g_search,nullptr,TRUE);
        { BOOL dark=g_dark?TRUE:FALSE; DwmSetWindowAttribute(g_hwnd,20,&dark,sizeof(dark)); }
        SetStatus(g_dark?L"Dark appearance enabled.":L"Light appearance enabled.");
        InvalidateRect(g_hwnd,nullptr,TRUE); break;
    case Action::CopyCode: CopyText(h.payload); break;
    case Action::OpenSolution: OpenSolution(); break;
    case Action::RunRoom: RunRoom(); break;
    case Action::OpenExample: OpenWithNotepad(ExamplePath(h.payload)); break;
    case Action::Play20: OpenExternal(AssetPath(L"anima-teaser-20s.mp4")); break;
    case Action::Play45: OpenExternal(AssetPath(L"anima-teaser-45s.mp4")); break;
    case Action::OpenBrandBoard: OpenExternal(AssetPath(L"anima-brand-board.png")); break;
    case Action::OpenRoomComparison: OpenExternal(AssetPath(L"raytrace-room-fallback-comparison.png")); break;
    case Action::OpenWebGPUHost: OpenExternal(ExamplePath(L"webgpu-host.html")); break;
    case Action::OpenBrowserRoomHost: OpenExternal(ExamplePath(L"raytrace-room-host.html")); break;
    case Action::RunLanguageExample: RunLanguage(h.payload); break;
    case Action::ScrollTop: g_scrollY=0; InvalidateRect(g_hwnd,nullptr,FALSE); break;
    case Action::OpenReference: OpenExternal(ExistingPath({g_exeDir/L"ReferenceArchive"/L"anima-apple-docs.html", g_projectRoot/L"AnimaDocs"/L"ReferenceArchive"/L"anima-apple-docs.html"})); break;
    }
}

void LayoutSearchControl(int width) {
    if (!g_search) return;
    int x=348;
    int w=std::max(200,std::min(450,width-820));
    MoveWindow(g_search,x,20,w,36,TRUE);
}

LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN && !g_searchText.empty()) {
            const auto q = Lower(g_searchText);
            for (int i=0;i<(int)g_pages.size();++i) {
                if (g_pages[i].searchable.find(q) != std::wstring::npos) { Navigate(i); return 0; }
            }
        }
        if (wParam == VK_ESCAPE && !g_searchText.empty()) {
            SetWindowTextW(hwnd,L"");
            SetFocus(g_hwnd);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL)&0x8000) && wParam=='K') {
            SendMessageW(hwnd,EM_SETSEL,0,-1);
            return 0;
        }
    }
    return g_originalSearchProc ? CallWindowProcW(g_originalSearchProc,hwnd,msg,wParam,lParam) : DefWindowProcW(hwnd,msg,wParam,lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
    case WM_CREATE: {
        g_hwnd=hwnd;
        g_search=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            348,20,380,36,hwnd,(HMENU)2001,GetModuleHandleW(nullptr),nullptr);
        g_originalSearchProc=(WNDPROC)SetWindowLongPtrW(g_search,GWLP_WNDPROC,(LONG_PTR)SearchEditProc);
        g_searchFont=CreateFontW(17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        SendMessageW(g_search,WM_SETFONT,(WPARAM)g_searchFont,TRUE);
        SendMessageW(g_search,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELONG(10,10));
#ifdef EM_SETCUEBANNER
        SendMessageW(g_search,EM_SETCUEBANNER,TRUE,(LPARAM)L"Search all documentation  ·  Ctrl+K");
#endif
        LayoutSearchControl(1380);
        return 0;
    }
    case WM_SIZE: LayoutSearchControl(LOWORD(lParam)); InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc=(HDC)wParam; auto c=Colors(); SetTextColor(dc,RGB(c.text.GetR(),c.text.GetG(),c.text.GetB()));
        SetBkColor(dc,g_dark?RGB(28,30,36):RGB(255,255,255));
        if(g_editBrush) return (LRESULT)g_editBrush;
        break;
    }
    case WM_COMMAND:
        if(LOWORD(wParam)==2001 && HIWORD(wParam)==EN_CHANGE) {
            int len=GetWindowTextLengthW(g_search);
            std::wstring text(len,L'\0'); if(len) GetWindowTextW(g_search,text.data(),len+1);
            g_searchText=Trim(text); g_scrollY=0;
            if(!g_searchText.empty()) SetStatus(L"Searching all documentation...");
            InvalidateRect(hwnd,nullptr,FALSE); return 0;
        }
        break;
    case WM_KEYDOWN:
        if((GetKeyState(VK_CONTROL)&0x8000) && wParam=='K') { SetFocus(g_search); SendMessageW(g_search,EM_SETSEL,0,-1); return 0; }
        if(wParam==VK_ESCAPE && !g_searchText.empty()) { SetWindowTextW(g_search,L""); SetFocus(hwnd); return 0; }
        if(wParam==VK_HOME) { g_scrollY=0; InvalidateRect(hwnd,nullptr,FALSE); return 0; }
        break;
    case WM_MOUSEWHEEL: {
        int delta=GET_WHEEL_DELTA_WPARAM(wParam);
        g_scrollY=std::max(0,g_scrollY-(delta/WHEEL_DELTA)*48);
        InvalidateRect(hwnd,nullptr,FALSE); return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        for(auto it=g_hits.rbegin();it!=g_hits.rend();++it) {
            if(PtInRect(&it->rect,p)) { HandleAction(*it); return 0; }
        }
        if(p.x>kContentX && p.y>kPageBodyTop) SetFocus(hwnd);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc); int w=rc.right-rc.left,h=rc.bottom-rc.top;
        HDC mem=CreateCompatibleDC(hdc); HBITMAP bmp=CreateCompatibleBitmap(hdc,w,h); HGDIOBJ old=SelectObject(mem,bmp);
        Graphics g(mem); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g_hits.clear(); g_clipHits=false;
        DrawSidebarAndHeader(g,w,h);
        if(!g_searchText.empty()) DrawSearchResults(g,w,h); else DrawMarkdown(g,w,h);
        DrawDock(g,w,h);
        BitBlt(hdc,0,0,w,h,mem,0,0,SRCCOPY);
        SelectObject(mem,old); DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd,&ps); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto m=(MINMAXINFO*)lParam; m->ptMinTrackSize.x=1040; m->ptMinTrackSize.y=700; return 0;
    }
    case WM_DESTROY:
        if(g_editBrush) { DeleteObject(g_editBrush); g_editBrush=nullptr; }
        if(g_searchFont) { DeleteObject(g_searchFont); g_searchFont=nullptr; }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GdiplusStartupInput gdiplusStartupInput;
    if(GdiplusStartup(&g_gdiplusToken,&gdiplusStartupInput,nullptr)!=Ok) return 1;
    wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr,exe,MAX_PATH);
    g_exeDir=fs::path(exe).parent_path();
    std::error_code ec;
    g_projectRoot=(g_exeDir/L".."/L"..").lexically_normal();
    g_editBrush=CreateSolidBrush(RGB(255,255,255));
    LoadPages();

    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.style=CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=WindowProc;
    wc.hInstance=hInstance; wc.hIcon=LoadIconW(hInstance,MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm=(HICON)LoadImageW(hInstance,MAKEINTRESOURCEW(IDI_APP),IMAGE_ICON,32,32,LR_DEFAULTCOLOR);
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName=L"AnimaNativeDocsWindow";
    if(!RegisterClassExW(&wc)) { GdiplusShutdown(g_gdiplusToken); return 2; }
    RECT r{0,0,1460,940}; AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"ANIMA Native Documentation",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,hInstance,nullptr);
    if(!hwnd) { GdiplusShutdown(g_gdiplusToken); return 3; }
    g_hwnd=hwnd;
    BOOL dark=FALSE; DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
    ShowWindow(hwnd,nCmdShow); UpdateWindow(hwnd);
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    g_images.clear(); GdiplusShutdown(g_gdiplusToken); return (int)msg.wParam;
}
