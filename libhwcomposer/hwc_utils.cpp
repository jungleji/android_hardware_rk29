/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.

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

#include <cutils/properties.h>
#include <utils/Log.h>
#include <utils/Timers.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <poll.h>
#include "hwc.h"
#include "../libgralloc_ump/gralloc_priv.h"
#include "../libon2/vpu_global.h"

#define RK_FBIOSET_OVERLAY_STATE     	0x5018
#define RK_FBIOSET_YUV_ADDR				0x5002

void dump_fps(void) {
	
	char property[PROPERTY_VALUE_MAX];
	if (property_get("debug.hwc.logfps", property, "0") && atoi(property) > 0) {
		static int mFrameCount;
	    static int mLastFrameCount = 0;
	    static nsecs_t mLastFpsTime = 0;
	    static float mFps = 0;
	    mFrameCount++;
	    nsecs_t now = systemTime();
	    nsecs_t diff = now - mLastFpsTime;
	    if (diff > ms2ns(500)) {
	        mFps =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
	        mLastFpsTime = now;
	        mLastFrameCount = mFrameCount;
	        ALOGD("---mFps = %2.3f", mFps);
	    }
	    // XXX: mFPS has the value we want
	}
}

int openFramebufferDevice(hwc_context_t *ctx)
{
	struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo info;

    int fb_fd = open("/dev/graphics/fb0", O_RDWR, 0);

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &info) == -1)
        return -errno;

    if (int(info.width) <= 0 || int(info.height) <= 0) {
        // the driver doesn't return that information
        // default to 160 dpi
        info.width  = ((info.xres * 25.4f)/160.0f + 0.5f);
        info.height = ((info.yres * 25.4f)/160.0f + 0.5f);
    }

    float xdpi = (info.xres * 25.4f) / info.width;
    float ydpi = (info.yres * 25.4f) / info.height;

    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) == -1)
        return -errno;

    if (finfo.smem_len <= 0)
        return -errno;

    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].fd = fb_fd;
    //xres, yres may not be 32 aligned
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].stride = finfo.line_length;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres = info.xres;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres = info.yres;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xdpi = xdpi;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].ydpi = ydpi;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period = 1000000000l / 60;

    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].isActive = true;
    
    ctx->mCopyBit = new CopyBit();
    
	//Enable overlay mode
	char property[PROPERTY_VALUE_MAX];
	int overlay;
	if (property_get("video.use.overlay", property, "0") && atoi(property) > 0) {
		overlay = 1;
		ioctl(fb_fd, RK_FBIOSET_OVERLAY_STATE, &overlay);
		// If sys.ui.fakesize is not defined, default set to 1280x720
		if(property_get("sys.ui.fakesize", property, NULL) <= 0) {
			ALOGD("set default fake ui size 1280x720");
			property_set("sys.ui.fakesize", "1280x720");
		}
		property_set("sys.hwc.compose_policy", "0");
	}
	else {
		property_set("sys.yuv.rgb.format", "1");
		overlay = 0;
		ioctl(fb_fd, RK_FBIOSET_OVERLAY_STATE, &overlay);
	}
    return 0;
}

static unsigned int videodata[2];

int hwc_overlay(hwc_context_t *ctx, int dpy, hwc_layer_1_t *Src)
{	
	struct private_handle_t* srchnd = (struct private_handle_t *) Src->handle;
	struct tVPU_FRAME *pFrame  = NULL;	
	hwc_rect_t * DstRect = &(Src->displayFrame);
	int enable;
	
	ALOGD_IF(HWC_DEBUG, "%s format %x width %d height %d address 0x%x", __FUNCTION__, srchnd->format, srchnd->width, srchnd->height, srchnd->base);

	if(srchnd->format == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO) {
		pFrame = (tVPU_FRAME *)srchnd->base;
		ALOGD_IF(HWC_DEBUG, "%s video Frame addr=%x,FrameWidth=%d,FrameHeight=%d DisplayWidth=%d, DisplayHeight=%d",
		 __FUNCTION__, pFrame->FrameBusAddr[0], pFrame->FrameWidth, pFrame->FrameHeight, pFrame->DisplayWidth, pFrame->DisplayHeight);
	}
	else
		return 0;
	
	ALOGD_IF(HWC_DEBUG, "%s DstRect %d %d %d %d", __FUNCTION__, DstRect->left, DstRect->top, DstRect->right, DstRect->bottom);
	
	if(ctx->dpyAttr[dpy].fd_video <= 0) {
		ctx->dpyAttr[dpy].fd_video = open("/dev/graphics/fb1", O_RDWR, 0);
		if(ctx->dpyAttr[dpy].fd_video <= 0)
			return -1;
	}
	
	struct fb_var_screeninfo info;
	
	if (ioctl(ctx->dpyAttr[dpy].fd_video, FBIOGET_VSCREENINFO, &info) == -1)
	{
		ALOGE("%s(%d):  fd[%d] Failed", __FUNCTION__, __LINE__, ctx->dpyAttr[dpy].fd_video);
        return -1;
    }
    
	info.activate = FB_ACTIVATE_NOW;	
	info.nonstd &= 0x00;	
	info.nonstd |= HAL_PIXEL_FORMAT_YCrCb_NV12;
	info.grayscale &= 0xff;
	info.xoffset = 0;
	info.yoffset = 0;
	info.xres = pFrame->DisplayWidth;
	info.yres = pFrame->DisplayHeight;
	info.xres_virtual = pFrame->FrameWidth;
	info.yres_virtual = pFrame->FrameHeight;
	info.nonstd &= 0xff;
	info.nonstd |= (DstRect->left<<8) + (DstRect->top<<20);
	info.grayscale &= 0xff;
	info.grayscale |= ((DstRect->right - DstRect->left) << 8) + ((DstRect->bottom - DstRect->top) << 20);
	info.activate |= FB_ACTIVATE_FORCE;
//	info.rotate = ;
	/* Check yuv format. */
	if(videodata[0] != pFrame->FrameBusAddr[0]) {
		videodata[0] = pFrame->FrameBusAddr[0];
		videodata[1] = pFrame->FrameBusAddr[1];
		if (ioctl(ctx->dpyAttr[dpy].fd_video, RK_FBIOSET_YUV_ADDR, videodata) == -1)
		{	
	    	ALOGE("%s(%d):  fd[%d] Failed,DataAddr=%x", __FUNCTION__, __LINE__,ctx->dpyAttr[dpy].fd_video,videodata[0]);	
	    	return -errno;
		}
	}
	
	if (ioctl(ctx->dpyAttr[dpy].fd_video, FBIOPUT_VSCREENINFO, &info) == -1)
	    return -errno;
	
	return 0;
}

