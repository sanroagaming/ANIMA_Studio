#include "AnimaGpu.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <regex>
#include <cctype>
#include <optional>
#include <sstream>
#include <set>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace anima {

struct Span { std::size_t start=0, end=0, line=1, col=1, endLine=1, endCol=1; };
enum class Severity { Error, Warning, Note };
struct Diagnostic { Severity severity; std::string code, message, file, help; Span span; };

class Diagnostics {
public:
    explicit Diagnostics(std::string file = "<memory>", std::string source = {}) : file_(std::move(file)), source_(std::move(source)) {}
    void add(Severity s, std::string code, std::string message, Span span={}, std::string help={}) {
        list.push_back({s, std::move(code), std::move(message), file_, std::move(help), span});
    }
    void error(std::string code, std::string message, Span span={}, std::string help={}) { add(Severity::Error, std::move(code), std::move(message), span, std::move(help)); }
    void warn(std::string code, std::string message, Span span={}, std::string help={}) { add(Severity::Warning, std::move(code), std::move(message), span, std::move(help)); }
    bool hasErrors() const { return std::any_of(list.begin(), list.end(), [](const auto& d){return d.severity==Severity::Error;}); }
    void print(std::ostream& out, bool json=false) const {
        for (const auto& d : list) {
            const char* sev = d.severity==Severity::Error ? "error" : d.severity==Severity::Warning ? "warning" : "note";
            if (json) {
                out << "{\"code\":\"" << escapeJson(d.code) << "\",\"severity\":\"" << sev << "\",\"file\":\"" << escapeJson(d.file) << "\",\"line\":" << d.span.line << ",\"column\":" << d.span.col << ",\"message\":\"" << escapeJson(d.message) << "\",\"help\":\"" << escapeJson(d.help) << "\"}\n";
                continue;
            }
            out << sev << '[' << d.code << "]: " << d.message << "\n  --> " << d.file << ':' << d.span.line << ':' << d.span.col << "\n";
            auto lines = splitLines(source_);
            if (d.span.line && d.span.line <= lines.size()) {
                const auto& line = lines[d.span.line-1];
                out << "   |\n" << std::setw(3) << d.span.line << " | " << line << "\n   | " << std::string(d.span.col>0?d.span.col-1:0,' ') << '^';
                const auto width = d.span.endLine==d.span.line && d.span.endCol>d.span.col ? d.span.endCol-d.span.col : std::size_t(1);
                out << std::string(std::min<std::size_t>(width>0?width-1:0,80),'~') << "\n";
            }
            if (!d.help.empty()) out << "  = help: " << d.help << "\n";
        }
    }
    std::vector<Diagnostic> list;
private:
    std::string file_, source_;
    static std::string escapeJson(std::string_view s) {
        std::string o;
        for (char c:s) { switch(c) { case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break; default: o += c; } }
        return o;
    }
    static std::vector<std::string> splitLines(const std::string& s) {
        std::vector<std::string> out; std::istringstream in(s); std::string x; while(std::getline(in,x)) out.push_back(x); if (out.empty()) out.push_back(""); return out;
    }
};

enum class TK { End, Newline, Identifier, Number, Text, Symbol };
struct Token { TK kind=TK::End; std::string text; Span span; };

static bool alpha(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static bool digit(char c) { return c>='0'&&c<='9'; }
static std::string tokenKindName(TK k) { switch(k){case TK::End:return "end";case TK::Newline:return "newline";case TK::Identifier:return "identifier";case TK::Number:return "number";case TK::Text:return "text";case TK::Symbol:return "symbol";} return "unknown"; }

class Lexer {
public:
    Lexer(std::string source, Diagnostics& diagnostics) : source_(std::move(source)), diag_(diagnostics) {}
    std::vector<Token> scanAll() {
        std::vector<Token> out;
        while (true) { Token t=next(); out.push_back(t); if(t.kind==TK::End) break; }
        return out;
    }
    Token next() {
        if (hasPending_) { hasPending_=false; return pending_; }
        bool newlineInsideComment = false;
        while (pos_ < source_.size()) {
            char c=source_[pos_];
            if(c==' '||c=='\t'||c=='\f'||c=='\v') { advance(); continue; }
            if(c=='\r'||c=='\n'||c==';') {
                Span sp=here();
                advance();
                if(parenDepth_==0 && bracketDepth_==0) return {TK::Newline,"\\n",sp};
                continue;
            }
            if(c=='#' || (c=='/'&&peekChar(1)=='/')) { while(pos_<source_.size() && source_[pos_]!='\n'&&source_[pos_]!='\r') advance(); continue; }
            if(c=='/'&&peekChar(1)=='*') {
                Span start=here(); advance();advance(); bool closed=false;
                while(pos_<source_.size()) {
                    if(source_[pos_]=='*'&&peekChar(1)=='/') {advance();advance();closed=true;break;}
                    if(source_[pos_]=='\n'||source_[pos_]=='\r') { newlineInsideComment=true; advance(); }
                    else advance();
                }
                if(!closed) diag_.error("ANI-L005","unterminated block comment",start,"add */ to close the comment");
                if(newlineInsideComment && parenDepth_==0 && bracketDepth_==0) { pending_={TK::Newline,"\\n",start}; hasPending_=true; return pendingAfterComment(); }
                continue;
            }
            break;
        }
        if(pos_>=source_.size()) return {TK::End,"",here()};
        Span sp=here(); char c=source_[pos_];
        if(alpha(c)) {
            std::size_t s=pos_; while(pos_<source_.size()&&(alpha(source_[pos_])||digit(source_[pos_])) ) advance();
            return make(TK::Identifier,source_.substr(s,pos_-s),sp);
        }
        if(digit(c)) return number(sp);
        if(c=='"') return stringToken(sp);
        static const std::vector<std::string> multi={"..=","==","!=","<=",">=","<-","->","+=","-=","*=","/=","%=",".."};
        for(const auto& op:multi) if(source_.compare(pos_,op.size(),op)==0) { for(std::size_t i=0;i<op.size();++i) advance(); return make(TK::Symbol,op,sp); }
        const std::string singles="{}()[],:.?+-*/%<>=.";
        if(singles.find(c)!=std::string::npos) {
            advance(); if(c=='(')++parenDepth_; else if(c==')'&&parenDepth_>0)--parenDepth_; else if(c=='[')++bracketDepth_; else if(c==']'&&bracketDepth_>0)--bracketDepth_;
            return make(TK::Symbol,std::string(1,c),sp);
        }
        advance(); diag_.error("ANI-L004",std::string("unexpected character '")+c+"'",sp,"remove the character or replace it with a supported ANIMA token");
        return next();
    }
private:
    std::string source_; Diagnostics& diag_; std::size_t pos_=0,line_=1,col_=1; int parenDepth_=0,bracketDepth_=0; bool hasPending_=false; Token pending_;
    char peekChar(std::size_t n) const {return pos_+n<source_.size()?source_[pos_+n]:'\0';}
    Span here() const { return {pos_,pos_,line_,col_,line_,col_}; }
    void advance() { if(pos_>=source_.size()) return; char c=source_[pos_++]; if(c=='\r') { if(pos_<source_.size()&&source_[pos_]=='\n') ++pos_; ++line_; col_=1; } else if(c=='\n') { ++line_; col_=1; } else if((static_cast<unsigned char>(c)&0xC0)!=0x80) ++col_; }
    Token make(TK kind,std::string text,Span start) const { start.end=pos_;start.endLine=line_;start.endCol=col_;return {kind,std::move(text),start}; }
    Token pendingAfterComment(){hasPending_=false;return pending_;}
    Token number(Span sp) {
        std::size_t s=pos_;
        if(source_[pos_]=='0'&&(peekChar(1)=='x'||peekChar(1)=='X'||peekChar(1)=='b'||peekChar(1)=='B')) {
            advance(); advance(); std::size_t digitsStart=pos_;
            while(pos_<source_.size()&&(digit(source_[pos_])||alpha(source_[pos_])||source_[pos_]=='_')) advance();
            if(pos_==digitsStart) diag_.error("ANI-L003","invalid number literal",sp,"add digits after the base prefix");
        } else {
            while(pos_<source_.size()&&(digit(source_[pos_])||source_[pos_]=='_')) advance();
            if(pos_<source_.size()&&source_[pos_]=='.'&&peekChar(1)!='.'&&digit(peekChar(1))) { advance(); while(pos_<source_.size()&&(digit(source_[pos_])||source_[pos_]=='_'))advance(); }
            if(pos_<source_.size()&&(source_[pos_]=='e'||source_[pos_]=='E')) { std::size_t e=pos_; advance(); if(pos_<source_.size()&&(source_[pos_]=='+'||source_[pos_]=='-'))advance(); std::size_t ds=pos_;while(pos_<source_.size()&&(digit(source_[pos_])||source_[pos_]=='_'))advance();if(ds==pos_){pos_=e;col_-=pos_-e;} }
        }
        return make(TK::Number,source_.substr(s,pos_-s),sp);
    }
    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if(cp<=0x7F)out.push_back(static_cast<char>(cp));else if(cp<=0x7FF){out.push_back(char(0xC0|(cp>>6)));out.push_back(char(0x80|(cp&63)));}else if(cp<=0xFFFF){out.push_back(char(0xE0|(cp>>12)));out.push_back(char(0x80|((cp>>6)&63)));out.push_back(char(0x80|(cp&63)));}else{out.push_back(char(0xF0|(cp>>18)));out.push_back(char(0x80|((cp>>12)&63)));out.push_back(char(0x80|((cp>>6)&63)));out.push_back(char(0x80|(cp&63)));}
    }
    Token stringToken(Span sp) {
        const bool block=source_.compare(pos_,3,"\"\"\"")==0; const std::size_t quoteCount=block?3:1;
        for(std::size_t i=0;i<quoteCount;++i) advance();
        std::string out; bool closed=false;
        while(pos_<source_.size()) {
            if(block&&source_.compare(pos_,3,"\"\"\"")==0){for(int i=0;i<3;++i)advance();closed=true;break;}
            char c=source_[pos_];
            if(!block&&c=='"'){advance();closed=true;break;}
            if(!block&&(c=='\n'||c=='\r')) {diag_.error("ANI-L001","unterminated text literal",sp,"close the string on this line or use a triple-quoted block literal");break;}
            advance();
            if(c!='\\'){out.push_back(c);continue;}
            if(pos_>=source_.size())break;
            char e=source_[pos_];advance();
            switch(e){case 'n':out.push_back('\n');break;case 'r':out.push_back('\r');break;case 't':out.push_back('\t');break;case '"':out.push_back('"');break;case '\\':out.push_back('\\');break;case '{':out.push_back('\x01');break;case '}':out.push_back('\x02');break;
            case 'u': { if(pos_>=source_.size()||source_[pos_]!='{'){diag_.error("ANI-L007","invalid Unicode escape",sp,"use \\u{HEX}");break;} advance();std::uint32_t cp=0;int n=0;while(pos_<source_.size()&&source_[pos_]!='}') {char h=source_[pos_];int v=(h>='0'&&h<='9')?h-'0':(h>='a'&&h<='f')?h-'a'+10:(h>='A'&&h<='F')?h-'A'+10:-1;if(v<0||n>=6){diag_.error("ANI-L007","invalid Unicode escape",sp,"use 1 to 6 hexadecimal digits");while(pos_<source_.size()&&source_[pos_]!='}')advance();break;}cp=(cp<<4)|static_cast<unsigned>(v);++n;advance();}if(pos_<source_.size()&&source_[pos_]=='}')advance();if(n==0||cp>0x10FFFF||(cp>=0xD800&&cp<=0xDFFF))diag_.error("ANI-L007","invalid Unicode scalar value",sp,"use a valid Unicode scalar value");else appendUtf8(out,cp);break;}
            default: diag_.error("ANI-L002", "invalid escape sequence", sp, "allowed escapes: newline, carriage return, tab, quote, backslash, escaped braces, or a Unicode brace escape"); out.push_back(e); break;
        }
        } // end string scanning loop
        if(!closed)diag_.error("ANI-L001","unterminated text literal",sp,"close this string literal");
        if(block && out.size()>=1 && out[0]=='\n')out.erase(out.begin());
        return make(TK::Text,std::move(out),sp);
    }
};

struct Expr; struct Stmt;
using ExprPtr=std::unique_ptr<Expr>; using StmtPtr=std::unique_ptr<Stmt>;
using Literal=std::variant<std::monostate,std::int64_t,double,bool,std::string>;
enum class EK { Literal, Identifier, Unary, Binary, Call, Member, List, Map, Index, Slice, Lambda, With };
struct Expr {
    EK kind=EK::Literal; Span span; std::string text; Literal literal; ExprPtr a,b; std::vector<ExprPtr> items; std::vector<std::string> names;
};
struct Param { std::string name,type; ExprPtr defaultValue; Span span; };
struct FunctionDecl { std::string name,returnType; bool hasReturn=false; std::vector<Param> params; StmtPtr body; Span span; };
enum class SK { Block, Let, Fixed, Assign, Announce, Give, Expr, When, RepeatCount, RepeatWhile, RepeatEach, RepeatRange, Stop, Skip, Fail, Attempt };
struct Branch { ExprPtr condition; StmtPtr body; };
struct Stmt {
    SK kind=SK::Block; Span span; std::string name,type,op,name2; ExprPtr expr,expr2; std::vector<StmtPtr> statements; StmtPtr body,otherwiseBody; std::vector<Branch> branches; std::vector<std::pair<std::string,StmtPtr>> rescues;
};
struct RawDecl { std::string kind,name,raw; Span span; };
struct RecordFieldDecl { std::string name,type,defaultSource; std::optional<Literal> defaultValue; std::string defaultEnum, defaultEnumCase; Span span{}; };
struct RecordDecl { std::string name; std::vector<RecordFieldDecl> fields; Span span{}; };
struct EnumCaseDecl { std::string name; std::int64_t value=0; };
struct EnumDecl { std::string name; std::vector<EnumCaseDecl> cases; Span span{}; };
struct TestDecl { std::string name; StmtPtr body; Span span; };
struct Program {
    std::string version="0.3", projectName; bool hasStart=false; StmtPtr start;
    std::vector<FunctionDecl> functions; std::vector<StmtPtr> globals; std::vector<TestDecl> tests; std::vector<RawDecl> raw; std::vector<RecordDecl> records; std::vector<EnumDecl> enums;
    std::vector<std::string> imports, capabilities; std::string source,file;
};

