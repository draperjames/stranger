/* Stranger parallel modulator. See docs/PROVENANCE.md for source lineage.
 * No allocation, locks, I/O, or buffer clearing in any plugin callback.
 * Host serializes each instance's process/live parameter calls. */
#define _POSIX_C_SOURCE 200809L
#include "host/audio_fx_api_v2.h"
#include "metadata.h"
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))
#define POOL_SIZE 32
#define DELAY_SIZE 2048
#define DELAY_MASK (DELAY_SIZE - 1)
#define PARAM_COUNT 8
#define TAU 6.2831853071795864769f
#define CEILING 0.5f
_Static_assert(ATOMIC_BOOL_LOCK_FREE == 2 && ATOMIC_INT_LOCK_FREE == 2,
               "Stranger requires lock-free pool and sample-rate atomics");

enum { RATE, DEPTH, SECONDARY, REGEN, MIX, PRIMARY_MODE, SECONDARY_MODE, OUTPUT };
static const char *const keys[PARAM_COUNT] = {
    "rate", "depth", "secondary", "regen", "mix", "primary_mode", "secondary_mode", "output"
};
static const float defaults[PARAM_COUNT] = {1.5f, 0.5f, 0, 0, 0.35f, 1, 1, 0.5f};
static const char *const modes[] = {"Tremolo", "Chorus", "Phaser"};

typedef struct {
    float delay[DELAY_SIZE];
    unsigned write, valid;
    float ap_x[4], ap_y[4];
} voice_t;
typedef struct {
    voice_t voices[2];
    float feedback, dc_x, dc_y;
} channel_t;
typedef struct {
    atomic_bool used;
    float target[PARAM_COUNT], smooth[PARAM_COUNT];
    float weights[2][3];
    double phase[2];
    float sample_rate, slew, release, dc_r, limiter_gain, startup;
    channel_t channels[2];
} stranger_t;
static stranger_t pool[POOL_SIZE];
static atomic_int host_sample_rate = 44100;

static float clampf(float x, float lo, float hi) { return fminf(hi, fmaxf(lo, x)); }
static float flush(float x) { return fabsf(x) < 1e-20f ? 0 : x; }
static int param_index(const char *key) {
    for (int i = 0; i < PARAM_COUNT; ++i) if (!strcmp(key, keys[i])) return i;
    return -1;
}
static int parse_value(int index, const char *s, float *out) {
    if (index == PRIMARY_MODE || index == SECONDARY_MODE) {
        int count = index == PRIMARY_MODE ? 3 : 2;
        for (int i = 0; i < count; ++i) {
            if (!strcmp(s, modes[i])) { *out = (float)i; return 1; }
        }
    }
    char *end = NULL;
    float v = strtof(s, &end);
    if (end == s || !isfinite(v)) return 0;
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
    if (*end) return 0;
    float lo = index == RATE ? 0.1f : 0;
    float hi = index == RATE ? 10 : index == PRIMARY_MODE ? 2 : 1;
    v = clampf(v, lo, hi);
    if (index == PRIMARY_MODE || index == SECONDARY_MODE) v = roundf(v);
    *out = v;
    return 1;
}
static void skip_space(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') ++*p;
}
/* Bounded, transactional flat JSON reader. Accepts numeric or string values.
 * Unknown scalar fields allow future state revisions. No partial restores. */
static int restore(stranger_t *s, const char *json) {
    size_t len = strnlen(json, 2049);
    if (!len || len > 2048) return 0;
    float values[PARAM_COUNT];
    memcpy(values, s->target, sizeof(values));
    const char *p = json;
    skip_space(&p);
    if (*p++ != '{') return 0;
    skip_space(&p);
    if (*p != '}') for (;;) {
        char key[40], val[80];
        size_t n = 0;
        if (*p++ != '"') return 0;
        while (*p && *p != '"' && n < sizeof(key)-1) key[n++] = *p++;
        key[n] = 0;
        if (*p++ != '"') return 0;
        skip_space(&p);
        if (*p++ != ':') return 0;
        skip_space(&p);
        int quoted = *p == '"';
        if (quoted) ++p;
        n = 0;
        while (*p && n < sizeof(val)-1 &&
               (quoted ? *p != '"' : *p != ',' && *p != '}')) {
            if (*p == '\\' || *p == '{' || *p == '[') return 0;
            val[n++] = *p++;
        }
        val[n] = 0;
        if (quoted && *p++ != '"') return 0;
        int i = param_index(key);
        if (i >= 0 && !parse_value(i, val, &values[i])) return 0;
        skip_space(&p);
        if (*p == '}') break;
        if (*p++ != ',') return 0;
        skip_space(&p);
    }
    if (*p++ != '}') return 0;
    skip_space(&p);
    if (*p) return 0;
    memcpy(s->target, values, sizeof(values));
    return 1;
}

