// DSplay UI sounds: original sounds synthesized at boot (no sampled audio is
// shipped), plus optional user packs loaded from sd:/_hyper/sfx/*.wav.
#include "sound_effects.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARM9
#include <nds.h>
#include <nds/arm9/sound.h>
#endif

#define MS(x) ((SFX_RATE * (x)) / 1000)

typedef struct {
    int16_t *data;
    int      len;
    uint32_t rate;
} Pcm;

static Pcm  s_hyper[SFX_COUNT];
static Pcm  s_user[SFX_COUNT];
static bool s_ready = false;
static bool s_hyper_built[SFX_COUNT];
static int  s_volume = 4;

static const char *const s_files[SFX_COUNT] = {
    "tick.wav", "decide.wav", "back.wav", "launch.wav", "popup.wav", "toggle.wav",
    "page.wav", "error.wav", "insert.wav", "eject.wav", "favorite.wav", "startup.wav", "key.wav",
    "boot_snap.wav", "lid_close.wav", "lid_open.wav"
};

const char *sound_event_filename(SfxEvent ev) {
    return ((unsigned)ev < SFX_COUNT) ? s_files[ev] : "";
}

// ---------------------------------------------------------------------------
// Tiny fixed-point synthesizer (fast on the ARM9 without an FPU): mixes
// decaying sine partials and filtered noise into an int32 buffer.
// ---------------------------------------------------------------------------
static int32_t *s_mix;
static int      s_mix_len;
static int16_t  s_sine[1024];
static bool     s_sine_ready = false;

static void sine_init(void) {
    if (s_sine_ready) return;
    for (int i = 0; i < 1024; i++) s_sine[i] = (int16_t)(sinf((float)i * 6.2831853f / 1024.0f) * 32767.0f);
    s_sine_ready = true;
}

static void mix_begin(int len) {
    s_mix_len = len;
    s_mix = (int32_t *)calloc((size_t)len, sizeof(int32_t));
}

// per-sample decay factor (Q16) for a time constant in ms
static uint32_t decay_q16(float ms) {
    if (ms <= 0.0f) return 65536;
    return (uint32_t)(expf(-1.0f / ((float)SFX_RATE * ms / 1000.0f)) * 65536.0f);
}

// tone: frequency glide f0->f1, amplitude (0..1), start, duration (samples), attack / decay (ms), 2nd harmonic level
static void tone(float f0, float f1, float amp, int start, int dur, float attack_ms, float decay_ms, float harm2) {
    if (!s_mix || dur <= 0) return;
    uint32_t phase = 0;
    int32_t step0 = (int32_t)(f0 * 4294967296.0f / (float)SFX_RATE / 4.0f); // phase step / 4 (avoid overflow)
    int32_t step1 = (int32_t)(f1 * 4294967296.0f / (float)SFX_RATE / 4.0f);
    int att = (int)((float)SFX_RATE * attack_ms / 1000.0f);
    if (att < 1) att = 1;
    uint32_t dq = decay_q16(decay_ms);
    uint32_t env = 65536;                                // Q16
    int32_t a = (int32_t)(amp * 32767.0f);               // Q15
    int32_t h2 = (int32_t)(harm2 * 32767.0f);
    int64_t dstep = ((int64_t)(step1 - step0) * 65536) / dur;   // glide per sample (Q16)
    for (int i = 0; i < dur && start + i < s_mix_len; i++) {
        int32_t st = step0 + (int32_t)((dstep * i) >> 16);
        phase += (uint32_t)st * 4u;
        int32_t v = s_sine[phase >> 22];
        if (h2) v += (s_sine[(phase * 2u) >> 22] * h2) >> 15;
        int32_t att_q15 = (i < att) ? (i * 32767) / att : 32767;
        int32_t e = (int32_t)((env * (uint32_t)att_q15) >> 16);     // Q15
        int32_t sample = (int32_t)(((int64_t)v * a >> 15) * e >> 15);
        s_mix[start + i] += sample;
        env = (uint32_t)(((uint64_t)env * dq) >> 16);
    }
}

static void noise(float amp, int start, int dur, float decay_ms, float lowpass) {
    if (!s_mix) return;
    uint32_t seed = 0x1234567u;
    int32_t y = 0, lp = (int32_t)(lowpass * 32767.0f), a = (int32_t)(amp * 32767.0f);
    uint32_t env = 65536, dq = decay_q16(decay_ms);
    for (int i = 0; i < dur && start + i < s_mix_len; i++) {
        seed = seed * 1664525u + 1013904223u;
        int32_t x = (int32_t)(seed >> 16) - 32768;
        y += ((x - y) * lp) >> 15;
        s_mix[start + i] += (int32_t)((((int64_t)y * a) >> 15) * (int32_t)(env >> 1) >> 15);
        env = (uint32_t)(((uint64_t)env * dq) >> 16);
    }
}

