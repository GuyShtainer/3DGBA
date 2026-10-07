// test_voxel_daylight.c -- host test for the day cycle of the voxel world's light (voxel_daylight.c) and
// what it asks of the baked lighting (voxel_lighting.c): look backlog L4. Synthetic state only (vx_fixture.h).
//
//   clang -std=c11 -Wall -Wextra -O2 -fsanitize=address,undefined -DVOXEL_HOST_FILES -DCTR_VOXEL_LIGHTING=1 \
//         -DVOXEL_LIGHTING_TESTS -I source/voxel -I source/romgen -I test/host test/host/test_voxel_daylight.c \
//         <the VWORLD list of tools/romgen/Makefile> -lm -o /tmp/tvdl && /tmp/tvdl
//
// (`make -C tools/romgen vtest` builds and runs it.)
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "vx_fixture.h"
#include "voxel_world.h"
#include "voxel_lighting.h"
#include "voxel_daylight.h"

static int sChecks, sFails;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void Take(void)
{
    VxMemSrc s = fxSrc();
    CHECK(vx_snapshot_take(fxSnap, &s));
    CHECK(vx_adapter_decode(fxSnap));
    VoxelWorld_BeginBatch();
    VoxelWorld_BuildInstances();
}

static float Len(VoxelSunDir s)
{
    return sqrtf(s.dx * s.dx + s.dz * s.dz);
}

/* The light a tint lets through, all channels of the sun and the shade. */
static float Brightness(const VoxelDayTint *t)
{
    return t->sun[0] + t->sun[1] + t->sun[2] + t->shade[0] + t->shade[1] + t->shade[2];
}

static bool IsIdentity(const VoxelDayTint *t)
{
    for (int i = 0; i < 3; ++i)
        if (t->sun[i] != 1.0f || t->shade[i] != 1.0f || t->haze[i] != 1.0f)
            return false;
    return t->glow == 1.0f && t->shadow == 1.0f;
}

/* The tile run a sun asks of a ray, per side, as the march walks it: a ray from anywhere in tile 0 to
 * x - dx * REACH, z - dz * REACH. */
static void MarchRun(VoxelSunDir s, int *west, int *east, int *north, int *south)
{
    *west = *east = *north = *south = 0;
    for (int k = 0; k <= 16; ++k)
    {
        float f = (float)k / 16.0f * 0.999f;
        int ex = (int)floorf(f - s.dx * (float)VOXEL_LIGHT_REACH);
        int ez = (int)floorf(f - s.dz * (float)VOXEL_LIGHT_REACH);

        if (-ex > *west) *west = -ex;
        if (ex > *east) *east = ex;
        if (-ez > *north) *north = -ez;
        if (ez > *south) *south = ez;
    }
}

/* ---- the sun's path ---- */

