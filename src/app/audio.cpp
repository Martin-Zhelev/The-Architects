// audio.cpp
// Sound effects and music, all synthesized in code (no audio files).
//
// Sound effects are short waves built from oscillators (sine, square, saw,
// triangle, noise) with envelopes. Each world has its own music: a small
// procedural composer turns a chord progression, a scale and a drum style
// into an 8-bar loop that is streamed without gaps.

#include "app.h"

#include <cmath>
#include <cstdlib>

namespace
{

const int SAMPLE_RATE = 22050;
const int STREAM_FRAMES = 4096;
const int TRACK_COUNT = static_cast<int>(MusicTrack::None);

enum class Osc
{
    Sine,
    Square,
    Saw,
    Triangle,
    Noise
};

bool audioReady = false;
Sound sounds[static_cast<int>(Sfx::Count)];
AudioStream stream;
std::vector<short> trackData[TRACK_COUNT];
MusicTrack currentTrack = MusicTrack::None;
MusicTrack wantedTrack = MusicTrack::None;
std::size_t trackPosition = 0;
float trackFade = 0.0f; // 0 = silent, 1 = full volume
short streamChunk[STREAM_FRAMES];
float masterVolume = 0.8f;
float focusGain = 1.0f;          // fades to 0 while the window is in the background
bool backgroundMuted = false;
float musicVolume = 0.6f;
float sfxVolume = 0.8f;
unsigned int noiseState = 12345u;

float noise()
{
    noiseState = noiseState * 1664525u + 1013904223u;
    return static_cast<float>((noiseState >> 9) & 0x7FFF) / 16383.5f - 1.0f;
}

float oscillator(Osc wave, float phase)
{
    const float p = phase - std::floor(phase);
    switch (wave)
    {
    case Osc::Sine:
        return std::sin(p * 2.0f * PI);
    case Osc::Square:
        return p < 0.5f ? 0.8f : -0.8f;
    case Osc::Saw:
        return (2.0f * p - 1.0f) * 0.8f;
    case Osc::Triangle:
        return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
    case Osc::Noise:
        return noise();
    }
    return 0.0f;
}

float midiToFrequency(float midi)
{
    return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
}

// ---- Building blocks ----

struct Buffer
{
    std::vector<float> samples;
    explicit Buffer(float seconds) : samples(static_cast<std::size_t>(seconds * SAMPLE_RATE) + 1, 0.0f) {}
};

// A tone that glides from one frequency to another with an attack and an
// exponential decay. `vibrato` adds a wobble in Hz.
void tone(std::vector<float>& out, float start, float duration, float fromHz, float toHz, Osc wave, float volume,
          float attack = 0.005f, float decay = 6.0f, float vibrato = 0.0f)
{
    const std::size_t first = static_cast<std::size_t>(start * SAMPLE_RATE);
    const std::size_t count = static_cast<std::size_t>(duration * SAMPLE_RATE);
    float phase = 0.0f;
    for (std::size_t i = 0; i < count && first + i < out.size(); ++i)
    {
        const float t = static_cast<float>(i) / SAMPLE_RATE;
        const float progress = t / duration;
        float frequency = fromHz + (toHz - fromHz) * progress;
        if (vibrato > 0.0f)
        {
            frequency += std::sin(t * 2.0f * PI * 6.0f) * vibrato;
        }
        phase += frequency / SAMPLE_RATE;
        const float envelopeIn = t < attack ? t / attack : 1.0f;
        const float envelopeOut = std::exp(-decay * t) * (1.0f - std::pow(progress, 8.0f));
        out[first + i] += oscillator(wave, phase) * volume * envelopeIn * envelopeOut;
    }
}

// Filtered noise; `brightness` 0..1 controls the simple low-pass filter.
void noiseBurst(std::vector<float>& out, float start, float duration, float volume, float decay, float brightness,
                float brightnessEnd = -1.0f)
{
    const std::size_t first = static_cast<std::size_t>(start * SAMPLE_RATE);
    const std::size_t count = static_cast<std::size_t>(duration * SAMPLE_RATE);
    float filtered = 0.0f;
    if (brightnessEnd < 0.0f)
    {
        brightnessEnd = brightness;
    }
    for (std::size_t i = 0; i < count && first + i < out.size(); ++i)
    {
        const float t = static_cast<float>(i) / SAMPLE_RATE;
        const float progress = t / duration;
        const float b = brightness + (brightnessEnd - brightness) * progress;
        filtered += (noise() - filtered) * b;
        out[first + i] += filtered * volume * std::exp(-decay * t) * (1.0f - progress);
    }
}

Sound makeSound(const std::vector<float>& samples)
{
    float peak = 0.0001f;
    for (const float s : samples)
    {
        peak = std::fmax(peak, std::fabs(s));
    }
    const float gain = peak > 0.95f ? 0.95f / peak : 1.0f;
    short* data = static_cast<short*>(MemAlloc(static_cast<unsigned int>(samples.size() * sizeof(short))));
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        data[i] = static_cast<short>(samples[i] * gain * 32767.0f);
    }
    Wave rayWave{};
    rayWave.frameCount = static_cast<unsigned int>(samples.size());
    rayWave.sampleRate = SAMPLE_RATE;
    rayWave.sampleSize = 16;
    rayWave.channels = 1;
    rayWave.data = data;
    Sound sound = LoadSoundFromWave(rayWave);
    UnloadWave(rayWave);
    return sound;
}

