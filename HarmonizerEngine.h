#pragma once
// Glizzarine Harmonizer engine: real-time pitch-synchronous overlap-add (PSOLA).
// The singer's voice is analysed (pitch + one mark per vocal cycle). Every held MIDI key
// gets its own voice that rebuilds the singing at the key's pitch from those cycles, so the
// timbre (formants) of the voice stays. No JUCE dependency, so it can be tested offline.
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

class HarmonizerEngine
{
public:
    static constexpr int maxVoices = 16;

    struct Params
    {
        float dry = 0.8f;          // original voice level
        float harmony = 0.8f;      // harmony voices level
        float formant = 0.0f;      // -1 .. +1 (darker / brighter, ~ +-6 semitones of formant shift)
        float consonants = 0.6f;   // how much of S, T, breath goes into the harmonies
        float attack = 0.015f;     // seconds
        float release = 0.6f;      // seconds, fade after key / pedal release
        float spread = 0.7f;       // stereo spread of the harmony voices
        float lowestHz = 80.0f;    // lowest sung pitch to detect (sets latency)
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        inSize = 1 << 15;
        in.assign ((size_t) inSize, 0.0f);
        lp.assign ((size_t) inSize, 0.0f);
        marks.clear();
        n = 0;
        lastMark = 0;
        t0 = (float) (sr / 150.0);
        voiced = false;
        lpState = lpState2 = 0.0f;
        lpC = 1.0f - std::exp (-2.0f * 3.14159265f * 900.0f / (float) sr);
        setLowest (params.lowestHz);
        for (auto& v : voices) { v = Voice(); v.ola.assign ((size_t) olaSize, 0.0f); }
        hopCount = 0;
        dryDelay.assign ((size_t) inSize, 0.0f);
        vflag.assign ((size_t) inSize, 0);
        uvSmooth = 0.0f;
    }

    void setParams (const Params& p) { if (p.lowestHz != params.lowestHz) setLowest (p.lowestHz); params = p; }
    void setVelocitySensitivity (float s) { velSens = std::min (1.0f, std::max (0.0f, s)); }
    int activeVoices() const { int k = 0; for (auto& v : voices) if (v.active && (v.gate || v.sustained)) ++k; return k; }
    int getLatencySamples() const { return delay; }

