#include "OxfordConverter.h"
#include <cstdio>
#include <cmath>
#include <vector>
static const double SR=48000; static constexpr double PI=3.14159265358979323846;
// sinus à PÉRIODE ENTIÈRE (cycles entiers sur N) -> aucune fuite : harmonique k au bin k*C
void meas(double lvDb){
  const int N=1<<17; const int C=2731;        // ~1000.24 Hz, entier de cycles
  const double f=(double)C*SR/N;
  OxfordConverter c; c.prepare(SR);
  std::vector<double> buf(N);
  double w=2*PI*f/SR, ph=0; double amp=std::pow(10.0,lvDb/20.0);
  for(int i=0;i<8192;i++){ c.processSample(amp*std::sin(ph)); ph+=w; }
  ph=0; for(int i=0;i<N;i++){ buf[i]=c.processSample(amp*std::sin(ph)); ph+=w; }
  auto bin=[&](int k){ double re=0,im=0,wk=2*PI*k/N;
    for(int i=0;i<N;i++){re+=buf[i]*std::cos(wk*i);im+=buf[i]*std::sin(wk*i);}
    return 2.0*std::sqrt(re*re+im*im)/N; };
  double fund=bin(C), harm=0;
  for(int k=2;k<=10;k++){ double m=bin(k*C); harm+=m*m; }
  harm=std::sqrt(harm);
  printf("signal %4.0f dBFS : harm abs %7.1f dBFS | THD rel %6.1f dB (%.6f %%) | h2 %6.1f h3 %6.1f dBFS\n",
    lvDb, 20*std::log10(harm+1e-30), 20*std::log10(harm/(fund+1e-30)), 100*harm/(fund+1e-30),
    20*std::log10(bin(2*C)+1e-30), 20*std::log10(bin(3*C)+1e-30));
}
int main(){ for(double l:{0.0,-20.0,-50.0}) meas(l); return 0; }
