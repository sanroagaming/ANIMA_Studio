#pragma once

// ANIMA 0.3b/0.3c integrated preview pipeline.
// This is a deliberately inspectable first VM slice. Unsupported language
// constructs fail during code generation instead of falling back to the tree evaluator.

enum class BCOp {
    Push, Load, Declare, Store, Pop, Announce, ToText,
    NegW, NegR, Not,
    AddW, AddR, SubW, SubR, MulW, MulR, DivW, DivR, ModW, ModR,
    Compare, Concat, Fallback, IsNone,
    Jump, JumpFalse, JumpTrue, EnterScope, ExitScope,
    MakeList, MakeMap, Index, MapKeyAt, MapValueAt, Slice, Length,
    MakeRange, RangeStart, RangeContinue, RangeAdvance,
    Call, MakeRecord, GetField, SetField, WithRecord, GetEnumMeta, EnumAll, EnumFromName, Return, Fail, Halt
};

struct BCInstruction {
    BCOp op = BCOp::Halt;
    Value value{};
    std::string name, name2, mode;
    std::vector<std::string> argNames;
    int a = 0;
    int b = 0;
    Span span{};
};

struct BytecodeChunk {
    std::string name;
    std::vector<BCInstruction> code;
    bool expressionOnly = false;
};

struct BytecodeFunction {
    const FunctionDecl* declaration = nullptr;
    BytecodeChunk body;
    std::vector<std::unique_ptr<BytecodeChunk>> defaults;
};

struct BytecodeProgram {
    BytecodeChunk init;
    BytecodeChunk start;
    std::unordered_map<std::string, BytecodeFunction> functions;
    std::unordered_map<std::string, RecordDecl> records;
    std::unordered_map<std::string, EnumDecl> enums;
    std::vector<std::pair<std::string, BytecodeChunk>> tests;
    std::vector<std::string> disassembly;
};

class BytecodeCompiler {
public:
    BytecodeCompiler(Program& p, Diagnostics& d) : program_(p), diagnostics_(d) {}

    std::optional<BytecodeProgram> compile() {
        out_.init.name = "<module-init>";
        out_.start.name = "<start>";
        for (const auto& record : program_.records) out_.records.emplace(record.name, record);
        for (const auto& en : program_.enums) out_.enums.emplace(en.name, en);
        out_.init.code.clear(); out_.start.code.clear();
        typeScopes_.clear(); typeScopes_.emplace_back();
        for (const auto& f : program_.functions) {
            if (out_.functions.contains(f.name)) continue;
            BytecodeFunction bf; bf.declaration = &f; bf.body.name = f.name;
            bf.defaults.resize(f.params.size());
            out_.functions.emplace(f.name, std::move(bf));
            functionTypes_[f.name] = f.hasReturn ? f.returnType : "none";
        }
        // Predeclare module globals so forward global references can compile.
        for (const auto& g : program_.globals) {
            if (g && (g->kind == SK::Let || g->kind == SK::Fixed))
                typeScopes_.front()[g->name] = g->type.empty() ? guessType(g->expr.get()) : g->type;
        }
        const auto moduleTypes = typeScopes_.front();
        for (auto& [name, bf] : out_.functions) {
            const auto& f = *bf.declaration;
            typeScopes_.clear(); typeScopes_.push_back(moduleTypes); typeScopes_.emplace_back();
            for (const auto& param : f.params) typeScopes_.back()[param.name] = param.type;
            for (std::size_t i=0; i<f.params.size(); ++i) {
                if (f.params[i].defaultValue) {
                    auto dc = std::make_unique<BytecodeChunk>(); dc->name = name + "::<default:" + f.params[i].name + ">"; dc->expressionOnly=true;
                    // A default sees earlier parameters, not later ones.
                    typeScopes_.back().clear();
                    for (std::size_t j=0; j<i; ++j) typeScopes_.back()[f.params[j].name] = f.params[j].type;
                    compileExpr(f.params[i].defaultValue.get(), *dc);
                    emit(*dc, BCOp::Halt, f.params[i].span);
                    bf.defaults[i] = std::move(dc);
                }
            }
            typeScopes_.back().clear();
            for (const auto& param : f.params) typeScopes_.back()[param.name] = param.type;
            compileStmt(f.body.get(), bf.body);
            emit(bf.body, BCOp::Push, f.span, Value{});
            emit(bf.body, BCOp::Return, f.span);
        }

        typeScopes_.clear(); typeScopes_.push_back(moduleTypes);
        for (const auto& g : program_.globals) {
            if (!g) continue;
            compileStmt(g.get(), out_.init);
            if (g->kind == SK::Let || g->kind == SK::Fixed)
                typeScopes_.front()[g->name] = g->type.empty() ? guessType(g->expr.get()) : g->type;
        }
        emit(out_.init, BCOp::Halt);
        const auto finalModuleTypes = typeScopes_.front();

        typeScopes_.clear(); typeScopes_.push_back(finalModuleTypes); typeScopes_.emplace_back();
        if (program_.start) compileStmt(program_.start.get(), out_.start);
        emit(out_.start, BCOp::Halt);

        for (const auto& t : program_.tests) {
            BytecodeChunk chunk; chunk.name = "test:" + t.name;
            typeScopes_.clear(); typeScopes_.push_back(finalModuleTypes); typeScopes_.emplace_back();
            compileStmt(t.body.get(), chunk); emit(chunk, BCOp::Halt, t.span);
            out_.tests.emplace_back(t.name, std::move(chunk));
        }
        if (diagnostics_.hasErrors()) return std::nullopt;
        buildDisassembly();
        return std::move(out_);
    }

private:
    Program& program_; Diagnostics& diagnostics_; BytecodeProgram out_;
    std::vector<std::unordered_map<std::string,std::string>> typeScopes_;
    std::unordered_map<std::string,std::string> functionTypes_;
    struct LoopContext { std::size_t continueTarget=0; std::vector<std::size_t> breakJumps; std::vector<std::size_t> continueJumps; };
    std::vector<LoopContext> loops_;
    std::size_t tempId_=0; int lexicalDepth_=0;

