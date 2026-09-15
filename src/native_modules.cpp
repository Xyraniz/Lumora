#include "native_modules.h"
#include "lua.h"
#include "lualib.h"
#include "Luau/Ast.h"
#include "Luau/Allocator.h"
#include "Luau/Parser.h"
#include "Luau/Compiler.h"
#include "Luau/Bytecode.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <curl/curl.h>
#include <sodium.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

#undef luaL_error
#define luaL_error(L, ...) (luaL_errorL(L, __VA_ARGS__), 0)

namespace {
static void setfn(lua_State* L, int table, const char* name, lua_CFunction fn) { table = lua_absindex(L, table); lua_pushcfunction(L, fn, name); lua_setfield(L, table, name); }
static void setstr(lua_State* L, int table, const char* name, const std::string& value) { table = lua_absindex(L, table); lua_pushlstring(L, value.data(), value.size()); lua_setfield(L, table, name); }
static void setbool(lua_State* L, int table, const char* name, bool value) { table = lua_absindex(L, table); lua_pushboolean(L, value); lua_setfield(L, table, name); }
static std::string checkString(lua_State* L, int i) { size_t n=0; const char* s=luaL_checklstring(L,i,&n); return std::string(s,n); }
static std::string optString(lua_State* L, int i, const char* d="") { size_t n=0; const char* s=luaL_optlstring(L,i,d,&n); return std::string(s,n); }

// ---------------- Crypto ----------------
static int cryptoDigest(lua_State* L) {
    std::string algorithm=optString(L,1,"sha256"), data=checkString(L,2);
    const EVP_MD* md=EVP_get_digestbyname(algorithm.c_str());
    if (!md) return luaL_error(L,"unknown digest '%s'",algorithm.c_str());
    EVP_MD_CTX* ctx=EVP_MD_CTX_new(); unsigned char out[EVP_MAX_MD_SIZE]; unsigned int n=0;
    if (!ctx || EVP_DigestInit_ex(ctx,md,nullptr)!=1 || EVP_DigestUpdate(ctx,data.data(),data.size())!=1 || EVP_DigestFinal_ex(ctx,out,&n)!=1) { EVP_MD_CTX_free(ctx); return luaL_error(L,"digest failed"); }
    EVP_MD_CTX_free(ctx); lua_pushlstring(L,(const char*)out,n); return 1;
}
static int cryptoHmac(lua_State* L) {
    std::string algorithm=optString(L,1,"sha256"), key=checkString(L,2), data=checkString(L,3); const EVP_MD* md=EVP_get_digestbyname(algorithm.c_str());
    if (!md) return luaL_error(L,"unknown digest '%s'",algorithm.c_str()); unsigned char out[EVP_MAX_MD_SIZE]; unsigned int n=0;
    if (!HMAC(md,key.data(),(int)key.size(),(const unsigned char*)data.data(),data.size(),out,&n)) return luaL_error(L,"hmac failed"); lua_pushlstring(L,(const char*)out,n); return 1;
}
static int cryptoRandom(lua_State* L) { int n=luaL_checkinteger(L,1); if(n<0||n>1024*1024) return luaL_error(L,"length out of range"); std::string out(n,'\0'); if(n) randombytes_buf(out.data(),n); lua_pushlstring(L,out.data(),out.size()); return 1; }
static int cryptoBase64Encode(lua_State* L) { std::string in=checkString(L,1); std::string out(4*((in.size()+2)/3),'\0'); int n=EVP_EncodeBlock((unsigned char*)out.data(),(const unsigned char*)in.data(),in.size()); lua_pushlstring(L,out.data(),n); return 1; }
static int cryptoBase64Decode(lua_State* L) { std::string in=checkString(L,1); std::string out(3*(in.size()/4+1),'\0'); int n=EVP_DecodeBlock((unsigned char*)out.data(),(const unsigned char*)in.data(),in.size()); if(n<0) return luaL_error(L,"invalid base64"); while(!in.empty()&&in.back()=='=') --n; lua_pushlstring(L,out.data(),n); return 1; }
static int cryptoOpen(lua_State* L) { lua_newtable(L); int t=lua_gettop(L); setfn(L,t,"digest",cryptoDigest); setfn(L,t,"hash",cryptoDigest); setfn(L,t,"hmac",cryptoHmac); setfn(L,t,"randomBytes",cryptoRandom); lua_newtable(L); int b=lua_gettop(L); setfn(L,b,"encode",cryptoBase64Encode); setfn(L,b,"decode",cryptoBase64Decode); lua_setfield(L,t,"base64"); return 1; }

// ---------------- HTTP and WebSocket ----------------
struct CurlBuffer { std::string body; long status=0; };
static size_t curlWrite(char* ptr,size_t size,size_t nmemb,void* userdata){auto* b=(CurlBuffer*)userdata; b->body.append(ptr,size*nmemb); return size*nmemb;}
static int netHttpRequest(lua_State* L) {
    luaL_checktype(L,1,LUA_TTABLE); lua_getfield(L,1,"url"); std::string url=checkString(L,-1); lua_pop(L,1); std::string method="GET", body; lua_getfield(L,1,"method"); if(!lua_isnil(L,-1)) method=checkString(L,-1); lua_pop(L,1); lua_getfield(L,1,"body"); if(!lua_isnil(L,-1)) body=checkString(L,-1); lua_pop(L,1);
    CURL* c=curl_easy_init(); if(!c) return luaL_error(L,"curl initialization failed"); CurlBuffer b; curl_easy_setopt(c,CURLOPT_URL,url.c_str()); curl_easy_setopt(c,CURLOPT_CUSTOMREQUEST,method.c_str()); curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,curlWrite); curl_easy_setopt(c,CURLOPT_WRITEDATA,&b); curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L); curl_easy_setopt(c,CURLOPT_TIMEOUT,30L); curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L); if(!body.empty()) curl_easy_setopt(c,CURLOPT_POSTFIELDS,body.data());
    struct curl_slist* headers=nullptr; lua_getfield(L,1,"headers"); if(lua_istable(L,-1)){lua_pushnil(L); while(lua_next(L,-1)){ if(lua_type(L,-2)==LUA_TSTRING&&lua_type(L,-1)==LUA_TSTRING){std::string h=checkString(L,-2)+": "+checkString(L,-1); headers=curl_slist_append(headers,h.c_str());} lua_pop(L,1);} } lua_pop(L,1); if(headers) curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);
    CURLcode rc=curl_easy_perform(c); if(rc==CURLE_OK) curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&b.status); if(headers) curl_slist_free_all(headers); curl_easy_cleanup(c); if(rc!=CURLE_OK) return luaL_error(L,"HTTP request failed: %s",curl_easy_strerror(rc)); lua_newtable(L); lua_pushinteger(L,b.status); lua_setfield(L,-2,"status"); setstr(L,lua_gettop(L),"body",b.body); setbool(L,lua_gettop(L),"ok",b.status>=200&&b.status<400); return 1;
}
static int netHttpGet(lua_State* L){lua_newtable(L); lua_pushvalue(L,1); lua_setfield(L,-2,"url"); lua_insert(L,1); return netHttpRequest(L);}
struct Socket { int fd=-1; bool websocket=false; bool serverSide=false; };
static Socket* checkSocket(lua_State* L,int i){return (Socket*)luaL_checkudata(L,i,"Lumora.Socket");}
static int socketClose(lua_State* L){auto*s=checkSocket(L,1); if(s->fd>=0){::close(s->fd);s->fd=-1;} return 0;}
static int socketSend(lua_State* L){auto*s=checkSocket(L,1); std::string data=checkString(L,2); if(s->fd<0)return luaL_error(L,"socket closed"); if(!s->websocket){ssize_t n=send(s->fd,data.data(),data.size(),0);if(n<0)return luaL_error(L,"send failed");lua_pushinteger(L,n);return 1;} bool masked=!s->serverSide; std::vector<unsigned char> frame; frame.push_back(0x81); unsigned char mask[4]={0x13,0x37,0x42,0x99}; if(data.size()<126)frame.push_back((unsigned char)data.size()|(masked?0x80:0)); else if(data.size()<=65535){frame.push_back(126|(masked?0x80:0));frame.push_back(data.size()>>8);frame.push_back(data.size());} else return luaL_error(L,"message too large"); if(masked)frame.insert(frame.end(),mask,mask+4); for(size_t i=0;i<data.size();i++)frame.push_back((unsigned char)data[i]^(masked?mask[i%4]:0)); if(send(s->fd,frame.data(),frame.size(),0)<0)return luaL_error(L,"send failed"); return 0;}
static int socketReceive(lua_State* L){auto*s=checkSocket(L,1); if(s->fd<0)return luaL_error(L,"socket closed"); char buf[65536]; ssize_t n=recv(s->fd,buf,sizeof(buf),0); if(n<=0){lua_pushnil(L);return 1;} if(!s->websocket){lua_pushlstring(L,buf,n);return 1;} if(n<2)return luaL_error(L,"incomplete websocket frame"); bool masked=(buf[1]&128)!=0; size_t len=buf[1]&127, pos=2; if(len==126){if(n<4)return luaL_error(L,"incomplete websocket frame");len=((unsigned char)buf[2]<<8)|(unsigned char)buf[3];pos=4;} if(len>65535||pos+len+(masked?4:0)>(size_t)n)return luaL_error(L,"unsupported websocket frame"); unsigned char mask[4]{};if(masked){memcpy(mask,buf+pos,4);pos+=4;}std::string payload(buf+pos,len);if(masked)for(size_t i=0;i<len;i++)payload[i]^=mask[i%4];lua_pushlstring(L,payload.data(),payload.size()); return 1;}
static int netConnect(lua_State* L){std::string host=checkString(L,1);int port=luaL_checkinteger(L,2);int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return luaL_error(L,"socket failed");sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);if(inet_pton(AF_INET,host.c_str(),&a.sin_addr)!=1){::close(fd);return luaL_error(L,"invalid IPv4 address");}if(connect(fd,(sockaddr*)&a,sizeof(a))<0){::close(fd);return luaL_error(L,"connect failed: %s",strerror(errno));}auto*s=(Socket*)lua_newuserdata(L,sizeof(Socket));*s={fd,false,false};luaL_getmetatable(L,"Lumora.Socket");lua_setmetatable(L,-2);return 1;}
static std::string wsAccept(const std::string& key){std::string input=key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";unsigned char digest[EVP_MAX_MD_SIZE];unsigned int n=0;EVP_MD_CTX* ctx=EVP_MD_CTX_new();EVP_DigestInit_ex(ctx,EVP_sha1(),nullptr);EVP_DigestUpdate(ctx,input.data(),input.size());EVP_DigestFinal_ex(ctx,digest,&n);EVP_MD_CTX_free(ctx);std::string out(4*((n+2)/3),'\0');int len=EVP_EncodeBlock((unsigned char*)out.data(),digest,n);out.resize(len);return out;}
static int netWebsocketConnect(lua_State* L){std::string url=checkString(L,1);if(url.rfind("ws://",0)!=0)return luaL_error(L,"only ws:// is supported");std::string rest=url.substr(5),hostport=rest,path="/";size_t slash=rest.find('/');if(slash!=std::string::npos){hostport=rest.substr(0,slash);path=rest.substr(slash);}size_t colon=hostport.rfind(':');if(colon==std::string::npos)return luaL_error(L,"WebSocket URL needs a port");std::string host=hostport.substr(0,colon);int port=std::stoi(hostport.substr(colon+1));int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return luaL_error(L,"socket failed");sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);if(inet_pton(AF_INET,host.c_str(),&a.sin_addr)!=1||connect(fd,(sockaddr*)&a,sizeof(a))<0){::close(fd);return luaL_error(L,"WebSocket connect failed");}std::string key="bXlzdGF0aWNrLWtleQ==";std::string request="GET "+path+" HTTP/1.1\r\nHost: "+hostport+"\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "+key+"\r\nSec-WebSocket-Version: 13\r\n\r\n";send(fd,request.data(),request.size(),0);char response[4096];ssize_t n=recv(fd,response,sizeof(response)-1,0);if(n<0||std::string(response,n).find("101") == std::string::npos){::close(fd);return luaL_error(L,"WebSocket handshake failed");}auto*s=(Socket*)lua_newuserdata(L,sizeof(Socket));*s={fd,true,false};luaL_getmetatable(L,"Lumora.Socket");lua_setmetatable(L,-2);return 1;}
static int netServerListen(lua_State* L){std::string host=optString(L,1,"0.0.0.0");int port=luaL_checkinteger(L,2);luaL_checktype(L,3,LUA_TFUNCTION);int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return luaL_error(L,"socket failed");int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);a.sin_addr.s_addr=host=="0.0.0.0"?INADDR_ANY:inet_addr(host.c_str());if(bind(fd,(sockaddr*)&a,sizeof(a))<0||listen(fd,8)<0){::close(fd);return luaL_error(L,"listen failed: %s",strerror(errno));}int client=accept(fd,nullptr,nullptr);::close(fd);if(client<0)return luaL_error(L,"accept failed");char buf[65536];ssize_t n=recv(client,buf,sizeof(buf)-1,0);if(n<0){::close(client);return luaL_error(L,"receive failed");}buf[n]=0;lua_newtable(L);lua_pushlstring(L,buf,n);lua_setfield(L,-2,"raw");lua_pushliteral(L,"GET");lua_setfield(L,-2,"method");lua_pushliteral(L,"/");lua_setfield(L,-2,"path");lua_pushvalue(L,3);lua_insert(L,-2);if(lua_pcall(L,1,1,0)!=0){::close(client);lua_error(L);return 0;}std::string response=lua_isstring(L,-1)?checkString(L,-1):"";lua_pop(L,1);std::string out="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(response.size())+"\r\nConnection: close\r\n\r\n"+response;send(client,out.data(),out.size(),0);::close(client);lua_pushboolean(L,1);return 1;}
static int netServerWebSocket(lua_State* L){std::string host=optString(L,1,"0.0.0.0");int port=luaL_checkinteger(L,2);luaL_checktype(L,3,LUA_TFUNCTION);int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return luaL_error(L,"socket failed");int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);a.sin_addr.s_addr=host=="0.0.0.0"?INADDR_ANY:inet_addr(host.c_str());if(bind(fd,(sockaddr*)&a,sizeof(a))<0||listen(fd,8)<0){::close(fd);return luaL_error(L,"listen failed: %s",strerror(errno));}int client=accept(fd,nullptr,nullptr);::close(fd);if(client<0)return luaL_error(L,"accept failed");char buf[8192];ssize_t n=recv(client,buf,sizeof(buf)-1,0);if(n<=0){::close(client);return luaL_error(L,"handshake receive failed");}std::string request(buf,n),key="";size_t p=request.find("Sec-WebSocket-Key:");if(p!=std::string::npos){p+=19;while(p<request.size()&&request[p]==' ')p++;size_t e=request.find("\r\n",p);key=request.substr(p,e-p);}if(key.empty()){::close(client);return luaL_error(L,"missing WebSocket key");}std::string response="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+wsAccept(key)+"\r\n\r\n";send(client,response.data(),response.size(),0);auto*s=(Socket*)lua_newuserdata(L,sizeof(Socket));*s={client,true,true};luaL_getmetatable(L,"Lumora.Socket");lua_setmetatable(L,-2);lua_pushvalue(L,3);lua_insert(L,-2);if(lua_pcall(L,1,0,0)!=0){::close(client);lua_error(L);return 0;}lua_pushboolean(L,1);return 1;}
static int netOpen(lua_State* L){lua_newtable(L);int t=lua_gettop(L);setfn(L,t,"request",netHttpRequest);setfn(L,t,"get",netHttpGet);setfn(L,t,"connect",netConnect);setfn(L,t,"websocketConnect",netWebsocketConnect);lua_newtable(L);int srv=lua_gettop(L);setfn(L,srv,"listen",netServerListen);setfn(L,srv,"listenWebSocket",netServerWebSocket);lua_setfield(L,t,"server");luaL_newmetatable(L,"Lumora.Socket");setfn(L,-1,"send",socketSend);setfn(L,-1,"receive",socketReceive);setfn(L,-1,"close",socketClose);lua_pushvalue(L,-1);lua_setfield(L,-2,"__index");lua_pushcfunction(L,socketClose,"__gc");lua_setfield(L,-2,"__gc");lua_pop(L,1);return 1;}

