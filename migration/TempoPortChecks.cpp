#include "data/TempoReference.h"
#include <cmath>
#include <iostream>
#include <chrono>
int main(){int failures=0;float worst=0;
 for(const auto& fixture:tempoFixtures()){
  phthongos::midi::bpm::Config c;c.center=fixture.center;c.range=fixture.range;
  auto result=phthongos::midi::bpm::detect(fixture.notes,c);
  float maximum=0,error=0;for(auto x:fixture.histogram)maximum=std::max(maximum,x);
  bool ok=std::abs(result.bpm-fixture.bpm)<.001f;
  if(!fixture.histogram.empty()){
   ok&=result.bins.size()==fixture.histogram.size();
   if(result.bins.size()==fixture.histogram.size())for(std::size_t i=0;i<result.bins.size();++i)error=std::max(error,std::abs(result.bins[i]-fixture.histogram[i])/std::max(1.f,maximum));
   ok&=error<.0001f;
  }else for(auto x:result.bins)ok&=x==0;
  worst=std::max(worst,error);
  std::cout<<(ok?"PASS ":"FAIL ")<<fixture.name<<" bpm="<<result.bpm<<" reference="<<fixture.bpm<<" histogram_error="<<error<<"\n";
  if(!ok)++failures;
 }
 std::cout<<"Worst relative histogram error: "<<worst<<"; failures="<<failures<<"\n";return failures?1:0;
}
