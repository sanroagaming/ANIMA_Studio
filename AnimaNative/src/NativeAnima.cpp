#include "NativeAnima.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace anima::native {
namespace {
enum class TokenKind { Identifier, Number, String, Color, Symbol };
struct Token { TokenKind kind; std::string text; std::size_t line; std::size_t column; };

std::vector<Token> Lex(std::string_view source) {
    std::vector<Token> out;
    std::size_t i = 0, line = 1, column = 1;
    auto advance = [&](char c) {
        if (c == '\n') { ++line; column = 1; }
        else { ++column; }
    };
    while (i < source.size()) {
        const char c = source[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(c); ++i; continue; }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') { advance(source[i]); ++i; }
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            advance(source[i++]); advance(source[i++]);
            bool closed = false;
            while (i < source.size()) {
                if (source[i] == '*' && i + 1 < source.size() && source[i + 1] == '/') {
                    advance(source[i++]); advance(source[i++]); closed = true; break;
                }
                advance(source[i++]);
            }
            if (!closed) throw std::runtime_error("Unterminated block comment at line " + std::to_string(line));
            continue;
        }
        const std::size_t start = i, startLine = line, startColumn = column;
        TokenKind kind = TokenKind::Symbol;
        if (c == '"' || c == '\'') {
            const char quote = c; advance(source[i++]); std::string value; bool closed = false;
            while (i < source.size()) {
                char ch = source[i];
                if (ch == quote) { advance(source[i++]); closed = true; break; }
                if (ch == '\\' && i + 1 < source.size()) { advance(source[i++]); value.push_back(source[i]); advance(source[i++]); }
                else { value.push_back(ch); advance(source[i++]); }
            }
            if (!closed) throw std::runtime_error("Unterminated string at " + std::to_string(startLine) + ":" + std::to_string(startColumn));
            out.push_back({TokenKind::String, std::move(value), startLine, startColumn});
            continue;
        }
        if (c == '#' && i + 1 < source.size() && std::isxdigit(static_cast<unsigned char>(source[i + 1]))) {
            kind = TokenKind::Color; advance(source[i++]);
            while (i < source.size() && std::isxdigit(static_cast<unsigned char>(source[i]))) advance(source[i++]);
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
            kind = TokenKind::Identifier;
            while (i < source.size()) {
                char ch = source[i];
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '$' && ch != '-') break;
                advance(source[i++]);
            }
        } else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[i + 1])))) {
            kind = TokenKind::Number;
            while (i < source.size() && (std::isdigit(static_cast<unsigned char>(source[i])) || source[i] == '.')) advance(source[i++]);
            while (i < source.size() && (std::isalpha(static_cast<unsigned char>(source[i])) || source[i] == '%')) advance(source[i++]);
        } else {
            // Preserve punctuation and two-character operators, enough for the
            // current ANIMA declarations and event/action expressions.
            static constexpr std::string_view pairs[] = {"->", "=>", "==", "!=", "<=", ">=", "&&", "||", "::"};
            bool pair = false;
            if (i + 1 < source.size()) {
                const std::string_view candidate = source.substr(i, 2);
                for (auto p : pairs) if (candidate == p) { advance(source[i++]); advance(source[i++]); pair = true; break; }
            }
            if (!pair) advance(source[i++]);
        }
        if (kind == TokenKind::Symbol) out.push_back({kind, std::string(source.substr(start, i - start)), startLine, startColumn});
        else out.push_back({kind, std::string(source.substr(start, i - start)), startLine, startColumn});
    }
    return out;
}
}

ProgramSummary ParseProgram(std::string_view source) {
    const auto tokens = Lex(source);
    if (tokens.empty()) throw std::runtime_error("The ANIMA source is empty.");
    ProgramSummary result{};
    result.tokenCount = tokens.size();
    std::vector<Token> braces;
    bool foundApp = false;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.kind == TokenKind::Identifier && t.text == "app" && !foundApp) {
            if (i + 1 >= tokens.size() || tokens[i + 1].kind != TokenKind::Identifier)
                throw std::runtime_error("Expected app name after 'app' at " + std::to_string(t.line) + ":" + std::to_string(t.column));
            result.appName = tokens[i + 1].text;
            foundApp = true;
        }
        if (t.kind == TokenKind::Identifier && t.text == "state") ++result.stateCount;
        if (t.kind == TokenKind::Identifier && t.text == "scene") ++result.sceneCount;
        if (t.kind == TokenKind::Identifier && t.text == "node") ++result.nodeCount;
        if (t.kind == TokenKind::Symbol && t.text == "{") braces.push_back(t);
        if (t.kind == TokenKind::Symbol && t.text == "}") {
            if (braces.empty()) throw std::runtime_error("Unexpected '}' at " + std::to_string(t.line) + ":" + std::to_string(t.column));
            braces.pop_back();
        }
    }
    if (!foundApp) throw std::runtime_error("No 'app Name { ... }' declaration was found.");
    if (!braces.empty()) {
        const auto& t = braces.back();
        throw std::runtime_error("Unclosed '{' opened at " + std::to_string(t.line) + ":" + std::to_string(t.column));
    }
    if (result.sceneCount == 0) throw std::runtime_error("The app has no scene declaration.");
    return result;
}

ProgramSummary ParseFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not open ANIMA program: " + path.string());
    std::ostringstream content; content << file.rdbuf();
    return ParseProgram(content.str());
}
}