// ---- Sound effect recipes ----

std::vector<float> buildSfx(Sfx sfx)
{
    switch (sfx)
    {
    case Sfx::Step:
    {
        Buffer b(0.09f);
        noiseBurst(b.samples, 0.0f, 0.07f, 0.35f, 40.0f, 0.25f);
        tone(b.samples, 0.0f, 0.08f, 130.0f, 60.0f, Osc::Sine, 0.5f, 0.002f, 30.0f);
        return b.samples;
    }
    case Sfx::Bump:
    {
        Buffer b(0.2f);
        tone(b.samples, 0.0f, 0.18f, 95.0f, 45.0f, Osc::Sine, 0.9f, 0.002f, 14.0f);
        noiseBurst(b.samples, 0.0f, 0.06f, 0.4f, 40.0f, 0.15f);
        return b.samples;
    }
    case Sfx::Key:
    {
        Buffer b(0.6f);
        const float notes[4] = {880.0f, 1108.7f, 1318.5f, 1760.0f};
        for (int i = 0; i < 4; ++i)
        {
            tone(b.samples, 0.06f * static_cast<float>(i), 0.3f, notes[i], notes[i], Osc::Triangle, 0.45f, 0.003f, 9.0f);
        }
        return b.samples;
    }
    case Sfx::Gem:
    {
        Buffer b(0.6f);
        tone(b.samples, 0.0f, 0.5f, 1567.98f, 1567.98f, Osc::Sine, 0.5f, 0.002f, 7.0f);
        tone(b.samples, 0.07f, 0.5f, 2093.0f, 2093.0f, Osc::Sine, 0.45f, 0.002f, 7.0f);
        tone(b.samples, 0.07f, 0.5f, 4186.0f, 4186.0f, Osc::Sine, 0.12f, 0.002f, 10.0f);
        return b.samples;
    }
    case Sfx::PowerUp:
    {
        Buffer b(0.7f);
        tone(b.samples, 0.0f, 0.4f, 300.0f, 1200.0f, Osc::Square, 0.35f, 0.01f, 2.0f, 30.0f);
        tone(b.samples, 0.3f, 0.4f, 1046.5f, 1046.5f, Osc::Triangle, 0.35f, 0.005f, 5.0f);
        tone(b.samples, 0.3f, 0.4f, 1318.5f, 1318.5f, Osc::Triangle, 0.3f, 0.005f, 5.0f);
        return b.samples;
    }
    case Sfx::Door:
    {
        Buffer b(0.9f);
        noiseBurst(b.samples, 0.0f, 0.6f, 0.5f, 3.0f, 0.05f, 0.6f);
        const float chord[3] = {523.25f, 659.25f, 783.99f};
        for (int i = 0; i < 3; ++i)
        {
            tone(b.samples, 0.15f + 0.05f * static_cast<float>(i), 0.6f, chord[i], chord[i], Osc::Triangle, 0.3f,
                 0.01f, 4.0f);
        }
        return b.samples;
    }
    case Sfx::Teleport:
    {
        Buffer b(0.8f);
        tone(b.samples, 0.0f, 0.35f, 200.0f, 2200.0f, Osc::Saw, 0.3f, 0.01f, 1.0f, 60.0f);
        tone(b.samples, 0.3f, 0.45f, 2200.0f, 300.0f, Osc::Sine, 0.4f, 0.005f, 3.0f, 40.0f);
        noiseBurst(b.samples, 0.0f, 0.7f, 0.15f, 2.0f, 0.7f);
        return b.samples;
    }
    case Sfx::Hurt:
    {
        Buffer b(0.4f);
        tone(b.samples, 0.0f, 0.35f, 440.0f, 110.0f, Osc::Square, 0.5f, 0.002f, 5.0f);
        tone(b.samples, 0.0f, 0.35f, 466.0f, 116.0f, Osc::Square, 0.35f, 0.002f, 5.0f);
        noiseBurst(b.samples, 0.0f, 0.15f, 0.4f, 20.0f, 0.5f);
        return b.samples;
    }
    case Sfx::Roar:
    {
        Buffer b(1.3f);
        tone(b.samples, 0.0f, 1.2f, 75.0f, 52.0f, Osc::Saw, 0.6f, 0.08f, 1.5f, 12.0f);
        tone(b.samples, 0.0f, 1.2f, 112.0f, 80.0f, Osc::Saw, 0.35f, 0.1f, 1.5f, 15.0f);
        noiseBurst(b.samples, 0.0f, 1.2f, 0.55f, 1.6f, 0.12f, 0.04f);
        return b.samples;
    }
    case Sfx::Heartbeat:
    {
        Buffer b(0.5f);
        tone(b.samples, 0.0f, 0.15f, 70.0f, 45.0f, Osc::Sine, 1.0f, 0.004f, 18.0f);
        tone(b.samples, 0.22f, 0.15f, 65.0f, 42.0f, Osc::Sine, 0.8f, 0.004f, 18.0f);
        return b.samples;
    }
    case Sfx::WallBreak:
    {
        Buffer b(0.9f);
        noiseBurst(b.samples, 0.0f, 0.8f, 0.9f, 4.0f, 0.4f, 0.05f);
        tone(b.samples, 0.0f, 0.5f, 90.0f, 40.0f, Osc::Sine, 0.9f, 0.002f, 6.0f);
        for (int i = 0; i < 6; ++i)
        {
            noiseBurst(b.samples, 0.15f + 0.08f * static_cast<float>(i), 0.05f, 0.3f, 30.0f, 0.6f);
        }
        return b.samples;
    }
    case Sfx::Victory:
    {
        Buffer b(2.2f);
        const float melody[5] = {523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f};
        for (int i = 0; i < 4; ++i)
        {
            tone(b.samples, 0.13f * static_cast<float>(i), 0.2f, melody[i], melody[i], Osc::Square, 0.35f, 0.005f, 4.0f);
        }
        const float chord[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
        for (int i = 0; i < 4; ++i)
        {
            tone(b.samples, 0.55f, 1.5f, chord[i], chord[i], Osc::Triangle, 0.3f, 0.02f, 1.5f, 3.0f);
            tone(b.samples, 0.55f, 1.5f, chord[i] * 1.003f, chord[i] * 1.003f, Osc::Saw, 0.08f, 0.02f, 1.5f);
        }
        tone(b.samples, 0.55f, 1.0f, melody[4], melody[4], Osc::Square, 0.2f, 0.01f, 2.0f, 6.0f);
        return b.samples;
    }
    case Sfx::Defeat:
    {
        Buffer b(1.8f);
        const float notes[4] = {392.0f, 369.99f, 349.23f, 329.63f};
        for (int i = 0; i < 4; ++i)
        {
            tone(b.samples, 0.3f * static_cast<float>(i), 0.5f, notes[i], notes[i] * 0.98f, Osc::Triangle, 0.45f,
                 0.01f, 2.5f, 4.0f);
        }
        tone(b.samples, 1.2f, 0.6f, 82.4f, 60.0f, Osc::Saw, 0.4f, 0.01f, 3.0f);
        return b.samples;
    }
    case Sfx::MenuMove:
    {
        Buffer b(0.05f);
        tone(b.samples, 0.0f, 0.04f, 1400.0f, 1200.0f, Osc::Sine, 0.4f, 0.001f, 40.0f);
        return b.samples;
    }
    case Sfx::MenuSelect:
    {
        Buffer b(0.25f);
        tone(b.samples, 0.0f, 0.08f, 660.0f, 660.0f, Osc::Square, 0.3f, 0.002f, 10.0f);
        tone(b.samples, 0.07f, 0.15f, 990.0f, 990.0f, Osc::Square, 0.3f, 0.002f, 10.0f);
        return b.samples;
    }
    case Sfx::Achievement:
    {
        Buffer b(1.6f);
        const float bell[3] = {1318.5f, 1661.2f, 1975.5f};
        for (int i = 0; i < 3; ++i)
        {
            const float start = 0.08f * static_cast<float>(i);
            tone(b.samples, start, 1.4f, bell[i], bell[i], Osc::Sine, 0.35f, 0.002f, 2.5f);
            tone(b.samples, start, 1.0f, bell[i] * 2.76f, bell[i] * 2.76f, Osc::Sine, 0.08f, 0.002f, 5.0f);
        }
        return b.samples;
    }
    case Sfx::Star:
    {
        Buffer b(0.5f);
        tone(b.samples, 0.0f, 0.45f, 1318.5f, 1318.5f, Osc::Triangle, 0.5f, 0.002f, 6.0f);
        tone(b.samples, 0.0f, 0.3f, 2637.0f, 2637.0f, Osc::Sine, 0.2f, 0.002f, 9.0f);
        return b.samples;
    }
    case Sfx::Tick:
    {
        Buffer b(0.04f);
        tone(b.samples, 0.0f, 0.03f, 2200.0f, 2000.0f, Osc::Square, 0.25f, 0.001f, 50.0f);
        return b.samples;
    }
    case Sfx::TrailCorrect:
    {
        Buffer b(0.18f);
        tone(b.samples, 0.0f, 0.16f, 660.0f, 680.0f, Osc::Triangle, 0.5f, 0.002f, 12.0f);
        tone(b.samples, 0.0f, 0.1f, 1320.0f, 1320.0f, Osc::Sine, 0.15f, 0.002f, 18.0f);
        return b.samples;
    }
    case Sfx::TrailWrong:
    {
        Buffer b(0.35f);
        tone(b.samples, 0.0f, 0.3f, 150.0f, 140.0f, Osc::Square, 0.4f, 0.002f, 4.0f);
        tone(b.samples, 0.0f, 0.3f, 158.0f, 148.0f, Osc::Square, 0.4f, 0.002f, 4.0f);
        return b.samples;
    }
    case Sfx::Whoosh:
    {
        Buffer b(0.4f);
        noiseBurst(b.samples, 0.0f, 0.38f, 0.35f, 2.0f, 0.04f, 0.5f);
        return b.samples;
    }
    case Sfx::Count:
        break;
    }
    return std::vector<float>(10, 0.0f);
}

// ---- Procedural music ----

enum class DrumStyle
{
    None,
    Light,
    Synth,
    Heavy,
    Toms,
    Bouncy
};

struct TrackRecipe
{
    float bpm;
    int root;          // MIDI note of the key
    int scale[7];      // semitone steps of the scale
    int progression[4]; // scale degrees, one chord per bar (repeated)
    Osc bass;
    Osc arp;
    Osc pad;
    Osc lead;
    DrumStyle drums;
    int arpEvery;      // 1 = sixteenths, 2 = eighths
    float bassVolume;
    float arpVolume;
    float padVolume;
    float leadVolume;
    float echo;        // amount of echo on arp and lead
    int seed;
};

const int MINOR[7] = {0, 2, 3, 5, 7, 8, 10};
const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
const int PHRYGIAN[7] = {0, 1, 3, 5, 7, 8, 10};
const int DORIAN[7] = {0, 2, 3, 5, 7, 9, 10};
const int LYDIAN[7] = {0, 2, 4, 6, 7, 9, 11};

TrackRecipe makeRecipe(float bpm, int root, const int* scale, int p0, int p1, int p2, int p3, Osc bass, Osc arp,
                       Osc pad, Osc lead, DrumStyle drums, int arpEvery, float bassVolume, float arpVolume,
                       float padVolume, float leadVolume, float echo, int seed)
{
    TrackRecipe r{};
    r.bpm = bpm;
    r.root = root;
    for (int i = 0; i < 7; ++i)
    {
        r.scale[i] = scale[i];
    }
    r.progression[0] = p0;
    r.progression[1] = p1;
    r.progression[2] = p2;
    r.progression[3] = p3;
    r.bass = bass;
    r.arp = arp;
    r.pad = pad;
    r.lead = lead;
    r.drums = drums;
    r.arpEvery = arpEvery;
    r.bassVolume = bassVolume;
    r.arpVolume = arpVolume;
    r.padVolume = padVolume;
    r.leadVolume = leadVolume;
    r.echo = echo;
    r.seed = seed;
    return r;
}

TrackRecipe recipeFor(MusicTrack track)
{
    switch (track)
    {
    case MusicTrack::Menu:
        return makeRecipe(96, 45, MINOR, 0, 5, 2, 6, Osc::Saw, Osc::Triangle, Osc::Saw, Osc::Square,
                          DrumStyle::Light, 1, 0.35f, 0.18f, 0.10f, 0.12f, 0.35f, 1);
    case MusicTrack::Neon:
        return makeRecipe(110, 45, MINOR, 0, 5, 3, 4, Osc::Saw, Osc::Square, Osc::Saw, Osc::Saw, DrumStyle::Synth,
                          1, 0.42f, 0.15f, 0.09f, 0.12f, 0.3f, 2);
    case MusicTrack::Dungeon:
        return makeRecipe(72, 38, DORIAN, 0, 0, 5, 4, Osc::Sine, Osc::Triangle, Osc::Triangle, Osc::Triangle,
                          DrumStyle::Toms, 2, 0.45f, 0.18f, 0.12f, 0.16f, 0.4f, 3);
    case MusicTrack::Lava:
        return makeRecipe(126, 40, PHRYGIAN, 0, 1, 0, 6, Osc::Square, Osc::Saw, Osc::Saw, Osc::Square,
                          DrumStyle::Heavy, 1, 0.38f, 0.12f, 0.08f, 0.12f, 0.2f, 4);
    case MusicTrack::Ice:
        return makeRecipe(86, 48, LYDIAN, 0, 1, 4, 3, Osc::Sine, Osc::Sine, Osc::Triangle, Osc::Sine,
                          DrumStyle::None, 2, 0.35f, 0.22f, 0.12f, 0.18f, 0.5f, 5);
    case MusicTrack::Candy:
        return makeRecipe(132, 48, MAJOR, 0, 5, 3, 4, Osc::Square, Osc::Square, Osc::Triangle, Osc::Triangle,
                          DrumStyle::Bouncy, 1, 0.35f, 0.13f, 0.08f, 0.2f, 0.25f, 6);
    case MusicTrack::Trail:
        return makeRecipe(120, 43, MAJOR, 0, 4, 5, 3, Osc::Triangle, Osc::Square, Osc::Triangle, Osc::Square,
                          DrumStyle::Bouncy, 1, 0.38f, 0.12f, 0.08f, 0.14f, 0.25f, 7);
    case MusicTrack::None:
        break;
    }
    return recipeFor(MusicTrack::Menu);
}

float scaleNote(const TrackRecipe& r, int degree, int octave)
{
    int wrapped = degree % 7;
    int extraOctave = degree / 7;
    if (wrapped < 0)
    {
        wrapped += 7;
        extraOctave -= 1;
    }
    return static_cast<float>(r.root + r.scale[wrapped] + 12 * (octave + extraOctave));
}

void kick(std::vector<float>& out, float at, float volume)
{
    tone(out, at, 0.2f, 150.0f, 42.0f, Osc::Sine, volume, 0.001f, 14.0f);
}

void snare(std::vector<float>& out, float at, float volume)
{
    noiseBurst(out, at, 0.16f, volume, 18.0f, 0.6f);
    tone(out, at, 0.08f, 220.0f, 180.0f, Osc::Triangle, volume * 0.4f, 0.001f, 25.0f);
}

void hat(std::vector<float>& out, float at, float volume)
{
    noiseBurst(out, at, 0.04f, volume, 60.0f, 0.95f);
}

void tom(std::vector<float>& out, float at, float volume, float hz)
{
    tone(out, at, 0.3f, hz, hz * 0.6f, Osc::Sine, volume, 0.002f, 8.0f);
}

int hashInt(int a, int b, int seed)
{
    unsigned int h = static_cast<unsigned int>(a * 7919 + b * 104729 + seed * 1299709);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return static_cast<int>(h & 0x7FFFFFFF);
}

std::vector<short> composeTrack(MusicTrack track)
{
    const TrackRecipe r = recipeFor(track);
    const int bars = 8;
    const float sixteenth = 60.0f / r.bpm / 4.0f;
    const float barLength = sixteenth * 16.0f;
    const float total = barLength * static_cast<float>(bars);
    std::vector<float> dry(static_cast<std::size_t>(total * SAMPLE_RATE) + 1, 0.0f);
    std::vector<float> wet(dry.size(), 0.0f);

    int leadDegree = 4;
    for (int bar = 0; bar < bars; ++bar)
    {
        const int degree = r.progression[bar % 4];
        const float barStart = sixteenth * 16.0f * static_cast<float>(bar);
        const int chord[3] = {degree, degree + 2, degree + 4};

        // Pad: the chord held for the whole bar, slightly detuned for width.
        for (const int note : chord)
        {
            const float hz = midiToFrequency(scaleNote(r, note, 1));
            tone(dry, barStart, barLength, hz, hz, r.pad, r.padVolume, 0.25f, 0.6f);
            tone(dry, barStart, barLength, hz * 1.004f, hz * 1.004f, r.pad, r.padVolume * 0.6f, 0.3f, 0.6f);
        }

        for (int step = 0; step < 16; ++step)
        {
            const float at = barStart + sixteenth * static_cast<float>(step);

            // Bass
            const bool bassHit = (r.drums == DrumStyle::Toms || r.drums == DrumStyle::None) ? (step % 8 == 0)
                                 : (r.drums == DrumStyle::Heavy)                         ? (step % 2 == 0)
                                                                                         : (step % 4 == 0 || step % 4 == 2);
            if (bassHit)
            {
                const int octave = (r.drums == DrumStyle::Synth && step % 4 == 2) ? 1 : 0;
                const float hz = midiToFrequency(scaleNote(r, degree, octave));
                const float length = (r.drums == DrumStyle::Toms || r.drums == DrumStyle::None) ? sixteenth * 7.0f
                                                                                                  : sixteenth * 1.8f;
                tone(dry, at, length, hz, hz, r.bass, r.bassVolume, 0.005f, 3.0f);
            }

            // Arpeggio over the chord, two octaves
            if (step % r.arpEvery == 0)
            {
                const int index = (step / r.arpEvery) % 6;
                const int pattern[6] = {0, 1, 2, 3, 2, 1};
                const int note = chord[pattern[index] % 3] + (pattern[index] == 3 ? 7 : 0);
                const float hz = midiToFrequency(scaleNote(r, note, 2));
                tone(wet, at, sixteenth * 1.5f * static_cast<float>(r.arpEvery), hz, hz, r.arp, r.arpVolume, 0.003f,
                     9.0f);
            }

            // Lead melody in the second half: a gentle random walk on the scale.
            if (bar >= 4 && step % 4 == 0 && hashInt(bar, step, r.seed) % 3 != 0)
            {
                leadDegree += (hashInt(step, bar, r.seed + 1) % 5) - 2;
                leadDegree = leadDegree < 0 ? 0 : (leadDegree > 9 ? 9 : leadDegree);
                const float hz = midiToFrequency(scaleNote(r, leadDegree, 2));
                tone(wet, at, sixteenth * 3.5f, hz, hz, r.lead, r.leadVolume, 0.01f, 2.5f, 3.0f);
            }

            // Drums
            switch (r.drums)
            {
            case DrumStyle::None:
                if (step % 4 == 2)
                {
                    hat(dry, at, 0.06f);
                }
                break;
            case DrumStyle::Light:
                if (step == 0 || step == 8)
                {
                    kick(dry, at, 0.5f);
                }
                if (step % 4 == 2)
                {
                    hat(dry, at, 0.08f);
                }
                break;
            case DrumStyle::Synth:
                if (step % 8 == 0)
                {
                    kick(dry, at, 0.75f);
                }
                if (step % 8 == 4)
                {
                    snare(dry, at, 0.4f);
                }
                if (step % 2 == 0)
                {
                    hat(dry, at, 0.1f);
                }
                break;
            case DrumStyle::Heavy:
                if (step % 4 == 0)
                {
                    kick(dry, at, 0.8f);
                }
                if (step % 8 == 4)
                {
                    snare(dry, at, 0.5f);
                }
                if (step % 2 == 1)
                {
                    hat(dry, at, 0.12f);
                }
                if (bar % 4 == 3 && step >= 12)
                {
                    tom(dry, at, 0.4f, 160.0f - static_cast<float>(step - 12) * 20.0f);
                }
                break;
            case DrumStyle::Toms:
                if (step == 0)
                {
                    tom(dry, at, 0.55f, 90.0f);
                }
                if (step == 10)
                {
                    tom(dry, at, 0.35f, 120.0f);
                }
                break;
            case DrumStyle::Bouncy:
                if (step == 0 || step == 6 || step == 10)
                {
                    kick(dry, at, 0.6f);
                }
                if (step == 4 || step == 12)
                {
                    snare(dry, at, 0.35f);
                }
                hat(dry, at, step % 2 == 0 ? 0.09f : 0.05f);
                break;
            }
        }
    }

    // Echo on the melodic parts (3/16 delay with feedback), wrapping round the
    // loop so the repeat point is seamless.
    const std::size_t delay = static_cast<std::size_t>(sixteenth * 3.0f * SAMPLE_RATE);
    std::vector<float> echo(wet.size(), 0.0f);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (std::size_t i = 0; i < wet.size(); ++i)
        {
            const std::size_t from = (i + wet.size() - delay) % wet.size();
            echo[i] = (wet[from] + echo[from]) * 0.45f;
        }
    }

    std::vector<float> mixed(dry.size());
    float peak = 0.0001f;
    for (std::size_t i = 0; i < dry.size(); ++i)
    {
        mixed[i] = dry[i] + wet[i] + echo[i] * r.echo;
        peak = std::fmax(peak, std::fabs(mixed[i]));
    }
    std::vector<short> result(mixed.size());
    const float gain = 0.8f / peak;
    for (std::size_t i = 0; i < mixed.size(); ++i)
    {
        result[i] = static_cast<short>(mixed[i] * gain * 32767.0f);
    }
    return result;
}

