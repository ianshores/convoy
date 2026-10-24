// SPDX-License-Identifier: MIT
#include "convoy.hpp"
#include <chrono>
#include <cstdio>
#include <random>
int main(){
 std::puts("mode,callback,channels,sample_rate,ir_seconds,cpu_percent,p99_us,max_us,deadline_us,misses");
 for(int callback:{64,256})for(int changing:{0,1}){
  std::mt19937 rng(91);std::uniform_real_distribution<float>noise(-.1,.1);
  std::vector<float>ir(192000),input(callback),output(callback);
  for(std::size_t i=0;i<ir.size();++i)ir[i]=noise(rng)*std::pow(10.,-6.*i/ir.size());
  for(auto&v:input)v=noise(rng);
  convoy::Convolver left(ir.data(),ir.size()),right(ir.data(),ir.size());
  for(int i=0;i<192000;i+=callback){left.process(input.data(),output.data(),callback);right.process(input.data(),output.data(),callback);}
  std::vector<double>times;const int count=30*48000/callback;times.reserve(count);double total=0;int misses=0;
  for(int i=0;i<count;++i){const auto start=std::chrono::steady_clock::now();
   if(changing){float cutoff=((i*callback)/48000)%2?-96.f:0.f;left.set_tail_cutoff_db(cutoff);right.set_tail_cutoff_db(cutoff);}
   left.process(input.data(),output.data(),callback);right.process(input.data(),output.data(),callback);
   double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();times.push_back(us);total+=us;misses+=us>callback/48000.*1e6;
  }
  std::sort(times.begin(),times.end());std::printf("%s,%d,2,48000,4,%.9g,%.9g,%.9g,%.9g,%d\n",changing?"automate_every_second":"off",callback,100*total/30e6,times[times.size()*99/100],times.back(),callback/48000.*1e6,misses);
 }
}
