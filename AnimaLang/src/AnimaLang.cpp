#include "AnimaGpu.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
namespace {
using Series = std::vector<double>;
using Value = std::variant<double, bool, std::string, Series>;

struct SourceError : std::runtime_error {
    std::size_t line;
    SourceError(std::size_t ln, const std::string& message)
        : std::runtime_error(message), line(ln) {}
};

std::string ValueTypeName(const Value& value) {
    if (std::holds_alternative<double>(value)) return "number";
    if (std::holds_alternative<bool>(value)) return "truth";
    if (std::holds_alternative<std::string>(value)) return "text";
    return "series";
}
std::string NumberString(double d) {
    std::ostringstream out;
    if (std::isfinite(d) && std::floor(d) == d && std::abs(d) < 9.0e15) out << std::fixed << std::setprecision(0) << d;
    else out << std::setprecision(12) << d;
    return out.str();
}
std::string Display(const Value& value) {
    if (const auto* n = std::get_if<double>(&value)) return NumberString(*n);
    if (const auto* b = std::get_if<bool>(&value)) return *b ? "true" : "false";
    if (const auto* s = std::get_if<std::string>(&value)) return *s;
    const auto& series = std::get<Series>(value);
    std::ostringstream out; out << '[';
    for (std::size_t i = 0; i < series.size(); ++i) {
        if (i) out << ", ";
        out << NumberString(series[i]);
    }
    out << ']';
    return out.str();
}
double AsNumber(const Value& value, std::size_t line, const char* context) {
    if (const auto* n = std::get_if<double>(&value)) return *n;
    throw SourceError(line, std::string(context) + " needs a number, but received " + ValueTypeName(value) + ".");
}
bool AsTruth(const Value& value, std::size_t line) {
    if (const auto* b = std::get_if<bool>(&value)) return *b;
    throw SourceError(line, "A 'when' condition must return truth (true or false), not " + ValueTypeName(value) + ".");
}

// The 0.2 front end is deliberately a direct source reader/evaluator. It has
// no generated token stream, AST package, external parser, or scripting engine.
// It reads ANIMA characters, recognizes this language's own statements, and
// evaluates statements immediately. Expression precedence is implemented by
// small in-house routines over the same cursor.
class Reader {
public:
    Reader(std::string text, std::size_t initialLine = 1)
        : source_(std::move(text)), line_(initialLine) {}

    std::size_t Line() const { return line_; }
    bool End() { SkipTrivia(); return pos_ >= source_.size(); }
    const std::string& Source() const { return source_; }

