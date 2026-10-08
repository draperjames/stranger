#define _POSIX_C_SOURCE 200809L
#include "../src/host/audio_fx_api_v2.h"
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)
#define BLOCK 128
#define TAU 6.2831853071795864769
static audio_fx_api_v2_t *api;
static uint32_t random_state=1;
static int16_t noise(void) {
    random_state^=random_state<<13;
    random_state^=random_state>>17;
    random_state^=random_state<<5;
    return (int16_t)(random_state & 65535);
}
static void param(void *s,const char *key,float v) {
    char text[40]; snprintf(text,sizeof(text),"%.9g",(double)v); api->set_param(s,key,text);
}
static float value(void *s,const char *key) {
    char text[80]; CHECK(api->get_param(s,key,text,sizeof(text))>0); return strtof(text,NULL);
}
static void silence(void *s,int frames) {
    int16_t b[2*BLOCK];
    while (frames>0) { int count=frames<BLOCK?frames:BLOCK; memset(b,0,sizeof(b)); api->process_block(s,b,count); frames-=count; }
}
static void test_contract(void) {
    void *s=api->create_instance(".",NULL); CHECK(s);
    char state[1024],copy[1024],tiny[4];
    CHECK(api->get_param(s,"state",state,sizeof(state))>0);
    CHECK(api->get_param(s,"state",tiny,sizeof(tiny))==-1);
    CHECK(api->get_param(s,"unknown",copy,sizeof(copy))==-1);
    param(s,"rate",999); CHECK(value(s,"rate")==10);
    param(s,"rate",-20); CHECK(fabsf(value(s,"rate")-.1f)<1e-6f);
    api->set_param(s,"rate","nan"); CHECK(fabsf(value(s,"rate")-.1f)<1e-6f);
    api->set_param(s,"rate","inf"); CHECK(fabsf(value(s,"rate")-.1f)<1e-6f);
    api->set_param(s,"rate","0.5junk"); CHECK(fabsf(value(s,"rate")-.1f)<1e-6f);
    api->set_param(s,"rate",""); CHECK(fabsf(value(s,"rate")-.1f)<1e-6f);
    api->set_param(s,"state",state);
    CHECK(api->get_param(s,"state",copy,sizeof(copy))>0); CHECK(!strcmp(copy,state));
    const char *malformed[]={"", "{", "[]", "{\"mix\":1,}", "{\"mix\":1,\"rate\":\"nan\"}", "{\"mix\":1", "{\"mix\":[1]}","{\"mix\":1} trailing"};
    for (unsigned i=0;i<sizeof(malformed)/sizeof(malformed[0]);++i) {
        api->set_param(s,"state",malformed[i]);
        CHECK(api->get_param(s,"state",copy,sizeof(copy))>0); CHECK(!strcmp(copy,state));
    }
    api->set_param(s,"state"," { \"rate\": \"2.75\", \"primary_mode\": \"Phaser\", \"secondary_mode\": 0, \"output\": 0.25 } ");
    CHECK(value(s,"rate")==2.75f); CHECK(value(s,"output")==.25f);
    CHECK(api->get_param(s,"primary_mode",copy,sizeof(copy))>0); CHECK(!strcmp(copy,"Phaser"));
    CHECK(api->get_param(s,"secondary_mode",copy,sizeof(copy))>0); CHECK(!strcmp(copy,"Tremolo"));
    void *other=api->create_instance(".",NULL); CHECK(other); CHECK(value(other,"rate")==1.5f);
    api->destroy_instance(other);
    /* Parser fuzz under ASan: bounded random strings must never corrupt state. */
    for (int i=0;i<2000;++i) {
        char fuzz[256]; int n=i%255;
        for (int j=0;j<n;++j) fuzz[j]=(char)(32+((uint16_t)noise()%95));
        fuzz[n]=0; api->set_param(s,"state",fuzz);
    }
    api->process_block(s,NULL,128); api->process_block(s,(int16_t *)tiny,0);
    api->destroy_instance(s);
    void *instances[33];
    for (int i=0;i<32;++i) { instances[i]=api->create_instance(".",NULL); CHECK(instances[i]); }
    CHECK(!api->create_instance(".",NULL));
    for (int i=0;i<32;++i) api->destroy_instance(instances[i]);
    puts("PASS ABI, finite/clamped parameters, transactional recall, parser fuzz, independent instances/pool");
}
static void test_reference(int sr) {
    int16_t b[2*BLOCK];
    void *s=api->create_instance(".","{\"mix\":0,\"output\":0.5}"); CHECK(s);
    silence(s,sr/10);
    for (int n=0;n<BLOCK;++n) { b[2*n]=10000; b[2*n+1]=-10000; }
    api->process_block(s,b,BLOCK);
    for (int n=0;n<BLOCK;++n) { CHECK(b[2*n]==5000); CHECK(b[2*n+1]==-5000); }
    api->destroy_instance(s);
    s=api->create_instance(".","{\"primary_mode\":0,\"secondary\":0,\"mix\":1,\"depth\":1,\"rate\":1,\"output\":1}"); CHECK(s);
    double error=0;
    for (int pos=0;pos<sr*2;pos+=BLOCK) {
        for (int n=0;n<BLOCK;++n) b[2*n]=b[2*n+1]=10000;
        api->process_block(s,b,BLOCK);
        for (int n=0;n<BLOCK;++n) if (pos+n>sr/10) {
            double expected=2500*(1+sin(TAU*(pos+n)/sr));
            double e=fabs(b[2*n]-expected); if(e>error)error=e;
            CHECK(b[2*n]==b[2*n+1]);
        }
    }
    CHECK(error<12); api->destroy_instance(s);
    /* Secondary=Tremolo must have its own ~19.5 ms offset at knob max. */
    s=api->create_instance(".","{\"primary_mode\":0,\"secondary_mode\":0,\"depth\":0,\"secondary\":1,\"mix\":1,\"output\":1}"); CHECK(s);
    silence(s,sr/10);
    int late_first=-1;
    for(int pos=0;pos<sr/20;pos+=BLOCK) {
        memset(b,0,sizeof(b)); if(pos==0)b[0]=12000;
        api->process_block(s,b,BLOCK);
        for(int n=0;n<BLOCK;++n) {
            CHECK(b[2*n+1]==0); /* No L -> R crossfeed. */
            if(pos+n>4 && b[2*n]!=0 && late_first<0)late_first=pos+n;
        }
    }
    CHECK(abs(late_first-(int)(.0195*sr))<3); api->destroy_instance(s);
    /* Chorus impulse arrives at 7 ms and not one sample earlier. */
    s=api->create_instance(".","{\"primary_mode\":1,\"depth\":0,\"secondary\":0,\"mix\":1,\"output\":1}"); CHECK(s);
    silence(s,sr/10);
    int first=-1,peak=0;
    for(int pos=0;pos<sr/50;pos+=BLOCK) {
        memset(b,0,sizeof(b)); if(pos==0)b[0]=12000;
        api->process_block(s,b,BLOCK);
        for(int n=0;n<BLOCK;++n) {
            CHECK(b[2*n+1]==0);
            if(abs(b[2*n])>peak)peak=abs(b[2*n]);
            if(b[2*n]!=0 && first<0)first=pos+n;
        }
    }
    CHECK(abs(first-(int)(.007*sr))<3); CHECK(peak>3000); api->destroy_instance(s);
    printf("PASS %d Hz: dry gain, analytic tremolo (max error %.1f int16 units), secondary pre-delay, chorus timing, stereo isolation\n",sr,error);
}
static void test_sweep(int sr) {
    int16_t b[2*BLOCK]; int global_peak=0;
    for(int pm=0;pm<3;++pm)for(int sm=0;sm<2;++sm) {
        char state[256];
        snprintf(state,sizeof(state),"{\"primary_mode\":%d,\"secondary_mode\":%d,\"depth\":1,\"secondary\":1,\"regen\":1,\"mix\":1,\"output\":1,\"rate\":10}",pm,sm);
        void *s=api->create_instance(".",state);CHECK(s);
        for(int block=0;block<1800;++block) {
            /* DC, Nyquist, sine, noise, impulses and extremes. */
            for(int n=0;n<BLOCK;++n) {
                int pos=block*BLOCK+n;
                int16_t v;
                switch(block/300) {
                    case 0: v=32767;break;
                    case 1: v=pos%2?32767:-32768;break;
                    case 2: v=(int16_t)(32767*sin(TAU*997*pos/sr));break;
                    case 3: v=noise();break;
                    case 4: v=pos%89==0?-32768:0;break;
                    default:v=noise();break;
                }
                b[2*n]=v;b[2*n+1]=(int16_t)(-((int)v)/2);
            }
            if(block>=1500 && block%10==0) {
                param(s,"rate",block%20?.1f:10);param(s,"secondary",block%20?0:1);
                param(s,"mix",block%20?0:1);param(s,"regen",block%20?0:1);
                param(s,"primary_mode",(float)(block%3));param(s,"secondary_mode",(float)(block%2));
            }
            api->process_block(s,b,BLOCK);
            for(int i=0;i<2*BLOCK;++i){CHECK(abs(b[i])<=16384);if(abs(b[i])>global_peak)global_peak=abs(b[i]);}
        }
        param(s,"mix",1);param(s,"regen",1);
        /* Regeneration cannot keep making sound indefinitely without input. */
        silence(s,sr*5);
        memset(b,0,sizeof(b));api->process_block(s,b,BLOCK);
        for(int i=0;i<2*BLOCK;++i)CHECK(abs(b[i])<=1);
        api->destroy_instance(s);
        /* Reused pool cells/delay data must not leak a previous instance. */
        s=api->create_instance(".",state);CHECK(s);
        for(int block=0;block<32;++block) {
            memset(b,0,sizeof(b));api->process_block(s,b,BLOCK);
            for(int i=0;i<2*BLOCK;++i)CHECK(b[i]==0);
        }
        api->destroy_instance(s);
    }
    printf("PASS %d Hz: six mode combinations, maximum feedback/input, abrupt controls, decay, fresh-instance silence; peak %d (-6.02 dBFS cap)\n",sr,global_peak);
}
static void test_transitions(int sr) {
    int16_t b[2*BLOCK];
    void *s=api->create_instance(".","{\"mix\":0,\"output\":0.25}");CHECK(s);
    silence(s,sr/10);
    param(s,"output",1);
    for(int n=0;n<BLOCK;++n)b[2*n]=b[2*n+1]=10000;
    api->process_block(s,b,BLOCK);
    CHECK(b[0]>2500 && b[0]<2520); /* No abrupt output gain jump. */
    for(int n=1;n<BLOCK;++n)CHECK(abs(b[2*n]-b[2*n-2])<20);
    api->destroy_instance(s);
    s=api->create_instance(".","{\"mix\":0,\"output\":1}");CHECK(s);
    silence(s,sr/10);
    for(int n=0;n<BLOCK;++n){b[2*n]=20000;b[2*n+1]=4000;}
    api->process_block(s,b,BLOCK);
    for(int n=0;n<BLOCK;++n){CHECK(b[2*n]==16384);CHECK(abs(b[2*n+1]-3277)<=1);}
    api->destroy_instance(s);
    void *reference=api->create_instance(".","{\"primary_mode\":0,\"depth\":1,\"rate\":0.1,\"mix\":1,\"output\":1}");CHECK(reference);
    s=api->create_instance(".","{\"primary_mode\":0,\"depth\":1,\"rate\":0.1,\"mix\":1,\"output\":1}");CHECK(s);
    int16_t other[2*BLOCK];
    for(int block=0;block<100;++block) {
        for(int n=0;n<2*BLOCK;++n)b[n]=other[n]=10000;
        api->process_block(s,b,BLOCK);api->process_block(reference,other,BLOCK);
    }
    param(s,"primary_mode",1);
    for(int n=0;n<2*BLOCK;++n)b[n]=other[n]=10000;
    api->process_block(s,b,BLOCK);api->process_block(reference,other,BLOCK);
    CHECK(abs(b[0]-other[0])<20); /* Crossfade first sample, not hard switching. */
    api->destroy_instance(s);api->destroy_instance(reference);
    printf("PASS %d Hz: output smoothing, mode crossfade and stereo-linked limiting preserve balance\n",sr);
}
static double seconds(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9; }
static void benchmark(int sr) {
    void *s=api->create_instance(".","{\"primary_mode\":2,\"secondary\":1,\"regen\":1,\"mix\":1}");CHECK(s);
    int16_t input[2*BLOCK],b[2*BLOCK];
    for(int i=0;i<2*BLOCK;++i)input[i]=noise();
    double start=seconds();
    for(int i=0;i<5000;++i){memcpy(b,input,sizeof(b));api->process_block(s,b,BLOCK);}
    double mean=(seconds()-start)*1e6/5000;
    printf("BENCH %d Hz: %.2f us/128-frame block (%.2f%% of %.1f us period)\n",sr,mean,mean/(BLOCK*1e6/sr)*100,BLOCK*1e6/sr);
    CHECK(mean<500);api->destroy_instance(s);
}
int main(int argc,char **argv) {
    CHECK(argc==2);
    void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib){fprintf(stderr,"dlopen: %s\n",dlerror());return 1;}
    audio_fx_init_v2_fn init;
    void *symbol=dlsym(lib,AUDIO_FX_INIT_V2_SYMBOL);CHECK(symbol);memcpy(&init,&symbol,sizeof(init));
    host_api_v1_t host={.api_version=1,.sample_rate=44100,.frames_per_block=128};
    api=init(&host);CHECK(api && api->api_version==2);test_contract();
    for(int i=0;i<2;++i) {
        host.sample_rate=i?48000:44100;api=init(&host);CHECK(api);
        test_reference(host.sample_rate);test_sweep(host.sample_rate);test_transitions(host.sample_rate);benchmark(host.sample_rate);
    }
    host.sample_rate=96000;CHECK(!init(&host));host.sample_rate=44100;host.api_version=99;CHECK(!init(&host));
    CHECK(init(NULL));
    dlclose(lib);puts("ALL TESTS PASSED (offline DSP; no hardware audio output)");return 0;
}
