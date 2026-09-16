#include "analyzer.h"
#include "lumora.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stack>
#include <vector>

namespace {
struct Finding { std::string category, pattern, evidence; int line = 0; };
struct FunctionInfo { std::string name; int line = 0; std::set<std::string> calls; };
struct ModuleInfo { std::string path; std::string source; std::vector<std::string> requires; };
struct Result {
    std::string original, strings, constants, normalized, reconstructed;
    std::vector<std::string> trace, ir;
    std::size_t instructions = 0; bool limited = false; std::string limitReason;
    std::vector<Finding> findings;
    std::vector<std::string> decodedStrings;
    std::vector<std::string> decodedBytes;
    std::vector<std::string> recoveredConstants;
    std::vector<std::pair<std::string,std::string>> edges;
    std::vector<FunctionInfo> functions;
    std::vector<ModuleInfo> modules;
    std::vector<std::vector<std::string>> cycles;
};

std::string readText(const std::string& path) { std::ifstream f(path); if (!f) throw std::runtime_error("cannot open input: " + path); std::ostringstream s; s << f.rdbuf(); return s.str(); }
void writeText(const std::filesystem::path& path, const std::string& text) { std::ofstream f(path); if (!f) throw std::runtime_error("cannot write: " + path.string()); f << text; }
std::string replaceAll(std::string s, const std::string& a, const std::string& b) { size_t p=0; while ((p=s.find(a,p))!=std::string::npos) { s.replace(p,a.size(),b); p += b.size(); } return s; }
int lineAt(const std::string& s, size_t p) { return int(std::count(s.begin(), s.begin()+std::min(p,s.size()), '\n'))+1; }
std::string trim(std::string s) { size_t a=0,b=s.size(); while(a<b&&std::isspace((unsigned char)s[a]))++a; while(b>a&&std::isspace((unsigned char)s[b-1]))--b; return s.substr(a,b-a); }
std::string json(const std::string& s) { return jsonEscape(s); }
std::string luaQuote(const std::string& s) { std::string out="\""; for(unsigned char c:s){if(c=='\\'||c=='\"')out+='\\'; if(c=='\n')out+="\\n"; else if(c=='\r')out+="\\r"; else if(c=='\t')out+="\\t"; else if(c<32){char buf[5];std::snprintf(buf,sizeof(buf),"\\%03u",unsigned(c));out+=buf;} else out+=char(c);} return out+"\""; }
std::string array(const std::vector<std::string>& v) { std::string o="["; for(size_t i=0;i<v.size();++i){if(i)o+=",";o+=json(v[i]);}return o+"]"; }
std::string hexBytes(const std::string& value) { static const char* digits="0123456789abcdef"; std::string out; for(unsigned char c:value){out+=digits[c>>4];out+=digits[c&15];} return out; }
bool printable(const std::string& value) { for(unsigned char c:value) if(c<32 || c>126) return false; return !value.empty(); }
std::string diffJson(const std::string& before, const std::string& after) {
    auto lines=[](const std::string& s){std::vector<std::string> v;std::stringstream ss(s);std::string x;while(std::getline(ss,x))v.push_back(x);return v;};
    const auto a=lines(before), b=lines(after); size_t common=0; for(size_t i=0;i<std::min(a.size(),b.size());++i) if(a[i]==b[i]) ++common;
    return "{\"beforeBytes\":"+std::to_string(before.size())+",\"afterBytes\":"+std::to_string(after.size())+",\"beforeLines\":"+std::to_string(a.size())+",\"afterLines\":"+std::to_string(b.size())+",\"changedLines\":"+std::to_string(std::max(a.size(),b.size())-common)+",\"commonLeadingLines\":"+std::to_string(common)+"}";
}

double evalSimple(const std::string& expression, bool& ok) {
    std::string s; for(char c: expression) if(!std::isspace((unsigned char)c)) s+=c;
    while (s.size() >= 2 && s.front() == '(' && s.back() == ')') s = s.substr(1, s.size() - 2);
    size_t pos=0;
    std::function<double()> expr, term, factor;
    factor=[&](){ bool neg=false; if(pos<s.size()&&(s[pos]=='+'||s[pos]=='-')){neg=s[pos++]=='-';} double n=0; bool any=false; while(pos<s.size()&&std::isdigit((unsigned char)s[pos])){n=n*10+(s[pos++]-'0');any=true;} if(!any){ok=false;return 0.0;} return neg?-n:n; };
    term=[&](){double n=factor();while(ok&&pos<s.size()&&(s[pos]=='*'||s[pos]=='/')){char op=s[pos++];double r=factor();if(op=='*')n*=r;else if(r!=0)n/=r;else ok=false;}return n;};
    expr=[&](){double n=term();while(ok&&pos<s.size()&&(s[pos]=='+'||s[pos]=='-')){char op=s[pos++];double r=term();n=op=='+'?n+r:n-r;}return n;};
    double value=expr(); ok=ok&&pos==s.size(); return value;
}

std::string decodeStrings(const std::string& input, std::vector<std::string>& decodedStrings, std::vector<std::string>& decodedBytes) {
    std::string out=input;
    std::regex chars(R"(string\.char\s*\(([^\)]*)\))");
    std::smatch m; std::string result; std::string::const_iterator searchStart=out.cbegin();
    while(std::regex_search(searchStart,out.cend(),m,chars)) {
        result.append(searchStart,m[0].first); std::stringstream ss(m[1].str()); std::string part, value; bool valid=true;
        while(std::getline(ss,part,',')){bool ok=true;double n=evalSimple(trim(part),ok);if(!ok||n<0||n>255){valid=false;break;}value.push_back(char(int(n)));}
        if(valid&&!value.empty()){if(printable(value)) decodedStrings.push_back(value); else decodedBytes.push_back(hexBytes(value));result+=luaQuote(value);}
        else result+=m[0].str(); searchStart=m[0].second;
    }
    result.append(searchStart,out.cend());
    return result;
}

std::string foldConstants(const std::string& input, std::vector<std::string>& recoveredConstants) {
    std::string out=input; std::regex expr(R"(\(?\s*[0-9]+(?:\s*[+\-*/]\s*[0-9]+)+\s*\)?)"); std::smatch m; std::string result; auto it=out.cbegin();
    while(std::regex_search(it,out.cend(),m,expr)){result.append(it,m[0].first);bool ok=true;double n=evalSimple(m[0].str(),ok);if(ok&&std::isfinite(n)){std::ostringstream v;v<<((n==int(n))?std::to_string(int(n)):std::to_string(n));result+=v.str();recoveredConstants.push_back(v.str());}else result+=m[0].str();it=m[0].second;} result.append(it,out.cend()); return result;
}

std::string renameSymbols(const std::string& input) {
    std::string out=input; std::map<std::string,std::string> names; std::regex decl(R"(\blocal\s+(?:function\s+)?(_0x[0-9a-fA-F]+|[A-Za-z]+_[0-9]+)\b)");
    for(std::sregex_iterator i(out.begin(),out.end(),decl),e;i!=e;++i){std::string old=(*i)[1].str();if(!names.count(old))names[old]="local_"+std::to_string(names.size()+1);}
    for(const auto& p:names){std::regex word("\\b"+p.first+"\\b");out=std::regex_replace(out,word,p.second);} return out;
}

std::string decodeTableConcat(const std::string& input, std::vector<std::string>& decodedStrings) {
    std::regex table(R"(table\.concat\s*\(\s*\{\s*((?:"[^"]*"\s*,?\s*)+)\}\s*\))"); std::string out; std::smatch m; auto it=input.cbegin();
    while(std::regex_search(it,input.cend(),m,table)){out.append(it,m[0].first);std::string joined;std::regex item("\\\"([^\\\"]*)\\\"");for(std::sregex_iterator i(m[1].first,m[1].second,item),e;i!=e;++i)joined+=(*i)[1].str();decodedStrings.push_back(joined);out+=luaQuote(joined);it=m[0].second;}out.append(it,input.cend());return out;
}