static void TestSun(void)
{
    VoxelSunDir ref = VoxelDaylight_Sun((float)VOXEL_SUN_REF_MINUTE), refStep = VoxelDaylight_StepSun(VOXEL_SUN_REF_STEP);
    VoxelSunDir moon = VoxelDaylight_Sun(23.0f * 60.0f), noon = VoxelDaylight_Sun(12.0f * 60.0f);
    VoxelSunDir dawn = VoxelDaylight_Sun(5.75f * 60.0f), dusk = VoxelDaylight_Sun(19.0f * 60.0f);
    float maxDelta = 0.0f;

    /* L3's sun, bit for bit, at the reference time, its step, by night, and before the cycle moves it. */
    CHECK(ref.dx == VOXEL_SUN_REF_DX && ref.dz == VOXEL_SUN_REF_DZ);
    CHECK(refStep.dx == VOXEL_SUN_REF_DX && refStep.dz == VOXEL_SUN_REF_DZ);
    CHECK(VoxelDaylight_Step((float)VOXEL_SUN_REF_MINUTE) == VOXEL_SUN_REF_STEP);
    CHECK(VoxelDaylight_Step((float)VOXEL_SUN_REF_MINUTE + 9.99f) == VOXEL_SUN_REF_STEP);
    CHECK(moon.dx == VOXEL_SUN_REF_DX && moon.dz == VOXEL_SUN_REF_DZ);
    CHECK(VOXEL_SUN_DX == VOXEL_SUN_REF_DX && VOXEL_SUN_DZ == VOXEL_SUN_REF_DZ);

    /* East in the morning, west in the evening, overhead-south at noon. */
    CHECK(dawn.dx < -1.0f && dusk.dx > 1.0f);
    CHECK(noon.dx == 0.0f && noon.dz < 0.0f);
    CHECK(VoxelDaylight_Sun(9.0f * 60.0f).dx < 0.0f && VoxelDaylight_Sun(15.0f * 60.0f).dx > 0.0f);

    for (unsigned m = 0; m < VOXEL_DAY_MINUTES * 4u; ++m)
    {
        float minute = (float)m * 0.25f;
        VoxelSunDir s = VoxelDaylight_Sun(minute);

        /* Always on the camera's side (L3), never lower than ~30 degrees. */
        CHECK(s.dz < 0.0f);
        CHECK(Len(s) <= 1.75f);
    }
    /* Step to step, by day (05:00-19:50), the baked sun moves a little: no pop of the shadows. The two
     * swings to and from the moon (20:00-20:30, 04:00-04:30) fall in full night. */
    for (unsigned step = 0; step < VOXEL_SUN_STEPS; ++step)
    {
        VoxelSunDir a = VoxelDaylight_StepSun(step), b = VoxelDaylight_StepSun(step + 1u);
        float d = sqrtf((a.dx - b.dx) * (a.dx - b.dx) + (a.dz - b.dz) * (a.dz - b.dz));
        unsigned minute = step * VOXEL_SUN_STEP_MINUTES;

        CHECK(a.dz < 0.0f && Len(a) <= 1.75f);
        if (minute >= 5u * 60u && minute < 19u * 60u + 50u)
        {
            CHECK(d < 0.12f);
            if (d > maxDelta)
                maxDelta = d;
        }
        else if (d > 0.12f)
        {
            VoxelDayTint t = VoxelDaylight_Tint((float)minute);

            CHECK(t.glow == 0.0f && t.shadow <= 0.30f);   /* a big step only in full night */
        }
    }
    CHECK(VoxelDaylight_StepSun(VOXEL_SUN_STEPS).dx == VoxelDaylight_StepSun(0).dx);   /* it wraps */
    CHECK(VoxelDaylight_Step(-1.0f) == VOXEL_SUN_STEPS - 1u && VoxelDaylight_Step(1440.0f) == 0u);
    CHECK(VoxelDaylight_Wrap(1500.0f) == 60.0f && VoxelDaylight_Wrap(-60.0f) == 1380.0f);
    printf("  sun: %u steps of %u min; largest daytime step %.4f\n", VOXEL_SUN_STEPS, VOXEL_SUN_STEP_MINUTES,
           (double)maxDelta);
}

/* ---- the tint ---- */

