/*
 * xrick/system/syssnd_psx.c -- PlayStation: sound
 *
 * Sound effects: converted to SPU ADPCM by tools/mkassets.py ("sndbank.bin",
 * linked into the executable), uploaded to SPU RAM once at startup. There are
 * 23 effects and 24 hardware voices, so each effect owns one voice: playing
 * an effect again simply restarts its voice, no software mixing needed.
 *
 * Music: the six tunes are CD-DA audio tracks 2..7, played by the CD drive
 * straight into the SPU. Auto-pause tells us when a track has ended so that
 * looping tunes (the title music) can be restarted.
 *
 * PlayStation port 2026.
 */

#include "xrick/config.h"

#ifdef ENABLE_SOUND

#include "xrick/system/system.h"
#include "xrick/game.h"

#include <string.h>
#include <psxapi.h>
#include <psxetc.h>
#include <psxspu.h>
#include <psxcd.h>
#include <hwregs_c.h>

const U8 syssnd_period = 20;

extern const U8 sndbank[];
extern const size_t sndbank_size;

enum
{
    NAME_LEN = 48,
    MAX_EFFECTS = 24,
    SPU_BASE_ADDR = 0x1010,   /* after capture buffers and SDK dummy block */
    FULL_VOLUME = 0x3fff
};

typedef struct
{
    const char *name;
    U32 spuAddr;
    U16 pitch;
} effect_t;

static effect_t effects[MAX_EFFECTS];
static int effectCount = 0;

/* music tunes -> CD-DA track numbers (must match tools/mkassets.py) */
static const struct { const char *name; int track; } tunes[] =
{
    { "sounds/tune0.wav", 2 },
    { "sounds/tune1.wav", 3 },
    { "sounds/tune2.wav", 4 },
    { "sounds/tune3.wav", 5 },
    { "sounds/tune4.wav", 6 },
    { "sounds/tune5.wav", 7 },
};

static CdlLOC toc[100];
static int trackCount = 0;
static sound_t *music = NULL;
static int musicTrack = 0;
static S8 musicLoop = 0;
static volatile bool musicEnded = false;
static bool musicPaused = false;
static CdlLOC musicResumePos;

static bool isAudioInitialised = false;
static bool isMuted = false;
static S16 masterVolume = FULL_VOLUME;

static U32 rd32(const U8 *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((U32)p[3] << 24);
}

static void autoPauseHandler(CdlIntrResult result, uint8_t *data)
{
    (void)result;
    (void)data;
    musicEnded = true;
}

static void applyMasterVolume(void)
{
    S16 v = isMuted ? 0 : masterVolume;
    SpuSetCommonMasterVolume(v, v);
}

/*
 * CD-DA music
 */
static int tuneTrack(const sound_t *sound)
{
    size_t i;

    if (!sound || !sound->name)
    {
        return 0;
    }
    for (i = 0; i < sizeof(tunes) / sizeof(tunes[0]); i++)
    {
        if (strcmp(sound->name, tunes[i].name) == 0)
        {
            return tunes[i].track;
        }
    }
    return 0;
}

static void playTrackFrom(const CdlLOC *pos)
{
    uint8_t mode = CdlModeDA | CdlModeAP;

    musicEnded = false;
    CdControl(CdlSetmode, &mode, 0);
    CdControl(CdlSetloc, pos, 0);
    CdControl(CdlPlay, 0, 0);
}

static void startMusic(sound_t *sound, int track, S8 loop)
{
    if (track < 1 || track > trackCount)
    {
        sys_error("(audio) CD track %d not found", track);
        return;
    }
    music = sound;
    musicTrack = track;
    musicLoop = loop;
    musicPaused = false;
    playTrackFrom(&toc[track - 1]);
}

static void stopMusic(void)
{
    if (music)
    {
        CdControlF(CdlPause, 0);
    }
    music = NULL;
    musicTrack = 0;
    musicPaused = false;
}

/*
 * SPU effects
 */
static int findEffect(const sound_t *sound)
{
    int i;

    if (!sound || !sound->name)
    {
        return -1;
    }
    for (i = 0; i < effectCount; i++)
    {
        if (strcmp(sound->name, effects[i].name) == 0)
        {
            return i;
        }
    }
    return -1;
}

static void keyOff(int voice)
{
    SpuSetKey(0, 1 << voice);
}

static void keyOn(int voice)
{
    keyOff(voice);
    SPU_CH_FREQ(voice) = effects[voice].pitch;
    SPU_CH_ADDR(voice) = getSPUAddr(effects[voice].spuAddr);
    SPU_CH_VOL_L(voice) = FULL_VOLUME;
    SPU_CH_VOL_R(voice) = FULL_VOLUME;
    SPU_CH_ADSR1(voice) = 0x00ff;
    SPU_CH_ADSR2(voice) = 0x0000;
    SpuSetKey(1, 1 << voice);
}

