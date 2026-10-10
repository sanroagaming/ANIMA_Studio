#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <iterator>
#include <utility>

namespace fs = std::filesystem;
static fs::path appDir;
static std::wstring lower(std::wstring s) { std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));}); return s; }
static std::wstring quoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"")==std::wstring::npos) return arg;
    std::wstring out=L"\""; unsigned slashes=0;
    for (wchar_t c : arg) {
        if (c==L'\\') { ++slashes; continue; }
        if (c==L'"') { out.append(slashes*2+1,L'\\'); out+=L'"'; slashes=0; continue; }
        out.append(slashes,L'\\'); slashes=0; out+=c;
    }
    out.append(slashes*2,L'\\'); out+=L'"'; return out;
}
static fs::path exe(const wchar_t* name) { return appDir/name; }
static std::vector<std::wstring> resolveSourceArgs(std::vector<std::wstring> args) {
    const fs::path caller=fs::current_path();
    bool outputPathNext=false;
    for(auto& arg:args) {
        if(outputPathNext) { outputPathNext=false; continue; }
        if(arg==L"-o") { outputPathNext=true; continue; }
        fs::path p(arg);
        if(p.is_absolute()) continue;
        const auto ext=lower(p.extension().wstring());
        if(ext!=L".ani" && ext!=L".anima") continue;
        std::error_code ec;
        if(fs::is_regular_file(caller/p,ec)) { arg=fs::absolute(caller/p,ec).wstring(); continue; }
        ec.clear();
        if(fs::is_regular_file(appDir/p,ec)) arg=(appDir/p).wstring();
    }
    return args;
}
static void usage() {
    std::wcout << L"ANIMA Native Studio 0.3 — unified launcher\n\n"
        L"Usage:\n  anima                       Open native documentation\n  anima docs                  Open native documentation\n  anima room                  Open D3D12 room demo\n  anima new <folder>          Create an ANIMA project starter\n  anima legacy <args...>      Use 0.2 reference runtime\n  anima doctor                Check installed companion apps\n  anima <language command>    Run/check/test/dis/migrate/etc.\n\n"
        L"Examples:\n  anima new MyGame\n  anima run Anima03Examples\\fibonacci.ani\n  anima self-test\n  anima scene Anima03Examples\\SpecificationTargets\\ui-two-scenes.ani\n";
}
static int launch(const fs::path& target, const std::vector<std::wstring>& args, bool detached=false) {
    if (!fs::exists(target)) {
        std::wcerr << L"ANIMA launcher: required component was not found:\n  " << target.wstring()
                   << L"\nBuild the complete ANIMA_Studio.sln in Visual Studio first.\n";
        return 2;
    }
    std::wstring cmd=quoteArg(target.wstring());
    for (const auto& a:args) { cmd+=L' '; cmd+=quoteArg(a); }
    std::vector<wchar_t> writable(cmd.begin(),cmd.end()); writable.push_back(L'\0');
    STARTUPINFOW si{}; si.cb=sizeof(si); PROCESS_INFORMATION pi{};
    const DWORD flags=detached?DETACHED_PROCESS:0;
    const fs::path workingDirectory=detached?appDir:fs::current_path();
    if (!CreateProcessW(target.c_str(),writable.data(),nullptr,nullptr,TRUE,flags,nullptr,workingDirectory.c_str(),&si,&pi)) {
        std::wcerr << L"Could not launch " << target.filename().wstring() << L" (Windows error " << GetLastError() << L").\n";
        return 2;
    }
    CloseHandle(pi.hThread);
    if (detached) { CloseHandle(pi.hProcess); return 0; }
    WaitForSingleObject(pi.hProcess,INFINITE); DWORD code=1;
    GetExitCodeProcess(pi.hProcess,&code); CloseHandle(pi.hProcess); return static_cast<int>(code);
}
static std::wstring stemName(const fs::path& p) {
    std::wstring s=p.filename().wstring(); if(s.empty())s=L"ANIMAProject";
    for(auto& c:s) if(!std::iswalnum(static_cast<wint_t>(c))&&c!=L'_'&&c!=L'-') c=L'_'; return s;
}
static int createProject(const fs::path& folder) {
    try {
        fs::path dir=folder.is_absolute()?folder:(fs::current_path()/folder);
        if(fs::exists(dir) && !fs::is_empty(dir)) { std::wcerr<<L"Refusing to overwrite a non-empty directory: "<<dir.wstring()<<L"\n"; return 2; }
        fs::create_directories(dir);
        const std::wstring name=stemName(dir);
        std::ofstream main(dir/"main.ani",std::ios::binary);
        main << "ANIMA 0.3\n\nproject \"";
        for(wchar_t c:name) main << (c<128?static_cast<char>(c):'_');
        main << "\" {\n    target native.windows\n    runtime direct\n    capability cpu.parallel\n}\n\ndefine greet(name: text) -> text {\n    give \"Hello, \" + name\n}\n\nstart {\n    announce greet(\"ANIMA\")\n    announce \"Project created successfully.\"\n}\n";
        main.close();
        std::ofstream readme(dir/"README.md");
        readme << "# ANIMA project\n\nCreated by ANIMA Native Studio 0.3.\n\nFrom the ANIMA Studio output directory, run:\n\n```bat\nanima-core.exe check \"FULL_PATH_TO_THIS_FOLDER\\main.ani\"\nanima-core.exe run \"FULL_PATH_TO_THIS_FOLDER\\main.ani\"\n```\n\nThe 0.3 preview currently supports a documented subset of the full specification; see the Studio documentation for limitations.\n";
        std::wcout<<L"Created ANIMA starter project:\n  "<<dir.wstring()<<L"\n  main.ani\n  README.md\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<"Could not create project: "<<e.what()<<"\n"; return 2; }
}
static int doctor() {
    struct Component {const wchar_t* name; const wchar_t* file;};
    const Component list[]={{L"ANIMA Studio launcher",L"AnimaStudio.exe"},{L"ANIMA 0.3 language runtime",L"anima-core.exe"},{L"Native documentation",L"AnimaDocs.exe"},{L"Native D3D12 room",L"AnimaNative.exe"},{L"ANIMA 0.2 compatibility runtime",L"AnimaLang.exe"}};
    bool missing=false; std::wcout<<L"ANIMA Native Studio — component check\n\n";
    for(const auto& c:list){auto p=exe(c.file);const bool ok=fs::is_regular_file(p);std::wcout<<(ok?L"[OK]   ":L"[MISS] ")<<c.name<<L" — "<<p.filename().wstring()<<L"\n";missing|=!ok;}
    std::wcout<<L"\nThis check verifies package files, not GPU hardware or DXR execution. Use machine-inspector.ani to query runtime capabilities.\n";
    return missing?1:0;
}
int wmain(int argc,wchar_t** argv) {
    wchar_t buf[32768]{}; DWORD n=GetModuleFileNameW(nullptr,buf,static_cast<DWORD>(std::size(buf)));
    appDir=fs::path(std::wstring(buf,n)).parent_path();
    if(argc<2) return launch(exe(L"AnimaDocs.exe"),{},true);
    const std::wstring cmd=lower(argv[1]);
    if(cmd==L"help"||cmd==L"--help"||cmd==L"-h"){usage();return 0;}
    if(cmd==L"docs"||cmd==L"doc"||cmd==L"open-docs")return launch(exe(L"AnimaDocs.exe"),{},true);
    if(cmd==L"room"||cmd==L"play-room")return launch(exe(L"AnimaNative.exe"),{},true);
    if(cmd==L"doctor")return doctor();
    if(cmd==L"new") { if(argc<3){std::wcerr<<L"Usage: anima new <folder>\n";return 3;}return createProject(fs::path(argv[2])); }
    if(cmd==L"legacy") {std::vector<std::wstring> args;for(int i=2;i<argc;++i)args.emplace_back(argv[i]);args=resolveSourceArgs(std::move(args));return launch(exe(L"AnimaLang.exe"),args,false);}
    std::vector<std::wstring> args;
    if(argc>=2) for(int i=1;i<argc;++i)args.emplace_back(argv[i]);
    args=resolveSourceArgs(std::move(args));
    if(args[0].size()>5 && lower(args[0].substr(args[0].size()-6))==L".anima") {
        std::wcerr<<L"Legacy .anima UI source is not directly executed by the 0.3 VM. Try: anima migrate \""<<args[0]<<L"\" -o migrated.ani\n";
        return 2;
    }
    return launch(exe(L"anima-core.exe"),args,false);
}
