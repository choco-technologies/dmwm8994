#include "dmod.h"
#include "dmwm8994.h"
#include "dmsai_ioctl.h"
#include <stdint.h>

#define MELODY_SAMPLE_RATE 48000U
#define MELODY_QUARTER_FRAMES 18000U
#define MELODY_CHUNK_FRAMES 256U
#define MELODY_FADE_FRAMES 256U

typedef enum {
    note_c4, note_d4, note_e4, note_f4, note_g4, note_a4
} melody_pitch_t;

typedef struct {
    uint8_t pitch;
    uint8_t beats;
} melody_note_t;

/* Phase increments for equal-tempered notes C4 to A4 at 48 kHz. */
static const uint32_t phase_steps[] = {
    23409859U, 26276679U, 29494575U,
    31248413U, 35075158U, 39370534U,
};

/* Public-domain melody: Twinkle Twinkle Little Star. */
static const melody_note_t song[] = {
    {note_c4, 1}, {note_c4, 1}, {note_g4, 1}, {note_g4, 1},
    {note_a4, 1}, {note_a4, 1}, {note_g4, 2},
    {note_f4, 1}, {note_f4, 1}, {note_e4, 1}, {note_e4, 1},
    {note_d4, 1}, {note_d4, 1}, {note_c4, 2},
    {note_g4, 1}, {note_g4, 1}, {note_f4, 1}, {note_f4, 1},
    {note_e4, 1}, {note_e4, 1}, {note_d4, 2},
    {note_g4, 1}, {note_g4, 1}, {note_f4, 1}, {note_f4, 1},
    {note_e4, 1}, {note_e4, 1}, {note_d4, 2},
    {note_c4, 1}, {note_c4, 1}, {note_g4, 1}, {note_g4, 1},
    {note_a4, 1}, {note_a4, 1}, {note_g4, 2},
    {note_f4, 1}, {note_f4, 1}, {note_e4, 1}, {note_e4, 1},
    {note_d4, 1}, {note_d4, 1}, {note_c4, 2},
};

/* Static storage avoids a PCM buffer on the module's task stack. */
static int16_t pcm[MELODY_CHUNK_FRAMES * 2U];

/** @brief Render a short stereo triangle-wave chunk with quiet note edges. */
static void render_chunk(uint32_t step, uint32_t *phase, uint32_t position,
                         uint32_t duration, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; ++i)
    {
        *phase += step;
        uint32_t angle = *phase >> 16;
        int32_t triangle = angle < 32768U ? (int32_t)angle
                                           : 65535 - (int32_t)angle;
        int32_t sample = (2 * triangle - 32767) / 4;
        uint32_t edge = position + i;
        uint32_t tail = duration - 1U - edge;
        if (edge > tail) edge = tail;
        if (edge < MELODY_FADE_FRAMES)
            sample = sample * (int32_t)edge / (int32_t)MELODY_FADE_FRAMES;
        pcm[2U * i] = (int16_t)sample;
        pcm[2U * i + 1U] = (int16_t)sample;
    }
}

/** @brief Send every byte of one generated chunk through the codec node. */
static int write_chunk(void *codec, uint32_t frames)
{
    const uint8_t *data = (const uint8_t *)pcm;
    size_t length = (size_t)frames * 2U * sizeof(int16_t);
    size_t sent = 0;
    while (sent < length)
    {
        size_t wrote = Dmod_FileWrite(data + sent, 1, length - sent, codec);
        if (!wrote || wrote > length - sent) return -1;
        sent += wrote;
    }
    return 0;
}

/** @brief Play one note at 160 quarter notes per minute. */
static int play_note(void *codec, melody_note_t note, uint32_t *phase)
{
    uint32_t duration = (uint32_t)note.beats * MELODY_QUARTER_FRAMES;
    uint32_t step = phase_steps[note.pitch];
    for (uint32_t position = 0; position < duration;)
    {
        uint32_t frames = duration - position;
        if (frames > MELODY_CHUNK_FRAMES) frames = MELODY_CHUNK_FRAMES;
        render_chunk(step, phase, position, duration, frames);
        if (write_chunk(codec, frames)) return -1;
        position += frames;
    }
    return 0;
}

/** @brief Repeat one melody and verify that the SAI DMA stream stays healthy. */
static int play_song(void *codec, uint32_t *phase)
{
    for (size_t i = 0; i < sizeof(song) / sizeof(song[0]); ++i)
        if (play_note(codec, song[i], phase)) return -1;
    dmsai_status_t status;
    int ret = Dmod_Ioctl(codec, DMSAI_IOCTL_GET_STATUS, &status);
    if (ret || status.transfer_errors) return -2;
    return 0;
}

/** @brief Configure WM8994 and play the melody until an error or board reset. */
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        Dmod_Printf("Usage: wm8994melody CODEC_DEVICE\n");
        return 1;
    }
    void *codec = Dmod_FileOpen(argv[1], "r+");
    if (!codec) return 1;
    dmdrvi_audio_config_t config = {
        .sample_rate_hz = MELODY_SAMPLE_RATE, .channels = 2,
        .sample_bits = 16, .output = DMDRVI_AUDIO_OUTPUT_HEADPHONE,
        .volume_percent = 60, .muted = true,
    };
    int ret = Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_CONFIGURE, &config);
    uint32_t timeout_ms = 1000;
    if (!ret) ret = Dmod_Ioctl(codec, DMSAI_IOCTL_SET_IO_TIMEOUT, &timeout_ms);
    bool muted = false;
    if (!ret) ret = Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_SET_MUTE, &muted);
    Dmod_Printf("WM8994 MELODY: %s\n", ret ? "setup failed" : "playing in a loop");
    uint32_t phase = 0;
    uint32_t loops = 0;
    while (!ret)
    {
        ret = play_song(codec, &phase);
        if (!ret) Dmod_Printf("WM8994 MELODY: loop %lu OK\n",
                              (unsigned long)++loops);
    }
    muted = true;
    Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_SET_MUTE, &muted);
    Dmod_FileClose(codec);
    Dmod_Printf("WM8994 MELODY: stopped (%d)\n", ret);
    return 1;
}