class Parser {
public:
    Parser(std::string source,std::string file,Diagnostics& d,bool compat02=false):source_(std::move(source)),file_(std::move(file)),diag_(d),compat02_(compat02){Lexer l(source_,diag_);tokens_=l.scanAll();}
    Program parse() {
        Program p; p.source=source_;p.file=file_;
        skipNl();
        if(!word("ANIMA")) {diag_.error("ANI-P001","expected ANIMA 0.3 header",peek().span,"start the file with ANIMA 0.3");} else {
            consume(); const Token ver=consume();p.version=ver.text;
            if(ver.kind!=TK::Number || (ver.text!="0.3"&&!(compat02_&&ver.text=="0.2")))diag_.error("ANI-P005","unsupported ANIMA source version '"+ver.text+"'",ver.span,compat02_?"use ANIMA 0.3 or ANIMA 0.2 with --compat 0.2":"run `anima migrate` for ANIMA 0.2 source");
            terminator(false);
        }
        while(!isEnd()) {
            skipNl(); if(isEnd())break;
            if(word("project")) parseProject(p);
            else if(word("use")||word("from")) parseImport(p);
            else if(word("define")) p.functions.push_back(parseFunction());
            else if(word("start")) {Token t=consume();if(p.hasStart)diag_.error("ANI-P004","duplicate start block",t.span,"only the entry module may contain one start block");p.hasStart=true;p.start=parseBlock();}
            else if(word("test")) {Token t=consume();std::string label="unnamed test";if(peek().kind==TK::Text)label=consume().text;auto b=parseBlock();p.tests.push_back({label,std::move(b),t.span});}
            else if(word("fixed")||word("let")) p.globals.push_back(parseStatement());
            else if(word("record")) { auto r=parseRaw(); p.records.push_back(parseRecordMetadata(r)); p.raw.push_back(std::move(r)); }
            else if(word("enum")) { auto r=parseRaw(); p.enums.push_back(parseEnumMetadata(r)); p.raw.push_back(std::move(r)); }
            else if(word("app")||word("class")||word("trait")||word("abstract")||word("extend")||word("export")) p.raw.push_back(parseRaw());
            else {diag_.error("ANI-P002","unexpected top-level token '"+peek().text+"'",peek().span,"expected project, use, define, start, test, fixed, or a type/UI declaration"); synchronizeTop();}
        }
        return p;
    }
    ExprPtr parseExpressionOnly() { auto e=expression();skipNl();if(!isEnd())diag_.error("ANI-P002","unexpected token after expression: '"+peek().text+"'",peek().span);return e; }
    const std::vector<Token>& tokens()const{return tokens_;}
private:
    std::string source_,file_;Diagnostics& diag_;bool compat02_;std::vector<Token> tokens_;std::size_t i_=0;
    const Token& peek(std::size_t n=0)const{return tokens_[std::min(i_+n,tokens_.size()-1)];}
    bool isEnd()const{return peek().kind==TK::End;}
    Token consume(){return tokens_[i_++];}
    bool symbol(std::string_view s)const{return peek().kind==TK::Symbol&&peek().text==s;}
    bool word(std::string_view s)const{return peek().kind==TK::Identifier&&peek().text==s;}
    bool matchSym(std::string_view s){if(symbol(s)){consume();return true;}return false;}
    bool matchWord(std::string_view s){if(word(s)){consume();return true;}return false;}
    Token requireSym(std::string_view s,const std::string& why){if(symbol(s))return consume();diag_.error("ANI-P003","expected '"+std::string(s)+"' "+why,peek().span);return {TK::Symbol,std::string(s),peek().span};}
    Token requireWordToken(const std::string& why){if(peek().kind==TK::Identifier)return consume();diag_.error("ANI-P003","expected name "+why,peek().span);return {TK::Identifier,"<missing>",peek().span};}
    void skipNl(){while(peek().kind==TK::Newline)consume();}
    void terminator(bool required=true){if(matchSym(";"))return;if(peek().kind==TK::Newline){skipNl();return;}if(symbol("}")||isEnd())return;if(required)diag_.error("ANI-P006","expected end of statement",peek().span,"put each statement on its own line or add ;");}
    void synchronizeTop(){while(!isEnd()&&!symbol("}")&&peek().kind!=TK::Newline)consume();skipNl();}
    std::string qname(){Token t=requireWordToken("in qualified name");std::string out=t.text;while(matchSym(".")){out+='.';out+=requireWordToken("after .").text;}return out;}
    std::string typeName(){
        if(matchWord("list")){if(!matchWord("of"))diag_.error("ANI-P003","expected 'of' in list type",peek().span);return "list of "+typeName();}
        if(matchWord("map")){if(!matchWord("of"))diag_.error("ANI-P003","expected 'of' in map type",peek().span);auto k=typeName();if(!matchWord("to"))diag_.error("ANI-P003","expected 'to' in map type",peek().span);return "map of "+k+" to "+typeName();}
        if(matchWord("maybe"))return "maybe "+typeName();
        if(matchWord("weak"))return "weak "+qname();
        if(matchWord("fn")){requireSym("(","after fn in function type");std::string s="fn(";bool first=true;while(!symbol(")")&&!isEnd()){if(!first)s+=", ";s+=typeName();first=false;if(!matchSym(","))break;}requireSym(")","after function type");s+=")";if(matchSym("->"))s+=" -> "+typeName();return s;}
        return qname();
    }
    void parseProject(Program& p){
        Token t=consume();if(peek().kind==TK::Text)p.projectName=consume().text;else diag_.error("ANI-P003","project needs a quoted name",peek().span);
        requireSym("{","after project name");
        while(!isEnd()&&!symbol("}")){
            skipNl();if(symbol("}")||isEnd())break;
            if(word("capability")){consume();auto n=qname();p.capabilities.push_back(n);terminator();continue;}
            if(word("target")||word("entry")||word("version")||word("runtime")){consume();while(!isEnd()&&peek().kind!=TK::Newline&&!symbol("}"))consume();skipNl();continue;}
            if(word("paths")){consume();while(!isEnd()&&peek().kind!=TK::Newline&&!symbol("}"))consume();skipNl();continue;}
            diag_.error("ANI-P002","unknown project field '"+peek().text+"'",peek().span);synchronizeTop();
        }
        requireSym("}","to close project block"); (void)t;
    }
    void parseImport(Program& p){
        if(matchWord("use")){std::string n;if(peek().kind==TK::Text)n=consume().text;else n=qname();if(matchWord("as"))n+=" as "+requireWordToken("after as").text;p.imports.push_back(n);terminator();return;}
        consume();std::string module=qname();if(!matchWord("use"))diag_.error("ANI-P003","expected use after module name",peek().span);std::string name=requireWordToken("after from ... use").text;while(matchSym(","))name+=", "+requireWordToken("after comma").text;p.imports.push_back(module+" use "+name);terminator();
    }
    FunctionDecl parseFunction(){
        Token start=consume();FunctionDecl f;f.name=requireWordToken("after define").text;f.span=start.span;
        // ANIMA 0.2 compatibility only: legacy zero-argument declarations used
        // `define name { ... }`. ANIMA 0.3 requires the canonical `name(...)` form.
        if (!symbol("(") && compat02_) {
            f.body=parseBlock();
            return f;
        }
        requireSym("(","after function name");skipNl();
        while(!isEnd()&&!symbol(")")){
            Param p;p.span=peek().span;p.name=requireWordToken("as parameter").text;
            if(!matchSym(":"))diag_.error("ANI-P003","parameter '"+p.name+"' requires a type annotation",peek().span,"write name: type");else p.type=typeName();
            if(matchSym("<-")) p.defaultValue=expression();
            f.params.push_back(std::move(p));
            if(!matchSym(",")) break;
            skipNl();
        }
        requireSym(")","after parameters");if(matchSym("->")){f.returnType=typeName();f.hasReturn=true;}
        if(peek().kind==TK::Newline && peek(1).text=="{")diag_.error("ANI-P012","opening { must be on the same line as the function signature",peek().span,"put the block opening brace on the declaration line");
        f.body=parseBlock();return f;
    }
    StmtPtr parseBlock(){
        auto b=std::make_unique<Stmt>();b->kind=SK::Block;b->span=peek().span;
        if(!matchSym("{")){diag_.error("ANI-P003","expected a block beginning with '{'",peek().span);return b;}
        while(!isEnd()&&!symbol("}")){skipNl();if(symbol("}")||isEnd())break;auto before=i_;b->statements.push_back(parseStatement());if(i_==before)consume();}
        requireSym("}","to close block");return b;
    }
    StmtPtr parseStatement(){
        skipNl();Token start=peek();
        if(word("let")||word("fixed")){bool cn=word("fixed");consume();auto s=std::make_unique<Stmt>();s->kind=cn?SK::Fixed:SK::Let;s->span=start.span;s->name=requireWordToken("after declaration").text;if(matchSym(":"))s->type=typeName();if(!matchSym("<-"))diag_.error("ANI-P003","expected <- in declaration",peek().span);s->expr=expression();terminator();return s;}
        if(matchWord("announce")){auto s=std::make_unique<Stmt>();s->kind=SK::Announce;s->span=start.span;s->expr=expression();terminator();return s;}
        if(matchWord("give")){auto s=std::make_unique<Stmt>();s->kind=SK::Give;s->span=start.span;if(peek().kind!=TK::Newline&&!symbol("}")&&!isEnd())s->expr=expression();terminator(false);return s;}
        if(matchWord("stop")){auto s=std::make_unique<Stmt>();s->kind=SK::Stop;s->span=start.span;terminator();return s;}
        if(matchWord("skip")){auto s=std::make_unique<Stmt>();s->kind=SK::Skip;s->span=start.span;terminator();return s;}
        if(matchWord("fail")){auto s=std::make_unique<Stmt>();s->kind=SK::Fail;s->span=start.span;s->expr=expression();terminator();return s;}
        if(matchWord("perform")){Token name=requireWordToken("after perform");auto s=std::make_unique<Stmt>();s->kind=SK::Expr;s->span=start.span;s->expr=std::make_unique<Expr>();s->expr->kind=EK::Call;s->expr->span=start.span;s->expr->a=std::make_unique<Expr>();s->expr->a->kind=EK::Identifier;s->expr->a->text=name.text;s->expr->a->span=name.span;if(!compat02_)diag_.warn("ANI-W004","perform is deprecated; write "+name.text+"()",start.span,"replace perform name with name()");terminator();return s;}
        if(matchWord("when"))return parseWhen(start);
        if(matchWord("repeat"))return parseRepeat(start);
        if(matchWord("attempt")){auto s=std::make_unique<Stmt>();s->kind=SK::Attempt;s->span=start.span;s->body=parseBlock();skipNl();while(matchWord("rescue")){std::string nm=requireWordToken("after rescue").text;if(matchSym(":"))nm+=" : "+qname();s->rescues.emplace_back(nm,parseBlock());skipNl();}if(matchWord("always"))s->otherwiseBody=parseBlock();return s;}
        if(symbol("{"))return parseBlock();
        auto left=expression();
        if(symbol("<-")||symbol("+=")||symbol("-=")||symbol("*=")||symbol("/=")||symbol("%=")){auto op=consume().text;auto s=std::make_unique<Stmt>();s->kind=SK::Assign;s->span=start.span;s->name=left->kind==EK::Identifier?left->text:"";s->expr=std::move(left);s->op=op;s->expr2=expression();if(s->name.empty()&&s->expr->kind!=EK::Member&&s->expr->kind!=EK::Index)diag_.error("ANI-P003","invalid assignment target",s->span,"use a declared name, member access, or index expression");terminator();return s;}
        auto s=std::make_unique<Stmt>();s->kind=SK::Expr;s->span=start.span;s->expr=std::move(left);if(s->expr->kind!=EK::Call)diag_.error("ANI-P014","an expression statement must be a function call",s->span,"write a call such as update(dt)");terminator();return s;
    }
    StmtPtr parseWhen(Token start){
        auto s=std::make_unique<Stmt>();s->kind=SK::When;s->span=start.span;s->expr=expression();s->body=parseBlock();skipNl();
        while(matchWord("otherwise")){if(matchWord("when")){Branch b;b.condition=expression();b.body=parseBlock();s->branches.push_back(std::move(b));skipNl();continue;}s->otherwiseBody=parseBlock();break;}
        return s;
    }
    StmtPtr parseRepeat(Token start){
        auto s=std::make_unique<Stmt>();s->span=start.span;
        if(matchWord("while")){s->kind=SK::RepeatWhile;s->expr=expression();s->body=parseBlock();return s;}
        if(matchWord("each")){s->kind=SK::RepeatEach;s->name=requireWordToken("after repeat each").text;if(matchSym(","))s->name2=requireWordToken("after comma").text;if(!matchWord("in"))diag_.error("ANI-P003","expected in after repeat each variable",peek().span);s->expr=expression();s->body=parseBlock();return s;}
        if(peek().kind==TK::Identifier&&peek(1).kind==TK::Identifier&&peek(1).text=="in") {s->kind=SK::RepeatRange;s->name=consume().text;consume();s->expr=expression();s->body=parseBlock();return s;}
        s->kind=SK::RepeatCount;s->expr=expression();s->body=parseBlock();return s;
    }
    static std::string trimText(std::string value) {
        auto first=value.find_first_not_of(" \t\r\n");
        if(first==std::string::npos) return {};
        auto last=value.find_last_not_of(" \t\r\n");
        return value.substr(first,last-first+1);
    }
    static std::optional<Literal> parseDefaultLiteral(const std::string& source) {
        const auto value=trimText(source);
        if(value.empty()) return std::nullopt;
        if(value=="true") return Literal(true);
        if(value=="false") return Literal(false);
        if(value=="none") return Literal(std::monostate{});
        if(value.size()>=2 && value.front()=='"' && value.back()=='"') {
            std::string out;
            for(std::size_t i=1;i+1<value.size();++i) {
                char c=value[i];
                if(c=='\\' && i+2<value.size()) {
                    char n=value[++i];
                    switch(n){case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;case '"':out+='"';break;case '\\':out+='\\';break;default:out+=n;break;}
                } else out+=c;
            }
            return Literal(std::move(out));
        }
        if(std::regex_match(value,std::regex(R"([+-]?[0-9][0-9_]*)"))) {
            std::string n=value; n.erase(std::remove(n.begin(),n.end(),'_'),n.end());
            std::int64_t out=0;auto [ptr,ec]=std::from_chars(n.data(),n.data()+n.size(),out);
            if(ec==std::errc{}&&ptr==n.data()+n.size()) return Literal(out);
            return std::nullopt;
        }
        if(std::regex_match(value,std::regex(R"([+-]?(?:[0-9]+\.[0-9]+|[0-9]+[eE][+-]?[0-9]+|[0-9]+\.[0-9]+[eE][+-]?[0-9]+))"))) {
            char* end=nullptr;double out=std::strtod(value.c_str(),&end);if(end&&*end=='\0')return Literal(out);
        }
        return std::nullopt;
    }
    RecordDecl parseRecordMetadata(const RawDecl& raw) {
        RecordDecl rec; rec.name=raw.name; rec.span=raw.span;
        const auto open=raw.raw.find('{'), close=raw.raw.rfind('}');
        if(open==std::string::npos||close==std::string::npos||close<=open)return rec;
        std::istringstream input(raw.raw.substr(open+1,close-open-1));std::string line;bool insideMethod=false;int methodDepth=0;std::size_t lineNo=raw.span.line;
        const std::regex fieldRe(R"(^\s*([A-Za-z_][A-Za-z_0-9]*)\s*:\s*(.*?)\s*$)");
        while(std::getline(input,line)) {
            ++lineNo;auto text=trimText(line);if(text.empty()||text[0]=='#'||text.rfind("//",0)==0)continue;
            if(text.rfind("define ",0)==0||text.rfind("private define ",0)==0||text.rfind("shared define ",0)==0){insideMethod=true;methodDepth=0;for(char c:text){if(c=='{')++methodDepth;if(c=='}')--methodDepth;}if(methodDepth<=0)insideMethod=false;continue;}
            if(insideMethod){for(char c:text){if(c=='{')++methodDepth;if(c=='}')--methodDepth;}if(methodDepth<=0)insideMethod=false;continue;}
            std::smatch match;if(!std::regex_match(line,match,fieldRe))continue;
            std::string name=match[1].str(), spec=trimText(match[2].str());
            const auto arrow=spec.find("<-");std::string def;
            if(arrow!=std::string::npos){def=trimText(spec.substr(arrow+2));spec=trimText(spec.substr(0,arrow));}
            if(spec.empty()||spec.rfind("define",0)==0)continue;
            RecordFieldDecl fld;fld.name=name;fld.type=spec;fld.defaultSource=def;fld.span=raw.span;fld.span.line=lineNo;
            if(!def.empty()) {
                fld.defaultValue=parseDefaultLiteral(def);
                if(!fld.defaultValue) {
                    std::smatch enumDefault;
                    static const std::regex enumDefaultRe(R"(^([A-Za-z_][A-Za-z_0-9]*(?:\.[A-Za-z_][A-Za-z_0-9]*)*)\.([A-Za-z_][A-Za-z_0-9]*)$)");
                    if(std::regex_match(def,enumDefault,enumDefaultRe)) {
                        fld.defaultEnum=enumDefault[1].str();
                        fld.defaultEnumCase=enumDefault[2].str();
                    }
                }
            }
            rec.fields.push_back(std::move(fld));
        }
        return rec;
    }
    EnumDecl parseEnumMetadata(const RawDecl& raw) {
        EnumDecl result; result.name=raw.name;result.span=raw.span;
        const auto open=raw.raw.find('{'),close=raw.raw.rfind('}');
        if(open==std::string::npos||close==std::string::npos||close<=open)return result;
        std::string body=raw.raw.substr(open+1,close-open-1);std::string token;std::int64_t nextValue=0;
        auto flush=[&](){auto item=trimText(token);token.clear();if(item.empty())return;auto comment=item.find('#');if(comment!=std::string::npos)item=trimText(item.substr(0,comment));if(item.empty())return;std::smatch m;const std::regex caseRe(R"(^([A-Za-z_][A-Za-z_0-9]*)(?:\s*<-\s*(-?[0-9]+))?$)");if(!std::regex_match(item,m,caseRe))return;std::int64_t v=nextValue;if(m[2].matched){try{v=std::stoll(m[2].str());}catch(...){v=nextValue;}}result.cases.push_back({m[1].str(),v});nextValue=v==std::numeric_limits<std::int64_t>::max()?v:v+1;};
        for(char c:body){if(c==','||c=='\n'||c=='\r'){flush();}else token+=c;}flush();return result;
    }
    RawDecl parseRaw(){
        Span sp=peek().span;std::size_t start=peek().span.start;std::string kind;
        if(matchWord("export")){kind="export";if(!isEnd()&&peek().kind==TK::Identifier)kind+=" "+consume().text;} else if(matchWord("abstract")){kind="abstract";if(!isEnd()&&peek().kind==TK::Identifier)kind+=" "+consume().text;}else kind=consume().text;
        std::string name;if(peek().kind==TK::Identifier)name=consume().text;else if(peek().kind==TK::Text)name=consume().text;
        while(!isEnd()&&!symbol("{")&&peek().kind!=TK::Newline)consume();
        if(symbol("{")){int depth=0;do{if(matchSym("{"))++depth;else if(matchSym("}"))--depth;else consume();}while(depth>0&&!isEnd());if(depth>0)diag_.error("ANI-P003","unclosed declaration block",sp,"add a closing }");}
        std::size_t end=(i_>0?tokens_[i_-1].span.end:sp.end);return {kind,name,source_.substr(start,end-start),sp};
    }
    ExprPtr expression(int minPrec=0){
        auto left=prefix();
        while(true){
            if(symbol("(")&&9>=minPrec){Token s=consume();auto c=std::make_unique<Expr>();c->kind=EK::Call;c->span=left->span;c->a=std::move(left);skipNl();while(!isEnd()&&!symbol(")")){std::string nm;if(peek().kind==TK::Identifier&&peek(1).text==":"){nm=consume().text;consume();}c->names.push_back(nm);c->items.push_back(expression());if(!matchSym(","))break;skipNl();}Token e=requireSym(")","after arguments");c->span.end=e.span.end;c->items.shrink_to_fit();left=std::move(c);continue;}
            if(symbol(".")&&9>=minPrec){consume();auto m=std::make_unique<Expr>();m->kind=EK::Member;m->span=left->span;m->a=std::move(left);m->text=requireWordToken("after .").text;m->span.end=peek().span.end;left=std::move(m);continue;}
            if(symbol("[")&&9>=minPrec){consume();auto idx=std::make_unique<Expr>();idx->kind=EK::Index;idx->span=left->span;idx->a=std::move(left);idx->b=expression();if(matchSym("..")||matchSym("..=")){auto rangeOp=tokens_[i_-1].text;idx->kind=EK::Slice;auto end=expression();auto combined=std::make_unique<Expr>();combined->kind=EK::Binary;combined->text=rangeOp;combined->a=std::move(idx->b);combined->b=std::move(end);idx->b=std::move(combined);}requireSym("]","after index");left=std::move(idx);continue;}
            const std::string op=peek().text;int prec=precedence(op);if(prec<minPrec||prec<0)break;
            if(op=="with") { Token ot=consume(); auto update=std::make_unique<Expr>();update->kind=EK::With;update->span=left->span;update->a=std::move(left);requireSym("{","after with");skipNl();while(!isEnd()&&!symbol("}")){std::string field=requireWordToken("as record field in with expression").text;requireSym(":","after record field name");update->names.push_back(field);update->items.push_back(expression());skipNl();if(!matchSym(","))break;skipNl();}auto end=requireSym("}","after record update");update->span.end=end.span.end;left=std::move(update);(void)ot;continue; }
            Token ot=consume();auto right=expression(prec+1);auto b=std::make_unique<Expr>();b->kind=EK::Binary;b->text=ot.text;b->span=left->span;b->a=std::move(left);b->b=std::move(right);left=std::move(b);
        }
        return left;
    }
    int precedence(const std::string& op)const{
        if(op=="with") return 1;
        if(op=="fallback") return 2;
        if(op=="or") return 3;
        if(op=="and") return 4;
        if(op=="=="||op=="!=") return 5;
        if(op=="<"||op=="<="||op==">"||op==">="||op=="is"||op=="as") return 6;
        if(op==".."||op=="..=") return 7;
        if(op=="+"||op=="-") return 8;
        if(op=="*"||op=="/"||op=="%") return 9;
        return -1;
    }
    ExprPtr prefix(){
        Token t=consume();auto e=std::make_unique<Expr>();e->span=t.span;
        // The signed minimum cannot be represented as a positive int64 token, so
        // accept that one decimal spelling only when immediately preceded by unary -.
        if(t.kind==TK::Symbol&&t.text=="-"&&peek().kind==TK::Number&&peek().text=="9223372036854775808"){
            Token magnitude=consume();e->kind=EK::Literal;e->text="-"+magnitude.text;e->literal=std::numeric_limits<std::int64_t>::min();e->span.end=magnitude.span.end;return e;
        }
        if(t.kind==TK::Number){
            e->kind=EK::Literal;e->text=t.text;std::string clean;for(char c:t.text)if(c!='_')clean+=c;
            try{
                if(clean.size()>2&&clean[0]=='0'&&(clean[1]=='x'||clean[1]=='X')){
                    auto value=std::stoull(clean,nullptr,16);if(value>static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max()))throw std::out_of_range("int64");e->literal=static_cast<std::int64_t>(value);
                }else if(clean.size()>2&&clean[0]=='0'&&(clean[1]=='b'||clean[1]=='B')){
                    auto value=std::stoull(clean.substr(2),nullptr,2);if(value>static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max()))throw std::out_of_range("int64");e->literal=static_cast<std::int64_t>(value);
                }else if(clean.find_first_of(".eE")!=std::string::npos)e->literal=std::stod(clean);
                else e->literal=static_cast<std::int64_t>(std::stoll(clean));
            }catch(...){diag_.error("ANI-L008","whole literal is out of range or malformed",t.span,"use a valid number within signed 64-bit range");e->literal=std::int64_t(0);}return e;
        }
        if(t.kind==TK::Text){e->kind=EK::Literal;e->literal=t.text;return e;}
        if(t.kind==TK::Identifier&& (t.text=="true"||t.text=="false")){e->kind=EK::Literal;e->literal=(t.text=="true");return e;}
        if(t.kind==TK::Identifier&&t.text=="none"){e->kind=EK::Literal;e->literal=std::monostate{};return e;}
        if(t.kind==TK::Identifier&&(t.text=="not"||t.text=="-")){e->kind=EK::Unary;e->text=t.text;e->a=expression(10);return e;}
        if(t.kind==TK::Symbol&&t.text=="-"){e->kind=EK::Unary;e->text=t.text;e->a=expression(10);return e;}
        if(t.kind==TK::Identifier&&t.text=="fn") {e->kind=EK::Lambda;auto fake=parseLambdaBody(t);e->text=fake;return e;}
        if(t.kind==TK::Symbol&&t.text=="("){auto inner=expression();requireSym(")","after expression");return inner;}
        if(t.kind==TK::Symbol&&t.text=="["){
            e->kind=EK::List;skipNl();if(matchSym(":")){e->kind=EK::Map;requireSym("]","after empty map");return e;}if(matchSym("]"))return e;
            auto first=expression();if(matchSym(":")){e->kind=EK::Map;e->items.push_back(std::move(first));e->items.push_back(expression());while(matchSym(",")){skipNl();if(symbol("]"))break;e->items.push_back(expression());requireSym(":","between map key and value");e->items.push_back(expression());}requireSym("]","after map literal");return e;}
            e->items.push_back(std::move(first));while(matchSym(",")){skipNl();if(symbol("]"))break;e->items.push_back(expression());}requireSym("]","after list literal");return e;
        }
        if(t.kind==TK::Identifier){e->kind=EK::Identifier;e->text=t.text;return e;}
        diag_.error("ANI-P002","expected an expression, found '"+t.text+"'",t.span,"check the expression or add parentheses");e->kind=EK::Literal;e->literal=std::monostate{};return e;
    }
    std::string parseLambdaBody(const Token& t){
        // The front-end preview recognises and preserves lambda syntax. Evaluation of lambda values is scheduled for 0.3b.
        if(!matchSym("(")){diag_.error("ANI-P003","expected ( after fn",peek().span);return "fn";}
        int par=1;std::string raw="fn(";while(!isEnd()&&par>0){auto tok=consume();if(tok.text=="(")++par;if(tok.text==")")--par;raw+=tok.text;}if(matchSym("->")){raw+=" -> ";raw+=typeName();}
        if(symbol("{")){int d=0;do{auto tok=consume();if(tok.text=="{")++d;if(tok.text=="}")--d;raw+=tok.text;}while(!isEnd()&&d>0);}else diag_.error("ANI-P003","expected lambda block",peek().span);(void)t;return raw;
    }
};