static Pcm mix_end(float gain) {
    Pcm p = { NULL, 0, SFX_RATE };
    if (!s_mix) return p;
    int len = (s_mix_len + 1) & ~1;
    p.data = (int16_t *)calloc((size_t)len, sizeof(int16_t));
    int32_t g = (int32_t)(gain * 30000.0f / 32767.0f * 1024.0f); // Q10
    int fade = (SFX_RATE * 4) / 1000;
    if (p.data) {
        for (int i = 0; i < s_mix_len; i++) {
            int32_t v = (s_mix[i] * g) >> 10;
            if (i > s_mix_len - fade) v = v * (s_mix_len - i) / fade;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            p.data[i] = (int16_t)v;
        }
        p.len = len;
    }
    free(s_mix);
    s_mix = NULL;
    return p;
}

static void build_hyper(SfxEvent ev) {
    switch (ev) {
    case SFX_TICK: {
        // Navigation tick: crisp, very short wooden click
        mix_begin(MS(28));
        noise(0.35f, 0, MS(3), 0.6f, 0.6f);
        tone(2350, 2250, 0.55f, 0, MS(28), 0.3f, 5.0f, 0.15f);
        s_hyper[SFX_TICK] = mix_end(0.9f);
        break;
    }
    case SFX_DECIDE: {
        // Decide: two rising soft notes
        mix_begin(MS(110));
        tone(1318, 1318, 0.45f, 0, MS(60), 1.0f, 22.0f, 0.25f);
        tone(1976, 1976, 0.45f, MS(38), MS(72), 1.0f, 26.0f, 0.2f);
        s_hyper[SFX_DECIDE] = mix_end(0.9f);
        break;
    }
    case SFX_BACK: {
        // Back: falling pair
        mix_begin(MS(100));
        tone(1480, 1480, 0.4f, 0, MS(55), 1.0f, 20.0f, 0.2f);
        tone(988, 988, 0.4f, MS(35), MS(65), 1.0f, 24.0f, 0.2f);
        s_hyper[SFX_BACK] = mix_end(0.85f);
        break;
    }
    case SFX_LAUNCH: {
        // Launch: bright arpeggio + airy swell
        mix_begin(MS(520));
        tone(1046, 1046, 0.30f, 0, MS(300), 2.0f, 110.0f, 0.3f);
        tone(1318, 1318, 0.30f, MS(70), MS(300), 2.0f, 110.0f, 0.3f);
        tone(1568, 1568, 0.30f, MS(140), MS(330), 2.0f, 120.0f, 0.3f);
        tone(2093, 2093, 0.28f, MS(210), MS(310), 2.0f, 130.0f, 0.2f);
        noise(0.10f, MS(150), MS(360), 120.0f, 0.08f);
        s_hyper[SFX_LAUNCH] = mix_end(0.95f);
        break;
    }
    case SFX_MODAL: {
        // Popup: soft bubble sweeping upwards
        mix_begin(MS(90));
        tone(520, 1180, 0.55f, 0, MS(90), 2.0f, 30.0f, 0.1f);
        s_hyper[SFX_MODAL] = mix_end(0.85f);
        break;
    }
    case SFX_TOGGLE: {
        // Toggle: two clicks
        mix_begin(MS(70));
        tone(1760, 1760, 0.40f, 0, MS(30), 0.5f, 8.0f, 0.2f);
        tone(2637, 2637, 0.40f, MS(28), MS(40), 0.5f, 10.0f, 0.2f);
        s_hyper[SFX_TOGGLE] = mix_end(0.85f);
        break;
    }
    case SFX_PAGE: {
        // Page: short airy swoosh with a tick
        mix_begin(MS(80));
        noise(0.45f, 0, MS(80), 25.0f, 0.15f);
        tone(1900, 2400, 0.25f, 0, MS(40), 0.5f, 10.0f, 0.0f);
        s_hyper[SFX_PAGE] = mix_end(0.8f);
        break;
    }
    case SFX_ERROR: {
        // Error: low double bonk
        mix_begin(MS(190));
        tone(233, 220, 0.6f, 0, MS(90), 1.0f, 35.0f, 0.5f);
        tone(220, 196, 0.6f, MS(95), MS(95), 1.0f, 40.0f, 0.5f);
        s_hyper[SFX_ERROR] = mix_end(0.9f);
        break;
    }
    case SFX_INSERT: {
        // Insert: clack + rising chime
        mix_begin(MS(260));
        noise(0.6f, 0, MS(10), 2.0f, 0.5f);
        tone(880, 880, 0.3f, MS(30), MS(140), 1.0f, 60.0f, 0.3f);
        tone(1760, 1760, 0.3f, MS(90), MS(170), 1.0f, 70.0f, 0.2f);
        s_hyper[SFX_INSERT] = mix_end(0.9f);
        break;
    }
    case SFX_EJECT: {
        // Eject: clack + falling chime
        mix_begin(MS(220));
        noise(0.6f, 0, MS(10), 2.0f, 0.5f);
        tone(1320, 660, 0.35f, MS(20), MS(200), 1.0f, 60.0f, 0.2f);
        s_hyper[SFX_EJECT] = mix_end(0.9f);
        break;
    }
    case SFX_FAVORITE: {
        // Favorite: sparkle
        mix_begin(MS(200));
        tone(2093, 2093, 0.30f, 0, MS(120), 0.5f, 40.0f, 0.1f);
        tone(2637, 2637, 0.30f, MS(50), MS(120), 0.5f, 45.0f, 0.1f);
        tone(3136, 3136, 0.25f, MS(100), MS(100), 0.5f, 45.0f, 0.1f);
        s_hyper[SFX_FAVORITE] = mix_end(0.85f);
        break;
    }
    case SFX_STARTUP: {
        // Startup: warm chord swell
        mix_begin(MS(900));
        tone(523, 523, 0.18f, 0, MS(900), 60.0f, 380.0f, 0.4f);
        tone(659, 659, 0.16f, MS(60), MS(840), 60.0f, 380.0f, 0.4f);
        tone(784, 784, 0.16f, MS(120), MS(780), 60.0f, 380.0f, 0.4f);
        tone(1046, 1046, 0.14f, MS(180), MS(720), 60.0f, 400.0f, 0.3f);
        tone(1568, 1568, 0.10f, MS(260), MS(640), 40.0f, 300.0f, 0.2f);
        s_hyper[SFX_STARTUP] = mix_end(0.95f);
        break;
    }
    case SFX_KEY: {
        // Keyboard key: softer tick
        mix_begin(MS(22));
        noise(0.25f, 0, MS(2), 0.5f, 0.6f);
        tone(1900, 1850, 0.4f, 0, MS(22), 0.3f, 4.0f, 0.1f);
        s_hyper[SFX_KEY] = mix_end(0.8f);
        break;
    }
    case SFX_BOOT: {
        // Boot "Snap": the two halves click together, then a light sparkle
        mix_begin(MS(1420));
        noise(0.85f, MS(520), MS(40), 6.0f, 0.75f);
        tone(2100, 2000, 0.25f, MS(520), MS(60), 0.2f, 12.0f, 0.0f);
        tone(980, 900, 0.45f, MS(540), MS(90), 0.5f, 28.0f, 0.3f);
        tone(1568, 1568, 0.22f, MS(1000), MS(400), 2.0f, 150.0f, 0.2f);
        tone(2093, 2093, 0.16f, MS(1060), MS(340), 2.0f, 130.0f, 0.1f);
        s_hyper[SFX_BOOT] = mix_end(0.95f);
        break;
    }
    case SFX_LID_CLOSE: {
        // Lid closed: a soft "tick-thud"
        mix_begin(MS(170));
        noise(0.45f, 0, MS(20), 3.0f, 0.6f);
        tone(2200, 2000, 0.18f, 0, MS(30), 0.2f, 7.0f, 0.0f);
        tone(210, 140, 0.60f, MS(18), MS(150), 1.0f, 45.0f, 0.45f);
        s_hyper[SFX_LID_CLOSE] = mix_end(0.9f);
        break;
    }
    case SFX_LID_OPEN: {
        // Lid opened: two soft notes going up
        mix_begin(MS(340));
        tone(988, 988, 0.30f, 0, MS(160), 6.0f, 70.0f, 0.25f);
        tone(1480, 1480, 0.30f, MS(80), MS(260), 6.0f, 110.0f, 0.2f);
        s_hyper[SFX_LID_OPEN] = mix_end(0.9f);
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------
// WAV loader for user packs
// ---------------------------------------------------------------------------
#define USER_MAX_SAMPLES (32000 * 3)   // 3 s at 32 kHz, 6 s at 16 kHz (Studio writes 16 kHz)

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static bool load_wav(const char *path, Pcm *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t riff[12];
    if (fread(riff, 1, 12, f) != 12 || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) { fclose(f); return false; }
    uint16_t tag = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    bool have_fmt = false;
    uint8_t ck[8];
    while (fread(ck, 1, 8, f) == 8) {
        uint32_t sz = rd32(ck + 4);
        if (!memcmp(ck, "fmt ", 4)) {
            uint8_t fmt[16];
            if (sz < 16 || fread(fmt, 1, 16, f) != 16) break;
            tag = rd16(fmt); ch = rd16(fmt + 2); rate = rd32(fmt + 4); bits = rd16(fmt + 14);
            have_fmt = true;
            if (sz > 16) fseek(f, (long)(sz - 16 + (sz & 1)), SEEK_CUR);
        } else if (!memcmp(ck, "data", 4)) {
            if (!have_fmt || tag != 1 || ch < 1 || ch > 2 || (bits != 8 && bits != 16) || rate < 4000 || rate > 48000) break;
            uint32_t fb = ch * (bits / 8);
            uint32_t frames = sz / fb;
            if (frames > USER_MAX_SAMPLES) frames = USER_MAX_SAMPLES;
            if (frames < 16) break;
            uint32_t alen = (frames + 1) & ~1u;
            int16_t *pcm = (int16_t *)calloc(alen, sizeof(int16_t));
            uint8_t *raw = (uint8_t *)malloc(frames * fb);
            if (!pcm || !raw) { free(pcm); free(raw); break; }
            uint32_t got = (uint32_t)fread(raw, 1, frames * fb, f) / fb;
            for (uint32_t i = 0; i < got; i++) {
                int32_t acc = 0;
                for (uint32_t c = 0; c < ch; c++) {
                    const uint8_t *s = raw + i * fb + c * (bits / 8);
                    acc += (bits == 16) ? (int16_t)rd16(s) : ((int32_t)s[0] - 128) << 8;
                }
                pcm[i] = (int16_t)(acc / (int32_t)ch);
            }
            free(raw);
            fclose(f);
            out->data = pcm; out->len = (int)alen; out->rate = rate;
            return got > 0;
        } else {
            fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
        }
    }
    fclose(f);
    return false;
}

int sound_load_pack(const char *dir) {
    int n = 0;
    for (int i = 0; i < SFX_COUNT; i++) {
        if (s_user[i].data) { free(s_user[i].data); s_user[i].data = NULL; }
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", dir, s_files[i]);
        if (load_wav(path, &s_user[i])) n++;
    }
    return n;
}

static Pcm s_boot;

bool sound_load_boot(const char *path) {
    if (s_boot.data) { free(s_boot.data); s_boot.data = NULL; s_boot.len = 0; }
    return path && load_wav(path, &s_boot);
}

bool sound_play_boot(void) {
    if (!s_ready || !s_boot.data || s_boot.len <= 0) return false;
    if (s_volume == 0) return true;
#ifdef ARM9
    DC_FlushRange(s_boot.data, (u32)s_boot.len * sizeof(int16_t));
    int vol = s_volume * 25 + (s_volume == 5 ? 2 : 0);
    soundPlaySample(s_boot.data, SoundFormat_16Bit, (u32)s_boot.len * sizeof(int16_t), (u16)s_boot.rate, (u8)vol, 64, false, 0);
#endif
    return true;
}

// ---------------------------------------------------------------------------
void sound_init(void) {
    if (s_ready) return;
    sine_init();   // sounds are synthesized on first use (keeps the boot fast)
#ifdef ARM9
    soundEnable();
#endif
    s_ready = true;
}

void sound_set_volume(int level) {
    if (level < 0) level = 0;
    if (level > 5) level = 5;
    s_volume = level;
}

// Synthesizes a built-in sound the first time it is needed
static void ensure_built(SfxEvent ev, int pack) {
    if ((unsigned)ev >= SFX_COUNT) return;
    sine_init();
    (void)pack;                 // one built-in pack (the classic one is a download in DSplay Studio)
    if (!s_hyper_built[ev]) {
        build_hyper(ev);
        s_hyper_built[ev] = true;
    }
}

void sound_prepare(SfxEvent ev, int pack) {
    if (pack == SFX_PACK_MUTE) return;
    ensure_built(ev, pack);
}

int sound_builtin(SfxEvent ev, int pack, const int16_t **data) {
    if ((unsigned)ev >= SFX_COUNT) return 0;
    ensure_built(ev, pack);
    const Pcm *p = &s_hyper[ev];
    if (data) *data = p->data;
    return p->len;
}

void sound_play(SfxEvent ev, int pack) {
    if (!s_ready || pack == SFX_PACK_MUTE || s_volume == 0 || (unsigned)ev >= SFX_COUNT) return;
    const Pcm *p = &s_user[ev];
    if (!p->data) {                       // no custom sound for this event
        ensure_built(ev, pack);
        p = &s_hyper[ev];
    }
    if (!p->data || p->len <= 0) return;
#ifdef ARM9
    DC_FlushRange(p->data, (u32)p->len * sizeof(int16_t));
    int vol = s_volume * 25 + (s_volume == 5 ? 2 : 0);
    soundPlaySample(p->data, SoundFormat_16Bit, (u32)p->len * sizeof(int16_t), (u16)p->rate, (u8)vol, 64, false, 0);
#endif
}
