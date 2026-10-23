// SPDX-License-Identifier: MIT
// Offline coefficient-floor and tail-trimming experiment; no production policy.
#include "heavyverb.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
int main(int argc, char **argv) {
    const int trial = argc > 1 ? std::atoi(argv[1]) : 0;
    constexpr int rate=48000, length=4*rate, block=256;
    const double threshold=std::pow(10., -96./20.);
    std::puts("decay_db,policy,retained_samples,zeroed_samples,cpu_percent,residual_peak_dbfs,residual_rms_dbfs,tail_l1_bound");
    for (double decay : {52.1153378,120.,160.}) {
        std::mt19937 rng(99); std::uniform_real_distribution<float> noise(-1,1);
        std::vector<float> full(length);
        for(int i=0;i<length;++i) full[i]=float(.1*noise(rng)*std::pow(10.,-decay*i/length/20.));
        std::vector<float> signal(length+8192+256,0), reference(signal.size()), rendered(signal.size());
        for(int i=0;i<8192;++i) signal[i]=.25f*noise(rng);
        heavyverb::Convolver original(full.data(),full.size()); original.process(signal.data(),reference.data(),signal.size());
        for(int step=0;step<4;++step) {
            const int policy=(step+trial)%4; auto ir=full; int zeroed=0;
            const char* names[]={"full","floor_coefficients","trim_peak_tail","trim_l1_tail"};
            if(policy==1) for(auto &v:ir) if(std::abs(v)<threshold) {v=0; ++zeroed;}
            if(policy==2) while(ir.size()>1 && std::abs(ir.back())<threshold) ir.pop_back();
            if(policy==3) { double sum=0; while(ir.size()>1 && sum+std::abs(ir.back())<=threshold) {sum+=std::abs(ir.back());ir.pop_back();} }
            double bound=0; for(std::size_t i=0;i<full.size();++i) bound+=std::abs(double(full[i])-(i<ir.size()?ir[i]:0));
            heavyverb::Convolver engine(ir.data(),ir.size()); engine.process(signal.data(),rendered.data(),signal.size());
            double peak=0,squared=0; for(std::size_t i=0;i<signal.size();++i) {double e=double(rendered[i])-reference[i];peak=std::max(peak,std::abs(e));squared+=e*e;}
            std::vector<float> in(block),out(block); for(auto&v:in)v=.1f*noise(rng);
            for(int i=0;i<length;i+=block)engine.process(in.data(),out.data(),block);
            const auto start=std::chrono::steady_clock::now();
            for(int i=0;i<30*rate;i+=block)engine.process(in.data(),out.data(),block);
            const double cpu=100*std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()/30.;
            std::printf("%.8g,%s,%zu,%d,%.9g,%.9g,%.9g,%.9g\n",decay,names[policy],ir.size(),zeroed,cpu,20*std::log10(peak),10*std::log10(squared/signal.size()),bound);
        }
    }
}
