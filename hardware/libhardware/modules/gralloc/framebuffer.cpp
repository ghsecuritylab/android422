/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <sys/mman.h>

#include <dlfcn.h>

#include <cutils/ashmem.h>
#include <cutils/log.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#if HAVE_ANDROID_OS
#include <linux/fb.h>
#endif

#include "gralloc_priv.h"
#include "gr.h"

#define USE_LIBSDL 1


#ifdef USE_LIBSDL
#include <sys/ipc.h>
#include <sys/msg.h>
#include <linux/input.h>
#include "SDL/SDL.h" 
static SDL_Surface *screen = NULL;

static int msqid = 0;

#define MSGKEY 10088 
#endif

/*****************************************************************************/

// numbers of buffers for page flipping
#define NUM_BUFFERS 2


enum {
    PAGE_FLIP = 0x00000001,
    LOCKED = 0x00000002
};

struct fb_context_t {
    framebuffer_device_t  device;
};

#ifdef USE_LIBSDL

typedef enum TouchMode {
    kTouchDown = 0,
    kTouchUp = 1,
    kTouchDrag = 2
} TouchMode;


static int sendAbsMovement(int x, int y)
{
    struct input_event iev;
    ssize_t actual;
    int ret;

    ALOGV("absMove x=%d y=%d\n", x, y);

    gettimeofday(&iev.time, NULL);
    iev.type = EV_ABS;
    iev.code = ABS_X;
    iev.value = x;

    ret = msgsnd(msqid, &iev, sizeof(iev), 0);
    if(ret < 0){
    	ALOGV("sendAbsMovement: send abs_x error");
    }

    iev.code = ABS_Y;
    iev.value = y;

    ret = msgsnd(msqid, &iev, sizeof(iev), 0);
    if(ret < 0){
    	ALOGV("sendAbsMovement: send abs_y error");
    }

    return 0;
}

static int sendAbsButton(int x, int y, int isDown)
{
    struct input_event iev;
    ssize_t actual;
    int ret;

    ALOGV("absButton x=%d y=%d down=%d\n", x, y, isDown);

    gettimeofday(&iev.time, NULL);
    iev.type = EV_KEY;
    iev.code = BTN_TOUCH;
    iev.value = (isDown != 0) ? 1 : 0;

    ret = msgsnd(msqid, &iev, sizeof(iev), 0);
    if(ret < 0){
    	ALOGV("sendAbsMovement: send ev_key error");
    }

    return 0;
}

static int sendAbsSyn(void)
{
    struct input_event iev;
    ssize_t actual;
    int ret;

    gettimeofday(&iev.time, NULL);
    iev.type = EV_SYN;
    iev.code = 0;
    iev.value = 0;

    ret = msgsnd(msqid, &iev, sizeof(iev), 0);
    if(ret < 0){
    	ALOGV("sendAbsMovement: send ev_syn error");
    }

    return 0;
}


void wsSendSimTouchEvent(int action, int x, int y)
{
    if (action == kTouchDown) {
        sendAbsMovement(x, y);
        sendAbsButton(x, y, 1);
        sendAbsSyn();
    } else if (action == kTouchUp) {
        sendAbsButton( x, y, 0);
        sendAbsSyn();
    } else if (action == kTouchDrag) {
        sendAbsMovement( x, y);
        sendAbsSyn();
    } else {
        ALOGV("WARNING: unexpected sim touch action  %d\n", action);
    }
}

void wsSendSimKeyEvent(int key, int isDown)
{
    struct input_event iev;
    ssize_t actual;
    int ret;

    gettimeofday(&iev.time, NULL);
    iev.type = EV_KEY;
    iev.code = key;
    iev.value = (isDown != 0) ? 1 : 0;
    	
    	ret = msgsnd(msqid, &iev, sizeof(iev), 0);
    if(ret < 0){
    	ALOGV("sendAbsMovement: send abs_x error");
    }

    return 0;
}

static void* fbThreadEntry(void* arg)
{
	SDL_Event event;
	while(1){
		if(SDL_PollEvent(&event)>0)
		{
				switch(event.type){
					case SDL_QUIT: exit(0); break;
					case SDL_MOUSEBUTTONDOWN:
						wsSendSimTouchEvent(kTouchDown, event.button.x, event.button.y);
						break;
					case SDL_MOUSEBUTTONUP:
						wsSendSimTouchEvent(kTouchUp, event.button.x, event.button.y);
						break;
					case SDL_MOUSEMOTION:
						wsSendSimTouchEvent(kTouchDrag, event.motion.x, event.motion.y);
						break;
					case SDL_KEYDOWN:
						wsSendSimKeyEvent(event.key.keysym.sym, 1);
						break;
					case SDL_KEYUP:
						wsSendSimKeyEvent(event.key.keysym.sym, 0);
						break;
					default:
						break;
					}
		}
		usleep(1000);
	}
}
#endif