void collectFindings(Result& r) {
    const std::vector<std::pair<std::string,std::string>> rules={{"dynamic-code","loadstring"},{"dynamic-code","load("},{"encoded-string","string.char"},{"encoded-string","table.concat"},{"bit-decoder","bit32."},{"network","request"},{"network","HttpGet"},{"filesystem","writefile"},{"filesystem","readfile"},{"environment","getfenv"},{"environment","setfenv"},{"process","os.execute"}};
    for(const auto& rule:rules){size_t p=0;while((p=r.original.find(rule.second,p))!=std::string::npos){Finding f{rule.first,rule.second,rule.second};f.line=lineAt(r.original,p);r.findings.push_back(f);p+=rule.second.size();}}
}

void collectFunctions(Result& r) {
    std::regex fn(R"((?:local\s+)?function\s+([A-Za-z_][A-Za-z0-9_]*))"), call(R"(([A-Za-z_][A-Za-z0-9_]*)\s*\()") ; std::stringstream ss(r.reconstructed);std::string line;int n=0;FunctionInfo* current=nullptr;
    while(std::getline(ss,line)){++n;std::smatch m;if(std::regex_search(line,m,fn)){r.functions.push_back({m[1].str(),n,{}});current=&r.functions.back();}for(std::sregex_iterator i(line.begin(),line.end(),call),e;i!=e;++i){std::string c=(*i)[1].str();if(c!="if"&&c!="for"&&c!="while"&&c!="function"){std::string from=current?current->name:"<main>";r.edges.emplace_back(from,"call:"+c);if(current)current->calls.insert(c);}}}
}