    static std::string dotted(Expr* e) {
        if (!e) return {};
        if (e->kind==EK::Identifier) return e->text;
        if (e->kind==EK::Member) { auto left=dotted(e->a.get()); return left.empty()?e->text:left+"."+e->text; }
        return {};
    }
    static bool isNumericType(const std::string& s) { return s=="whole"||s=="real"; }
    std::string lookupType(const std::string& name) const {
        for (auto it=typeScopes_.rbegin(); it!=typeScopes_.rend(); ++it) { auto f=it->find(name); if(f!=it->end()) return f->second; }
        return "unknown";
    }
    std::string guessType(Expr* e) const {
        if(!e) return "none";
        switch(e->kind) {
            case EK::Literal: return typeOfLiteral(e->literal);
            case EK::Identifier: { auto t=lookupType(e->text); if(t!="unknown")return t; if(functionTypes_.contains(e->text))return "function"; if(e->text=="PI"||e->text=="TAU"||e->text=="E"||e->text=="INF"||e->text=="NAN")return "real"; return "unknown"; }
            case EK::Unary: return e->text=="not"?"truth":guessType(e->a.get());
            case EK::Binary: {
                const std::string op=e->text; auto a=guessType(e->a.get()), b=guessType(e->b.get());
                if(op=="and"||op=="or"||op=="=="||op=="!="||op=="<"||op=="<="||op==">"||op==">="||op=="is")return "truth";
                if(op==".."||op=="..=") return "range";
                if(op=="fallback") return a=="none"?b:a;
                if(op=="+"&&(a=="text"||b=="text"))return "text";
                if(isNumericType(a)&&isNumericType(b)) return a=="real"||b=="real"?"real":"whole";
                return "unknown";
            }
            case EK::List: { std::string t="unknown"; for(auto&x:e->items){auto q=guessType(x.get());if(t=="unknown")t=q;else if(t!=q)t="value";}return "list of "+t; }
            case EK::Map: {std::string k="unknown",v="unknown";for(std::size_t i=0;i<e->items.size();++i){auto t=guessType(e->items[i].get());if(i%2==0){if(k=="unknown")k=t;else if(k!=t)k="value";}else{if(v=="unknown")v=t;else if(v!=t)v="value";}}return e->items.empty()?"map":"map of "+k+" to "+v;}
            case EK::Index: { auto a=guessType(e->a.get()); if(a.rfind("list of ",0)==0)return a.substr(8);if(a=="text")return "text";if(a=="map")return "value";return "unknown"; }
            case EK::Slice: return guessType(e->a.get());
            case EK::Call: {
                auto n=dotted(e->a.get()); if(out_.records.contains(n))return n; if(functionTypes_.contains(n))return functionTypes_.at(n);
                if(n=="host.platform"||n=="gpu.adapter"||n=="gpu.backend"||n=="to_text"||n=="type_name")return "text";
                if(n=="cpu.logical_cores"||n=="cpu.cores"||n=="cpu.parallel_sum"||n=="gpu.vram_mb"||n=="to_whole"||n=="math.floor"||n=="math.ceil"||n=="math.round"||n=="math.trunc")return "whole";
                if(n=="gpu.available"||n=="gpu.dxr_supported"||n=="assert"||n=="expect")return "truth";
                if(n=="gpu.squares")return "list of real";
                if(n.rfind("math.",0)==0) return "real";
                return "unknown";
            }
            case EK::Member: {
                const auto path=dotted(e);
                for(const auto& [enumName,en]:out_.enums){
                    if(path.rfind(enumName+".",0)==0){
                        const auto suffix=path.substr(enumName.size()+1);
                        auto dot=suffix.find('.');
                        const auto caseName=dot==std::string::npos?suffix:suffix.substr(0,dot);
                        auto it=std::find_if(en.cases.begin(),en.cases.end(),[&](const EnumCaseDecl& c){return c.name==caseName;});
                        if(it!=en.cases.end()){
                            if(dot==std::string::npos)return enumName;
                            const auto meta=suffix.substr(dot+1);if(meta=="name")return "text";if(meta=="value")return "whole";
                        }
                    }
                }
                const auto owner=guessType(e->a.get());
                if(out_.enums.contains(owner)){if(e->text=="name")return "text";if(e->text=="value")return "whole";}
                for(const auto& [recordName, record]:out_.records){if(owner==recordName){auto it=std::find_if(record.fields.begin(),record.fields.end(),[&](const RecordFieldDecl& f){return f.name==e->text;});if(it!=record.fields.end())return it->type;}}
                return "unknown";
            }
            case EK::Lambda:return "function"; case EK::With:return guessType(e->a.get());
        }
        return "unknown";
    }
    std::size_t emit(BytecodeChunk& c, BCOp op, Span span={}, Value v={}, std::string n={}, std::string n2={}, int a=0, int b=0, std::vector<std::string> args={}) {
        BCInstruction i; i.op=op;i.span=span;i.value=std::move(v);i.name=std::move(n);i.name2=std::move(n2);i.a=a;i.b=b;i.argNames=std::move(args);c.code.push_back(std::move(i));return c.code.size()-1;
    }
    std::size_t emitJump(BytecodeChunk& c, BCOp op, Span sp={}) { return emit(c,op,sp,{}, {}, {}, -1, -1); }
    void patchJump(BytecodeChunk& c,std::size_t at,std::size_t target,int depth) { c.code[at].a=static_cast<int>(target);c.code[at].b=depth; }
    void emitScopeEnter(BytecodeChunk& c,Span sp={}) { emit(c,BCOp::EnterScope,sp); ++lexicalDepth_;typeScopes_.push_back(typeScopes_.back()); }
    void emitScopeExit(BytecodeChunk& c,Span sp={}) { emit(c,BCOp::ExitScope,sp); if(lexicalDepth_>0)--lexicalDepth_;if(typeScopes_.size()>1)typeScopes_.pop_back(); }
    void pushType(const std::string& n,const std::string& t) { typeScopes_.back()[n]=t.empty()?"unknown":t; }

