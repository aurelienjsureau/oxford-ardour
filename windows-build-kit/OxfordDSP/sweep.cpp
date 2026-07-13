#include "OxfordEQ.h"
#include <cstdio>
#include <cmath>
static const double SR=48000; static constexpr double PI=3.14159265358979323846;
template<class F> double rmsAt(double f,F&&p,int cyc=120,int warm=8192){
  double w=2*PI*f/SR,ph=0,acc=0;long n=0,tot=warm+(long)(cyc*SR/f);
  for(long i=0;i<tot;i++){double y=p(std::sin(ph));ph+=w;if(ph>2*PI)ph-=2*PI;if(i>=warm){acc+=y*y;n++;}}
  return std::sqrt(acc/n);}
static double db(double l){return 20*std::log10(l+1e-12);}
int main(){
  double base=0.70710678;
  const double fs[]={31,63,125,250,500,1000,2000,4000,8000,12000,16000};
  // TEST 1 : cloche +6 @1k Q=2, en SSL-E (constant Q) vs Neve-G (proportional)
  for(int ct=0;ct<4;ct++){
    OxfordEQ eq;eq.prepare(SR);eq.setCurveType((OxfordEQ::CurveType)ct);
    eq.setBand(OxfordEQ::MF,1000,6,2.0);
    printf("type %d (MF +6 @1k Q=2): ",ct);
    for(double f:fs){double g=db(rmsAt(f,[&](double x){return eq.processSample(x);})/base);printf("%5.0f:%+5.1f ",f,g);}
    printf("\n");
  }
  // TEST 2 : HF shelf +6 @10k (bell off), regarde le gain a 10k et au plateau (16k)
  {OxfordEQ eq;eq.prepare(SR);eq.setHFShelf(true);eq.setBand(OxfordEQ::HF,10000,6,0.7);
   printf("HF shelf +6 @10k : ");for(double f:fs){double g=db(rmsAt(f,[&](double x){return eq.processSample(x);})/base);printf("%5.0f:%+5.1f ",f,g);}printf("\n");}
  // TEST 3 : LF shelf +6 @80
  {OxfordEQ eq;eq.prepare(SR);eq.setLFShelf(true);eq.setBand(OxfordEQ::LF,80,6,0.7);
   printf("LF shelf +6 @80  : ");for(double f:fs){double g=db(rmsAt(f,[&](double x){return eq.processSample(x);})/base);printf("%5.0f:%+5.1f ",f,g);}printf("\n");}
  return 0;
}
