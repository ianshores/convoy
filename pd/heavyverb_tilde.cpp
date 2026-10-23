// SPDX-License-Identifier: MIT
#include "m_pd.h"
#include "heavyverb.hpp"
#include <memory>
#include <vector>
static_assert(sizeof(t_sample) == sizeof(float), "heavyverb requires a float32 Pd build");
static t_class* klass;
struct Object {
    t_object object;
    t_float signal;
    t_symbol* table;
    int partition;
    t_float cutoff;
    heavyverb::Convolver* engine;
};
static void reload(Object* x) {
    // Pd DSP method runs while the audio graph is suspended. Never rebuild from
    // perform or from a live control message.
    delete x->engine; x->engine = nullptr;
    auto array = reinterpret_cast<t_garray*>(pd_findbyclass(x->table, garray_class));
    int count = 0; t_word* data = nullptr;
    if (!array || !garray_getfloatwords(array, &count, &data) || count <= 0) {
        pd_error(x, "heavyverb~: missing/empty float array %s", x->table->s_name); return;
    }
    try {
        std::vector<float> ir(count);
        for (int i = 0; i < count; ++i) ir[i] = data[i].w_float;
        x->engine = new heavyverb::Convolver(ir.data(), ir.size(), x->partition);
    } catch (const std::exception& e) { pd_error(x, "heavyverb~: %s", e.what()); }
}
static t_int* perform(t_int* w) {
    auto x = reinterpret_cast<Object*>(w[1]);
    auto in = reinterpret_cast<t_sample*>(w[2]), out = reinterpret_cast<t_sample*>(w[3]);
    auto n = static_cast<int>(w[4]);
    if (x->engine) { x->engine->set_tail_cutoff_db(x->cutoff); x->engine->process(in, out, n); }
    else std::fill(out, out + n, 0.f);
    return w + 5;
}
static void dsp(Object* x, t_signal** s) {
    reload(x);
    dsp_add(perform, 4, x, s[0]->s_vec, s[1]->s_vec, s[0]->s_n);
}
static void destroy(Object* x) { delete x->engine; }
static void* create(t_symbol* table, t_floatarg partition) {
    if (!std::isfinite(partition) || partition < 0 || partition > 8192 || std::floor(partition) != partition) {
        pd_error(nullptr, "heavyverb~: invalid partition argument"); return nullptr;
    }
    auto x = reinterpret_cast<Object*>(pd_new(klass));
    x->table = table; x->partition = partition == 0 ? 256 : static_cast<int>(partition);
    x->cutoff = 0; floatinlet_new(&x->object, &x->cutoff);
    x->engine = nullptr; outlet_new(&x->object, &s_signal);
    return x;
}
extern "C" void heavyverb_tilde_setup() {
    klass = class_new(gensym("heavyverb~"), reinterpret_cast<t_newmethod>(create),
        reinterpret_cast<t_method>(destroy), sizeof(Object), CLASS_DEFAULT, A_DEFSYM, A_DEFFLOAT, 0);
    CLASS_MAINSIGNALIN(klass, Object, signal);
    class_addmethod(klass, reinterpret_cast<t_method>(dsp), gensym("dsp"), A_CANT, 0);
}
