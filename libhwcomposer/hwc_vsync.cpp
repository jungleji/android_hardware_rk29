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


#define HWC_VSYNC_THREAD_NAME "hwcVsyncThread"

int hwc_vsync_control(hwc_context_t* ctx, int dpy, int enable)
{
    int ret = 0;
    if(!ctx->vstate.fakevsync &&
       ioctl(ctx->dpyAttr[dpy].fd, RK_FBIOSET_VSYNC_ENABLE,
             &enable) < 0) {
        ALOGE("%s: vsync control failed. Dpy=%d, enable=%d : %s",
              __FUNCTION__, dpy, enable, strerror(errno));
        ret = -errno;
    }
    return ret;
}

static void *vsync_loop(void *param)
{
    const char* vsync_timestamp_fb0 = "/sys/class/graphics/fb0/vsync";
    int dpy = HWC_DISPLAY_PRIMARY;

    hwc_context_t * ctx = reinterpret_cast<hwc_context_t *>(param);

    char thread_name[64] = HWC_VSYNC_THREAD_NAME;
    prctl(PR_SET_NAME, (unsigned long) &thread_name, 0, 0, 0);
    setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);

    const int MAX_DATA = 64;
    static char vdata[MAX_DATA];

    uint64_t cur_timestamp=0;
    ssize_t len = -1;
    int fd_timestamp = -1;
    int ret = 0;
    bool logvsync = false;

    char property[PROPERTY_VALUE_MAX];
    if(property_get("debug.hwc.fakevsync", property, NULL) > 0) {
        if(atoi(property) == 1)
            ctx->vstate.fakevsync = true;
    }

    if(property_get("debug.hwc.logvsync", property, 0) > 0) {
        if(atoi(property) == 1)
            logvsync = true;
    }

    /* Currently read vsync timestamp from drivers
       e.g. VSYNC=41800875994
       */
    fd_timestamp = open(vsync_timestamp_fb0, O_RDONLY);
    if (fd_timestamp < 0) {
        // Make sure fb device is opened before starting this thread so this
        // never happens.
        ALOGE ("FATAL:%s:not able to open file:%s, %s",  __FUNCTION__,
               vsync_timestamp_fb0,
               strerror(errno));
        ctx->vstate.fakevsync = true;
    }

	struct pollfd fds[1];
    fds[0].fd = fd_timestamp;
    fds[0].events = POLLPRI;

    do {
        if (LIKELY(!ctx->vstate.fakevsync)) {
        	int err = poll(fds, 1, -1);
	        if (err > 0) {
	            if (fds[0].revents & POLLPRI) {
	                len = pread(fd_timestamp, vdata, MAX_DATA, 0);
		            if (len < 0) {
		                // If the read was just interrupted - it is not a fatal error
		                // In either case, just continue.
		                if (errno != EAGAIN &&
		                    errno != EINTR  &&
		                    errno != EBUSY) {
		                    ALOGE ("FATAL:%s:not able to read file:%s, %s",
		                           __FUNCTION__,
		                           vsync_timestamp_fb0, strerror(errno));
		                }
		                continue;
		            }
		            // extract timestamp
		            const char *str = vdata;
		            cur_timestamp = strtoull(str, NULL, 0);
	            }
	        }
	        else if (err == -1) {
	            if (errno == EINTR)
	                break;
	            ALOGE("error in vsync thread: %s", strerror(errno));
        	}
        } else {
            usleep(16666);
            cur_timestamp = systemTime();
        }
        // send timestamp to HAL
        if(ctx->vstate.enable) {
            ALOGD_IF (logvsync, "%s: timestamp %llu sent to HWC for %s",
                      __FUNCTION__, cur_timestamp, "fb0");
            ctx->procs->vsync(ctx->procs, dpy, cur_timestamp);
        }

    } while (true);
    if(fd_timestamp >= 0)
        close (fd_timestamp);

    return NULL;
}

void init_vsync_thread(hwc_context_t* ctx)
{
    int ret;
    pthread_t vsync_thread;
    ALOGI("Initializing VSYNC Thread");
    ret = pthread_create(&vsync_thread, NULL, vsync_loop, (void*) ctx);
    if (ret) {
        ALOGE("%s: failed to create %s: %s", __FUNCTION__,
              HWC_VSYNC_THREAD_NAME, strerror(ret));
    }
}