static std::string typeOfLiteral(const Literal& l){if(std::holds_alternative<std::int64_t>(l))return "whole";if(std::holds_alternative<double>(l))return "real";if(std::holds_alternative<bool>(l))return "truth";if(std::holds_alternative<std::string>(l))return "text";return "maybe value";}
static std::string sexpr(const Expr* e){if(!e)return "_";std::ostringstream o;switch(e->kind){case EK::Literal:if(auto p=std::get_if<std::string>(&e->literal))o<<"(text \""<<*p<<"\")";else if(auto p=std::get_if<std::int64_t>(&e->literal))o<<"(whole "<<*p<<")";else if(auto p=std::get_if<double>(&e->literal))o<<"(real "<<*p<<")";else if(auto p=std::get_if<bool>(&e->literal))o<<"(truth "<<(*p?"true":"false")<<")";else o<<"none";break;case EK::Identifier:o<<"(name "<<e->text<<")";break;case EK::Unary:o<<"("<<e->text<<' '<<sexpr(e->a.get())<<")";break;case EK::Binary:o<<"("<<e->text<<' '<<sexpr(e->a.get())<<' '<<sexpr(e->b.get())<<")";break;case EK::Member:o<<"(. "<<sexpr(e->a.get())<<' '<<e->text<<")";break;case EK::Call:o<<"(call "<<sexpr(e->a.get());for(std::size_t i=0;i<e->items.size();++i)o<<" "<<(i<e->names.size()&&!e->names[i].empty()?e->names[i]+":" : "")<<sexpr(e->items[i].get());o<<")";break;case EK::List:o<<"(list";for(auto&x:e->items)o<<' '<<sexpr(x.get());o<<")";break;case EK::Map:o<<"(map";for(auto&x:e->items)o<<' '<<sexpr(x.get());o<<")";break;case EK::Index:o<<"(index "<<sexpr(e->a.get())<<' '<<sexpr(e->b.get())<<")";break;case EK::Slice:o<<"(slice "<<sexpr(e->a.get())<<' '<<sexpr(e->b.get())<<")";break;case EK::Lambda:o<<"(lambda-raw \""<<e->text<<"\")";break;case EK::With:o<<"(with)";break;}return o.str();}
static std::string stmtName(SK k){switch(k){case SK::Block:return "block";case SK::Let:return "let";case SK::Fixed:return "fixed";case SK::Assign:return "assign";case SK::Announce:return "announce";case SK::Give:return "give";case SK::Expr:return "expr";case SK::When:return "when";case SK::RepeatCount:return "repeat-count";case SK::RepeatWhile:return "repeat-while";case SK::RepeatEach:return "repeat-each";case SK::RepeatRange:return "repeat-range";case SK::Stop:return "stop";case SK::Skip:return "skip";case SK::Fail:return "fail";case SK::Attempt:return "attempt";}return "stmt";}
static void printStmtAst(std::ostream& o,const Stmt* s,int ind=0){if(!s)return;std::string pad(ind,' ');o<<pad<<'('<<stmtName(s->kind);if(!s->name.empty())o<<' '<<s->name;if(!s->type.empty())o<<":"<<s->type;if(s->expr)o<<' '<<sexpr(s->expr.get());if(s->expr2)o<<' '<<sexpr(s->expr2.get());if(!s->statements.empty()||s->body||s->otherwiseBody||!s->branches.empty()){o<<"\n";for(auto& x:s->statements)printStmtAst(o,x.get(),ind+2);if(s->body)printStmtAst(o,s->body.get(),ind+2);for(auto&b:s->branches){o<<pad<<"  (otherwise-when "<<sexpr(b.condition.get())<<"\n";printStmtAst(o,b.body.get(),ind+4);o<<pad<<"  )\n";}if(s->otherwiseBody)printStmtAst(o,s->otherwiseBody.get(),ind+2);o<<pad;}o<<")\n";}
static void printAst(const Program&p,std::ostream&o){o<<"(module ANIMA-"<<p.version<<"\n";if(!p.projectName.empty())o<<"  (project \""<<p.projectName<<"\")\n";for(auto&x:p.imports)o<<"  (use "<<x<<")\n";for(auto&f:p.functions){o<<"  (define "<<f.name<<" (params";for(auto&a:f.params)o<<" ("<<a.name<<":"<<a.type<<")";o<<")"<<(f.hasReturn?" -> "+f.returnType:"")<<"\n";printStmtAst(o,f.body.get(),4);o<<"  )\n";}for(auto&g:p.globals)printStmtAst(o,g.get(),2);if(p.start){o<<"  (start\n";printStmtAst(o,p.start.get(),4);o<<"  )\n";}for(auto&t:p.tests){o<<"  (test \""<<t.name<<"\"\n";printStmtAst(o,t.body.get(),4);o<<"  )\n";}for(auto&r:p.raw)o<<"  ("<<r.kind<<' '<<r.name<<" <raw declaration>)\n";o<<")\n";}

struct RangeValue { std::int64_t start=0,end=0;bool inclusive=false;bool exhausted=false; };
struct ListValue;
struct MapValue;
struct RecordValue;
struct EnumValue { std::string typeName, caseName; std::int64_t number=0; };
struct FunctionValue { FunctionDecl* function=nullptr; };
using Value=std::variant<std::monostate,std::int64_t,double,bool,std::string,std::shared_ptr<ListValue>,std::shared_ptr<MapValue>,std::shared_ptr<RecordValue>,EnumValue,RangeValue,FunctionValue>;
struct ListValue { std::vector<Value> values; };
struct MapValue { std::vector<std::pair<Value,Value>> entries; };
struct RecordValue { std::string typeName; std::vector<std::pair<std::string,Value>> fields; };
static std::string numberText(double x){if(std::isnan(x))return "math.NAN";if(std::isinf(x))return x>0?"math.INF":"-math.INF";std::ostringstream o;o<<std::setprecision(17)<<x;std::string s=o.str();if(s.find('.')==std::string::npos&&s.find('e')==std::string::npos&&s.find('E')==std::string::npos)s+=".0";return s;}
static std::string valueType(const Value&v){if(std::holds_alternative<std::monostate>(v))return "maybe value";if(std::holds_alternative<std::int64_t>(v))return "whole";if(std::holds_alternative<double>(v))return "real";if(std::holds_alternative<bool>(v))return "truth";if(std::holds_alternative<std::string>(v))return "text";if(std::holds_alternative<std::shared_ptr<ListValue>>(v))return "list";if(std::holds_alternative<std::shared_ptr<MapValue>>(v))return "map";if(auto r=std::get_if<std::shared_ptr<RecordValue>>(&v))return *r?(*r)->typeName:"record";if(auto e=std::get_if<EnumValue>(&v))return e->typeName;if(std::holds_alternative<RangeValue>(v))return "range";return "function";}
static std::string display(const Value&v){if(std::holds_alternative<std::monostate>(v))return "none";if(auto p=std::get_if<std::int64_t>(&v))return std::to_string(*p);if(auto p=std::get_if<double>(&v))return numberText(*p);if(auto p=std::get_if<bool>(&v))return *p?"true":"false";if(auto p=std::get_if<std::string>(&v))return *p;if(auto p=std::get_if<RangeValue>(&v))return std::to_string(p->start)+(p->inclusive?"..=":"..")+std::to_string(p->end);if(auto p=std::get_if<std::shared_ptr<ListValue>>(&v)){std::string o="[";for(std::size_t i=0;i<(*p)->values.size();++i){if(i)o+=", ";o+=display((*p)->values[i]);}return o+"]";}if(auto p=std::get_if<std::shared_ptr<MapValue>>(&v)){std::string o="[";for(std::size_t i=0;i<(*p)->entries.size();++i){if(i)o+=", ";o+=display((*p)->entries[i].first)+": "+display((*p)->entries[i].second);}return o+"]";}if(auto p=std::get_if<std::shared_ptr<RecordValue>>(&v)){if(!*p)return "<null record>";std::string o=(*p)->typeName+" { ";for(std::size_t i=0;i<(*p)->fields.size();++i){if(i)o+=", ";o+=(*p)->fields[i].first+": "+display((*p)->fields[i].second);}return o+" }";}if(auto e=std::get_if<EnumValue>(&v))return e->typeName+"."+e->caseName;return "<function>";}
static bool truthOf(const Value&v){if(auto p=std::get_if<bool>(&v))return *p;throw std::runtime_error("condition must be truth (true or false), received "+valueType(v));}
static double numeric(const Value&v){if(auto p=std::get_if<std::int64_t>(&v))return static_cast<double>(*p);if(auto p=std::get_if<double>(&v))return *p;throw std::runtime_error("expected numeric value, found "+valueType(v));}
static bool numericType(const Value&v){return std::holds_alternative<std::int64_t>(v)||std::holds_alternative<double>(v);}

struct Slot {Value value;std::string type;bool fixed=false;};
struct Env {Env* parent=nullptr;std::unordered_map<std::string,Slot> values;explicit Env(Env*p=nullptr):parent(p){};Slot* find(const std::string&n){auto i=values.find(n);if(i!=values.end())return &i->second;return parent?parent->find(n):nullptr;}Slot* local(const std::string&n){auto i=values.find(n);return i==values.end()?nullptr:&i->second;}};
struct ReturnSignal{Value value;};struct BreakSignal{};struct ContinueSignal{};