static void TestTint(void)
{
    VoxelDayTint night = VoxelDaylight_Tint(23.0f * 60.0f), dawn = VoxelDaylight_Tint(5.75f * 60.0f);
    VoxelDayTint dusk = VoxelDaylight_Tint(19.0f * 60.0f), noon = VoxelDaylight_Tint(12.0f * 60.0f);
    VoxelDayTint ref = VoxelDaylight_Tint((float)VOXEL_SUN_REF_MINUTE), prev = VoxelDaylight_Tint(0.0f);
    float maxStep = 0.0f;

    /* The whole middle of the day is the old picture. */
    CHECK(IsIdentity(&noon) && IsIdentity(&ref));
    for (unsigned m = 9u * 60u; m <= 16u * 60u + 30u; ++m)
    {
        VoxelDayTint t = VoxelDaylight_Tint((float)m);

        CHECK(IsIdentity(&t));
    }
    /* Night is darker than dusk and dawn, which are darker than the day; no glow at night. */
    CHECK(Brightness(&night) < Brightness(&dusk) && Brightness(&night) < Brightness(&dawn));
    CHECK(Brightness(&dusk) < Brightness(&noon) && Brightness(&dawn) < Brightness(&noon));
    CHECK(night.glow == 0.0f && night.shadow > 0.0f && night.shadow < dusk.shadow);
    CHECK(night.sun[2] > night.sun[0]);                  /* moonlight is blue */
    CHECK(dusk.sun[0] > dusk.sun[2] && dawn.sun[0] > dawn.sun[2]);   /* the low sun is warm */
    /* Every factor in range all day (they multiply the weather's light, so the shade's may exceed the
     * sun's in blue: a cool shadow under a warm low sun). */
    for (unsigned m = 0; m < VOXEL_DAY_MINUTES; ++m)
    {
        VoxelDayTint t = VoxelDaylight_Tint((float)m + 0.5f);

        for (int i = 0; i < 3; ++i)
        {
            CHECK(t.sun[i] > 0.0f && t.shade[i] > 0.0f && t.haze[i] > 0.0f);
            CHECK(t.sun[i] <= 1.10f && t.shade[i] <= 1.10f && t.haze[i] <= 1.0f);
        }
        CHECK(t.glow >= 0.0f && t.glow <= 1.0f && t.shadow >= 0.0f && t.shadow <= 1.0f);
    }
    /* Continuous: no channel moves by more than a little a minute, around the whole day (the wrap too). */
    for (unsigned q = 1; q <= VOXEL_DAY_MINUTES * 4u; ++q)
    {
        VoxelDayTint t = VoxelDaylight_Tint((float)q * 0.25f);
        float d = 0.0f;

        for (int i = 0; i < 3; ++i)
        {
            d = fmaxf(d, fabsf(t.sun[i] - prev.sun[i]));
            d = fmaxf(d, fabsf(t.shade[i] - prev.shade[i]));
            d = fmaxf(d, fabsf(t.haze[i] - prev.haze[i]));
        }
        d = fmaxf(d, fabsf(t.glow - prev.glow));
        d = fmaxf(d, fabsf(t.shadow - prev.shadow));
        CHECK(d < 0.01f);   /* per quarter minute */
        if (d > maxStep)
            maxStep = d;
        prev = t;
    }
    printf("  tint: largest change %.5f per quarter minute\n", (double)maxStep);
}

/* ---- the override file ---- */

static void TestOverride(void)
{
    VoxelDayOverride o;

    CHECK(!VoxelDaylight_ParseOverride(NULL, &o) && o.mode == VOXEL_DAY_LIVE);
    CHECK(VoxelDaylight_ParseOverride("off", &o) && o.mode == VOXEL_DAY_OFF);
    CHECK(VoxelDaylight_ParseOverride("  OFF\r\n", &o) && o.mode == VOXEL_DAY_OFF);
    CHECK(VoxelDaylight_ParseOverride("05:45", &o) && o.mode == VOXEL_DAY_FIXED && o.minute == 345.0f && o.speed == 0.0f);
    CHECK(VoxelDaylight_ParseOverride("23:00\n", &o) && o.mode == VOXEL_DAY_FIXED && o.minute == 1380.0f);
    CHECK(VoxelDaylight_ParseOverride("13:55 x60", &o) && o.minute == 835.0f && o.speed == 60.0f);
    CHECK(VoxelDaylight_ParseOverride("0:00 X3600\n", &o) && o.minute == 0.0f && o.speed == 3600.0f);
    CHECK(!VoxelDaylight_ParseOverride("", &o) && o.mode == VOXEL_DAY_LIVE);
    CHECK(!VoxelDaylight_ParseOverride("24:00", &o) && o.mode == VOXEL_DAY_LIVE);
    CHECK(!VoxelDaylight_ParseOverride("12:60", &o));
    CHECK(!VoxelDaylight_ParseOverride("12:5", &o));
    CHECK(!VoxelDaylight_ParseOverride("12:00 x0", &o));
    CHECK(!VoxelDaylight_ParseOverride("12:00 x3601", &o));
    CHECK(!VoxelDaylight_ParseOverride("12:00 fast", &o));
    CHECK(!VoxelDaylight_ParseOverride("offf", &o));
    CHECK(!VoxelDaylight_ParseOverride("noon", &o) && o.mode == VOXEL_DAY_LIVE);

    VoxelDaylight_ParseOverride("12:00", &o);
    CHECK(VoxelDaylight_OverrideMinute(&o, 0.0) == 720.0f && VoxelDaylight_OverrideMinute(&o, 3600.0) == 720.0f);
    VoxelDaylight_ParseOverride("23:30 x60", &o);
    CHECK(fabsf(VoxelDaylight_OverrideMinute(&o, 15.0) - 1425.0f) < 0.01f);   /* 15 s at x60: 15 minutes */
    CHECK(fabsf(VoxelDaylight_OverrideMinute(&o, 60.0) - 30.0f) < 0.01f);     /* past midnight */
    CHECK(fabsf(VoxelDaylight_OverrideMinute(&o, 86400.0 * 7.0 + 60.0) - 30.0f) < 0.05f);   /* a week on */
    for (unsigned s = 0; s < 100000u; s += 7u)
    {
        float m = VoxelDaylight_OverrideMinute(&o, (double)s);

        CHECK(m >= 0.0f && m < (float)VOXEL_DAY_MINUTES);
    }
}

