

#ifndef __VPU_MEM_H__
#define __VPU_MEM_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "vpu_type.h"
/* Linear memory area descriptor */

typedef struct VPUMem
{
    RK_U32  phy_addr;
    RK_U32  *vir_addr;
    RK_U32  size;
    RK_S32  offset;
#ifdef RVDS_PROGRAME
    RK_U8  *pbase;
#endif
} VPUMemLinear_t;

#define VPU_MEM_IS_NULL(p)          ((p)->offset < 0)

/* SW/HW shared memory */
RK_S32 VPUMallocLinear(VPUMemLinear_t *p, RK_U32 size);
RK_S32 VPUFreeLinear(VPUMemLinear_t *p);
RK_S32 VPUMemDuplicate(VPUMemLinear_t *dst, VPUMemLinear_t *src);
RK_S32 VPUMemLink(VPUMemLinear_t *p);
RK_S32 VPUMemFlush(VPUMemLinear_t *p);
RK_S32 VPUMemClean(VPUMemLinear_t *p);
RK_S32 VPUMemInvalidate(VPUMemLinear_t *p);
RK_U32 *VPUMemVirtual(VPUMemLinear_t *p);

#ifdef __cplusplus
}

#endif

#endif /* __VPU_MEM_H__ */