class Runtime {
public:
    Runtime(Program& p,bool compat=false):program_(p),compat_(compat){for(auto&f:p.functions)functions_[f.name]=&f;gpu_=anima02::QueryGpu();globals_.values["PI"]={3.14159265358979323846,"real",true};globals_.values["TAU"]={6.28318530717958647692,"real",true};globals_.values["E"]={2.71828182845904523536,"real",true};globals_.values["INF"]={std::numeric_limits<double>::infinity(),"real",true};globals_.values["NAN"]={std::numeric_limits<double>::quiet_NaN(),"real",true};}
    void run(){
        for(auto&g:program_.globals)exec(g.get(),globals_);
        if(!program_.start)throw std::runtime_error("entry module has no start block");
        if(compat_){
            // 0.2 used a single shared value scope for the whole program.
            // Preserve that observable behaviour only in the explicit migration mode.
            for(auto&stmt:program_.start->statements)exec(stmt.get(),globals_);
        }else{Env local(&globals_);exec(program_.start.get(),local);}
    }
    int runTests(){int pass=0,fail=0;for(auto&g:program_.globals)exec(g.get(),globals_);for(auto&t:program_.tests){try{Env local(&globals_);exec(t.body.get(),local);std::cout<<"PASS "<<t.name<<"\n";++pass;}catch(const std::exception&e){std::cerr<<"FAIL "<<t.name<<": "<<e.what()<<"\n";++fail;}}std::cout<<"\n"<<pass<<" passed, "<<fail<<" failed\n";return fail?1:0;}
    Value evalOnly(Expr*e,Env&env){return eval(e,env);}
    Env& globals(){return globals_;}
    Value invokeBuiltin(const std::string& name,const std::vector<Value>& args,std::size_t line=1) { return builtin(name,args,line); }
    Value applyBinary(const std::string& op,const Value& lhs,const Value& rhs) { return binary(op,lhs,rhs); }
private:
    Program& program_;bool compat_;Env globals_;std::unordered_map<std::string,FunctionDecl*> functions_;anima02::GpuStatus gpu_;
    Value eval(Expr*e,Env&env){if(!e)return {};switch(e->kind){case EK::Literal:return std::visit([](auto&&x)->Value{return x;},e->literal);case EK::Identifier:{if(auto*s=env.find(e->text))return s->value;auto f=functions_.find(e->text);if(f!=functions_.end())return FunctionValue{f->second};if(e->text=="PI")return 3.14159265358979323846;if(e->text=="TAU")return 6.28318530717958647692;if(e->text=="E")return 2.71828182845904523536;if(e->text=="true")return true;if(e->text=="false")return false;return e->text;}case EK::Unary:{Value v=eval(e->a.get(),env);if(e->text=="not")return !truthOf(v);if(e->text=="-"){if(auto p=std::get_if<std::int64_t>(&v)){if(*p==std::numeric_limits<std::int64_t>::min())throw std::runtime_error("error[ANI-R002]: integer overflow");return -*p;}return -numeric(v);}break;}case EK::Binary:{if(e->text=="and"){auto a=eval(e->a.get(),env);return truthOf(a)&&truthOf(eval(e->b.get(),env));}if(e->text=="or"){auto a=eval(e->a.get(),env);return truthOf(a)||truthOf(eval(e->b.get(),env));}Value a=eval(e->a.get(),env),b=eval(e->b.get(),env);return binary(e->text,a,b);}case EK::List:{auto l=std::make_shared<ListValue>();for(auto&x:e->items)l->values.push_back(eval(x.get(),env));return l;}case EK::Map:throw std::runtime_error("map literals are parsed but their runtime storage is scheduled for milestone 0.3c");case EK::Index:{Value a=eval(e->a.get(),env),idx=eval(e->b.get(),env);auto ix=std::get_if<std::int64_t>(&idx);if(!ix||*ix<0)throw std::runtime_error("index must be a non-negative whole");if(auto l=std::get_if<std::shared_ptr<ListValue>>(&a)){if(static_cast<std::size_t>(*ix)>=(*l)->values.size())throw std::runtime_error("error[ANI-R003]: list index out of range");return (*l)->values[static_cast<std::size_t>(*ix)];}if(auto s=std::get_if<std::string>(&a)){if(static_cast<std::size_t>(*ix)>=s->size())throw std::runtime_error("error[ANI-R003]: text index out of range");return std::string(1,(*s)[static_cast<std::size_t>(*ix)]);}throw std::runtime_error("value is not indexable");}case EK::Slice:throw std::runtime_error("slices are parsed; full slice evaluation arrives in 0.3c");case EK::Member:{auto path=dotted(e);if(path=="math.PI"||path=="std.math.PI")return 3.14159265358979323846;if(path=="math.TAU")return 6.28318530717958647692;if(path=="math.E")return 2.71828182845904523536;return path;}case EK::Call:{std::string n=dotted(e->a.get());std::vector<Value> args;for(auto&x:e->items)args.push_back(eval(x.get(),env));if(auto fn=functions_.find(n);fn!=functions_.end())return callUser(*fn->second,e->items,e->names,args,env);return builtin(n,args,e->span.line);}case EK::Lambda:throw std::runtime_error("lambda values are parsed but will execute after closure support lands in 0.3b");case EK::With:break;}throw std::runtime_error("unsupported expression");}
    static std::string dotted(Expr*e){if(!e)return {};if(e->kind==EK::Identifier)return e->text;if(e->kind==EK::Member){auto p=dotted(e->a.get());return p.empty()?e->text:p+"."+e->text;}return {};}
    Value callUser(FunctionDecl&f,const std::vector<ExprPtr>&original,const std::vector<std::string>&names,const std::vector<Value>&values,Env&caller){(void)original;(void)caller;std::vector<std::optional<Value>> ordered(f.params.size());std::size_t pos=0;for(std::size_t i=0;i<values.size();++i){if(i<names.size()&&!names[i].empty()){auto it=std::find_if(f.params.begin(),f.params.end(),[&](auto&p){return p.name==names[i];});if(it==f.params.end())throw std::runtime_error("error[ANI-T025]: unknown named argument '"+names[i]+"'");std::size_t k=static_cast<std::size_t>(it-f.params.begin());if(ordered[k])throw std::runtime_error("error[ANI-T026]: argument supplied twice");ordered[k]=values[i];}else{while(pos<ordered.size()&&ordered[pos])++pos;if(pos>=ordered.size())throw std::runtime_error("too many arguments for "+f.name);ordered[pos++]=values[i];}}
        Env local(&globals_);for(std::size_t i=0;i<f.params.size();++i){if(ordered[i])local.values[f.params[i].name]={*ordered[i],f.params[i].type,false};else if(f.params[i].defaultValue)local.values[f.params[i].name]={eval(f.params[i].defaultValue.get(),local),f.params[i].type,false};else throw std::runtime_error("missing argument '"+f.params[i].name+"' for "+f.name);}try{exec(f.body.get(),local);}catch(const ReturnSignal&r){return r.value;}if(f.hasReturn)throw std::runtime_error("error[ANI-T008]: function '"+f.name+"' ended without give");return {};}
    static std::int64_t checkedInt(std::int64_t x, std::int64_t y, const std::string& op) {
        using Limits = std::numeric_limits<std::int64_t>;
        std::int64_t out = 0;
        bool overflow = false;
        if (op == "+") {
            overflow = (y > 0 && x > Limits::max() - y) || (y < 0 && x < Limits::min() - y);
            if (!overflow) out = x + y;
        } else if (op == "-") {
            overflow = (y < 0 && x > Limits::max() + y) || (y > 0 && x < Limits::min() + y);
            if (!overflow) out = x - y;
        } else {
            if (x == 0 || y == 0) out = 0;
            else if ((x == -1 && y == Limits::min()) || (y == -1 && x == Limits::min())) overflow = true;
            else if (x > 0) overflow = y > 0 ? x > Limits::max() / y : y < Limits::min() / x;
            else overflow = y > 0 ? x < Limits::min() / y : y < Limits::max() / x;
            if (!overflow) out = x * y;
        }
        if (overflow) throw std::runtime_error("error[ANI-R002]: integer overflow");
        return out;
    }
    Value binary(const std::string&op,const Value&a,const Value&b){if(op=="fallback")return std::holds_alternative<std::monostate>(a)?b:a;if(op=="=="||op=="!="){bool eq=false;if(a.index()==b.index()){if(auto p=std::get_if<std::int64_t>(&a))eq=*p==std::get<std::int64_t>(b);else if(auto p=std::get_if<double>(&a))eq=*p==std::get<double>(b);else if(auto p=std::get_if<bool>(&a))eq=*p==std::get<bool>(b);else if(auto p=std::get_if<std::string>(&a))eq=*p==std::get<std::string>(b);else if(std::holds_alternative<std::monostate>(a))eq=true;else if(auto p=std::get_if<EnumValue>(&a)){auto q=std::get<EnumValue>(b);eq=p->typeName==q.typeName&&p->caseName==q.caseName;}else if(std::holds_alternative<std::shared_ptr<ListValue>>(a))eq=display(a)==display(b);}else if(numericType(a)&&numericType(b))eq=numeric(a)==numeric(b);return op=="=="?eq:!eq;}
        if(op=="<"||op=="<="||op==">"||op==">="){if(auto x=std::get_if<std::string>(&a)){auto y=std::get_if<std::string>(&b);if(!y)throw std::runtime_error("comparison type mismatch");if(op=="<")return *x<*y;if(op=="<=")return *x<=*y;if(op==">")return *x>*y;return *x>=*y;}double x=numeric(a),y=numeric(b);if(op=="<")return x<y;if(op=="<=")return x<=y;if(op==">")return x>y;return x>=y;}
        if(op==".."||op=="..="){auto x=std::get_if<std::int64_t>(&a),y=std::get_if<std::int64_t>(&b);if(!x||!y)throw std::runtime_error("range bounds must be whole values");return RangeValue{*x,*y,op=="..="};}
        if(op=="+"&&(std::holds_alternative<std::string>(a)||std::holds_alternative<std::string>(b)))return display(a)+display(b);
        if(op=="+"||op=="-"||op=="*"||op=="/"||op=="%"){if(std::holds_alternative<std::int64_t>(a)&&std::holds_alternative<std::int64_t>(b)){auto x=std::get<std::int64_t>(a),y=std::get<std::int64_t>(b);if((op=="/"||op=="%")&&y==0)throw std::runtime_error("error[ANI-R001]: division by zero");if(op=="/"){if(x==std::numeric_limits<std::int64_t>::min()&&y==-1)throw std::runtime_error("error[ANI-R002]: integer overflow");return x/y;}if(op=="%"){if(x==std::numeric_limits<std::int64_t>::min()&&y==-1)return std::int64_t(0);return x%y;}return checkedInt(x,y,op);}double x=numeric(a),y=numeric(b);if(op=="+")return x+y;if(op=="-")return x-y;if(op=="*")return x*y;if(op=="/")return x/y;return std::fmod(x,y);}
        throw std::runtime_error("unsupported operator '"+op+"'");}
    Value builtin(const std::string&raw,const std::vector<Value>&a,std::size_t line){auto n=raw;if(n.rfind("std.",0)==0)n=n.substr(4);if(n=="math.PI")return 3.14159265358979323846;if(n=="math.TAU")return 6.28318530717958647692;if(n=="math.E")return 2.71828182845904523536;
        if(n=="host.platform"){
#ifdef _WIN32
            return std::string("windows");
#else
            return std::string("linux");
#endif
        }
        if(n=="cpu.logical_cores"||n=="cpu.cores")return static_cast<std::int64_t>(std::max(1u,std::thread::hardware_concurrency()));
        if(n=="cpu.parallel_sum"){if(a.empty())throw std::runtime_error("cpu.parallel_sum expects N");auto nn=std::get_if<std::int64_t>(&a[0]);if(!nn||*nn<0||*nn>1000000000)throw std::runtime_error("cpu.parallel_sum expects whole from 0 to 1,000,000,000");std::uint64_t N=static_cast<std::uint64_t>(*nn);unsigned workers=std::max(1u,std::thread::hardware_concurrency());workers=static_cast<unsigned>(std::min<std::uint64_t>(workers,std::max<std::uint64_t>(N,1)));std::vector<std::uint64_t> part(workers);std::vector<std::thread> ts;for(unsigned w=0;w<workers;++w){std::uint64_t lo=N*w/workers+1,hi=N*(w+1)/workers;ts.emplace_back([&,w,lo,hi]{std::uint64_t sum=0;for(auto i=lo;i<=hi&&i>0;++i)sum+=i;part[w]=sum;});}for(auto&t:ts)t.join();std::uint64_t sum=std::accumulate(part.begin(),part.end(),std::uint64_t(0));return static_cast<std::int64_t>(sum);}
        if(n=="gpu.available") return gpu_.available;
        if(n=="gpu.adapter") return gpu_.adapter;
        if(n=="gpu.backend") return gpu_.backend;
        if(n=="gpu.vram_mb") return static_cast<std::int64_t>(gpu_.dedicatedMemoryMB);
        if(n=="gpu.dxr_supported") return gpu_.dxrSupported;
        if(n=="gpu.squares"){if(a.empty())throw std::runtime_error("gpu.squares expects count");auto nn=std::get_if<std::int64_t>(&a[0]);if(!nn||*nn<1)throw std::runtime_error("gpu.squares expects positive whole count");auto nums=anima02::ComputeSquaresOnGpu(static_cast<std::size_t>(*nn));auto l=std::make_shared<ListValue>();for(double x:nums)l->values.emplace_back(x);return l;}
        if(n=="to_text") return a.empty()?std::string(""):display(a[0]);
        if(n=="type_name") return a.empty()?std::string("none"):valueType(a[0]);
        if(n=="to_whole"){if(a.empty())throw std::runtime_error("to_whole expects value");return static_cast<std::int64_t>(numeric(a[0]));}
        if(n=="to_real"){if(a.empty())throw std::runtime_error("to_real expects value");return numeric(a[0]);}
        auto argn=[&](std::size_t i)->double{if(i>=a.size())throw std::runtime_error(n+" expects more arguments");return numeric(a[i]);};
        if(n.rfind("math.",0)==0){auto m=n.substr(5);if(m=="sqrt")return std::sqrt(argn(0));if(m=="pow")return std::pow(argn(0),argn(1));if(m=="abs")return std::holds_alternative<std::int64_t>(a.at(0))?Value(std::int64_t(std::llabs(std::get<std::int64_t>(a.at(0))))):Value(std::fabs(argn(0)));if(m=="min")return std::min(argn(0),argn(1));if(m=="max")return std::max(argn(0),argn(1));if(m=="clamp")return std::clamp(argn(0),argn(1),argn(2));if(m=="floor")return static_cast<std::int64_t>(std::floor(argn(0)));if(m=="ceil")return static_cast<std::int64_t>(std::ceil(argn(0)));if(m=="round")return static_cast<std::int64_t>(std::round(argn(0)));if(m=="trunc")return static_cast<std::int64_t>(std::trunc(argn(0)));if(m=="sin")return std::sin(argn(0));if(m=="cos")return std::cos(argn(0));if(m=="tan")return std::tan(argn(0));if(m=="exp")return std::exp(argn(0));if(m=="ln")return std::log(argn(0));if(m=="log10")return std::log10(argn(0));if(m=="radians")return argn(0)*3.14159265358979323846/180.0;if(m=="degrees")return argn(0)*180.0/3.14159265358979323846;}
        if(n=="expect"||n=="assert"){if(a.empty()||!truthOf(a[0]))throw std::runtime_error(a.size()>1?display(a[1]):"test assertion failed");return {};}
        if(n=="expect_equal"){if(a.size()<2)throw std::runtime_error("expect_equal expects two arguments");if(!truthOf(binary("==",a[0],a[1])))throw std::runtime_error("expected "+display(a[1])+", received "+display(a[0]));return {};}
        if(n=="list.length"&&a.size())return static_cast<std::int64_t>(std::get<std::shared_ptr<ListValue>>(a[0])->values.size());
        throw std::runtime_error("error[ANI-R011]: native function '"+raw+"' is not registered in this 0.3bc preview (line "+std::to_string(line)+")");}
    Value evalInterpolated(const std::string&str,Env&env){std::string out;for(std::size_t i=0;i<str.size();){if(str[i]=='\x01'){out+='{';++i;continue;}if(str[i]=='\x02'){out+='}';++i;continue;}if(str[i]=='{' ){std::size_t j=i+1;int depth=1;while(j<str.size()&&depth){if(str[j]=='{')++depth;else if(str[j]=='}')--depth;if(depth)++j;}if(depth==0){std::string inner=str.substr(i+1,j-i-1);Diagnostics d("<interpolation>",inner);Parser p(inner,"<interpolation>",d,compat_);auto ex=p.parseExpressionOnly();if(d.hasErrors())throw std::runtime_error("invalid text interpolation expression");out+=display(eval(ex.get(),env));i=j+1;continue;}}out+=str[i++];}return out;}
    void exec(Stmt*s,Env&env){if(!s)return;switch(s->kind){case SK::Block:{Env local(&env);for(auto&x:s->statements)exec(x.get(),local);break;}case SK::Let:case SK::Fixed:{Value v=eval(s->expr.get(),env);if(compat_){if(auto*old=env.local(s->name)){old->value=v;break;}}if(env.local(s->name))throw std::runtime_error("error[ANI-T009]: '"+s->name+"' is already declared in this scope");env.values[s->name]={v,s->type.empty()?valueType(v):s->type,s->kind==SK::Fixed};break;}case SK::Assign:{if(s->name.empty())throw std::runtime_error("member/index assignment is parsed but not yet executable in the 0.3bc VM; object semantics are scheduled for 0.3d");auto slot=env.find(s->name);if(!slot)throw std::runtime_error("error[ANI-T001]: undefined name '"+s->name+"'");if(slot->fixed)throw std::runtime_error("error[ANI-T019]: cannot assign to fixed constant '"+s->name+"'");Value rhs=eval(s->expr2.get(),env);if(s->op!="<-"){std::string op=s->op.substr(0,1);rhs=binary(op,slot->value,rhs);}slot->value=rhs;break;}case SK::Announce:{Value v=eval(s->expr.get(),env);if(auto text=std::get_if<std::string>(&v))std::cout<<display(evalInterpolated(*text,env))<<"\n";else std::cout<<display(v)<<"\n";break;}case SK::Give:throw ReturnSignal{s->expr?eval(s->expr.get(),env):Value{}};case SK::Expr:(void)eval(s->expr.get(),env);break;case SK::When:if(truthOf(eval(s->expr.get(),env)))exec(s->body.get(),env);else{bool hit=false;for(auto&b:s->branches)if(truthOf(eval(b.condition.get(),env))){exec(b.body.get(),env);hit=true;break;}if(!hit&&s->otherwiseBody)exec(s->otherwiseBody.get(),env);}break;case SK::RepeatCount:{Value n=eval(s->expr.get(),env);auto p=std::get_if<std::int64_t>(&n);if(!p)throw std::runtime_error("repeat count must be whole");if(*p>10000000)throw std::runtime_error("error[ANI-R007]: loop iteration limit exceeded");for(std::int64_t i=0;i<*p;++i){try{exec(s->body.get(),env);}catch(const ContinueSignal&){}catch(const BreakSignal&){break;}}break;}case SK::RepeatWhile:{std::uint64_t n=0;while(truthOf(eval(s->expr.get(),env))){if(++n>10000000)throw std::runtime_error("error[ANI-R007]: loop iteration limit exceeded");try{exec(s->body.get(),env);}catch(const ContinueSignal&){}catch(const BreakSignal&){break;}}break;}case SK::RepeatRange:{Value r=eval(s->expr.get(),env);auto rg=std::get_if<RangeValue>(&r);if(!rg)throw std::runtime_error("repeat i in expects a range expression");if(std::llabs(rg->end-rg->start)>10000000)throw std::runtime_error("error[ANI-R007]: range iteration limit exceeded");for(std::int64_t i=rg->start;rg->inclusive?i<=rg->end:i<rg->end;++i){Env local(&env);local.values[s->name]={i,"whole",true};try{exec(s->body.get(),local);}catch(const ContinueSignal&){}catch(const BreakSignal&){break;}if(i==std::numeric_limits<std::int64_t>::max())break;}break;}case SK::RepeatEach:{Value v=eval(s->expr.get(),env);if(auto list=std::get_if<std::shared_ptr<ListValue>>(&v)){for(std::size_t i=0;i<(*list)->values.size();++i){Env local(&env);local.values[s->name]={(*list)->values[i],valueType((*list)->values[i]),true};if(!s->name2.empty())local.values[s->name2]={static_cast<std::int64_t>(i),"whole",true};try{exec(s->body.get(),local);}catch(const ContinueSignal&){}catch(const BreakSignal&){break;}}}else if(auto text=std::get_if<std::string>(&v)){for(char c:*text){Env local(&env);local.values[s->name]={std::string(1,c),"text",true};try{exec(s->body.get(),local);}catch(const ContinueSignal&){}catch(const BreakSignal&){break;}}}else throw std::runtime_error("repeat each source must be a list, map or text in the supported 0.3bc VM subset");break;}case SK::Stop:throw BreakSignal{};case SK::Skip:throw ContinueSignal{};case SK::Fail:throw std::runtime_error("error[ANI-R010]: "+display(eval(s->expr.get(),env)));case SK::Attempt:{try{exec(s->body.get(),env);}catch(const std::exception&e){if(s->rescues.empty())throw;Env local(&env);local.values[s->rescues[0].first]={std::string(e.what()),"error",true};exec(s->rescues[0].second.get(),local);}if(s->otherwiseBody)exec(s->otherwiseBody.get(),env);break;}}}
};

struct VarInfo { std::string type; bool fixed=false; Span span; };

class Checker {
public:
    Checker(Program& p, Diagnostics& d, bool compat=false) : p_(p), d_(d), compat_(compat) {
        for (auto& f : p.functions) funcs_[f.name] = &f;
        for (const auto& rec : p.records) records_[rec.name] = &rec;
        for (const auto& en : p.enums) enums_[en.name] = &en;
        for (const auto& imp : p.imports) {
            auto at=imp.find(" as "); std::string base=at==std::string::npos?imp:imp.substr(0,at);
            auto dot=base.rfind('.'); aliases_.insert(dot==std::string::npos?base:base.substr(dot+1));
        }
    }