void applyMusicVolume()
{
    if (audioReady)
    {
        SetAudioStreamVolume(stream, musicVolume * trackFade);
    }
}

} // namespace

void initAudio()
{
    InitAudioDevice();
    audioReady = IsAudioDeviceReady();
    if (!audioReady)
    {
        return; // no sound card: the game still works, silently
    }
    for (int i = 0; i < static_cast<int>(Sfx::Count); ++i)
    {
        sounds[i] = makeSound(buildSfx(static_cast<Sfx>(i)));
    }
    SetAudioStreamBufferSizeDefault(STREAM_FRAMES);
    stream = LoadAudioStream(SAMPLE_RATE, 16, 1);
    SetAudioStreamVolume(stream, 0.0f);
    PlayAudioStream(stream);
    SetMasterVolume(masterVolume);
}

void unloadAudio()
{
    if (!audioReady)
    {
        return;
    }
    for (Sound& sound : sounds)
    {
        UnloadSound(sound);
    }
    UnloadAudioStream(stream);
    CloseAudioDevice();
    audioReady = false;
}

bool isAudioAvailable()
{
    return audioReady;
}

void setVolumes(int master, int music, int sfx)
{
    masterVolume = static_cast<float>(master) / 100.0f;
    musicVolume = static_cast<float>(music) / 100.0f;
    sfxVolume = static_cast<float>(sfx) / 100.0f;
    if (audioReady)
    {
        SetMasterVolume(masterVolume * focusGain);
    }
    applyMusicVolume();
}

