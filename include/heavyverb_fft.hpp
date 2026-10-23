// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ian Shores
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <type_traits>
#include <vector>
#if !defined(HEAVYVERB_FFT_SCALAR) && (defined(__SSE2__) || defined(_M_X64))
#include <xmmintrin.h>
#define HEAVYVERB_FFT_SSE 1
#ifdef HEAVYVERB_FFT_FMA
#ifndef __FMA__
#error "FMA requires FMA compiler support and a compatible target CPU"
#endif
#include <immintrin.h>
#endif
#endif
namespace heavyverb {
namespace detail {
// Even/odd real samples form an N/2-point complex FFT. Forward DIF and
// inverse DIT radix-4 stages share a mixed-radix permutation; no reorder pass.
// Spectrum contract: separate real/imaginary arrays, N/2+1 slots. Logical bin k
// lives at slot(k); DC is slot 0, Nyquist slot N/2, both with zero imaginary
// part. All ordinary slots use ordinary complex multiplication. IRs, history,
// accumulator, and inverse share this representation. The inverse has unit
// roundtrip gain. All plans/scratch belong to this instance, prepared off-thread.
// Inputs/outputs of the transform must not alias its private scratch buffers.
class RealFFT {
    friend struct FFTProbe;
    using Complex = std::complex<float>;
    std::size_t n_, m_;
    std::vector<Complex> real_roots_;
    std::vector<float> r_, i_;
    Complex get(std::size_t k) const noexcept { return Complex(r_[k], i_[k]); }
    void set(std::size_t k, Complex x) noexcept {
        r_[k] = x.real();
        i_[k] = x.imag();
    }
    struct Stage {
        std::size_t len, offset;
    };
    std::vector<Stage> stages_;
    std::vector<std::size_t> order_;
    std::vector<float> radix_r_, radix_i_;
#ifdef HEAVYVERB_FFT_SSE
    struct Four {
        __m128 v;
        Four(__m128 x) : v(x) {}
        Four(float x) : v(_mm_set1_ps(x)) {}
        friend Four operator+(Four a, Four b) { return _mm_add_ps(a.v, b.v); }
        friend Four operator-(Four a, Four b) { return _mm_sub_ps(a.v, b.v); }
        friend Four operator*(Four a, Four b) { return _mm_mul_ps(a.v, b.v); }
    };
#endif
    template <class T> static T load(const float *p) noexcept {
#ifdef HEAVYVERB_FFT_SSE
        if constexpr (!std::is_same<T, float>::value)
            return _mm_loadu_ps(p);
        else
#endif
            return *p;
    }
    template <class T> static void store(float *p, T x) noexcept {
#ifdef HEAVYVERB_FFT_SSE
        if constexpr (!std::is_same<T, float>::value)
            _mm_storeu_ps(p, x.v);
        else
#endif
            *p = x;
    }
    template <bool inverse, bool twiddled, class T, class Roots>
    static void kernel4(T &ar, T &ai, T &br, T &bi, T &cr, T &ci, T &dr, T &di, std::size_t tw,
                        std::size_t q, Roots root) noexcept {
        auto multiply = [&](T &x, T &y, std::size_t w) {
            const T wr = root(w, false), wi = root(w, true) * T(inverse ? -1.f : 1.f);
#ifdef HEAVYVERB_FFT_FMA
            if constexpr (!std::is_same<T, float>::value) {
                const T re = _mm_fmsub_ps(x.v, wr.v, _mm_mul_ps(y.v, wi.v));
                y = _mm_fmadd_ps(x.v, wi.v, _mm_mul_ps(y.v, wr.v));
                x = re;
            } else {
                const T re = std::fma(x, wr, -y * wi);
                y = std::fma(x, wi, y * wr);
                x = re;
            }
#else
            const T re = x * wr - y * wi;
            y = x * wi + y * wr;
            x = re;
#endif
        };
        if constexpr (inverse && twiddled) {
            multiply(br, bi, tw);
            multiply(cr, ci, tw + q);
            multiply(dr, di, tw + 2 * q);
        }
        const T ur = ar + cr, ui = ai + ci, vr = ar - cr, vi = ai - ci;
        const T sr = br + dr, si = bi + di, tr = br - dr, ti = bi - di;
        ar = ur + sr;
        ai = ui + si;
        cr = ur - sr;
        ci = ui - si;
        if constexpr (inverse) {
            br = vr - ti;
            bi = vi + tr;
            dr = vr + ti;
            di = vi - tr;
        } else {
            br = vr + ti;
            bi = vi - tr;
            dr = vr - ti;
            di = vi + tr;
        }
        if constexpr (!inverse && twiddled) {
            multiply(br, bi, tw);
            multiply(cr, ci, tw + q);
            multiply(dr, di, tw + 2 * q);
        }
    }
    template <bool inverse, class T>
    void butterfly4(std::size_t a, std::size_t q, std::size_t tw) noexcept {
        T ar = load<T>(&r_[a]), ai = load<T>(&i_[a]);
        T br = load<T>(&r_[a + q]), bi = load<T>(&i_[a + q]);
        T cr = load<T>(&r_[a + 2 * q]), ci = load<T>(&i_[a + 2 * q]);
        T dr = load<T>(&r_[a + 3 * q]), di = load<T>(&i_[a + 3 * q]);
        auto root = [&](std::size_t w, bool imaginary) {
            return load<T>((imaginary ? radix_i_ : radix_r_).data() + w);
        };
        kernel4<inverse, true>(ar, ai, br, bi, cr, ci, dr, di, tw, q, root);
        store(&r_[a], ar);
        store(&i_[a], ai);
        store(&r_[a + q], br);
        store(&i_[a + q], bi);
        store(&r_[a + 2 * q], cr);
        store(&i_[a + 2 * q], ci);
        store(&r_[a + 3 * q], dr);
        store(&i_[a + 3 * q], di);
    }
#ifdef HEAVYVERB_FFT_SSE
    // Small terminal stages vectorize across independent groups. Q=1 uses
    // four groups, Q=2 uses two. All supported sizes contain complete batches.
    template <bool inverse, int Q> void terminal_stage(const Stage &stage) noexcept {
        for (std::size_t base = 0; base < m_; base += 16) {
            auto read = [&](const std::vector<float> &v, __m128 &a, __m128 &b, __m128 &c,
                            __m128 &d) {
                a = _mm_loadu_ps(v.data() + base);
                b = _mm_loadu_ps(v.data() + base + 4);
                c = _mm_loadu_ps(v.data() + base + 8);
                d = _mm_loadu_ps(v.data() + base + 12);
                if constexpr (Q == 1) {
                    _MM_TRANSPOSE4_PS(a, b, c, d);
                } else {
                    const auto aa = _mm_movelh_ps(a, c), bb = _mm_movehl_ps(c, a);
                    const auto cc = _mm_movelh_ps(b, d), dd = _mm_movehl_ps(d, b);
                    a = aa;
                    b = bb;
                    c = cc;
                    d = dd;
                }
            };
            __m128 ar0, br0, cr0, dr0, ai0, bi0, ci0, di0;
            read(r_, ar0, br0, cr0, dr0);
            read(i_, ai0, bi0, ci0, di0);
            Four ar(ar0), br(br0), cr(cr0), dr(dr0), ai(ai0), bi(bi0), ci(ci0), di(di0);
            auto root = [&](std::size_t w, bool imaginary) -> Four {
                const auto &v = imaginary ? radix_i_ : radix_r_;
                if constexpr (Q == 2)
                    return _mm_set_ps(v[w + 1], v[w], v[w + 1], v[w]);
                else
                    return Four(imaginary ? 0.f : 1.f);
            };
            kernel4<inverse, Q != 1>(ar, ai, br, bi, cr, ci, dr, di, stage.offset, Q, root);
            auto write = [&](std::vector<float> &v, __m128 a, __m128 b, __m128 c, __m128 d) {
                if constexpr (Q == 1) {
                    _MM_TRANSPOSE4_PS(a, b, c, d);
                } else {
                    const auto aa = _mm_movelh_ps(a, b), bb = _mm_movelh_ps(c, d);
                    const auto cc = _mm_movehl_ps(b, a), dd = _mm_movehl_ps(d, c);
                    a = aa;
                    b = bb;
                    c = cc;
                    d = dd;
                }
                _mm_storeu_ps(v.data() + base, a);
                _mm_storeu_ps(v.data() + base + 4, b);
                _mm_storeu_ps(v.data() + base + 8, c);
                _mm_storeu_ps(v.data() + base + 12, d);
            };
            write(r_, ar.v, br.v, cr.v, dr.v);
            write(i_, ai.v, bi.v, ci.v, di.v);
        }
    }
#endif
    template <bool inverse> void radix_stage(const Stage &stage) noexcept {
        const auto q = stage.len / 4;
#ifdef HEAVYVERB_FFT_SSE
        if (q == 1) {
            terminal_stage<inverse, 1>(stage);
            return;
        }
        if (q == 2) {
            terminal_stage<inverse, 2>(stage);
            return;
        }
#endif
        for (std::size_t base = 0; base < m_; base += stage.len) {
            std::size_t j = 0;
#ifdef HEAVYVERB_FFT_SSE
            for (; j + 4 <= q; j += 4)
                butterfly4<inverse, Four>(base + j, q, stage.offset + j);
#endif
            for (; j < q; ++j)
                butterfly4<inverse, float>(base + j, q, stage.offset + j);
        }
    }
    void radix2_last() noexcept {
        for (std::size_t k = 0; k < m_; k += 2) {
            const float ar = r_[k], ai = i_[k], br = r_[k + 1], bi = i_[k + 1];
            r_[k] = ar + br;
            i_[k] = ai + bi;
            r_[k + 1] = ar - br;
            i_[k + 1] = ai - bi;
        }
    }
    void complex_fft(bool inverse) noexcept {
        const bool odd = ((m_ >> (2 * stages_.size())) == 2);
        if (inverse) {
            if (odd)
                radix2_last();
            for (auto s = stages_.rbegin(); s != stages_.rend(); ++s)
                radix_stage<true>(*s);
        } else {
            for (const auto &s : stages_)
                radix_stage<false>(s);
            if (odd)
                radix2_last();
        }
    }