    void check() {
        declaredScopes_.clear(); declaredScopes_.emplace_back();
        for (auto& f : p_.functions) {
            if (declaredScopes_.back().contains(f.name)) d_.error("ANI-T009","'"+f.name+"' is already declared in this module",f.span);
            declaredScopes_.back().insert(f.name);
            for (const auto& arg : f.params) if (arg.type.empty())
                d_.error("ANI-T001","parameter '"+arg.name+"' requires a type annotation",arg.span,"write name: type");
        }
        for (auto& raw : p_.raw) {
            if(raw.kind.find("class")!=std::string::npos||raw.kind.find("record")!=std::string::npos||raw.kind.find("trait")!=std::string::npos||raw.kind.find("enum")!=std::string::npos) rawTypes_.insert(raw.name);
        }
        for (const auto& rec : p_.records) {
            std::set<std::string> fieldNames;
            for (const auto& field : rec.fields) {
                if (!fieldNames.insert(field.name).second) d_.error("ANI-T009", "record '"+rec.name+"' repeats field '"+field.name+"'", field.span);
                if (!field.defaultSource.empty() && !field.defaultValue && field.defaultEnum.empty()) d_.error("ANI-C008", "record field default for '"+field.name+"' is not supported by this preview", field.span, "use a literal or an enum case such as ActorState.idle");
                if (field.defaultValue && !compatible(field.type, typeOfLiteral(*field.defaultValue))) d_.error("ANI-T002", "default for field '"+field.name+"' expects "+field.type+", found "+typeOfLiteral(*field.defaultValue), field.span);
                if (!field.defaultEnum.empty()) {
                    if (field.type != field.defaultEnum) d_.error("ANI-T002", "enum default for field '"+field.name+"' expects "+field.type+", found "+field.defaultEnum, field.span);
                    const auto en=std::find_if(p_.enums.begin(),p_.enums.end(),[&](const EnumDecl& x){return x.name==field.defaultEnum;});
                    if(en==p_.enums.end() || std::none_of(en->cases.begin(),en->cases.end(),[&](const EnumCaseDecl& x){return x.name==field.defaultEnumCase;})) d_.error("ANI-T002", "unknown enum default '"+field.defaultEnum+"."+field.defaultEnumCase+"'", field.span);
                }
            }
        }
        for (const auto& en : p_.enums) {
            std::set<std::string> caseNames;
            for (const auto& item : en.cases) if (!caseNames.insert(item.name).second)
                d_.error("ANI-T009", "enum '"+en.name+"' repeats case '"+item.name+"'", en.span);
            if (en.cases.empty()) d_.error("ANI-T004", "enum '"+en.name+"' must declare at least one case", en.span);
        }
        for (const auto& alias : aliases_) {
            if (declaredScopes_.back().contains(alias)) d_.error("ANI-T009","module alias '"+alias+"' conflicts with another declaration",{});
            declaredScopes_.back().insert(alias);
        }
        // Seed typed global names first so forward references can be resolved. Initialisation
        // order is still source order; static use-before-init analysis is a later checker pass.
        for (const auto& g : p_.globals) if(g&&(g->kind==SK::Let||g->kind==SK::Fixed))
            scopeGlobal_[g->name]={g->type.empty()?"unknown":g->type,g->kind==SK::Fixed,g->span};
        for (auto& g : p_.globals) checkStmt(g.get(),scopeGlobal_,"");
        for (auto& f : p_.functions) {
            std::unordered_map<std::string,VarInfo> scope;
            declaredScopes_.emplace_back();
            for (auto& arg : f.params) {
                if (declaredScopes_.back().contains(arg.name)) d_.error("ANI-T009","parameter '"+arg.name+"' is repeated",arg.span);
                declaredScopes_.back().insert(arg.name); scope[arg.name]={arg.type,false,arg.span};
                if (arg.defaultValue) {
                    auto dt=infer(arg.defaultValue.get(),scope);
                    if(!compatible(arg.type,dt))d_.error("ANI-T002","default for '"+arg.name+"' expects "+arg.type+", found "+dt,arg.span);
                }
            }
            checkStmt(f.body.get(),scope,f.hasReturn?f.returnType:"");
            if(f.hasReturn&&!alwaysReturns(f.body.get()))d_.error("ANI-T008","function '"+f.name+"' does not give a value on every path",f.span,"return a value on every branch or fail explicitly");
            declaredScopes_.pop_back();
        }
        if(p_.start){std::unordered_map<std::string,VarInfo> s;checkStmt(p_.start.get(),s,"");}
        for(auto& t:p_.tests){std::unordered_map<std::string,VarInfo>s;checkStmt(t.body.get(),s,"");}
    }

private:
    Program& p_; Diagnostics& d_; bool compat_;
    std::unordered_map<std::string,FunctionDecl*> funcs_;
    std::unordered_map<std::string,const RecordDecl*> records_;
    std::unordered_map<std::string,const EnumDecl*> enums_;
    std::unordered_map<std::string,VarInfo> scopeGlobal_;
    std::set<std::string> aliases_,rawTypes_;
    std::vector<std::set<std::string>> declaredScopes_;

    static bool numericType(const std::string& s){return s=="whole"||s=="real";}
    static bool compatible(std::string expected,std::string actual){
        if(expected.empty()||actual.empty()||expected=="unknown"||actual=="unknown"||expected=="value"||actual=="value")return true;
        if(expected==actual)return true;
        if(expected=="real"&&actual=="whole")return true;
        if(expected.rfind("maybe ",0)==0&&(expected.substr(6)==actual||actual=="maybe value"))return true;
        if(expected.rfind("list of ",0)==0&&actual=="list")return true;
        if(expected.rfind("map of ",0)==0&&actual=="map")return true;
        return false;
    }
    static std::string dotted(Expr* e){if(!e)return{};if(e->kind==EK::Identifier)return e->text;if(e->kind==EK::Member){auto p=dotted(e->a.get());return p.empty()?e->text:p+"."+e->text;}return{};}
    bool declaredInOuterScope(const std::string& n) const {
        if(declaredScopes_.size()<2)return false;
        for(std::size_t i=0;i+1<declaredScopes_.size();++i)if(declaredScopes_[i].contains(n))return true;
        return false;
    }
    void declareLocal(const std::string& n,const std::string& t,bool fixed,Span span,std::unordered_map<std::string,VarInfo>& scope){
        if(declaredScopes_.empty())declaredScopes_.emplace_back();
        if(declaredScopes_.back().contains(n)){d_.error("ANI-T009","'"+n+"' is already declared in this scope",span);return;}
        if(declaredInOuterScope(n)||scopeGlobal_.contains(n))d_.warn("ANI-W003","declaration of '"+n+"' shadows an outer name",span,"use a distinct name when practical");
        declaredScopes_.back().insert(n);scope[n]={t,fixed,span};
    }
    std::string infer(Expr* e,std::unordered_map<std::string,VarInfo>& scope){
        if(!e)return "none";
        switch(e->kind){
            case EK::Literal:return typeOfLiteral(e->literal);
            case EK::Identifier:{
                auto it=scope.find(e->text);if(it!=scope.end())return it->second.type;
                auto g=scopeGlobal_.find(e->text);if(g!=scopeGlobal_.end())return g->second.type;
                if(funcs_.contains(e->text))return "function";
                if(e->text=="PI"||e->text=="TAU"||e->text=="E"||e->text=="INF"||e->text=="NAN")return "real";
                if(aliases_.contains(e->text)||e->text=="std"||e->text=="math"||e->text=="cpu"||e->text=="gpu"||e->text=="host"||e->text=="time"||e->text=="random"||e->text=="ui")return "namespace";
                d_.error("ANI-T001","unknown name '"+e->text+"'",e->span,"declare it, import its module, or check the spelling");return "unknown";
            }
            case EK::Unary:{auto a=infer(e->a.get(),scope);if(e->text=="not"){if(a!="truth"&&a!="unknown")d_.error("ANI-T006","not requires truth, found '"+a+"'",e->span);return "truth";}if(!numericType(a)&&a!="unknown")d_.error("ANI-T022","unary - is not defined for '"+a+"'",e->span);return a;}
            case EK::Binary:{
                auto a=infer(e->a.get(),scope),b=infer(e->b.get(),scope);const auto& op=e->text;
                if(op=="and"||op=="or"){if(a!="truth"&&a!="unknown")d_.error("ANI-T006",op+" requires truth operands",e->span);if(b!="truth"&&b!="unknown")d_.error("ANI-T006",op+" requires truth operands",e->span);return "truth";}
                if(op=="=="||op=="!="){if(!compatible(a,b)&&!(numericType(a)&&numericType(b)))d_.error("ANI-T022","operator "+op+" is not defined for "+a+" and "+b,e->span);return "truth";}
                if(op=="<"||op=="<="||op==">"||op==">="){if(!(numericType(a)&&numericType(b))&&!(a=="text"&&b=="text")&&a!="unknown"&&b!="unknown")d_.error("ANI-T022","comparison requires two numeric values or two text values",e->span);return "truth";}
                if(op=="is"){if(!(b=="maybe value"||b=="unknown"||rawTypes_.contains(b)||b=="none"||(e->b&&e->b->kind==EK::Unary)))d_.error("ANI-T022","right side of 'is' must be a type or none",e->span);return "truth";}
                if(op=="as")return "maybe "+b;
                if(op==".."||op=="..="){if(a!="whole"||b!="whole")d_.error("ANI-T002","range bounds must be whole",e->span);return "range";}
                if(op=="fallback"){if(a.rfind("maybe ",0)!=0&&a!="unknown")d_.error("ANI-T022","fallback requires a maybe value on the left",e->span);return a.rfind("maybe ",0)==0?a.substr(6):b;}
                if(op=="+"&&(a=="text"||b=="text"))return "text";
                if(!numericType(a)&&a!="unknown")d_.error("ANI-T022","operator "+op+" is not defined for '"+a+"'",e->span);
                if(!numericType(b)&&b!="unknown")d_.error("ANI-T022","operator "+op+" is not defined for '"+b+"'",e->span);
                return a=="real"||b=="real"?"real":"whole";
            }
            case EK::List:{std::string elem="unknown";for(auto& x:e->items){auto t=infer(x.get(),scope);if(elem=="unknown")elem=t;else if(!compatible(elem,t))d_.error("ANI-T002","list values must have compatible types",x->span);}return "list of "+elem;}
            case EK::Map:{std::string kt="unknown",vt="unknown";for(std::size_t i=0;i<e->items.size();++i){auto t=infer(e->items[i].get(),scope);if(i%2==0){if(kt=="unknown")kt=t;else if(!compatible(kt,t))d_.error("ANI-T002","map keys must have one compatible type",e->items[i]->span);if(t!="whole"&&t!="text"&&t!="truth"&&t!="unknown"&&!enums_.contains(t))d_.error("ANI-T002","map keys must be whole, text, truth or enum",e->items[i]->span);}else{if(vt=="unknown")vt=t;else if(!compatible(vt,t))d_.error("ANI-T002","map values must have one compatible type",e->items[i]->span);}}return e->items.empty()?"map":"map of "+kt+" to "+vt;}
            case EK::Member:{auto n=dotted(e);if(n=="math.PI"||n=="math.TAU"||n=="math.E"||n=="std.math.PI")return "real";for(const auto& [enumName,en] : enums_){if(n.rfind(enumName+".",0)==0){auto suffix=n.substr(enumName.size()+1);auto dot=suffix.find('.');auto caseName=dot==std::string::npos?suffix:suffix.substr(0,dot);auto c=std::find_if(en->cases.begin(),en->cases.end(),[&](const EnumCaseDecl& x){return x.name==caseName;});if(c!=en->cases.end()){if(dot==std::string::npos)return enumName;const auto tail=suffix.substr(dot+1);if(tail=="name")return "text";if(tail=="value")return "whole";d_.error("ANI-T020","enum value has no member '"+tail+"'",e->span);return "unknown";}}}auto owner=infer(e->a.get(),scope);if(enums_.contains(owner)){if(e->text=="name")return "text";if(e->text=="value")return "whole";d_.error("ANI-T020","enum '"+owner+"' has no member '"+e->text+"'",e->span);return "unknown";}auto rec=records_.find(owner);if(rec!=records_.end()){auto f=std::find_if(rec->second->fields.begin(),rec->second->fields.end(),[&](const RecordFieldDecl& x){return x.name==e->text;});if(f==rec->second->fields.end()){d_.error("ANI-T020","record '"+owner+"' has no field '"+e->text+"'",e->span);return "unknown";}return f->type;}return "unknown";}
            case EK::Call:{
                const std::string n=dotted(e->a.get());
                for(const auto& [enumName,en] : enums_){if(n==enumName+".all"){if(!e->items.empty())d_.error("ANI-T003",n+" expects no arguments",e->span);return "list of "+enumName;}if(n==enumName+".from_name"){if(e->items.size()!=1)d_.error("ANI-T003",n+" expects one text argument",e->span);else{auto arg=infer(e->items[0].get(),scope);if(arg!="text"&&arg!="unknown")d_.error("ANI-T002",n+" expects text",e->items[0]->span);}return "maybe "+enumName;}(void)en;}
                if(auto record=records_.find(n);record!=records_.end()){
                    const auto& fields=record->second->fields;std::vector<bool> supplied(fields.size(),false);std::size_t positional=0;
                    for(std::size_t i=0;i<e->items.size();++i){std::size_t fi=0;const std::string arg=i<e->names.size()?e->names[i]:std::string{};if(!arg.empty()){auto found=std::find_if(fields.begin(),fields.end(),[&](const RecordFieldDecl& f){return f.name==arg;});if(found==fields.end()){d_.error("ANI-T025","unknown record field argument '"+arg+"' for "+n,e->items[i]->span);infer(e->items[i].get(),scope);continue;}fi=static_cast<std::size_t>(found-fields.begin());}else{while(positional<supplied.size()&&supplied[positional])++positional;if(positional>=fields.size()){d_.error("ANI-T003","too many constructor arguments for '"+n+"'",e->span);infer(e->items[i].get(),scope);continue;}fi=positional++;}auto got=infer(e->items[i].get(),scope);if(supplied[fi])d_.error("ANI-T026","record field '"+fields[fi].name+"' supplied more than once",e->items[i]->span);supplied[fi]=true;if(!compatible(fields[fi].type,got))d_.error("ANI-T002","record field '"+fields[fi].name+"' expects "+fields[fi].type+", found "+got,e->items[i]->span);}
                    for(std::size_t i=0;i<fields.size();++i)if(!supplied[i]&&fields[i].defaultSource.empty())d_.error("ANI-T004","missing required record field '"+fields[i].name+"' for '"+n+"'",e->span);
                    return n;
                }
                const auto builtinType=[&]()->std::string{
                    if(n=="host.platform"||n=="gpu.adapter"||n=="gpu.backend"||n=="to_text"||n=="type_name")return "text";
                    if(n=="cpu.logical_cores"||n=="cpu.cores"||n=="cpu.parallel_sum"||n=="gpu.vram_mb"||n=="to_whole"||n=="math.floor"||n=="math.ceil"||n=="math.round"||n=="math.trunc")return "whole";
                    if(n=="gpu.available"||n=="gpu.dxr_supported"||n=="assert"||n=="expect")return "truth";
                    if(n=="gpu.squares")return "list of real";
                    if(n=="expect_equal")return "none";
                    if(n.rfind("math.",0)==0)return "real";
                    return "";
                };
                const std::string bt=builtinType();
                if(!bt.empty()){
                    for(auto&x:e->items)infer(x.get(),scope);
                    auto argc=e->items.size();
                    if((n=="to_text"||n=="type_name"||n=="math.sqrt"||n=="math.abs"||n=="math.floor"||n=="math.ceil"||n=="math.round"||n=="math.trunc"||n=="expect"||n=="assert")&&argc!=1)d_.error("ANI-T003","'"+n+"' expects one argument",e->span);
                    if(n=="math.pow"&&argc!=2)d_.error("ANI-T003","'math.pow' expects two arguments",e->span);
                    return bt;
                }
                if(auto it=funcs_.find(n);it!=funcs_.end()){
                    auto* f=it->second;std::vector<bool> supplied(f->params.size(),false);std::size_t positional=0;
                    for(std::size_t i=0;i<e->items.size();++i){
                        std::size_t pi=0;const std::string nm=i<e->names.size()?e->names[i]:std::string{};
                        if(!nm.empty()){
                            auto found=std::find_if(f->params.begin(),f->params.end(),[&](const Param& p){return p.name==nm;});
                            if(found==f->params.end()){d_.error("ANI-T025","unknown named argument '"+nm+"' for '"+n+"'",e->items[i]->span);infer(e->items[i].get(),scope);continue;}
                            pi=static_cast<std::size_t>(found-f->params.begin());
                        }else{while(positional<supplied.size()&&supplied[positional])++positional;if(positional>=supplied.size()){d_.error("ANI-T003","too many arguments for '"+n+"'",e->span);infer(e->items[i].get(),scope);continue;}pi=positional++;}
                        auto got=infer(e->items[i].get(),scope);if(supplied[pi])d_.error("ANI-T026","argument '"+f->params[pi].name+"' supplied more than once",e->items[i]->span);supplied[pi]=true;
                        if(!compatible(f->params[pi].type,got))d_.error("ANI-T002","argument '"+f->params[pi].name+"' expects "+f->params[pi].type+", found "+got,e->items[i]->span);
                    }
                    for(std::size_t i=0;i<f->params.size();++i)if(!supplied[i]&&!f->params[i].defaultValue)d_.error("ANI-T004","missing argument '"+f->params[i].name+"' for '"+n+"'",e->span);
                    return f->hasReturn?f->returnType:"none";
                }
                for(auto&x:e->items)infer(x.get(),scope);
                if(n.empty())return "unknown";
                d_.error("ANI-M005","native function '"+n+"' is not registered in the 0.3 host API",e->span,"check the module name and native API table");return "unknown";
            }
            case EK::Index:{auto a=infer(e->a.get(),scope),b=infer(e->b.get(),scope);if(a.rfind("list of ",0)==0){if(b!="whole"&&b!="unknown")d_.error("ANI-T002","list index must be whole",e->b->span);return a.substr(8);}if(a=="text"){if(b!="whole"&&b!="unknown")d_.error("ANI-T002","text index must be whole",e->b->span);return "text";}if(a.rfind("map of ",0)==0){auto to=a.find(" to ",7);if(to!=std::string::npos){auto keyType=a.substr(7,to-7);if(!compatible(keyType,b))d_.error("ANI-T002","map index expects key type "+keyType+", found "+b,e->b->span);return a.substr(to+4);}}if(a=="map")return "unknown";if(a!="unknown")d_.error("ANI-T002","value of type "+a+" cannot be indexed",e->span);return "unknown";}
            case EK::Slice:{auto a=infer(e->a.get(),scope);if(e->b&&e->b->kind==EK::Binary){auto lo=infer(e->b->a.get(),scope),hi=infer(e->b->b.get(),scope);if(lo!="whole"||hi!="whole")d_.error("ANI-T002","slice bounds must be whole",e->span);}return a;}
            case EK::Lambda:d_.error("ANI-T035","lambda type checking is not implemented in the 0.3bc preview",e->span);return "function";
            case EK::With:{auto type=infer(e->a.get(),scope);auto rec=records_.find(type);if(rec==records_.end()){d_.error("ANI-T036","with requires a known record value, found '"+type+"'",e->span);for(auto&x:e->items)infer(x.get(),scope);return "unknown";}for(std::size_t i=0;i<e->items.size();++i){auto f=std::find_if(rec->second->fields.begin(),rec->second->fields.end(),[&](const RecordFieldDecl& x){return x.name==(i<e->names.size()?e->names[i]:std::string{});});auto got=infer(e->items[i].get(),scope);if(f==rec->second->fields.end())d_.error("ANI-T020","record '"+type+"' has no field '"+(i<e->names.size()?e->names[i]:std::string{})+"'",e->items[i]->span);else if(!compatible(f->type,got))d_.error("ANI-T002","record field '"+f->name+"' expects "+f->type+", found "+got,e->items[i]->span);}return type;}
        }
        return "unknown";
    }
    static bool alwaysReturns(const Stmt* s){
        if(!s)return false;
        if(s->kind==SK::Give||s->kind==SK::Fail)return true;
        if(s->kind==SK::Block){for(const auto& child:s->statements)if(alwaysReturns(child.get()))return true;return false;}
        if(s->kind==SK::When){if(!s->otherwiseBody||!alwaysReturns(s->body.get())||!alwaysReturns(s->otherwiseBody.get()))return false;for(const auto& branch:s->branches)if(!alwaysReturns(branch.body.get()))return false;return true;}
        return false;
    }
    void checkStmt(Stmt* s,std::unordered_map<std::string,VarInfo>& scope,const std::string& ret){
        if(!s)return;
        switch(s->kind){
            case SK::Block:{auto blockScope=scope;declaredScopes_.emplace_back();bool unreachable=false;for(auto& x:s->statements){if(unreachable)d_.warn("ANI-W002","unreachable statement",x->span);checkStmt(x.get(),blockScope,ret);if(alwaysReturns(x.get()))unreachable=true;}declaredScopes_.pop_back();break;}
            case SK::Let:case SK::Fixed:{auto t=infer(s->expr.get(),scope);const auto target=s->type.empty()?t:s->type;if(t.rfind("list of unknown",0)==0)d_.error("ANI-T016","empty or untyped list requires an explicit element type",s->span,"add a context such as let xs: list of whole <- []");if(!s->type.empty()&&!compatible(s->type,t))d_.error("ANI-T002","type mismatch: expected '"+s->type+"', found '"+t+"'",s->span,"use an explicit conversion such as to_whole(value)");declareLocal(s->name,target,s->kind==SK::Fixed,s->span,scope);break;}
            case SK::Assign:{if(s->name.empty()){if(s->expr&&s->expr->kind==EK::Member){auto ot=infer(s->expr->a.get(),scope);auto rit=records_.find(ot);auto got=infer(s->expr2.get(),scope);if(rit==records_.end())d_.error("ANI-T020","member assignment requires a record value",s->span);else{auto f=std::find_if(rit->second->fields.begin(),rit->second->fields.end(),[&](const RecordFieldDecl& x){return x.name==s->expr->text;});if(f==rit->second->fields.end())d_.error("ANI-T020","record '"+ot+"' has no field '"+s->expr->text+"'",s->span);else if(!compatible(f->type,got))d_.error("ANI-T002","field '"+f->name+"' expects "+f->type+", found "+got,s->span);}break;}infer(s->expr.get(),scope);infer(s->expr2.get(),scope);d_.error("ANI-T020","indexed assignment is not enabled in this preview",s->span);break;}auto it=scope.find(s->name);auto g=scopeGlobal_.find(s->name);VarInfo* v=it!=scope.end()?&it->second:g!=scopeGlobal_.end()?&g->second:nullptr;if(!v){d_.error("ANI-T001","unknown name '"+s->name+"'",s->span,"declare it with let before assigning");infer(s->expr2.get(),scope);break;}if(v->fixed)d_.error("ANI-T019","cannot assign to fixed constant '"+s->name+"'",s->span);auto t=infer(s->expr2.get(),scope);if(!compatible(v->type,t))d_.error("ANI-T002","type mismatch assigning "+t+" to "+v->type,s->span);if(s->op!="<-"&& !numericType(v->type) && !(s->op=="+="&&v->type=="text"))d_.error("ANI-T022","compound assignment is not defined for '"+v->type+"'",s->span);break;}
            case SK::Announce:infer(s->expr.get(),scope);break;
            case SK::Give:{auto t=s->expr?infer(s->expr.get(),scope):"none";if(!ret.empty()&&!compatible(ret,t))d_.error("ANI-T002","give expects "+ret+", found "+t,s->span);if(ret.empty()&&s->expr)d_.error("ANI-T005","function gives a value but has no return type annotation",s->span,"add -> type to the function signature");break;}
            case SK::Expr:infer(s->expr.get(),scope);break;
            case SK::When:{auto t=infer(s->expr.get(),scope);if(t!="truth"&&t!="unknown")d_.error("ANI-T006","when condition must be truth, found '"+t+"'",s->span);checkStmt(s->body.get(),scope,ret);for(auto&b:s->branches){auto bt=infer(b.condition.get(),scope);if(bt!="truth"&&bt!="unknown")d_.error("ANI-T006","otherwise when condition must be truth",b.condition->span);checkStmt(b.body.get(),scope,ret);}if(s->otherwiseBody)checkStmt(s->otherwiseBody.get(),scope,ret);break;}
            case SK::RepeatCount:{auto t=infer(s->expr.get(),scope);if(t!="whole"&&t!="unknown")d_.error("ANI-T002","repeat count must be whole",s->span);checkStmt(s->body.get(),scope,ret);break;}
            case SK::RepeatWhile:{auto t=infer(s->expr.get(),scope);if(t!="truth"&&t!="unknown")d_.error("ANI-T006","repeat while condition must be truth",s->span);checkStmt(s->body.get(),scope,ret);break;}
            case SK::RepeatRange:{auto t=infer(s->expr.get(),scope);if(t!="range"&&t!="unknown")d_.error("ANI-T002","repeat i in expects a range",s->span);auto old=scope.find(s->name);std::optional<VarInfo> save=old==scope.end()?std::nullopt:std::optional<VarInfo>(old->second);scope[s->name]={"whole",true,s->span};declaredScopes_.emplace_back();declaredScopes_.back().insert(s->name);checkStmt(s->body.get(),scope,ret);declaredScopes_.pop_back();if(save)scope[s->name]=*save;else scope.erase(s->name);break;}
            case SK::RepeatEach:{auto t=infer(s->expr.get(),scope);std::string item="unknown",second="unknown";if(t.rfind("list of ",0)==0){item=t.substr(8);if(!s->name2.empty())d_.error("ANI-T002","repeat each list iteration accepts one value name in 0.3; map iteration accepts key, value",s->span);}else if(t=="text"){item="text";if(!s->name2.empty())d_.error("ANI-T002","repeat each text iteration accepts one value name",s->span);}else if(t.rfind("map of ",0)==0){auto to=t.find(" to ",7);if(to!=std::string::npos){item=t.substr(7,to-7);second=t.substr(to+4);}}else if(t!="unknown")d_.error("ANI-T002","repeat each requires a list, map or text",s->span);auto s1=scope.find(s->name);std::optional<VarInfo> old1=s1==scope.end()?std::nullopt:std::optional<VarInfo>(s1->second);std::optional<VarInfo> old2;auto s2=scope.find(s->name2);if(!s->name2.empty()&&s2!=scope.end())old2=s2->second;scope[s->name]={item,true,s->span};if(!s->name2.empty())scope[s->name2]={second,true,s->span};declaredScopes_.emplace_back();declaredScopes_.back().insert(s->name);if(!s->name2.empty())declaredScopes_.back().insert(s->name2);checkStmt(s->body.get(),scope,ret);declaredScopes_.pop_back();if(old1)scope[s->name]=*old1;else scope.erase(s->name);if(!s->name2.empty()){if(old2)scope[s->name2]=*old2;else scope.erase(s->name2);}break;}
            case SK::Stop:case SK::Skip:if(s->expr)infer(s->expr.get(),scope);break;
            case SK::Fail:infer(s->expr.get(),scope);break;
            case SK::Attempt:{checkStmt(s->body.get(),scope,ret);for(auto&r:s->rescues){auto local=scope;local[r.first]={"error",false,s->span};checkStmt(r.second.get(),local,ret);}if(s->otherwiseBody)checkStmt(s->otherwiseBody.get(),scope,ret);break;}
        }
    }
};