/*****************************************************************************/

static int fb_setSwapInterval(struct framebuffer_device_t* dev,
            int interval)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if (interval < dev->minSwapInterval || interval > dev->maxSwapInterval)
        return -EINVAL;
    // FIXME: implement fb_setSwapInterval
    return 0;
}

static int fb_setUpdateRect(struct framebuffer_device_t* dev,
        int l, int t, int w, int h)
{
    if (((w|h) <= 0) || ((l|t)<0))
        return -EINVAL;
        
    fb_context_t* ctx = (fb_context_t*)dev;
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);
    m->info.reserved[0] = 0x54445055; // "UPDT";
    m->info.reserved[1] = (uint16_t)l | ((uint32_t)t << 16);
    m->info.reserved[2] = (uint16_t)(l+w) | ((uint32_t)(t+h) << 16);
    return 0;
}

static int fb_post(struct framebuffer_device_t* dev, buffer_handle_t buffer)
{
    if (private_handle_t::validate(buffer) < 0)
        return -EINVAL;
		
		printf("fb_post\n");
    fb_context_t* ctx = (fb_context_t*)dev;

    private_handle_t const* hnd = reinterpret_cast<private_handle_t const*>(buffer);
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);

    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
        const size_t offset = hnd->base - m->framebuffer->base;
        m->info.activate = FB_ACTIVATE_VBL;
        m->info.yoffset = offset / m->finfo.line_length;
        if (ioctl(m->framebuffer->fd, FBIOPUT_VSCREENINFO, &m->info) == -1) {
            ALOGE("FBIOPUT_VSCREENINFO failed");
            m->base.unlock(&m->base, buffer); 
            return -errno;
        }
        m->currentBuffer = buffer;
        
    } else {
        // If we can't do the page_flip, just copy the buffer to the front 
        // FIXME: use copybit HAL instead of memcpy
        
        void* fb_vaddr;
        void* buffer_vaddr;
        
        m->base.lock(&m->base, m->framebuffer, 
                GRALLOC_USAGE_SW_WRITE_RARELY, 
                0, 0, m->info.xres, m->info.yres,
                &fb_vaddr);

        m->base.lock(&m->base, buffer, 
                GRALLOC_USAGE_SW_READ_RARELY, 
                0, 0, m->info.xres, m->info.yres,
                &buffer_vaddr);

#ifdef USE_LIBSDL
				memcpy(screen->pixels, buffer_vaddr, m->finfo.line_length * m->info.yres);
				SDL_Flip( screen );
#else
        memcpy(fb_vaddr, buffer_vaddr, m->finfo.line_length * m->info.yres);
#endif
        m->base.unlock(&m->base, buffer); 
        m->base.unlock(&m->base, m->framebuffer); 
    }
    
    return 0;
}

/*****************************************************************************/

int mapFrameBufferLocked(struct private_module_t* module)
{
    // already initialized...
    if (module->framebuffer) {
        return 0;
    }
        
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo info;

    info.reserved[0] = 0;
    info.reserved[1] = 0;
    info.reserved[2] = 0;
    info.xoffset = 0;
    info.yoffset = 0;
    info.activate = FB_ACTIVATE_NOW;

#ifdef USE_LIBSDL
		pthread_attr_t threadAttr;
    pthread_t threadHandle;
    int cc;
        
    if (SDL_Init (SDL_INIT_VIDEO) < 0) {
    	ALOGW("SDL_Init error!");
    	return -1;
    }
    atexit (SDL_Quit);
    
    screen = SDL_SetVideoMode (480, 800, 32, SDL_SWSURFACE |SDL_DOUBLEBUF);   
    if (screen == NULL)   
    {   
       ALOGW ("SDL_SetVideoMode: error %s\n", SDL_GetError ());   
       return -1;  
    }
    SDL_WM_SetCaption ("android", NULL);
#if 0
    pthread_attr_init(&threadAttr);
			pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED);
			cc = pthread_create(&threadHandle, &threadAttr, fbThreadEntry, NULL);
			if (cc != 0) {
            ALOGW("Unable to create new thread: %s\n", strerror(errno));
            SDL_Quit();
            return -1;
			}
			

			msqid=msgget(MSGKEY,IPC_EXCL);
			if(msqid < 0) {
				ALOGW("msgq %d do not create, create it", msqid);
				msqid = msgget(MSGKEY, IPC_CREAT|0666);
				if(msqid < 0){
					ALOGW("msgq create error %d", msqid);
					SDL_Quit();
					return -1;
				}
			}