    void SkipTrivia() {
        while (pos_ < source_.size()) {
            const char c = source_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { Advance(); continue; }
            if (c == '#' || (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/')) {
                while (pos_ < source_.size() && source_[pos_] != '\n') Advance();
                continue;
            }
            break;
        }
    }
    void SkipInline() {
        while (pos_ < source_.size() && (source_[pos_] == ' ' || source_[pos_] == '\t' || source_[pos_] == '\r')) Advance();
    }
    bool Consume(char c) {
        SkipInline();
        if (pos_ < source_.size() && source_[pos_] == c) { Advance(); return true; }
        return false;
    }
    bool ConsumeText(std::string_view text) {
        SkipInline();
        if (source_.compare(pos_, text.size(), text) == 0) { for (std::size_t i=0;i<text.size();++i) Advance(); return true; }
        return false;
    }
    void Require(char c, const std::string& expected) {
        if (!Consume(c)) Fail("Expected " + expected + ".");
    }
    void RequireText(std::string_view text, const std::string& expected) {
        if (!ConsumeText(text)) Fail("Expected " + expected + ".");
    }
    std::string ReadWord() {
        SkipTrivia();
        if (pos_ >= source_.size() || !(IsAlpha(source_[pos_]) || source_[pos_] == '_')) Fail("Expected a name.");
        const std::size_t start = pos_;
        while (pos_ < source_.size() && (IsAlpha(source_[pos_]) || IsDigit(source_[pos_]) || source_[pos_] == '_')) Advance();
        return source_.substr(start, pos_ - start);
    }
    bool TryWord(std::string_view wanted) {
        const std::size_t savePos = pos_, saveLine = line_;
        try {
            const std::string got = ReadWord();
            if (got == wanted) return true;
        } catch (...) { }
        pos_ = savePos; line_ = saveLine;
        return false;
    }
    std::string ReadString() {
        SkipTrivia();
        if (pos_ >= source_.size() || source_[pos_] != '"') Fail("Expected quoted text.");
        Advance(); std::string out;
        while (pos_ < source_.size()) {
            char c = source_[pos_]; Advance();
            if (c == '"') return out;
            if (c == '\\') {
                if (pos_ >= source_.size()) Fail("Unfinished escape sequence.");
                char e = source_[pos_]; Advance();
                switch (e) { case 'n': out.push_back('\n'); break; case 'r': out.push_back('\r'); break; case 't': out.push_back('\t'); break; case '"': out.push_back('"'); break; case '\\': out.push_back('\\'); break; default: out.push_back(e); break; }
            } else out.push_back(c);
        }
        Fail("Text literal was not closed.");
        return {};
    }
    std::string ReadNumberText() {
        SkipTrivia();
        const std::size_t start = pos_;
        bool dot = false;
        if (pos_ < source_.size() && (source_[pos_] == '-' || source_[pos_] == '+')) Advance();
        while (pos_ < source_.size()) {
            char c = source_[pos_];
            if (IsDigit(c)) { Advance(); continue; }
            if (c == '.' && !dot) { dot = true; Advance(); continue; }
            break;
        }
        if (pos_ == start || (pos_ == start + 1 && (source_[start] == '-' || source_[start] == '+'))) Fail("Expected a numeric value.");
        return source_.substr(start, pos_ - start);
    }
    std::string ReadPath() {
        std::string value = ReadWord();
        while (true) {
            const std::size_t savePos = pos_, saveLine = line_;
            if (!Consume('.')) { pos_ = savePos; line_ = saveLine; break; }
            value += '.'; value += ReadWord();
        }
        return value;
    }
    std::string CaptureBlock() {
        SkipTrivia();
        if (pos_ >= source_.size() || source_[pos_] != '{') Fail("Expected a block beginning with '{'.");
        Advance();
        const std::size_t start = pos_;
        const std::size_t bodyStartLine = line_;
        int depth = 1; bool quoted = false, escaped = false;
        while (pos_ < source_.size()) {
            const char c = source_[pos_];
            if (quoted) {
                Advance();
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') quoted = false;
                continue;
            }
            if (c == '"') { quoted = true; Advance(); continue; }
            if (c == '#' || (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/')) {
                while (pos_ < source_.size() && source_[pos_] != '\n') Advance();
                continue;
            }
            if (c == '{') { ++depth; Advance(); continue; }
            if (c == '}') {
                --depth;
                if (depth == 0) {
                    const std::size_t end = pos_;
                    Advance();
                    // Preserve original source line numbers when the body is
                    // executed later. Padding is ordinary ANIMA whitespace.
                    return std::string(bodyStartLine > 1 ? bodyStartLine - 1 : 0, '\n') + source_.substr(start, end - start);
                }
                Advance(); continue;
            }
            Advance();
        }
        Fail("Block was not closed with '}'.");
        return {};
    }
    void SkipToEndOfLine() {
        SkipInline();
        while (pos_ < source_.size() && source_[pos_] != '\n') Advance();
        if (pos_ < source_.size() && source_[pos_] == '\n') Advance();
    }
    bool AtLineEnd() {
        SkipInline();
        return pos_ >= source_.size() || source_[pos_] == '\n' || source_[pos_] == '}' || source_[pos_] == '#' || (source_[pos_] == '/' && pos_ + 1 < source_.size() && source_[pos_+1] == '/');
    }
    bool At(char c) { SkipInline(); return pos_ < source_.size() && source_[pos_] == c; }
    void Fail(const std::string& msg) const { throw SourceError(line_, msg); }

private:
    std::string source_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    static bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
    static bool IsDigit(char c) { return c >= '0' && c <= '9'; }
    void Advance() { if (source_[pos_++] == '\n') ++line_; }
};

class Runtime {
public:
    Runtime(std::string source, fs::path sourcePath, bool checkOnly)
        : source_(std::move(source)), sourcePath_(std::move(sourcePath)), checkOnly_(checkOnly) {}

    void Run() {
        Reader r(source_);
        r.SkipTrivia();
        if (r.ReadWord() != "ANIMA") r.Fail("Every ANIMA 0.2 source must start with 'ANIMA 0.2'.");
        const std::string version = r.ReadNumberText();
        if (version != "0.2") r.Fail("This preview runs ANIMA 0.2 source. Found version " + version + ".");
        bool sawStart = false;
        std::string startBlock;
        while (!r.End()) {
            const std::string declaration = r.ReadWord();
            if (declaration == "project") {
                (void)r.ReadString();
                (void)r.CaptureBlock(); // project capabilities are declarative in 0.2
            } else if (declaration == "define") {
                const std::string name = r.ReadWord();
                if (definitions_.contains(name)) r.Fail("Definition '" + name + "' was declared more than once.");
                definitions_[name] = r.CaptureBlock();
            } else if (declaration == "start") {
                if (sawStart) r.Fail("Only one 'start' block is allowed.");
                sawStart = true;
                startBlock = r.CaptureBlock();
            } else {
                r.Fail("Unknown top-level declaration '" + declaration + "'. Use project, define, or start.");
            }
        }
        if (!sawStart) throw SourceError(1, "Missing required 'start { ... }' entry block.");
        if (!checkOnly_) {
            std::cout << "\nANIMA Native 0.2 Preview\n" << std::string(29, '-') << "\n";
            std::cout << "Source: " << sourcePath_.string() << "\n\n";
        }
        ExecuteBlock(startBlock);
        if (!checkOnly_) std::cout << "\n[ANIMA] execution complete\n";
    }

private:
    std::string source_;
    fs::path sourcePath_;
    bool checkOnly_ = false;
    std::map<std::string, std::string> definitions_;
    std::map<std::string, Value> values_;
    int callDepth_ = 0;

    Value ParseExpression(Reader& r) { return ParseCompare(r); }
    Value ParseCompare(Reader& r) {
        Value left = ParseAdd(r);
        for (;;) {
            if (r.ConsumeText("==")) { Value right = ParseAdd(r); left = Display(left) == Display(right); }
            else if (r.ConsumeText("!=")) { Value right = ParseAdd(r); left = Display(left) != Display(right); }
            else if (r.ConsumeText("<=")) { Value right = ParseAdd(r); left = AsNumber(left, r.Line(), "Comparison") <= AsNumber(right, r.Line(), "Comparison"); }
            else if (r.ConsumeText(">=")) { Value right = ParseAdd(r); left = AsNumber(left, r.Line(), "Comparison") >= AsNumber(right, r.Line(), "Comparison"); }
            else if (r.Consume('<')) { Value right = ParseAdd(r); left = AsNumber(left, r.Line(), "Comparison") < AsNumber(right, r.Line(), "Comparison"); }
            else if (r.Consume('>')) { Value right = ParseAdd(r); left = AsNumber(left, r.Line(), "Comparison") > AsNumber(right, r.Line(), "Comparison"); }
            else break;
        }
        return left;
    }
    Value ParseAdd(Reader& r) {
        Value left = ParseMultiply(r);
        for (;;) {
            if (r.Consume('+')) {
                Value right = ParseMultiply(r);
                if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)) left = Display(left) + Display(right);
                else left = AsNumber(left, r.Line(), "Addition") + AsNumber(right, r.Line(), "Addition");
            } else if (r.Consume('-')) {
                Value right = ParseMultiply(r); left = AsNumber(left, r.Line(), "Subtraction") - AsNumber(right, r.Line(), "Subtraction");
            } else break;
        }
        return left;
    }
    Value ParseMultiply(Reader& r) {
        Value left = ParsePrimary(r);
        for (;;) {
            if (r.Consume('*')) { Value right = ParsePrimary(r); left = AsNumber(left, r.Line(), "Multiplication") * AsNumber(right, r.Line(), "Multiplication"); }
            else if (r.Consume('/')) {
                Value right = ParsePrimary(r); const double denominator = AsNumber(right, r.Line(), "Division");
                if (denominator == 0.0) r.Fail("Division by zero is not allowed.");
                left = AsNumber(left, r.Line(), "Division") / denominator;
            } else break;
        }
        return left;
    }
    Value ParsePrimary(Reader& r) {
        r.SkipInline();
        if (r.Consume('(')) { Value v = ParseExpression(r); r.Require(')', "')'"); return v; }
        if (r.At('"')) return r.ReadString();
        if (r.At('-') || r.At('+') || (r.At('0') || r.At('1') || r.At('2') || r.At('3') || r.At('4') || r.At('5') || r.At('6') || r.At('7') || r.At('8') || r.At('9'))) {
            const std::string text = r.ReadNumberText();
            char* end = nullptr; const double number = std::strtod(text.c_str(), &end);
            if (!end || *end != '\0' || !std::isfinite(number)) r.Fail("Invalid numeric literal '" + text + "'.");
            return number;
        }
        const std::string path = r.ReadPath();
        if (path == "true") return true;
        if (path == "false") return false;
        if (r.Consume('(')) {
            std::vector<Value> args;
            if (!r.Consume(')')) {
                do { args.push_back(ParseExpression(r)); } while (r.Consume(','));
                r.Require(')', "')' after function arguments");
            }
            return Call(path, args, r.Line());
        }
        auto found = values_.find(path);
        if (found == values_.end()) r.Fail("Unknown value '" + path + "'. Use a declared value or a built-in capability.");
        return found->second;
    }

