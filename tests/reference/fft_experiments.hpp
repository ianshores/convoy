// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ian Shores
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>
#include <type_traits>
#ifndef HEAVYVERB_FFT_STEP
#define HEAVYVERB_FFT_STEP 7
#endif
#if HEAVYVERB_FFT_STEP >= 4 && !defined(HEAVYVERB_FFT_SCALAR) && (defined(__SSE2__) || defined(_M_X64))
#include <xmmintrin.h>
#ifdef HEAVYVERB_FFT_FMA
#ifndef __FMA__
#error "FMA experiment requires FMA compiler support and a compatible target CPU"
#endif
#include <immintrin.h>
#endif
#define HEAVYVERB_FFT_SSE 1
#endif
#ifndef HEAVYVERB_RECON_MIN_SIZE
#define HEAVYVERB_RECON_MIN_SIZE 0
#endif
namespace heavyverb { namespace detail {
// Real transform: pack even/odd samples into a half-size complex FFT.
// Spectrum contract: separate real/imaginary arrays, N/2+1 slots; DC and
// Nyquist are real scalars at slots 0 and N/2. The inverse normalizes by N.
// Plans and scratch are per engine, created off the audio thread.
class RealFFT {
    friend struct FFTProbe;
    using Complex = std::complex<float>;
    std::size_t n_, m_;
    std::vector<std::size_t> reverse_;
    std::vector<Complex> roots_, real_roots_, work_;
    std::vector<float> r_, i_, twr_, twi_, stage_r_, stage_i_;
    Complex get(std::size_t k) const noexcept {
#if HEAVYVERB_FFT_STEP >= 2
        return Complex(r_[k],i_[k]);
#else
        return work_[k];
#endif
    }
    void set(std::size_t k,Complex x) noexcept {
#if HEAVYVERB_FFT_STEP >= 2
        r_[k]=x.real();i_[k]=x.imag();
#else
        work_[k]=x;
#endif
    }
    struct Stage {std::size_t len, offset;};
    std::vector<Stage> stages_;
    std::vector<std::size_t> order_;
    std::vector<float> radix_r_,radix_i_;
    std::vector<std::size_t> mirror_;
    std::vector<float> unpack_r_,unpack_i_;
#ifdef HEAVYVERB_FFT_SSE
    struct Four {
        __m128 v;
        Four(__m128 x):v(x){}
        Four(float x):v(_mm_set1_ps(x)){}
        friend Four operator+(Four a,Four b){return _mm_add_ps(a.v,b.v);}
        friend Four operator-(Four a,Four b){return _mm_sub_ps(a.v,b.v);}
        friend Four operator*(Four a,Four b){return _mm_mul_ps(a.v,b.v);}
    };
#endif
    template<class T> static T load(const float* p) noexcept {
#ifdef HEAVYVERB_FFT_SSE
        if constexpr(!std::is_same<T,float>::value) return _mm_loadu_ps(p);
        else
#endif
        return *p;
    }
    template<class T> static void store(float* p,T x) noexcept {
#ifdef HEAVYVERB_FFT_SSE
        if constexpr(!std::is_same<T,float>::value) _mm_storeu_ps(p,x.v);
        else
#endif
        *p=x;
    }
    template<bool inverse,class T> void butterfly4(std::size_t a,std::size_t q,std::size_t tw) noexcept {
        T ar=load<T>(&r_[a]),ai=load<T>(&i_[a]);
        T br=load<T>(&r_[a+q]),bi=load<T>(&i_[a+q]);
        T cr=load<T>(&r_[a+2*q]),ci=load<T>(&i_[a+2*q]);
        T dr=load<T>(&r_[a+3*q]),di=load<T>(&i_[a+3*q]);
        auto multiply=[&](T& x,T& y,std::size_t w) {
            const T wr=load<T>(&radix_r_[w]),wi=load<T>(&radix_i_[w]) * T(inverse?-1.f:1.f);
#ifdef HEAVYVERB_FFT_FMA
            if constexpr(!std::is_same<T,float>::value) {
                const T re=_mm_fmsub_ps(x.v,wr.v,_mm_mul_ps(y.v,wi.v));
                y=_mm_fmadd_ps(x.v,wi.v,_mm_mul_ps(y.v,wr.v)); x=re;
            } else {
                const T re=std::fma(x,wr,-y*wi); y=std::fma(x,wi,y*wr); x=re;
            }
#else
            const T re=x*wr-y*wi; y=x*wi+y*wr; x=re;
#endif
        };
        if constexpr(inverse) {multiply(br,bi,tw);multiply(cr,ci,tw+q);multiply(dr,di,tw+2*q);}
        const T ur=ar+cr,ui=ai+ci,vr=ar-cr,vi=ai-ci;
        const T sr=br+dr,si=bi+di,tr=br-dr,ti=bi-di;
        ar=ur+sr;ai=ui+si;cr=ur-sr;ci=ui-si;
        if constexpr(inverse) {br=vr-ti;bi=vi+tr;dr=vr+ti;di=vi-tr;}
        else {br=vr+ti;bi=vi-tr;dr=vr-ti;di=vi+tr;}
        if constexpr(!inverse) {multiply(br,bi,tw);multiply(cr,ci,tw+q);multiply(dr,di,tw+2*q);}
        store(&r_[a],ar);store(&i_[a],ai);store(&r_[a+q],br);store(&i_[a+q],bi);
        store(&r_[a+2*q],cr);store(&i_[a+2*q],ci);store(&r_[a+3*q],dr);store(&i_[a+3*q],di);
    }
    template<bool inverse> void radix_stage(const Stage& stage) noexcept {
        const auto q=stage.len/4;
        for(std::size_t base=0;base<m_;base+=stage.len) {
            std::size_t j=0;
#ifdef HEAVYVERB_FFT_SSE
            for(;j+4<=q;j+=4) butterfly4<inverse,Four>(base+j,q,stage.offset+j);
#endif
            for(;j<q;++j) butterfly4<inverse,float>(base+j,q,stage.offset+j);
        }
    }
    void radix2_last() noexcept {
        for(std::size_t k=0;k<m_;k+=2) {
            const float ar=r_[k],ai=i_[k],br=r_[k+1],bi=i_[k+1];
            r_[k]=ar+br;i_[k]=ai+bi;r_[k+1]=ar-br;i_[k+1]=ai-bi;
        }
    }
    void reorder(bool inverse) noexcept {
        if(inverse) for(std::size_t k=0;k<m_;++k) work_[order_[k]]=get(k);
        else for(std::size_t k=0;k<m_;++k) work_[k]=get(order_[k]);
        for(std::size_t k=0;k<m_;++k) set(k,work_[k]);
    }
#ifdef HEAVYVERB_FFT_SSE
    __m128 mirror4(const float* x,std::size_t q) const noexcept {
        return _mm_set_ps(x[mirror_[q+3]],x[mirror_[q+2]],x[mirror_[q+1]],x[mirror_[q]]);
    }
#endif
    void radix_fft(bool inverse,bool normalize) noexcept {
        const bool odd=((m_>>(2*stages_.size()))==2);
        if(inverse) {
#if HEAVYVERB_FFT_STEP == 5
            reorder(true);
#endif
            if(odd)radix2_last();
            for(auto s=stages_.rbegin();s!=stages_.rend();++s)radix_stage<true>(*s);
            if(normalize)for(std::size_t k=0;k<m_;++k){r_[k]/=float(m_);i_[k]/=float(m_);}
        } else {
            for(const auto& s:stages_)radix_stage<false>(s);
            if(odd)radix2_last();
#if HEAVYVERB_FFT_STEP == 5
            reorder(false);
#endif
        }
    }
    void complex_fft(bool inverse,bool normalize=true) noexcept {
#if HEAVYVERB_FFT_STEP >= 5
        radix_fft(inverse,normalize);
#elif HEAVYVERB_FFT_STEP >= 2
        for(std::size_t k=0;k<m_;++k) if(k<reverse_[k]) {std::swap(r_[k],r_[reverse_[k]]);std::swap(i_[k],i_[reverse_[k]]);}
        for(std::size_t len=2;len<=m_;len*=2) {
            const auto half=len/2,step=m_/len;
            for(std::size_t base=0;base<m_;base+=len) {
                std::size_t j=0;
#ifdef HEAVYVERB_FFT_SSE
                const auto sign=_mm_set1_ps(inverse?-0.f:0.f);
                for(;j+4<=half;j+=4) {
                    const auto a=base+j,b=a+half;
                    const auto wr=_mm_loadu_ps(stage_r_.data()+half-1+j);
                    const auto wi=_mm_xor_ps(_mm_loadu_ps(stage_i_.data()+half-1+j),sign);
                    const auto br=_mm_loadu_ps(r_.data()+b),bi=_mm_loadu_ps(i_.data()+b);
                    const auto ur=_mm_loadu_ps(r_.data()+a),ui=_mm_loadu_ps(i_.data()+a);
                    const auto vr=_mm_sub_ps(_mm_mul_ps(br,wr),_mm_mul_ps(bi,wi));
                    const auto vi=_mm_add_ps(_mm_mul_ps(br,wi),_mm_mul_ps(bi,wr));
                    _mm_storeu_ps(r_.data()+a,_mm_add_ps(ur,vr));_mm_storeu_ps(i_.data()+a,_mm_add_ps(ui,vi));
                    _mm_storeu_ps(r_.data()+b,_mm_sub_ps(ur,vr));_mm_storeu_ps(i_.data()+b,_mm_sub_ps(ui,vi));
                }
#endif
                for(;j<half;++j) {
                    const auto a=base+j,b=a+half;
#if HEAVYVERB_FFT_STEP >= 3
                    (void)step;
                    const float wr=stage_r_[half-1+j],wi=inverse?-stage_i_[half-1+j]:stage_i_[half-1+j];
#else
                    const float wr=twr_[j*step],wi=inverse?-twi_[j*step]:twi_[j*step];
#endif
                    const float vr=r_[b]*wr-i_[b]*wi,vi=r_[b]*wi+i_[b]*wr,ur=r_[a],ui=i_[a];
                    r_[a]=ur+vr;i_[a]=ui+vi;r_[b]=ur-vr;i_[b]=ui-vi;
                }
            }
        }
        if(inverse && normalize)for(std::size_t k=0;k<m_;++k){r_[k]/=float(m_);i_[k]/=float(m_);}
#else
        for (std::size_t i=0;i<m_;++i)
            if (i<reverse_[i]) std::swap(work_[i],work_[reverse_[i]]);
        for (std::size_t len=2;len<=m_;len*=2) {
            const auto half=len/2, step=m_/len;
            for(std::size_t base=0;base<m_;base+=len)
                for(std::size_t j=0;j<half;++j) {
                    const auto w=inverse?std::conj(roots_[j*step]):roots_[j*step];
                    const auto u=work_[base+j], z=work_[base+j+half];
                    const Complex v(z.real()*w.real()-z.imag()*w.imag(),z.real()*w.imag()+z.imag()*w.real());
                    work_[base+j]=u+v; work_[base+j+half]=u-v;
                }
        }
        if(inverse && normalize) for(auto& v:work_) v/=float(m_);
#endif
    }
public:
    explicit RealFFT(std::size_t n):n_(n),m_(n/2),real_roots_(m_) {
#ifdef HEAVYVERB_COMPACT_PLAN
#if HEAVYVERB_FFT_STEP < 5
        reverse_.resize(m_);
#endif
#if HEAVYVERB_FFT_STEP < 2 || HEAVYVERB_FFT_STEP == 5
        work_.resize(m_);
#endif
#if HEAVYVERB_FFT_STEP < 2
        roots_.resize(m_/2);
#endif
#else
        reverse_.resize(m_);roots_.resize(m_/2);work_.resize(m_);
#endif
        for(std::size_t i=0;i<m_;++i) {
            if(!reverse_.empty()) {
                auto k=i; std::size_t r=0;
                for(auto bit=m_;bit>1;bit>>=1) {r=(r<<1)|(k&1);k>>=1;}
                reverse_[i]=r;
            }
            const double angle=-6.2831853071795864769*double(i)/n_;
            real_roots_[i]=Complex(float(std::cos(angle)),float(std::sin(angle)));
        }
        for(std::size_t i=0;i<roots_.size();++i) roots_[i]=real_roots_[2*i];
#if HEAVYVERB_FFT_STEP >= 2
        r_.resize(m_);i_.resize(m_);
#if HEAVYVERB_FFT_STEP < 5 || !defined(HEAVYVERB_COMPACT_PLAN)
        twr_.resize(m_/2);twi_.resize(m_/2);
        for(std::size_t k=0;k<m_/2;++k){twr_[k]=real_roots_[2*k].real();twi_[k]=real_roots_[2*k].imag();}
#if HEAVYVERB_FFT_STEP >= 3
        stage_r_.resize(m_-1);stage_i_.resize(m_-1);
        for(std::size_t half=1;half<m_;half*=2)
            for(std::size_t j=0;j<half;++j) {stage_r_[half-1+j]=twr_[j*m_/(2*half)];stage_i_[half-1+j]=twi_[j*m_/(2*half)];}
#endif
#endif
#endif
#if HEAVYVERB_FFT_STEP >= 5
        order_.resize(m_);
        for(std::size_t k=0;k<m_;++k) {
            std::size_t v=k,len=m_,index=0;
            while(len>=4){len/=4;index+=(v%4)*len;v/=4;}
            if(len==2)index+=v%2;
            order_[k]=index;
        }
#ifdef HEAVYVERB_ORDERED_RECONSTRUCTION
        if(n_>=HEAVYVERB_RECON_MIN_SIZE) {
            mirror_.resize(m_);unpack_r_.resize(m_);unpack_i_.resize(m_);
            for(std::size_t k=0;k<m_;++k) {
                const auto q=order_[k];mirror_[q]=order_[(m_-k)%m_];
                unpack_r_[q]=real_roots_[k].real();unpack_i_[q]=real_roots_[k].imag();
            }
        }
#endif
        for(std::size_t len=m_;len>=4;len/=4) {
            const auto q=len/4,offset=radix_r_.size();stages_.push_back({len,offset});
            for(std::size_t power=1;power<=3;++power)for(std::size_t j=0;j<q;++j) {
                const double angle=-6.2831853071795864769*double(j*power)/len;
                radix_r_.push_back(float(std::cos(angle)));radix_i_.push_back(float(std::sin(angle)));
            }
        }
#endif
    }
    std::size_t slot(std::size_t k) const noexcept {
#if HEAVYVERB_FFT_STEP >= 6
        return k==m_?m_:order_[k];
#else
        return k;
#endif
    }
    std::size_t memory_bytes() const noexcept {
        return sizeof(*this)+reverse_.capacity()*sizeof(std::size_t)+(roots_.capacity()+real_roots_.capacity()+work_.capacity())*sizeof(Complex)+(r_.capacity()+i_.capacity()+twr_.capacity()+twi_.capacity()+stage_r_.capacity()+stage_i_.capacity()+radix_r_.capacity()+radix_i_.capacity())*sizeof(float)+order_.capacity()*sizeof(std::size_t)+stages_.capacity()*sizeof(Stage)+mirror_.capacity()*sizeof(std::size_t)+(unpack_r_.capacity()+unpack_i_.capacity())*sizeof(float);
    }
    void forward(const float* time,float* real,float* imag) noexcept {
        for(std::size_t j=0;j<m_;++j) set(j,Complex(time[2*j],time[2*j+1]));
        complex_fft(false);
        real[0]=get(0).real()+get(0).imag();imag[0]=0;
        real[m_]=get(0).real()-get(0).imag();imag[m_]=0;
#if defined(HEAVYVERB_ORDERED_RECONSTRUCTION) && HEAVYVERB_FFT_STEP >= 6
        if(n_>=HEAVYVERB_RECON_MIN_SIZE) {
        std::size_t q=1;
#if defined(HEAVYVERB_SSE_RECONSTRUCTION) && defined(HEAVYVERB_FFT_SSE)
        const auto half=_mm_set1_ps(.5f),sign=_mm_set1_ps(-0.f);
        for(;q+4<=m_;q+=4) {
            const auto ar=_mm_loadu_ps(r_.data()+q),ai=_mm_loadu_ps(i_.data()+q);
            const auto br=mirror4(r_.data(),q),bi=_mm_xor_ps(mirror4(i_.data(),q),sign);
            const auto wr=_mm_loadu_ps(unpack_r_.data()+q),wi=_mm_loadu_ps(unpack_i_.data()+q);
            const auto dr=_mm_sub_ps(ar,br),di=_mm_sub_ps(ai,bi);
            const auto re=_mm_add_ps(_mm_add_ps(ar,br),_mm_add_ps(_mm_mul_ps(wr,di),_mm_mul_ps(wi,dr)));
            const auto im=_mm_sub_ps(_mm_add_ps(_mm_add_ps(ai,bi),_mm_mul_ps(wi,di)),_mm_mul_ps(wr,dr));
            _mm_storeu_ps(real+q,_mm_mul_ps(half,re));_mm_storeu_ps(imag+q,_mm_mul_ps(half,im));
        }
#endif
        for(;q<m_;++q) {
            const auto p=mirror_[q];
            const float ar=r_[q],ai=i_[q],br=r_[p],bi=-i_[p],wr=unpack_r_[q],wi=unpack_i_[q];
            const float dr=ar-br,di=ai-bi;
            real[q]=.5f*(ar+br+wr*di+wi*dr);
            imag[q]=.5f*(ai+bi+wi*di-wr*dr);
        }
        } else
#endif
        {
        for(std::size_t k=1;k<m_;++k) {
            const auto a=get(slot(k)), b=std::conj(get(slot(m_-k))),w=real_roots_[k];
            const auto d=a-b;
            real[slot(k)]=.5f*(a.real()+b.real()+w.real()*d.imag()+w.imag()*d.real());
            imag[slot(k)]=.5f*(a.imag()+b.imag()+w.imag()*d.imag()-w.real()*d.real());
        }
        }
    }
    void inverse(const float* real,const float* imag,float* time,bool normalize=true) noexcept {
        set(0,Complex(.5f*(real[0]+real[m_]),.5f*(real[0]-real[m_])));
#if defined(HEAVYVERB_ORDERED_RECONSTRUCTION) && HEAVYVERB_FFT_STEP >= 6
        if(n_>=HEAVYVERB_RECON_MIN_SIZE) {
        std::size_t q=1;
#if defined(HEAVYVERB_SSE_RECONSTRUCTION) && defined(HEAVYVERB_FFT_SSE)
        const auto half=_mm_set1_ps(.5f),sign=_mm_set1_ps(-0.f);
        for(;q+4<=m_;q+=4) {
            const auto xr=_mm_loadu_ps(real+q),xi=_mm_loadu_ps(imag+q);
            const auto yr=mirror4(real,q),yi=_mm_xor_ps(mirror4(imag,q),sign);
            const auto wr=_mm_loadu_ps(unpack_r_.data()+q),wi=_mm_loadu_ps(unpack_i_.data()+q);
            const auto dr=_mm_sub_ps(xr,yr),di=_mm_sub_ps(xi,yi);
            const auto re=_mm_add_ps(_mm_sub_ps(_mm_add_ps(xr,yr),_mm_mul_ps(wr,di)),_mm_mul_ps(wi,dr));
            const auto im=_mm_add_ps(_mm_add_ps(xi,yi),_mm_add_ps(_mm_mul_ps(wr,dr),_mm_mul_ps(wi,di)));
            _mm_storeu_ps(r_.data()+q,_mm_mul_ps(half,re));_mm_storeu_ps(i_.data()+q,_mm_mul_ps(half,im));
        }
#endif
        for(;q<m_;++q) {
            const auto p=mirror_[q];
            const float xr=real[q],xi=imag[q],yr=real[p],yi=-imag[p],wr=unpack_r_[q],wi=unpack_i_[q];
            const float dr=xr-yr,di=xi-yi;
            r_[q]=.5f*(xr+yr-wr*di+wi*dr);i_[q]=.5f*(xi+yi+wr*dr+wi*di);
        }
        } else
#endif
        {
        for(std::size_t k=1;k<m_;++k) {
            const float xr=real[slot(k)],xi=imag[slot(k)],yr=real[slot(m_-k)],yi=-imag[slot(m_-k)];
            const auto w=real_roots_[k];
            const float dr=xr-yr,di=xi-yi;
            set(slot(k),Complex(.5f*(xr+yr-w.real()*di+w.imag()*dr),.5f*(xi+yi+w.real()*dr+w.imag()*di)));
        }
        }
        complex_fft(true,normalize);
        for(std::size_t j=0;j<m_;++j) {time[2*j]=get(j).real();time[2*j+1]=get(j).imag();}
    }
};
} }