#endif
#endif
    info.bits_per_pixel = 32;//screen->format->BytesPerPixel*8;
		
		info.yres = 800;
		info.xres = 480;
		info.width = 0;
		info.height = 0;
    /*
     * Request NUM_BUFFERS screens (at lest 2 for page flipping)
     */
    info.yres_virtual = info.yres * NUM_BUFFERS;
		
		finfo.line_length = info.xres * 4;//screen->format->BytesPerPixel;

    uint32_t flags = PAGE_FLIP;
		info.yres_virtual = info.yres;
		flags &= ~PAGE_FLIP;
		ALOGW("page flipping not supported (yres_virtual=%d, requested=%d)",
                info.yres_virtual, info.yres*2);

//		refreshRate = 60*1000;  // 60 Hz

    if (int(info.width) <= 0 || int(info.height) <= 0) {
        // the driver doesn't return that information
        // default to 160 dpi
        info.width  = ((info.xres * 25.4f)/160.0f + 0.5f);
        info.height = ((info.yres * 25.4f)/160.0f + 0.5f);
    }

    float xdpi = (info.xres * 25.4f) / info.width;
    float ydpi = (info.yres * 25.4f) / info.height;
    float fps  = 60.0f;

    ALOGI(   "using \n"
            "id           = %s\n"
            "xres         = %d px\n"
            "yres         = %d px\n"
            "xres_virtual = %d px\n"
            "yres_virtual = %d px\n"
            "bpp          = %d\n"
            "r            = %2u:%u\n"
            "g            = %2u:%u\n"
            "b            = %2u:%u\n",
            finfo.id,
            info.xres,
            info.yres,
            info.xres_virtual,
            info.yres_virtual,
            info.bits_per_pixel,
            info.red.offset, info.red.length,
            info.green.offset, info.green.length,
            info.blue.offset, info.blue.length
    );

    ALOGI(   "width        = %d mm (%f dpi)\n"
            "height       = %d mm (%f dpi)\n"
            "refresh rate = %.2f Hz\n",
            info.width,  xdpi,
            info.height, ydpi,
            fps
    );

    module->flags = flags;
    module->info = info;
    module->finfo = finfo;
    module->xdpi = xdpi;
    module->ydpi = ydpi;
    module->fps = fps;

    /*
     * map the framebuffer
     */

    int err;
    size_t fbSize = roundUpToPageSize(finfo.line_length * info.yres_virtual);
    module->framebuffer = new private_handle_t(0, fbSize, 0);

    module->numBuffers = info.yres_virtual / info.yres;
    module->bufferMask = 0;

    void* vaddr = malloc(fbSize);
    if (vaddr == NULL) {
        ALOGE("Error mapping the framebuffer (%s)", strerror(errno));
        return -errno;
    }
    module->framebuffer->base = intptr_t(vaddr);
    memset(vaddr, 0, fbSize);
    return 0;
}

static int mapFrameBuffer(struct private_module_t* module)
{
    pthread_mutex_lock(&module->lock);
    int err = mapFrameBufferLocked(module);
    pthread_mutex_unlock(&module->lock);
    return err;
}

/*****************************************************************************/

static int fb_close(struct hw_device_t *dev)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if (ctx) {
        free(ctx);
    }
    return 0;
}

int fb_device_open(hw_module_t const* module, const char* name,
        hw_device_t** device)
{
    int status = -EINVAL;
    if (!strcmp(name, GRALLOC_HARDWARE_FB0)) {
        /* initialize our state here */
        fb_context_t *dev = (fb_context_t*)malloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = fb_close;
        dev->device.setSwapInterval = fb_setSwapInterval;
        dev->device.post            = fb_post;
        dev->device.setUpdateRect = 0;

        private_module_t* m = (private_module_t*)module;
        status = mapFrameBuffer(m);
        if (status >= 0) {
            int stride = m->finfo.line_length / (m->info.bits_per_pixel >> 3);
            int format = (m->info.bits_per_pixel == 32)
                         ? HAL_PIXEL_FORMAT_RGBX_8888
                         : HAL_PIXEL_FORMAT_RGB_565;
            const_cast<uint32_t&>(dev->device.flags) = 0;
            const_cast<uint32_t&>(dev->device.width) = m->info.xres;
            const_cast<uint32_t&>(dev->device.height) = m->info.yres;
            const_cast<int&>(dev->device.stride) = stride;
            const_cast<int&>(dev->device.format) = format;
            const_cast<float&>(dev->device.xdpi) = m->xdpi;
            const_cast<float&>(dev->device.ydpi) = m->ydpi;
            const_cast<float&>(dev->device.fps) = m->fps;
            const_cast<int&>(dev->device.minSwapInterval) = 1;
            const_cast<int&>(dev->device.maxSwapInterval) = 1;
            *device = &dev->device.common;
        }
    }
    return status;
}
