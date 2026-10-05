/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/src/voxel/voxel_file.h,
 * MIT License - see source/voxel/NOTICE.md. */
#ifndef VOXEL_FILE_H
#define VOXEL_FILE_H

/*
 * How the voxel modules open their generated data. On the console every file
 * is game data and goes through the data backend; the host tests build with
 * VOXEL_HOST_FILES and read the files the generators wrote.
 */

#include <stdio.h>

#ifdef VOXEL_HOST_FILES
#define VoxelFile_Open(path) fopen((path), "rb")
#else
#include "vx_data.h"
#define VoxelFile_Open(path) CtrData_Open(path)
#endif

#endif
