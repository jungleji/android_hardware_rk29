/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <cutils/properties.h>
#include <EGL/egl.h>

#include "hwc.h"
#include "../libgralloc_ump/gralloc_priv.h"
/*****************************************************************************/

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "Hardware Composer Module",
        author: "RockChip Box Team",
        methods: &hwc_module_methods,
      	dso: 0,
        reserved: {0},
    }
};

/*****************************************************************************/

static void dump_layer(hwc_layer_1_t const* l) {
    ALOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
            l->compositionType, l->flags, l->handle, l->transform, l->blending,
            l->sourceCrop.left,
            l->sourceCrop.top,
            l->sourceCrop.right,
            l->sourceCrop.bottom,
            l->displayFrame.left,
            l->displayFrame.top,
            l->displayFrame.right,
            l->displayFrame.bottom);
}

static int hwc_prepare_primary(hwc_composer_device_1 *dev,
        hwc_display_contents_1_t *list) {
	
	hwc_context_t* ctx = (hwc_context_t*)(dev);
	
	for (uint32_t i = 0; i < list->numHwLayers; i++)
    {
        hwc_layer_1_t* layer = &(list->hwLayers[i]);
        struct private_handle_t *handle = (struct private_handle_t *) layer->handle;
        if(handle)
        	ALOGD_IF(HWC_DEBUG, "%s layer %d format %x", __FUNCTION__, i, handle->format);
//        dump_layer(layer);
        if(handle && handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO) {
        	char property[PROPERTY_VALUE_MAX];
        	memset(property, 0, PROPERTY_VALUE_MAX);
        	if(property_get("video.use.overlay", property, NULL) <= 0 || atoi(property) == 0)
        		hwc_yuv2rgb(ctx, layer);
        	else {
        		layer->compositionType = HWC_OVERLAY;
        		layer->hints |= HWC_HINT_CLEAR_FB;
        	}
        }
    }
    
    return 0;
}

static int hwc_prepare(hwc_composer_device_1_t *dev,
        size_t numDisplays, hwc_display_contents_1_t** displays) {
        	
     int ret;
     
     for (int32_t i = numDisplays - 1; i >= 0; i--) {
        hwc_display_contents_1_t *list = displays[i];
        switch(i) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_prepare_primary(dev, list);
                break;
//            case HWC_DISPLAY_EXTERNAL:
//                ret = hwc_prepare_external(dev, list, i);
//                break;
//            case HWC_DISPLAY_VIRTUAL:
//                ret = hwc_prepare_virtual(dev, list, i);
//                break;
            default:
                ret = -EINVAL;
        }
    }

    return 0;
}

static int hwc_set_primary(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
	const int dpy = HWC_DISPLAY_PRIMARY;
	int overlay_flag = 0;
	bool NeedSwap = false;
	int ret = 0;
	
	for (uint32_t i = 0; i < list->numHwLayers; i++)
    {
        if (list->hwLayers[i].acquireFenceFd >0 )
			sync_wait(list->hwLayers[i].acquireFenceFd, -1);
        switch (list->hwLayers[i].compositionType)
        {
	        case HWC_OVERLAY:
	            /* TODO: HANDLE OVERLAY LAYERS HERE. */
	            ALOGD_IF(HWC_DEBUG, "%s(%d):Layer %d is OVERLAY", __FUNCTION__, __LINE__, i);
                ret = hwc_overlay(ctx, dpy, &list->hwLayers[i]);
	            overlay_flag = 1;
	            break;
	
			case HWC_FRAMEBUFFER_TARGET:
				ret = hwc_postfb(ctx, dpy, &list->hwLayers[i]);
				break;
	        default:
	            ALOGD_IF(HWC_DEBUG, "%s(%d):Layer %d is FRAMEBUFFER", __FUNCTION__, __LINE__, i);
	            //NeedSwap = true;
	            break;
	    }
	    if (list->hwLayers[i].acquireFenceFd >0 )
			close(list->hwLayers[i].acquireFenceFd);
    }
    
    if(ctx->dpyAttr[dpy].isActive == 1 && overlay_flag && list->numHwLayers == 1)
	{
		//There is only one layer and this layet is overlay to win0.
		//So we disable win1 which is map to fb0
		ctx->dpyAttr[dpy].isActive = 0;
		ioctl(ctx->dpyAttr[dpy].fd, 0x5019, &(ctx->dpyAttr[dpy].isActive));
	}
	else if(ctx->dpyAttr[dpy].isActive == 0 && (overlay_flag == 0 || list->numHwLayers > 1) )
	{
		ctx->dpyAttr[dpy].isActive = 1;
		ioctl(ctx->dpyAttr[dpy].fd, 0x5019, &(ctx->dpyAttr[dpy].isActive));
	}
	
	if(!overlay_flag && ctx->dpyAttr[dpy].fd_video)
	{
		// Close video layer
		int enable = 0;
		ioctl(ctx->dpyAttr[dpy].fd_video, 0x5019, &enable);
		close(ctx->dpyAttr[dpy].fd_video);
		ctx->dpyAttr[dpy].fd_video = 0;
	}
	
	if (NeedSwap)
    {    	
        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)list->dpy,
            (EGLSurface)list->sur);
	    if (!sucess) {
	    	ALOGE("%s(%d):  eglSwapBuffers Failed", __FUNCTION__, __LINE__);	
	        ret = HWC_EGL_ERROR;
	    }
    }
    
    dump_fps();
    
    return ret;
}