/* ---- the re-bake pace ---- */

static void TestPacer(void)
{
    VoxelRebakePacer p;
    unsigned stale = 60, frames = 0, worst = 0;

    CHECK(VOXEL_SUN_REBAKES_PER_FRAME >= 1u && VOXEL_SUN_REBAKES_PER_FRAME <= 2u);
    VoxelRebake_Init(&p, VOXEL_SUN_REBAKES_PER_FRAME);
    CHECK(VoxelRebake_Take(&p, 5));
    for (unsigned i = 1; i < VOXEL_SUN_REBAKES_PER_FRAME; ++i)
        CHECK(VoxelRebake_Take(&p, 5));
    CHECK(!VoxelRebake_Take(&p, 5) && !VoxelRebake_Take(&p, 5));   /* the frame's budget is spent */
    CHECK(VoxelRebake_Take(&p, 6));                                /* a new frame, a new budget */
    VoxelRebake_Init(&p, 3);
    CHECK(VoxelRebake_Take(&p, 0xFFFFFFFFu) && VoxelRebake_Take(&p, 0xFFFFFFFFu) && VoxelRebake_Take(&p, 0xFFFFFFFFu));
    CHECK(!VoxelRebake_Take(&p, 0xFFFFFFFFu) && VoxelRebake_Take(&p, 0u));   /* the counter wraps */

    /* A wave: the whole view stale at once, a scheduler asking for every chunk every frame (as
     * StartNextJob may). It never starts more than the cap in a frame, and it finishes. */
    VoxelRebake_Init(&p, VOXEL_SUN_REBAKES_PER_FRAME);
    for (uint32_t frame = 100; stale != 0 && frames < 1000u; ++frame, ++frames)
    {
        unsigned started = 0;

        for (unsigned ask = 0; ask < stale + 8u; ++ask)
            if (ask < stale && VoxelRebake_Take(&p, frame))
                ++started;
        CHECK(started <= VOXEL_SUN_REBAKES_PER_FRAME);
        if (started > worst)
            worst = started;
        stale -= started;
    }
    CHECK(stale == 0 && frames == 60u / VOXEL_SUN_REBAKES_PER_FRAME);
    printf("  pace: 60 stale chunks re-baked over %u frames, at most %u started a frame\n", frames, worst);
}

/* ---- what the day asks of the lighting ---- */

static void TestReach(void)
{
    int west, east, north, south, w, e, n, s;

    /* Before the day widens it, the hash reads what it did under L3 alone. */
    VoxelLighting_Reach(&west, &east, &north, &south);
    CHECK(west == 4 && east == 0 && north == 0 && south == 6);
    MarchRun(VoxelDaylight_StepSun(VOXEL_SUN_REF_STEP), &w, &e, &n, &s);
    CHECK(w <= west && e <= east && n <= north && s <= south);

    for (unsigned step = 0; step < VOXEL_SUN_STEPS; ++step)
    {
        VoxelSunDir sun = VoxelDaylight_StepSun(step);

        VoxelLighting_WidenReach(sun.dx, sun.dz);
    }
    VoxelLighting_Reach(&west, &east, &north, &south);
    /* Every sun the day bakes with reads only tiles the hash reads: a chunk's hash holds whichever is up. */
    for (unsigned step = 0; step < VOXEL_SUN_STEPS; ++step)
    {
        MarchRun(VoxelDaylight_StepSun(step), &w, &e, &n, &s);
        CHECK(w <= west && e <= east && n <= north && s <= south);
    }
    CHECK(north == 0);   /* the sun never goes north: the north margin is VOXEL_CHUNK_MARGIN_NORTH's alone */
    printf("  hash reach over the day: W%d E%d N%d S%d (L3 alone: W4 E0 N0 S6)\n", west, east, north, south);
    /* The default sun is still L3's: widening reads further, it moves nothing. */
    CHECK(VOXEL_SUN_DX == VOXEL_SUN_REF_DX && VOXEL_SUN_DZ == VOXEL_SUN_REF_DZ);
}