    static Value literalValue(const Literal& l) { return std::visit([](const auto& x)->Value { return Value(x); }, l); }
    void compileText(const Expr* e,BytecodeChunk& c) {
        const auto* raw=std::get_if<std::string>(&e->literal); if(!raw){emit(c,BCOp::Push,e->span,literalValue(e->literal));return;}
        const std::string& s=*raw;
        bool has=false;for(char ch:s)if(ch=='{'||ch=='\x01'||ch=='\x02'){has=true;break;}
        if(!has){emit(c,BCOp::Push,e->span,Value(s));return;}
        emit(c,BCOp::Push,e->span,Value(std::string{}));
        std::string part;
        auto appendPart=[&](){if(!part.empty()){emit(c,BCOp::Push,e->span,Value(part));emit(c,BCOp::Concat,e->span);part.clear();}};
        for(std::size_t i=0;i<s.size();) {
            if(s[i]=='\x01'){part+='{';++i;continue;} if(s[i]=='\x02'){part+='}';++i;continue;}
            if(s[i]!='{'){part+=s[i++];continue;}
            std::size_t j=i+1;int depth=1;while(j<s.size()&&depth){if(s[j]=='{')++depth;else if(s[j]=='}')--depth;if(depth)++j;}
            if(depth){part+=s.substr(i);break;}
            appendPart(); std::string inner=s.substr(i+1,j-i-1);Diagnostics innerDiag("<interpolation>",inner);Parser p(inner,"<interpolation>",innerDiag);auto expr=p.parseExpressionOnly();
            if(innerDiag.hasErrors()){diagnostics_.error("ANI-C004","invalid interpolation expression",e->span,"check the expression inside {...}");return;}
            compileExpr(expr.get(),c);emit(c,BCOp::ToText,e->span);emit(c,BCOp::Concat,e->span);i=j+1;
        }
        appendPart();
    }
    void compileExpr(Expr* e,BytecodeChunk& c) {
        if(!e){emit(c,BCOp::Push,{},Value{});return;}
        switch(e->kind) {
            case EK::Literal: compileText(e,c); return;
            case EK::Identifier: emit(c,BCOp::Load,e->span,{},e->text); return;
            case EK::Member: {
                const auto path=dotted(e);
                const auto ownerType=guessType(e->a.get());
                bool resolvedEnum=false;
                for(const auto& [enumName,en]:out_.enums){
                    if(path.rfind(enumName+".",0)!=0)continue;
                    const auto suffix=path.substr(enumName.size()+1);
                    const auto dot=suffix.find('.');
                    const auto caseName=dot==std::string::npos?suffix:suffix.substr(0,dot);
                    auto it=std::find_if(en.cases.begin(),en.cases.end(),[&](const EnumCaseDecl& item){return item.name==caseName;});
                    if(it==en.cases.end())continue;
                    if(dot==std::string::npos)emit(c,BCOp::Push,e->span,Value(EnumValue{enumName,it->name,it->value}));
                    else {const auto meta=suffix.substr(dot+1);if(meta=="name")emit(c,BCOp::Push,e->span,Value(it->name));else if(meta=="value")emit(c,BCOp::Push,e->span,Value(it->value));else diagnostics_.error("ANI-T020","enum value has no member '"+meta+"'",e->span);}
                    resolvedEnum=true;break;
                }
                if(resolvedEnum)return;
                if(out_.records.contains(ownerType)) { compileExpr(e->a.get(),c); emit(c,BCOp::GetField,e->span,{},e->text,ownerType); }
                else if(out_.enums.contains(ownerType)&&(e->text=="name"||e->text=="value")){compileExpr(e->a.get(),c);emit(c,BCOp::GetEnumMeta,e->span,{},e->text,ownerType);}
                else if(path=="math.PI"||path=="std.math.PI"||path=="PI")emit(c,BCOp::Push,e->span,Value(3.14159265358979323846));
                else if(path=="math.TAU"||path=="TAU")emit(c,BCOp::Push,e->span,Value(6.28318530717958647692));
                else if(path=="math.E"||path=="E")emit(c,BCOp::Push,e->span,Value(2.71828182845904523536));
                else emit(c,BCOp::Push,e->span,Value(path));
                return;
            }
            case EK::Unary: compileExpr(e->a.get(),c);if(e->text=="not")emit(c,BCOp::Not,e->span);else {auto t=guessType(e->a.get());emit(c,t=="whole"?BCOp::NegW:BCOp::NegR,e->span);}return;
            case EK::Binary: {
                const auto op=e->text;
                if(op=="and"||op=="or") {
                    compileExpr(e->a.get(),c);auto branch=emitJump(c,op=="and"?BCOp::JumpFalse:BCOp::JumpTrue,e->span);
                    compileExpr(e->b.get(),c);auto done=emitJump(c,BCOp::Jump,e->span);
                    patchJump(c,branch,c.code.size(),lexicalDepth_);emit(c,BCOp::Push,e->span,Value(op=="or"));patchJump(c,done,c.code.size(),lexicalDepth_);return;
                }
                if(op==".."||op=="..=") {compileExpr(e->a.get(),c);compileExpr(e->b.get(),c);emit(c,BCOp::MakeRange,e->span,{},op);return;}
                if(op=="is" && e->b && ((e->b->kind==EK::Literal && std::holds_alternative<std::monostate>(e->b->literal)) || (e->b->kind==EK::Unary&&e->b->text=="not"&&e->b->a&&e->b->a->kind==EK::Literal&&std::holds_alternative<std::monostate>(e->b->a->literal)))) {
                    compileExpr(e->a.get(),c);emit(c,BCOp::IsNone,e->span,{},e->b->kind==EK::Unary?"not":"is");return;
                }
                compileExpr(e->a.get(),c);compileExpr(e->b.get(),c);
                const auto a=guessType(e->a.get()), b=guessType(e->b.get());const std::string result=guessType(e);
                if(op=="fallback")emit(c,BCOp::Fallback,e->span);
                else if(op=="is") { diagnostics_.error("ANI-C007","type-test expressions require object metadata from 0.3d",e->span); }
                else if(op=="+"&&(a=="text"||b=="text"))emit(c,BCOp::Concat,e->span);
                else if(op=="+"&&result=="whole")emit(c,BCOp::AddW,e->span);
                else if(op=="+"&&result=="real")emit(c,BCOp::AddR,e->span);
                else if(op=="-"&&result=="whole")emit(c,BCOp::SubW,e->span);
                else if(op=="-"&&result=="real")emit(c,BCOp::SubR,e->span);
                else if(op=="*"&&result=="whole")emit(c,BCOp::MulW,e->span);
                else if(op=="*"&&result=="real")emit(c,BCOp::MulR,e->span);
                else if(op=="/"&&result=="whole")emit(c,BCOp::DivW,e->span);
                else if(op=="/"&&result=="real")emit(c,BCOp::DivR,e->span);
                else if(op=="%"&&result=="whole")emit(c,BCOp::ModW,e->span);
                else if(op=="%"&&result=="real")emit(c,BCOp::ModR,e->span);
                else emit(c,BCOp::Compare,e->span,{},op,a,b=="text"?1:0);
                return;
            }
            case EK::Call: {
                const auto callee=dotted(e->a.get());
                if(callee.empty()){diagnostics_.error("ANI-C002","indirect calls are not yet supported by the 0.3 preview",e->span);return;}
                for(const auto& [enumName,en]:out_.enums){(void)en;
                    if(callee==enumName+".all"){if(!e->items.empty())diagnostics_.error("ANI-T003",callee+" expects no arguments",e->span);emit(c,BCOp::EnumAll,e->span,{},enumName);return;}
                    if(callee==enumName+".from_name"){if(e->items.size()!=1){diagnostics_.error("ANI-T003",callee+" expects one argument",e->span);return;}compileExpr(e->items.front().get(),c);emit(c,BCOp::EnumFromName,e->span,{},enumName);return;}
                }
                std::vector<std::string> argNames;argNames.reserve(e->items.size());
                for(std::size_t i=0;i<e->items.size();++i){compileExpr(e->items[i].get(),c);argNames.push_back(i<e->names.size()?e->names[i]:std::string{});}
                if(out_.records.contains(callee)) emit(c,BCOp::MakeRecord,e->span,{},callee,{},static_cast<int>(e->items.size()),0,std::move(argNames));
                else emit(c,BCOp::Call,e->span,{},callee,{},static_cast<int>(e->items.size()),0,std::move(argNames));
                return;
            }
            case EK::List: for(auto& item:e->items)compileExpr(item.get(),c);emit(c,BCOp::MakeList,e->span,{}, {}, {},static_cast<int>(e->items.size()));return;
            case EK::Map: {for(auto& item:e->items)compileExpr(item.get(),c);emit(c,BCOp::MakeMap,e->span,{}, {}, {},static_cast<int>(e->items.size()/2));return;}
            case EK::Index: compileExpr(e->a.get(),c);if(e->b&&e->b->kind==EK::Binary&&(e->b->text==".."||e->b->text=="..=")){compileExpr(e->b->a.get(),c);compileExpr(e->b->b.get(),c);emit(c,BCOp::Slice,e->span,{},e->b->text);}else{compileExpr(e->b.get(),c);emit(c,BCOp::Index,e->span);}return;
            case EK::Slice: compileExpr(e->a.get(),c);compileExpr(e->b.get(),c);emit(c,BCOp::Slice,e->span);return;
            case EK::Lambda: diagnostics_.error("ANI-C003","lambda compilation is not included in this combined preview",e->span,"named functions are compiled to bytecode; lambda and closure lowering is the next 0.3b/0.3c gap");return;
            case EK::With: { auto type=guessType(e->a.get()); if(!out_.records.contains(type)){diagnostics_.error("ANI-C003","with requires a record value",e->span);return;}compileExpr(e->a.get(),c);std::vector<std::string> fields;for(std::size_t i=0;i<e->items.size();++i){compileExpr(e->items[i].get(),c);fields.push_back(i<e->names.size()?e->names[i]:std::string{});}emit(c,BCOp::WithRecord,e->span,{},type,{},static_cast<int>(e->items.size()),0,std::move(fields));return; }
        }
    }
    void compileStmt(Stmt* s,BytecodeChunk& c) {
        if(!s) return;
        switch(s->kind) {
            case SK::Block: emitScopeEnter(c,s->span);for(auto&x:s->statements)compileStmt(x.get(),c);emitScopeExit(c,s->span);return;
            case SK::Let: case SK::Fixed: compileExpr(s->expr.get(),c);emit(c,BCOp::Declare,s->span,{},s->name,s->type.empty()?guessType(s->expr.get()):s->type,s->kind==SK::Fixed?1:0);pushType(s->name,s->type.empty()?guessType(s->expr.get()):s->type);return;
            case SK::Assign: {
                if(s->name.empty()){
                    if(s->expr&&s->expr->kind==EK::Member&&s->expr->a&&s->expr->a->kind==EK::Identifier){
                        const auto baseName=s->expr->a->text;const auto baseType=lookupType(baseName);
                        if(!out_.records.contains(baseType)||s->op!="<-"){diagnostics_.error("ANI-C003","only direct record field assignment with <- is enabled in this preview",s->span);return;}
                        compileExpr(s->expr->a.get(),c);compileExpr(s->expr2.get(),c);emit(c,BCOp::SetField,s->span,{},s->expr->text,baseType);return;
                    }
                    diagnostics_.error("ANI-C003","indexed assignment is not enabled in this preview",s->span);return;
                }
                if(s->op!="<-") {emit(c,BCOp::Load,s->span,{},s->name);compileExpr(s->expr2.get(),c);std::string op=s->op.substr(0,1);auto t=lookupType(s->name);BCOp b=op=="+"?(t=="real"?BCOp::AddR:BCOp::AddW):op=="-"?(t=="real"?BCOp::SubR:BCOp::SubW):op=="*"?(t=="real"?BCOp::MulR:BCOp::MulW):op=="/"?(t=="real"?BCOp::DivR:BCOp::DivW):(t=="real"?BCOp::ModR:BCOp::ModW);emit(c,b,s->span);}
                else compileExpr(s->expr2.get(),c);
                emit(c,BCOp::Store,s->span,{},s->name);return;
            }
            case SK::Announce: compileExpr(s->expr.get(),c);emit(c,BCOp::Announce,s->span);return;
            case SK::Give: if(s->expr)compileExpr(s->expr.get(),c);else emit(c,BCOp::Push,s->span,Value{});emit(c,BCOp::Return,s->span);return;
            case SK::Expr: compileExpr(s->expr.get(),c);emit(c,BCOp::Pop,s->span);return;
            case SK::When: {
                std::vector<std::size_t> ends;compileExpr(s->expr.get(),c);auto no=emitJump(c,BCOp::JumpFalse,s->span);compileStmt(s->body.get(),c);ends.push_back(emitJump(c,BCOp::Jump,s->span));patchJump(c,no,c.code.size(),lexicalDepth_);
                for(auto& branch:s->branches){compileExpr(branch.condition.get(),c);auto next=emitJump(c,BCOp::JumpFalse,branch.condition->span);compileStmt(branch.body.get(),c);ends.push_back(emitJump(c,BCOp::Jump,s->span));patchJump(c,next,c.code.size(),lexicalDepth_);}
                if(s->otherwiseBody) compileStmt(s->otherwiseBody.get(),c);
                for(auto j:ends)patchJump(c,j,c.code.size(),lexicalDepth_);
                return;
            }
            case SK::RepeatCount: {
                const std::string count=std::string(1,'\x1f')+"count_"+std::to_string(tempId_), idx=std::string(1,'\x1f')+"index_"+std::to_string(tempId_++);
                compileExpr(s->expr.get(),c);emit(c,BCOp::Declare,s->span,{},count,"whole",0);emit(c,BCOp::Push,s->span,Value(std::int64_t(0)));emit(c,BCOp::Declare,s->span,{},idx,"whole",0);
                const int outer=lexicalDepth_;std::size_t top=c.code.size();emit(c,BCOp::Load,s->span,{},idx);emit(c,BCOp::Load,s->span,{},count);emit(c,BCOp::Compare,s->span,{},"<","whole");auto end=emitJump(c,BCOp::JumpFalse,s->span);
                loops_.push_back({0,{},{}});compileStmt(s->body.get(),c);auto& loop=loops_.back();loop.continueTarget=c.code.size();for(auto cj:loop.continueJumps)patchJump(c,cj,loop.continueTarget,outer);emit(c,BCOp::Load,s->span,{},idx);emit(c,BCOp::Push,s->span,Value(std::int64_t(1)));emit(c,BCOp::AddW,s->span);emit(c,BCOp::Store,s->span,{},idx);auto back=emitJump(c,BCOp::Jump,s->span);patchJump(c,back,top,outer);std::size_t exit=c.code.size();patchJump(c,end,exit,outer);for(auto j:loop.breakJumps)patchJump(c,j,exit,outer);loops_.pop_back();return;
            }
            case SK::RepeatWhile: {
                const int outer=lexicalDepth_;std::size_t top=c.code.size();compileExpr(s->expr.get(),c);auto end=emitJump(c,BCOp::JumpFalse,s->span);loops_.push_back({0,{},{}});compileStmt(s->body.get(),c);auto& loop=loops_.back();loop.continueTarget=top;for(auto cj:loop.continueJumps)patchJump(c,cj,top,outer);auto back=emitJump(c,BCOp::Jump,s->span);patchJump(c,back,top,outer);std::size_t exit=c.code.size();patchJump(c,end,exit,outer);for(auto j:loop.breakJumps)patchJump(c,j,exit,outer);loops_.pop_back();return;
            }
            case SK::RepeatRange: {
                if(!s->expr||s->expr->kind!=EK::Binary||(s->expr->text!=".."&&s->expr->text!="..=")){diagnostics_.error("ANI-C005","repeat range requires a..b or a..=b",s->span);return;}
                const auto id=std::to_string(tempId_++);const std::string rn="\x1frange_"+id, ix="\x1findex_"+id;
                compileExpr(s->expr.get(),c);emit(c,BCOp::Declare,s->span,{},rn,"range",0);emit(c,BCOp::Load,s->span,{},rn);emit(c,BCOp::RangeStart,s->span);emit(c,BCOp::Declare,s->span,{},ix,"whole",0);
                const int outer=lexicalDepth_;std::size_t top=c.code.size();emit(c,BCOp::Load,s->span,{},ix);emit(c,BCOp::Load,s->span,{},rn);emit(c,BCOp::RangeContinue,s->span);auto end=emitJump(c,BCOp::JumpFalse,s->span);
                emitScopeEnter(c,s->span);emit(c,BCOp::Load,s->span,{},ix);emit(c,BCOp::Declare,s->span,{},s->name,"whole",1);pushType(s->name,"whole");
                loops_.push_back({0,{},{}});compileStmt(s->body.get(),c);emitScopeExit(c,s->span);auto& loop=loops_.back();loop.continueTarget=c.code.size();for(auto cj:loop.continueJumps)patchJump(c,cj,loop.continueTarget,outer);emit(c,BCOp::RangeAdvance,s->span,{},ix,rn);auto back=emitJump(c,BCOp::Jump,s->span);patchJump(c,back,top,outer);std::size_t exit=c.code.size();patchJump(c,end,exit,outer);for(auto j:loop.breakJumps)patchJump(c,j,exit,outer);loops_.pop_back();return;
            }
            case SK::RepeatEach: {
                const auto id=std::to_string(tempId_++);const std::string src="\x1fsource_"+id, ix="\x1findex_"+id;
                const std::string sourceType=guessType(s->expr.get());
                const bool isMap=sourceType.rfind("map of ",0)==0;
                std::string keyType="unknown", valueTypeName="unknown";
                if(isMap){const auto split=sourceType.find(" to ",7);if(split!=std::string::npos){keyType=sourceType.substr(7,split-7);valueTypeName=sourceType.substr(split+4);}}
                compileExpr(s->expr.get(),c);emit(c,BCOp::Declare,s->span,{},src,sourceType,0);emit(c,BCOp::Push,s->span,Value(std::int64_t(0)));emit(c,BCOp::Declare,s->span,{},ix,"whole",0);
                const int outer=lexicalDepth_;std::size_t top=c.code.size();emit(c,BCOp::Load,s->span,{},ix);emit(c,BCOp::Load,s->span,{},src);emit(c,BCOp::Length,s->span);emit(c,BCOp::Compare,s->span,{},"<","whole");auto end=emitJump(c,BCOp::JumpFalse,s->span);
                emitScopeEnter(c,s->span);
                emit(c,BCOp::Load,s->span,{},src);emit(c,BCOp::Load,s->span,{},ix);
                if(isMap) emit(c,BCOp::MapKeyAt,s->span); else emit(c,BCOp::Index,s->span);
                std::string firstType=isMap?keyType:(sourceType.rfind("list of ",0)==0?sourceType.substr(8):"text");
                emit(c,BCOp::Declare,s->span,{},s->name,firstType,1);pushType(s->name,firstType);
                if(isMap&&!s->name2.empty()){emit(c,BCOp::Load,s->span,{},src);emit(c,BCOp::Load,s->span,{},ix);emit(c,BCOp::MapValueAt,s->span);emit(c,BCOp::Declare,s->span,{},s->name2,valueTypeName,1);pushType(s->name2,valueTypeName);}
                loops_.push_back({0,{},{}});compileStmt(s->body.get(),c);emitScopeExit(c,s->span);auto& loop=loops_.back();loop.continueTarget=c.code.size();for(auto cj:loop.continueJumps)patchJump(c,cj,loop.continueTarget,outer);emit(c,BCOp::Load,s->span,{},ix);emit(c,BCOp::Push,s->span,Value(std::int64_t(1)));emit(c,BCOp::AddW,s->span);emit(c,BCOp::Store,s->span,{},ix);auto back=emitJump(c,BCOp::Jump,s->span);patchJump(c,back,top,outer);std::size_t exit=c.code.size();patchJump(c,end,exit,outer);for(auto j:loop.breakJumps)patchJump(c,j,exit,outer);loops_.pop_back();return;
            }
            case SK::Stop: if(loops_.empty())diagnostics_.error("ANI-P010","stop is only valid inside a loop",s->span);else loops_.back().breakJumps.push_back(emitJump(c,BCOp::Jump,s->span));return;
            case SK::Skip: if(loops_.empty())diagnostics_.error("ANI-P010","skip is only valid inside a loop",s->span);else loops_.back().continueJumps.push_back(emitJump(c,BCOp::Jump,s->span));return;
            case SK::Fail: compileExpr(s->expr.get(),c);emit(c,BCOp::Fail,s->span);return;
            case SK::Attempt: diagnostics_.error("ANI-C006","attempt/rescue/always bytecode handlers are not enabled in the first combined 0.3bc preview",s->span,"use fail for an uncaught runtime failure; handler tables are part of C6");return;
        }
    }
    void buildDisassembly() {
        auto label=[](BCOp op){switch(op){case BCOp::Push:return "CONST";case BCOp::Load:return "LOAD_G/NAME";case BCOp::Declare:return "DECLARE";case BCOp::Store:return "STORE_NAME";case BCOp::Pop:return "POP";case BCOp::Announce:return "ANNOUNCE";case BCOp::ToText:return "TO_TEXT";case BCOp::NegW:return "NEG_W";case BCOp::NegR:return "NEG_R";case BCOp::Not:return "NOT";case BCOp::AddW:return "ADD_W";case BCOp::AddR:return "ADD_R";case BCOp::SubW:return "SUB_W";case BCOp::SubR:return "SUB_R";case BCOp::MulW:return "MUL_W";case BCOp::MulR:return "MUL_R";case BCOp::DivW:return "DIV_W";case BCOp::DivR:return "DIV_R";case BCOp::ModW:return "MOD_W";case BCOp::ModR:return "MOD_R";case BCOp::Compare:return "COMPARE";case BCOp::Concat:return "CONCAT";case BCOp::Fallback:return "FALLBACK";case BCOp::IsNone:return "IS_NONE";case BCOp::Jump:return "JMP";case BCOp::JumpFalse:return "JMP_F";case BCOp::JumpTrue:return "JMP_T";case BCOp::EnterScope:return "SCOPE_IN";case BCOp::ExitScope:return "SCOPE_OUT";case BCOp::MakeList:return "LIST";case BCOp::MakeMap:return "MAP";case BCOp::Index:return "INDEX";case BCOp::MapKeyAt:return "MAP_KEY_AT";case BCOp::MapValueAt:return "MAP_VALUE_AT";case BCOp::Slice:return "SLICE";case BCOp::Length:return "LENGTH";case BCOp::MakeRange:return "RANGE";case BCOp::RangeStart:return "RANGE_START";case BCOp::RangeContinue:return "RANGE_NEXT_TEST";case BCOp::RangeAdvance:return "RANGE_ADVANCE";case BCOp::Call:return "CALL";case BCOp::MakeRecord:return "MAKE_RECORD";case BCOp::GetField:return "GET_FIELD";case BCOp::SetField:return "SET_FIELD";case BCOp::WithRecord:return "WITH_RECORD";case BCOp::GetEnumMeta:return "ENUM_META";case BCOp::EnumAll:return "ENUM_ALL";case BCOp::EnumFromName:return "ENUM_FROM_NAME";case BCOp::Return:return "RET_V";case BCOp::Fail:return "THROW";case BCOp::Halt:return "HALT";}return "?";};
        auto add=[&](const BytecodeChunk& c){for(std::size_t i=0;i<c.code.size();++i){const auto&x=c.code[i];std::ostringstream o;o<<c.name<<' '<<std::setw(4)<<i<<"  "<<label(x.op);if(!x.name.empty())o<<' '<<x.name;if(!x.mode.empty())o<<' '<<x.mode;if(x.a)o<<" a="<<x.a;if(x.op==BCOp::Push)o<<" "<<display(x.value);if(x.span.line)o<<"  ; line "<<x.span.line;out_.disassembly.push_back(o.str());}};
        add(out_.init);add(out_.start);for(const auto&[name,f]:out_.functions){(void)name;add(f.body);for(const auto&d:f.defaults)if(d)add(*d);}for(const auto&t:out_.tests)add(t.second);
    }
};

