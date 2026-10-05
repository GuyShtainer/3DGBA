/* ctr_shims.c -- the ARM-only half of the voxel shims (3DGBA, GPLv3). The pure half lives in
 * ctr_shims_pure.c so the host tests link it without libctru. Builds only for the 3DS. */
#include <3ds.h>

#include "ctr_shims.h"

/* GX_BindQueue is wrapped (-Wl,--wrap=GX_BindQueue) to remember the queue the app binds; the
 * voxel budget probe then reads how many commands are already queued this frame. */
void __real_GX_BindQueue(gxCmdQueue_s *queue);

static gxCmdQueue_s *sQueue;

void __wrap_GX_BindQueue(gxCmdQueue_s *queue)
{
    sQueue = queue;
    __real_GX_BindQueue(queue);
}

static unsigned QueueEntries(void)
{
    return sQueue != NULL ? (unsigned)sQueue->numEntries : 0u;
}

void vx_gx_install_probe(void)
{
    vx_gx_set_probe(QueueEntries);
}
