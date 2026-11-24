#pragma once
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <cstdint>

namespace wand400 {
constexpr int CHANNELS = 6;
constexpr int RESAMPLED = 256;
constexpr int FEATURES = 60;
constexpr int RAW_LIMIT = 800;
constexpr double MW_PI = 3.14159265358979323846;

// Replicates numpy.interp(linspace(t[0], t[last], 256), t, x).
// Source timestamps must be strictly increasing. Output arrays use [time][channel].
inline bool resample(const uint32_t* t, const float raw[][CHANNELS],
                     int count, float out[][CHANNELS]) {
  if (count < 2 || count > RAW_LIMIT) return false;
  for (int i = 1; i < count; ++i) if (t[i] <= t[i-1]) return false;
  const double t0 = static_cast<double>(t[0]);
  const double span = static_cast<double>(t[count-1]) - t0;
  int left = 0;
  for (int j = 0; j < RESAMPLED; ++j) {
    const double target = t0 + span * static_cast<double>(j) / (RESAMPLED-1);
    while (left + 1 < count-1 && static_cast<double>(t[left+1]) < target) ++left;
    const double a = (target - t[left]) /
                     (static_cast<double>(t[left+1]) - t[left]);
    for (int c = 0; c < CHANNELS; ++c) {
      out[j][c] = static_cast<float>(
        static_cast<double>(raw[left][c]) * (1.0-a) +
        static_cast<double>(raw[left+1][c]) * a);
    }
  }
  return true;
}

inline void features(const float samples[][CHANNELS], float out[FEATURES]) {
  for (int c = 0; c < CHANNELS; ++c) {
    double x[RESAMPLED];
    double sum=0.0, sq=0.0, mn=1e300, mx=-1e300, diff=0.0;
    for (int i=0; i<RESAMPLED; ++i) {
      const double v=samples[i][c]; x[i]=v;
      sum+=v; sq+=v*v; mn=std::min(mn,v); mx=std::max(mx,v);
      if (i>0) diff+=std::abs(v-x[i-1]);
    }
    const double mean=sum/RESAMPLED;
    const double var=std::max(0.0, sq/RESAMPLED-mean*mean);
    double powersTotal=0.0, powersLow=0.0, peakPower=-1.0;
    int peak=1;
    // np.fft.rfft(col-col.mean()): bins 0..128, using unnormalized DFT.
    for (int k=0; k<=RESAMPLED/2; ++k) {
      const double angle=-2.0*MW_PI*k/RESAMPLED;
      const double cs=std::cos(angle), sn=std::sin(angle);
      double ur=1.0, ui=0.0, real=0.0, imag=0.0;
      for (int i=0; i<RESAMPLED; ++i) {
        const double v=x[i]-mean;
        real+=v*ur; imag+=v*ui;
        const double nr=ur*cs-ui*sn;
        ui=ur*sn+ui*cs; ur=nr;
      }
      const double power=real*real+imag*imag;
      powersTotal+=power;
      if (k>=1 && k<20) powersLow+=power;
      if (k>=1 && power>peakPower) {peakPower=power; peak=k;}
    }
    std::sort(x, x+RESAMPLED);
    // np.percentile(col, 25/75) uses linear interpolation by default.
    const double q25=x[63]*0.25+x[64]*0.75;   // index 63.75
    const double q75=x[191]*0.75+x[192]*0.25; // index 191.25
    const int b=c*10;
    out[b+0]=static_cast<float>(mean);
    out[b+1]=static_cast<float>(std::sqrt(var));
    out[b+2]=static_cast<float>(mn);
    out[b+3]=static_cast<float>(mx);
    out[b+4]=static_cast<float>(std::sqrt(sq/RESAMPLED));
    out[b+5]=static_cast<float>(diff/(RESAMPLED-1));
    out[b+6]=static_cast<float>(peak);
    out[b+7]=static_cast<float>(powersLow/(powersTotal+1e-10));
    out[b+8]=static_cast<float>(q25);
    out[b+9]=static_cast<float>(q75);
  }
}
} // namespace wand400