static int hwc_set(hwc_composer_device_1_t *dev,
        size_t numDisplays, hwc_display_contents_1_t** displays)
{
    if (!numDisplays || !displays)
        return 0;

    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);

    for (uint32_t i = 0; i < numDisplays; i++) {
        hwc_display_contents_1_t* list = displays[i];
        switch(i) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_set_primary(ctx, list);
                break;
//            case HWC_DISPLAY_EXTERNAL:
//                ret = hwc_set_external(ctx, list, i);
//                break;
//            case HWC_DISPLAY_VIRTUAL:
//                ret = hwc_set_virtual(ctx, list, i);
//                break;
            default:
                ret = -EINVAL;
        }
    }
	
    return ret;
}

static int hwc_event_control(struct hwc_composer_device_1* dev,
        int dpy, int event, int enable)
{
	int ret = 0;
	
    hwc_context_t* ctx = (hwc_context_t*)(dev);
//    if(!ctx->dpyAttr[dpy].isActive) {
//        ALOGE("Display is blanked - Cannot %s vsync",
//              enable ? "enable" : "disable");
//        return -EINVAL;
//    }

    switch(event) {
        case HWC_EVENT_VSYNC:
            if (ctx->vstate.enable == enable)
                break;
            ret = hwc_vsync_control(ctx, dpy, enable);
            if(ret == 0)
                ctx->vstate.enable = !!enable;
            ALOGD_IF(HWC_DEBUG, "VSYNC state changed to %s",
                      (enable)?"ENABLED":"DISABLED");
            break;
        default:
            ret = -EINVAL;
    }
    return ret;
}

static int hwc_blank(struct hwc_composer_device_1 *dev, int dpy, int blank)
{
    // We're using an older method of screen blanking based on
    // early_suspend in the kernel.  No need to do anything here.
    return 0;
}

static int hwc_query(struct hwc_composer_device_1* dev,int what, int* value)
{

    return 0;
}

static void hwc_registerProcs(struct hwc_composer_device_1* dev,
            hwc_procs_t const* procs)
{
	struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
	ctx->procs = procs;
	
	// Now that we have the functions needed, kick off
    // the uevent & vsync threads
    init_uevent_thread(ctx);
    init_vsync_thread(ctx);
}

static int hwc_getDisplayConfigs(struct hwc_composer_device_1* dev, int disp,
        uint32_t* configs, size_t* numConfigs) {
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    //in 1.1 there is no way to choose a config, report as config id # 0
    //This config is passed to getDisplayAttributes. Ignore for now.
    switch(disp) {
        case HWC_DISPLAY_PRIMARY:
            if(*numConfigs > 0) {
                configs[0] = 0;
                *numConfigs = 1;
            }
            ret = 0; //NO_ERROR
            break;
        case HWC_DISPLAY_EXTERNAL:
            ret = -1; //Not connected
            if(ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].connected) {
                ret = 0; //NO_ERROR
                if(*numConfigs > 0) {
                    configs[0] = 0;
                    *numConfigs = 1;
                }
            }
            break;
    }
    return ret;
}

