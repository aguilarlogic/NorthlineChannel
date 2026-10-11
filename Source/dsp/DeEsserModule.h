#pragma once

#include "ParamSnapshot.h"
#include "Filters.h"

namespace nl
{
struct DsBandParams
{
    bool on = true;
    int type = 3; // 0 low shelf, 1 bell, 2 narrow, 3 high shelf
    float freq = 6000.0f, thresh = -20.0f, range = 12.0f;
};

struct DsParams
{
    bool linked = true, relative = true, ext = false;
    int listen = 0; // 0 off, 1 band 1, 2 band 2
    float releaseMs = 60.0f, outDb = 0.0f;
    DsBandParams band[2];
};

// Two-band dynamic de-esser built as a dynamic EQ: a fixed band filter feeds the
// detector, and the audio runs through a TPT SVF shelf/bell whose gain follows the
// detector (coefficients recomputed per sample; the TPT structure keeps this free
// of zipper noise). At 0 dB gain the SVF output is m0*v0 = x exactly, so the
// module is bit-transparent until it acts.
class DeEsserModule
{
public:
    // Latency in samples. Must depend only on non-automatable settings (quality,
    // lookahead), never on whether the module is on or on automatable parameters.
    int latency() const noexcept { return 0; }
    // Detector level of one band in dBFS, for the scope.
    float getKeyLevelDb (int /*band*/) const noexcept { return -120.0f; } // TODO(ds)

    void prepare (double sampleRate, int /*maxBlock*/ = 4096)
    {
        fs = sampleRate;
        attackCoef = timeCoef (0.4f, fs);
        fullEnvRelease = timeCoef (60.0f, fs);
        detRelease = timeCoef (5.0f, fs);
        outGain.prepare (fs, 20.0f);
        reset();
    }

    void reset()
    {
        for (auto& b : bands)
        {
            for (auto& c : b.ch) c = {};
            b.grDb = 0.0f;
        }
        for (auto& e : fullEnv) e = 0.0f;
    }

    void setParams (const DsParams& p)
    {
        params = p;
        releaseCoef = timeCoef (p.releaseMs, fs);
        outGain.setTarget (dbToGain (p.outDb));
        for (int b = 0; b < 2; ++b)
        {
            const auto& bp = p.band[b];
            auto& band = bands[b];
            if (bp.type != band.type || std::abs (bp.freq - band.freq) > 0.01f)
            {
                band.type = bp.type;
                band.freq = bp.freq;
                const float q = bp.type == 1 ? 1.4f : bp.type == 2 ? 5.0f : 0.7071f;
                switch (bp.type)
                {
                    case 0:  band.c = makeSvf (SvfType::LowPass, fs, bp.freq, q); break;
                    case 1:
                    case 2:  band.c = makeSvf (SvfType::BandPass, fs, bp.freq, q); break;
                    default: band.c = makeSvf (SvfType::HighPass, fs, bp.freq, q); break;
                }
                band.g0 = band.c.g;
                band.k0 = band.c.k;
                band.dynGr[0] = band.dynGr[1] = 1.0e9f;
            }
        }
    }

    void process (float* const* ch, int numCh, int n, const float* const* key)
    {
        const bool useExt = params.ext && key != nullptr;
        constexpr float ratioSlope = 1.0f - 1.0f / 5.0f; // 5:1
        constexpr float kneeHalf = 3.0f;

        float minG[2] { 1.0f, 1.0f };
        for (int i = 0; i < n; ++i)
        {
            if ((i % kControlBlock) == 0) outGain.tick();
            const float og = outGain.current;

            float x[2] {}, s[2][2] {}, over[2][2] {};
            for (int c = 0; c < numCh; ++c)
            {
                x[c] = ch[c][i];
                const float det = useExt ? key[c][i] : x[c];
                const float a = std::abs (det);
                fullEnv[c] = a > fullEnv[c] ? a : a + (fullEnv[c] - a) * fullEnvRelease;
                const float fullDb = fastGainToDb (fullEnv[c]);

                for (int b = 0; b < 2; ++b)
                {
                    auto& band = bands[b];
                    if (! params.band[b].on) continue;
                    auto& st = band.ch[c];
                    // Main path band signal.
                    s[b][c] = runBand (st.main, x[c], band);
                    // Detector band signal (external key uses its own filter state).
                    const float sd = useExt ? runBand (st.key, det, band) : s[b][c];
                    const float ad = std::abs (sd);
                    st.env = ad > st.env ? ad : ad + (st.env - ad) * attackCoefRelease();
                    float lvl = fastGainToDb (st.env);
                    if (params.relative) lvl = (fullDb > -60.0f) ? lvl - fullDb : -120.0f;
                    over[b][c] = lvl - params.band[b].thresh;
                }
            }

            for (int b = 0; b < 2; ++b)
            {
                if (! params.band[b].on) continue;
                auto& band = bands[b];
                const float range = params.band[b].range;
                auto computeTarget = [&] (float ov)
                {
                    float gr;
                    if (ov <= -kneeHalf) gr = 0.0f;
                    else if (ov < kneeHalf) { const float t = ov + kneeHalf; gr = -ratioSlope * t * t / (4.0f * kneeHalf); }
                    else gr = -ratioSlope * ov;
                    return std::max (gr, -range);
                };

                if (params.linked && numCh > 1)
                {
                    const float target = computeTarget (std::max (over[b][0], over[b][1]));
                    const float g = smooth (band.ch[0], target);
                    band.ch[1].grSm = band.ch[0].grSm;
                    const SvfCoefs& dc = cachedCoefs (band, 0, g);
                    for (int c = 0; c < numCh; ++c) ch[c][i] = band.ch[c].dyn.process (ch[c][i], dc);
                    minG[b] = std::min (minG[b], fastDbToGain (g));
                }
                else
                {
                    for (int c = 0; c < numCh; ++c)
                    {
                        const float g = smooth (band.ch[c], computeTarget (over[b][c]));
                        ch[c][i] = band.ch[c].dyn.process (ch[c][i], cachedCoefs (band, c, g));
                        minG[b] = std::min (minG[b], fastDbToGain (g));
                    }
                }
            }

            for (int c = 0; c < numCh; ++c)
            {
                if (params.listen > 0) ch[c][i] = s[params.listen - 1][c];
                ch[c][i] *= og;
            }
        }
        for (int b = 0; b < 2; ++b) bands[b].grDb = fastGainToDb (minG[b]);
    }

