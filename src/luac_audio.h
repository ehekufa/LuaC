#ifndef LUAC_AUDIO_H
#define LUAC_AUDIO_H

/* Native interleaved-float PCM, WAV I/O, synthesis and optional miniaudio
   playback. DSP and WAV support never require an audio device or system SDK. */
#ifdef LUAC_WITH_MINIAUDIO
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "third_party/miniaudio.h"
#endif

static float lc_audio_clamp(float value) { return value < -1.0f ? -1.0f : value > 1.0f ? 1.0f : value; }
static void lc_audio_dispose(LcAudio *audio) {
#ifdef LUAC_WITH_MINIAUDIO
    if (audio->device) {
        ma_device *device = (ma_device *)audio->device;
        ma_device_uninit(device); free(device); audio->device = NULL;
    }
#else
    (void)audio;
#endif
}
static LcAudio *lc_audio_arg(LcContext *ctx, int argc, LcValue *argv, int at) {
    LcValue value = at < argc ? lc_first_value(argv[at]) : lc_nil();
    if (value.type != LC_AUDIO) { lc_error(ctx, "audio buffer expected"); return NULL; }
    return value.as.audio;
}
static double lc_audio_number_arg(LcContext *ctx, int argc, LcValue *argv, int at, double fallback) {
    int ok = 1;
    if (at >= argc || lc_first_value(argv[at]).type == LC_NIL) return fallback;
    return lc_to_number(ctx, argv[at], &ok);
}
static LcValue lc_audio_value(LcContext *ctx, size_t frames, int channels, int sample_rate) {
    LcAudio *audio;
    LcValue value;
    if (channels < 1 || channels > 64 || sample_rate < 1000 || sample_rate > 384000 ||
        frames > SIZE_MAX / (sizeof(float) * (size_t)channels)) {
        lc_error(ctx, "invalid audio buffer dimensions"); return lc_nil();
    }
    audio = (LcAudio *)calloc(1, sizeof(*audio));
    if (!audio) { lc_error(ctx, "out of memory"); return lc_nil(); }
    if (frames) {
        audio->samples = (float *)calloc(frames * (size_t)channels, sizeof(float));
        if (!audio->samples) { free(audio); lc_error(ctx, "out of memory"); return lc_nil(); }
    }
    audio->frames = frames; audio->channels = channels; audio->sample_rate = sample_rate;
    audio->object.refs = 1; audio->object.kind = LC_AUDIO;
    audio->object.next = ctx->objects; ctx->objects = (LcObject *)audio;
    value.type = LC_AUDIO; value.as.audio = audio;
    return value;
}
static LcValue lc_audio_silence(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    double seconds = lc_audio_number_arg(ctx, argc, argv, 0, 1.0);
    int rate = (int)lc_audio_number_arg(ctx, argc, argv, 1, 44100);
    int channels = (int)lc_audio_number_arg(ctx, argc, argv, 2, 1); (void)self;
    if (seconds < 0 || seconds > 86400) { lc_error(ctx, "audio duration must be between 0 and 86400 seconds"); return lc_nil(); }
    return lc_audio_value(ctx, (size_t)(seconds * rate + 0.5), channels, rate);
}
static LcValue lc_audio_tone(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    double frequency = lc_audio_number_arg(ctx, argc, argv, 0, 440.0);
    double seconds = lc_audio_number_arg(ctx, argc, argv, 1, 1.0);
    int rate = 44100, channels = 1, waveform = 0;
    double amplitude = 0.25, attack = 0.005, release = 0.02, pan = 0.0;
    const char *shape = "sine";
    size_t i, frames;
    unsigned state = 0x12345678u;
    LcValue value; LcAudio *audio;
    if (argc > 2 && lc_first_value(argv[2]).type == LC_TABLE) {
        LcValue option;
#define LC_AUDIO_OPTION_NUMBER(field, name, target) do { LcValue key = lc_string(ctx, name); option = lc_table_raw_get(ctx, lc_first_value(argv[2]), key); lc_release(ctx, key); if (option.type == LC_NUMBER) target = option.as.number; lc_release(ctx, option); } while (0)
        LC_AUDIO_OPTION_NUMBER(field, "sample_rate", rate);
        LC_AUDIO_OPTION_NUMBER(field, "channels", channels);
        LC_AUDIO_OPTION_NUMBER(field, "amplitude", amplitude);
        LC_AUDIO_OPTION_NUMBER(field, "attack", attack);
        LC_AUDIO_OPTION_NUMBER(field, "release", release);
        LC_AUDIO_OPTION_NUMBER(field, "pan", pan);
        { LcValue key = lc_string(ctx, "waveform"); option = lc_table_raw_get(ctx, lc_first_value(argv[2]), key); lc_release(ctx, key); }
        if (option.type == LC_STRING) shape = option.as.string->data;
        if (!strcmp(shape, "square")) waveform = 1;
        else if (!strcmp(shape, "saw")) waveform = 2;
        else if (!strcmp(shape, "triangle")) waveform = 3;
        else if (!strcmp(shape, "noise")) waveform = 4;
        lc_release(ctx, option);
#undef LC_AUDIO_OPTION_NUMBER
    }
    if (frequency < 0 || seconds < 0 || seconds > 86400 || attack < 0 || release < 0) {
        lc_error(ctx, "invalid tone parameters"); return lc_nil();
    }
    frames = (size_t)(seconds * rate + 0.5);
    value = lc_audio_value(ctx, frames, channels, rate);
    if (value.type != LC_AUDIO) return value;
    audio = value.as.audio;
    for (i = 0; i < frames; ++i) {
        double time = (double)i / rate, phase = frequency * time, envelope = 1.0, sample;
        int channel;
        phase -= floor(phase);
        if (waveform == 1) sample = phase < 0.5 ? 1.0 : -1.0;
        else if (waveform == 2) sample = phase * 2.0 - 1.0;
        else if (waveform == 3) sample = 1.0 - 4.0 * fabs(phase - 0.5);
        else if (waveform == 4) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; sample = (state / 2147483648.0) - 1.0; }
        else sample = sin(phase * 6.28318530717958647692);
        if (attack > 0 && time < attack) envelope = time / attack;
        if (release > 0 && seconds - time < release) envelope *= (seconds - time) / release;
        if (envelope < 0) envelope = 0;
        sample *= amplitude * envelope;
        for (channel = 0; channel < channels; ++channel) {
            double gain = channels == 2 ? (channel == 0 ? (pan > 0 ? 1.0 - pan : 1.0) : (pan < 0 ? 1.0 + pan : 1.0)) : 1.0;
            audio->samples[i * channels + channel] = lc_audio_clamp((float)(sample * gain));
        }
    }
    return value;
}
static uint16_t lc_audio_u16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t lc_audio_u32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void lc_audio_put16(LcContext *ctx, LcLibBuffer *buffer, uint16_t value) { unsigned char p[2]; p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8); lc_lib_append(ctx, buffer, (char *)p, 2); }
static void lc_audio_put32(LcContext *ctx, LcLibBuffer *buffer, uint32_t value) { unsigned char p[4]; p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8); p[2] = (unsigned char)(value >> 16); p[3] = (unsigned char)(value >> 24); lc_lib_append(ctx, buffer, (char *)p, 4); }
static LcValue lc_audio_error(LcContext *ctx, const char *message) {
    LcValue out[2]; out[0] = lc_nil(); out[1] = lc_string(ctx, message); return lc_tuple_new(ctx, 2, out, 1);
}
static LcValue lc_audio_read_wav(LcContext *ctx, const unsigned char *data, size_t size) {
    size_t at = 12, data_at = 0, data_size = 0, bytes_per_sample, frames, i;
    int format = 0, channels = 0, rate = 0, bits = 0;
    LcValue value;
    if (size < 12 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4)) return lc_audio_error(ctx, "not a RIFF/WAVE file");
    while (at + 8 <= size) {
        uint32_t length = lc_audio_u32(data + at + 4);
        size_t next = at + 8 + (size_t)length + (length & 1u);
        if (at + 8 + length > size) return lc_audio_error(ctx, "truncated WAV chunk");
        if (!memcmp(data + at, "fmt ", 4) && length >= 16) {
            format = lc_audio_u16(data + at + 8); channels = lc_audio_u16(data + at + 10);
            rate = (int)lc_audio_u32(data + at + 12); bits = lc_audio_u16(data + at + 22);
            if (format == 0xfffe && length >= 40) format = (int)lc_audio_u16(data + at + 32);
        } else if (!memcmp(data + at, "data", 4)) { data_at = at + 8; data_size = length; }
        if (next <= at) break; at = next;
    }
    if (!data_at || !channels || !rate || ((format != 1 && format != 3) || (format == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32) || (format == 3 && bits != 32)))
        return lc_audio_error(ctx, "unsupported WAV: use PCM 8/16/24/32-bit or float32");
    bytes_per_sample = (size_t)bits / 8;
    frames = data_size / (bytes_per_sample * (size_t)channels);
    value = lc_audio_value(ctx, frames, channels, rate);
    if (value.type != LC_AUDIO) return value;
    for (i = 0; i < frames * (size_t)channels; ++i) {
        const unsigned char *p = data + data_at + i * bytes_per_sample;
        float sample;
        if (format == 3) { uint32_t bits_value = lc_audio_u32(p); memcpy(&sample, &bits_value, 4); }
        else if (bits == 8) sample = ((int)p[0] - 128) / 128.0f;
        else if (bits == 16) sample = (int16_t)lc_audio_u16(p) / 32768.0f;
        else if (bits == 24) { int32_t n = (int32_t)(p[0] | p[1] << 8 | p[2] << 16); if (n & 0x800000) n |= (int32_t)0xff000000; sample = n / 8388608.0f; }
        else sample = (int32_t)lc_audio_u32(p) / 2147483648.0f;
        value.as.audio->samples[i] = lc_audio_clamp(sample);
    }
    return value;
}
static LcValue lc_audio_load(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *path = lc_lib_string_arg(ctx, argc, argv, 0); FILE *file; long length; unsigned char *data; LcValue result; (void)self;
    if (!path) return lc_nil();
    file = fopen(path->data, "rb"); if (!file) return lc_audio_error(ctx, "cannot open WAV file");
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) { fclose(file); return lc_audio_error(ctx, "cannot size WAV file"); }
    data = (unsigned char *)malloc((size_t)length + 1);
    if (!data) { fclose(file); return lc_audio_error(ctx, "out of memory"); }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) { free(data); fclose(file); return lc_audio_error(ctx, "cannot read WAV file"); }
    fclose(file); result = lc_audio_read_wav(ctx, data, (size_t)length); free(data); return result;
}
static LcValue lc_audio_save(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *audio = lc_audio_arg(ctx, argc, argv, 0);
    LcString *path = lc_lib_string_arg(ctx, argc, argv, 1);
    LcLibBuffer buffer = {0}; FILE *file; size_t i, samples, data_size; int floating = 0;
    if (!audio || !path) return lc_nil();
    if (argc > 2 && lc_first_value(argv[2]).type == LC_STRING && !strcmp(lc_first_value(argv[2]).as.string->data, "float")) floating = 1;
    samples = audio->frames * (size_t)audio->channels; data_size = samples * (floating ? 4 : 2);
    if (data_size > UINT32_MAX - 36) { lc_error(ctx, "WAV file exceeds RIFF size limit"); return lc_nil(); }
    lc_lib_append(ctx, &buffer, "RIFF", 4); lc_audio_put32(ctx, &buffer, (uint32_t)(36 + data_size));
    lc_lib_append(ctx, &buffer, "WAVEfmt ", 8); lc_audio_put32(ctx, &buffer, 16);
    lc_audio_put16(ctx, &buffer, (uint16_t)(floating ? 3 : 1)); lc_audio_put16(ctx, &buffer, (uint16_t)audio->channels);
    lc_audio_put32(ctx, &buffer, (uint32_t)audio->sample_rate); lc_audio_put32(ctx, &buffer, (uint32_t)(audio->sample_rate * audio->channels * (floating ? 4 : 2)));
    lc_audio_put16(ctx, &buffer, (uint16_t)(audio->channels * (floating ? 4 : 2))); lc_audio_put16(ctx, &buffer, (uint16_t)(floating ? 32 : 16));
    lc_lib_append(ctx, &buffer, "data", 4); lc_audio_put32(ctx, &buffer, (uint32_t)data_size);
    for (i = 0; i < samples; ++i) {
        if (floating) { uint32_t bits; float sample = audio->samples[i]; memcpy(&bits, &sample, 4); lc_audio_put32(ctx, &buffer, bits); }
        else { double sample = audio->samples[i] * 32767.0; lc_audio_put16(ctx, &buffer, (uint16_t)(int16_t)(sample < -32768 ? -32768 : sample > 32767 ? 32767 : sample)); }
    }
    file = fopen(path->data, "wb");
    if (!file) { free(buffer.data); return lc_audio_error(ctx, "cannot create WAV file"); }
    { size_t written = fwrite(buffer.data, 1, buffer.length, file); int close_status = fclose(file);
      if (written != buffer.length || close_status != 0) { free(buffer.data); return lc_audio_error(ctx, "cannot write WAV file"); } }
    free(buffer.data); return lc_bool(1);
}
static LcValue lc_audio_info(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *audio = lc_audio_arg(ctx, argc, argv, 0); LcValue result; (void)self; if (!audio) return lc_nil();
    result = lc_table(ctx); lc_set_field(ctx, result, "frames", lc_number((double)audio->frames));
    lc_set_field(ctx, result, "channels", lc_number(audio->channels)); lc_set_field(ctx, result, "sample_rate", lc_number(audio->sample_rate));
    lc_set_field(ctx, result, "duration", lc_number((double)audio->frames / audio->sample_rate)); return result;
}
static LcValue lc_audio_frames(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) { LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); (void)self; return a ? lc_number((double)a->frames) : lc_nil(); }
static LcValue lc_audio_channels(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) { LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); (void)self; return a ? lc_number(a->channels) : lc_nil(); }
static LcValue lc_audio_rate(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) { LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); (void)self; return a ? lc_number(a->sample_rate) : lc_nil(); }
static LcValue lc_audio_duration(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) { LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); (void)self; return a ? lc_number((double)a->frames / a->sample_rate) : lc_nil(); }
static LcValue lc_audio_get(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); int64_t frame, channel; (void)self; if (!a) return lc_nil();
    frame = lc_to_integer(ctx, argc > 1 ? argv[1] : lc_nil()); channel = lc_to_integer(ctx, argc > 2 ? argv[2] : lc_number(1));
    if (frame < 1 || frame > (int64_t)a->frames || channel < 1 || channel > a->channels) { lc_error(ctx, "audio sample index out of range"); return lc_nil(); }
    return lc_number(a->samples[((size_t)frame - 1) * a->channels + channel - 1]);
}
static LcValue lc_audio_set(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); int64_t frame, channel; double sample; int ok = 1; (void)self; if (!a) return lc_nil();
    frame = lc_to_integer(ctx, argc > 1 ? argv[1] : lc_nil()); channel = lc_to_integer(ctx, argc > 2 ? argv[2] : lc_number(1));
    sample = lc_to_number(ctx, argc > 3 ? argv[3] : lc_nil(), &ok);
    if (frame < 1 || frame > (int64_t)a->frames || channel < 1 || channel > a->channels) { lc_error(ctx, "audio sample index out of range"); return lc_nil(); }
    a->samples[((size_t)frame - 1) * a->channels + channel - 1] = lc_audio_clamp((float)sample); return lc_bool(1);
}
static LcValue lc_audio_gain(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); double gain = lc_audio_number_arg(ctx, argc, argv, 1, 1.0); size_t i, n; (void)self; if (!a) return lc_nil();
    n = a->frames * (size_t)a->channels; for (i = 0; i < n; ++i) a->samples[i] = lc_audio_clamp(a->samples[i] * (float)gain); return lc_retain(argv[0]);
}
static LcValue lc_audio_fade(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); double in = lc_audio_number_arg(ctx, argc, argv, 1, 0.01), out = lc_audio_number_arg(ctx, argc, argv, 2, in); size_t i; (void)self; if (!a) return lc_nil();
    if (in < 0 || out < 0) { lc_error(ctx, "fade duration cannot be negative"); return lc_nil(); }
    for (i = 0; i < a->frames; ++i) {
        double time = (double)i / a->sample_rate, left = (double)(a->frames - 1 - i) / a->sample_rate, gain = 1.0; int c;
        if (in > 0 && time < in) gain = time / in; if (out > 0 && left < out) gain *= left / out;
        for (c = 0; c < a->channels; ++c) a->samples[i * a->channels + c] = (float)(a->samples[i * a->channels + c] * gain);
    }
    return lc_retain(argv[0]);
}
static LcValue lc_audio_normalize(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); double peak = lc_audio_number_arg(ctx, argc, argv, 1, 0.95), maximum = 0; size_t i, n; (void)self; if (!a) return lc_nil();
    n = a->frames * (size_t)a->channels; for (i = 0; i < n; ++i) if (fabs(a->samples[i]) > maximum) maximum = fabs(a->samples[i]);
    if (maximum > 0 && peak >= 0) for (i = 0; i < n; ++i) a->samples[i] = lc_audio_clamp((float)(a->samples[i] * peak / maximum));
    return lc_retain(argv[0]);
}
static LcValue lc_audio_mix(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *first = lc_audio_arg(ctx, argc, argv, 0); size_t frames = 0, i; int at; LcValue value; LcAudio *output; (void)self; if (!first) return lc_nil();
    for (at = 0; at < argc; ++at) { LcAudio *a = lc_audio_arg(ctx, argc, argv, at); if (!a) return lc_nil(); if (a->channels != first->channels || a->sample_rate != first->sample_rate) { lc_error(ctx, "mix requires identical channel counts and sample rates"); return lc_nil(); } if (a->frames > frames) frames = a->frames; }
    value = lc_audio_value(ctx, frames, first->channels, first->sample_rate); if (value.type != LC_AUDIO) return value; output = value.as.audio;
    for (at = 0; at < argc; ++at) { LcAudio *a = lc_first_value(argv[at]).as.audio; for (i = 0; i < a->frames * (size_t)a->channels; ++i) output->samples[i] = lc_audio_clamp(output->samples[i] + a->samples[i]); }
    return value;
}
static LcValue lc_audio_filter(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); double cutoff = lc_audio_number_arg(ctx, argc, argv, 1, 1000.0), alpha; size_t i; int c; int high; float *previous; (void)self;
    if (!a) return lc_nil(); high = !strcmp(lc_lib_name(self), "highpass"); if (cutoff <= 0 || cutoff >= a->sample_rate / 2.0) { lc_error(ctx, "filter cutoff must be between 0 and Nyquist"); return lc_nil(); }
    alpha = 1.0 - exp(-2.0 * 3.14159265358979323846 * cutoff / a->sample_rate); previous = (float *)calloc((size_t)a->channels, sizeof(float)); if (!previous) return lc_audio_error(ctx, "out of memory");
    for (i = 0; i < a->frames; ++i) for (c = 0; c < a->channels; ++c) { size_t at = i * a->channels + c; float input = a->samples[at]; previous[c] += (float)alpha * (input - previous[c]); a->samples[at] = high ? input - previous[c] : previous[c]; }
    free(previous); return lc_retain(argv[0]);
}
static LcValue lc_audio_delay(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); double seconds = lc_audio_number_arg(ctx, argc, argv, 1, 0.2), feedback = lc_audio_number_arg(ctx, argc, argv, 2, 0.35), wet = lc_audio_number_arg(ctx, argc, argv, 3, 0.3); size_t i, offset; (void)self;
    if (!a) return lc_nil(); if (seconds <= 0 || feedback < 0 || feedback >= 1 || wet < 0) { lc_error(ctx, "invalid delay parameters"); return lc_nil(); } offset = (size_t)(seconds * a->sample_rate);
    for (i = offset; i < a->frames; ++i) { int c; for (c = 0; c < a->channels; ++c) { size_t at = i * a->channels + c, earlier = (i - offset) * a->channels + c; a->samples[at] = lc_audio_clamp((float)(a->samples[at] + wet * a->samples[earlier] * (1.0 + feedback))); } }
    return lc_retain(argv[0]);
}
static LcValue lc_audio_resample(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); int rate = (int)lc_audio_number_arg(ctx, argc, argv, 1, a ? a->sample_rate : 44100); size_t frames, i; LcValue value; (void)self; if (!a) return lc_nil();
    if (rate < 1000 || rate > 384000) { lc_error(ctx, "invalid sample rate"); return lc_nil(); } frames = (size_t)((double)a->frames * rate / a->sample_rate + 0.5);
    value = lc_audio_value(ctx, frames, a->channels, rate); if (value.type != LC_AUDIO) return value;
    for (i = 0; i < frames; ++i) { double position = (double)i * a->sample_rate / rate; size_t left = (size_t)position, right = left + 1 < a->frames ? left + 1 : left; double fraction = position - left; int c; if (left >= a->frames) left = right = a->frames ? a->frames - 1 : 0; for (c = 0; c < a->channels && a->frames; ++c) value.as.audio->samples[i * a->channels + c] = (float)(a->samples[left * a->channels + c] * (1.0 - fraction) + a->samples[right * a->channels + c] * fraction); }
    return value;
}
#ifdef LUAC_WITH_MINIAUDIO
static void lc_audio_playback_callback(ma_device *device, void *output, const void *input, ma_uint32 frames) {
    LcAudio *audio = (LcAudio *)device->pUserData; float *out = (float *)output; size_t available, count; (void)input;
    available = audio->cursor < audio->frames ? audio->frames - audio->cursor : 0; count = frames < available ? frames : available;
    if (count) memcpy(out, audio->samples + audio->cursor * audio->channels, count * audio->channels * sizeof(float));
    if (count < frames) memset(out + count * audio->channels, 0, (frames - count) * audio->channels * sizeof(float));
    audio->cursor += count;
}
#endif
static LcValue lc_audio_play(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); (void)self; if (!a) return lc_nil();
#ifdef LUAC_WITH_MINIAUDIO
    ma_device_config config; ma_device *device; ma_result result;
    if (a->device) return lc_bool(1);
    device = (ma_device *)calloc(1, sizeof(*device)); if (!device) return lc_audio_error(ctx, "out of memory");
    config = ma_device_config_init(ma_device_type_playback); config.playback.format = ma_format_f32; config.playback.channels = a->channels;
    config.sampleRate = a->sample_rate; config.dataCallback = lc_audio_playback_callback; config.pUserData = a;
    result = ma_device_init(NULL, &config, device); if (result != MA_SUCCESS) { free(device); return lc_audio_error(ctx, "cannot open default playback device"); }
    a->device = device; a->cursor = 0; result = ma_device_start(device);
    if (result != MA_SUCCESS) { ma_device_uninit(device); free(device); a->device = NULL; return lc_audio_error(ctx, "cannot start playback device"); }
    return lc_bool(1);