#include "AnimaBytecode.h"

static std::string readFile(const fs::path& p){std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("cannot open file: "+p.string());std::ostringstream s;s<<in.rdbuf();if(s.str().size()>8*1024*1024)throw std::runtime_error("source exceeds the 8 MiB 0.3 limit");return s.str();}
static std::string dottedExprPath(const Expr* e) {
    if (!e) return {};
    if (e->kind==EK::Identifier) return e->text;
    if (e->kind==EK::Member) { auto left=dottedExprPath(e->a.get()); return left.empty()?e->text:left+"."+e->text; }
    return {};
}
static void qualifyModuleExpr(Expr* e,const std::string& alias,const std::set<std::string>& localFunctions,const std::set<std::string>& localTypes) {
    if(!e)return;
    qualifyModuleExpr(e->a.get(),alias,localFunctions,localTypes);
    qualifyModuleExpr(e->b.get(),alias,localFunctions,localTypes);
    for(auto& item:e->items)qualifyModuleExpr(item.get(),alias,localFunctions,localTypes);
    if(e->kind==EK::Call && e->a && e->a->kind==EK::Identifier && localFunctions.contains(e->a->text)) {
        auto base=std::make_unique<Expr>();base->kind=EK::Identifier;base->text=alias;base->span=e->a->span;
        auto member=std::make_unique<Expr>();member->kind=EK::Member;member->a=std::move(base);member->text=e->a->text;member->span=e->a->span;
        e->a=std::move(member);return;
    }
    if(e->kind==EK::Call && e->a && e->a->kind==EK::Identifier && localTypes.contains(e->a->text)) {
        auto base=std::make_unique<Expr>();base->kind=EK::Identifier;base->text=alias;base->span=e->a->span;
        auto member=std::make_unique<Expr>();member->kind=EK::Member;member->a=std::move(base);member->text=e->a->text;member->span=e->a->span;
        e->a=std::move(member);return;
    }
    if(e->kind==EK::Member) {
        // Type-qualified enum references such as Mood.calm and Mood.all() get a module prefix.
        const auto path=dottedExprPath(e);
        for(const auto& typeName:localTypes) if(path==typeName || path.rfind(typeName+".",0)==0) {
            Expr* root=e;while(root->kind==EK::Member&&root->a)root=root->a.get();
            if(root->kind==EK::Identifier&&root->text==typeName) {
                auto base=std::make_unique<Expr>();base->kind=EK::Identifier;base->text=alias;base->span=root->span;
                auto typeMember=std::make_unique<Expr>();typeMember->kind=EK::Member;typeMember->a=std::move(base);typeMember->text=typeName;typeMember->span=root->span;
                // Locate the owning unique_ptr from this expression chain and replace its leftmost identifier.
                std::function<bool(Expr*)> replaceRoot=[&](Expr* current)->bool {
                    if(!current)return false;
                    if(current->kind==EK::Member && current->a.get()==root) { current->a=std::move(typeMember); return true; }
                    return replaceRoot(current->a.get())||replaceRoot(current->b.get());
                };
                (void)replaceRoot(e);
            }
            break;
        }
    }
}
static void qualifyModuleStmt(Stmt* s,const std::string& alias,const std::set<std::string>& funcs,const std::set<std::string>& types) {
    if(!s)return;
    qualifyModuleExpr(s->expr.get(),alias,funcs,types);qualifyModuleExpr(s->expr2.get(),alias,funcs,types);
    for(auto& x:s->statements)qualifyModuleStmt(x.get(),alias,funcs,types);
    qualifyModuleStmt(s->body.get(),alias,funcs,types);qualifyModuleStmt(s->otherwiseBody.get(),alias,funcs,types);
    for(auto& b:s->branches){qualifyModuleExpr(b.condition.get(),alias,funcs,types);qualifyModuleStmt(b.body.get(),alias,funcs,types);}
    for(auto& r:s->rescues)qualifyModuleStmt(r.second.get(),alias,funcs,types);
}
static std::string qualifyTypeName(std::string type,const std::set<std::string>& names,const std::string& alias) {
    for(const auto& t:names) {
        if(type==t)return alias+"."+t;
        for(const std::string prefix:{"maybe ","weak ","list of "}) if(type.rfind(prefix,0)==0&&type.substr(prefix.size())==t)return prefix+alias+"."+t;
    }
    return type;
}
static bool loadLocalFunctionModules(Program& root,Diagnostics& diagnostics) {
    std::set<fs::path> loaded;
    for(const auto& importSpec:root.imports) {
        if(importSpec.find(" use ")!=std::string::npos) {
            const auto module=importSpec.substr(0,importSpec.find(" use "));
            if(module.rfind("std.",0)==0||module.rfind("engine.",0)==0)continue;
            diagnostics.error("ANI-M004","named 'from ... use' imports are not yet in this integrated build",{} ,"use `use module.path as alias` and call alias.function(...) in this preview");
            return false;
        }
        const auto asAt=importSpec.rfind(" as ");
        const std::string target=asAt==std::string::npos?importSpec:importSpec.substr(0,asAt);
        std::string alias=asAt==std::string::npos?std::string{}:importSpec.substr(asAt+4);
        if(target.rfind("std.",0)==0||target.rfind("engine.",0)==0)continue;
        fs::path relative(target);
        if(alias.empty()) {
            if(relative.has_extension())alias=relative.stem().string();
            else {auto dot=target.rfind('.');alias=dot==std::string::npos?target:target.substr(dot+1);}
        }
        fs::path resolved;
        std::vector<fs::path> candidates;
        if(relative.has_extension()||target.find('/')!=std::string::npos||target.find('\\')!=std::string::npos) {
            candidates.push_back(fs::path(root.file).parent_path()/relative);
        } else {
            std::string modulePath=target; std::replace(modulePath.begin(),modulePath.end(),'.', static_cast<char>(fs::path::preferred_separator));
            candidates.push_back(fs::path(root.file).parent_path()/(modulePath+".ani"));
            candidates.push_back(fs::path(root.file).parent_path()/"src"/(modulePath+".ani"));
        }
        for(auto candidate:candidates)if(fs::is_regular_file(candidate)){resolved=fs::weakly_canonical(candidate);break;}
        if(resolved.empty()) {diagnostics.error("ANI-M001","cannot resolve local module '"+target+"' from "+root.file,{},"place the module beside the importing file or use a relative path with .ani");return false;}
        if(!loaded.insert(resolved).second)continue;
        std::string source;try{source=readFile(resolved);}catch(const std::exception& e){diagnostics.error("ANI-M001","cannot read module '"+resolved.string()+"': "+e.what());return false;}
        Diagnostics md(resolved.string(),source);Parser parser(source,resolved.string(),md);Program module=parser.parse();
        if(md.hasErrors()) {
            for(const auto& d:md.list)if(d.severity==Severity::Error)diagnostics.error(d.code,"module "+resolved.string()+" line "+std::to_string(d.span.line)+":"+std::to_string(d.span.col)+": "+d.message,{},d.help);
            return false;
        }
        if(module.hasStart||!module.globals.empty()||!module.tests.empty()||!module.imports.empty()) {
            diagnostics.error("ANI-M008","preview module '"+resolved.string()+"' must contain declarations only (functions, records, enums)",{},"module globals, nested imports and start blocks are a later module-loader slice");return false;
        }
        std::set<std::string> localFunctions,localTypes;
        for(const auto& f:module.functions)localFunctions.insert(f.name);
        for(const auto& r:module.records)localTypes.insert(r.name);
        for(const auto& e:module.enums)localTypes.insert(e.name);
        for(auto& f:module.functions) {
            qualifyModuleStmt(f.body.get(),alias,localFunctions,localTypes);
            for(auto& arg:f.params)if(arg.defaultValue)qualifyModuleExpr(arg.defaultValue.get(),alias,localFunctions,localTypes);
            for(auto& arg:f.params)arg.type=qualifyTypeName(arg.type,localTypes,alias);
            f.returnType=qualifyTypeName(f.returnType,localTypes,alias);
            f.name=alias+"."+f.name;root.functions.push_back(std::move(f));
        }
        for(auto& r:module.records){r.name=alias+"."+r.name;for(auto& f:r.fields){f.type=qualifyTypeName(f.type,localTypes,alias);if(!f.defaultEnum.empty())f.defaultEnum=qualifyTypeName(f.defaultEnum,localTypes,alias);}root.records.push_back(std::move(r));}
        for(auto& e:module.enums){e.name=alias+"."+e.name;root.enums.push_back(std::move(e));}
        for(auto raw:module.raw){
            if(raw.kind.find("record")!=std::string::npos||raw.kind.find("enum")!=std::string::npos){raw.name=alias+"."+raw.name;root.raw.push_back(std::move(raw));}
        }
    }
    return !diagnostics.hasErrors();
}

