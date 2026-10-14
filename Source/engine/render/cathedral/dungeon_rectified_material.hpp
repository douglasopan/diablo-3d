#pragma once
// Diablo 3D adaptation, 2026-10-11: square-floor sampling method from
// plopsDOTio/plops.io Diablo First Person 0.3.2 GetTexture.
// Modified: native decoder is retained; coverage is explicit (0/255 are paint),
// and this module does not rasterize, read global game state or change collision.
// Sustainable Use License 1.0; retain LICENSE.md and modification notice.
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
namespace devilution::cathedral::rectified_material {
inline constexpr unsigned Revision = 3;
struct SquareFloor {
 std::array<uint8_t, 64 * 64> pixels {};
 std::array<uint8_t, 64 * 64> coverage {};
};
inline std::array<unsigned, 2> FloorSource(unsigned x, unsigned y) {
 if (x >= 64 || y >= 64) throw std::invalid_argument("Square floor texel out of range");
 // Integer form of 32+(u-v)*31, 1+(u+v)*15, with center samples.
 // Positive source coordinates make truncation equivalent to floor.
 return { static_cast<unsigned>((2048 + (static_cast<int>(x) - static_cast<int>(y)) * 31) / 64),
          1U + ((x + y + 1U) * 15U) / 64U };
}
inline SquareFloor RectifyFloor(std::span<const uint8_t> pixels, std::span<const uint8_t> coverage) {
 if (pixels.size() != 64 * 32 || coverage.size() != pixels.size())
  throw std::invalid_argument("Rectification requires decoded 64x32 native diamond and coverage");
 SquareFloor out;
 for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) {
  auto source = FloorSource(x, y);
  if (coverage[source[1] * 64 + source[0]] == 0) {
   // Center sampling can round just outside a native triangular scanline.
   // Project that boundary sample onto the actual covered row, copying its
   // native paint (including 0/255). Never fill an interior coverage hole.
   unsigned first = 0, last = 63;
   while (first < 64 && coverage[source[1] * 64 + first] == 0) ++first;
   while (last > 0 && coverage[source[1] * 64 + last] == 0) --last;
   if (first >= 64 || (source[0] >= first && source[0] <= last))
    throw std::invalid_argument("Native floor contains an interior or empty scanline hole");
   const unsigned edge = source[0] < first ? first : last;
   if (source[0] > edge + 2 || edge > source[0] + 2)
    throw std::invalid_argument("Native floor boundary projection exceeds two texels");
   source[0] = edge;
  }
  const auto src = source[1] * 64 + source[0];
  const auto dst = y * 64 + x; out.pixels[dst] = pixels[src]; out.coverage[dst] = coverage[src];
 }
 return out;
}
// Complete isometric wall faces use their far diamond edge as ground.
// Sampling follows the reference's 90-pixel art height, with explicit coverage.
inline std::array<unsigned, 2> WallSource(unsigned x, unsigned y, unsigned column, bool flip) {
 if (x>=32 || y>=128 || column>1) throw std::invalid_argument("Wall texel or column invalid");
 const unsigned localX=flip ? 31-x : x;
 const int ground=column==0 ? 240-static_cast<int>(localX)/2 : 225+static_cast<int>(localX)/2;
 return { column*32+localX, static_cast<unsigned>(ground-static_cast<int>((127-y)*90/128)) };
}
struct WallFace {
 std::array<uint8_t,32*128> pixels{},coverage{};
 unsigned filled=0;
};
struct StoneCourses {
 std::array<uint8_t,64*128> pixels{}, bevel{};
};
// Reference FindTerrainDefaults reconstructs staggered courses from native
// stone grain rather than repeating the cornice. Coverage is not inferred
// from palette codes: this material is admitted only for solid backing.
inline StoneCourses BuildStoneCourses(std::span<const uint8_t> wall) {
 if (wall.size()!=32*128) throw std::invalid_argument("Stone courses require a rectified 32x128 masonry face");
 StoneCourses out;
 for(unsigned y=0;y<128;++y)for(unsigned x=0;x<64;++x){
  const unsigned course=y/10,localY=y%10,shifted=x+(course%2)*16;
  const unsigned localX=shifted%32,stone=shifted/32;
  const unsigned sourceX=(x+course*13+stone*7)%32;
  const unsigned sourceY=16+(localY+stone*3)%9;
  const auto i=y*64+x;
  out.pixels[i]=wall[sourceY*32+sourceX];
  out.bevel[i]=localY==0||localX==0?5:localY==1||localX==1?2:localY==9||localX==31?1:0;
 }
 return out;
}
inline SquareFloor CeilingFromStoneCourses(std::span<const uint8_t> masonry) {
 if(masonry.size()!=64*128) throw std::invalid_argument("Ceiling requires complete native stone courses");
 SquareFloor out;
 for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
  out.pixels[y*64+x]=masonry[(24+y)*64+x];out.coverage[y*64+x]=1;
 }
 return out;
}
inline WallFace RectifySolidWall(std::span<const uint8_t> iso, std::span<const uint8_t> coverage,
 std::span<const uint8_t> band, unsigned column, bool flip) {
 if(iso.size()!=64*256 || coverage.size()!=iso.size() || band.size()!=32*16 || column>1)
  throw std::invalid_argument("Solid wall requires decoded native column atlas and covered fallback band");
 WallFace out;
 for(unsigned y=0;y<128;++y)for(unsigned x=0;x<32;++x){
  const auto coord=WallSource(x,y,column,flip);const auto src=coord[1]*64+coord[0],dst=y*32+x;
  out.pixels[dst]=coverage[src] ? iso[src] : band[(y%16)*32+x];
  out.coverage[dst]=1;out.filled+=!coverage[src];
 }
 return out;
}
} // namespace devilution::cathedral::rectified_material