class BytecodeVM {
public:
    BytecodeVM(Program& p, BytecodeProgram& code, Runtime& host, std::uint64_t stepLimit=100000000, std::size_t maxDepth=256)
        : program_(p), code_(code), host_(host), stepLimit_(stepLimit), maxDepth_(maxDepth) {
        globals_.values["PI"]={3.14159265358979323846,"real",true};
        globals_.values["TAU"]={6.28318530717958647692,"real",true};
        globals_.values["E"]={2.71828182845904523536,"real",true};
        globals_.values["INF"]={std::numeric_limits<double>::infinity(),"real",true};
        globals_.values["NAN"]={std::numeric_limits<double>::quiet_NaN(),"real",true};
    }
    void run() {
        steps_=0; callDepth_=0;
        execute(code_.init,globals_);
        if(!program_.hasStart)throw std::runtime_error("error[ANI-P003]: entry module has no start block");
        Env start(&globals_); execute(code_.start,start);
    }
    int runTests() {
        int pass=0,fail=0;
        for(const auto& [label,chunk] : code_.tests) {
            try { Env fresh; fresh.values=globals_.values; execute(code_.init,fresh); Env local(&fresh); execute(chunk,local);std::cout<<"PASS "<<label<<"\n";++pass; }
            catch(const std::exception& e){std::cerr<<"FAIL "<<label<<": "<<e.what()<<"\n";++fail;}
        }
        std::cout<<"\n"<<pass<<" passed, "<<fail<<" failed\n";return fail?1:0;
    }
    const std::vector<std::string>& disassembly() const { return code_.disassembly; }
private:
    Program& program_;BytecodeProgram& code_;Runtime& host_;Env globals_;std::uint64_t stepLimit_,steps_=0;std::size_t maxDepth_,callDepth_=0;