std::string canonicalPath(const std::filesystem::path& p){std::error_code e;auto c=std::filesystem::weakly_canonical(p,e);return(e?p.lexically_normal():c).generic_string();}
std::string resolve(const std::string& parent,const std::string& req){if(req.empty()||req[0]!='.')return{};auto b=std::filesystem::path(parent).parent_path()/req;std::vector<std::filesystem::path> c={b,b.string()+".luau",b.string()+".lua",b/"init.luau",b/"init.lua"};for(auto&p:c){std::error_code e;if(std::filesystem::is_regular_file(p,e)&&!e)return canonicalPath(p);}return{};}
void collectModules(Result& r,const std::string& path,std::set<std::string>& seen){std::string id=canonicalPath(path);if(seen.count(id))return;seen.insert(id);ModuleInfo m{id,readText(id),{}};std::regex req(R"(require\s*\(\s*["']([^"']+)["']\s*\))");for(std::sregex_iterator i(m.source.begin(),m.source.end(),req),e;i!=e;++i){std::string target=resolve(id,(*i)[1].str());m.requires.push_back(target.empty()?(*i)[1].str():target);if(!target.empty())collectModules(r,target,seen);}r.modules.push_back(std::move(m));}
void cycles(Result& r){std::map<std::string,std::vector<std::string>> a;for(const auto&m:r.modules)for(const auto&t:m.requires)if(t[0]=='/')a[m.path].push_back(t);std::map<std::string,int> color;std::vector<std::string> stack;std::function<void(const std::string&)> dfs=[&](const std::string&n){color[n]=1;stack.push_back(n);for(auto&t:a[n]){if(!color[t])dfs(t);else if(color[t]==1){auto b=std::find(stack.begin(),stack.end(),t);if(b!=stack.end()){std::vector<std::string> c(b,stack.end());c.push_back(t);r.cycles.push_back(c);}}}stack.pop_back();color[n]=2;};for(auto&m:r.modules)if(!color[m.path])dfs(m.path);}