    float getGainReductionDb (int band) const noexcept { return bands[band].grDb; }

private:
    struct BandChan
    {
        SvfState main, key, dyn;
        float env = 0.0f, grSm = 0.0f;
    };

    struct Band
    {
        SvfCoefs c;          // fixed detector / listen band filter
        float g0 = 0.0f, k0 = 1.0f;
        SvfCoefs dynC[2];    // last dynamic coefficients per channel (index 0 when linked)
        float dynGr[2] { 1.0e9f, 1.0e9f };
        int type = -1;
        float freq = -1.0f, grDb = 0.0f;
        BandChan ch[2];
    };

    static inline float runBand (SvfState& st, float x, const Band& band) noexcept
    {
        return st.process (x, band.c);
    }

    // Recomputes only when the gain has moved by more than 0.02 dB (inaudible step)
    // or lands exactly on 0 dB, where the filter must return to bit-transparency.
    static inline const SvfCoefs& cachedCoefs (Band& band, int c, float grDb) noexcept
    {
        float& last = band.dynGr[c];
        if (std::abs (grDb - last) > 0.02f || (grDb == 0.0f && last != 0.0f))
        {
            band.dynC[c] = dynamicCoefs (band, grDb);
            last = grDb;
        }
        return band.dynC[c];
    }

    // Simper shelf / bell coefficients for a gain of grDb (<= 0) at the band.
    static inline SvfCoefs dynamicCoefs (const Band& band, float grDb) noexcept
    {
        SvfCoefs c;
        const float A = fastDbToGain (0.5f * grDb); // 10^(dB/40)
        float g = band.g0, k = band.k0;
        switch (band.type)
        {
            case 0: // low shelf
                g /= std::sqrt (A);
                c.m0 = 1.0f; c.m1 = k * (A - 1.0f); c.m2 = A * A - 1.0f;
                break;
            case 1:
            case 2: // bell
                k /= A;
                c.m0 = 1.0f; c.m1 = k * (A * A - 1.0f); c.m2 = 0.0f;
                break;
            default: // high shelf
                g *= std::sqrt (A);
                c.m0 = A * A; c.m1 = k * (1.0f - A) * A; c.m2 = 1.0f - A * A;
                break;
        }
        c.g = g;
        c.k = k;
        c.a1 = 1.0f / (1.0f + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }

    inline float attackCoefRelease() const noexcept { return detRelease; }

    inline float smooth (BandChan& st, float target) noexcept
    {
        const float coef = target < st.grSm ? attackCoef : releaseCoef;
        st.grSm = target + (st.grSm - target) * coef;
        if (target == 0.0f && st.grSm > -1.0e-3f) st.grSm = 0.0f; // settle exactly, back to bit-transparent
        return st.grSm;
    }

    double fs = 48000.0;
    DsParams params;
    Band bands[2];
    float fullEnv[2] {};
    float attackCoef = 0.0f, releaseCoef = 0.0f, fullEnvRelease = 0.0f, detRelease = 0.995f;
    Smoothed outGain;
};
// Maps the parameter snapshot to DE-ESS settings (owned by this module).
inline DsParams readDsParams (const ParamSnapshot& p)
{
    DsParams dp;
    dp.linked = p.choice (P::dsMode) == 0;
    dp.relative = p.on (P::dsRelative);
    dp.ext = p.on (P::dsExt);
    dp.listen = std::min (p.choice (P::dsListen), 2); // TODO(ds): Delta
    dp.releaseMs = p.f (P::dsRelease);
    dp.outDb = p.f (P::dsOut);
    for (int b = 0; b < 2; ++b)
    {
        const int o = P::ds1On + b * P::kDsBandStride;
        dp.band[b].on = p.on (o);
        dp.band[b].type = p.choice (o + 2);
        dp.band[b].freq = p.f (o + 3);
        dp.band[b].thresh = p.f (o + 4);
        dp.band[b].range = p.f (o + 5);
    }
    return dp;
}
} // namespace nl
