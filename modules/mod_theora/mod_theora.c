/* Theora module for BennuGD
 *
 *  This software is provided 'as-is', without any express or implied
 *  warranty. In no event will the authors be held liable for any damages
 *  arising from the use of this software.
 *
 *  Permission is granted to anyone to use this software for any purpose,
 *  including commercial applications, and to alter it and redistribute it
 *  freely, subject to the following restrictions:
 *
 *     1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software
 *     in a product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 *
 *     2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 *
 *     3. This notice may not be removed or altered from any source
 *     distribution.
*/


#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <assert.h>

#include <theoraplay.h>
#include <SDL.h>
#include <SDL_mixer.h>

/* BennuGD stuff */
#include <bgddl.h>
#include <xstrings.h>
#include <libgrbase.h>
#include <g_video.h>

struct ctx
{
    GRAPH *graph;
    THEORAPLAY_VideoFrame *frame;
    THEORAPLAY_AudioPacket *audio;
    THEORAPLAY_Decoder *decoder;
    Uint32 baseticks;
    Uint32 framems;
};

typedef struct AudioQueue
{
    const THEORAPLAY_AudioPacket *audio;
    int offset;
    struct AudioQueue *next;
} AudioQueue;

static volatile AudioQueue *audio_queue = NULL;
static volatile AudioQueue *audio_queue_tail = NULL;

struct ctx video;

int playing_video=0;

static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len)
{
    if(! playing_video) {
        return;
    }
    
    // !!! FIXME: this should refuse to play if item->playms is in the future.
    //const Uint32 now = SDL_GetTicks() - baseticks;
    Sint16 *dst = (Sint16 *) stream;
    int freq = (int *)userdata;
    
    while (audio_queue && (len > 0))
    {
        volatile AudioQueue *item = audio_queue;
        AudioQueue *next = item->next;
        const int channels = item->audio->channels;
        
        const float *src = item->audio->samples + (item->offset * channels);
        int cpy = (item->audio->frames - item->offset) * channels;
        int i;
        
        if (cpy > (len / sizeof (Sint16)))
            cpy = len / sizeof (Sint16);
        
        for (i = 0; i < cpy; i++)
        {
            const float val = *(src++);
            if (val < -1.0f)
                *(dst++) = -32768;
            else if (val > 1.0f)
                *(dst++) = 32767;
            else
                *(dst++) = (Sint16) (val * 32767.0f);
        } // for
        
        item->offset += (cpy / channels);
        len -= cpy * sizeof (Sint16);
        
        if (item->offset >= item->audio->frames)
        {
            THEORAPLAY_freeAudio(item->audio);
            free((void *) item);
            audio_queue = next;
        } // if
    } // while
    
    if (!audio_queue)
        audio_queue_tail = NULL;
    
    if (len > 0)
        memset(dst, '\0', len);
} // audio_callback


static void queue_audio(const THEORAPLAY_AudioPacket *audio)
{
    AudioQueue *item = (AudioQueue *) malloc(sizeof (AudioQueue));
    if (!item)
    {
        THEORAPLAY_freeAudio(audio);
        return;  // oh well.
    } // if
    
    item->audio = audio;
    item->offset = 0;
    item->next = NULL;
    
    SDL_LockAudio();
    if (audio_queue_tail)
        audio_queue_tail->next = item;
    else
        audio_queue = item;
    audio_queue_tail = item;
    SDL_UnlockAudio();
} // queue_audio

// Paint the current video frame onscreen, skipping those that we already missed
static int refresh_video()
{
    if(! playing_video) {
        return 0;
    }
        
    const Uint32 now = SDL_GetTicks() - video.baseticks;

    if (!video.frame) {
        video.frame = THEORAPLAY_getVideo(video.decoder);
    }
    
    // Play video frames when it's time.
    if (video.frame && (video.frame->playms <= now))
    {
        if ( video.framems && ((now - video.frame->playms) >= video.framems) )
        {
            // Skip frames to catch up, but keep track of the last one
            // in case we catch up to a series of dupe frames, which
            // means we'd have to draw that final frame and then wait for
            // more.
            const THEORAPLAY_VideoFrame *last = video.frame;
            while ((video.frame = THEORAPLAY_getVideo(video.decoder)) != NULL)
            {
                THEORAPLAY_freeVideo(last);
                last = video.frame;
                if ((now - video.frame->playms) < video.framems)
                    break;
            } // while
            
            if (!video.frame)
                video.frame = last;
        } // if
        
        if (!video.frame)  // do nothing; we're far behind and out of options.
        {
            static int warned = 0;
            if (!warned)
            {
                warned = 1;
                fprintf(stderr, "WARNING: Playback can't keep up!\n");
            } // if
        } // if        
        else
        {
            memcpy(video.graph->data, video.frame->pixels, video.graph->height * video.graph->pitch);
            // Mark the video GRAPH as dirty so that BennuGD redraws it
            video.graph->modified = 1;
            video.graph->info_flags &=~GI_CLEAN;
        }
        THEORAPLAY_freeVideo(video.frame);
        video.frame = NULL;
    } // if
    
    while ((video.audio = THEORAPLAY_getAudio(video.decoder)) != NULL)
        queue_audio(video.audio);

    
    return 0;
}

