/* voxel_daylight.c -- the day cycle of the voxel world's light (3DGBA, GPLv3). See voxel_daylight.h. */
#include <stddef.h>
#include <stdlib.h>

#include "voxel_daylight.h"

/*
 * The sun's path, as keys interpolated in a straight line. The sun stays SOUTH of the scene all
 * day (dz < 0), on the camera's side: that is the whole point of L3 - the GBA sprites are drawn
 * lit from the front - so the day only swings it from east to west and lowers it at either end.
 * Azimuth a from south (west positive) and horizontal length L = cot(elevation):
 * dx = sin(a) L, dz = -cos(a) L.
 *
 *   04:30-06:00  a -70, L 1.70 (30 deg up)   long shadows to the north-west
 *   08:00        a -50, L 1.20 (40 deg)
 *   10:00        a -25, L 0.90 (48 deg)
 *   12:00        a   0, L 0.75 (53 deg)
 *   14:00        L3's sun, exactly (a 35, 52 deg)
 *   16:00        a  55, L 1.10 (42 deg)
 *   18:00-20:00  a  70, L 1.70 (30 deg)      long shadows to the north-east
 *   20:30-04:00  the moon: L3's vector again (dim: the tint does that)
 *
 * The two swings between the sun and the moon (20:00-20:30, 04:00-04:30) happen in full night,
 * where the shadows are faint, so the large steps they take are not seen. The longest run any key
 * asks of a ray is |dx| 1.5975 x VOXEL_LIGHT_REACH: the hash margins (voxel_lighting.c) are
 * widened to every step's sun.
 */
typedef struct
{
    float minute;
    float dx, dz;
} SunKey;

#define SUN_EAST_DX (-1.5975f)
#define SUN_LOW_DZ (-0.5814f)
#define SUN_WEST_DX 1.5975f

static const SunKey kSun[] = {
    {4.0f * 60.0f, VOXEL_SUN_REF_DX, VOXEL_SUN_REF_DZ},
    {4.5f * 60.0f, SUN_EAST_DX, SUN_LOW_DZ},
    {6.0f * 60.0f, SUN_EAST_DX, SUN_LOW_DZ},
    {8.0f * 60.0f, -0.9193f, -0.7713f},
    {10.0f * 60.0f, -0.3804f, -0.8157f},
    {12.0f * 60.0f, 0.0f, -0.75f},
    {(float)VOXEL_SUN_REF_MINUTE, VOXEL_SUN_REF_DX, VOXEL_SUN_REF_DZ},
    {16.0f * 60.0f, 0.9011f, -0.6309f},
    {18.0f * 60.0f, SUN_WEST_DX, SUN_LOW_DZ},
    {20.0f * 60.0f, SUN_WEST_DX, SUN_LOW_DZ},
    {20.5f * 60.0f, VOXEL_SUN_REF_DX, VOXEL_SUN_REF_DZ},
};
#define SUN_KEYS (sizeof(kSun) / sizeof(kSun[0]))

/*
 * The light's colour over the day, as multipliers on the weather's light. Day (09:00-16:30) is
 * the identity, so the whole middle of the day, and L3's reference time in it, is the picture
 * as it was. Night is a dim blue moonlight with a cool shade close to it (so the shadows are
 * faint), no glow; dawn a pink-orange; the evening a golden hour, then a red dusk.
 */
typedef struct
{
    float minute;
    VoxelDayTint tint;
} TintKey;

#define NIGHT {{0.46f, 0.52f, 0.76f}, {0.40f, 0.46f, 0.70f}, {0.20f, 0.25f, 0.42f}, 0.0f, 0.30f}
#define DAY {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, 1.0f, 1.0f}

static const TintKey kTint[] = {
    {4.75f * 60.0f, NIGHT},
    {5.75f * 60.0f, {{0.92f, 0.74f, 0.70f}, {0.62f, 0.62f, 0.82f}, {1.00f, 0.78f, 0.74f}, 0.35f, 0.60f}},
    {7.0f * 60.0f, {{1.03f, 0.95f, 0.86f}, {0.90f, 0.93f, 1.02f}, {1.00f, 0.94f, 0.88f}, 0.80f, 0.90f}},
    {9.0f * 60.0f, DAY},
    {16.5f * 60.0f, DAY},
    {18.0f * 60.0f, {{1.06f, 0.88f, 0.68f}, {0.82f, 0.80f, 0.94f}, {1.00f, 0.84f, 0.70f}, 0.85f, 0.90f}},
    {19.0f * 60.0f, {{0.84f, 0.58f, 0.54f}, {0.54f, 0.50f, 0.72f}, {0.74f, 0.52f, 0.54f}, 0.30f, 0.55f}},
    {20.0f * 60.0f, NIGHT},
};
#define TINT_KEYS (sizeof(kTint) / sizeof(kTint[0]))

float VoxelDaylight_Wrap(float minute)
{
    float m = minute;

    if (!(m == m))
        return 0.0f; /* NaN */
    while (m >= (float)VOXEL_DAY_MINUTES)
        m -= (float)VOXEL_DAY_MINUTES;
    while (m < 0.0f)
        m += (float)VOXEL_DAY_MINUTES;
    return m >= (float)VOXEL_DAY_MINUTES ? 0.0f : m;
}

/*
 * The keys either side of `m` on a wrapped table: a in [prev, next), with the span and how far
 * through it m is. At a key's own minute t is exactly 0, so the value there is the key's own,
 * bit for bit (a + (b - a) * 0 == a).
 */