    void noteOn (int note, float velocity)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (x.active && x.note == note) v = &x;            // retrigger same key
        if (! v) for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age < v->age) v = &x; }  // steal oldest
        const bool fresh = ! v->active;
        v->active = true; v->note = note; v->gate = true; v->sustained = false;
        v->vel = 1.0f - velSens * (1.0f - velocity); v->age = ++ageCounter;
        if (fresh) { v->env = 0.0f; v->pIn = v->pOut = 0.0f; v->agc = 1.5f; v->nextMark = (double) (n - delay); std::fill (v->ola.begin(), v->ola.end(), 0.0f); }
        int k = 0; for (auto& x : voices) if (x.active) ++k;
        static const float panSeq[8] = { 0.0f, -0.6f, 0.6f, -0.3f, 0.3f, -0.9f, 0.9f, -0.45f };
        v->pan = panSeq[(k - 1) & 7];
    }

    void noteOff (int note)
    {
        for (auto& v : voices)
            if (v.active && v.note == note && v.gate)
            {
                v.gate = false;
                v.sustained = pedal;
            }
    }

    void setSustain (bool down)
    {
        pedal = down;
        if (! down) for (auto& v : voices) v.sustained = false;
    }

    void allNotesOff() { for (auto& v : voices) { v.gate = false; v.sustained = false; } }

    // mono in, stereo out
    void process (const float* input, float* outL, float* outR, int num)
    {
        const float atkC = 1.0f - std::exp (-1.0f / (std::max (0.001f, params.attack) * (float) sr));
        const float relC = 1.0f - std::exp (-1.0f / (std::max (0.02f, params.release) * (float) sr * 0.25f));
        const float fShift = std::exp2 (params.formant * 0.5f);   // grain read rate

        for (int i = 0; i < num; ++i)
        {
            const float x = input[i];
            in[(size_t) (n & (inSize - 1))] = x;
            lpState += lpC * (x - lpState);                   // ~900 Hz two-pole low-pass
            lpState2 += lpC * (lpState - lpState2);           // for pitch and cycle marks
            lp[(size_t) (n & (inSize - 1))] = lpState2;
            ++n;

            if (++hopCount >= 256) { hopCount = 0; detectPitch(); }
            vflag[(size_t) (n & (inSize - 1))] = voiced ? 1 : 0;
            placeMarks();

            const int64_t t = n - delay;                       // output time (aligned with input)
            float l = 0.0f, r = 0.0f, envMax = 0.0f;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                const bool on = v.gate || v.sustained;
                v.env += ((on ? 1.0f : 0.0f) - v.env) * (on ? atkC : relC);
                if (! on && v.env < 1.0e-4f) { v.active = false; continue; }

                const float targetPeriod = (float) (sr / (440.0 * std::pow (2.0, (v.note - 69) / 12.0)));
                // schedule grains a half-grain ahead of the output position
                int guard = 0;
                while (v.nextMark <= (double) t + (double) maxT0 && guard++ < 8)
                    emitGrain (v, t, targetPeriod, fShift);

                envMax = std::max (envMax, v.env);
                const size_t idx = (size_t) (t & (olaSize - 1));
                // level match: each harmony voice is kept about as loud as the sung voice
                const float raw = v.ola[idx];
                const float tin = in[(size_t) (t & (inSize - 1))];
                v.pIn  += 0.0001f * (tin * tin - v.pIn);
                v.pOut += 0.0001f * (raw * raw - v.pOut);
                const float want = std::sqrt ((v.pIn + 1.0e-10f) / (v.pOut + 1.0e-10f));
                v.agc += 0.0005f * (std::min (4.0f, std::max (0.5f, want)) - v.agc);
                const float s = raw * v.agc * v.env * v.vel;
                v.ola[idx] = 0.0f;
                const float pl = std::sqrt (0.5f * (1.0f - v.pan * params.spread));
                const float pr = std::sqrt (0.5f * (1.0f + v.pan * params.spread));
                l += s * pl; r += s * pr;
            }

            // consonants (S, T, breath) go into the harmonies once, in the centre, not once per voice
            const float tIn = in[(size_t) (t & (inSize - 1))];
            const float uv = vflag[(size_t) (t & (inSize - 1))] ? 0.0f : 1.0f;
            uvSmooth += (uv - uvSmooth) * 0.01f;
            const float cons = tIn * uvSmooth * params.consonants * envMax;
            const float d = tIn * params.dry;
            outL[i] = d + (l + cons) * params.harmony;
            outR[i] = d + (r + cons) * params.harmony;
        }
    }

    // for tests
    bool isVoiced() const { return voiced; }
    float currentPitchHz() const { return (float) (sr / t0); }
    float aperiodicity() const { return lastAperiodicity; }