void playSfx(Sfx sfx, float pitch, float volume)
{
    if (!audioReady)
    {
        return;
    }
    Sound& sound = sounds[static_cast<int>(sfx)];
    SetSoundPitch(sound, pitch);
    SetSoundVolume(sound, volume * sfxVolume);
    PlaySound(sound);
}

MusicTrack musicForTheme(ThemeId theme)
{
    switch (theme)
    {
    case ThemeId::Neon:
        return MusicTrack::Neon;
    case ThemeId::Dungeon:
        return MusicTrack::Dungeon;
    case ThemeId::Lava:
        return MusicTrack::Lava;
    case ThemeId::Ice:
        return MusicTrack::Ice;
    case ThemeId::Candy:
        return MusicTrack::Candy;
    }
    return MusicTrack::Menu;
}

void playMusic(MusicTrack track)
{
    wantedTrack = track;
}

void setBackgroundMute(bool muted)
{
    backgroundMuted = muted;
}

void updateAudio(float dt)
{
    if (!audioReady)
    {
        return;
    }

    // Fade out quickly when the window goes to the background, fade back in
    // when it returns (no sudden blast of music).
    const float wantedGain = backgroundMuted ? 0.0f : 1.0f;
    if (focusGain != wantedGain)
    {
        const float speed = backgroundMuted ? 6.0f : 2.5f;
        focusGain = wantedGain > focusGain ? std::fmin(wantedGain, focusGain + dt * speed)
                                           : std::fmax(wantedGain, focusGain - dt * speed);
        SetMasterVolume(masterVolume * focusGain);
    }

    // Cross-fade: fade the old track out, switch, fade the new one in.
    if (wantedTrack != currentTrack)
    {
        trackFade -= dt * 2.0f;
        if (trackFade <= 0.0f || currentTrack == MusicTrack::None)
        {
            trackFade = 0.0f;
            currentTrack = wantedTrack;
            trackPosition = 0;
            if (currentTrack != MusicTrack::None && trackData[static_cast<int>(currentTrack)].empty())
            {
                trackData[static_cast<int>(currentTrack)] = composeTrack(currentTrack);
            }
        }
    }
    else if (currentTrack != MusicTrack::None && trackFade < 1.0f)
    {
        trackFade = std::fmin(1.0f, trackFade + dt * 1.5f);
    }
    applyMusicVolume();

    while (IsAudioStreamProcessed(stream))
    {
        if (currentTrack == MusicTrack::None)
        {
            for (short& sample : streamChunk)
            {
                sample = 0;
            }
        }
        else
        {
            const std::vector<short>& data = trackData[static_cast<int>(currentTrack)];
            for (short& sample : streamChunk)
            {
                sample = data[trackPosition];
                trackPosition = (trackPosition + 1) % data.size();
            }
        }
        UpdateAudioStream(stream, streamChunk, STREAM_FRAMES);
    }
}