    Value Call(const std::string& name, const std::vector<Value>& args, std::size_t line) {
        auto needCount = [&](std::size_t count) { if (args.size() != count) throw SourceError(line, name + " expects " + std::to_string(count) + " argument(s)."); };
        if (name == "cpu.logical_cores") {
            needCount(0); const unsigned int n = std::thread::hardware_concurrency(); return static_cast<double>(n ? n : 1);
        }
        if (name == "cpu.parallel_sum") {
            needCount(1); const double raw = AsNumber(args[0], line, name.c_str());
            if (raw < 0 || raw > 5000000 || std::floor(raw) != raw) throw SourceError(line, "cpu.parallel_sum expects a whole count between 0 and 5,000,000.");
            const std::uint64_t count = static_cast<std::uint64_t>(raw);
            if (checkOnly_) return 0.0;
            if (count == 0) return 0.0;
            unsigned int workers = std::thread::hardware_concurrency(); if (!workers) workers = 2;
            workers = static_cast<unsigned int>(std::min<std::uint64_t>({ workers, 16, count }));
            std::vector<std::uint64_t> partial(workers, 0);
            std::vector<std::thread> threads; threads.reserve(workers);
            for (unsigned int w = 0; w < workers; ++w) {
                const std::uint64_t begin = count * w / workers + 1;
                const std::uint64_t end = count * (w + 1) / workers;
                threads.emplace_back([&, w, begin, end] { std::uint64_t sum = 0; for (std::uint64_t v=begin; v<=end; ++v) sum += v; partial[w] = sum; });
            }
            for (auto& thread : threads) thread.join();
            std::uint64_t total = 0; for (auto p : partial) total += p;
            return static_cast<double>(total);
        }
        if (name == "gpu.available") { needCount(0); return checkOnly_ ? Value(true) : Value(anima02::QueryGpu().available); }
        if (name == "gpu.adapter") { needCount(0); return checkOnly_ ? Value(std::string("<GPU adapter resolved at runtime>")) : Value(anima02::QueryGpu().adapter); }
        if (name == "gpu.backend") { needCount(0); return checkOnly_ ? Value(std::string("<native GPU backend>")) : Value(anima02::QueryGpu().backend); }
        if (name == "gpu.vram_mb") { needCount(0); return checkOnly_ ? Value(0.0) : Value(static_cast<double>(anima02::QueryGpu().dedicatedMemoryMB)); }
        if (name == "gpu.dxr_supported") { needCount(0); return checkOnly_ ? Value(true) : Value(anima02::QueryGpu().dxrSupported); }
        if (name == "gpu.squares") {
            needCount(1); const double raw = AsNumber(args[0], line, name.c_str());
            if (raw < 1 || raw > 65536 || std::floor(raw) != raw) throw SourceError(line, "gpu.squares expects a whole count between 1 and 65536.");
            const auto count = static_cast<std::size_t>(raw);
            if (checkOnly_) return Series(count, 0.0);
            try { return anima02::ComputeSquaresOnGpu(count); }
            catch (const std::exception& e) { throw SourceError(line, e.what()); }
        }
        if (name == "host.platform") { needCount(0);
#ifdef _WIN32
            return std::string("windows.native");
#else
            return std::string("portable.host");
#endif
        }
        throw SourceError(line, "Unknown built-in '" + name + "'.");
    }