private:
    struct Mark { int64_t pos; float period; bool voiced; };
    struct Voice
    {
        bool active = false, gate = false, sustained = false;
        int note = -1; float vel = 1.0f, env = 0.0f, pan = 0.0f;
        double nextMark = 0.0; uint64_t age = 0;
        float pIn = 0.0f, pOut = 0.0f, agc = 1.5f;
        std::vector<float> ola;
    };

    static constexpr int olaSize = 1 << 13;

    void setLowest (float hz)
    {
        params.lowestHz = hz;
        maxT0 = (int) std::ceil (sr / std::max (50.0f, hz));
        delay = (int) (maxT0 * 1.6f) + 32;
    }

    float at (int64_t p) const { return in[(size_t) (p & (inSize - 1))]; }
    float atLp (int64_t p) const { return lp[(size_t) (p & (inSize - 1))]; }

    // YIN pitch detection on the newest samples
    void detectPitch()
    {
        const int W = 768;
        const int tauMin = (int) (sr / 1000.0), tauMax = maxT0;
        if (n < W + tauMax + 2) return;
        const int64_t start = n - W - tauMax;
        double energy = 0.0;
        for (int j = 0; j < W; ++j) { const float s = atLp (start + tauMax + j); energy += s * s; }
        const float rms = (float) std::sqrt (energy / W);
        diff.assign ((size_t) tauMax + 2, 0.0f);
        float running = 0.0f; int best = -1; float bestVal = 1.0f;
        std::vector<float>& d = diff;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < W; j += 2)   // every second sample is plenty for pitch
            {
                const float a = atLp (start + tauMax + j) - atLp (start + tauMax + j - tau);
                sum += a * a;
            }
            running += (float) sum;
            d[(size_t) tau] = (float) sum * (float) tau / std::max (1.0e-9f, running);
        }
        for (int tau = std::max (2, tauMin); tau < tauMax; ++tau)
            if (d[(size_t) tau] < 0.15f && d[(size_t) tau] < d[(size_t) tau - 1] && d[(size_t) tau] <= d[(size_t) tau + 1])
            { best = tau; bestVal = d[(size_t) tau]; break; }
        if (best < 0)
            for (int tau = std::max (2, tauMin); tau < tauMax; ++tau)
                if (d[(size_t) tau] < bestVal) { bestVal = d[(size_t) tau]; best = tau; }

        lastAperiodicity = bestVal;
        // lenient with hysteresis: real singing is never perfectly periodic
        const bool periodic = best > 0 && bestVal < (voiced ? 0.6f : 0.4f);
        const bool loud = rms > 0.002f;
        unvoicedHops = (periodic && loud) ? 0 : unvoicedHops + 1;
        const bool isVoice = (periodic && loud) || (voiced && loud && unvoicedHops < 2);
        if (isVoice && periodic)
        {
            float tau = (float) best;
            if (best > 1 && best < tauMax)
            {
                const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
                const float den = a - 2.0f * b + c;
                if (std::abs (den) > 1.0e-9f) tau += 0.5f * (a - c) / den;
            }
            // ignore single-hop jumps (octave / fifth errors); accept a new pitch once it repeats
            const bool jump = voiced && std::abs (tau - t0) > 0.2f * t0;
            if (jump && std::abs (tau - pendingTau) > 0.1f * tau) { pendingTau = tau; }
            else { t0 = voiced && ! jump ? 0.6f * t0 + 0.4f * tau : tau; pendingTau = 0.0f; }
        }
        voiced = isVoice;
    }

    // one analysis mark per vocal cycle, placed on the cycle's peak
    void placeMarks()
    {
        const float period = voiced ? t0 : 256.0f;
        const int64_t search = (int64_t) (period * 0.25f);
        if (n < lastMark + (int64_t) period + search + 1) return;
        int64_t m = lastMark + (int64_t) period;
        if (voiced)
        {
            int64_t bestP = m; float bestV = -1.0e9f;
            for (int64_t p = m - search; p <= m + search; ++p)
            { const float v = atLp (p); if (v > bestV) { bestV = v; bestP = p; } }
            m = bestP;
        }
        marks.push_back ({ m, period, voiced });
        lastMark = m;
        while (marks.size() > 512) marks.erase (marks.begin());
    }

    void emitGrain (Voice& v, int64_t, float targetPeriod, float fShift)
    {
        // analysis mark nearest to the current output time, fully available
        const Mark* src = nullptr; double bestDist = 1.0e18;
        for (auto it = marks.rbegin(); it != marks.rend(); ++it)
        {
            if (it->pos + (int64_t) it->period + 2 > n) continue;
            const double dd = std::abs ((double) it->pos - (double) v.nextMark);
            if (dd < bestDist) { bestDist = dd; src = &*it; }
            if ((double) it->pos < v.nextMark - 2.0 * maxT0) break;
        }
        if (! src) { v.nextMark += targetPeriod; return; }

        const float P = src->period;
        const bool vc = src->voiced;
        const float hop = vc ? targetPeriod : P;                 // unvoiced: plain overlap-add
        const float rate = vc ? fShift : 1.0f;
        const int half = std::min (maxT0, (int) (P / rate));
        // Hann grains of length 2*half at spacing hop overlap to half/hop: normalise to unity
        if (! vc) { v.nextMark += hop; return; }           // unvoiced handled as one shared layer
        const float gain = std::min (2.0f, hop / (float) half);
        const int64_t centre = (int64_t) std::llround (v.nextMark);
        for (int k = -half; k <= half; ++k)
        {
            const float w = 0.5f + 0.5f * std::cos (3.14159265f * (float) k / (float) half);
            const double sp = (double) src->pos + (double) k * rate;
            const int64_t s0 = (int64_t) std::floor (sp); const float fr = (float) (sp - (double) s0);
            const float s = at (s0) + fr * (at (s0 + 1) - at (s0));
            v.ola[(size_t) ((centre + k) & (olaSize - 1))] += s * w * gain;
        }
        v.nextMark += hop;
    }

    Params params;
    double sr = 44100.0;
    std::vector<float> in, lp, diff, dryDelay;
    std::vector<uint8_t> vflag;
    float uvSmooth = 0.0f;
    int inSize = 0, hopCount = 0, maxT0 = 600, delay = 1000;
    int64_t n = 0, lastMark = 0;
    float t0 = 300.0f, lpState = 0.0f, lpState2 = 0.0f, lpC = 0.12f;
    bool voiced = false, pedal = false;
    int unvoicedHops = 0;
    float pendingTau = 0.0f;
    float lastAperiodicity = 1.0f;
    std::vector<Mark> marks;
    Voice voices[maxVoices];
    uint64_t ageCounter = 0;
    float velSens = 0.6f;
};