// ---------------- Syntax / AST ----------------
struct CountVisitor : Luau::AstVisitor { int nodes=0, expressions=0, statements=0; std::vector<std::pair<int,int>> locations; bool visit(Luau::AstNode* n) override {nodes++; if(n->asExpr())expressions++; if(n->asStat())statements++; locations.push_back({n->location.begin.line+1,n->location.begin.column+1}); return true;} };
static int syntaxParse(lua_State* L){std::string source=checkString(L,1); Luau::Allocator allocator; Luau::AstNameTable names(allocator); Luau::ParseOptions options; options.captureComments=true; options.storeCstData=true; Luau::ParseResult result=Luau::Parser::parse(source.data(),source.size(),names,allocator,options); lua_newtable(L);int out=lua_gettop(L);setstr(L,out,"source",source);lua_pushinteger(L,result.lines);lua_setfield(L,out,"lines");lua_newtable(L);int errs=lua_gettop(L);for(size_t i=0;i<result.errors.size();i++){lua_pushinteger(L,i+1);lua_newtable(L);lua_pushstring(L,result.errors[i].getMessage().c_str());lua_setfield(L,-2,"message");lua_pushinteger(L,result.errors[i].getLocation().begin.line+1);lua_setfield(L,-2,"line");lua_settable(L,errs);}lua_setfield(L,out,"errors");if(result.root){CountVisitor v;result.root->visit(&v);lua_pushinteger(L,v.nodes);lua_setfield(L,out,"nodes");lua_pushinteger(L,v.expressions);lua_setfield(L,out,"expressions");lua_pushinteger(L,v.statements);lua_setfield(L,out,"statements");lua_pushinteger(L,(int)result.cstNodeMap.size());lua_setfield(L,out,"cstNodes");lua_newtable(L);for(size_t i=0;i<v.locations.size();i++){lua_pushinteger(L,i+1);lua_newtable(L);lua_pushinteger(L,v.locations[i].first);lua_setfield(L,-2,"line");lua_pushinteger(L,v.locations[i].second);lua_setfield(L,-2,"column");lua_settable(L,-3);}lua_setfield(L,out,"nodeList");lua_pushboolean(L,1);lua_setfield(L,out,"complete");}else{lua_pushinteger(L,0);lua_setfield(L,out,"nodes");lua_pushinteger(L,0);lua_setfield(L,out,"cstNodes");lua_newtable(L);lua_setfield(L,out,"nodeList");lua_pushboolean(L,0);lua_setfield(L,out,"complete");}return 1;}
static int syntaxOpen(lua_State* L){lua_newtable(L);setfn(L,-1,"parse",syntaxParse);return 1;}

