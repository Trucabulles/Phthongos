#pragma once
#include <array>
#include <cstdint>
#include <cmath>
namespace voicePalette {
// Twelve voice colours. Copper, ochre and turquoise are adjusted away from
// diagnostic orange/teal and playback yellow. Bright magenta stays reserved.
inline constexpr std::array<std::uint32_t,12> colours{{
  0xff4e79a7,0xff59a5d8,0xff6c6ecf,0xff8b6bb8,
  0xffb07aa1,0xffc96a86,0xffb85c4a,0xffdf9d76,
  0xffaa943d,0xff7f9e55,0xff4f9b78,0xff52babb}};
inline constexpr const char* names[]{"Bleu","Bleu clair","Indigo","Violet","Mauve","Rose sourd","Rouge brique","Orange cuivré","Ocre","Vert olive","Vert","Turquoise"};
inline double luminance(std::uint32_t rgb){
  auto channel=[](unsigned n){double v=n/255.;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
  return .2126*channel((rgb>>16)&255)+.7152*channel((rgb>>8)&255)+.0722*channel(rgb&255);
}
inline std::uint32_t textColour(std::uint32_t rgb){
  constexpr std::uint32_t dark=0xff000000,light=0xffffffff;auto l=luminance(rgb);
  auto againstDark=(l+.05)/(luminance(dark)+.05),againstLight=(luminance(light)+.05)/(l+.05);
  return againstDark>=againstLight?dark:light;
}
}