std::string summaryValue(const std::string& s) { std::string v=trim(s); if(v.size()>160) v=v.substr(0,160)+"…"; return v; }
std::string eventJson(const std::string& kind, int line, const std::string& detail) { return "{\"kind\":"+json(kind)+",\"line\":"+std::to_string(line)+",\"detail\":"+json(detail)+"}"; }
void collectSemanticTrace(Result& r, const AnalyzerOptions& o) {
    std::stringstream ss(r.reconstructed); std::string line; int n=0;
    std::regex index(R"(([A-Za-z_][A-Za-z0-9_]*)\s*\[([^\]]+)\])"), call(R"(([A-Za-z_][A-Za-z0-9_.:]*)\s*\(([^\)]*)\))"), global(R"(\b([A-Z][A-Za-z0-9_]*)\b)");
    auto add=[&](const std::string& e){ if(r.trace.size()<o.eventLimit) r.trace.push_back(e); else {r.limited=true;r.limitReason="event-limit";} };
    while(std::getline(ss,line)) { ++n; if(r.instructions++>=o.instructionLimit){r.limited=true;r.limitReason="instruction-limit";break;}
        if(o.traceVm && (line.find("while")!=std::string::npos || line.find("repeat")!=std::string::npos || line.find("Yt")!=std::string::npos)) add(eventJson("vm-step",n,summaryValue(line)));
        if(o.traceIndexes) for(std::sregex_iterator i(line.begin(),line.end(),index),e;i!=e;++i) { add(eventJson("table-index",n,(*i)[1].str()+"["+(*i)[2].str()+"]")); r.ir.push_back("pc="+std::to_string(n)+" op=GETTABLE object="+(*i)[1].str()+" key="+(*i)[2].str()); }
        if(o.traceCalls) for(std::sregex_iterator i(line.begin(),line.end(),call),e;i!=e;++i) { add(eventJson("call",n,(*i)[1].str()+"("+summaryValue((*i)[2].str())+")")); r.ir.push_back("pc="+std::to_string(n)+" op=CALL callee="+(*i)[1].str()+" args="+summaryValue((*i)[2].str())); }
        if(o.traceGlobals) for(std::sregex_iterator i(line.begin(),line.end(),global),e;i!=e;++i) { add(eventJson("global-read",n,(*i)[1].str())); r.ir.push_back("pc="+std::to_string(n)+" op=GETGLOBAL name="+(*i)[1].str()); }
        for(const char* cap: {"request","http_request","syn.request","HttpGet","HttpPost","readfile","writefile","loadfile","loadstring","require"}) if(line.find(cap)!=std::string::npos) add(eventJson("blocked-capability",n,cap));
    }
}
std::string jsonLines(const std::vector<std::string>& lines, std::size_t limit) { std::string o; for(const auto& l:lines){ if(o.size()+l.size()+1>limit) break; o+=l+"\n"; } return o; }
std::string jsonArrayLines(const std::vector<std::string>& lines, std::size_t limit) { std::string o="["; bool first=true; for(const auto& l:lines){ if(o.size()+l.size()+2>limit) break; if(!first)o+=","; o+=l; first=false; } return o+"]\n"; }
std::string irJson(const Result& r) { std::string o="["; for(size_t i=0;i<r.ir.size();++i){ if(i)o+=","; o+="{\"text\":"+json(r.ir[i])+",\"source\":\"static-trace\"}"; } return o+"]"; }
std::string pseudo(const Result& r) { std::string o="-- Lumora reconstructed pseudocode (inferred; not original source)\n"; for(const auto& x:r.ir) o+="-- "+x+"\n"; o+="-- unexecuted paths and blocked capabilities are not inferred\n"; return o; }

