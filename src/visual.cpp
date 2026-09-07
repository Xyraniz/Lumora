#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"
#include "lumora.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
TTF_Font* gFont=nullptr;
struct V3 { float x, y, z; V3 operator+(V3 b) const { return {x+b.x,y+b.y,z+b.z}; } V3 operator-(V3 b) const { return {x-b.x,y-b.y,z-b.z}; } V3 operator*(float s) const { return {x*s,y*s,z*s}; } };
float dot(V3 a, V3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
float length(V3 a) { return std::sqrt(dot(a,a)); }
V3 norm(V3 a) { float n=length(a); return n > .001f ? a*(1.f/n) : V3{0,0,1}; }
struct Target { std::string name; V3 pos; bool highlighted; };
struct Camera { V3 pos{0,5,18}; float yaw=3.14159f, pitch=-0.12f; };
struct Wall { float x,z,w,d,h; };
const std::vector<Wall> kWalls{{-15.f,-10.f,3.f,14.f,5.f},{14.f,-3.f,3.f,15.f,5.f},{0.f,-24.f,24.f,2.f,5.f}};

bool fieldNumber(lua_State* L, int index, const char* key, float& out) {
    index = lua_absindex(L,index); lua_getfield(L,index,key); if (!lua_isnumber(L,-1)) { lua_pop(L,1); return false; } out=(float)lua_tonumber(L,-1); lua_pop(L,1); return true;
}
V3 vectorFrom(lua_State* L, int index) { V3 v{0,0,0}; fieldNumber(L,index,"X",v.x); fieldNumber(L,index,"Y",v.y); fieldNumber(L,index,"Z",v.z); return v; }
V3 positionOf(lua_State* L, int instance) {
    instance=lua_absindex(L,instance); lua_getfield(L,instance,"Position"); V3 p=vectorFrom(L,-1); lua_pop(L,1); return p;
}
std::string fieldString(lua_State* L,int index,const char* key) { lua_getfield(L,index,key); std::string s=lua_tostring(L,-1)?lua_tostring(L,-1):""; lua_pop(L,1); return s; }
bool truthField(lua_State* L,int index,const char* key) { lua_getfield(L,index,key); bool b=lua_toboolean(L,-1); lua_pop(L,1); return b; }
bool hasHighlight(lua_State* L, int character) {
    lua_getfield(L, character, "_children");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; }
    int list=lua_absindex(L,-1), n=(int)lua_objlen(L,list);
    for(int i=1;i<=n;i++){lua_rawgeti(L,list,i);std::string cls=fieldString(L,-1,"ClassName");bool enabled=truthField(L,-1,"Enabled");lua_pop(L,1);if(cls=="Highlight"&&enabled){lua_pop(L,1);return true;}}
    lua_pop(L,1); return false;
}

