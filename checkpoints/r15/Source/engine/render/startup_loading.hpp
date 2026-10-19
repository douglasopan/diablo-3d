#pragma once
// Loading presentation only: main thread owns SDL; workers publish copied names.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <future>
#include <chrono>
#include <utility>
#include <string>
#include <string_view>
#include <vector>
#if !defined(USE_SDL1) && !defined(USE_SDL3)
#include <SDL.h>
#include "engine/render/startup_loading_art.hpp"
namespace devilution::loading_ui {
struct Status { std::string module, operation, file; uint64_t sequence = 0; uint32_t completed = 0, total = 0; };
inline std::mutex Mutex;
inline Status Current;
inline unsigned Depth = 0;
inline SDL_threadID Owner = 0;
inline SDL_Renderer *Output = nullptr;
inline uint64_t Drawn = 0;
inline uint32_t LastDraw = 0;
inline SDL_Renderer *LastOutput = nullptr;
inline uint64_t Presentations = 0;
inline int LastWidth = 0, LastHeight = 0;
// Optional owner-thread diagnostic observer; never replaces SDL presentation.
using BeforePresentObserver = void (*)(SDL_Renderer *, const SDL_Rect &, const Status &, void *) noexcept;
inline BeforePresentObserver BeforePresent = nullptr;
inline void *BeforePresentContext = nullptr;
inline Status Snapshot() { std::lock_guard<std::mutex> lock(Mutex); return Current; }
// Text advances in fixed cells, but its visible glyph coverage is centered.
// Keep the stage on one line and paths on two, above the native progress bar.
inline void Text(std::vector<uint32_t> &pixels, std::string_view text, int y, uint32_t color, int maximumLines=3)
{
 size_t cursor=0;
 for(int line=0;line<maximumLines && cursor<text.size();++line,y+=18){
  unsigned glyphs[76]{};int count=0;
  while(cursor<text.size() && count<76){
   unsigned c=static_cast<unsigned char>(text[cursor++]);
   if(c==0xC3 && cursor<text.size() && (static_cast<unsigned char>(text[cursor])&0xC0)==0x80)
    c=static_cast<unsigned char>(text[cursor++])+64;
   if(c==10)break;
   glyphs[count++]=(c>=32 && c<256)?c:static_cast<unsigned>('?');
  }
  // The copied status keeps the full filename; only the bounded display elides.
  if(line+1==maximumLines && cursor<text.size() && count>=3)
   glyphs[count-3]=glyphs[count-2]=glyphs[count-1]='.';
  int left=count*9,right=-1;
  for(int i=0;i<count;++i)for(int r=0;r<18;++r)for(int b=0;b<9;++b)
   if(GlyphRows[(glyphs[i]-32)*18+r]&(1<<b)){left=std::min(left,i*9+b);right=std::max(right,i*9+b);}
  if(right<left || y<0 || y+18>320)continue;
  const int origin=(760-(right-left+1))/2-left;
  for(int i=0;i<count;++i)for(int r=0;r<18;++r)for(int b=0;b<9;++b)
   if(GlyphRows[(glyphs[i]-32)*18+r]&(1<<b))pixels[(y+r)*760+origin+i*9+b]=color;
 }
}
inline SDL_Rect CenteredPanelBounds(int width,int height)
{
 if(width<=0 || height<=0)return {};
 const float scale=std::min({1.0F,width*0.92F/760,height*0.9F/320});
 const int w=std::max(1,static_cast<int>(760*scale));
 const int h=std::max(1,static_cast<int>(320*scale));
 return {(width-w)/2,(height-h)/2,w,h};
}
inline bool Draw(bool force=false)
{
 if(Output==nullptr || SDL_ThreadID()!=Owner)return false;
 SDL_PumpEvents(); // Service the OS window; native loader retains ownership of queued events.
 Status status;
 {std::lock_guard<std::mutex> lock(Mutex);if(Depth==0)return false;status=Current;}
 // SDL_GetRendererOutputSize reports the active texture's size when a target
 // is bound. Query with the window target and restore every state on all exits.
 struct RestoreRendererState {
  SDL_Renderer *renderer;SDL_Texture *target;SDL_Rect viewport{},clip{};
  bool clipped,valid;int lw=0,lh=0;float sx=1,sy=1;Uint8 r=0,g=0,b=0,a=0;
  explicit RestoreRendererState(SDL_Renderer *output):renderer(output),target(SDL_GetRenderTarget(output)),clipped(SDL_RenderIsClipEnabled(output)==SDL_TRUE),valid(false){
   SDL_RenderGetViewport(renderer,&viewport);SDL_RenderGetClipRect(renderer,&clip);
   SDL_RenderGetLogicalSize(renderer,&lw,&lh);SDL_RenderGetScale(renderer,&sx,&sy);
   valid=SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a)==0;
  }
  ~RestoreRendererState(){if(valid){
   SDL_SetRenderTarget(renderer,target);SDL_RenderSetLogicalSize(renderer,lw,lh);
   SDL_RenderSetScale(renderer,sx,sy);SDL_RenderSetViewport(renderer,&viewport);
   SDL_RenderSetClipRect(renderer,clipped?&clip:nullptr);SDL_SetRenderDrawColor(renderer,r,g,b,a);
  }}
 } restore(Output);
 if(!restore.valid || SDL_SetRenderTarget(Output,nullptr)!=0)return false;
 int w=0,h=0;if(SDL_GetRendererOutputSize(Output,&w,&h)!=0 || w<=0 || h<=0)return false;
 // Observe resize before throttling. Content-only reports still present at
 // most 10 Hz, but a new output size must be repainted on the next draw.
 if(!force && Presentations!=0 && LastOutput==Output && w==LastWidth && h==LastHeight && SDL_GetTicks()-LastDraw<100)return true;
 std::vector<uint32_t> pixels;pixels.reserve(760*320);
 for(auto run:PanelRuns)pixels.insert(pixels.end(),run>>24,0xFF000000U|(run&0xFFFFFFU));
 // The inherited RLE includes a Portuguese caption. Replace only its flat
 // background band; keep the owner-provided logo and divider byte-exact.
 for(int y=174;y<212;++y)for(int x=0;x<760;++x)pixels[y*760+x]=0xFF0A090A;
 Text(pixels,"Loading...",184,0xFFE0D3BC,1);
 Text(pixels,status.module+" | "+status.operation,230,0xFFE0C79C,1);
 Text(pixels,status.file,254,0xFFC0BEBA,2);
 // Real native stage count when known; otherwise an animated indeterminate bar.
 const int bx=38, by=306, bw=684, bh=8;
 for(int y=by;y<by+bh;++y)for(int x=bx;x<bx+bw;++x)pixels[y*760+x]=0xFF39332C;
 const int filled=status.total ? static_cast<int>(uint64_t(bw)*std::min(status.completed,status.total)/status.total) : 130;
 const int offset=status.total ? 0 : static_cast<int>((SDL_GetTicks()/6)%(bw-130));
 for(int y=by+1;y<by+bh-1;++y)for(int x=bx+offset;x<bx+offset+filled;++x)pixels[y*760+x]=0xFFD3AA61;
 std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture(SDL_CreateTexture(Output,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,760,320),&SDL_DestroyTexture);
 if(!texture || SDL_UpdateTexture(texture.get(),nullptr,pixels.data(),760*4)!=0)return false;
 if(SDL_RenderSetLogicalSize(Output,0,0)!=0 || SDL_RenderSetScale(Output,1,1)!=0 || SDL_RenderSetViewport(Output,nullptr)!=0 || SDL_RenderSetClipRect(Output,nullptr)!=0)return false;
 if(SDL_SetRenderDrawColor(Output,10,9,10,255)!=0 || SDL_RenderClear(Output)!=0)return false;
 const SDL_Rect dst=CenteredPanelBounds(w,h);
 if(SDL_RenderCopy(Output,texture.get(),nullptr,&dst)!=0)return false;
 if(BeforePresent!=nullptr)BeforePresent(Output,dst,status,BeforePresentContext);
 SDL_RenderPresent(Output);
 Drawn=status.sequence;LastDraw=SDL_GetTicks();LastOutput=Output;LastWidth=w;LastHeight=h;++Presentations;return true;
}
inline void Report(std::string_view module,std::string_view operation,std::string_view file={})
{
 {std::lock_guard<std::mutex> lock(Mutex);if(Depth==0)return;Current.module=module;Current.operation=operation;Current.file=file;++Current.sequence;}
 if(SDL_ThreadID()==Owner)Draw(false);
}
inline void SetProgress(uint32_t completed, uint32_t total)
{
 std::lock_guard<std::mutex> lock(Mutex);
 if(Depth==0)return;
 Current.completed=std::min(completed,total);Current.total=total;++Current.sequence;
}
// Worker owns only the CPU task. SDL and D3D device/context stay on their owner.
// Pump without consuming events: native progress handling still handles quit/focus.
template<class Task> auto RunResponsive(Task &&task) -> decltype(task())
{
 auto result=std::async(std::launch::async,std::forward<Task>(task));
 while(result.wait_for(std::chrono::milliseconds(16))!=std::future_status::ready){
  if(SDL_ThreadID()==Owner){SDL_PumpEvents();Draw();}
 }
 if(SDL_ThreadID()==Owner){SDL_PumpEvents();Draw();}
 return result.get();
}
class Scope {
 bool entered_=false;
public:
 Scope(SDL_Renderer *output,std::string_view module,std::string_view operation,std::string_view file={}) {
  if(output==nullptr || (Owner!=0 && Owner!=SDL_ThreadID()))return;
  Owner=SDL_ThreadID();Output=output;{std::lock_guard<std::mutex> lock(Mutex);if(Depth==0){Current={};LastOutput=nullptr;LastWidth=LastHeight=0;}++Depth;entered_=true;}
  Report(module,operation,file);
 }
 ~Scope(){if(entered_){std::lock_guard<std::mutex> lock(Mutex);if(--Depth==0){Current={};Output=nullptr;Drawn=0;}}}
 Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
}
#else
namespace devilution::loading_ui {
inline void Report(std::string_view,std::string_view,std::string_view={}){}
inline bool Draw(bool=false){return false;}
inline void SetProgress(uint32_t,uint32_t){}
template<class Task> auto RunResponsive(Task &&task) -> decltype(task()){return task();}
struct Scope { template<class T>Scope(T,std::string_view,std::string_view,std::string_view={}){} };
}
#endif