/* Checks wether the current video is being played */
static int video_is_playing() {
    return playing_video;
}


/*********************************************/
/* Plays the given video with theoraplay     */
/* Must be given:                            */
/*    filename to be played                  */
/*********************************************/
static int video_play(INSTANCE *my, int * params)
{
    int bpp;
    int freq;
    const int MAX_FRAMES = 30;

    // Get the current screen bpp
    bpp = screen->format->BitsPerPixel;

    /* Ensure we're not playing a video already */
    if(playing_video == 1)
        return -1;

    /* Video mode must've already been initialized (we depend on mod_video) */
	if(! scr_initialized) return (-1);
	
    /* Lock the video playback */
    playing_video = 1;
 
    /* Start the decoding, 8bpp not supported */
    switch (bpp) {
        case 16:
            video.decoder = THEORAPLAY_startDecodeFile(string_get(params[0]), MAX_FRAMES, THEORAPLAY_VIDFMT_RGB565);
            string_discard(params[0]);
            break;
        /* Just in case this ever gets supported in BennuGD */
        case 24:
            video.decoder = THEORAPLAY_startDecodeFile(string_get(params[0]), MAX_FRAMES, THEORAPLAY_VIDFMT_RGB);
            string_discard(params[0]);
            break;
        case 32:
            video.decoder = THEORAPLAY_startDecodeFile(string_get(params[0]), MAX_FRAMES, THEORAPLAY_VIDFMT_RGBA);
            string_discard(params[0]);
            break;
    }
    
    if (!video.decoder)
    {
        fprintf(stderr, "Failed to start decoding '%s'!\n", string_get(params[0]));
        string_discard(params[0]);
        return -1;
    }
    
    // Wait until the decoder has parsed out some basic truths from the
    //  file. In a video game, you could choose not to block on this, and
    //  instead do something else until the file is ready.
    while (!THEORAPLAY_isInitialized(video.decoder)) {
        SDL_Delay(10);
    }
    
    if(! THEORAPLAY_hasVideoStream(video.decoder)) {
        THEORAPLAY_stopDecode(video.decoder);
        return -1;
    }

    while (!video.frame || !video.audio) {
        if (!video.frame) video.frame = THEORAPLAY_getVideo(video.decoder);
        if (!video.audio) video.audio = THEORAPLAY_getAudio(video.decoder);
        SDL_Delay(10);
    }
    
    video.framems = (video.frame->fps == 0.0) ? 0 : ((Uint32) (1000.0 / video.frame->fps));
    
    // Create the graph holding the video surface
    video.graph = bitmap_new_syslib(video.frame->width, video.frame->height, bpp);
    THEORAPLAY_freeVideo(video.frame);
    video.frame = NULL;
    if(! video.graph) {
        THEORAPLAY_stopDecode(video.decoder);
        video.decoder = NULL;
    }
    
    Mix_QuerySpec(&freq, NULL, NULL);
    Mix_HookMusic(audio_callback, &freq);
    
    while (video.audio)
    {
        queue_audio(video.audio);
        video.audio = THEORAPLAY_getAudio(video.decoder);
    } // while
    
    video.baseticks = SDL_GetTicks();
    
    SDL_PauseAudio(0);
    
    playing_video = 1;
    
    return video.graph->code;
}

/* Stop the currently being played video and release theoraplay stuff */
static int video_stop(INSTANCE *my, int * params)
{
    if(! playing_video) {
        return 0;
    }

    if(video.graph) {
        grlib_unload_map(0, video.graph->code);
        video.graph = NULL;
    }
    
    if(video.decoder) {
        THEORAPLAY_stopDecode(video.decoder);
        video.decoder = NULL;
    }
    
    Mix_HookMusic(NULL, NULL);

    /* Release the video playback lock */
    playing_video = 0;        

    return 0;
}

/* Pause the currently playing video */
static int video_pause() {
    return 0;
}

DLSYSFUNCS __bgdexport( mod_theora, functions_exports )[] =
{
	{"VIDEO_PLAY"                  , "S"    , TYPE_DWORD , video_play       },
	{"VIDEO_STOP"                  , ""     , TYPE_DWORD , video_stop       },
    {"VIDEO_PAUSE"                 , ""     , TYPE_DWORD , video_pause      },
	{"VIDEO_IS_PLAYING"            , ""     , TYPE_DWORD , video_is_playing },
	{ NULL        , NULL , 0         , NULL              }
};
 
char * __bgdexport( mod_theora, modules_dependency )[] =
{
	"libgrbase",
	"libvideo",
    "mod_sound",
	NULL
};

void __bgdexport( mod_theora, module_initialize )()
{
    if ( !SDL_WasInit( SDL_INIT_AUDIO ) ) SDL_InitSubSystem( SDL_INIT_AUDIO );
    //if ( !audio_initialized ) sound_init();
}
 
void __bgdexport( mod_theora, module_finalize )()
{
    video_stop(NULL, NULL);
}

/* ----------------------------------------------------------------- */

/* Bigest priority first execute
 Lowest priority last execute */

HOOK __bgdexport( mod_theora, handler_hooks )[] =
{
    { 3000, refresh_video                     },
    {    0, NULL                              }
} ;
