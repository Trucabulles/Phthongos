#include "Verification.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <stdexcept>
namespace phthongos::services {
namespace {
using juce::var;using juce::String;using analysis::StereoPcm;using analysis::frequency;using analysis::band;
constexpr int low=0,high=127,partials=6;constexpr double hop=.02;
void check(const std::atomic_bool& cancel){if(cancel.load())throw std::invalid_argument("Vérification annulée.");}
var obj(std::initializer_list<std::pair<const char*,var>> xs){auto* p=new juce::DynamicObject;for(auto& x:xs)p->setProperty(x.first,x.second);return var(p);}
var text(const char* s){return var(String::fromUTF8(s));}
var strings(std::initializer_list<const char*> xs){juce::Array<var> a;for(auto* x:xs)a.add(text(x));return var(a);}
var id(NoteId n){return var(static_cast<juce::int64>(n));}
double num(const var& v,const char* key){return (double)v[key];}
void set(var v,const char* key,var value){v.getDynamicObject()->setProperty(key,value);}
void reason(var v,const char* s){auto a=v["reasons"];a.getArray()->add(text(s));}
double clamp(double x){return std::clamp(x,0.,1.);}
double quantile(std::vector<double> x,double q){if(x.empty())return 0;std::sort(x.begin(),x.end());return x[static_cast<std::size_t>(std::round((x.size()-1)*q))];}
bool melodic(const Note& n){return !n.part||n.part->program!=128;}
bool overlapping(const Note& n,double t,double tail){return melodic(n)&&!n.muted&&n.start<=t+.06&&n.end+tail>=t;}
bool shares(int p,int h,const Note& n){for(int k=1;k<=partials;++k)if(std::abs(std::log(frequency(n.pitch)*k/(frequency(p)*h)))<.018)return true;return false;}
bool selected(NoteId id,const std::vector<NoteId>& ids){return std::find(ids.begin(),ids.end(),id)!=ids.end();}
var known(const VerificationGrid& g,const Note& n,const std::vector<Note>& notes){
 double a=n.start,b=n.end;int p=n.pitch;bool shortNote=b-a<=.1;
 auto e=obj({{"id",id(n.id)},{"pitch",p},{"start",a},{"end",b},{"presence","undetermined"},{"score",.5},{"fundamental",0.},{"harmonics",0.},{"attack",0.},{"continuity",0.},{"shared",0.},{"short",shortNote},{"reasons",strings({})}});
 if(p<low||p>high||a<g.start||b>g.end||n.muted){reason(e,"Contexte audio insuffisant, note muette ou hauteur hors de la plage MIDI.");return e;}
 auto first=g.nearest(a),last=g.nearest(std::min(a+.16,b)),index=first;
 for(auto i=first;i<=last;++i)if(g.frames[i].amp(p,1)>=g.frames[index].amp(p,1))index=i;
 auto& f=g.frames[index];double peak=f.amp(p,1),shared=0,harmonic=0;
 std::vector<const Note*> peers;for(auto& m:notes)if(m.id!=n.id&&overlapping(m,f.time,.12))peers.push_back(&m);
 auto explained=[&](int h){return std::any_of(peers.begin(),peers.end(),[&](auto m){return shares(p,h,*m);});};
 for(int h=1;h<=partials;++h){shared+=explained(h);if(h>=2)harmonic+=f.amp(p,h)/h;}shared/=partials;
 double fundamental=clamp(peak/std::max(f.rms,1e-12)/.25),harmonics=clamp(harmonic/std::max(f.rms,1e-12)/.2);
 double before=g.frames[g.nearest(std::max(a-.12,g.start))].amp(p,1),attack=clamp((peak-before)/std::max(peak,1e-12)),continuity=0,maxAudio=f.rms;
 for(int i=0;i<8;++i){auto& s=g.frames[g.nearest(a+(b-a)*(i+.5)/8)];continuity+=s.amp(p,1)>std::max(peak,1e-8)*.15&&s.amp(p,1)>s.rms*.015;maxAudio=std::max(maxAudio,s.rms);}continuity/=8;
 double score=clamp(.35*fundamental+.25*harmonics+.15*attack+.25*continuity);int isolated=0;for(int h=2;h<=partials;++h)isolated+=f.amp(p,h)>f.rms*.025&&!explained(h);
 bool same=std::any_of(peers.begin(),peers.end(),[&](auto m){return m->pitch==p;});
 set(e,"fundamental",fundamental);set(e,"harmonics",harmonics);set(e,"attack",attack);set(e,"continuity",continuity);set(e,"shared",shared);set(e,"score",score);
 if(maxAudio<1e-7&&b-a>=.045){set(e,"presence","probably_absent");reason(e,"Aucun signal détectable pendant la note, même en tenant compte des autres parties MIDI.");}
 else if(same||shared>.8||(explained(1)&&isolated<2)||p<40||b-a<.045)reason(e,"Harmoniques partagées, doublon possible, note trop brève ou résolution grave insuffisante : attribution incertaine.");
 else if(fundamental<.08&&harmonics<.1&&continuity<.2&&shared<.5){set(e,"presence","probably_absent");reason(e,maxAudio<1e-7?"Aucun signal détectable sur cette portion audio.":"Très peu d’énergie compatible avec cette hauteur, sans continuité ni soutien harmonique.");}
 else if(score>=.72&&fundamental>.35&&continuity>=.5){set(e,"presence","strong");reason(e,"Présence spectrale soutenue et plusieurs indices concordants.");}
 else if(score>=.42&&fundamental>.15){set(e,"presence","probable");reason(e,"Énergie compatible avec la note, sans certitude d’isolation.");}
 else reason(e,"Indices trop faibles ou contradictoires pour conclure à une note en trop.");
 if(attack<.2)reason(e,"Pas d’attaque nette : cela peut aussi correspondre à un lié ou à un onset décalé.");if(shortNote)reason(e,"Note courte : information contextuelle, aucune suppression déduite de sa durée.");return e;
}
using Profiles=std::array<std::array<double,partials>,128>;
Profiles profiles(const VerificationGrid& g,const std::vector<Note>& notes){Profiles out{};for(int p=low;p<=high;++p){out[p][0]=1;
 for(int h=2;h<=partials;++h){std::vector<double> ratios;for(auto& n:notes){if(n.muted||!melodic(n)||n.pitch!=p)continue;double time=n.start+.08;if(time<g.start||time>g.end)continue;
 if(std::any_of(notes.begin(),notes.end(),[&](auto& m){return m.id!=n.id&&m.pitch!=p&&overlapping(m,time,.1)&&shares(p,h,m);}))continue;
 auto& f=g.frames[g.nearest(time)];auto base=f.amp(p,1);if(base>f.rms*.03&&base>1e-8)ratios.push_back(std::clamp(f.amp(p,h)/base,0.,4.));}
 out[p][h-1]=ratios.size()>=3?std::clamp(quantile(ratios,.65)*1.25,.02,3.):1./h;
 }}return out;}
struct Residual{double remaining,total;int independent;};
Residual residual(const VerificationFrame& f,int p,const std::vector<Note>& notes,const Profiles& profiles){Residual out{};
 for(int h=1;h<=partials;++h){double observed=f.amp(p,h),target=frequency(p)*h,explained=0;
 for(auto& m:notes)if(overlapping(m,f.time,.25)&&m.pitch!=p&&m.pitch>=low&&m.pitch<=high)
 for(int k=1;k<=partials;++k)if(std::abs(std::log(frequency(m.pitch)*k/target))<.018)explained=std::max(explained,f.amp(m.pitch,1)*profiles[m.pitch][k-1]);
 double remaining=std::max(observed-explained,0.);out.remaining+=remaining/h;out.total+=observed/h;out.independent+=remaining>observed*.3&&remaining>f.rms*.025;
 }return out;}
void sort(juce::Array<var>& a){std::stable_sort(a.begin(),a.end(),[](auto& a,auto& b){return num(a,"start")==num(b,"start")?(int)a["pitch"]<(int)b["pitch"]:num(a,"start")<num(b,"start");});}
}
double VerificationFrame::amp(int p,int h)const{return p<low||p>high||h<1||h>partials?0:amplitudes[(p-low)*partials+h-1];}
double VerificationFrame::tonal(int p)const{return p<low||p>high?0:prominence[p-low];}
std::size_t VerificationGrid::nearest(double t)const{return std::min(static_cast<std::size_t>(std::max(0.,std::round((t-start)/hop))),frames.size()-1);}
namespace {
// Long windows establish low-pitch selectivity; the short spectrum retains attacks.
// Coarse low-frequency frames are reused for 100 ms, never for onset timing.
class Spectrum {
 juce::dsp::FFT fft;int size;std::vector<float> window,buffer;double norm=0;
public:
 std::vector<float> power;
 explicit Spectrum(int order):fft(order),size(1<<order),window(size),buffer(size*2),power(size/2+1){
  double sum=0;for(int j=0;j<size;++j){window[j]=float(.5-.5*std::cos(juce::MathConstants<double>::twoPi*j/(size-1)));sum+=window[j];}norm=sum*sum;
 }
 void analyse(const StereoPcm& pcm,unsigned rate,double time,const std::atomic_bool& cancel){
  auto centre=std::int64_t(std::round(time*rate));std::fill(power.begin(),power.end(),0.f);
  for(int ch=0;ch<2;++ch){check(cancel);std::fill(buffer.begin(),buffer.end(),0.f);
   for(int j=0;j<size;++j){auto k=centre+j-size/2;if(k>=0&&k<std::int64_t(pcm.size()))buffer[j]=pcm[std::size_t(k)][ch]*window[j];}
   fft.performRealOnlyForwardTransform(buffer.data(),true);
   for(std::size_t k=0;k<power.size();++k){auto re=buffer[k*2],im=buffer[k*2+1];power[k]+=float((re*re+im*im)/norm);}
  }
 }
 double at(double hz,unsigned rate)const{
  if(hz<=0||hz>=rate*.47)return 0;double bin=hz*size/rate;int k=int(bin);double f=bin-k;
  return power[std::size_t(k)]*(1-f)+power[std::size_t(k+1)]*f;
 }
 double peak(double hz,unsigned rate)const{
  if(hz<=0||hz>=rate*.47)return 0;int k=int(std::round(hz*size/rate));
  int radius=std::max(1,int(std::ceil(hz*.008*size/rate)));float peak=0;
  for(int j=std::max(0,k-radius);j<=std::min(int(power.size())-1,k+radius);++j)peak=std::max(peak,power[j]);return peak;
 }
 double tonal(double hz,unsigned rate)const{
  auto centre=at(hz,rate),neighbours=(at(hz*std::exp2(-.65/12),rate)+at(hz*std::exp2(.65/12),rate))*.5;
  return std::min(centre/(neighbours+1e-15),1000.);
 }
};
}
VerificationGrid verificationSpectrum(const analysis::StereoPcm& pcm,unsigned rate,double start,double end,const std::atomic_bool& cancel){
 if(rate<16000||!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start||end>double(pcm.size())/rate+.001||end-start>420||std::any_of(pcm.begin(),pcm.end(),[](auto& f){return !std::isfinite(f[0])||!std::isfinite(f[1]);}))throw std::invalid_argument("Intervalle ou audio invalide pour la vérification.");
 Spectrum shortSpectrum(rate>32000?13:12),bassSpectrum(rate>32000?15:14),lowSpectrum(rate>32000?17:16);
 std::vector<float> previous;VerificationGrid out;out.start=start;out.end=end;out.sampleRate=rate;
 for(std::size_t i=0;i<=std::size_t(std::ceil((end-start)/hop));++i){
  check(cancel);double time=std::min(start+i*hop,end);shortSpectrum.analyse(pcm,rate,time,cancel);
  if(i%5==0){bassSpectrum.analyse(pcm,rate,time,cancel);lowSpectrum.analyse(pcm,rate,time,cancel);}
  auto centre=std::int64_t(std::round(time*rate)),a=std::max<std::int64_t>(0,centre-rate/100),b=std::clamp<std::int64_t>(centre+rate/100,0,pcm.size());double rms=0;
  if(b>a){for(auto k=a;k<b;++k){double l=pcm[std::size_t(k)][0],r=pcm[std::size_t(k)][1];rms+=l*l+r*r;}rms=std::sqrt(rms/(2*(b-a)));}
  const auto& power=shortSpectrum.power;auto size=(power.size()-1)*2;int binEnd=std::min(int(size/2),int(std::min(20000.,rate*.47)*size/rate));double mean=0,logs=0,flux=0,total=0;
  for(int k=1;k<binEnd;++k){mean+=power[k];logs+=std::log(double(power[k])+1e-20);double value=std::sqrt(power[k]);total+=value;if(!previous.empty())flux+=std::max(value-std::sqrt(double(previous[k])),0.);}
  mean/=binEnd-1;VerificationFrame frame;frame.time=time;frame.rms=rms;frame.flatness=mean>1e-20?std::exp(logs/(binEnd-1))/mean:0;
  frame.transient=previous.empty()||rms<1e-6?0:clamp(2*flux/std::max(total,1e-12));previous=power;
  for(int p=low;p<=high;++p){double f=frequency(p);const auto& spectrum=p<36?lowSpectrum:p<60?bassSpectrum:shortSpectrum;
   frame.prominence.push_back(float(spectrum.tonal(f,rate)));
   for(int h=1;h<=partials;++h)frame.amplitudes.push_back(float(std::sqrt(shortSpectrum.peak(f*h,rate))));
  }
  out.frames.push_back(std::move(frame));
 }
 return out;
}
namespace {
void diagnostic(juce::Array<var>& rows,int pitch,const char* key){for(auto row:rows)if(int(row["pitch"])==pitch){set(row,key,int(row[key])+1);return;}}
void reject(juce::Array<var>& rows,juce::Array<var>& rejected,const var& candidate,const char* key){
 diagnostic(rows,int(candidate["pitch"]),key);auto entry=candidate.clone();set(entry,"rejection",key);rejected.add(entry);
}
juce::Array<var> missing(const VerificationGrid& g,const std::vector<Note>& notes,int lo,int hi,std::size_t& onsets,juce::Array<var>& diagnostics,juce::Array<var>& rejected,const std::atomic_bool& cancel){
 std::array<std::array<int,7>,128> counts{};auto prof=profiles(g,notes);juce::Array<var> out;for(int p=std::max(lo,low);p<=std::min(hi,high);++p){check(cancel);double covered=g.start;
 for(std::size_t i=6;i+5<g.frames.size();++i){auto& f=g.frames[i];if(f.time<covered)continue;auto& count=counts[p];if(f.rms<1e-6||f.flatness>.25){++count[0];continue;}
 if(std::any_of(notes.begin(),notes.end(),[&](auto& n){return n.pitch==p&&overlapping(n,f.time,.10);}))continue;
 double energy=f.amp(p,1),pre=g.frames[i-5].amp(p,1),next=g.frames[i+1].amp(p,1),attack=clamp((energy-pre)/std::max(energy,1e-12));
if(energy<f.rms*.12||next<energy*.6){++count[0];continue;}
 // A very low fundamental needs several periods after the attack to become selective.
 auto settle=p<36?std::size_t(std::ceil(std::min(.5,4/frequency(p))/hop)):0;
 auto tonal=[&](std::size_t j){return std::max(g.frames[j].tonal(p),g.frames[std::min(j+settle,g.frames.size()-1)].tonal(p));};
 if(tonal(i)<1.35){++count[1];continue;}
 double harmonicRise=0;for(int h=2;h<=partials;++h){auto value=f.amp(p,h);if(value>f.rms*.025)harmonicRise=std::max(harmonicRise,clamp((value-g.frames[i-5].amp(p,h))/std::max(value,1e-12)));}
 bool transientSupport=f.transient>.3&&attack>=.22&&harmonicRise>=.25;
 if(attack<.4&&!transientSupport){++count[2];continue;}
 auto r=residual(f,p,notes,prof);double excess=r.remaining/std::max(r.total,1e-12);bool clear=r.independent==1&&energy>f.rms*.4&&f.tonal(p)>3&&f.flatness<.03;
 if(excess<.25||r.remaining<f.rms*.12||(r.independent<2&&!clear)){++count[3];continue;}++onsets;
 auto stop=std::min(i+4,g.frames.size()-1);double stable=0;for(auto j=i;j<=stop;++j)stable+=g.frames[j].amp(p,1)>energy*.4&&tonal(j)>1.25;stable/=stop-i+1;if(stable<.6){++count[4];continue;}
 double onset=f.time;for(std::size_t j=i;j-->i-5;){if(g.frames[j].amp(p,1)<energy*.25)break;onset=g.frames[j].time;}
 double peak=energy;for(auto j=i;j<=std::min(i+8,g.frames.size()-1);++j)peak=std::max(peak,g.frames[j].amp(p,1));double end=std::min(f.time+.08,g.end);int quiet=0;
 for(auto j=i+1;j<g.frames.size()&&j<i+401;++j){auto& s=g.frames[j];if(j>i+6&&s.amp(p,1)>g.frames[j-4].amp(p,1)*1.7&&s.amp(p,1)>peak*.35){auto post=residual(s,p,notes,prof).remaining,pre=residual(g.frames[j-4],p,notes,prof).remaining;if(post>std::max(pre,s.rms*.03)*1.7&&post>s.rms*.12){end=std::max(s.time-.04,onset+.06);break;}}
 if(s.amp(p,1)<peak*.15||s.amp(p,1)<s.rms*.035)++quiet;else quiet=0;end=s.time;if(quiet>=4){end=std::max(s.time-3*hop,onset+hop);break;}}
 if(end-onset<.06){++count[5];continue;}++count[6];double score=clamp(.4*excess+.25*attack+.2*stable+.15*std::min(r.independent/4.,1.));covered=end;
 auto reasons=strings({"Montée d’énergie tonale non représentée par une note MIDI de cette hauteur."});reasons.getArray()->add(String(r.independent)+String::fromUTF8(" composantes restent insuffisamment expliquées par les notes MIDI voisines."));reasons.getArray()->add(text("Durée acoustique estimée : peut inclure résonance et pédale ; à ajuster après insertion."));
 out.add(obj({{"id",String(p)+"-"+String(static_cast<juce::int64>(std::round(onset*1000)))},{"pitch",p},{"start",onset},{"end",end},{"score",score},{"confidence",text(score>=.8&&r.independent>=3?"forte":"à vérifier")},{"residual",excess},{"attack",attack},{"transient",f.transient},{"stability",stable},{"reasons",reasons}}));
 }}
 for(int p=std::max(lo,low);p<=std::min(hi,high);++p){const auto& c=counts[p];diagnostics.add(obj({{"pitch",p},{"energy",c[0]},{"tonality",c[1]},{"attack",c[2]},{"residual",c[3]},{"stability",c[4]},{"duration",c[5]},{"accepted",c[6]},{"harmonic_rejected",0},{"refine_covered",0},{"refine_pitch",0},{"refine_duration",0},{"final_accepted",0}}));}
 sort(out);auto before=out;juce::Array<var> pruned;
 for(auto& c:out){check(cancel);bool skip=false;double start=num(c,"start");
 auto first=std::lower_bound(before.begin(),before.end(),start-.08,[](const var& value,double t){return num(value,"start")<t;});
 for(auto it=first;it!=before.end()&&num(*it,"start")<start+.08;++it){const auto& lower=*it;
  if(std::abs(num(lower,"start")-start)<.08&&(int)lower["pitch"]<(int)c["pitch"]&&num(lower,"score")>=num(c,"score"))for(int h=2;h<=partials;++h)
   if(std::abs(std::log(frequency((int)lower["pitch"])*h/frequency((int)c["pitch"])))<.018){skip=true;set(c,"explained_by_pitch",lower["pitch"]);}
 }if(!skip)pruned.add(c);else reject(diagnostics,rejected,c,"harmonic_rejected");
 }return pruned;
}
juce::Array<var> refine(const StereoPcm& pcm,unsigned rate,const VerificationGrid& g,juce::Array<var> input,const std::vector<Note>& notes,juce::Array<var>& diagnostics,juce::Array<var>& rejected,const std::atomic_bool& cancel){juce::Array<var> out;
 for(auto c:input){check(cancel);int p=(int)c["pitch"];double start=num(c,"start"),end=num(c,"end"),best=0,time=start;
 for(int offset=-4;offset<=12;++offset){double t=start+offset*.01;if(t<g.start+.03||t+.05>g.end)continue;double before=band(pcm,rate,t-.03,.03,frequency(p)),after=band(pcm,rate,t,.03,frequency(p)),rise=std::max(after-before,0.);if(rise>best){best=rise;time=t;}}
 start=std::round(std::min(time,end-.04)*1000)/1000;set(c,"start",start);set(c,"id",String(p)+"-"+String(static_cast<juce::int64>(std::round(start*1000))));
 if(std::any_of(notes.begin(),notes.end(),[&](auto& n){return n.pitch==p&&overlapping(n,start,.12);})){reject(diagnostics,rejected,c,"refine_covered");continue;}
 double length=std::clamp(24/frequency(p),.09,3.),center=band(pcm,rate,start+.01,length,frequency(p)),below=band(pcm,rate,start+.01,length,frequency(p)*std::pow(2.,-1./12)),above=band(pcm,rate,start+.01,length,frequency(p)*std::pow(2.,1./12));
 if(center<std::max(below,above)*1.35){reject(diagnostics,rejected,c,"refine_pitch");continue;}
 if(end-start<.06){reject(diagnostics,rejected,c,"refine_duration");continue;}
 diagnostic(diagnostics,p,"final_accepted");
 if(p<48||std::any_of(notes.begin(),notes.end(),[&](auto& n){return overlapping(n,start,.2)&&n.pitch!=p&&shares(p,1,n);})){set(c,"confidence",text("à vérifier"));reason(c,"Présence spectrale distincte d’une note perçue : harmonique partagée ou registre grave, écoute indispensable.");}
 if(std::any_of(notes.begin(),notes.end(),[&](auto& n){return !melodic(n)&&std::abs(n.start-start)<.08;})){set(c,"confidence",text("à vérifier"));reason(c,"Attaque percussive proche : attribution à une note tonale incertaine.");}out.add(c);
 }sort(out);return out;
}
juce::Array<var> holds(const StereoPcm& pcm,unsigned rate,const VerificationGrid& g,const std::vector<Note>& notes,const std::vector<NoteId>& ids,const std::atomic_bool& cancel){
 std::map<std::pair<PartId,int>,std::vector<const Note*>> lanes;for(auto& n:notes)if(melodic(n))lanes[{n.key(),n.pitch}].push_back(&n);juce::Array<var> out;
 for(auto& lane:lanes){int p=lane.first.second;if(p<low||p>high)continue;auto& ns=lane.second;std::stable_sort(ns.begin(),ns.end(),[](auto a,auto b){return a->start==b->start?a->id<b->id:a->start<b->start;});juce::Array<var> run;double start=0,end=0,continuity=1,maxRise=0;
 auto flush=[&]{if(run.size()>=2&&out.size()<128)out.add(obj({{"id","hold-"+String(p)+"-"+run[0].toString()},{"ids",var(run)},{"pitch",p},{"start",start},{"end",end},{"score",clamp((continuity+1-maxRise)/2)},{"continuity",continuity},{"max_reattack",maxRise},{"reasons",strings({"Énergie tonale continue entre des fragments consécutifs de même hauteur et même partie.","Aucune réattaque distincte établie aux frontières testées dans les composantes non partagées.","Une répétition douce, la pédale ou la réverbération peuvent donner le même indice : écouter avant toute fusion."})}}));run.clear();};
 for(std::size_t i=1;i<ns.size();++i){check(cancel);auto& a=*ns[i-1];auto& b=*ns[i];double t=b.start,gap=t-a.end;bool valid=selected(a.id,ids)&&selected(b.id,ids)&&!a.muted&&!b.muted&&gap>=-.04&&gap<=.35&&a.start>=g.start+.04&&b.end<=g.end-.04&&t-a.start>=.10&&b.end-t>=.045;bool evidence=false;double cont=0,rise=0;
 if(valid){std::vector<int> hs;for(int h=1;h<=4;++h)if(!std::any_of(notes.begin(),notes.end(),[&](auto& n){return n.id!=a.id&&n.id!=b.id&&overlapping(n,t,.15)&&shares(p,h,n);}))hs.push_back(h);
 if(std::find(hs.begin(),hs.end(),1)!=hs.end()){double length=std::clamp(3/frequency(p),.03,.065),lo=std::max(std::min(t-.1,double(a.end)-.04),double(a.start)+.04),hi=std::min(t+.1,double(b.end)-.015);std::vector<std::vector<double>> all;
 for(int h:hs){std::vector<double> v;for(int j=0;j<=int(std::max(std::floor((hi-lo)/.02),0.));++j)v.push_back(std::sqrt(band(pcm,rate,lo+j*.02-length/2,length,frequency(p)*h)));all.push_back(std::move(v));}
 double peak=*std::max_element(all[0].begin(),all[0].end()),rms=0;bool tonal=false;for(auto j=g.nearest(lo);j<=g.nearest(hi);++j){auto& f=g.frames[j];rms=std::max(rms,f.rms);tonal|=f.tonal(p)>1.15&&f.flatness<.3;}
 for(auto x:all[0])cont+=x>peak*.18&&x>rms*.015;cont/=std::max<std::size_t>(1,all[0].size());for(auto& v:all){double pk=*std::max_element(v.begin(),v.end());for(std::size_t j=2;j<v.size();++j)rise=std::max(rise,std::max((v[j]-v[j-2])/std::max(pk,1e-12),0.));}evidence=tonal&&peak>rms*.04&&peak>1e-7&&cont>=.85&&rise<.22;}}
 if(evidence){if(run.isEmpty()){run.add(id(a.id));start=a.start;continuity=1;maxRise=0;}run.add(id(b.id));end=b.end;continuity=std::min(continuity,cont);maxRise=std::max(maxRise,rise);}else flush();}flush();
 }sort(out);return out;
}
juce::Array<var> timing(const StereoPcm& pcm,unsigned rate,const VerificationGrid& g,const std::vector<Note>& notes,const std::vector<NoteId>& ids,const std::atomic_bool& cancel){juce::Array<var> out;
 for(auto& n:notes){if(!selected(n.id,ids)||n.muted||!melodic(n))continue;check(cancel);int p=n.pitch;double a=n.start,b=n.end;if(p<low||p>high||a<g.start||b>g.end||b-a<.20)continue;
 auto unshared=[&](double t){return !std::any_of(notes.begin(),notes.end(),[&](auto& m){return m.id!=n.id&&overlapping(m,t,.12)&&shares(p,1,m);});};
 auto support=[&](const VerificationFrame& f){return f.rms>1e-6&&f.flatness<.30&&f.tonal(p)>1.01&&f.amp(p,1)>f.rms*.18;};
 double length=std::clamp(4/frequency(p),.025,.07);auto energy=[&](double t){return std::sqrt(band(pcm,rate,t-length/2,length,frequency(p)));};std::optional<std::pair<double,double>> strongest;
 auto first=g.nearest(a+.16),last=g.nearest(b-.12);for(auto i=first;i<=last;++i){check(cancel);if(i<6||i+4>=g.frames.size())continue;auto& f=g.frames[i];double rise=(f.amp(p,1)-g.frames[i-5].amp(p,1))/std::max(f.amp(p,1),1e-12);if(rise<.48||!support(f)||!unshared(f.time))continue;
 bool pre=false,post=true;for(auto j=i-6;j<i-3;++j)pre|=support(g.frames[j]);for(auto j=i+1;j<=i+3;++j)post&=support(g.frames[j]);if(!pre||!post)continue;
 if(std::any_of(notes.begin(),notes.end(),[&](auto& m){return m.id!=n.id&&std::abs(m.start-f.time)<.12;}))continue;
 double peakRise=0,at=f.time;for(int j=0;j<=12;++j){double t=f.time-.14+j*.01;if(t<a+.10||t>b-.10)continue;double pre=energy(t-.025),post=energy(t+.025),delta=(post-pre)/std::max(post,1e-12);if(delta>peakRise&&post>f.rms*.05){peakRise=delta;at=t;}}
 if(peakRise>.55&&(!strongest||peakRise>strongest->second))strongest={{at,peakRise}};
 }
 if(strongest)out.add(obj({{"id","split-"+String(static_cast<juce::int64>(n.id))},{"kind","split"},{"note_id",id(n.id)},{"pitch",p},{"start",a},{"end",b},{"at",strongest->first},{"proposed_end",b},{"score",strongest->second},{"confidence",text("à vérifier")},{"reasons",strings({"Trace tonale présente avant et après une nouvelle montée d’énergie dans la note MIDI.","Réattaque possible ; modulation, vibrato ou autre source sonore peuvent produire un indice similaire."})}}));
 if(b+.16>=g.end||!unshared(b+.08))continue;double pre=energy(b-.06);if(pre<1e-7||!support(g.frames[g.nearest(b-.06)]))continue;
 double ceiling=std::min(b+2,g.end-.04),next=ceiling;for(auto& m:notes)if(m.id!=n.id&&m.pitch==p&&m.start>b-.10)next=std::min(next,double(m.start));double end=b,previous=pre;int stable=0;
 for(int i=1;i<=100;++i){check(cancel);double t=b+i*hop;if(t>=next-.04||t>=ceiling)break;auto& f=g.frames[g.nearest(t)];double value=energy(t);if(!support(f)||!unshared(t)||value<pre*.50||value>previous*1.65)break;previous=value;end=t;++stable;}
 if(stable>=6&&energy(b+.10)>pre*.65)out.add(obj({{"id","extend-"+String(static_cast<juce::int64>(n.id))},{"kind","extend"},{"note_id",id(n.id)},{"pitch",p},{"start",a},{"end",b},{"at",b},{"proposed_end",end},{"score",std::min(stable/15.,1.)},{"confidence",text("à vérifier")},{"reasons",strings({"Énergie tonale continue après la fin MIDI, sans nouvelle attaque distincte établie.","Prolongation possible ; résonance, pédale et réverbération peuvent expliquer cette continuité."})}}));
 if(out.size()>=128)break;
 }sort(out);return out;
}
}
juce::var verifyMidi(const VerificationGrid& g,const analysis::StereoPcm& pcm,unsigned rate,const std::vector<Note>& notes,const std::vector<NoteId>& ids,std::optional<std::pair<int,int>> range,const std::atomic_bool& cancel,const std::vector<NoteId>* completeIds){
 if(g.frames.empty())throw std::invalid_argument("Grille de vérification vide.");if(range&&(range->first<0||range->first>range->second||range->second>127))throw std::invalid_argument("Registre invalide.");
 juce::Array<var> existing;for(auto& n:notes)if(selected(n.id,ids)&&melodic(n)&&(!range||(n.pitch>=range->first&&n.pitch<=range->second))){check(cancel);existing.add(known(g,n,notes));}
 std::vector<NoteId> complete;for(auto& n:notes)if(selected(n.id,ids)&&(!completeIds||selected(n.id,*completeIds))&&(!range||(n.pitch>=range->first&&n.pitch<=range->second)))complete.push_back(n.id);
 std::size_t onsets=0;juce::Array<var> diagnostics,rejected;
 auto initial=missing(g,notes,range?range->first:low,range?range->second:high,onsets,diagnostics,rejected,cancel);
 auto candidates=refine(pcm,rate,g,initial,notes,diagnostics,rejected,cancel);
 return obj({{"audio_id",id(0)},{"analysis_id",id(0)},{"revision",id(0)},{"threshold",0.},{"start",g.start},{"end",g.end},{"cached",false},{"pitch_low",range?var(range->first):var()},{"pitch_high",range?var(range->second):var()},
 {"timing",var(timing(pcm,rate,g,notes,complete,cancel))},{"existing",var(existing)},{"missing",var(candidates)},{"fragmented",var(holds(pcm,rate,g,notes,complete,cancel))},{"onsets",id(onsets)},{"omitted",id(0)},
 {"search_diagnostics",var(diagnostics)},{"rejected_candidates",var(rejected)},{"sample_rate",int(g.sampleRate)},{"searched_low",range?range->first:low},{"searched_high",range?range->second:high},
 {"warnings",strings({"Comparaison spectrale expérimentale sur la plage MIDI 0–127, limitée par le registre choisi : scores heuristiques, pas des probabilités. Les extrêmes, résonances, unissons et harmoniques partagées peuvent rester indéterminés.","Audio et MIDI doivent partager la même origine temporelle. Aucun réalignement ni changement du MIDI n’est effectué."})}});
}
}
