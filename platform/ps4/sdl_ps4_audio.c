/* PS4 main-port playback for the SDL audio thread.
 * The engine mixes at its own rate. The console port only accepts 48 kHz grains. */
#include "SDL_internal.h"
#include "SDL_audio.h"
#include "audio/SDL_sysaudio.h"

#include <orbis/AudioOut.h>
#include <orbis/UserService.h>
#include <stdio.h>
#include <string.h>

#define PS4_RATE 48000
#define PS4_GRAIN 1024
#define PS4_SRC_CAP 8192

struct SDL_PrivateAudioData {
    int32_t handle;
    int src_rate;
    int src_channels;
    int16_t *callback;
    int16_t src[PS4_SRC_CAP * 2];
    int src_count;
    uint32_t phase;
    int16_t out[PS4_GRAIN * 2];
};

static int PS4AUDIO_Init(SDL_AudioDriverImpl *impl);

AudioBootStrap PS4AUDIO_bootstrap = {
    "ps4", "PS4 audio driver", PS4AUDIO_Init, 0
};

static uint32_t resample_step(int src_rate) {
    if (src_rate < 8000)
        src_rate = 44100;
    return (uint32_t)(((uint64_t)src_rate << 16) / PS4_RATE);
}

static void append_source(struct SDL_PrivateAudioData *hidden, const int16_t *frames, int count) {
    int i;
    if (count <= 0)
        return;
    if (hidden->src_count + count > PS4_SRC_CAP) {
        int drop = hidden->src_count + count - PS4_SRC_CAP;
        if (drop > hidden->src_count)
            drop = hidden->src_count;
        memmove(hidden->src, hidden->src + drop * 2, (size_t)(hidden->src_count - drop) * 2 * sizeof(int16_t));
        hidden->src_count -= drop;
        if ((hidden->phase >> 16) >= (uint32_t)drop)
            hidden->phase -= (uint32_t)drop << 16;
        else
            hidden->phase = hidden->phase & 0xffffu;
    }
    if (hidden->src_channels == 1) {
        for (i = 0; i < count; i++) {
            int16_t sample = frames[i];
            hidden->src[(hidden->src_count + i) * 2] = sample;
            hidden->src[(hidden->src_count + i) * 2 + 1] = sample;
        }
    } else {
        memcpy(hidden->src + hidden->src_count * 2, frames, (size_t)count * 2 * sizeof(int16_t));
    }
    hidden->src_count += count;
}

static void play_source(struct SDL_PrivateAudioData *hidden) {
    const uint32_t step = resample_step(hidden->src_rate);
    while (hidden->src_count > 2) {
        const uint64_t need = ((uint64_t)(hidden->phase & 0xffffu) + (uint64_t)step * PS4_GRAIN + 65535u) >> 16;
        int i;
        if ((uint64_t)hidden->src_count < need + 1)
            break;
        for (i = 0; i < PS4_GRAIN; i++) {
            const uint32_t idx = hidden->phase >> 16;
            const uint32_t frac = hidden->phase & 0xffffu;
            int channel;
            for (channel = 0; channel < 2; channel++) {
                const int s0 = hidden->src[idx * 2 + channel];
                const int s1 = hidden->src[(idx + 1) * 2 + channel];
                hidden->out[i * 2 + channel] = (int16_t)(s0 + (((s1 - s0) * (int)frac) >> 16));
            }
            hidden->phase += step;
        }
        {
            const uint32_t consumed = hidden->phase >> 16;
            const int remain = hidden->src_count - (int)consumed;
            if (consumed && remain > 0)
                memmove(hidden->src, hidden->src + consumed * 2, (size_t)remain * 2 * sizeof(int16_t));
            hidden->src_count = remain > 0 ? remain : 0;
            hidden->phase &= 0xffffu;
        }
        sceAudioOutOutput(hidden->handle, NULL);
        sceAudioOutOutput(hidden->handle, hidden->out);
    }
}

static int PS4AUDIO_OpenDevice(SDL_AudioDevice *this, void *handle, const char *devname, int iscapture) {
    int32_t volumes[2];
    (void)handle;
    (void)devname;
    (void)iscapture;
    this->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*this->hidden));
    if (!this->hidden)
        return SDL_OutOfMemory();
    if (this->spec.channels < 1)
        this->spec.channels = 2;
    if (this->spec.freq < 8000)
        this->spec.freq = 44100;
    if (this->spec.format != AUDIO_S16LSB) {
        SDL_free(this->hidden);
        this->hidden = NULL;
        return SDL_SetError("PS4 audio wants S16");
    }
    this->hidden->src_rate = this->spec.freq;
    this->hidden->src_channels = this->spec.channels > 1 ? 2 : 1;
    SDL_CalculateAudioSpec(&this->spec);
    this->hidden->callback = (int16_t *)SDL_malloc(this->spec.size ? this->spec.size : 4);
    if (!this->hidden->callback) {
        SDL_free(this->hidden);
        this->hidden = NULL;
        return SDL_OutOfMemory();
    }
    this->hidden->handle = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,
                                           0, PS4_GRAIN, PS4_RATE, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (this->hidden->handle <= 0) {
        fprintf(stderr, "PS4 audio open failed %#x\n", (unsigned)this->hidden->handle);
        SDL_free(this->hidden->callback);
        SDL_free(this->hidden);
        this->hidden = NULL;
        return SDL_SetError("sceAudioOutOpen failed");
    }
    volumes[0] = 32768;
    volumes[1] = 32768;
    sceAudioOutSetVolume(this->hidden->handle, 3, volumes);
    fprintf(stderr, "PS4 audio open handle=%d mix=%dHz grain=%d\n", this->hidden->handle, this->hidden->src_rate, PS4_GRAIN);
    return 0;
}

static void PS4AUDIO_PlayDevice(SDL_AudioDevice *this) {
    const int frames = this->spec.samples;
    if (!this->hidden || this->hidden->handle <= 0 || frames <= 0)
        return;
    append_source(this->hidden, this->hidden->callback, frames);
    play_source(this->hidden);
}

static void PS4AUDIO_WaitDevice(SDL_AudioDevice *this) {
    (void)this;
}

static Uint8 *PS4AUDIO_GetDeviceBuf(SDL_AudioDevice *this) {
    return (Uint8 *)this->hidden->callback;
}

static void PS4AUDIO_CloseDevice(SDL_AudioDevice *this) {
    if (!this->hidden)
        return;
    if (this->hidden->handle > 0)
        sceAudioOutClose(this->hidden->handle);
    SDL_free(this->hidden->callback);
    SDL_free(this->hidden);
    this->hidden = NULL;
}

static int PS4AUDIO_Init(SDL_AudioDriverImpl *impl) {
    sceUserServiceInitialize(NULL);
    if (sceAudioOutInit() != 0) {
        fprintf(stderr, "PS4 audio init failed\n");
        return 0;
    }
    impl->OpenDevice = PS4AUDIO_OpenDevice;
    impl->PlayDevice = PS4AUDIO_PlayDevice;
    impl->WaitDevice = PS4AUDIO_WaitDevice;
    impl->GetDeviceBuf = PS4AUDIO_GetDeviceBuf;
    impl->CloseDevice = PS4AUDIO_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = 1;
    fprintf(stderr, "PS4 audio driver ready\n");
    return 1;
}