void drawText(SDL_Renderer* r, int x, int y, const std::string& text, SDL_Color c) {
    if(!gFont){SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);SDL_Rect box{x,y,(int)text.size()*6,8};SDL_RenderFillRect(r,&box);return;}
    SDL_Surface* s=TTF_RenderUTF8_Blended(gFont,text.c_str(),c);if(!s)return;SDL_Texture* t=SDL_CreateTextureFromSurface(r,s);SDL_Rect dst{x,y,s->w,s->h};SDL_FreeSurface(s);if(t){SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);SDL_RenderCopy(r,t,nullptr,&dst);SDL_DestroyTexture(t);}
}
void roundedRect(SDL_Renderer* r,SDL_Rect rect,int radius,SDL_Color c){SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);radius=std::min(radius,std::min(rect.w,rect.h)/2);SDL_Rect mid{rect.x+radius,rect.y,rect.w-2*radius,rect.h};SDL_RenderFillRect(r,&mid);SDL_Rect side{rect.x,rect.y+radius,rect.w,rect.h-2*radius};SDL_RenderFillRect(r,&side);for(int y=-radius;y<=radius;y++)for(int x=-radius;x<=radius;x++)if(x*x+y*y<=radius*radius){SDL_RenderDrawPoint(r,rect.x+radius+x,rect.y+radius+y);SDL_RenderDrawPoint(r,rect.x+rect.w-radius-1+x,rect.y+radius+y);SDL_RenderDrawPoint(r,rect.x+radius+x,rect.y+rect.h-radius-1+y);SDL_RenderDrawPoint(r,rect.x+rect.w-radius-1+x,rect.y+rect.h-radius-1+y);}}
SDL_Color colorOf(lua_State* L,int index,SDL_Color fallback) { if(!lua_istable(L,index))return fallback;float r=1,g=1,b=1;fieldNumber(L,index,"R",r);fieldNumber(L,index,"G",g);fieldNumber(L,index,"B",b);return {(Uint8)(r<=1?r*255:r),(Uint8)(g<=1?g*255:g),(Uint8)(b<=1?b*255:b),255}; }
bool point2(lua_State* L,int index,int& x,int& y) { if(!lua_istable(L,index))return false;float fx=0,fy=0;fieldNumber(L,index,"X",fx);fieldNumber(L,index,"Y",fy);x=(int)fx;y=(int)fy;return true; }
void line(SDL_Renderer* r,int x1,int y1,int x2,int y2,SDL_Color c);
void renderDrawings(lua_State* L,SDL_Renderer* r) { lua_getglobal(L,"Drawing");lua_getfield(L,-1,"_objects");if(!lua_istable(L,-1)){lua_pop(L,2);return;}int list=lua_absindex(L,-1),n=(int)lua_objlen(L,list);for(int i=1;i<=n;i++){lua_rawgeti(L,list,i);int o=lua_absindex(L,-1);if(!truthField(L,o,"Visible")){lua_pop(L,1);continue;}std::string type=fieldString(L,o,"Type");lua_getfield(L,o,"Color");SDL_Color c=colorOf(L,-1,{255,255,255,255});lua_pop(L,1);if(type=="Line"){lua_getfield(L,o,"From");int x1,y1;bool a=point2(L,-1,x1,y1);lua_pop(L,1);lua_getfield(L,o,"To");int x2,y2;bool b=point2(L,-1,x2,y2);lua_pop(L,1);if(a&&b)line(r,x1,y1,x2,y2,c);}else if(type=="Text"){drawText(r,10,110,fieldString(L,o,"Text"),c);}lua_pop(L,1);}lua_pop(L,2); }
float numberField(lua_State* L,int index,const char* key,float fallback=0) { float v=fallback;fieldNumber(L,index,key,v);return v; }
float udim(lua_State* L,int index,const char* key,float parent,float fallback) { lua_getfield(L,index,key);if(!lua_istable(L,-1)){lua_pop(L,1);return fallback;}float scale=numberField(L,-1,"Scale"),offset=numberField(L,-1,"Offset");lua_pop(L,1);return parent*scale+offset; }
SDL_Color propertyColor(lua_State* L,int index,const char* key,SDL_Color fallback) {lua_getfield(L,index,key);SDL_Color c=colorOf(L,-1,fallback);lua_pop(L,1);return c;}
void renderGuiNode(lua_State* L,SDL_Renderer* r,int node,float px,float py,float pw,float ph) {
    std::string cls=fieldString(L,node,"ClassName");if(cls=="ScreenGui"){px=0;py=0;pw=1280;ph=720;}
    lua_getfield(L,node,"Position");int pos=lua_absindex(L,-1);float x=px,y=py;if(lua_istable(L,pos)){x=udim(L,pos,"X",pw,px);y=udim(L,pos,"Y",ph,py);}lua_pop(L,1);
    lua_getfield(L,node,"Size");int size=lua_absindex(L,-1);float w=numberField(L,node,"AbsoluteSizeX",120),h=numberField(L,node,"AbsoluteSizeY",30);if(lua_istable(L,size)){w=udim(L,size,"X",pw,w);h=udim(L,size,"Y",ph,h);}lua_pop(L,1);
    lua_getfield(L,node,"Visible");bool visible=lua_isnil(L,-1)||lua_toboolean(L,-1);lua_pop(L,1);if(!visible)return;
    if(cls=="Frame"||cls=="TextButton"||cls=="TextBox"||cls=="ScrollingFrame"||cls=="ImageLabel"||cls=="ImageButton"){SDL_Color bg=propertyColor(L,node,"BackgroundColor3",{40,40,55,255});lua_getfield(L,node,"BackgroundTransparency");float tr=lua_isnumber(L,-1)?(float)lua_tonumber(L,-1):0;lua_pop(L,1);if(tr<1){SDL_Rect rect{(int)x,(int)y,std::max(1,(int)w),std::max(1,(int)h)};roundedRect(r,rect,4,{bg.r,bg.g,bg.b,(Uint8)(255*(1-tr))});}lua_getfield(L,node,"UICorner");lua_pop(L,1);}
    if(cls=="TextLabel"||cls=="TextButton"||cls=="TextBox"||cls=="TextButton"){std::string text=fieldString(L,node,"Text");if(!text.empty()){SDL_Color tc=propertyColor(L,node,"TextColor3",{235,235,235,255});drawText(r,(int)x+4,(int)y+4,text,tc);}}
    lua_getfield(L,node,"_children");if(lua_istable(L,-1)){int list=lua_absindex(L,-1),n=(int)lua_objlen(L,list);for(int i=1;i<=n;i++){lua_rawgeti(L,list,i);if(lua_istable(L,-1))renderGuiNode(L,r,lua_absindex(L,-1),x,y,w,h);lua_pop(L,1);}}lua_pop(L,1);
}
void renderCoreGui(lua_State* L,SDL_Renderer* r) {lua_getglobal(L,"game");lua_getfield(L,-1,"CoreGui");if(lua_istable(L,-1))renderGuiNode(L,r,lua_absindex(L,-1),0,0,1280,720);lua_pop(L,2);}
void line(SDL_Renderer* r,int x1,int y1,int x2,int y2,SDL_Color c) { SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a); SDL_RenderDrawLine(r,x1,y1,x2,y2); }