    static std::string mapKey(const Value& v) { return valueType(v)+":"+display(v); }
    static bool equals(const Value&a,const Value&b) {
        if(a.index()==b.index()) {
            if(std::holds_alternative<std::monostate>(a))return true;
            if(auto p=std::get_if<std::int64_t>(&a))return *p==std::get<std::int64_t>(b);
            if(auto p=std::get_if<double>(&a))return *p==std::get<double>(b);
            if(auto p=std::get_if<bool>(&a))return *p==std::get<bool>(b);
            if(auto p=std::get_if<std::string>(&a))return *p==std::get<std::string>(b);
            if(auto p=std::get_if<RangeValue>(&a)){auto q=std::get<RangeValue>(b);return p->start==q.start&&p->end==q.end&&p->inclusive==q.inclusive;}
            if(auto p=std::get_if<EnumValue>(&a)){auto q=std::get<EnumValue>(b);return p->typeName==q.typeName&&p->caseName==q.caseName;}
            return display(a)==display(b);
        }
        if(numericType(a)&&numericType(b))return numeric(a)==numeric(b);
        return false;
    }
    static bool typeAccepts(const std::string& expected,const Value& v) {
        if(expected.empty()||expected=="unknown"||expected=="value"||expected=="maybe value")return true;
        if(expected=="real"&&std::holds_alternative<std::int64_t>(v))return true;
        if(expected.rfind("maybe ",0)==0)return std::holds_alternative<std::monostate>(v)||expected.substr(6)==valueType(v);
        if(expected.rfind("list of ",0)==0)return std::holds_alternative<std::shared_ptr<ListValue>>(v);
        if(expected.rfind("map of ",0)==0||expected=="map")return std::holds_alternative<std::shared_ptr<MapValue>>(v);
        if(std::holds_alternative<std::shared_ptr<RecordValue>>(v))return expected==valueType(v);
        return expected==valueType(v);
    }
    static Value widenFor(const std::string& expected,Value v) { if(expected=="real"&&std::holds_alternative<std::int64_t>(v))return static_cast<double>(std::get<std::int64_t>(v));return v; }
    Value pop(std::vector<Value>& stack,const BCInstruction& ins) { if(stack.empty())throw std::runtime_error("error[ANI-I001]: bytecode stack underflow at line "+std::to_string(ins.span.line));Value v=std::move(stack.back());stack.pop_back();return v; }
    Slot* findSlot(Env* env,const std::string& n) { return env?env->find(n):nullptr; }
    void unwindScopes(Env*& env,std::vector<std::unique_ptr<Env>>& scopes,int targetDepth) {
        if(targetDepth<0)targetDepth=0;
        while(static_cast<int>(scopes.size())>targetDepth){env=env->parent;scopes.pop_back();}
    }
    Value call(const BCInstruction& ins,const std::vector<Value>& args,Env* caller) {
        (void)caller;
        auto fi=code_.functions.find(ins.name);
        if(fi==code_.functions.end())return host_.invokeBuiltin(ins.name,args,ins.span.line);
        const auto& fn=fi->second;const auto& decl=*fn.declaration;
        if(++callDepth_>maxDepth_){--callDepth_;throw std::runtime_error("error[ANI-R006]: call depth limit exceeded");}
        struct Guard{std::size_t& n;~Guard(){--n;}} guard{callDepth_};
        std::vector<std::optional<Value>> ordered(decl.params.size());std::size_t pos=0;
        for(std::size_t i=0;i<args.size();++i){
            const std::string nm=i<ins.argNames.size()?ins.argNames[i]:std::string{};std::size_t idx=0;
            if(!nm.empty()){auto it=std::find_if(decl.params.begin(),decl.params.end(),[&](const Param& p){return p.name==nm;});if(it==decl.params.end())throw std::runtime_error("error[ANI-T025]: unknown named argument '"+nm+"'");idx=static_cast<std::size_t>(it-decl.params.begin());if(ordered[idx])throw std::runtime_error("error[ANI-T026]: argument supplied twice for '"+nm+"'");}
            else {while(pos<ordered.size()&&ordered[pos])++pos;if(pos>=ordered.size())throw std::runtime_error("error[ANI-T003]: too many arguments to '"+ins.name+"'");idx=pos++;}
            ordered[idx]=args[i];
        }
        Env local(&globals_);
        for(std::size_t i=0;i<decl.params.size();++i) {
            const auto& p=decl.params[i];
            if(ordered[i])local.values[p.name]={widenFor(p.type,*ordered[i]),p.type,false};
            else if(i<fn.defaults.size()&&fn.defaults[i])local.values[p.name]={execute(*fn.defaults[i],local),p.type,false};
            else throw std::runtime_error("error[ANI-T004]: missing argument '"+p.name+"' for '"+ins.name+"'");
        }
        return execute(fn.body,local);
    }
    Value execute(const BytecodeChunk& chunk,Env& root) {
        std::vector<Value> stack;stack.reserve(64);std::vector<std::unique_ptr<Env>> scopes;Env* env=&root;std::size_t ip=0;
        auto jump=[&](const BCInstruction& ins){unwindScopes(env,scopes,ins.b);if(ins.a<0||static_cast<std::size_t>(ins.a)>chunk.code.size())throw std::runtime_error("error[ANI-I001]: invalid bytecode jump");ip=static_cast<std::size_t>(ins.a);};
        while(ip<chunk.code.size()) {
            if(++steps_>stepLimit_)throw std::runtime_error("error[ANI-R007]: instruction budget exceeded; use --step-limit to change the limit");
            const auto& ins=chunk.code[ip++];
            try {
                switch(ins.op) {
                    case BCOp::Push: stack.push_back(ins.value);break;
                    case BCOp::Load:{auto* slot=findSlot(env,ins.name);if(!slot)throw std::runtime_error("error[ANI-T001]: unknown name '"+ins.name+"'");stack.push_back(slot->value);break;}
                    case BCOp::Declare:{Value v=pop(stack,ins);if(env->local(ins.name))throw std::runtime_error("error[ANI-T009]: '"+ins.name+"' already exists in this scope");if(!typeAccepts(ins.name2,v))throw std::runtime_error("error[ANI-T002]: expected '"+ins.name2+"', found '"+valueType(v)+"'");v=widenFor(ins.name2,std::move(v));env->values[ins.name]={std::move(v),ins.name2,ins.a!=0};break;}
                    case BCOp::Store:{Value v=pop(stack,ins);auto*slot=findSlot(env,ins.name);if(!slot)throw std::runtime_error("error[ANI-T001]: unknown name '"+ins.name+"'");if(slot->fixed)throw std::runtime_error("error[ANI-T019]: cannot assign to fixed '"+ins.name+"'");if(!typeAccepts(slot->type,v))throw std::runtime_error("error[ANI-T002]: assignment expects '"+slot->type+"', found '"+valueType(v)+"'");slot->value=widenFor(slot->type,std::move(v));break;}
                    case BCOp::Pop:(void)pop(stack,ins);break;
                    case BCOp::Announce:{Value v=pop(stack,ins);std::cout<<display(v)<<"\n";break;}
                    case BCOp::ToText:{Value v=pop(stack,ins);stack.emplace_back(display(v));break;}
                    case BCOp::NegW:{Value v=pop(stack,ins);auto p=std::get_if<std::int64_t>(&v);if(!p||*p==std::numeric_limits<std::int64_t>::min())throw std::runtime_error("error[ANI-R002]: whole negation overflow/type error");stack.emplace_back(-*p);break;}
                    case BCOp::NegR:{Value v=pop(stack,ins);stack.emplace_back(-numeric(v));break;}
                    case BCOp::Not:{Value v=pop(stack,ins);stack.emplace_back(!truthOf(v));break;}
                    case BCOp::AddW:case BCOp::SubW:case BCOp::MulW:case BCOp::DivW:case BCOp::ModW:case BCOp::AddR:case BCOp::SubR:case BCOp::MulR:case BCOp::DivR:case BCOp::ModR: {
                        Value rhs=pop(stack,ins),lhs=pop(stack,ins);std::string op;switch(ins.op){case BCOp::AddW:case BCOp::AddR:op="+";break;case BCOp::SubW:case BCOp::SubR:op="-";break;case BCOp::MulW:case BCOp::MulR:op="*";break;case BCOp::DivW:case BCOp::DivR:op="/";break;default:op="%";break;}
                        bool real=ins.op==BCOp::AddR||ins.op==BCOp::SubR||ins.op==BCOp::MulR||ins.op==BCOp::DivR||ins.op==BCOp::ModR;if(real){lhs=numeric(lhs);rhs=numeric(rhs);}stack.push_back(host_.applyBinary(op,lhs,rhs));break;
                    }
                    case BCOp::Compare:{Value rhs=pop(stack,ins),lhs=pop(stack,ins);if(ins.name=="<"||ins.name=="<="||ins.name==">"||ins.name==">="){if(ins.name2=="whole"){/* retain whole precision; generic operator already compares as whole when both are integers */} }stack.push_back(host_.applyBinary(ins.name,lhs,rhs));break;}
                    case BCOp::Concat:{Value rhs=pop(stack,ins),lhs=pop(stack,ins);stack.emplace_back(display(lhs)+display(rhs));break;}
                    case BCOp::Fallback:{Value rhs=pop(stack,ins),lhs=pop(stack,ins);stack.push_back(std::holds_alternative<std::monostate>(lhs)?rhs:lhs);break;}
                    case BCOp::IsNone:{Value lhs=pop(stack,ins);bool yes=std::holds_alternative<std::monostate>(lhs);if(ins.name=="not")yes=!yes;stack.emplace_back(yes);break;}
                    case BCOp::Jump:jump(ins);break;
                    case BCOp::JumpFalse:{Value v=pop(stack,ins);if(!truthOf(v))jump(ins);break;}
                    case BCOp::JumpTrue:{Value v=pop(stack,ins);if(truthOf(v))jump(ins);break;}
                    case BCOp::EnterScope:scopes.push_back(std::make_unique<Env>(env));env=scopes.back().get();break;
                    case BCOp::ExitScope:if(scopes.empty())throw std::runtime_error("error[ANI-I001]: scope stack underflow");env=env->parent;scopes.pop_back();break;
                    case BCOp::MakeList:{auto list=std::make_shared<ListValue>();list->values.resize(static_cast<std::size_t>(ins.a));for(int i=ins.a-1;i>=0;--i)list->values[static_cast<std::size_t>(i)]=pop(stack,ins);stack.emplace_back(list);break;}
                    case BCOp::MakeMap:{auto map=std::make_shared<MapValue>();std::vector<Value> vals(static_cast<std::size_t>(ins.a)*2);for(int i=static_cast<int>(vals.size())-1;i>=0;--i)vals[static_cast<std::size_t>(i)]=pop(stack,ins);for(int i=0;i<ins.a;++i){Value key=vals[static_cast<std::size_t>(i*2)];Value val=vals[static_cast<std::size_t>(i*2+1)];if(!std::holds_alternative<std::int64_t>(key)&&!std::holds_alternative<std::string>(key)&&!std::holds_alternative<bool>(key)&&!std::holds_alternative<EnumValue>(key))throw std::runtime_error("error[ANI-T002]: map key must be whole, text, truth or enum");auto it=std::find_if(map->entries.begin(),map->entries.end(),[&](auto& e){return mapKey(e.first)==mapKey(key);});if(it==map->entries.end())map->entries.emplace_back(std::move(key),std::move(val));else it->second=std::move(val);}stack.emplace_back(map);break;}
                    case BCOp::MakeRecord: {
                        auto def=code_.records.find(ins.name);if(def==code_.records.end())throw std::runtime_error("error[ANI-C008]: unknown record type '"+ins.name+"'");
                        std::vector<Value> args(static_cast<std::size_t>(ins.a));for(int i=ins.a-1;i>=0;--i)args[static_cast<std::size_t>(i)]=pop(stack,ins);
                        const auto& fields=def->second.fields;std::vector<std::optional<Value>> supplied(fields.size());std::size_t positional=0;
                        for(std::size_t i=0;i<args.size();++i){std::size_t fi=0;const std::string nm=i<ins.argNames.size()?ins.argNames[i]:std::string{};if(!nm.empty()){auto it=std::find_if(fields.begin(),fields.end(),[&](const RecordFieldDecl& f){return f.name==nm;});if(it==fields.end())throw std::runtime_error("error[ANI-T025]: unknown record field argument '"+nm+"'");fi=static_cast<std::size_t>(it-fields.begin());}else{while(positional<supplied.size()&&supplied[positional])++positional;if(positional>=fields.size())throw std::runtime_error("error[ANI-T003]: too many record constructor arguments");fi=positional++;}if(supplied[fi])throw std::runtime_error("error[ANI-T026]: record field supplied twice");supplied[fi]=std::move(args[i]);}
                        auto record=std::make_shared<RecordValue>();record->typeName=ins.name;
                        for(std::size_t i=0;i<fields.size();++i){Value v;if(supplied[i])v=std::move(*supplied[i]);else if(fields[i].defaultValue)v=std::visit([](const auto& x)->Value{return x;},*fields[i].defaultValue);else if(!fields[i].defaultEnum.empty()){auto en=code_.enums.find(fields[i].defaultEnum);if(en==code_.enums.end())throw std::runtime_error("error[ANI-I001]: record default enum metadata missing for '"+fields[i].name+"'");auto ec=std::find_if(en->second.cases.begin(),en->second.cases.end(),[&](const EnumCaseDecl& x){return x.name==fields[i].defaultEnumCase;});if(ec==en->second.cases.end())throw std::runtime_error("error[ANI-I001]: record default enum case missing for '"+fields[i].name+"'");v=EnumValue{fields[i].defaultEnum,ec->name,ec->value};}else throw std::runtime_error("error[ANI-T004]: missing required record field '"+fields[i].name+"'");if(!typeAccepts(fields[i].type,v))throw std::runtime_error("error[ANI-T002]: record field '"+fields[i].name+"' expects "+fields[i].type+", found "+valueType(v));v=widenFor(fields[i].type,std::move(v));record->fields.emplace_back(fields[i].name,std::move(v));}
                        stack.emplace_back(record);break;
                    }
                    case BCOp::GetField: {
                        Value base=pop(stack,ins);auto record=std::get_if<std::shared_ptr<RecordValue>>(&base);if(!record||!*record||(*record)->typeName!=ins.name2)throw std::runtime_error("error[ANI-T020]: field access expected record '"+ins.name2+"'");auto f=std::find_if((*record)->fields.begin(),(*record)->fields.end(),[&](const auto& item){return item.first==ins.name;});if(f==(*record)->fields.end())throw std::runtime_error("error[ANI-T020]: record field not found '"+ins.name+"'");stack.push_back(f->second);break;
                    }
                    case BCOp::SetField: {
                        Value value=pop(stack,ins),base=pop(stack,ins);auto record=std::get_if<std::shared_ptr<RecordValue>>(&base);if(!record||!*record||(*record)->typeName!=ins.name2)throw std::runtime_error("error[ANI-T020]: field assignment expected record '"+ins.name2+"'");auto f=std::find_if((*record)->fields.begin(),(*record)->fields.end(),[&](const auto& item){return item.first==ins.name;});if(f==(*record)->fields.end())throw std::runtime_error("error[ANI-T020]: record field not found '"+ins.name+"'");auto fd=code_.records.find(ins.name2);std::string fieldType=valueType(value);if(fd!=code_.records.end()){auto fieldDef=std::find_if(fd->second.fields.begin(),fd->second.fields.end(),[&](const RecordFieldDecl& x){return x.name==ins.name;});if(fieldDef!=fd->second.fields.end()){fieldType=fieldDef->type;if(!typeAccepts(fieldType,value))throw std::runtime_error("error[ANI-T002]: field assignment type mismatch");}}f->second=widenFor(fieldType,std::move(value));break;
                    }
                    case BCOp::WithRecord: {
                        std::vector<Value> values(static_cast<std::size_t>(ins.a));for(int i=ins.a-1;i>=0;--i)values[static_cast<std::size_t>(i)]=pop(stack,ins);Value base=pop(stack,ins);auto source=std::get_if<std::shared_ptr<RecordValue>>(&base);if(!source||!*source||(*source)->typeName!=ins.name)throw std::runtime_error("error[ANI-T036]: with expected record '"+ins.name+"'");auto updated=std::make_shared<RecordValue>(**source);auto def=code_.records.find(ins.name);for(std::size_t i=0;i<values.size();++i){const auto name=i<ins.argNames.size()?ins.argNames[i]:std::string{};auto f=std::find_if(updated->fields.begin(),updated->fields.end(),[&](const auto& item){return item.first==name;});if(f==updated->fields.end())throw std::runtime_error("error[ANI-T020]: unknown updated field '"+name+"'");if(def!=code_.records.end()){auto fieldDef=std::find_if(def->second.fields.begin(),def->second.fields.end(),[&](const RecordFieldDecl& x){return x.name==name;});if(fieldDef!=def->second.fields.end()&&!typeAccepts(fieldDef->type,values[i]))throw std::runtime_error("error[ANI-T002]: update type mismatch for field '"+name+"'");if(fieldDef!=def->second.fields.end())values[i]=widenFor(fieldDef->type,std::move(values[i]));}f->second=std::move(values[i]);}stack.emplace_back(updated);break;
                    }
                    case BCOp::GetEnumMeta:{Value base=pop(stack,ins);auto e=std::get_if<EnumValue>(&base);if(!e||e->typeName!=ins.name2)throw std::runtime_error("error[ANI-T020]: enum metadata access has wrong value type");if(ins.name=="name")stack.emplace_back(e->caseName);else if(ins.name=="value")stack.emplace_back(e->number);else throw std::runtime_error("error[ANI-I001]: unknown enum metadata field");break;}
                    case BCOp::EnumAll:{auto en=code_.enums.find(ins.name);if(en==code_.enums.end())throw std::runtime_error("error[ANI-I001]: enum metadata missing");auto list=std::make_shared<ListValue>();for(const auto& item:en->second.cases)list->values.emplace_back(EnumValue{ins.name,item.name,item.value});stack.emplace_back(list);break;}
                    case BCOp::EnumFromName:{Value needle=pop(stack,ins);auto name=std::get_if<std::string>(&needle);if(!name)throw std::runtime_error("error[ANI-T002]: enum.from_name expects text");auto en=code_.enums.find(ins.name);if(en==code_.enums.end())throw std::runtime_error("error[ANI-I001]: enum metadata missing");auto it=std::find_if(en->second.cases.begin(),en->second.cases.end(),[&](const EnumCaseDecl& item){return item.name==*name;});if(it==en->second.cases.end())stack.emplace_back(std::monostate{});else stack.emplace_back(EnumValue{ins.name,it->name,it->value});break;}
                    case BCOp::Index:{Value index=pop(stack,ins),base=pop(stack,ins);if(auto l=std::get_if<std::shared_ptr<ListValue>>(&base)){auto ix=std::get_if<std::int64_t>(&index);if(!ix||*ix<0||static_cast<std::size_t>(*ix)>=(*l)->values.size())throw std::runtime_error("error[ANI-R003]: list index out of range");stack.push_back((*l)->values[static_cast<std::size_t>(*ix)]);}else if(auto t=std::get_if<std::string>(&base)){auto ix=std::get_if<std::int64_t>(&index);if(!ix||*ix<0||static_cast<std::size_t>(*ix)>=t->size())throw std::runtime_error("error[ANI-R003]: text index out of range");stack.emplace_back(std::string(1,(*t)[static_cast<std::size_t>(*ix)]));}else if(auto m=std::get_if<std::shared_ptr<MapValue>>(&base)){auto it=std::find_if((*m)->entries.begin(),(*m)->entries.end(),[&](auto& e){return mapKey(e.first)==mapKey(index);});if(it==(*m)->entries.end())throw std::runtime_error("error[ANI-R004]: map key not found");stack.push_back(it->second);}else throw std::runtime_error("value is not indexable");break;}
                    case BCOp::MapKeyAt:case BCOp::MapValueAt:{Value index=pop(stack,ins),base=pop(stack,ins);auto ix=std::get_if<std::int64_t>(&index);auto map=std::get_if<std::shared_ptr<MapValue>>(&base);if(!ix||!map||*ix<0||static_cast<std::size_t>(*ix)>=(*map)->entries.size())throw std::runtime_error("error[ANI-R003]: map iteration index out of range");stack.push_back(ins.op==BCOp::MapKeyAt?(*map)->entries[static_cast<std::size_t>(*ix)].first:(*map)->entries[static_cast<std::size_t>(*ix)].second);break;}
                    case BCOp::Slice:{Value end=pop(stack,ins),start=pop(stack,ins),base=pop(stack,ins);auto lo=std::get_if<std::int64_t>(&start),hi=std::get_if<std::int64_t>(&end);if(!lo||!hi||*lo<0||*hi<*lo)throw std::runtime_error("error[ANI-R003]: invalid slice bounds");std::size_t high=static_cast<std::size_t>(*hi)+(ins.name=="..="?1:0);if(auto l=std::get_if<std::shared_ptr<ListValue>>(&base)){if(high>(*l)->values.size())throw std::runtime_error("error[ANI-R003]: slice out of range");auto out=std::make_shared<ListValue>();out->values.assign((*l)->values.begin()+*lo,(*l)->values.begin()+high);stack.emplace_back(out);}else if(auto t=std::get_if<std::string>(&base)){if(high>t->size())throw std::runtime_error("error[ANI-R003]: slice out of range");stack.emplace_back(t->substr(static_cast<std::size_t>(*lo),high-static_cast<std::size_t>(*lo)));}else throw std::runtime_error("value is not sliceable");break;}
                    case BCOp::Length:{Value v=pop(stack,ins);if(auto l=std::get_if<std::shared_ptr<ListValue>>(&v))stack.emplace_back(static_cast<std::int64_t>((*l)->values.size()));else if(auto t=std::get_if<std::string>(&v))stack.emplace_back(static_cast<std::int64_t>(t->size()));else if(auto m=std::get_if<std::shared_ptr<MapValue>>(&v))stack.emplace_back(static_cast<std::int64_t>((*m)->entries.size()));else throw std::runtime_error("length requires list, map or text");break;}
                    case BCOp::MakeRange:{Value end=pop(stack,ins),start=pop(stack,ins);auto lo=std::get_if<std::int64_t>(&start),hi=std::get_if<std::int64_t>(&end);if(!lo||!hi)throw std::runtime_error("range bounds must be whole");stack.emplace_back(RangeValue{*lo,*hi,ins.name=="..=",false});break;}
                    case BCOp::RangeStart:{Value v=pop(stack,ins);auto r=std::get_if<RangeValue>(&v);if(!r)throw std::runtime_error("internal bytecode error: range expected");stack.emplace_back(r->start);break;}
                    case BCOp::RangeContinue:{Value range=pop(stack,ins),idx=pop(stack,ins);auto r=std::get_if<RangeValue>(&range);auto i=std::get_if<std::int64_t>(&idx);stack.emplace_back(r&&i&&!r->exhausted&&(r->inclusive?*i<=r->end:*i<r->end));break;}
                    case BCOp::RangeAdvance:{auto*ix=findSlot(env,ins.name);auto*rn=findSlot(env,ins.name2);if(!ix||!rn)throw std::runtime_error("internal range variable missing");auto i=std::get_if<std::int64_t>(&ix->value);auto r=std::get_if<RangeValue>(&rn->value);if(!i||!r)throw std::runtime_error("internal range state invalid");const bool atEnd=r->inclusive?*i>=r->end:(*i==std::numeric_limits<std::int64_t>::max()||*i+1>=r->end);if(atEnd||*i==std::numeric_limits<std::int64_t>::max())r->exhausted=true;else ++(*i);break;}
                    case BCOp::Call:{std::vector<Value> args(static_cast<std::size_t>(ins.a));for(int i=ins.a-1;i>=0;--i)args[static_cast<std::size_t>(i)]=pop(stack,ins);stack.push_back(call(ins,args,env));break;}
                    case BCOp::Return:return stack.empty()?Value{}:pop(stack,ins);
                    case BCOp::Fail:{Value v=pop(stack,ins);throw std::runtime_error("error[ANI-R010]: "+display(v));}
                    case BCOp::Halt:return stack.empty()?Value{}:stack.back();
                }
            } catch(const std::exception& ex) {
                std::string msg=ex.what();
                if(msg.find(" at ")==std::string::npos && ins.span.line)msg+="\n  at "+chunk.name+":"+std::to_string(ins.span.line)+":"+std::to_string(ins.span.col);
                throw std::runtime_error(msg);
            }
        }
        return stack.empty()?Value{}:stack.back();
    }
};