// ---------------- Isolated VMs ----------------
struct IsolatedVM { lua_State* L=nullptr; };
static IsolatedVM* checkVM(lua_State* L,int i){return (IsolatedVM*)luaL_checkudata(L,i,"Lumora.IsolatedVM");}
static int vmClose(lua_State* L){auto*v=checkVM(L,1);if(v->L){lua_close(v->L);v->L=nullptr;}return 0;}
static int vmEval(lua_State* L){auto*v=checkVM(L,1);if(!v->L)return luaL_error(L,"VM closed");std::string src=checkString(L,2);Luau::CompileOptions o;o.optimizationLevel=1;o.debugLevel=2;std::string bc=Luau::compile(src,o);if(luau_load(v->L,"=isolated",bc.data(),bc.size(),0)!=0){lua_pushnil(L);lua_xmove(v->L,L,1);return 2;}int rc=lua_pcall(v->L,0,1,0);if(rc!=0){lua_pushnil(L);lua_xmove(v->L,L,1);return 2;}if(lua_isnumber(v->L,-1))lua_pushnumber(L,lua_tonumber(v->L,-1));else if(lua_isboolean(v->L,-1))lua_pushboolean(L,lua_toboolean(v->L,-1));else if(lua_isstring(v->L,-1)){size_t n=0;const char*s=lua_tolstring(v->L,-1,&n);lua_pushlstring(L,s,n);}else lua_pushnil(L);lua_pop(v->L,1);return 1;}
static int vmCreate(lua_State* L){auto*v=(IsolatedVM*)lua_newuserdata(L,sizeof(IsolatedVM));v->L=luaL_newstate();if(!v->L)return luaL_error(L,"could not create isolated VM");luaL_openlibs(v->L);luaL_getmetatable(L,"Lumora.IsolatedVM");lua_setmetatable(L,-2);return 1;}
static int vmRunIn(lua_State* L){return vmEval(L);}
static int vmOpen(lua_State* L){luaL_newmetatable(L,"Lumora.IsolatedVM");setfn(L,-1,"eval",vmEval);setfn(L,-1,"close",vmClose);lua_pushvalue(L,-1);lua_setfield(L,-2,"__index");lua_pushcfunction(L,vmClose,"__gc");lua_setfield(L,-2,"__gc");lua_pop(L,1);lua_newtable(L);setfn(L,-1,"create",vmCreate);setfn(L,-1,"runIn",vmRunIn);return 1;}