static int hwc_getDisplayAttributes(struct hwc_composer_device_1* dev, int disp,
        uint32_t config, const uint32_t* attributes, int32_t* values) {

    hwc_context_t* ctx = (hwc_context_t*)(dev);
    //If hotpluggable displays are inactive return error
    if(disp == HWC_DISPLAY_EXTERNAL && !ctx->dpyAttr[disp].connected) {
        return -1;
    }

    //From HWComposer
    static const uint32_t DISPLAY_ATTRIBUTES[] = {
        HWC_DISPLAY_VSYNC_PERIOD,
        HWC_DISPLAY_WIDTH,
        HWC_DISPLAY_HEIGHT,
        HWC_DISPLAY_DPI_X,
        HWC_DISPLAY_DPI_Y,
        HWC_DISPLAY_NO_ATTRIBUTE,
    };

    const int NUM_DISPLAY_ATTRIBUTES = (sizeof(DISPLAY_ATTRIBUTES) /
            sizeof(DISPLAY_ATTRIBUTES)[0]);

    for (size_t i = 0; i < NUM_DISPLAY_ATTRIBUTES - 1; i++) {
        switch (attributes[i]) {
        case HWC_DISPLAY_VSYNC_PERIOD:
            values[i] = ctx->dpyAttr[disp].vsync_period;
            ALOGD("%s disp = %d, vsync_period = %d",__FUNCTION__, disp,
                    ctx->dpyAttr[disp].vsync_period);
            break;
        case HWC_DISPLAY_WIDTH:
            values[i] = ctx->dpyAttr[disp].xres;
            ALOGD("%s disp = %d, width = %d",__FUNCTION__, disp,
                    ctx->dpyAttr[disp].xres);
            break;
        case HWC_DISPLAY_HEIGHT:
            values[i] = ctx->dpyAttr[disp].yres;
            ALOGD("%s disp = %d, height = %d",__FUNCTION__, disp,
                    ctx->dpyAttr[disp].yres);
            break;
        case HWC_DISPLAY_DPI_X:
            values[i] = (int32_t) (ctx->dpyAttr[disp].xdpi*1000.0);
            ALOGD("%s disp = %d, xdpi = %f",__FUNCTION__, disp,
                    ctx->dpyAttr[disp].xdpi);
            break;
        case HWC_DISPLAY_DPI_Y:
            values[i] = (int32_t) (ctx->dpyAttr[disp].ydpi*1000.0);
            ALOGD("%s disp = %d, ydpi = %f",__FUNCTION__, disp,
                    ctx->dpyAttr[disp].ydpi);
            break;
        default:
            ALOGE("Unknown display attribute %d",
                    attributes[i]);
            return -EINVAL;
        }
    }
    return 0;
}

static void hwc_dump(struct hwc_composer_device_1* dev, char *buff, int buff_len)
{

}

static int hwc_device_close(struct hw_device_t *dev)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (ctx) {
    	if(ctx->mCopyBit) {
    		delete ctx->mCopyBit;
    		ctx->mCopyBit = NULL;
        }
        if(ctx->dpyAttr[0].fd_video)
        	close(ctx->dpyAttr[0].fd_video);
        free(ctx);
        ctx = NULL;
    }
    return 0;
}

/*****************************************************************************/
static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int ret = 0;

    if (strcmp(name, HWC_HARDWARE_COMPOSER))
    	return -EINVAL;
    	
    struct hwc_context_t *dev;
    dev = (hwc_context_t*)malloc(sizeof(*dev));

    /* initialize our state here */
    memset(dev, 0, sizeof(*dev));
	//Initialize hwc context
    ret = openFramebufferDevice(dev);
    if(ret)
    	return ret;
    	
    /* initialize the procs */
    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = HWC_DEVICE_API_VERSION_1_3;
    dev->device.common.module = const_cast<hw_module_t*>(module);
    dev->device.common.close = hwc_device_close;

    dev->device.prepare = hwc_prepare;
    dev->device.set = hwc_set;
	dev->device.eventControl = hwc_event_control;
	dev->device.blank = hwc_blank;
	dev->device.query = hwc_query;
    dev->device.dump = hwc_dump;
	dev->device.registerProcs = hwc_registerProcs;
	dev->device.getDisplayConfigs = hwc_getDisplayConfigs;
    dev->device.getDisplayAttributes = hwc_getDisplayAttributes;
    *device = &dev->device.common;
	ALOGI("hwc box version open success.");
	return 0;
}
