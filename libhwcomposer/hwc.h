/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C)2012-2013, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _HWC_H_
#define _HWC_H_

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include "hwc_copybit.h"
#define MAX_DISPLAYS            (HWC_NUM_DISPLAY_TYPES)

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

#define HWC_DEBUG		false

struct DisplayAttributes {
    uint32_t vsync_period; //nanos
    uint32_t xres;
    uint32_t yres;
    uint32_t stride;
    float xdpi;
    float ydpi;
    int fd;
    int fd_video;
    bool connected; //Applies only to pluggable disp.
    //Connected does not mean it ready to use.
    //It should be active also. (UNBLANKED)
    bool isActive;
    // In pause state, composition is bypassed
    // used for WFD displays only
    bool isPause;
};

struct VsyncState {
    bool enable;
    bool fakevsync;
};

struct hwc_context_t {
    hwc_composer_device_1_t device;
    /* our private state goes below here */
	const hwc_procs_t			*procs;
	struct DisplayAttributes	dpyAttr[MAX_DISPLAYS];
	struct VsyncState			vstate;

	CopyBit					*mCopyBit;
};

#define RK_FBIOSET_VSYNC_ENABLE     0x4629

extern int hwc_vsync_control(hwc_context_t* ctx, int dpy, int enable);
extern void init_vsync_thread(hwc_context_t* ctx);
extern void init_uevent_thread(hwc_context_t* ctx);
extern void dump_fps(void);
extern int hwc_overlay(hwc_context_t *ctx, int dpy, hwc_layer_1_t *Src);
extern int hwc_postfb(hwc_context_t *ctx, int dpy, hwc_layer_1_t *Src);
extern int hwc_yuv2rgb(hwc_context_t *ctx, hwc_layer_1_t *Src);
extern int openFramebufferDevice(hwc_context_t *ctx);
#endif //_HWC_H_