int hwc_postfb(hwc_context_t *ctx, int dpy, hwc_layer_1_t *Src)
{
	struct private_handle_t* srchnd = (struct private_handle_t *) Src->handle;	
	hwc_rect_t * DstRect = &(Src->displayFrame);
	
	if(dpy == 0 && srchnd) {
		ALOGD_IF(HWC_DEBUG, "%s format %x width %d height %d address 0x%x offset 0x%x", __FUNCTION__, srchnd->format, srchnd->width, srchnd->height, srchnd->base, srchnd->offset);
		
		struct fb_var_screeninfo info;
		if (ioctl(ctx->dpyAttr[dpy].fd, FBIOGET_VSCREENINFO, &info) == -1)
	    	return -errno;
	    info.yoffset = srchnd->offset/ctx->dpyAttr[dpy].stride;
		if (ioctl(ctx->dpyAttr[dpy].fd, FBIOPUT_VSCREENINFO, &info) == -1)
			return -errno;
	}
	return 0;
}

int hwc_yuv2rgb(hwc_context_t *ctx, hwc_layer_1_t *Src)
{
	struct private_handle_t* srchnd = (struct private_handle_t *) Src->handle;
	
	if(ctx->mCopyBit == NULL) {
		ALOGE("%s device not ready.", __FUNCTION__);
		return -1;
	}
	
	if(srchnd == NULL || srchnd->format != HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO) {
		ALOGE("%s no need to do color convert.", __FUNCTION__);
		return 0;
	}
	
	struct tVPU_FRAME *pFrame  = (tVPU_FRAME *)srchnd->base;
	ALOGD_IF(HWC_DEBUG, "%s video Frame addr=%x,FrameWidth=%u,FrameHeight=%u DisplayWidth=%u, DisplayHeight=%u",
	 __FUNCTION__, pFrame->FrameBusAddr[0], pFrame->FrameWidth, pFrame->FrameHeight, pFrame->DisplayWidth, pFrame->DisplayHeight);
	
	if(pFrame->FrameWidth > 3840 || pFrame->FrameHeight > 2160 || pFrame->FrameBusAddr[0] == 0xFFFFFFFF) {
		ALOGE("%s error parameter, cannot convert.", __FUNCTION__);
		return -1;
	}
	
//	memset((void*)srchnd->base, 0xFF, srchnd->width * srchnd->height * 4);
//	return 0;
	struct _rga_img_info_t src, dst;
	memset(&src, 0, sizeof(struct _rga_img_info_t));
	memset(&dst, 0, sizeof(struct _rga_img_info_t));

    src.yrgb_addr =  (int)pFrame->FrameBusAddr[0]+ 0x60000000;
    src.uv_addr  = src.yrgb_addr + ((pFrame->FrameWidth + 15)&(~15)) * ((pFrame->FrameHeight+ 15)&(~15));
    src.v_addr   = src.uv_addr;
    src.vir_w = (pFrame->FrameWidth + 15)&(~15);
    src.vir_h = (pFrame->FrameHeight + 15)&(~15);
    src.format = RK_FORMAT_YCbCr_420_SP;
  	src.act_w = pFrame->DisplayWidth;
    src.act_h = pFrame->DisplayHeight;
    src.x_offset = 0;
    src.y_offset = 0;
    
    dst.yrgb_addr = (uint32_t)srchnd->base;
    dst.uv_addr  = 0;
    dst.v_addr   = 0;
   	dst.vir_w = ((srchnd->width*2 + (8-1)) & ~(8-1))/2;
    dst.vir_h = srchnd->height;
    dst.format = RK_FORMAT_RGBA_8888;
	dst.act_w = pFrame->DisplayWidth;
	dst.act_h = pFrame->DisplayHeight;
	dst.x_offset = 0;
	dst.y_offset = 0;
	
	return ctx->mCopyBit->draw(&src, &dst, RK_MMU_ENABLE | RK_BT_601_MPEG);
}