bool loadScript(lua_State* L,const char* path,int argc,char** argv) {
    std::ifstream f(path); if(!f) return false; std::string source((std::istreambuf_iterator<char>(f)),{});
    lua_createtable(L,argc,0); for(int i=0;i<argc;i++){lua_pushinteger(L,i);lua_pushstring(L,argv[i]);lua_settable(L,-3);} lua_setglobal(L,"arg");
    if(!installPrelude(L)){std::fprintf(stderr,"visual: failed to install prelude\n");return false;} registerRobloxGlobals(L); registerHostGlobals(L);
    Luau::CompileOptions options; options.optimizationLevel=1; options.debugLevel=1; std::string bc=Luau::compile(source,options);
    if(luau_load(L,path,bc.data(),bc.size(),0)!=0){std::fprintf(stderr,"visual compile error: %s\n",lua_tostring(L,-1));return false;}
    if(lua_pcall(L,0,0,0)!=0){std::fprintf(stderr,"visual script error: %s\n",lua_tostring(L,-1));return false;}
    // Always provide a useful laboratory scene while retaining script-created state.
    lua_getglobal(L,"lumora"); lua_getfield(L,-1,"simulatePlayers"); lua_newtable(L);
    const char* names[]={"Target Alpha","Target Bravo","Target Charlie","Target Delta"}; const float xs[]={-10.f, -3.f, 5.f, 12.f};
    for(int i=0;i<4;i++){lua_newtable(L);lua_pushstring(L,names[i]);lua_setfield(L,-2,"Name");lua_pushstring(L,names[i]);lua_setfield(L,-2,"DisplayName");lua_pushinteger(L,900+i);lua_setfield(L,-2,"UserId");lua_newtable(L);lua_pushnumber(L,xs[i]);lua_rawseti(L,-2,1);lua_pushnumber(L,5);lua_rawseti(L,-2,2);lua_pushnumber(L,-4);lua_rawseti(L,-2,3);lua_setfield(L,-2,"Position");lua_rawseti(L,-2,i+1);}
    if(lua_pcall(L,1,1,0)!=0) lua_pop(L,1); else lua_pop(L,1); lua_pop(L,1);
    return true;
}
std::vector<Target> readPlayers(lua_State* L) {
    std::vector<Target> out; lua_getglobal(L,"game"); lua_getfield(L,-1,"GetService"); lua_pushvalue(L,-2); lua_pushstring(L,"Players");
    if(lua_pcall(L,2,1,0)!=0){lua_pop(L,2);return out;} lua_getfield(L,-1,"GetPlayers"); lua_pushvalue(L,-2); if(lua_pcall(L,1,1,0)!=0){lua_pop(L,2);return out;}
    int list=lua_absindex(L,-1); int n=(int)lua_objlen(L,list);
    for(int i=1;i<=n;i++){lua_rawgeti(L,list,i); int p=lua_absindex(L,-1); std::string name=fieldString(L,p,"DisplayName"); if(name=="LocalPlayer"){lua_pop(L,1);continue;} lua_getfield(L,p,"Character"); int ch=lua_absindex(L,-1); if(lua_istable(L,ch)){lua_getfield(L,ch,"HumanoidRootPart"); if(lua_istable(L,-1)){Target t{name,positionOf(L,-1),hasHighlight(L,ch)}; out.push_back(t);} lua_pop(L,1);} lua_pop(L,2);}
    lua_pop(L,3); return out;
}
void fireSignal(lua_State* L,const char* service,const char* signal,float value) {
    int top=lua_gettop(L); lua_getglobal(L,"game");lua_getfield(L,-1,"GetService");lua_pushvalue(L,-2);lua_pushstring(L,service);
    if(lua_pcall(L,2,1,0)==0){lua_getfield(L,-1,"_signals");if(lua_istable(L,-1)){lua_getfield(L,-1,signal);lua_getfield(L,-1,"Fire");lua_pushvalue(L,-2);lua_pushnumber(L,value);if(lua_pcall(L,2,0,0)!=0)lua_pop(L,1);else{} }lua_settop(L,top);return;}lua_settop(L,top);
}
bool blocked(V3 a,V3 b) { V3 d=b-a; for(const Wall& w:kWalls){ if(std::abs(d.z)<.001f)continue; float t=(w.z-a.z)/d.z;if(t<=0||t>=1)continue;float x=a.x+d.x*t;if(x>w.x-w.w*.5f&&x<w.x+w.w*.5f&&a.y< w.h&&b.y<w.h)return true;}return false; }
}