  public:
    explicit RealFFT(std::size_t n) : n_(n), m_(n / 2), real_roots_(m_), r_(m_), i_(m_) {
        for (std::size_t k = 0; k < m_; ++k) {
            const double angle = -6.2831853071795864769 * double(k) / n_;
            real_roots_[k] = Complex(float(std::cos(angle)), float(std::sin(angle)));
        }
        order_.resize(m_);
        for (std::size_t k = 0; k < m_; ++k) {
            std::size_t v = k, len = m_, index = 0;
            while (len >= 4) {
                len /= 4;
                index += (v % 4) * len;
                v /= 4;
            }
            if (len == 2)
                index += v % 2;
            order_[k] = index;
        }
        for (std::size_t len = m_; len >= 4; len /= 4) {
            const auto q = len / 4, offset = radix_r_.size();
            stages_.push_back({len, offset});
            for (std::size_t power = 1; power <= 3; ++power)
                for (std::size_t j = 0; j < q; ++j) {
                    const double angle = -6.2831853071795864769 * double(j * power) / len;
                    radix_r_.push_back(float(std::cos(angle)));
                    radix_i_.push_back(float(std::sin(angle)));
                }
        }
    }
    std::size_t slot(std::size_t k) const noexcept { return k == m_ ? m_ : order_[k]; }
    std::size_t memory_bytes() const noexcept {
        return sizeof(*this) + real_roots_.capacity() * sizeof(Complex) +
               (r_.capacity() + i_.capacity() + radix_r_.capacity() + radix_i_.capacity()) *
                   sizeof(float) +
               order_.capacity() * sizeof(std::size_t) + stages_.capacity() * sizeof(Stage);
    }
    void forward(const float *time, float *real, float *imag) noexcept {
        for (std::size_t j = 0; j < m_; ++j)
            set(j, Complex(time[2 * j], time[2 * j + 1]));
        complex_fft(false);
        real[0] = get(0).real() + get(0).imag();
        imag[0] = 0;
        real[m_] = get(0).real() - get(0).imag();
        imag[m_] = 0;
        // Paired bins share the same two complex inputs and twiddle products.
        for (std::size_t k = 1; k < m_ / 2; ++k) {
            const auto a = get(slot(k)), b = std::conj(get(slot(m_ - k))), w = real_roots_[k];
            const float ur = a.real() + b.real(), ui = a.imag() + b.imag();
            const float dr = a.real() - b.real(), di = a.imag() - b.imag();
            const float tr = w.real() * di + w.imag() * dr, ti = w.imag() * di - w.real() * dr;
            real[slot(k)] = .5f * (ur + tr);
            imag[slot(k)] = .5f * (ui + ti);
            real[slot(m_ - k)] = .5f * (ur - tr);
            imag[slot(m_ - k)] = .5f * (ti - ui);
        }
        const auto half = get(slot(m_ / 2));
        real[slot(m_ / 2)] = half.real();
        imag[slot(m_ / 2)] = -half.imag();
    }
    void inverse(const float *real, const float *imag, float *time) noexcept {
        set(0, Complex(.5f * (real[0] + real[m_]), .5f * (real[0] - real[m_])));
        for (std::size_t k = 1; k < m_ / 2; ++k) {
            const auto a = slot(k), b = slot(m_ - k);
            const float xr = real[a], xi = imag[a], yr = real[b], yi = -imag[b];
            const float ur = xr + yr, ui = xi + yi, dr = xr - yr, di = xi - yi;
            const auto w = real_roots_[k];
            const float tr = -w.real() * di + w.imag() * dr, ti = w.real() * dr + w.imag() * di;
            set(a, Complex(.5f * (ur + tr), .5f * (ui + ti)));
            set(b, Complex(.5f * (ur - tr), .5f * (ti - ui)));
        }
        set(slot(m_ / 2), Complex(real[slot(m_ / 2)], -imag[slot(m_ / 2)]));
        complex_fft(true);
        const float scale = 1.f / float(m_);
        for (std::size_t j = 0; j < m_; ++j) {
            time[2 * j] = get(j).real() * scale;
            time[2 * j + 1] = get(j).imag() * scale;
        }
    }
};
} // namespace detail
} // namespace heavyverb