#define POINTS (28 * 26 * 3)

static void Points(float out[POINTS])
{
    unsigned i = 0;

    for (int z = -4; z < 24; ++z)
        for (int x = -2; x < 24; ++x)
            for (int k = 0; k < 3; ++k)
            {
                float px = (float)x + 0.17f + 0.31f * (float)k, pz = (float)z + 0.62f - 0.23f * (float)k;
                float py = 0.4f + 0.8f * (float)k;

                out[i++] = VoxelLighting_Sample(px, py, pz);
            }
}

extern bool gVoxelLightingStepEveryPoint;

static void TestLighting(void)
{
    static float before[POINTS], after[POINTS], ref[POINTS];
    static const float faces[][3] = {{0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0.5f}, {0, 1, -0.5f}};
    float faceBefore[7];
    unsigned same = 0, shadowed[3] = {0, 0, 0};
    const VoxelSunDir suns[3] = {VoxelDaylight_Sun(5.75f * 60.0f), VoxelDaylight_Sun(12.0f * 60.0f),
                                 VoxelDaylight_Sun(19.0f * 60.0f)};

    Take();
    /* At L3's time the day bakes exactly what L3 baked: same march, same face terms. */
    VoxelLighting_Reset();
    Points(before);
    for (int f = 0; f < 7; ++f)
        faceBefore[f] = VoxelLighting_Face(faces[f][0], faces[f][1], faces[f][2]);
    {
        VoxelSunDir sun = VoxelDaylight_StepSun(VOXEL_SUN_REF_STEP);

        VoxelLighting_SetSun(sun.dx, sun.dz);
    }
    Points(after);
    for (unsigned i = 0; i < POINTS; ++i)
        same += before[i] == after[i];
    CHECK(same == POINTS);
    for (int f = 0; f < 7; ++f)
        CHECK(VoxelLighting_Face(faces[f][0], faces[f][1], faces[f][2]) == faceBefore[f]);

    /* At other suns the skipping march still agrees with the point-by-point one; SetSun drops the
     * caches (a sample cached under one sun is never served under another). */
    for (int k = 0; k < 3; ++k)
    {
        VoxelLighting_SetSun(suns[k].dx, suns[k].dz);
        gVoxelLightingStepEveryPoint = false;
        VoxelLighting_Reset();
        Points(after);
        gVoxelLightingStepEveryPoint = true;
        VoxelLighting_Reset();
        Points(ref);
        gVoxelLightingStepEveryPoint = false;
        same = 0;
        for (unsigned i = 0; i < POINTS; ++i)
        {
            same += after[i] == ref[i];
            shadowed[k] += ref[i] < 1.0f;
        }
        CHECK(same == POINTS);
        /* The sun's facing moves with it: a west wall is lit in the evening, an east one in the morning. */
        if (k == 0)
            CHECK(VoxelLighting_Face(1, 0, 0) > VOXEL_AMBIENT && VoxelLighting_Face(-1, 0, 0) == VOXEL_AMBIENT);
        if (k == 2)
            CHECK(VoxelLighting_Face(-1, 0, 0) > VOXEL_AMBIENT && VoxelLighting_Face(1, 0, 0) == VOXEL_AMBIENT);
        CHECK(VoxelLighting_Face(0, 0, -1) == VOXEL_AMBIENT);   /* never the north wall */
    }
    /* And back: L3's sun again gives L3's picture. */
    VoxelLighting_SetSun(VOXEL_SUN_REF_DX, VOXEL_SUN_REF_DZ);
    Points(after);
    same = 0;
    for (unsigned i = 0; i < POINTS; ++i)
        same += before[i] == after[i];
    CHECK(same == POINTS);
    printf("  march: %u points at dawn/noon/dusk agree with the reference; in shadow %u/%u/%u\n", POINTS,
           shadowed[0], shadowed[1], shadowed[2]);
}

int main(void)
{
    CHECK(fxInit() == 0);
    TestSun();
    TestTint();
    TestOverride();
    TestPacer();
    TestReach();
    TestLighting();
    printf("test_voxel_daylight: %d checks, %d failures\n", sChecks, sFails);
    return sFails != 0;
}
