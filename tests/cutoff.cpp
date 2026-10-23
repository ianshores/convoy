// SPDX-License-Identifier: MIT
#include "heavyverb.hpp"
#include <cstdio>
#include <random>
#include <limits>
#include <stdexcept>
static void require(bool ok, const char *message) { if(!ok) throw std::runtime_error(message); }
int main() {
 try {
    std::mt19937 rng(51); std::uniform_real_distribution<float> noise(-.1f,.1f);
    double worst=0;
    for (std::size_t b : {16,64,256}) for(std::size_t length : {127,1597,192000}) {
        std::vector<float> ir(length);
        for(std::size_t i=0;i<length;++i) ir[i]=noise(rng)*std::pow(10.,-8.*i/length);
        heavyverb::Convolver engine(ir.data(),length,b);
        for(float cutoff : {0.f,-144.f,-120.f,-96.f,-72.f,-36.f,0.f,std::numeric_limits<float>::quiet_NaN(),1.f}) {
            engine.set_tail_cutoff_db(cutoff); engine.reset();
            const auto retained=std::min(length,engine.retained_samples());
            std::vector<float> input(length+128+b),actual(input.size());
            std::vector<double> expected(input.size());
            for(std::size_t i=0;i<128;++i) {
                input[i]=noise(rng);
                for(std::size_t j=0;j<retained;++j) expected[i+j+b]+=double(input[i])*ir[j];
            }
            for(std::size_t i=0;i<input.size();i+=127) engine.process(input.data()+i,actual.data()+i,std::min<std::size_t>(127,input.size()-i));
            for(std::size_t i=0;i<actual.size();++i) worst=std::max(worst,std::abs(actual[i]-expected[i]));
            require(worst<1e-5,"settled cutoff mismatch");
            if(!(cutoff<0)) require(retained==length,"off must restore full length");
        }
    }
    // Independent direct overlap-add model of every live gain transition.
    constexpr std::size_t b=16, length=127, parts=8, blocks=100;
    std::vector<float> ir(length),in(blocks*b),out(in.size()),gain(parts,1);
    std::vector<double> expected(in.size()+2*b);
    for(std::size_t i=0;i<length;++i) ir[i]=noise(rng)*std::pow(10.,-7.*i/length);
    for(auto &v:in)v=noise(rng);
    heavyverb::Convolver engine(ir.data(),length,b);
    for(std::size_t step=0;step<blocks;++step) {
        float db=step<16?0.f:step<48?-96.f:step<64?-48.f:step<80?0.f:(step%3==0?-72.f:0.f);
        engine.set_tail_cutoff_db(db);
        // Derive trailing whole-partition cutoff independently from raw taps.
        std::size_t target=parts;
        if(db<0) {std::size_t last=0; for(std::size_t i=0;i<length;++i)if(std::abs(ir[i])>=std::pow(10.,db/20.))last=i; target=last/b+1;}
        for(std::size_t p=0;p<parts;++p) {
            const float desired=p<target?1.f:0.f;
            gain[p]=gain[p]<desired?std::min(desired,gain[p]+.125f):std::max(desired,gain[p]-.125f);
            if(p>step)continue;
            for(std::size_t i=0;i<b;++i)for(std::size_t j=0;j<b && p*b+j<length;++j)
                expected[(step+1)*b+i+j]+=double(in[(step-p)*b+i])*ir[p*b+j]*gain[p];
        }
        engine.process(in.data()+step*b,out.data()+step*b,b);
    }
    for(std::size_t i=0;i<out.size();++i)worst=std::max(worst,std::abs(out[i]-expected[i]));
    require(worst<1e-5,"live transition mismatch");
    std::printf("PASS: 81 settled cutoff/full-restore cases and direct live transition model; worst error %.9g\n",worst);
 } catch(const std::exception&e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
