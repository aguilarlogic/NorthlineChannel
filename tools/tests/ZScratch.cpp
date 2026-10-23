#include "../TestUtil.h"
#include "DynamicsTestKit.h"

namespace tool
{
using namespace dk;

NL_TEST (zz_attackTable)
{
    for (double fs : { 44100.0, 192000.0 })
        for (int type = 0; type < 4; ++type)
            for (float a : { 0.5f, 1.0f, 2.0f, 3.0f, 10.0f, 40.0f })
                std::printf ("ATT fs %.0f type %d attack %.1f -> %.3f ms (%.1f%%)\n", fs, type, a, measureCompAttackMs (type, fs, a), 100.0 * (measureCompAttackMs (type, fs, a) / a - 1.0));
}

NL_TEST (zz_makeupTable)
{
    for (int type = 0; type < 4; ++type)
        for (float ratio : { 1.5f, 2.0f, 4.0f, 10.0f })
        {
            std::printf ("MK type %d ratio %.1f:", type, ratio);
            for (float thr : { -40.0f, -30.0f, -20.0f, -10.0f }) std::printf ("  T%.0f %+.2f", thr, makeupErrorLu (type, ratio, thr, kFs));
            std::printf ("\n");
        }
}

NL_TEST (zz_keyListen)
{
    auto in = pinkNoise (24000, -14.0f, 5);
    CompParams cp; cp.thresh = -30; cp.ratio = 6; cp.lookahead = 2; cp.scHp = 2000.0f; cp.knee = 3; cp.listen = 1;
    auto k = makeModule<CompModule> (cp);
    auto ok = runModule (*k, in, 128);
    auto hp = makeSvf (SvfType::HighPass, kFs, 2000.0f, 0.7071f);
    SvfState st;
    float worst = 0.0f; int wi = 0;
    for (int i = 0; i < 24000 - 96; ++i)
    {
        const float ref = st.process (in.getSample (0, i), hp);
        const float d = std::abs (ref - ok.getSample (0, i + 96));
        if (d > worst) { worst = d; wi = i; }
    }
    std::printf ("KEY worst %g at %d (ref %g out %g)\n", worst, wi, 0.0, ok.getSample (0, wi + 96));
}

NL_TEST (zz_optReset)
{
    CompParams cp; cp.type = CompModule::OPT; cp.thresh = -30; cp.ratio = 6; cp.releaseMs = 200;
    auto loud = sine (kFs, 500.0, lin (-6.0f), 3 * 48000);
    auto probeIn = sine (kFs, 500.0, lin (-20.0f), 24000);
    auto a = makeModule<CompModule> (cp);
    runModule (*a, loud, 256);
    a->reset();
    auto b = makeModule<CompModule> (cp);
    auto oa = runModule (*a, probeIn, 100), ob = runModule (*b, probeIn, 100);
    int first = -1;
    for (int i = 0; i < 24000 && first < 0; ++i) if (oa.getSample (0, i) != ob.getSample (0, i)) first = i;
    std::printf ("RESET first diff at %d (a %g b %g)\n", first, first >= 0 ? oa.getSample (0, first) : 0.0f, first >= 0 ? ob.getSample (0, first) : 0.0f);
}
} // namespace tool