// ---------------- Debugger ----------------
struct DebugSession { lua_State* L=nullptr; std::set<int> breakpoints; int lastLine=0; int hits=0; bool stopped=false; };
static DebugSession* checkDbg(lua_State* L,int i){return (DebugSession*)luaL_checkudata(L,i,"Lumora.DebugSession");}
static void debugStep(lua_State* L,lua_Debug* ar){auto*d=(DebugSession*)lua_callbacks(L)->userdata;if(!d)return;d->lastLine=ar?ar->currentline:0;d->hits++;d->stopped=true;lua_break(L);}
static void debugBreak(lua_State* L,lua_Debug* ar){auto*d=(DebugSession*)lua_callbacks(L)->userdata;if(!d)return;d->lastLine=ar?ar->currentline:0;d->hits++;d->stopped=true;lua_break(L);}
static int dbgSetBreakpoint(lua_State* L){auto*d=checkDbg(L,1);int line=luaL_checkinteger(L,2);bool on=luaL_optboolean(L,3,true);if(on)d->breakpoints.insert(line);else d->breakpoints.erase(line);lua_pushboolean(L,1);return 1;}
static int dbgStart(lua_State* L){auto*d=checkDbg(L,1);if(d->L)return luaL_error(L,"already started");d->L=luaL_newstate();luaL_openlibs(d->L);std::string src=checkString(L,2);Luau::CompileOptions o;o.optimizationLevel=1;o.debugLevel=2;std::string bc=Luau::compile(src,o);if(luau_load(d->L,"=debug-session",bc.data(),bc.size(),0)!=0){lua_pushnil(L);lua_xmove(d->L,L,1);return 2;}for(int line:d->breakpoints)lua_breakpoint(d->L,-1,line,1);lua_callbacks(d->L)->userdata=d;lua_callbacks(d->L)->debugstep=debugStep;lua_callbacks(d->L)->debugbreak=debugBreak;lua_singlestep(d->L,0);int rc=lua_resume(d->L,nullptr,0);if(rc!=0&&rc!=LUA_BREAK){lua_pushnil(L);lua_xmove(d->L,L,1);return 2;}lua_pushboolean(L,rc==0);return 1;}
static int dbgContinue(lua_State* L){auto*d=checkDbg(L,1);if(!d->L)return luaL_error(L,"session not started");d->stopped=false;int rc=lua_resume(d->L,nullptr,0);lua_newtable(L);lua_pushboolean(L,rc==0);lua_setfield(L,-2,"done");lua_pushinteger(L,d->lastLine);lua_setfield(L,-2,"line");lua_pushinteger(L,d->hits);lua_setfield(L,-2,"hits");if(rc!=0&&rc!=LUA_BREAK&&lua_gettop(d->L)>0){size_t n=0;const char*s=lua_tolstring(d->L,-1,&n);lua_pushlstring(L,s,n);lua_setfield(L,-2,"error");}return 1;}
static int dbgStep(lua_State* L){auto*d=checkDbg(L,1);if(!d->L)return luaL_error(L,"session not started");lua_singlestep(d->L,1);return dbgContinue(L);}
static int dbgState(lua_State* L){auto*d=checkDbg(L,1);lua_newtable(L);lua_pushboolean(L,d->L!=nullptr);lua_setfield(L,-2,"started");lua_pushinteger(L,d->lastLine);lua_setfield(L,-2,"line");lua_pushinteger(L,d->hits);lua_setfield(L,-2,"hits");return 1;}
static int dbgClose(lua_State* L){auto*d=checkDbg(L,1);if(d->L){lua_close(d->L);d->L=nullptr;}return 0;}
static int dbgCreate(lua_State* L){auto*d=(DebugSession*)lua_newuserdata(L,sizeof(DebugSession));new(d)DebugSession();luaL_getmetatable(L,"Lumora.DebugSession");lua_setmetatable(L,-2);return 1;}
static int debugOpen(lua_State* L){luaL_newmetatable(L,"Lumora.DebugSession");setfn(L,-1,"setBreakpoint",dbgSetBreakpoint);setfn(L,-1,"start",dbgStart);setfn(L,-1,"continue",dbgContinue);setfn(L,-1,"step",dbgStep);setfn(L,-1,"state",dbgState);setfn(L,-1,"close",dbgClose);lua_pushvalue(L,-1);lua_setfield(L,-2,"__index");lua_pushcfunction(L,dbgClose,"__gc");lua_setfield(L,-2,"__gc");lua_pop(L,1);lua_newtable(L);setfn(L,-1,"create",dbgCreate);return 1;}

static int returnModule(lua_State* L,const char* name){if(strcmp(name,"@lumora/crypto")==0)return cryptoOpen(L);if(strcmp(name,"@lumora/net")==0)return netOpen(L);if(strcmp(name,"@lumora/syntax")==0)return syntaxOpen(L);if(strcmp(name,"@lumora/vm")==0)return vmOpen(L);if(strcmp(name,"@lumora/debugger")==0)return debugOpen(L);return 0;}
}
void registerNativeModules(lua_State*) { static bool initialized=false; if(!initialized){curl_global_init(CURL_GLOBAL_DEFAULT); if(sodium_init()<0) std::abort(); initialized=true;} }
int requireNativeModule(lua_State* L,const char* name){ if(!name)return 0; if(strncmp(name,"@lumora/",8)!=0)return 0; if(strcmp(name,"@lumora/crypto")==0||strcmp(name,"@lumora/net")==0||strcmp(name,"@lumora/syntax")==0||strcmp(name,"@lumora/vm")==0||strcmp(name,"@lumora/debugger")==0){registerNativeModules(L);return returnModule(L,name);} return 0; }
