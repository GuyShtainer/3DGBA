/* rg_kspecs.c -- the aggregator of the Kanto recipe files (3DGBA original work, GPLv3). SPEC section 5.3.
 *
 * Each town slice adds `source/romgen/rg_kspecs_<town>.c`, exporting `const RgSpec rg_kspecs_<town>[]` and
 * `const unsigned rg_kspecs_<town>_count`, and one RG_TOWN line below in story order. B0 ships the empty table: with no
 * recipe the device draws the fallback boxes, as before. */
#include "rg_kspecs.h"

#include <string.h>

#define RG_KSPECS_MAX 160u          /* the plan is about 77 models; headroom for variants */

typedef struct KTown { const RgSpec *rows; const unsigned *count; } KTown;

/* Story order (SPEC section 6): K1 pallet, K2 landmarks, K3 viridian, ... Each entry is added with its slice:
 *   extern const RgSpec rg_kspecs_pallet[]; extern const unsigned rg_kspecs_pallet_count;   and   {rg_kspecs_pallet, &rg_kspecs_pallet_count}, */
extern const RgSpec rg_kspecs_pallet[];
extern const unsigned rg_kspecs_pallet_count;
extern const RgSpec rg_kspecs_landmarks[];
extern const unsigned rg_kspecs_landmarks_count;
extern const RgSpec rg_kspecs_viridian[];
extern const unsigned rg_kspecs_viridian_count;
extern const RgSpec rg_kspecs_pewter[];
extern const unsigned rg_kspecs_pewter_count;
extern const RgSpec rg_kspecs_cerulean[];
extern const unsigned rg_kspecs_cerulean_count;
extern const RgSpec rg_kspecs_vermilion[];
extern const unsigned rg_kspecs_vermilion_count;
extern const RgSpec rg_kspecs_lavender[];
extern const unsigned rg_kspecs_lavender_count;
extern const RgSpec rg_kspecs_celadon[];
extern const unsigned rg_kspecs_celadon_count;
extern const RgSpec rg_kspecs_fuchsia[];
extern const unsigned rg_kspecs_fuchsia_count;

static const KTown sTowns[] = {
    {rg_kspecs_pallet, &rg_kspecs_pallet_count},   /* K1 */
    {rg_kspecs_landmarks, &rg_kspecs_landmarks_count},   /* K2 */
    {rg_kspecs_viridian, &rg_kspecs_viridian_count},   /* K3 */
    {rg_kspecs_pewter, &rg_kspecs_pewter_count},   /* K4 */
    {rg_kspecs_cerulean, &rg_kspecs_cerulean_count},   /* K5 */
    {rg_kspecs_vermilion, &rg_kspecs_vermilion_count},   /* K6 */
    {rg_kspecs_lavender, &rg_kspecs_lavender_count},   /* K7 */
    {rg_kspecs_celadon, &rg_kspecs_celadon_count},   /* K8 */
    {rg_kspecs_fuchsia, &rg_kspecs_fuchsia_count},   /* K9 */
};

static RgSpec sTable[RG_KSPECS_MAX];
static unsigned sCount;
static bool sBuilt;

const RgSpec *rg_kspecs_table(const GameProfile *prof, unsigned *n)
{
    *n = 0;
    if (prof == NULL || (prof->game != GP_FIRERED && prof->game != GP_LEAFGREEN))
        return NULL;
    if (!sBuilt) {
        unsigned i;

        sCount = 0;
        for (i = 0; i < sizeof(sTowns) / sizeof(sTowns[0]); i++) {
            if (sTowns[i].rows == NULL || sTowns[i].count == NULL)
                continue;
            if (sCount + *sTowns[i].count > RG_KSPECS_MAX)
                break;          /* a town that does not fit is dropped whole; the K slice raises RG_KSPECS_MAX */
            memcpy(sTable + sCount, sTowns[i].rows, *sTowns[i].count * sizeof(RgSpec));
            sCount += *sTowns[i].count;
        }
        sBuilt = true;
    }
    *n = sCount;
    return sTable;
}