    void CheckType(const std::string& type, const Value& value, Reader& r) {
        bool ok = false;
        if (type == "whole") ok = std::holds_alternative<double>(value) && std::floor(std::get<double>(value)) == std::get<double>(value);
        else if (type == "real") ok = std::holds_alternative<double>(value);
        else if (type == "text") ok = std::holds_alternative<std::string>(value);
        else if (type == "truth") ok = std::holds_alternative<bool>(value);
        else if (type == "series") ok = std::holds_alternative<Series>(value);
        else if (type == "value") ok = true;
        else r.Fail("Unknown ANIMA type '" + type + "'. Types in 0.2: whole, real, text, truth, series, value.");
        if (!ok) r.Fail("Type mismatch: expected '" + type + "' but got '" + ValueTypeName(value) + "'.");
    }

    void ExecuteBlock(const std::string& body) {
        if (++callDepth_ > 64) throw SourceError(1, "Definition recursion limit (64) exceeded.");
        Reader r(body);
        while (!r.End()) {
            const std::string statement = r.ReadWord();
            if (statement == "announce") {
                Value v = ParseExpression(r);
                if (!checkOnly_) std::cout << Display(v) << '\n';
                r.SkipToEndOfLine();
            } else if (statement == "let") {
                const std::string name = r.ReadWord();
                std::string type;
                if (r.Consume(':')) type = r.ReadWord();
                r.RequireText("<-", "'<-' in a declaration");
                Value value = ParseExpression(r);
                if (!type.empty()) CheckType(type, value, r);
                values_[name] = std::move(value);
                r.SkipToEndOfLine();
            } else if (statement == "perform") {
                const std::string name = r.ReadWord();
                auto it = definitions_.find(name);
                if (it == definitions_.end()) r.Fail("No definition named '" + name + "' exists.");
                r.SkipToEndOfLine();
                ExecuteBlock(it->second);
            } else if (statement == "repeat") {
                const Value countValue = ParseExpression(r);
                const double raw = AsNumber(countValue, r.Line(), "repeat");
                if (raw < 0 || raw > 10000 || std::floor(raw) != raw) r.Fail("repeat needs a whole count between 0 and 10000.");
                const std::string repeated = r.CaptureBlock();
                r.SkipToEndOfLine();
                for (int i = 0; i < static_cast<int>(raw); ++i) ExecuteBlock(repeated);
            } else if (statement == "when") {
                const Value condition = ParseExpression(r);
                const bool truth = AsTruth(condition, r.Line());
                const std::string yesBody = r.CaptureBlock();
                const std::size_t saveLine = r.Line();
                (void)saveLine;
                const std::size_t markerLine = r.Line();
                if (truth) ExecuteBlock(yesBody);
                const std::size_t afterYes = r.Line();
                if (r.TryWord("otherwise")) {
                    const std::string noBody = r.CaptureBlock();
                    if (!truth) ExecuteBlock(noBody);
                    r.SkipToEndOfLine();
                } else {
                    (void)markerLine; (void)afterYes;
                }
            } else {
                r.Fail("Unknown statement '" + statement + "'. Use announce, let, perform, repeat, or when.");
            }
        }
        --callDepth_;
    }
};

std::string ReadUtf8(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open source file: " + path.string());
    std::ostringstream out; out << in.rdbuf();
    std::string text = out.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) text.erase(0,3);
    return text;
}
void PrintHelp() {
    std::cout << "ANIMA Native 0.2 Preview\n\n"
              << "Usage:\n"
              << "  AnimaLang.exe <file.ani>       Run an ANIMA program\n"
              << "  AnimaLang.exe --check <file>   Validate syntax without device work\n"
              << "  AnimaLang.exe --about          Show language/runtime information\n\n"
              << "ANIMA source is evaluated by the bundled native direct reader. No external\n"
              << "lexer/parser package, browser, Node.js, or scripting engine is required.\n";
}
}

int main(int argc, char** argv) {
    try {
        bool checkOnly = false;
        fs::path sourcePath;
        if (argc <= 1) { PrintHelp(); return 0; }
        std::string first = argv[1];
        if (first == "--help" || first == "-h") { PrintHelp(); return 0; }
        if (first == "--about") {
            std::cout << "ANIMA Native 0.2 Preview\n"
                      << "Native C++20 runtime; in-house direct reader/evaluator.\n"
                      << "CPU capabilities: logical core query, parallel sum.\n"
                      << "GPU capabilities: native adapter query, DXR feature query, D3D12 compute squares.\n";
            return 0;
        }
        if (first == "--check") {
            if (argc < 3) { PrintHelp(); return 2; }
            checkOnly = true; sourcePath = argv[2];
        } else sourcePath = argv[1];

        Runtime runtime(ReadUtf8(sourcePath), sourcePath, checkOnly);
        runtime.Run();
        if (checkOnly) std::cout << "ANIMA 0.2 syntax check passed: " << sourcePath.string() << '\n';
        return 0;
    } catch (const SourceError& e) {
        std::cerr << "ANIMA error at line " << e.line << ": " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "ANIMA runtime error: " << e.what() << '\n';
        return 1;
    }
}
