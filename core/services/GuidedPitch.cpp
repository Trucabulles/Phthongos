#include "GuidedPitch.h"
#include <algorithm>
#include <array>
#include <numeric>
namespace phthongos::services {
namespace {
using juce::var;
var obj(std::initializer_list<std::pair<const char*,var>> xs){auto* p=new juce::DynamicObject;for(auto& x:xs)p->setProperty(x.first,x.second);return var(p);}
void check(const std::atomic_bool& cancelled){if(cancelled.load())throw std::invalid_argument("Analyse guidée annulée.");}
double hz(double pitch){return 440.*std::exp2((pitch-69.)/12.);}
std::vector<std::uint8_t> raster(const SpectralMask& mask,int frames,int bins,double start,double hop,int low,const std::atomic_bool& cancel){
 std::vector<std::uint8_t> values(std::size_t(frames)*bins,0);
 for(auto& ptr:mask){auto& s=*ptr;auto previous=s.points.front();for(auto& p:s.points){
  int first=std::max(0,int(std::floor((std::min(previous.time,p.time)-s.timeRadius-start)/hop))),last=std::min(frames-1,int(std::ceil((std::max(previous.time,p.time)+s.timeRadius-start)/hop)));
  int bottom=std::max(0,int(std::floor((std::min(previous.pitch,p.pitch)-s.pitchRadius-low+.5)*6-.5))),top=std::min(bins-1,int(std::ceil((std::max(previous.pitch,p.pitch)+s.pitchRadius-low+.5)*6-.5)));
  double ax=previous.time/s.timeRadius,ay=previous.pitch/s.pitchRadius,dx=(p.time-previous.time)/s.timeRadius,dy=(p.pitch-previous.pitch)/s.pitchRadius,d=dx*dx+dy*dy;
  for(int f=first;f<=last;++f){if((f&255)==0)check(cancel);double x=(start+f*hop)/s.timeRadius;for(int j=bottom;j<=top;++j){double y=(low-.5+(j+.5)/6.)/s.pitchRadius,at=d>0?std::clamp(((x-ax)*dx+(y-ay)*dy)/d,0.,1.):0.;if(std::hypot(x-ax-at*dx,y-ay-at*dy)<=1)values[std::size_t(f)*bins+j]=s.erase?0:1;}}
  previous=p;
 }}return values;
}
}
juce::var guidedPitch(const analysis::StereoPcm& pcm,unsigned rate,double start,double end,int low,int high,const SpectralMask& mask,bool useMask,const std::atomic_bool& cancelled,std::function<void(double)> progress,const GuidedOptions& options){
 if(rate!=22050||low<0||high>127||low>high||!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start||end>double(pcm.size())/rate+.001)throw std::invalid_argument("Zone d’analyse guidée invalide.");
 if(options.scale&&(options.scale->tonic<0||options.scale->tonic>11||int(options.scale->mode)<0||int(options.scale->mode)>11))throw std::invalid_argument("Mode de gamme invalide.");
 validateMask(mask);if(useMask&&mask.empty())throw std::invalid_argument("Peignez une zone ou désactivez le masque pour examiner tout le registre.");
 constexpr int fftSize=8192,hopSize=256;const double hop=double(hopSize)/rate,binHz=double(rate)/fftSize;
 const int frames=int(std::ceil((end-start)/hop)),bins=(high-low+1)*6,unvoiced=bins,width=bins+1;
 if(frames<1||frames>37000)throw std::invalid_argument("Analyse guidée limitée à moins de sept minutes.");
 check(cancelled);auto coverage=useMask?raster(mask,frames,bins,start,hop,low,cancelled):std::vector<std::uint8_t>{};
 if(useMask&&std::none_of(coverage.begin(),coverage.end(),[](auto x){return x!=0;}))throw std::invalid_argument("Le masque ne contient aucune zone dans l’intervalle et le registre choisis.");
 juce::dsp::FFT fft(13);std::vector<float> left(fftSize*2),right(fftSize*2),power(fftSize/2+1);std::array<float,fftSize> window{};for(int i=0;i<fftSize;++i)window[i]=float(.5-.5*std::cos(juce::MathConstants<double>::twoPi*i/(fftSize-1)));
 std::vector<float> previous(width,0),current(width),best(width),strength(std::size_t(frames),0),rms(std::size_t(frames),0);std::vector<std::uint16_t> back(std::size_t(frames)*width),from(width);std::vector<float> qualities(std::size_t(frames)*bins,0);
 std::vector<std::uint16_t> harmonicEnergy(std::size_t(frames)*bins,0);std::vector<std::uint8_t> harmonicSupport(std::size_t(frames)*bins,0);
 std::vector<std::uint16_t> salience(std::size_t(frames)*bins,0);
 std::vector<double> frequencies(bins);for(int j=0;j<bins;++j)frequencies[j]=hz(low-.5+(j+.5)/6.);
 auto sample=[&](double f){double at=f/binHz;if(at<1||at>=power.size()-2)return 0.;int k=int(at);return power[k]*(1-(at-k))+power[k+1]*(at-k);};
 for(int frame=0;frame<frames;++frame){check(cancelled);if(progress&&(frame%128==0))progress(.75*double(frame)/frames);double time=start+frame*hop;auto center=std::int64_t(std::llround(time*rate));std::fill(left.begin(),left.end(),0.f);std::fill(right.begin(),right.end(),0.f);double shortEnergy=0;
  for(int i=0;i<fftSize;++i){auto index=center+i-fftSize/2;if(index<0||index>=std::int64_t(pcm.size()))continue;auto a=pcm[std::size_t(index)];left[i]=a[0]*window[i];right[i]=a[1]*window[i];if(std::abs(i-fftSize/2)<512)shortEnergy+=(double(a[0])*a[0]+double(a[1])*a[1])*.5/1023.;}
  rms[frame]=float(std::sqrt(shortEnergy));fft.performRealOnlyForwardTransform(left.data());fft.performRealOnlyForwardTransform(right.data());double energy=0,logs=0;
  for(int k=0;k<=fftSize/2;++k){power[k]=(left[k*2]*left[k*2]+left[k*2+1]*left[k*2+1]+right[k*2]*right[k*2]+right[k*2+1]*right[k*2+1])*.5f;energy+=power[k];logs+=std::log(std::max(double(power[k]),1e-20));}
  double average=energy/power.size(),flatness=std::exp(logs/power.size())/(average+1e-20),tonality=std::clamp(1-flatness*1.8,.02,1.);
  for(int j=0;j<bins;++j){double score=0,weights=0,supportedWeights=0;int support=0,gcd=0;double fundamental=0,explained=0,fundamentalPower=0,weightedPower=0;
   if(rms[frame]>1e-5&&(!useMask||coverage[std::size_t(frame)*bins+j]))for(int h=1;h<=8;++h){double f=frequencies[j]*h;if(f>=rate*.48)break;int k=int(std::round(f/binHz));for(int q=std::max(1,k-1);q<=std::min(int(power.size())-1,k+1);++q)explained+=power[q];double noise=0;int n=0;for(int shift=-8;shift<=8;++shift)if(std::abs(shift)>2&&k+shift>0&&k+shift<int(power.size())){noise+=power[k+shift];++n;}noise=std::max(average*1e-7,n?noise/n*.2:average);
    double partial=sample(f),ratio=partial/(energy+1e-20);
    double evidence=std::clamp(std::log((partial+1e-20)/(noise+1e-20))-.5,0.,10.),weight=1/std::pow(double(h),.9);
    // A high peak/noise ratio in an almost empty bin is not an independent
    // harmonic. Otherwise tiny odd-bin noise makes octave submultiples look
    // just as convincing as the observed fundamental.
    bool material=ratio>.001;
    if(h==1){fundamental=evidence;fundamentalPower=ratio;}
    if(evidence>1.5&&material){++support;supportedWeights+=weight;gcd=std::gcd(gcd,h);}
    weightedPower+=std::max(0.,partial-noise)/(h*h);
    score+=weight*evidence*std::clamp(std::sqrt(ratio/.001),0.,1.);weights+=weight;
   }
   // A weak fundamental is allowed when several coprime harmonics agree.
   if(fundamental<1.5&&support>=3&&gcd==1)weights=std::min(weights,supportedWeights+.5);
   score=weights?score/weights*tonality:0.;if(gcd>1)score*=.2;if(support==1&&fundamental<1.5)score*=.15;
   auto index=std::size_t(frame)*bins+j;harmonicEnergy[index]=std::uint16_t(std::clamp(explained/(energy+1e-20),0.,1.)*65535);harmonicSupport[index]=std::uint8_t(support+(fundamental>=2.5&&fundamentalPower>.0025?16:0));
   salience[index]=std::uint16_t(std::clamp(weightedPower/(energy+1e-20),0.,1.)*65535);
   // Harmonic evidence must explain a non-negligible part of the observed
   // spectrum. A high local peak/noise ratio alone also occurs in near-silence
   // and in unrelated partials. Keep weak fundamentals when their harmonics agree.
   double explainedRatio=harmonicEnergy[index]/65535.;
   double reliability=std::clamp(std::sqrt(explainedRatio/.02),0.,1.);
   score*=reliability;
   qualities[std::size_t(frame)*bins+j]=float(score);strength[frame]=std::max(strength[frame],float(score));
   current[j]=(!useMask||coverage[std::size_t(frame)*bins+j])?float(std::clamp((score-1.1)*.8,-3.,2.)):-10000.f;
   // A bounded preference only for ambiguous evidence. Never force strong
   // out-of-scale events, vibrato or glides onto the declared scale.
   if(options.scale&&!options.scale->contains(int(std::round(low-.5+(j+.5)/6.)))&&score<3.5)current[j]-=float(.12*(1-score/3.5));
  }
  double framePeak=0;
  for(int j=0;j<bins;++j)if(qualities[size_t(frame)*bins+j]>1.4)framePeak=std::max(framePeak,double(salience[size_t(frame)*bins+j]));
  if(framePeak>0)for(int j=0;j<bins;++j)current[j]+=float(.35*std::log(std::clamp(double(salience[size_t(frame)*bins+j])/framePeak,.05,1.)));
  int global=int(std::max_element(previous.begin(),previous.begin()+bins)-previous.begin());
  for(int j=0;j<bins;++j){best[j]=previous[j];from[j]=std::uint16_t(j);if(j&&best[j-1]-.018f>best[j]){best[j]=best[j-1]-.018f;from[j]=from[j-1];}}
  for(int j=bins-2;j>=0;--j)if(best[j+1]-.018f>best[j]){best[j]=best[j+1]-.018f;from[j]=from[j+1];}
  for(int j=0;j<bins;++j){if(previous[global]-.65f>best[j]){best[j]=previous[global]-.65f;from[j]=std::uint16_t(global);}if(previous[unvoiced]-.3f>best[j]){best[j]=previous[unvoiced]-.3f;from[j]=std::uint16_t(unvoiced);}current[j]+=best[j];back[std::size_t(frame)*width+j]=from[j];}
  current[unvoiced]=std::max(previous[unvoiced],previous[global]-.2f);back[std::size_t(frame)*width+unvoiced]=std::uint16_t(previous[unvoiced]>=previous[global]-.2f?unvoiced:global);
  float maximum=*std::max_element(current.begin(),current.end());for(auto& x:current)x-=maximum;previous.swap(current);
 }
 std::vector<int> path(frames);int chosen=int(std::max_element(previous.begin(),previous.end())-previous.begin());for(int i=frames;i-->0;){path[i]=chosen;chosen=back[std::size_t(i)*width+chosen];}
 // Independent per-pitch islands are found in the spectral evidence before
 // the monophonic path is rounded. They can recover a stable energetic note
 // which the path temporarily replaced with a sub-octave or an unrelated peak.
 struct CoreEvent{int pitch=0,first=0,last=0,dataFirst=0;double quality=0,energy=0,relative=0;std::vector<double> levels,attacks;};
 std::vector<CoreEvent> coreEvents;
 std::vector<std::uint16_t> pitchBins(size_t(frames)*(high-low+1),std::uint16_t(unvoiced));
 std::vector<double> frameCorePeak(frames,0);
 int pitchCount=high-low+1;
 for(int frame=0;frame<frames;++frame){
   check(cancelled);
   for(int j=0;j<bins;++j){auto at=size_t(frame)*bins+j;
     auto support=harmonicSupport[at];if(qualities[at]<1.5||((support&15)<2&&!(support&16)))continue;
     int pitch=std::clamp(int(std::round(low-.5+(j+.5)/6.)),low,high);
     auto& coreBin=pitchBins[size_t(frame)*pitchCount+pitch-low];
     double merit=salience[at]*std::min(1.,qualities[at]/3.);
     if(coreBin==unvoiced||merit>salience[size_t(frame)*bins+coreBin]*std::min(1.,qualities[size_t(frame)*bins+coreBin]/3.))coreBin=std::uint16_t(j);
     frameCorePeak[frame]=std::max(frameCorePeak[frame],merit);
   }
 }
 auto frameMerit=[&](int frame,int pitch){int bin=pitchBins[size_t(frame)*pitchCount+pitch-low];return bin==unvoiced?0.:salience[size_t(frame)*bins+bin]*std::min(1.,qualities[size_t(frame)*bins+bin]/3.);};
 for(int pitch=low;pitch<=high;++pitch){
   int first=-1;
   auto close=[&](int last){if(first<0)return;int begin=first;first=-1;if(last-begin<7)return;
     CoreEvent event;event.pitch=pitch;event.first=begin;event.last=last;
     double stable=0;for(int k=begin;k<last;++k){int bin=pitchBins[size_t(k)*pitchCount+pitch-low];if(bin==unvoiced)continue;auto at=size_t(k)*bins+bin;
       event.quality+=std::clamp((qualities[at]-.9)/3.,0.,1.);event.energy+=harmonicEnergy[at]/65535.;
       event.relative+=frameMerit(k,pitch)/std::max(1.,frameCorePeak[k]);
       stable+=std::abs(low-.5+(bin+.5)/6.-pitch)<.45?1:0;}
     double length=last-begin;event.quality/=length;event.energy/=length;event.relative/=length;
     if(event.quality>=.35&&event.energy>=.04&&stable/length>=.65){if(coreEvents.size()>=10000)throw std::invalid_argument("Plus de 10 000 propositions : examinez un intervalle plus court.");coreEvents.push_back(std::move(event));}
   };
   for(int k=0;k<frames;++k){bool active=frameCorePeak[k]>0&&frameMerit(k,pitch)>=frameCorePeak[k]*.45;
     if(active){if(first<0)first=k;}else close(k);
   }close(frames);
 }
 // An overtone does not form an independent note merely by being bright.
 // Compare actual active core candidates, not just pitch arithmetic.
 std::vector<CoreEvent> independent;
 std::sort(coreEvents.begin(),coreEvents.end(),[](const auto& a,const auto& b){return a.pitch<b.pitch;});
 for(const auto& event:coreEvents){bool explained=false;
   for(const auto& lower:coreEvents){if(lower.pitch>=event.pitch)break;
     double harmonic=std::exp2((event.pitch-lower.pitch)/12.);int h=int(std::round(harmonic));
     if(h<2||h>8||std::abs(12*std::log2(harmonic/h))>.35)continue;
     int common=std::min(event.last,lower.last)-std::max(event.first,lower.first);
     if(common>(event.last-event.first)*.7&&lower.quality>=event.quality*.65&&lower.relative>=event.relative*.65){explained=true;break;}
   }
   if(!explained)independent.push_back(event);
 }
 coreEvents=std::move(independent);
 std::vector<int> owner(frames,-1);std::vector<double> ownerMerit(frames,0);
 for(const auto& event:coreEvents)for(int k=event.first;k<event.last;++k){double merit=frameMerit(k,event.pitch);
   if(merit>ownerMerit[k]){ownerMerit[k]=merit;owner[k]=event.pitch;}
 }
 for(int k=0;k<frames;++k)if(owner[k]>=0){int candidate=pitchBins[size_t(k)*pitchCount+owner[k]-low];
   double old=path[k]==unvoiced?0:salience[size_t(k)*bins+path[k]];
   if(path[k]==unvoiced||salience[size_t(k)*bins+candidate]>old*1.4)path[k]=candidate;
 }
 // The displayed trajectory is continuous in musical pitch, not snapped to MIDI.
 // Sampling at one sixth of a semitone retains estimated vibrato and glides.
 juce::Array<var> curve;std::vector<double> pitches(frames,-1),confidence(frames,0);
 for(int i=0;i<frames;++i){if(path[i]==unvoiced)continue;confidence[i]=std::clamp((qualities[std::size_t(i)*bins+path[i]]-.9)/3.,0.,1.);if(confidence[i]<.12)continue;pitches[i]=low-.5+(path[i]+.5)/6.;}
 for(int i=0;i<frames;++i){juce::Array<var> point;point.add(start+i*hop);point.add(pitches[i]>=0?var(pitches[i]):var());point.add(confidence[i]);curve.add(var(point));}
 std::vector<int> labels(frames,-1);for(int i=0;i<frames;++i)if(pitches[i]>=0){std::vector<double> neighborhood;for(int j=std::max(0,i-4);j<=std::min(frames-1,i+4);++j)if(pitches[j]>=0)neighborhood.push_back(pitches[j]);std::sort(neighborhood.begin(),neighborhood.end());labels[i]=std::clamp(int(std::round(neighborhood[neighborhood.size()/2])),low,high);}
 // Hysteresis around a sustained pitch prevents a vibrato crossing a
 // half-semitone rounding boundary from becoming repeated MIDI notes. Require
 // three supported frames beyond the outer boundary before changing notes.
 for(int i=0,currentPitch=-1,candidate=-1,pending=0;i<frames;++i){
   if(labels[i]<0){currentPitch=-1;candidate=-1;pending=0;continue;}
   if(currentPitch<0){currentPitch=labels[i];continue;}
   if(std::abs(pitches[i]-currentPitch)<=.65){labels[i]=currentPitch;candidate=-1;pending=0;continue;}
   auto next=labels[i];if(next==currentPitch){candidate=-1;pending=0;continue;}
   if(candidate!=next){candidate=next;pending=1;}else ++pending;
   labels[i]=currentPitch;
   if(pending>=3){for(int j=i-pending+1;j<=i;++j)labels[j]=candidate;currentPitch=candidate;candidate=-1;pending=0;}
 }
 // A second, shorter window measures the energetic core and attacks. The long
 // window above resolves pitch; using it alone smears attacks and release tails.
 // Positive harmonic flux uses a frequency-local maximum in the older frame
 // (SuperFlux principle), so a moving vibrato partial is not a new attack.
 constexpr int onsetSize=2048;
 juce::dsp::FFT onsetFft(11);
 std::vector<float> onsetLeft(onsetSize*2),onsetRight(onsetSize*2),magnitude(onsetSize/2+1),older(magnitude.size()),recent(magnitude.size());
 std::vector<double> coreLevel(frames,0),novelty(frames,0);
 std::vector<std::vector<size_t>> activeCores(frames);
 std::vector<std::uint8_t> eventSupport(size_t(frames)*pitchCount,0);
 for(size_t id=0;id<coreEvents.size();++id){auto& event=coreEvents[id];event.dataFirst=std::max(0,event.first-8);int dataLast=std::min(frames,event.last+8);event.levels.resize(dataLast-event.dataFirst);event.attacks.resize(dataLast-event.dataFirst);
   for(int k=event.dataFirst;k<dataLast;++k)activeCores[k].push_back(id);
   for(int k=event.first;k<event.last;++k)eventSupport[size_t(k)*pitchCount+event.pitch-low]=1;
 }
 std::array<float,onsetSize> onsetWindow{};
 for(int i=0;i<onsetSize;++i)onsetWindow[i]=float(.5-.5*std::cos(juce::MathConstants<double>::twoPi*i/(onsetSize-1)));
 for(int frame=0;frame<frames;++frame){
   check(cancelled);if(progress&&(frame%128==0))progress(.75+.15*double(frame)/frames);
   auto center=std::int64_t(std::llround((start+frame*hop)*rate));
   std::fill(onsetLeft.begin(),onsetLeft.end(),0.f);std::fill(onsetRight.begin(),onsetRight.end(),0.f);
   for(int i=0;i<onsetSize;++i){auto at=center+i-onsetSize/2;if(at>=0&&at<std::int64_t(pcm.size())){onsetLeft[i]=pcm[size_t(at)][0]*onsetWindow[i];onsetRight[i]=pcm[size_t(at)][1]*onsetWindow[i];}}
   onsetFft.performRealOnlyForwardTransform(onsetLeft.data());onsetFft.performRealOnlyForwardTransform(onsetRight.data());
   for(size_t k=0;k<magnitude.size();++k)magnitude[k]=std::sqrt((onsetLeft[2*k]*onsetLeft[2*k]+onsetLeft[2*k+1]*onsetLeft[2*k+1]+onsetRight[2*k]*onsetRight[2*k]+onsetRight[2*k+1]*onsetRight[2*k+1])*.5f);
   double pitch=pitches[frame];
   if(pitch<0)for(int offset=1;offset<=4&&pitch<0;++offset){if(frame>=offset&&pitches[frame-offset]>=0)pitch=pitches[frame-offset];else if(frame+offset<frames&&pitches[frame+offset]>=0)pitch=pitches[frame+offset];}
   auto measure=[&](double candidatePitch){double powerSum=0,flux=0,total=0;
     if(candidatePitch<0)return std::pair<double,double>{0,0};
     for(int h=1;h<=8;++h){double frequency=hz(candidatePitch)*h;if(frequency>=rate*.48)break;int k=int(std::round(frequency*onsetSize/rate));
       double peak=0,oldPeak=0;int radius=std::max(2,int(std::ceil(k*.04)));
       for(int q=std::max(1,k-1);q<=std::min(int(magnitude.size())-1,k+1);++q)peak=std::max(peak,double(magnitude[size_t(q)]));
       for(int q=std::max(1,k-radius);q<=std::min(int(older.size())-1,k+radius);++q)oldPeak=std::max(oldPeak,double(older[size_t(q)]));
       double weight=1./h;powerSum+=weight*peak*peak;total+=weight*peak;flux+=weight*std::max(0.,peak-oldPeak);
     }
     return std::pair<double,double>{std::sqrt(powerSum)/(onsetSize*.5),total>1e-8?flux/total:0};
   };
   auto measured=measure(pitch);coreLevel[frame]=measured.first;novelty[frame]=measured.second;
   for(auto id:activeCores[frame]){auto& event=coreEvents[id];int bin=pitchBins[size_t(frame)*pitchCount+event.pitch-low];double candidatePitch=bin==unvoiced?event.pitch:low-.5+(bin+.5)/6.;auto local=measure(candidatePitch);event.levels[frame-event.dataFirst]=local.first;event.attacks[frame-event.dataFirst]=local.second;}

   older.swap(recent);recent.swap(magnitude);
 }
 // Bridge only very short unvoiced gaps with the same supported pitch on both
 // sides, continuous local energy, no new attack, and explicit mask coverage.
 for(int i=1;i+1<frames;){if(labels[i]>=0){++i;continue;}int j=i;while(j<frames&&labels[j]<0)++j;
   if(j<frames&&j-i<=4&&labels[i-1]>=0&&labels[i-1]==labels[j]){bool continuous=true;
     double floor=std::min(coreLevel[i-1],coreLevel[j])*.45;
     int bin=std::clamp(int(std::round((labels[i-1]-low+.5)*6-.5)),0,bins-1);
     for(int k=i;k<j;++k)continuous&=coreLevel[k]>=floor&&novelty[k]<.15&&(!useMask||coverage[size_t(k)*bins+bin]);
     if(continuous)for(int k=i;k<j;++k)labels[k]=labels[i-1];
   }i=j;
 }
 std::vector<double> coreLevels;
 for(int i=0;i<frames;++i)if(labels[i]>=0&&confidence[i]>=.12&&coreLevel[i]>1e-8)coreLevels.push_back(coreLevel[i]);
 std::sort(coreLevels.begin(),coreLevels.end());
 double referenceCore=coreLevels.empty()?1e-8:std::max(1e-8,coreLevels[std::min(coreLevels.size()-1,coreLevels.size()*9/10)]);
 auto levels=rms;std::sort(levels.begin(),levels.end());double referenceLevel=std::max(1e-8,double(levels[std::min(levels.size()-1,levels.size()*9/10)]));
 juce::Array<var> notes;int number=0;
 auto emit=[&](int first,int last,int pitch,const CoreEvent* event=nullptr){
   if(last<=first)return;
   int originalFirst=first,originalLast=last;
   auto localLevel=[&](int i){if(!event)return coreLevel[i];int index=i-event->dataFirst;return index>=0&&index<int(event->levels.size())?event->levels[size_t(index)]:0.;};
   auto localAttack=[&](int i){if(!event)return novelty[i];int index=i-event->dataFirst;return index>=0&&index<int(event->attacks.size())?event->attacks[size_t(index)]:0.;};

   std::vector<double> local;for(int i=first;i<last;++i)local.push_back(localLevel(i));std::sort(local.begin(),local.end());
   double peak=local[std::min(local.size()-1,local.size()*9/10)],threshold=std::max(peak*.20,referenceCore*.008);
   // Keep the weaker attack/release shoulders, but exclude a clearly fading
   // tail. This is an acoustic duration estimate, never a claim about key-up.
   while(first+2<last&&localLevel(first)<threshold)++first;
   while(last>first+2&&localLevel(last-1)<threshold)--last;
   // Do not push an already audible soft attack far into the bright core.
   // Likewise a short supported event must not disappear merely because its
   // trimmed core falls below the minimum proposal duration.
   if(localLevel(originalFirst)>=peak*.05)first=std::min(first,originalFirst+2);
   constexpr int minimumFrames=6;
   if(originalLast-originalFirst>=minimumFrames&&last-first<minimumFrames){
     first=std::max(originalFirst,std::min(first,originalLast-minimumFrames));
     last=std::min(originalLast,std::max(last,first+minimumFrames));
   }
   double a=std::max(start,start+(first-.5)*hop),b=std::min(end,start+(last-.5)*hop),quality=0,explained=0,supported=0,stable=0,level=0,dominance=0,core=0,attack=0,coreMean=0,eventFraction=0;int attackFrame=first;
   for(int i=std::max(0,first-3);i<std::min(frames,first+7);++i)if(localAttack(i)>attack){attack=localAttack(i);attackFrame=i;}
   for(int i=first;i<last;++i){quality+=confidence[i];level+=rms[i];coreMean+=localLevel(i);core+=localLevel(i)>=std::max(peak*.30,referenceCore*.05)?1:0;
     eventFraction+=eventSupport[size_t(i)*pitchCount+pitch-low]?1:0;
     int bin=event?pitchBins[size_t(i)*pitchCount+pitch-low]:path[i];if(bin==unvoiced)continue;
     auto index=size_t(i)*bins+bin;double localPitch=low-.5+(bin+.5)/6.;
     if(event)quality+=std::clamp((qualities[index]-.9)/3.,0.,1.)-confidence[i];
     dominance+=qualities[index]/std::max(1e-8f,strength[i]);explained+=harmonicEnergy[index]/65535.;auto support=harmonicSupport[index];supported+=((support&15)>=2||(support&16))?1:0;stable+=std::abs(localPitch-pitch)<=.45?1:0;
   }
   double count=last-first;quality/=count;explained/=count;supported/=count;stable/=count;level/=count*referenceLevel;dominance/=count;core/=count;coreMean/=count;eventFraction/=count;
   double before=0,after=0;int beforeCount=0,afterCount=0;
   for(int i=std::max(0,attackFrame-4);i<attackFrame;++i){before+=localLevel(i);++beforeCount;}
   for(int i=attackFrame;i<std::min(frames,attackFrame+4);++i){after+=localLevel(i);++afterCount;}
   before/=std::max(1,beforeCount);after/=std::max(1,afterCount);
   double growth=beforeCount?after/std::max(referenceCore*.005,before):1,increment=beforeCount?(after-before)/referenceCore:0;
   if(b-a<.065||quality<.14)return;
   double peakRelative=peak/referenceCore;
   int velocity=std::clamp(int(std::lround(30+80*std::sqrt(std::clamp(coreMean/referenceCore,0.,1.5)))),1,127);
   if(notes.size()>=10000)throw std::invalid_argument("Plus de 10 000 propositions : examinez un intervalle plus court.");
   notes.add(obj({{"id",juce::String("guided-")+juce::String(++number)},{"pitch",pitch},{"start",a},{"end",b},{"velocity",velocity},{"score",quality},
     {"evidence",obj({{"harmonic_energy",explained},{"harmonic_support",supported},{"stability",stable},{"relative_level",level},{"dominance",dominance},
       {"core_fraction",core},{"core_peak_relative",peakRelative},{"attack_strength",attack},{"attack_growth",growth},{"attack_increment",increment},{"event_core_fraction",eventFraction},{"source",event?"spectral_core":"trajectory"},{"resonance_tail_s",(originalLast-last)*hop},{"attack_shoulder_s",(first-originalFirst)*hop}})}}));
 };
 for(int i=0;i<frames;){int j=i+1;while(j<frames&&labels[j]==labels[i])++j;if(labels[i]>=0){int first=i;for(int k=i+3;k+2<j;++k){
   double before=(coreLevel[k-3]+coreLevel[k-2]+coreLevel[k-1])/3.,after=(coreLevel[k]+coreLevel[k+1]+coreLevel[k+2])/3.;
   bool peak=novelty[k]>=novelty[k-1]&&novelty[k]>=novelty[k+1];
   bool rising=peak&&novelty[k]>.12&&after>before*1.18&&after>referenceCore*.03&&confidence[k]>.2;
   if(k-first>=10&&rising){emit(first,k,labels[i]);first=k;k+=3;}
 }emit(first,j,labels[i]);}i=j;}
 for(const auto& event:coreEvents){
   int first=event.first;
   auto add=[&](int begin,int last){double a=start+(begin-.5)*hop,b=start+(last-.5)*hop;bool covered=false;
     for(const auto& n:notes)if(int(n["pitch"])==event.pitch){double common=std::min(b,double(n["end"]))-std::max(a,double(n["start"]));if(common>std::max(.025,.25*std::min(b-a,double(n["end"])-double(n["start"])))){covered=true;break;}}
     if(!covered)emit(begin,last,event.pitch,&event);
   };
   for(int k=first+3;k+2<event.last;++k){int index=k-event.dataFirst;double before=(event.levels[index-3]+event.levels[index-2]+event.levels[index-1])/3.,after=(event.levels[index]+event.levels[index+1]+event.levels[index+2])/3.;
     if(k-first>=10&&event.attacks[index]>.12&&event.attacks[index]>=event.attacks[index-1]&&event.attacks[index]>=event.attacks[index+1]&&after>before*1.18&&after>referenceCore*.03){add(first,k);first=k;k+=3;}
   }add(first,event.last);
 }
 std::vector<var> ordered;for(const auto& note:notes)ordered.push_back(note);
 std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return double(a["start"])!=double(b["start"])?double(a["start"])<double(b["start"]):int(a["pitch"])<int(b["pitch"]);});
 notes.clear();number=0;for(auto note:ordered){note.getDynamicObject()->setProperty("id",juce::String("guided-")+juce::String(++number));notes.add(note);}
 // Reversible proposal screening, independent of MIDI editing. No single
 // duration, leap or scale criterion can reject an otherwise convincing event.
 for(int i=0;i<notes.size();++i){auto n=notes[i];auto evidence=n["evidence"];double duration=double(n["end"])-double(n["start"]),quality=double(n["score"]),energy=double(evidence["harmonic_energy"]),support=double(evidence["harmonic_support"]),stability=double(evidence["stability"]),level=double(evidence["relative_level"]);
   bool shortNote=duration<.12,weakEnergy=energy<.025,weakSupport=support<.5,unstable=stability<.6,quiet=level<.01;
   bool isolatedJump=false;
   if(i>0&&i+1<notes.size()){auto previousNote=notes[i-1],next=notes[i+1];isolatedJump=std::abs(int(previousNote["pitch"])-int(next["pitch"]))<=2&&std::abs(int(n["pitch"])-int(previousNote["pitch"]))>=10&&double(n["start"])-double(previousNote["end"])<.35&&double(next["start"])-double(n["end"])<.35&&duration<.25;}
   bool contradicted=double(evidence["dominance"])<.85;
   bool weakCore=double(evidence["core_peak_relative"])<.08||double(evidence["core_fraction"])<.35;
   bool noAttack=double(evidence["attack_strength"])<.12;
   // Weak, unsupported sustained residues and brief excursions without a new
   // attack are screened out of the default view, but remain reversible proposals.
   bool residual=(weakCore&&(weakEnergy||weakSupport||contradicted))||
     (noAttack&&double(evidence["core_peak_relative"])<.28&&(weakEnergy||shortNote||contradicted));

   int weaknesses=int(contradicted)+int(shortNote)+int(weakEnergy)+int(weakSupport)+int(unstable)+int(quiet)+int(isolatedJump)+int(weakCore)+int(noAttack&&shortNote);
   bool materialAttack=double(evidence["attack_strength"])>=.12&&double(evidence["attack_growth"])>=1.2&&double(evidence["attack_increment"])>=.015;
   bool coherentCore=double(evidence["event_core_fraction"])>=.65&&double(evidence["core_fraction"])>=.65&&double(evidence["core_peak_relative"])>=.12&&energy>=.05&&support>=.65&&stability>=.65&&quality>=.4;
   bool supportedUncertainty=quality>=.28&&support>=.5&&(
     (double(evidence["event_core_fraction"])>=.25&&!weakCore&&stability>=.45)||
     (materialAttack&&stability>=.45&&energy>=.04)||
     (duration>=.25&&!weakCore&&energy>=.15&&stability>=.85));
   bool shortSupported=duration>=.12||coherentCore||(materialAttack&&quality>=.5&&energy>=.1&&stability>=.6);
   bool decayContinuation=false;
   if(!materialAttack&&double(evidence["core_peak_relative"])<.3){
     for(int j=i-1;j>=0;--j){auto old=notes[j];double gap=double(n["start"])-double(old["end"]);if(gap<-.03||gap>1.5)continue;
       int distance=std::abs(int(old["pitch"])-int(n["pitch"]));
       if(distance!=0&&distance!=12&&distance!=19&&distance!=24)continue;
       if(old["category"].toString()=="probable"&&double(old["evidence"]["core_peak_relative"])>double(evidence["core_peak_relative"])*2.5){decayContinuation=true;break;}
     }
   }
   evidence.getDynamicObject()->setProperty("decay_continuation",decayContinuation);
   juce::String category="review";
   if(decayContinuation||!shortSupported||(!coherentCore&&!supportedUncertainty)||(!materialAttack&&weakCore)||residual||(quiet&&(weakEnergy||weakSupport))||(weakEnergy&&(weakSupport||contradicted))||(shortNote&&weaknesses>=3)||(isolatedJump&&weaknesses>=3))category="unlikely";
   else if(coherentCore||(!weakCore&&quality>=.6&&energy>=.025&&support>=.5&&stability>=.6&&level>=.01&&(!shortNote||(quality>=.85&&energy>=.15))&&!isolatedJump))category="probable";
   n.getDynamicObject()->setProperty("category",category);
   n.getDynamicObject()->setProperty("weak_indicators",weaknesses);
 }
 if(progress)progress(1);return obj({{"algorithm","harmonic-events-v3"},{"independent_core_events",int(coreEvents.size())},{"velocity_estimate","relative energy; not a calibrated MIDI velocity"},{"curve",var(curve)},{"notes",var(notes)},{"start",start},{"end",end},{"low",low},{"high",high},{"masked",useMask},{"notice",juce::String::fromUTF8("Hauteurs et notes estimées par DSP : indices à vérifier à l’oreille, pouvant contenir des erreurs d’octave ou de durée. Aucune note MIDI n’est modifiée sans votre validation.")}});
}
}