static int parseAndCheck(const fs::path&file,bool compat,bool astOutput=false,bool tokenOutput=false,Program*outProgram=nullptr,bool jsonDiagnostics=false,bool checkTypes=true){
    std::string src;try{src=readFile(file);}catch(const std::exception&e){std::cerr<<"error[ANI-M001]: "<<e.what()<<"\n";return 2;}
    Diagnostics diag(file.string(),src);
    if(tokenOutput){Lexer lex(src,diag);auto ts=lex.scanAll();for(const auto&t:ts){std::cout<<std::setw(5)<<t.span.line<<':'<<std::setw(3)<<t.span.col<<"  "<<std::left<<std::setw(12)<<tokenKindName(t.kind)<<std::right<<"  "<<t.text<<"\n";}diag.print(std::cerr,jsonDiagnostics);return diag.hasErrors()?2:0;}
    Parser parser(src,file.string(),diag,compat);Program p=parser.parse();
    if(!diag.hasErrors()&&!compat&&!p.imports.empty())loadLocalFunctionModules(p,diag);
    if(!diag.hasErrors()&&!compat&&checkTypes){Checker checker(p,diag,compat);checker.check();}
    if(astOutput&&!diag.hasErrors())printAst(p,std::cout);
    if(outProgram)*outProgram=std::move(p);
    diag.print(std::cerr,jsonDiagnostics);return diag.hasErrors()?2:0;
}
static std::string trim(std::string s){auto notspace=[](unsigned char c){return !std::isspace(c);};s.erase(s.begin(),std::find_if(s.begin(),s.end(),notspace));s.erase(std::find_if(s.rbegin(),s.rend(),notspace).base(),s.end());return s;}
static bool replaceAll(std::string&text,const std::string&from,const std::string&to){bool any=false;if(from.empty())return any;std::size_t pos=0;while((pos=text.find(from,pos))!=std::string::npos){text.replace(pos,from.size(),to);pos+=to.size();any=true;}return any;}
static std::string migrateSource(std::string src,std::vector<std::string>&notes){
    const bool legacyUi=src.find("ANIMA 0.3")==std::string::npos && src.find("ANIMA 0.2")==std::string::npos;
    auto pos=src.find("ANIMA 0.2");if(pos!=std::string::npos){src.replace(pos,9,"ANIMA 0.3");notes.push_back("Updated header from ANIMA 0.2 to ANIMA 0.3.");}else if(src.find("ANIMA 0.3")==std::string::npos){src="ANIMA 0.3\n\n"+src;notes.push_back("Added ANIMA 0.3 header for a legacy .anima UI source.");}
    if(replaceAll(src,"series","list of real"))notes.push_back("Replaced the 0.2 series annotation with list of real (review type contexts).");
    std::string out;std::istringstream in(src);std::string line;std::smatch m;
    std::unordered_map<int,std::set<std::string>> declarations;int depth=0;
    std::regex performRe(R"(^([\t ]*)perform[\t ]+([A-Za-z_][A-Za-z_0-9]*)[\t ]*(#.*)?$)");
    std::regex letRe(R"(^([\t ]*)let[\t ]+([A-Za-z_][A-Za-z_0-9]*)([\t ]*:.*|[\t ]*<-.*)$)");
    std::regex zeroArgDefineRe(R"(^([\t ]*define[\t ]+[A-Za-z_][A-Za-z_0-9]*)([\t ]*)\{.*$)");
    std::regex legacyStateRe(R"(^([\t ]*state[\t ]+[A-Za-z_][A-Za-z_0-9]*[\t ]*)=([\t ]*.*?);?[\t ]*$)");
    std::regex canvasRe(R"((node[\t ]+[A-Za-z_][A-Za-z_0-9]*[\t ]*:[\t ]*)canvas\b)");
    std::regex legacyEventRe(R"(\bon[\t ]+(click|press|hover|focus)[\t ]*:[\t ]*([^\n{}]+?);?[\t ]*(?=\}|$))");
    while(std::getline(in,line)){
        if(std::regex_match(line,m,zeroArgDefineRe)){const auto brace=line.find('{');line=m[1].str()+"()"+m[2].str()+"{"+(brace==std::string::npos?std::string{}:line.substr(brace+1));notes.push_back("Added () to a legacy zero-argument definition for the 0.3 function grammar.");}
        if(std::regex_match(line,m,performRe)){line=m[1].str()+m[2].str()+"()"+m[3].str();notes.push_back("Rewrote deprecated perform call to ordinary function-call syntax.");}
        if(legacyUi){
            if(std::regex_match(line,m,legacyStateRe)){line=m[1].str()+"<-"+m[2].str();notes.push_back("Converted legacy state assignment '=' to '<-'.");}
            if(std::regex_search(line,m,canvasRe)){line=std::regex_replace(line,canvasRe,"$1Canvas");notes.push_back("Normalised legacy canvas node type to Canvas.");}
            if(std::regex_search(line,m,legacyEventRe)){auto event=m[1].str();auto body=trim(m[2].str());line=std::regex_replace(line,legacyEventRe,"on "+event+" { "+body+" }");notes.push_back("Converted colon-form UI event handler to block syntax.");}
        }
        if(std::regex_match(line,m,letRe)){auto name=m[2].str();if(declarations[depth].contains(name)){std::string suffix=m[3].str();auto arrow=suffix.find("<-");if(arrow!=std::string::npos)line=m[1].str()+name+" "+suffix.substr(arrow);else notes.push_back("Repeated let for '"+name+"' needs manual conversion to assignment.");}else declarations[depth].insert(name);}
        out+=line+"\n";
        bool inString=false,escape=false;for(char c:line){if(inString){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')inString=false;}else{if(c=='"')inString=true;else if(c=='{')++depth;else if(c=='}')depth=std::max(0,depth-1);}}
    }
    if(legacyUi)notes.push_back("Legacy UI properties are preserved verbatim; native event execution, complete node/property validation, and tree-equivalence verification remain future work.");
    return out;
}
static void sceneDump(const Program&p){
    bool found=false;
    const std::regex declarationRe(R"(\b(scene|node|state|on)[\t ]+([A-Za-z_][A-Za-z_0-9]*)(?:[\t ]*:[\t ]*([A-Za-z_][A-Za-z_0-9]*))?)");
    for(const auto&r:p.raw){if(r.kind.find("app")==std::string::npos)continue;found=true;std::cout<<"app "<<r.name<<"\n";std::vector<std::pair<std::size_t,std::string>> entries;std::string raw=r.raw;std::sregex_iterator it(raw.begin(),raw.end(),declarationRe),end;
        for(;it!=end;++it){const auto pos=static_cast<std::size_t>(it->position());const auto kind=(*it)[1].str();const auto name=(*it)[2].str();const auto type=(*it)[3].matched?(*it)[3].str():std::string{};int depth=0;bool inString=false,escape=false;for(std::size_t i=0;i<pos;++i){char c=raw[i];if(inString){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')inString=false;}else{if(c=='"')inString=true;else if(c=='{')++depth;else if(c=='}')depth=std::max(0,depth-1);}}std::string label=kind+" "+name;if(kind=="node")label+=(type.empty()?": Box":": "+type);else if(kind=="on")label="on "+name;entries.emplace_back(static_cast<std::size_t>(std::max(0,depth-1))*2,label);}
        std::set<std::string> scenes;for(const auto& item:entries){std::cout<<std::string(item.first,' ')<<item.second;if(item.second.rfind("scene ",0)==0){auto nm=item.second.substr(6);if(scenes.empty())std::cout<<" (initial)";scenes.insert(nm);}std::cout<<"\n";}
        if(entries.empty())std::cout<<"  <no scenes or nodes found>\n";
    }
    if(!found)std::cout<<"No app declaration found. Add `app Name { scene Main { node Screen { ... } } }`.\n";
}
static void uiCatalog(const std::string&filter={}){
    struct NodeInfo{const char*name;const char*summary;const char*properties;};
    static const NodeInfo nodes[]={
        {"Box","Generic container; default node type","wide, tall, fill, ink, inset, curve, edge, halo, flow, place, offset, cap"},
        {"Column","Box with column flow preset","wide, tall, fill, inset, gap, wrap, cross, route"},
        {"Row","Box with row flow preset","wide, tall, fill, inset, gap, wrap, cross, route"},
        {"Heading","Prominent text","text, scale, weight, tracking, leading, ink, motion, cap"},
        {"Paragraph","Body text","text, scale, weight, leading, ink, cap, offset"},
        {"Small","Compact label text","text, scale, weight, tracking, ink, cap"},
        {"Button","Interactive control","text, inset, curve, fill, ink, weight, edge, halo; events: click, press, hover, focus"},
        {"Canvas","Render-surface placeholder for native/graphics hosts","wide, tall, fill, curve, edge, halo, place"}
    };
    std::cout<<"ANIMA std.ui catalogue (0.3f preview)\n\nNode types and known properties:\n";
    for(const auto& n:nodes)if(filter.empty()||filter==n.name){std::cout<<"  "<<n.name<<" — "<<n.summary<<"\n    "<<n.properties<<"\n";}
    if(!filter.empty()&&std::none_of(std::begin(nodes),std::end(nodes),[&](const NodeInfo& n){return filter==n.name;})){std::cout<<"Unknown node type '"<<filter<<"'. Known types: Box, Column, Row, Heading, Paragraph, Small, Button, Canvas.\n";}
    std::cout<<"\nUI value types: Length, Spacing, Paint, Flow, Align, Place, TextCase.\nEvent names: click, press, hover, focus.\nScene/UI parsing and deterministic tree inspection are available in this preview; native rendering of this scene tree remains later runtime work.\n";
}
static int selfTest(){int pass=0,fail=0;auto check=[&](std::string name,bool ok){if(ok){++pass;std::cout<<"PASS "<<name<<"\n";}else{++fail;std::cerr<<"FAIL "<<name<<"\n";}};
    const std::string hello="ANIMA 0.3\nstart { announce \"Hello ANIMA\" }\n";
    {Diagnostics d("hello.ani",hello);Lexer l(hello,d);auto t=l.scanAll();check("lexer emits header and EOF",t.size()>5&&t.back().kind==TK::End);}
    {Diagnostics d("hello.ani",hello);Parser p(hello,"hello.ani",d);auto x=p.parse();check("parser recognizes start block",x.hasStart&&!d.hasErrors());}
    {Diagnostics d("expr.ani","ANIMA 0.3\nstart { announce 1 + 2 * 3 }\n");Parser p("ANIMA 0.3\nstart { announce 1 + 2 * 3 }\n","expr.ani",d);auto x=p.parse();Checker c(x,d);c.check();check("precedence parse",!d.hasErrors());}
    auto evalOutput=[&](std::string name,std::string source,std::string expected){Diagnostics d(name,source);Parser p(source,name,d);auto prog=p.parse();if(!d.hasErrors()){Checker c(prog,d);c.check();}if(d.hasErrors())return false;BytecodeCompiler compiler(prog,d);auto bc=compiler.compile();if(!bc||d.hasErrors())return false;std::ostringstream output;auto* old=std::cout.rdbuf(output.rdbuf());try{Runtime host(prog);BytecodeVM vm(prog,*bc,host);vm.run();}catch(...){std::cout.rdbuf(old);return false;}std::cout.rdbuf(old);return output.str()==expected;};
    auto parseClean=[&](std::string name,std::string source){Diagnostics d(name,source);Parser p(source,name,d);auto prog=p.parse();(void)prog;return !d.hasErrors();};
    auto checkerRejects=[&](std::string name,std::string source){Diagnostics d(name,source);Parser p(source,name,d);auto prog=p.parse();if(!d.hasErrors()){Checker c(prog,d);c.check();}return d.hasErrors();};
    check("text concatenation",evalOutput("concat.ani",R"ANI(ANIMA 0.3
start { announce "ANIMA " + "Native" }
)ANI","ANIMA Native\n"));
    check("truth literal output",evalOutput("truth.ani",R"ANI(ANIMA 0.3
start { announce true }
)ANI","true\n"));
    check("negative whole",evalOutput("negative.ani",R"ANI(ANIMA 0.3
start { announce -42 }
)ANI","-42\n"));
    check("mixed numeric division",evalOutput("mixed.ani",R"ANI(ANIMA 0.3
start { announce 7 / 2.0 }
)ANI","3.5\n"));
    check("whole modulo",evalOutput("mod.ani",R"ANI(ANIMA 0.3
start { announce 7 % 3 }
)ANI","1\n"));
    check("equality expression",evalOutput("eq.ani",R"ANI(ANIMA 0.3
start { announce 2 + 2 == 4 }
)ANI","true\n"));
    check("short-circuit and",evalOutput("and.ani",R"ANI(ANIMA 0.3
start { announce false and (1 / 0 == 1) }
)ANI","false\n"));
    check("short-circuit or",evalOutput("or.ani",R"ANI(ANIMA 0.3
start { announce true or (1 / 0 == 1) }
)ANI","true\n"));
    check("zero-count repeat",evalOutput("zero-repeat.ani",R"ANI(ANIMA 0.3
start { repeat 0 { announce 1 } }
)ANI",""));
    check("stop exits current loop",evalOutput("stop.ani",R"ANI(ANIMA 0.3
start { repeat 4 { announce 1
stop } }
)ANI","1\n"));
    check("skip advances loop",evalOutput("skip.ani",R"ANI(ANIMA 0.3
start { repeat i in 0..=4 { when i == 2 { skip }
announce i } }
)ANI","0\n1\n3\n4\n"));
    check("default parameter",evalOutput("default.ani",R"ANI(ANIMA 0.3
define add(a: whole, b: whole <- 2) -> whole { give a + b }
start { announce add(5) }
)ANI","7\n"));
    check("module-scope fixed value",evalOutput("global.ani",R"ANI(ANIMA 0.3
fixed base: whole <- 40
start { announce base + 2 }
)ANI","42\n"));
    check("repeat each over list",evalOutput("each.ani",R"ANI(ANIMA 0.3
start { repeat each item in [1, 2, 3] { announce item } }
)ANI","1\n2\n3\n"));
    check("escaped interpolation braces",evalOutput("braces.ani",R"ANI(ANIMA 0.3
start { announce "\{name\}" }
)ANI","{name}\n"));
    check("Unicode escape",evalOutput("unicode.ani",R"ANI(ANIMA 0.3
start { announce "\u{41}" }
)ANI","A\n"));
    check("hash comment",parseClean("hash-comment.ani",R"ANI(ANIMA 0.3
# comment ignored
start { announce 1 }
)ANI"));
    check("slash comment",parseClean("slash-comment.ani",R"ANI(ANIMA 0.3
// comment ignored
start { announce 1 }
)ANI"));
    check("multiline block comment",parseClean("block-comment.ani",R"ANI(ANIMA 0.3
/* first line
second line */
start { announce 1 }
)ANI"));
    check("semicolon as statement terminator",evalOutput("semicolon.ani",R"ANI(ANIMA 0.3
start { announce 1; announce 2 }
)ANI","1\n2\n"));
    check("multiline call arguments",evalOutput("multiline-call.ani",R"ANI(ANIMA 0.3
use std.math
start { announce math.sqrt(
81
) }
)ANI","9.0\n"));
    check("strict equality rejects unrelated types",checkerRejects("strict-eq.ani",R"ANI(ANIMA 0.3
start { announce 1 == "1" }
)ANI"));
    check("undefined name diagnosed",checkerRejects("undefined.ani",R"ANI(ANIMA 0.3
start { announce missing_value }
)ANI"));
    check("wrong parameter type diagnosed",checkerRejects("arg-type.ani",R"ANI(ANIMA 0.3
define identity(x: whole) -> whole { give x }
start { announce identity("wrong") }
)ANI"));
    check("too many arguments diagnosed",checkerRejects("arg-count.ani",R"ANI(ANIMA 0.3
define identity(x: whole) -> whole { give x }
start { announce identity(1, 2) }
)ANI"));
    check("duplicate function diagnosed",checkerRejects("duplicate-fn.ani",R"ANI(ANIMA 0.3
define same() { }
define same() { }
start { }
)ANI"));
    check("parameter annotation required",!parseClean("missing-param-type.ani",R"ANI(ANIMA 0.3
define identity(x) { give x }
start { }
)ANI"));
    check("numeric condition rejected",checkerRejects("numeric-condition.ani",R"ANI(ANIMA 0.3
start { when 1 { announce "bad condition" } }
)ANI"));
    check("invalid string escape diagnosed",!parseClean("bad-escape.ani",R"ANI(ANIMA 0.3
start { announce "\q" }
)ANI"));
    check("unclosed block diagnosed",!parseClean("unclosed.ani",R"ANI(ANIMA 0.3
start { announce 1
)ANI"));
    {std::string src="ANIMA 0.3\nstart { announce -9223372036854775808 }\n";Diagnostics d("min.ani",src);Parser p(src,"min.ani",d);auto x=p.parse();check("int64 minimum special case",!d.hasErrors()&&evalOutput("min.ani",src,"-9223372036854775808\n"));}
    {std::string src="ANIMA 0.3\nstart { announce 0xFE\nannounce 0b1010 }\n";Diagnostics d("bases.ani",src);Parser p(src,"bases.ani",d);auto x=p.parse();check("hex and binary whole literals",!d.hasErrors()&&evalOutput("bases.ani",src,"254\n10\n"));}
    {std::string src="ANIMA 0.3\nstart { let x: whole <- 1.5 }\n";Diagnostics d("widen.ani",src);Parser p(src,"widen.ani",d);auto x=p.parse();Checker c(x,d);c.check();check("real-to-whole implicit conversion rejected",d.hasErrors());}
    {std::string src="ANIMA 0.3\nstart { announce 3037000500 * 3037000500 }\n";Diagnostics d("overflow-mul.ani",src);Parser p(src,"overflow-mul.ani",d);auto x=p.parse();bool threw=false;try{Runtime r(x);r.run();}catch(const std::exception&e){threw=std::string(e.what()).find("ANI-R002")!=std::string::npos;}check("checked integer multiplication overflow",threw&&!d.hasErrors());}
    {std::string src="ANIMA 0.3\nstart { announce -9223372036854775808 % -1 }\n";check("minimum integer modulo edge case",evalOutput("mod-edge.ani",src,"0\n"));}
    check("run hello",evalOutput("hello.ani",hello,"Hello ANIMA\n"));
    check("checked 64-bit whole",evalOutput("whole.ani","ANIMA 0.3\nstart { announce 9007199254740993 }\n","9007199254740993\n"));
    check("integer division truncates",evalOutput("div.ani","ANIMA 0.3\nstart { announce 7 / 2 }\n","3\n"));
    check("math builtin",evalOutput("math.ani","ANIMA 0.3\nuse std.math\nstart { announce math.sqrt(81) }\n","9.0\n"));
    check("list and index",evalOutput("list.ani","ANIMA 0.3\nstart { let xs <- [3, 5, 7]\nannounce xs[1] }\n","5\n"));
    check("if/otherwise",evalOutput("when.ani","ANIMA 0.3\nstart { when 3 > 2 { announce \"yes\" } otherwise { announce \"no\" } }\n","yes\n"));
    check("counted repeat",evalOutput("repeat.ani","ANIMA 0.3\nstart { repeat 3 { announce 1 } }\n","1\n1\n1\n"));
    check("while repeat",evalOutput("while.ani","ANIMA 0.3\nstart { let i <- 0\nrepeat while i < 3 { announce i\ni += 1 } }\n","0\n1\n2\n"));
    check("user function recursion",evalOutput("fib.ani","ANIMA 0.3\ndefine fib(n: whole) -> whole { when n < 2 { give n } give fib(n - 1) + fib(n - 2) }\nstart { announce fib(10) }\n","55\n"));
    check("text interpolation",evalOutput("interp.ani","ANIMA 0.3\nstart { let hp <- 4\nannounce \"hp={hp + 1}\" }\n","hp=5\n"));
    {std::string bad="ANIMA 0.3\nstart { when false { announce ((( } }\n";Diagnostics d("bad.ani",bad);Parser p(bad,"bad.ani",d);auto x=p.parse();(void)x;check("find syntax error in unexecuted branch",d.hasErrors());}
    {std::string bad="ANIMA 0.3\nstart { let hp <- 1\nlet hp <- 2 }\n";Diagnostics d("dup.ani",bad);Parser p(bad,"dup.ani",d);auto x=p.parse();Checker c(x,d);c.check();check("duplicate binding diagnosed",d.hasErrors());}
    {Diagnostics d("overflow.ani","ANIMA 0.3\nstart { announce 9223372036854775808 }\n");Parser p("ANIMA 0.3\nstart { announce 9223372036854775808 }\n","overflow.ani",d);auto x=p.parse();(void)x;check("integer literal overflow diagnosed",d.hasErrors());}
    {std::string src="ANIMA 0.3\n/* hi\n there */\nstart { announce 2 }\n";Diagnostics d("comment.ani",src);Lexer l(src,d);auto t=l.scanAll();check("block comments accepted",!d.hasErrors()&&t.size()>5);}
    {std::string src="ANIMA 0.3\nstart { announce \"line\\nfeed\" }\n";Diagnostics d("esc.ani",src);Parser p(src,"esc.ani",d);auto x=p.parse();check("string escapes parsed",!d.hasErrors()&&x.hasStart);}
    {std::string src="ANIMA 0.3\nstart { announce [1, 2, 3] }\n";Diagnostics d("list.ani",src);Parser p(src,"list.ani",d);auto x=p.parse();check("list AST",!d.hasErrors()&&x.hasStart);}
    {std::string src="ANIMA 0.3\nproject \"Sample\" { target native.windows\ncapability cpu.parallel\n}\nstart {}\n";Diagnostics d("proj.ani",src);Parser p(src,"proj.ani",d);auto x=p.parse();check("project and capability declarations",!d.hasErrors()&&x.capabilities.size()==1);}
    {std::string src="ANIMA 0.3\napp A { scene Home { node Text: Heading { text: \"X\" } } }\nstart {}\n";Diagnostics d("ui.ani",src);Parser p(src,"ui.ani",d);auto x=p.parse();check("UI declarations preserved",!d.hasErrors()&&!x.raw.empty());}
    {std::string src="ANIMA 0.3\nstart { announce 7 / 0 }\n";Diagnostics d("zero.ani",src);Parser p(src,"zero.ani",d);auto x=p.parse();Checker c(x,d);c.check();bool threw=false;try{Runtime r(x);r.run();}catch(...){threw=true;}check("integer division zero runtime error",threw&&!d.hasErrors());}
    {std::string src="ANIMA 0.3\nstart { fixed x <- 2\nx <- 3 }\n";Diagnostics d("fixed.ani",src);Parser p(src,"fixed.ani",d);auto x=p.parse();Checker c(x,d);c.check();check("fixed assignment rejected",d.hasErrors());}
    {std::string src="ANIMA 0.3\ndefine add(a: whole, b: whole) -> whole { give a + b }\nstart { announce add(b: 4, a: 5) }\n";check("named argument parsing",evalOutput("named.ani",src,"9\n"));}
    {std::string src="ANIMA 0.3\nstart { repeat i in 1..=3 { announce i } }\n";check("range loop",evalOutput("range.ani",src,"1\n2\n3\n"));}
    {std::string src="ANIMA 0.3\nstart { announce cpu.logical_cores() }\n";check("CPU host builtin",evalOutput("cpu.ani",src,std::to_string(std::max(1u,std::thread::hardware_concurrency()))+"\n"));}
    {std::string src="ANIMA 0.3\nstart { announce cpu.parallel_sum(1000000) }\n";check("parallel CPU sum",evalOutput("sum.ani",src,"500000500000\n"));}
    {std::string src="ANIMA 0.3\nstart { announce gpu.available() }\n";auto gs=anima02::QueryGpu();check("GPU availability builtin",evalOutput("gpu.ani",src,gs.available?"true\n":"false\n"));}
    check("VM conditional branch",evalOutput("vm-branch.ani","ANIMA 0.3\nstart { when 2 < 3 { announce \"yes\" } otherwise { announce \"no\" } }\n","yes\n"));
    check("VM list index",evalOutput("vm-list.ani","ANIMA 0.3\nstart { let xs <- [3, 5, 7]\nannounce xs[1] }\n","5\n"));
    check("VM range inclusive",evalOutput("vm-range.ani","ANIMA 0.3\nstart { repeat i in 1..=3 { announce i } }\n","1\n2\n3\n"));
    check("VM short-circuit and",evalOutput("vm-short-and.ani","ANIMA 0.3\nstart { announce false and (1 / 0 == 0) }\n","false\n"));
    check("VM named arguments",evalOutput("vm-named.ani","ANIMA 0.3\ndefine add(a: whole, b: whole) -> whole { give a + b }\nstart { announce add(b: 4, a: 5) }\n","9\n"));
    check("VM default argument",evalOutput("vm-default.ani","ANIMA 0.3\ndefine add(a: whole, b: whole <- 7) -> whole { give a + b }\nstart { announce add(5) }\n","12\n"));
    check("VM interpolation",evalOutput("vm-interpolation.ani","ANIMA 0.3\nstart { let hp <- 4\nannounce \"hp={hp + 1}\" }\n","hp=5\n"));
    check("VM map literal and indexing",evalOutput("vm-map.ani","ANIMA 0.3\nstart { let m <- [\"a\": 3, \"b\": 8]\nannounce m[\"b\"] }\n","8\n"));
    check("VM map key/value iteration",evalOutput("vm-map-each.ani",R"ANI(ANIMA 0.3
start {
 let scores <- ["Ada": 10, "Grace": 12]
 repeat each name, score in scores { announce "{name}={score}" }
}
)ANI","Ada=10\nGrace=12\n"));
    check("checker catches type error in untaken branch",checkerRejects("unreachable-type.ani",R"ANI(ANIMA 0.3
start { when false { announce "not executed" - 1 } }
)ANI"));
    check("record constructor defaults and member read/write",evalOutput("record.ani",R"ANI(ANIMA 0.3
record Actor {
 name: text <- "Player"
 hp: whole <- 100
 speed: real <- 3.5
}
start {
 let hero: Actor <- Actor(name: "Ada", hp: 75)
 announce hero.name
 hero.hp <- hero.hp - 5
 announce hero.hp
 announce hero.speed
}
)ANI","Ada\n70\n3.5\n"));
    check("record with expression makes independent updated value",evalOutput("record-with.ani",R"ANI(ANIMA 0.3
record Point {
 x: whole <- 1
 y: whole <- 2
}
start {
 let p: Point <- Point()
 let q <- p with { x: 9 }
 announce q.x
 announce p.x
}
)ANI","9\n1\n"));
    check("record field type mismatch is rejected",checkerRejects("record-bad.ani",R"ANI(ANIMA 0.3
record Point {
 x: whole
}
start { let p <- Point("wrong") }
)ANI"));
    check("enum case values and metadata",evalOutput("enum.ani",R"ANI(ANIMA 0.3
enum Mood { calm, alert, hunting <- 7 }
start {
 let mood: Mood <- Mood.alert
 announce mood
 announce mood.name
 announce mood.value
 announce Mood.hunting.value
}
)ANI","Mood.alert\nalert\n1\n7\n"));
    check("record field defaults to enum case",evalOutput("enum-record-default.ani",R"ANI(ANIMA 0.3
enum ActorState { idle, roaming, hunting }
record Actor { name: text <- "Unknown"
state: ActorState <- ActorState.idle }
start { let actor <- Actor()
announce actor.state.name
let running <- actor with { state: ActorState.roaming }
announce running.state.name
announce actor.state.name }
)ANI","idle\nroaming\nidle\n"));
    check("enum all and from_name",evalOutput("enum-all.ani",R"ANI(ANIMA 0.3
enum Mood { calm, alert, hunting }
start {
 announce Mood.all()
 announce Mood.from_name("calm") fallback Mood.alert
 announce Mood.from_name("missing") is none
}
)ANI","[Mood.calm, Mood.alert, Mood.hunting]\nMood.calm\ntrue\n"));
    check("enum is a valid map key",evalOutput("enum-map.ani",R"ANI(ANIMA 0.3
enum Layer { background <- 10, world <- 20 }
start {
 let draws <- [Layer.background: 2, Layer.world: 5]
 announce draws[Layer.world]
 announce Layer.background == Layer.background
 announce Layer.background == Layer.world
}
)ANI","5\ntrue\nfalse\n"));
    check("duplicate enum cases rejected",checkerRejects("enum-duplicate.ani",R"ANI(ANIMA 0.3
enum Mood { calm, calm }
start {}
)ANI"));
    {Diagnostics d("dis.ani","ANIMA 0.3\nstart { announce 2 + 3 }\n");Parser p("ANIMA 0.3\nstart { announce 2 + 3 }\n","dis.ani",d);auto prog=p.parse();if(!d.hasErrors()){Checker c(prog,d);c.check();}BytecodeCompiler cc(prog,d);auto bc=cc.compile();bool hasAdd=false;if(bc)for(const auto& line:bc->disassembly)if(line.find("ADD_W")!=std::string::npos)hasAdd=true;check("typed bytecode disassembly",bc.has_value()&&!d.hasErrors()&&hasAdd);}
    check("VM continue loop",evalOutput("vm-skip.ani","ANIMA 0.3\nstart { repeat i in 0..5 { when i == 2 { skip } announce i } }\n","0\n1\n3\n4\n"));
    check("VM break loop",evalOutput("vm-stop.ani","ANIMA 0.3\nstart { repeat i in 0..5 { when i == 2 { stop } announce i } }\n","0\n1\n"));
    std::cout<<"\nANIMA Native 0.3 integrated preview self-test: "<<pass<<" passed, "<<fail<<" failed\n";return fail?1:0;
}

static void printUsage(){std::cout<<"ANIMA Native 0.3 Integrated Preview (0.2 compatibility + checker/typed VM + records/enums/modules)\n\n"
"Usage:\n  anima version\n  anima tokens <file.ani>\n  anima ast <file.ani>\n  anima check <file.ani> [--format json]\n  anima run <file.ani> [--compat 0.2]\n  anima test <file-or-folder>\n  anima scene <file.ani>\n  anima ui-catalog [NodeType]\n  anima dis <file.ani>\n  anima migrate <file.ani|file.anima> [-o output.ani]\n  anima repl\n  anima self-test\n\nWith the unified launcher: anima docs | room | new <folder> | doctor | legacy <args> | <command> ...\n";
}
static int runTestsPath(const fs::path&path){std::vector<fs::path> files;if(fs::is_regular_file(path))files.push_back(path);else if(fs::is_directory(path)){for(auto&x:fs::recursive_directory_iterator(path))if(x.is_regular_file()&&x.path().extension()==".ani")files.push_back(x.path());}else{std::cerr<<"error[ANI-M001]: file or directory not found: "<<path<<"\n";return 2;}std::sort(files.begin(),files.end());int total=0,failures=0;for(auto&file:files){try{auto src=readFile(file);Diagnostics d(file.string(),src);Parser parser(src,file.string(),d);auto p=parser.parse();if(!d.hasErrors()&&!p.imports.empty())loadLocalFunctionModules(p,d);if(!d.hasErrors()){Checker c(p,d);c.check();}if(!d.hasErrors()){BytecodeCompiler compiler(p,d);auto bc=compiler.compile();if(!bc||d.hasErrors()){d.print(std::cerr);++failures;continue;}if(p.tests.empty())continue;Runtime host(p);BytecodeVM vm(p,*bc,host);int result=vm.runTests();total+=static_cast<int>(p.tests.size());if(result)++failures;}else{std::cerr<<file<<": diagnostics while loading tests\n";d.print(std::cerr);++failures;}}catch(const std::exception&e){std::cerr<<file<<": "<<e.what()<<"\n";++failures;}}std::cout<<"Scanned "<<files.size()<<" files; executed "<<total<<" test blocks through the bytecode VM.\n";return failures?1:0;}
static int migrate(const fs::path&input,fs::path output){try{std::string src=readFile(input);std::vector<std::string>notes;std::string migrated=migrateSource(src,notes);if(output.empty()){output=input.parent_path()/(input.stem().string()+".0_3.ani");}
        Diagnostics d(output.string(),migrated);Parser parser(migrated,output.string(),d);auto p=parser.parse();if(!d.hasErrors()){Checker c(p,d);c.check();}if(d.hasErrors()){std::cerr<<"Migration validation failed; original file was not changed.\n";d.print(std::cerr);return 2;}std::ofstream out(output,std::ios::binary);out<<migrated;out.close();std::ofstream report(output.string()+".migration.md");report<<"# ANIMA Migration Report\n\nInput: `"<<input.string()<<"`\n\nOutput: `"<<output.string()<<"`\n\nThis is the experimental 0.3 migrator, not yet the full F6 migrator.\n\n## Rewrites\n";for(auto&n:notes)report<<"- "<<n<<"\n";if(notes.empty())report<<"- No textual rewrites were needed.\n";report<<"\n## Review required\n- Confirm integer division changes: ANIMA 0.3 uses truncating whole/whole division.\n- Review any legacy UI declarations; node/property inventory and tree-equivalence migration are not implemented in this preview.\n";std::cout<<"Migrated to "<<output<<"\nReport: "<<output.string()<<".migration.md\n";return 0;}catch(const std::exception&e){std::cerr<<"error[ANI-M001]: "<<e.what()<<"\n";return 2;}}

} // namespace anima

int main(int argc,char**argv){using namespace anima;if(argc<2){printUsage();return 3;}std::string cmd=argv[1];try{
    if(cmd=="version"){std::cout<<"ANIMA Native 0.3 Integrated Preview\nC++20 | handwritten lexer/parser | static checker | typed bytecode VM\nIncluded: 0.2 compatibility, records, enums, local function modules, native docs, D3D12 room renderer.\nLimits: full classes/traits/closures, complete module exports, native UI rendering and DXR pipeline remain in progress.\n";return 0;}
    if(cmd=="self-test"||cmd=="--self-test")return selfTest();
    if(cmd=="repl"){std::cout<<"ANIMA 0.3 integrated REPL (single-line start snippets; :quit to exit)\n";std::string line;while(std::cout<<">> "&&std::getline(std::cin,line)){if(line==":quit"||line==":q")break;if(trim(line).empty())continue;auto src=std::string("ANIMA 0.3\nstart {\n")+line+"\n}\n";Diagnostics d("<repl>",src);Parser parser(src,"<repl>",d);auto p=parser.parse();if(!d.hasErrors()){Checker c(p,d);c.check();}if(d.hasErrors()){d.print(std::cerr);continue;}try{BytecodeCompiler compiler(p,d);auto bc=compiler.compile();if(!bc||d.hasErrors()){d.print(std::cerr);continue;}Runtime host(p);BytecodeVM vm(p,*bc,host);vm.run();}catch(const std::exception&e){std::cerr<<e.what()<<"\n";}}return 0;}
    if(cmd=="ui-catalog"){uiCatalog(argc>2?argv[2]:std::string{});return 0;}
    if(cmd=="migrate"){if(argc<3){printUsage();return 3;}fs::path in=argv[2],out;for(int i=3;i+1<argc;++i)if(std::string(argv[i])=="-o")out=argv[i+1];return migrate(in,out);}
    if(cmd=="test"){if(argc<3){printUsage();return 3;}return runTestsPath(argv[2]);}
    if(cmd=="tokens"||cmd=="ast"||cmd=="check"||cmd=="run"||cmd=="scene"||cmd=="dis"){
        if(argc<3){printUsage();return 3;}fs::path file=argv[2];bool compat=false,json=false;std::uint64_t stepLimit=100000000;std::size_t maxDepth=256;
        for(int i=3;i<argc;++i){if(std::string(argv[i])=="--compat"&&i+1<argc&&std::string(argv[i+1])=="0.2"){compat=true;++i;}else if(std::string(argv[i])=="--format"&&i+1<argc&&std::string(argv[i+1])=="json"){json=true;++i;}else if(std::string(argv[i])=="--step-limit"&&i+1<argc){stepLimit=std::stoull(argv[++i]);}else if(std::string(argv[i])=="--max-depth"&&i+1<argc){maxDepth=static_cast<std::size_t>(std::stoull(argv[++i]));}}
        if(cmd=="tokens")return parseAndCheck(file,compat,false,true,nullptr,json);
        if(compat)std::cerr<<"notice: ANIMA 0.2 compatibility mode uses the reference evaluator and is deprecated.\n";
        Program p;int rc=parseAndCheck(file,compat,cmd=="ast",false,&p,json,cmd=="check"||cmd=="run"||cmd=="dis");if(rc)return rc;
        if(cmd=="ast") return 0;
        if(cmd=="check"){
            // The specified checker phase remains non-executing. In this preview we also
            // run code generation as a validation pass so check catches constructs that
            // type-check but are not yet supported by the integrated bytecode VM.
            Diagnostics compileDiag(file.string(),p.source);BytecodeCompiler compiler(p,compileDiag);auto bc=compiler.compile();
            if(!bc||compileDiag.hasErrors()){compileDiag.print(std::cerr,json);return 2;}
            std::cout<<"ANIMA 0.3 check passed: "<<file.string()<<"\nStatic checks and current bytecode validation passed; no program code was executed.\n";return 0;
        }
        if(cmd=="scene"){sceneDump(p);return 0;}
        if(compat&&cmd=="run"){try{Runtime rt(p,true);rt.run();return 0;}catch(const std::exception&e){std::cerr<<"error[ANI-R010]: "<<e.what()<<"\n";return 1;}}
        Diagnostics compileDiag(file.string(),p.source);BytecodeCompiler compiler(p,compileDiag);auto bc=compiler.compile();
        if(!bc||compileDiag.hasErrors()){compileDiag.print(std::cerr,json);return 2;}
        if(cmd=="dis"){for(const auto&line:bc->disassembly)std::cout<<line<<"\n";return 0;}
        try{Runtime host(p);BytecodeVM vm(p,*bc,host,stepLimit,maxDepth);vm.run();return 0;}catch(const std::exception&e){std::cerr<<"error[ANI-R010]: "<<e.what()<<"\n";return 1;}
    }
    printUsage();return 3;
}catch(const std::exception&e){std::cerr<<"error[ANI-I001]: unexpected internal error: "<<e.what()<<"\n";return 4;}}