static void *create_instance(const char *dir, const char *config) {
    (void)dir;
    stranger_t *s = NULL;
    for (int i = 0; i < POOL_SIZE; ++i) {
        bool expected = false;
        if (atomic_compare_exchange_strong(&pool[i].used, &expected, true)) { s = &pool[i]; break; }
    }
    if (!s) return NULL;
    int sr = atomic_load(&host_sample_rate);
    s->sample_rate = (float)sr;
    s->slew = 1.0f - expf(-1.0f / (0.015f * sr));
    s->release = 1.0f - expf(-1.0f / (0.050f * sr));
    s->dc_r = expf(-TAU * 20.0f / sr);
    s->limiter_gain = 1;
    s->startup = 0;
    s->phase[0] = s->phase[1] = 0;
    memcpy(s->target, defaults, sizeof(defaults));
    if (config) (void)restore(s, config);
    memcpy(s->smooth, s->target, sizeof(s->smooth));
    for (int v = 0; v < 2; ++v)
        for (int m = 0; m < 3; ++m)
            s->weights[v][m] = m == (int)s->target[v == 0 ? PRIMARY_MODE : SECONDARY_MODE] ? 1 : 0;
    for (int c = 0; c < 2; ++c) {
        channel_t *ch = &s->channels[c];
        ch->feedback = ch->dc_x = ch->dc_y = 0;
        for (int v = 0; v < 2; ++v) {
            voice_t *voice = &ch->voices[v];
            voice->write = voice->valid = 0;
            for (int a = 0; a < 4; ++a) voice->ap_x[a] = voice->ap_y[a] = 0;
        }
    }
    /* Delay data is invalidated by valid=0; old cells are never read. */
    return s;
}
static void destroy_instance(void *instance) {
    if (instance) atomic_store(&((stranger_t *)instance)->used, false);
}
static float tap(const voice_t *v, int index) {
    unsigned idx = (unsigned)index & DELAY_MASK;
    unsigned age = (v->write - 1 - idx) & DELAY_MASK;
    return age < v->valid ? v->delay[idx] : 0;
}
static float delay_read(const voice_t *v, float samples) {
    samples = clampf(samples, 1, DELAY_SIZE-3);
    float pos = (float)((v->write - 1) & DELAY_MASK) - samples;
    int base = (int)floorf(pos);
    float f = pos - (float)base;
    float xm = tap(v, base-1), x0 = tap(v, base), x1 = tap(v, base+1), x2 = tap(v, base+2);
    float c1 = 0.5f * (x1-xm);
    float c2 = xm - 2.5f*x0 + 2*x1 - 0.5f*x2;
    float c3 = 0.5f*(x2-xm) + 1.5f*(x0-x1);
    return ((c3*f+c2)*f+c1)*f+x0;
}
static float voice_process(voice_t *v, float x, float lfo, float depth,
                           float offset, float sr, const float weights[3]) {
    v->delay[v->write] = x;
    v->write = (v->write+1) & DELAY_MASK;
    if (v->valid < DELAY_SIZE) ++v->valid;
    float delayed = offset > 0 ? delay_read(v, offset*0.001f*sr) : x;
    float tremolo = delayed * (1 - depth * 0.5f * (1-lfo));
    float chorus = delay_read(v, (7+offset+4*depth*lfo)*0.001f*sr);
    float a = 0.55f + 0.4f*depth*lfo;
    float phaser = x;
    for (int i = 0; i < 4; ++i) {
        float y = -a*phaser + v->ap_x[i] + a*v->ap_y[i];
        v->ap_x[i] = flush(phaser);
        v->ap_y[i] = flush(y);
        phaser = y;
    }
    return weights[0]*tremolo + weights[1]*chorus + weights[2]*0.5f*(x+phaser);
}
static void process_block(void *instance, int16_t *audio, int frames) {
    if (!instance || !audio || frames <= 0) return;
    stranger_t *s = instance;
    for (int n = 0; n < frames; ++n) {
        for (int p = 0; p < PARAM_COUNT; ++p)
            s->smooth[p] += s->slew * (s->target[p] - s->smooth[p]);
        for (int v = 0; v < 2; ++v) {
            int mode = (int)s->target[v == 0 ? PRIMARY_MODE : SECONDARY_MODE];
            for (int m = 0; m < 3; ++m)
                s->weights[v][m] += s->slew * ((m == mode ? 1.0f : 0.0f) - s->weights[v][m]);
        }
        float rate=s->smooth[RATE], depth=s->smooth[DEPTH], sec=s->smooth[SECONDARY];
        float lfo = sinf(TAU*s->phase[0]);
        float sec_rate = clampf(rate*(0.25f+2.25f*sec)*(1+0.4f*depth*lfo), 0.05f, 15);
        float sec_lfo = sinf(TAU*s->phase[1]);
        s->phase[0] += rate/s->sample_rate;
        s->phase[1] += sec_rate/s->sample_rate;
        if (s->phase[0] >= 1) s->phase[0] -= 1;
        if (s->phase[1] >= 1) s->phase[1] -= 1;
        float sec_vol = sqrtf(sec);
        float sec_depth = 1-(1-sec)*(1-sec);
        float t = fmaxf(0, (sec-0.5f)*2);
        float offset = sec < 0.5f ? 0.5f+2*sec : 1.5f+18*t*t;
        float mix=s->smooth[MIX], regen=0.75f*s->smooth[REGEN];
        float out[2];
        for (int c = 0; c < 2; ++c) {
            channel_t *ch = &s->channels[c];
            float in = audio[2*n+c]*(1.0f/32768.0f);
            float driver = clampf(in+regen*ch->feedback, -2, 2);
            float primary = voice_process(&ch->voices[0],driver,lfo,depth,0,s->sample_rate,s->weights[0]);
            float secondary = voice_process(&ch->voices[1],driver,sec_lfo,sec_depth,offset,s->sample_rate,s->weights[1]);
            float wet = 0.5f*(primary+sec_vol*secondary);
            float dc = wet-ch->dc_x+s->dc_r*ch->dc_y;
            ch->dc_x = wet;
            ch->dc_y = flush(dc);
            ch->feedback = tanhf(dc);
            out[c] = ((1-mix)*in+mix*wet)*s->smooth[OUTPUT];
            if (!isfinite(out[c])) { out[c]=0; ch->feedback=ch->dc_x=ch->dc_y=0; }
        }
        /* Stereo-linked, immediate attack, 50 ms release. Final clamp is a
         * last-resort conversion guard, never a wrapping int16 cast. */
        float peak = fmaxf(fabsf(out[0]), fabsf(out[1]));
        float gain = peak > CEILING ? CEILING/peak : 1;
        if (gain < s->limiter_gain) s->limiter_gain = gain;
        else s->limiter_gain += s->release*(gain-s->limiter_gain);
        s->startup = fminf(1, s->startup+1/(0.020f*s->sample_rate));
        for (int c = 0; c < 2; ++c) {
            float y = clampf(out[c]*s->limiter_gain*s->startup, -CEILING, CEILING);
            audio[2*n+c] = (int16_t)lrintf(y*32768);
        }
    }
}
static void set_param(void *instance, const char *key, const char *val) {
    if (!instance || !key || !val) return;
    stranger_t *s = instance;
    if (!strcmp(key,"state")) { (void)restore(s,val); return; }
    if (strnlen(val,81) > 80) return;
    int i=param_index(key);
    float v;
    if (i>=0 && parse_value(i,val,&v)) s->target[i]=v;
}
static int copy_string(char *buf, int size, const char *text) {
    size_t len=strlen(text);
    if (len >= (size_t)size) return -1;
    memcpy(buf,text,len+1);
    return (int)len;
}
static int get_param(void *instance, const char *key, char *buf, int size) {
    if (!instance || !key || !buf || size<=0) return -1;
    stranger_t *s=instance;
    if (!strcmp(key,"name")) return copy_string(buf,size,"Stranger");
    if (!strcmp(key,"module_id")) return copy_string(buf,size,"stranger");
    if (!strcmp(key,"chain_params")) return copy_string(buf,size,stranger_chain_params);
    if (!strcmp(key,"ui_hierarchy")) return copy_string(buf,size,stranger_ui_hierarchy);
    int written;
    if (!strcmp(key,"state")) {
        written=snprintf(buf,(size_t)size,
            "{\"rate\":%.9g,\"depth\":%.9g,\"secondary\":%.9g,\"regen\":%.9g,\"mix\":%.9g,"
            "\"primary_mode\":\"%s\",\"secondary_mode\":\"%s\",\"output\":%.9g}",
            (double)s->target[RATE],(double)s->target[DEPTH],(double)s->target[SECONDARY],
            (double)s->target[REGEN],(double)s->target[MIX],modes[(int)s->target[PRIMARY_MODE]],
            modes[(int)s->target[SECONDARY_MODE]],(double)s->target[OUTPUT]);
    } else {
        int i=param_index(key);
        if (i<0) return -1;
        if (i==PRIMARY_MODE || i==SECONDARY_MODE) return copy_string(buf,size,modes[(int)s->target[i]]);
        written=snprintf(buf,(size_t)size,"%.9g",(double)s->target[i]);
    }
    return written>=0 && written<size ? written : -1;
}
static audio_fx_api_v2_t api = {
    .api_version=AUDIO_FX_API_VERSION_2,
    .create_instance=create_instance,.destroy_instance=destroy_instance,
    .process_block=process_block,.set_param=set_param,.get_param=get_param,.on_midi=NULL
};
EXPORT audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    /* Only read the stable prefix; no newer optional host-tail callbacks. */
    if (host && host->api_version != MOVE_PLUGIN_API_VERSION) return NULL;
    int sr=host ? host->sample_rate : MOVE_SAMPLE_RATE;
    if (sr!=44100 && sr!=48000) return NULL;
    atomic_store(&host_sample_rate,sr);
    return &api;
}