#else
    (void)ctx; return lc_audio_error(ctx, "playback is disabled: rebuild with LUAC_WITH_MINIAUDIO");
#endif
}
static LcValue lc_audio_stop(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) { LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); (void)self; if (!a) return lc_nil(); lc_audio_dispose(a); if (a) a->cursor = 0; return lc_bool(1); }
static LcValue lc_audio_wait(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcAudio *a = lc_audio_arg(ctx, argc, argv, 0); (void)self; if (!a) return lc_nil();
#ifdef LUAC_WITH_MINIAUDIO
    while (a->device && a->cursor < a->frames) ma_sleep(10);
    lc_audio_dispose(a); if (a) a->cursor = 0;
#endif
    return lc_bool(1);
}
static LcValue lc_audio_playback_available(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    (void)ctx; (void)self; (void)argc; (void)argv;
#ifdef LUAC_WITH_MINIAUDIO
    return lc_bool(1);
#else
    return lc_bool(0);
#endif
}
static void lc_audio_init(LcContext *ctx) {
    LcValue audio = lc_table(ctx), methods = lc_table(ctx);
    lc_add_native(ctx, audio, "silence", lc_audio_silence); lc_add_native(ctx, audio, "tone", lc_audio_tone);
    lc_add_native(ctx, audio, "load", lc_audio_load); lc_add_native(ctx, audio, "mix", lc_audio_mix);
    lc_add_native(ctx, audio, "playback_available", lc_audio_playback_available);
#define LC_AUDIO_METHOD(name, function) lc_add_native(ctx, audio, name, function); lc_add_native(ctx, methods, name, function)
    LC_AUDIO_METHOD("save", lc_audio_save); LC_AUDIO_METHOD("info", lc_audio_info); LC_AUDIO_METHOD("frames", lc_audio_frames);
    LC_AUDIO_METHOD("channels", lc_audio_channels); LC_AUDIO_METHOD("sample_rate", lc_audio_rate); LC_AUDIO_METHOD("duration", lc_audio_duration);
    LC_AUDIO_METHOD("get", lc_audio_get); LC_AUDIO_METHOD("set", lc_audio_set); LC_AUDIO_METHOD("gain", lc_audio_gain);
    LC_AUDIO_METHOD("fade", lc_audio_fade); LC_AUDIO_METHOD("normalize", lc_audio_normalize);
    LC_AUDIO_METHOD("lowpass", lc_audio_filter); LC_AUDIO_METHOD("highpass", lc_audio_filter); LC_AUDIO_METHOD("delay", lc_audio_delay);
    LC_AUDIO_METHOD("resample", lc_audio_resample); LC_AUDIO_METHOD("play", lc_audio_play); LC_AUDIO_METHOD("stop", lc_audio_stop); LC_AUDIO_METHOD("wait", lc_audio_wait);
#undef LC_AUDIO_METHOD
    ctx->audio_methods = lc_retain(methods); lc_lib_register(ctx, "audio", audio); lc_release(ctx, methods);
}
#endif