/*
 * public interface
 */
bool syssnd_init(void)
{
    U32 count, i, dataStart, dataSize;

    if (isAudioInitialised)
    {
        return true;
    }

    SpuInit();

    /* sound bank: "XRSB" u32 count, count x {name[48] off size rate}, pad to 64, ADPCM */
    if (memcmp(sndbank, "XRSB", 4) != 0)
    {
        sys_error("(audio) bad sound bank");
        return false;
    }
    count = rd32(sndbank + 4);
    if (count > MAX_EFFECTS)
    {
        count = MAX_EFFECTS;
    }
    dataStart = 8 + rd32(sndbank + 4) * (NAME_LEN + 12);
    dataStart = (dataStart + 63) & ~63u;
    dataSize = (U32)sndbank_size - dataStart;

    for (i = 0; i < count; i++)
    {
        const U8 *e = sndbank + 8 + i * (NAME_LEN + 12);
        effects[i].name = (const char *)e;
        effects[i].spuAddr = SPU_BASE_ADDR + rd32(e + NAME_LEN);
        effects[i].pitch = getSPUSampleRate(rd32(e + NAME_LEN + 8));
    }
    effectCount = (int)count;

    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    SpuSetTransferStartAddr(SPU_BASE_ADDR);
    SpuWrite((const uint32_t *)(sndbank + dataStart), (dataSize + 63) & ~63u);
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);

    /* CD drive, for the music tracks */
    if (CdInit())
    {
        trackCount = CdGetToc(toc);
        CdAutoPauseCallback(&autoPauseHandler);
    }
    if (trackCount < 7)
    {
        sys_printf("xrick/audio: only %d CD tracks, music disabled\n", trackCount);
    }

    applyMasterVolume();
    isAudioInitialised = true;
    return true;
}

void syssnd_shutdown(void)
{
    if (!isAudioInitialised)
    {
        return;
    }
    syssnd_stopAll();
    CdAutoPauseCallback(NULL);
    isAudioInitialised = false;
}

void syssnd_update(void)
{
    if (!isAudioInitialised || !music || !musicEnded || musicPaused)
    {
        return;
    }
    musicEnded = false;

    if (musicLoop > 0)
    {
        musicLoop--;
    }
    if (musicLoop != 0)
    {
        playTrackFrom(&toc[musicTrack - 1]);   /* loop the tune */
    }
    else
    {
        music = NULL;                          /* tune finished */
        musicTrack = 0;
    }
}

void syssnd_vol(S8 d)
{
    masterVolume += d * 0x0400;
    if (masterVolume < 0) masterVolume = 0;
    if (masterVolume > FULL_VOLUME) masterVolume = FULL_VOLUME;
    applyMasterVolume();
}

void syssnd_toggleMute(void)
{
    isMuted = !isMuted;
    applyMasterVolume();
}

void syssnd_play(sound_t *sound, S8 loop)
{
    int track, voice;

    if (!isAudioInitialised || !sound)
    {
        return;
    }

    track = tuneTrack(sound);
    if (track)
    {
        startMusic(sound, track, loop);
        return;
    }

    voice = findEffect(sound);
    if (voice < 0)
    {
        sys_error("(audio) unknown sound %s", sound->name);
        return;
    }
    keyOn(voice);
}

void syssnd_pauseAll(bool pause)
{
    int i;
    uint8_t result[8];

    if (!isAudioInitialised)
    {
        return;
    }

    if (pause)
    {
        for (i = 0; i < effectCount; i++)
        {
            keyOff(i);
        }
        if (music && !musicPaused)
        {
            /* remember where we are, then stop the drive */
            if (CdControlB(CdlGetlocP, 0, result))
            {
                musicResumePos.minute = result[5];
                musicResumePos.second = result[6];
                musicResumePos.sector = result[7];
                musicResumePos.track = 0;
            }
            else
            {
                musicResumePos = toc[musicTrack - 1];
            }
            CdControlB(CdlPause, 0, 0);
            musicPaused = true;
        }
    }
    else if (music && musicPaused)
    {
        musicPaused = false;
        playTrackFrom(&musicResumePos);
    }
}

void syssnd_stop(sound_t *sound)
{
    int voice;

    if (!isAudioInitialised || !sound)
    {
        return;
    }
    if (sound == music)
    {
        stopMusic();
        return;
    }
    voice = findEffect(sound);
    if (voice >= 0)
    {
        keyOff(voice);
    }
}

void syssnd_stopAll(void)
{
    int i;

    if (!isAudioInitialised)
    {
        return;
    }
    for (i = 0; i < effectCount; i++)
    {
        keyOff(i);
    }
    stopMusic();
}

#endif /* ENABLE_SOUND */

/* eof */
