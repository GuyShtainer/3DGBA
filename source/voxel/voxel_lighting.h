/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/src/voxel/voxel_lighting.h,
 * MIT License - see source/voxel/NOTICE.md. */
/* Fixed sun, baked into chunk colours; no extra terrain GPU pass. */
#ifndef CTR_VOXEL_LIGHTING_H
#define CTR_VOXEL_LIGHTING_H

#include "voxel_mesh_builder.h"
#include "voxel_daylight.h"

/* Standalone host consumers retain the original mesh unless explicitly enabled. */
#ifndef CTR_VOXEL_LIGHTING
#define CTR_VOXEL_LIGHTING 0
#endif

#if CTR_VOXEL_LIGHTING
/* THE sun. Every consumer (the ray march, the face term, the contact blob, an
 * object's cast shadow, the dapple) reads these two numbers and nothing else.
 *
 * The sun is in FRONT of the scene (south, the camera's side), high, and a
 * little to the west: it lies along (-DX, 1, -DZ) from a point, with +Z south.
 * DZ is therefore NEGATIVE and shadows travel (+DX, +DZ) = north and a touch
 * east: away from the camera, up the screen. The GBA sprites are drawn lit
 * from the front and above; this makes the baked terrain/building shadows
 * agree with them. Elevation: horizontal length 0.79 over height 1 = 52 deg.
 * A south wall gets 0.65 of the sun's face term, a west wall 0.45, a roof 1;
 * north and east walls take ambient. That vector is VOXEL_SUN_REF_DX/DZ
 * (voxel_daylight.h), the sun until something sets another.
 *
 * L4: the day cycle moves it in steps (ctr_voxel.c, VoxelLighting_SetSun) and
 * re-bakes the chunks. The people's cast shadows read the same baked sun every
 * frame, so they agree with the terrain's; the dapple and the dust follow the
 * day's continuous sun instead (ctr_voxel.c), so they never jump.
 *
 * Nothing assumes a sign: the march, the reach boxes and the hash derive
 * their extents from the signs of DX and DZ. */
extern float gVoxelSunDX, gVoxelSunDZ;
#define VOXEL_SUN_DX gVoxelSunDX
#define VOXEL_SUN_DZ gVoxelSunDZ
#define VOXEL_LIGHT_REACH 8
/* The light of what the sun does not reach: a cast shadow, and a face turned
 * away from the sun. The same number, so a wall and the shadow it casts on
 * the grass read as the same shade. */
#define VOXEL_AMBIENT 0.70f
/* Four clipped octagons, at most twelve vertices / ten triangles each. */
#define VOXEL_CONTACT_VERTICES 120u

/* Forgets every cached caster and sample. Required whenever the world they
 * were read from changes - a live tile, the set of maps or their origins -
 * and needed at no other time: the caches outlive builds and frames. */
void VoxelLighting_Reset(void);
/* The sun the bake uses from now on; forgets the caches (VoxelLighting_Reset),
 * whose answers were for the old one. Chunks baked before keep the old sun
 * until they are built again: the caller re-bakes them. */
void VoxelLighting_SetSun(float dx, float dz);
/* The chunk hash's margins: how far past a chunk, on each side, the casters
 * that shade it may stand. By default the current sun's. A sun that moves
 * must widen them to every direction it will take BEFORE any chunk is
 * hashed, so a chunk's hash reads the same tiles whichever sun is up. */
void VoxelLighting_WidenReach(float dx, float dz);
void VoxelLighting_Reach(int *west, int *east, int *north, int *south);
uint32_t VoxelLighting_Hash(int x0, int z0, int x1, int z1);
float VoxelLighting_Sample(float x, float y, float z);
/* Rays cast so far (samples not found in the cache), for the build logs. */
unsigned VoxelLighting_Rays(void);
/* The light a face gets from the sun for its facing alone, VOXEL_AMBIENT for
 * a face turned away. The normal need not be unit length. */
float VoxelLighting_Face(float nx, float ny, float nz);
void VoxelLighting_Quad(VoxelBuilder *builder, const VoxelVertex *a,
                         const VoxelVertex *b, const VoxelVertex *c,
                         const VoxelVertex *d);
/*
 * A modelled building's triangle. `drawnShade` is the face shade the model
 * was written with (voxel_building.py's SHADE_*): the side the face was drawn
 * for, since models are not wound consistently. It is replaced by the
 * sun's facing; models take no cast shadow.
 */
void VoxelLighting_ModelTri(VoxelBuilder *builder, const VoxelVertex *a,
                            const VoxelVertex *b, const VoxelVertex *c, float drawnShade);
void VoxelLighting_Contact(VoxelBuilder *builder, float x, float z);
#endif

#endif