static void Bracket(const float *minutes, size_t stride, size_t count, float m,
                    size_t *a, size_t *b, float *t)
{
    size_t i = count - 1u;
    float start, end;

    for (size_t k = 0; k < count; ++k)
    {
        if (*(const float *)((const char *)minutes + k * stride) <= m)
            i = k;
        else
            break;
    }
    *a = i;
    *b = (i + 1u) % count;
    start = *(const float *)((const char *)minutes + i * stride);
    end = *(const float *)((const char *)minutes + *b * stride);
    if (start > m)
        start -= (float)VOXEL_DAY_MINUTES; /* before the first key: the last key, yesterday */
    if (end <= start)
        end += (float)VOXEL_DAY_MINUTES;
    *t = (m - start) / (end - start);
}

static float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

VoxelSunDir VoxelDaylight_Sun(float minute)
{
    size_t a, b;
    float t;
    VoxelSunDir sun;

    Bracket(&kSun[0].minute, sizeof(SunKey), SUN_KEYS, VoxelDaylight_Wrap(minute), &a, &b, &t);
    sun.dx = Lerp(kSun[a].dx, kSun[b].dx, t);
    sun.dz = Lerp(kSun[a].dz, kSun[b].dz, t);
    return sun;
}

unsigned VoxelDaylight_Step(float minute)
{
    unsigned step = (unsigned)(VoxelDaylight_Wrap(minute) / (float)VOXEL_SUN_STEP_MINUTES);

    return step < VOXEL_SUN_STEPS ? step : VOXEL_SUN_STEPS - 1u;
}

VoxelSunDir VoxelDaylight_StepSun(unsigned step)
{
    return VoxelDaylight_Sun((float)((step % VOXEL_SUN_STEPS) * VOXEL_SUN_STEP_MINUTES));
}

VoxelDayTint VoxelDaylight_Tint(float minute)
{
    size_t a, b;
    float t;
    VoxelDayTint out;
    const VoxelDayTint *x, *y;

    Bracket(&kTint[0].minute, sizeof(TintKey), TINT_KEYS, VoxelDaylight_Wrap(minute), &a, &b, &t);
    x = &kTint[a].tint;
    y = &kTint[b].tint;
    for (int i = 0; i < 3; ++i)
    {
        out.sun[i] = Lerp(x->sun[i], y->sun[i], t);
        out.shade[i] = Lerp(x->shade[i], y->shade[i], t);
        out.haze[i] = Lerp(x->haze[i], y->haze[i], t);
    }
    out.glow = Lerp(x->glow, y->glow, t);
    out.shadow = Lerp(x->shadow, y->shadow, t);
    return out;
}

/* ── The override file ──────────────────────────────────────────────────── */

static const char *Space(const char *s)
{
    while (*s == ' ' || *s == '\t')
        ++s;
    return s;
}

static bool End(const char *s)
{
    s = Space(s);
    return *s == '\0' || *s == '\n' || *s == '\r';
}

bool VoxelDaylight_ParseOverride(const char *text, VoxelDayOverride *out)
{
    const char *s;
    char *end;
    long hours, minutes, speed = 0;

    out->mode = VOXEL_DAY_LIVE;
    out->minute = 0.0f;
    out->speed = 0.0f;
    if (text == NULL)
        return false;
    s = Space(text);
    if ((s[0] == 'o' || s[0] == 'O') && (s[1] == 'f' || s[1] == 'F') && (s[2] == 'f' || s[2] == 'F')
     && End(s + 3))
    {
        out->mode = VOXEL_DAY_OFF;
        return true;
    }
    if (*s < '0' || *s > '9')
        return false;
    hours = strtol(s, &end, 10);
    if (*end != ':' || hours < 0 || hours > 23)
        return false;
    s = end + 1;
    if (*s < '0' || *s > '9')
        return false;
    minutes = strtol(s, &end, 10);
    if (minutes < 0 || minutes > 59 || end - s != 2)
        return false;
    s = Space(end);
    if (*s == 'x' || *s == 'X')
    {
        ++s;
        if (*s < '0' || *s > '9')
            return false;
        speed = strtol(s, &end, 10);
        if (speed < 1 || speed > 3600)
            return false;
        s = end;
    }
    if (!End(s))
        return false;
    out->mode = VOXEL_DAY_FIXED;
    out->minute = (float)(hours * 60 + minutes);
    out->speed = (float)speed;
    return true;
}

float VoxelDaylight_OverrideMinute(const VoxelDayOverride *o, double seconds)
{
    double m = (double)o->minute + seconds / 60.0 * (double)o->speed;

    m -= (double)VOXEL_DAY_MINUTES * (double)(long long)(m / (double)VOXEL_DAY_MINUTES);
    return VoxelDaylight_Wrap((float)m);
}

/* ── The re-bake pace ───────────────────────────────────────────────────── */

void VoxelRebake_Init(VoxelRebakePacer *p, unsigned cap)
{
    p->frame = 0;
    p->started = 0;
    p->cap = cap;
}

bool VoxelRebake_Take(VoxelRebakePacer *p, uint32_t frame)
{
    if (frame != p->frame)
    {
        p->frame = frame;
        p->started = 0;
    }
    if (p->started >= p->cap)
        return false;
    ++p->started;
    return true;
}