std::string reportJson(const Result& r) {
    std::string o="{\"summary\":{";o+="\"input\":"+json(r.original.substr(0,std::min<size_t>(80,r.original.size())))+",\"functions\":"+std::to_string(r.functions.size())+",\"modules\":"+std::to_string(r.modules.size())+",\"cycles\":"+std::to_string(r.cycles.size())+"},\"findings\":[";
    for(size_t i=0;i<r.findings.size();++i){if(i)o+=",";const auto&f=r.findings[i];o+="{\"category\":"+json(f.category)+",\"pattern\":"+json(f.pattern)+",\"evidence\":"+json(f.evidence)+",\"line\":"+std::to_string(f.line)+"}";}o+="] ,\"decodedStrings\":"+array(r.decodedStrings)+",\"decodedBytesHex\":"+array(r.decodedBytes)+",\"recoveredConstants\":"+array(r.recoveredConstants)+",\"functions\":[";
    for(size_t i=0;i<r.functions.size();++i){if(i)o+=",";o+="{\"name\":"+json(r.functions[i].name)+",\"line\":"+std::to_string(r.functions[i].line)+",\"calls\":"+array(std::vector<std::string>(r.functions[i].calls.begin(),r.functions[i].calls.end()))+"}";}o+="] ,\"edges\":[";
    for(size_t i=0;i<r.edges.size();++i){if(i)o+=",";o+="{\"from\":"+json(r.edges[i].first)+",\"to\":"+json(r.edges[i].second)+"}";}o+="] ,\"moduleEdges\":[";
    for(const auto&m:r.modules)for(const auto&t:m.requires){if(o.back()!='[')o+=",";o+="{\"from\":"+json(m.path)+",\"to\":"+json(t)+"}";}o+="] ,\"hasCycle\":";o+=(r.cycles.empty()?"false":"true");o+=",\"cycles\":[";
    for(size_t i=0;i<r.cycles.size();++i){if(i)o+=",";o+=array(r.cycles[i]);}o+="] ,\"analysis\":{";o+="\"traceEvents\":"+std::to_string(r.trace.size())+",\"irRecords\":"+std::to_string(r.ir.size())+",\"instructions\":"+std::to_string(r.instructions)+",\"limited\":"+(r.limited?"true":"false")+",\"limitReason\":"+json(r.limitReason)+"}}";return o;
}

}

Result analyze(const std::string& path, const AnalyzerOptions& o){Result r;r.original=readText(path);r.strings=decodeStrings(r.original,r.decodedStrings,r.decodedBytes);r.strings=decodeTableConcat(r.strings,r.decodedStrings);r.constants=foldConstants(r.strings,r.recoveredConstants);r.normalized=renameSymbols(r.constants);r.reconstructed=r.normalized;collectFindings(r);collectFunctions(r);std::set<std::string> seen;collectModules(r,path,seen);cycles(r);if(o.deterministic||o.traceGlobals||o.traceIndexes||o.traceCalls||o.traceVm)collectSemanticTrace(r,o);return r;}

void printAnalyzerHelp(){std::cout<<"usage: lumora <inspect|deobfuscate|report> input.lua [--out directory] [--json]\n";}
int runAnalyzerCommand(const std::string& command,const AnalyzerOptions& options){try{Result r=analyze(options.input,options);if(command=="inspect"||command=="report"||options.json){std::cout<<reportJson(r)<<"\n";}if(command=="deobfuscate"){std::filesystem::path out=options.outputDir.empty()?std::filesystem::path(options.input).parent_path()/"lumora-analysis":std::filesystem::path(options.outputDir);std::filesystem::create_directories(out);writeText(out/"00-original.luau",r.original);writeText(out/"01-strings-decoded.luau",r.strings);writeText(out/"02-constants-folded.luau",r.constants);writeText(out/"03-symbols-renamed.luau",r.normalized);writeText(out/"04-reconstructed.luau",r.reconstructed);writeText(out/"trace.jsonl",jsonLines(r.trace,options.outputLimit));writeText(out/"calls.json",jsonArrayLines(r.trace,options.outputLimit));writeText(out/"blocked-capabilities.json",jsonArrayLines(r.trace,options.outputLimit));writeText(out/"constants.json",array(r.decodedStrings));writeText(out/"vm-ir.json",irJson(r));writeText(out/"pseudocode.luau",pseudo(r));writeText(out/"reconstructed.luau",pseudo(r));writeText(out/"diff.json",diffJson(r.original,r.reconstructed));writeText(out/"report.json",reportJson(r));if(!options.json)std::cout<<"analysis written to "<<out.string()<<"\n";}return 0;}catch(const std::exception&e){std::cerr<<"analysis error: "<<e.what()<<"\n";return 2;}}