int runVisual(const char* path,int argc,char** argv,bool sandbox) {
    (void)sandbox;
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0){std::fprintf(stderr,"SDL init failed: %s\n",SDL_GetError());return 2;}
    if(TTF_Init()!=0){std::fprintf(stderr,"TTF init failed: %s\n",TTF_GetError());SDL_Quit();return 2;}
    SDL_Window* win=SDL_CreateWindow("Lumora Visual Lab",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer=win?SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC):nullptr;
    if (win && !renderer) renderer=SDL_CreateRenderer(win,-1,SDL_RENDERER_SOFTWARE);
    if(!win||!renderer){std::fprintf(stderr,"visual window failed: %s\n",SDL_GetError());if(renderer)SDL_DestroyRenderer(renderer);if(win)SDL_DestroyWindow(win);TTF_Quit();SDL_Quit();return 2;}
    const char* fonts[]={"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"};for(const char* font:fonts){gFont=TTF_OpenFont(font,14);if(gFont)break;}
    lua_State* L=luaL_newstate();luaL_openlibs(L); if(!loadScript(L,path,argc,argv)){lua_close(L);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(win);SDL_Quit();return 1;}
    Camera cam; bool running=true, capture=true, aim=false; Uint64 last=SDL_GetPerformanceCounter(); SDL_SetRelativeMouseMode(SDL_TRUE);
    while(running){ Uint64 now=SDL_GetPerformanceCounter(); float dt=(float)((now-last)/(double)SDL_GetPerformanceFrequency());last=now;dt=std::min(dt,.05f); SDL_Event e; const Uint8* keys=SDL_GetKeyboardState(nullptr);
        while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT)running=false; if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_ESCAPE)running=false; if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_f)aim=!aim; if(e.type==SDL_KEYDOWN||e.type==SDL_KEYUP)fireSignal(L,"UserInputService",e.type==SDL_KEYDOWN?"InputBegan":"InputEnded",dt); if(e.type==SDL_MOUSEMOTION&&capture){cam.yaw+=e.motion.xrel*.003f;cam.pitch=std::clamp(cam.pitch-e.motion.yrel*.003f,-1.3f,1.3f);}}
        V3 forward{std::sin(cam.yaw),0,std::cos(cam.yaw)}, right{std::cos(cam.yaw),0,-std::sin(cam.yaw)}; V3 move{0,0,0}; if(keys[SDL_SCANCODE_W])move=move+forward;if(keys[SDL_SCANCODE_S])move=move-forward;if(keys[SDL_SCANCODE_D])move=move+right;if(keys[SDL_SCANCODE_A])move=move-right;cam.pos=cam.pos+norm(move)*(10.f*dt);
        int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);SDL_SetRenderDrawColor(renderer,34,42,48,255);SDL_RenderClear(renderer);
        // Ground with perspective grid.
        SDL_SetRenderDrawColor(renderer,54,115,62,255);SDL_Rect ground{0,h/2,w,h/2};SDL_RenderFillRect(renderer,&ground);
        for(int z=-40;z<=40;z+=4){int y=h/2+(int)(std::max(0,z+8)*4);line(renderer,0,y,w,y,{74,145,78,255});} for(int x=-60;x<=60;x+=4){int sx=w/2+x*10;line(renderer,sx,h/2,sx+w/3,h,{74,145,78,255});}
        fireSignal(L,"RunService","Heartbeat",dt); fireSignal(L,"RunService","RenderStepped",dt);
        auto players=readPlayers(L); float best=1e9f; int bestIndex=-1;
        for(size_t i=0;i<players.size();i++){V3 rel=players[i].pos-cam.pos;float depth=rel.z*std::cos(cam.yaw)-rel.x*std::sin(cam.yaw);float side=rel.x*std::cos(cam.yaw)+rel.z*std::sin(cam.yaw);if(depth<=.2f)continue;int sx=w/2+(int)(side/depth*520), sy=h/2-(int)((players[i].pos.y-cam.pos.y)/depth*520);int ph=std::max(18,(int)(420/depth)),pw=std::max(10,ph/3);bool visible=sx>0&&sx<w&&sy>0&&sy<h; bool occluded=blocked(cam.pos,players[i].pos); if(visible){SDL_SetRenderDrawColor(renderer,occluded?70:50,occluded?70:120,occluded?70:220,255);SDL_Rect body{sx-pw/2,sy-ph,pw,ph};SDL_RenderFillRect(renderer,&body);SDL_SetRenderDrawColor(renderer,235,190,125,255);SDL_Rect head{sx-pw/3,sy-ph-std::max(8,pw/2),2*pw/3,std::max(8,pw/2)};SDL_RenderFillRect(renderer,&head); bool target=players[i].highlighted||aim; if(target){SDL_SetRenderDrawColor(renderer,(i==0&&aim)?255:255,(i==0&&aim)?230:75,50,255);SDL_Rect box{sx-pw,sy-ph-std::max(8,pw/2),pw*2,ph+std::max(8,pw/2)};SDL_RenderDrawRect(renderer,&box);if(!occluded)line(renderer,w/2,h/2,sx,sy-ph/2,{255,80,50,255});} drawText(renderer,sx-(int)players[i].name.size()*3,sy-ph-18,players[i].name,{255,255,255,255});}float score=std::abs(side)+depth*.08f+(occluded?1000:0);if(score<best){best=score;bestIndex=(int)i;}}
        if(aim&&bestIndex>=0){V3 d=players[bestIndex].pos-cam.pos;cam.yaw=std::atan2(d.x,d.z);}
        renderDrawings(L,renderer);
        renderCoreGui(L,renderer);
        SDL_SetRenderDrawColor(renderer,255,255,255,255);SDL_Rect cross{w/2-8,h/2,16,1};SDL_RenderFillRect(renderer,&cross);SDL_Rect cross2{w/2,h/2-8,1,16};SDL_RenderFillRect(renderer,&cross2);
        SDL_SetRenderDrawColor(renderer,18,24,29,230);SDL_Rect panel{18,18,330,74};SDL_RenderFillRect(renderer,&panel);drawText(renderer,30,30,"LUMORA VISUAL LAB",{110,220,255,255});drawText(renderer,30,45,"WASD move | mouse look | F toggle aim",{230,230,230,255});drawText(renderer,30,60,aim?"AIM ASSIST: TARGETING":"ESP: HIGHLIGHTS ACTIVE",{255,190,80,255});if(bestIndex>=0&&aim)drawText(renderer,30,75,"locked: "+players[bestIndex].name,{255,100,90,255});
        SDL_RenderPresent(renderer);
    }
    SDL_SetRelativeMouseMode(SDL_FALSE);lua_close(L);if(gFont){TTF_CloseFont(gFont);gFont=nullptr;}SDL_DestroyRenderer(renderer);SDL_DestroyWindow(win);TTF_Quit();SDL_Quit();return 0;
}
