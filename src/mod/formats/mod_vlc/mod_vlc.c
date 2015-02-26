/*
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2014, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * William King william.king@quentustech.com
 * Seven Du <dujinfang@gmail.com>
 *
 * mod_vlc.c -- VLC streaming module
 *
 * Examples:
 *
 * To playback from an audio source into a file:
 * File: vlc:///path/to/file
 * Stream: http://path.to.file.com:port/file.pls
 * Stream: vlc://ftp://path.to.file.com:port/file.mp3
 *
 * To stream from a call(channel) out to a remote destination:
 * vlc://#transcode{acodec=vorb,channels=1,samplerate=16000}:standard{access=http,mux=ogg,dst=:8080/thing.ogg}
 *
 * To playback a video file
 * play_video /tmp/test.mp4
 *
 * Notes: 
 *
 * Audio Requires at least libvlc version 1.2
 * Video Requires at least libvlc 2.0.2
 *
 */
#include <switch.h>
#include <vlc/vlc.h>
#include <vlc/libvlc_media_player.h>
#include <vlc/libvlc_events.h>

#define VLC_BUFFER_SIZE 65536

static char *vlc_file_supported_formats[SWITCH_MAX_CODECS] = { 0 };

typedef int  (*imem_get_t)(void *data, const char *cookie,
                           int64_t *dts, int64_t *pts, unsigned *flags,
                           size_t *, void **);
typedef void (*imem_release_t)(void *data, const char *cookie, size_t, void *);

/* Change value to -vvv for vlc related debug. Be careful since vlc is at least as verbose as FS about logging */
const char *vlc_args[] = {""};
// const char *vlc_args[] = {"--network-caching", "500"};

libvlc_instance_t *read_inst;
switch_endpoint_interface_t *vlc_endpoint_interface = NULL;

typedef struct vlc_frame_data_s {
	int64_t pts;
} vlc_frame_data_t;

struct vlc_file_context {
	libvlc_media_player_t *mp;
	libvlc_media_t *m;
	switch_file_handle_t fh;
	switch_memory_pool_t *pool;
	switch_buffer_t *audio_buffer;
	switch_mutex_t *audio_mutex;
	switch_thread_cond_t *cond;
	char *path;
	int samples;
	int playing;
	int samplerate;
	int channels;
	int err;
	int pts;
	libvlc_instance_t *inst_out;
	void *frame_buffer;
	switch_size_t frame_buffer_len;
};

typedef struct vlc_file_context vlc_file_context_t;

struct vlc_video_context {
	libvlc_media_player_t *mp;
	libvlc_media_t *m;
	switch_mutex_t *audio_mutex;
	switch_file_handle_t fh;
	switch_memory_pool_t *pool;
	switch_thread_cond_t *cond;
	switch_buffer_t *audio_buffer;
	switch_queue_t *video_queue;
	int playing;
	int ending;
	switch_mutex_t *video_mutex;

	switch_core_session_t *session;
	switch_channel_t *channel;
	switch_frame_t *aud_frame;
	switch_frame_t *vid_frame;
	uint8_t video_packet[SWITCH_RECOMMENDED_BUFFER_SIZE];
	void *raw_yuyv_data;
	switch_image_t *img;
	switch_payload_t pt;
	uint32_t seq;
	int width;
	int height;
	int force_width;
	int force_height;
	int channels;
	int samplerate;
	int samples;
	//int pts;
	void *video_frame_buffer;
	switch_size_t video_frame_buffer_len;
	void *audio_frame_buffer;
	switch_size_t audio_frame_buffer_len;
	switch_timer_t timer;
	int64_t pts;
};

typedef struct vlc_video_context vlc_video_context_t;

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_vlc_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_vlc_load);
SWITCH_MODULE_DEFINITION(mod_vlc, mod_vlc_load, mod_vlc_shutdown, NULL);

void yuyv_to_i420(uint8_t *pixels, void *out_buffer, int src_width, int src_height)
{
	uint8_t *Y, *U, *V;
	int h, w;

	Y = out_buffer;
	U = Y + src_width * src_height;
	V = U + (src_width * src_height>>2);

	for (h=0; h<src_height; ++h) {
		for (w=0; w<src_width; ++w) {
			Y[w] = pixels[2 * w];
			if (w % 2 == 0 && h % 2 == 0) {
				U[w / 2] = pixels[2*w + 1];
				V[w / 2] = pixels[2*w + 3];
			}
		}
		pixels = pixels + src_width * 2;
		Y = Y + src_width;
		if ( h % 2 == 0) {
			U = U + (src_width >> 1);
			V = V + (src_width >> 1);
		}
	}
}

static void vlc_mediaplayer_error_callback(const libvlc_event_t * event, void * data)
{
        vlc_file_context_t *context = (vlc_file_context_t *) data;
        int status = libvlc_media_get_state(context->m);
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Got a libvlc_MediaPlayerEncounteredError callback. mediaPlayer Status: %d\n", status);
        if (status == libvlc_Error) {
               context->err = 1;
               switch_thread_cond_signal(context->cond);
	     }
}
static void vlc_media_state_callback(const libvlc_event_t * event, void * data)
{
        vlc_file_context_t *context = (vlc_file_context_t *) data;
        int new_state = event->u.media_state_changed.new_state;

        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Got a libvlc_MediaStateChanged callback. New state: %d\n", new_state);
        if (new_state == libvlc_Ended || new_state == libvlc_Error) {
                switch_thread_cond_signal(context->cond);
        }
}



void vlc_auto_play_callback(void *data, const void *samples, unsigned count, int64_t pts) {

	vlc_file_context_t *context = (vlc_file_context_t *) data;

	switch_mutex_lock(context->audio_mutex);
	if (context->audio_buffer) {
		if (!switch_buffer_write(context->audio_buffer, samples, count * 2 * context->channels)) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Buffer error\n");
		}
	}

	if(!context->playing ) {
		context->playing = 1;
		switch_thread_cond_signal(context->cond);
	}
	switch_mutex_unlock(context->audio_mutex);
}

#define FORCE_SPLIT 1

void vlc_play_audio_callback(void *data, const void *samples, unsigned count, int64_t pts) {
	vlc_video_context_t *context = (vlc_video_context_t *) data;
	switch_size_t bytes;

	switch_mutex_lock(context->audio_mutex);

	bytes = switch_buffer_inuse(context->audio_buffer);
	if (bytes > 2000000) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Buffer overflow %d\n", (int)bytes);
		switch_buffer_zero(context->audio_buffer);
	}

	switch_buffer_write(context->audio_buffer, samples, count * 2 * context->channels);

	if (!context->playing) {
		context->playing = 1;
		if (context->cond) switch_thread_cond_signal(context->cond);
	}

	switch_mutex_unlock(context->audio_mutex);

	// switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "VLC callback, play audio: %d \n", count);
}

static void *vlc_video_lock_callback(void *data, void **p_pixels)
{
	vlc_video_context_t *context = (vlc_video_context_t *)data;

	switch_mutex_lock(context->video_mutex);
	if (!context->raw_yuyv_data) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "no yuyv data\n");
		return NULL;
	}
	*p_pixels = context->raw_yuyv_data;

	return NULL; /* picture identifier, not needed here */
}

static void vlc_video_unlock_callback(void *data, void *id, void *const *p_pixels)
{
	vlc_video_context_t *context = (vlc_video_context_t *) data;

	if (context->channel && !switch_channel_test_flag(context->channel, CF_VIDEO)) return;

	if (!context->img) context->img = switch_img_alloc(NULL, SWITCH_IMG_FMT_I420, context->width, context->height, 0);

	switch_assert(context->img);

	yuyv_to_i420(*p_pixels, context->img->img_data, context->width, context->height);
	

	switch_mutex_unlock(context->video_mutex);
}

static void vlc_video_channel_unlock_callback(void *data, void *id, void *const *p_pixels)
{
	vlc_video_context_t *context = (vlc_video_context_t *)data;

	switch_assert(id == NULL); /* picture identifier, not needed here */

	if (context->channel && !switch_channel_test_flag(context->channel, CF_VIDEO)) return;

	if (!context->img) context->img = switch_img_alloc(NULL, SWITCH_IMG_FMT_I420, context->width, context->height, 0); switch_assert(context->img);
	
	yuyv_to_i420(*p_pixels, context->img->img_data, context->width, context->height);

	switch_mutex_unlock(context->video_mutex);
}

static void vlc_video_display_callback(void *data, void *id)
{
	/* VLC wants to display the video */

	vlc_video_context_t *context = (vlc_video_context_t *) data;

	if (context->channel && !switch_channel_test_flag(context->channel, CF_VIDEO)) return;

	if (context->video_queue) {
		switch_queue_push(context->video_queue, context->img);
		context->img = NULL;
	} else {
		context->vid_frame->img = context->img;
		context->vid_frame->packet = context->video_packet;
		context->vid_frame->data = context->video_packet + 12;
		switch_core_session_write_video_frame(context->session, context->vid_frame, SWITCH_IO_FLAG_NONE, 0);
	}

	//do we need this or not
	//switch_img_free(&context->img);

}

unsigned video_format_setup_callback(void **opaque, char *chroma, unsigned *width, unsigned *height, unsigned *pitches, unsigned *lines)
{

	vlc_video_context_t *context = (vlc_video_context_t *) (*opaque);
	unsigned frame_size;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "chroma: %s, width: %u, height: %u, pitches: %u, lines: %u\n", chroma, *width, *height, *pitches, *lines);

	/* You have to use YUYV here or it will crash */
	switch_set_string(chroma, "YUYV");

	if (context->force_width && context->force_height) { /* resize */
		*width = context->force_width;
		*height = context->force_height;
	}

	*pitches = (*width) * 2;
	*lines = (*height);

	context->width = *width;
	context->height = *height;

	frame_size = (*width) * (*height) * 4 * 2;
	context->raw_yuyv_data = malloc(frame_size);

	if (context->raw_yuyv_data == NULL) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "memory error\n");
		return 0;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "chroma: %s, width: %u, height: %u, pitches: %u, lines: %u\n", chroma, *width, *height, *pitches, *lines);

	return 1;
}

void video_format_clean_callback(void *opaque)
{

	vlc_video_context_t *context = (vlc_video_context_t *)opaque;
	switch_safe_free(context->raw_yuyv_data);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "cleanup\n");
}

#if 0
static int i420_size(int width, int height)
{
	int half_width = (width + 1) >> 1;
	int half_height = (height + 1) >> 1;

	return width * height + half_width * half_height * 2;
}
#endif


int  vlc_imem_get_callback(void *data, const char *cookie, int64_t *dts, int64_t *pts, unsigned *flags, size_t *size, void **output)
{
	vlc_file_context_t *context = (vlc_file_context_t *) data;
	//int samples = 0;
	int bytes = 0, bread = 0, blen = 0;
	
	if (context->playing == 0) return -1;

	switch_mutex_lock(context->audio_mutex);

	blen = switch_buffer_inuse(context->audio_buffer);
	
	if (!(bytes = blen)) {
		*size = 0;
		*output = NULL;
		switch_mutex_unlock(context->audio_mutex);
		return 0;
	}


	if (context->frame_buffer_len < bytes) {
		context->frame_buffer_len = bytes;
		context->frame_buffer = switch_core_alloc(context->pool, context->frame_buffer_len);
	}

	*output = context->frame_buffer;

	bread = switch_buffer_read(context->audio_buffer, *output, bytes);

	switch_mutex_unlock(context->audio_mutex);
	
	*size = (size_t) bread;

	return 0;
}

void vlc_imem_release_callback(void *data, const char *cookie, size_t size, void *unknown)
{
	//printf("Free?? %p\n", unknown);
	//free(unknown);
}

static switch_status_t vlc_file_open(switch_file_handle_t *handle, const char *path)
{
	vlc_file_context_t *context;
	libvlc_event_manager_t *mp_event_manager, *m_event_manager;
	
	context = switch_core_alloc(handle->memory_pool, sizeof(*context));
	context->pool = handle->memory_pool;

	context->path = switch_core_strdup(context->pool, path);

	switch_buffer_create_dynamic(&(context->audio_buffer), VLC_BUFFER_SIZE, VLC_BUFFER_SIZE * 8, 0);
	switch_mutex_init(&context->audio_mutex, SWITCH_MUTEX_NESTED, context->pool);
	switch_thread_cond_create(&(context->cond), context->pool);

	if (switch_test_flag(handle, SWITCH_FILE_FLAG_READ)) {

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "VLC open %s for reading\n", path);

		/* Determine if this is a url or a path */
		/* TODO: Change this so that it tries local files first, and then if it fails try location. */
		if(! strncmp(context->path, "http", 4)){
			context->m = libvlc_media_new_location(read_inst, context->path);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is http %s\n", context->path);
		} else if (! strncmp(context->path, "rtp", 3)){
			context->m = libvlc_media_new_path(read_inst, context->path);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is rtp %s\n", context->path);
		} else if (! strncmp(context->path, "mms", 3)){
			context->m = libvlc_media_new_path(read_inst, context->path);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is mms %s\n", context->path);
		} else if (! strncmp(context->path, "/", 1)){
			context->m = libvlc_media_new_path(read_inst, context->path);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is file %s\n", context->path);
		} else {
			context->m = libvlc_media_new_location(read_inst, context->path);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is unknown type %s\n", context->path);
		}

		if ( context->m == NULL ) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC error opening %s for reading\n", path);
			return SWITCH_STATUS_GENERR;
		}

		context->playing = 0;
		context->err = 0;

		context->mp = libvlc_media_player_new_from_media(context->m);

		if (!handle->samplerate) {
			handle->samplerate = 16000;
		}

		context->samplerate = handle->samplerate;
		context->channels = handle->channels;

		libvlc_audio_set_format(context->mp, "S16N", context->samplerate, handle->channels);
		
		m_event_manager = libvlc_media_event_manager(context->m);
		libvlc_event_attach(m_event_manager, libvlc_MediaStateChanged, vlc_media_state_callback, (void *) context);

		mp_event_manager = libvlc_media_player_event_manager(context->mp);
		libvlc_event_attach(mp_event_manager, libvlc_MediaPlayerEncounteredError, vlc_mediaplayer_error_callback, (void *) context);
		
		libvlc_audio_set_callbacks(context->mp, vlc_auto_play_callback, NULL,NULL,NULL,NULL, (void *) context);

		libvlc_media_player_play(context->mp);

	} else if (switch_test_flag(handle, SWITCH_FILE_FLAG_WRITE)) {
		const char * opts[25] = {
			*vlc_args,
			switch_core_sprintf(context->pool, "--sout=%s", path)
		};
		int opts_count = 2;
			
		if ( !handle->samplerate)
			handle->samplerate = 16000;
		
		context->samplerate = handle->samplerate;
		context->channels = handle->channels;

		opts[opts_count++] = switch_core_sprintf(context->pool, "--imem-get=%ld", vlc_imem_get_callback);
		opts[opts_count++] = switch_core_sprintf(context->pool, "--imem-release=%ld", vlc_imem_release_callback);
		opts[opts_count++] = switch_core_sprintf(context->pool, "--imem-cat=%d", 4);
		opts[opts_count++] = "--demux=rawaud";
		opts[opts_count++] = "--rawaud-fourcc=s16l";
		opts[opts_count++] = "--imem-codec=s16l";
		opts[opts_count++] = switch_core_sprintf(context->pool, "--imem-samplerate=%d", context->samplerate);
		opts[opts_count++] = switch_core_sprintf(context->pool, "--imem-channels=%d", context->channels);
		opts[opts_count++] = switch_core_sprintf(context->pool, "--rawaud-channels=%d", context->channels);
		opts[opts_count++] = switch_core_sprintf(context->pool, "--imem-data=%ld", context);


		/* Prepare to write to an output stream. */
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "VLC open %s for writing\n", path);

		/* load the vlc engine. */
		context->inst_out = libvlc_new(opts_count, opts);
		
		/* Tell VLC the audio will come from memory, and to use the callbacks to fetch it. */
		context->m = libvlc_media_new_location(context->inst_out, "imem/rawaud://");
		context->mp = libvlc_media_player_new_from_media(context->m);
		context->samples = 0;
		context->pts = 0;
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC tried to open %s for unknown reason\n", path);
		return SWITCH_STATUS_GENERR;
	}

	handle->private_info = context;

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t vlc_file_read(switch_file_handle_t *handle, void *data, size_t *len)
{
	vlc_file_context_t *context = handle->private_info;
	size_t bytes = *len * sizeof(int16_t) * handle->channels, read;
	libvlc_state_t status;

	if (!context) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC read handle context is NULL\n");
		return SWITCH_STATUS_GENERR;
	}

	if (context->err) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC error\n");
		return SWITCH_STATUS_GENERR;
	}

	status = libvlc_media_get_state(context->m);
	
	if (status == libvlc_Error) {
		return SWITCH_STATUS_GENERR;
	}

	switch_mutex_lock(context->audio_mutex); 
	while (context->playing == 0 && status != libvlc_Ended && status != libvlc_Error) {
		switch_thread_cond_wait(context->cond, context->audio_mutex);		
		status = libvlc_media_get_state(context->m);
	}

	if (context->err == 1) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC error\n");
		return SWITCH_STATUS_FALSE;
	}

	switch_mutex_unlock(context->audio_mutex);

	switch_mutex_lock(context->audio_mutex);
	read = switch_buffer_read(context->audio_buffer, data, bytes);
	switch_mutex_unlock(context->audio_mutex);
	
        status = libvlc_media_get_state(context->m);

	if (!read && (status == libvlc_Stopped || status == libvlc_Ended || status == libvlc_Error)) {
		return SWITCH_STATUS_FALSE;
	} else if (!read) {
		read = 2;
		memset(data, 0, read);
	}

	if (read) {
		*len = read / 2 / handle->channels;
	}

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t vlc_file_write(switch_file_handle_t *handle, void *data, size_t *len)
{
	vlc_file_context_t *context = handle->private_info;
	size_t bytes = *len * sizeof(int16_t) * handle->channels;
	
	switch_mutex_lock(context->audio_mutex);
	context->samples += *len;
	switch_buffer_write(context->audio_buffer, data, bytes);
	switch_mutex_unlock(context->audio_mutex);	

	if (!context->playing) {
		context->playing = 1;		
		libvlc_media_player_play(context->mp);
	}

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t vlc_file_close(switch_file_handle_t *handle)
{
	vlc_file_context_t *context = handle->private_info;
	
	context->playing = 0;

	libvlc_media_player_stop(context->mp);

	switch_buffer_zero(context->audio_buffer);

	if (context->m) libvlc_media_release(context->m);
	
	if (context->inst_out) libvlc_release(context->inst_out);

	return SWITCH_STATUS_SUCCESS;
}


SWITCH_STANDARD_APP(play_video_function)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_memory_pool_t *pool = switch_core_session_get_pool(session);
	switch_frame_t audio_frame = { 0 }, video_frame = { 0 };
	switch_codec_t codec = { 0 }, *read_vid_codec;
	switch_timer_t timer = { 0 };
	switch_payload_t pt = 0;
	switch_dtmf_t dtmf = { 0 };
	switch_frame_t *read_frame;
	switch_codec_implementation_t read_impl = { 0 };
	vlc_video_context_t *context;
	char *path = (char *)data;
	const char *tmp;

	switch_size_t audio_datalen;

	context = switch_core_session_alloc(session, sizeof(vlc_video_context_t));
	switch_assert(context);
	memset(context, 0, sizeof(vlc_file_context_t));

	if ((tmp = switch_channel_get_variable(channel, "vlc_force_width"))) {
		context->force_width = atoi(tmp);
	}

	if ((tmp = switch_channel_get_variable(channel, "vlc_force_height"))) {
		context->force_height = atoi(tmp);
	}

	switch_buffer_create_dynamic(&(context->audio_buffer), VLC_BUFFER_SIZE, VLC_BUFFER_SIZE * 8, 0);

	switch_channel_pre_answer(channel);
	switch_core_session_get_read_impl(session, &read_impl);

	if ((read_vid_codec = switch_core_session_get_video_read_codec(session))) {
		pt = read_vid_codec->agreed_pt;
	}

	context->pt = pt;
	context->channels = read_impl.number_of_channels;
	audio_frame.codec = &codec;
	video_frame.codec = read_vid_codec;
	video_frame.packet = context->video_packet;
	video_frame.data = context->video_packet + 12;

	switch_channel_set_variable(channel, SWITCH_PLAYBACK_TERMINATOR_USED, "");

	if (switch_core_timer_init(&timer, "soft", read_impl.microseconds_per_packet / 1000,
							   read_impl.samples_per_packet, pool) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Timer Activation Fail\n");
		switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "Timer activation failed!");
		goto end;
	}

	if (switch_core_codec_init(&codec,
							   "L16",
							   NULL,
							   read_impl.actual_samples_per_second,
							   read_impl.microseconds_per_packet/1000,
							   read_impl.number_of_channels, SWITCH_CODEC_FLAG_ENCODE | SWITCH_CODEC_FLAG_DECODE,
							   NULL, switch_core_session_get_pool(session)) == SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Audio Codec Activation Success\n");
	} else {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Audio Codec Activation Fail\n");
		switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "Audio codec activation failed");
		goto end;
	}

	audio_datalen = read_impl.decoded_bytes_per_packet; //codec.implementation->actual_samples_per_second / 1000 * (read_impl.microseconds_per_packet / 1000);

	context->session = session;
	context->channel = channel;
	context->pool = pool;
	context->aud_frame = &audio_frame;
	context->vid_frame = &video_frame;
	context->playing = 0;
	// context->err = 0;

	switch_mutex_init(&context->audio_mutex, SWITCH_MUTEX_NESTED, context->pool);
	switch_mutex_init(&context->video_mutex, SWITCH_MUTEX_NESTED, context->pool);

	switch_thread_cond_create(&(context->cond), context->pool);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "VLC open %s for reading\n", path);

	/* Determine if this is a url or a path */
	/* TODO: Change this so that it tries local files first, and then if it fails try location. */
	if(! strncmp(path, "http", 4)){
		context->m = libvlc_media_new_location(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is http %s\n", path);
	} else if (! strncmp(path, "rtp", 3)){
		context->m = libvlc_media_new_path(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is rtp %s\n", path);
	} else if (! strncmp(path, "mms", 3)){
		context->m = libvlc_media_new_path(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is mms %s\n", path);
	} else if (! strncmp(path, "rtsp", 3)){
		context->m = libvlc_media_new_path(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is rtsp %s\n", path);
	} else if (! strncmp(path, "/", 1)){
		context->m = libvlc_media_new_path(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is file %s\n", path);
	} else {
		context->m = libvlc_media_new_location(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is unknown type %s\n", path);
	}

	if ( context->m == NULL ) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC error opening %s for reading\n", data);
		switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
		return ;
	}

	context->mp = libvlc_media_player_new_from_media(context->m);

	libvlc_audio_set_format(context->mp, "S16N", read_impl.actual_samples_per_second, read_impl.number_of_channels);
	libvlc_audio_set_callbacks(context->mp, vlc_play_audio_callback, NULL,NULL,NULL,NULL, (void *) context);

	libvlc_video_set_format_callbacks(context->mp, video_format_setup_callback, video_format_clean_callback);
	libvlc_video_set_callbacks(context->mp, vlc_video_lock_callback, vlc_video_unlock_callback, vlc_video_display_callback, context);

	// start play
	if (-1 == libvlc_media_player_play(context->mp)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC error playing %s\n", path);
		switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
	};

	while (switch_channel_ready(channel)) {

		switch_core_timer_next(&timer);

		switch_core_session_read_frame(session, &read_frame, SWITCH_IO_FLAG_NONE, 0);

		if (switch_channel_test_flag(channel, CF_BREAK)) {
			switch_channel_clear_flag(channel, CF_BREAK);
			break;
		}

		switch_ivr_parse_all_events(session);

		//check for dtmf interrupts
		if (switch_channel_has_dtmf(channel)) {
			const char * terminators = switch_channel_get_variable(channel, SWITCH_PLAYBACK_TERMINATORS_VARIABLE);
			switch_channel_dequeue_dtmf(channel, &dtmf);

			if (terminators && !strcasecmp(terminators, "none"))
			{
				terminators = NULL;
			}

			if (terminators && strchr(terminators, dtmf.digit)) {

				char sbuf[2] = {dtmf.digit, '\0'};
				switch_channel_set_variable(channel, SWITCH_PLAYBACK_TERMINATOR_USED, sbuf);
				break;
			}

		}

		{
			libvlc_state_t status = libvlc_media_get_state(context->m);
			if (status == libvlc_Ended || status == libvlc_Error || status == libvlc_Stopped ) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "VLC done. status = %d\n", status);
				break;
			}
		}

		if (switch_buffer_inuse(context->audio_buffer) >= audio_datalen) {
			const void *decoded_data;
			//switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "%d %d\n", (int)switch_buffer_inuse(context->audio_buffer), (int)audio_datalen);
			switch_buffer_peek_zerocopy(context->audio_buffer, &decoded_data);
			audio_frame.data = (void *)decoded_data;
			audio_frame.datalen = audio_datalen;
			audio_frame.buflen = audio_datalen;
			switch_core_session_write_frame(context->session, &audio_frame, SWITCH_IO_FLAG_NONE, 0);
			switch_buffer_toss(context->audio_buffer, audio_datalen);
		}

	}

	switch_yield(50000);

	if( context->mp ) libvlc_media_player_stop(context->mp);
	if( context->m ) libvlc_media_release(context->m);

	context->playing = 0;

	switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "OK");

end:

	switch_img_free(&context->img);

	if (context->audio_buffer) {
		switch_buffer_destroy(&context->audio_buffer);
	}

	if (timer.interval) {
		switch_core_timer_destroy(&timer);
	}

	if (switch_core_codec_ready(&codec)) {
		switch_core_codec_destroy(&codec);
	}

	switch_core_session_video_reset(session);
}

int  vlc_write_video_imem_get_callback(void *data, const char *cookie, int64_t *dts, int64_t *pts, unsigned *flags, size_t *size, void **output)
{
	vlc_video_context_t *context = (vlc_video_context_t *) data;
	int bytes = 0, bread = 0, blen = 0;
	int r = 0;

	if (!context->ending) {
		switch_mutex_lock(context->video_mutex); 
		if (!switch_queue_size(context->video_queue)) {
			switch_thread_cond_wait(context->cond, context->video_mutex);		
		}
		switch_mutex_unlock(context->video_mutex); 
	}

	if (*cookie == 'v') {
		switch_image_t *img = NULL;
		vlc_frame_data_t *fdata = NULL;
		void *pop;
		
		if (switch_queue_trypop(context->video_queue, &pop) == SWITCH_STATUS_SUCCESS && pop) {
			img = (switch_image_t *) pop;
		} else {
			goto nada;
		}
		
		fdata = (vlc_frame_data_t *) img->user_priv;
		
		*dts = *pts = fdata->pts;

		*size = img->d_w * img->d_h * 2;

		if (context->video_frame_buffer_len < *size) {
			context->video_frame_buffer_len = *size;
			context->video_frame_buffer = switch_core_alloc(context->pool, context->video_frame_buffer_len);
		}
		
		*output = context->video_frame_buffer;
		*size = 0;
		switch_img_convert(img, SWITCH_CONVERT_FMT_YUYV, *output, size);
		switch_img_free(&img);

		return 0;
	}

	switch_mutex_lock(context->audio_mutex);

	if ((blen = switch_buffer_inuse(context->audio_buffer))) {
		switch_buffer_read(context->audio_buffer, &context->pts, sizeof(context->pts));
		blen = switch_buffer_inuse(context->audio_buffer);
		*pts = *dts = context->pts;
	}

	if (!(bytes = blen)) {
		switch_mutex_unlock(context->audio_mutex);
		goto nada;
	}

	if (context->audio_frame_buffer_len < bytes) {
		context->audio_frame_buffer_len = bytes;
		context->audio_frame_buffer = switch_core_alloc(context->pool, context->audio_frame_buffer_len);
	}

	*output = context->audio_frame_buffer;

	bread = switch_buffer_read(context->audio_buffer, *output, bytes);

	*size = (size_t) bread;

	switch_mutex_unlock(context->audio_mutex);

	return 0;

 nada:

	if (context->ending) {
		if (*cookie == 'a') {
			if (!switch_buffer_inuse(context->audio_buffer)) {
				r = -1;
			}
		} else {
			if (switch_queue_size(context->video_queue) == 0) {
				r = -1;
			}
		}
	}

	*size = 0;
	*output = NULL;

	return r;
}

void vlc_write_video_imem_release_callback(void *data, const char *cookie, size_t size, void *unknown)
{
	
}

static switch_status_t video_read_callback(switch_core_session_t *session, switch_frame_t *frame, void *user_data)
{
	vlc_video_context_t *context = (vlc_video_context_t *) user_data;
	switch_image_t *img_copy = NULL;
	vlc_frame_data_t *fdata = NULL;

	if (frame->img) {
		unsigned int size = switch_queue_size(context->video_queue);

		switch_img_copy(frame->img, &img_copy);
		switch_zmalloc(fdata, sizeof(*fdata));

		switch_mutex_lock(context->audio_mutex);
		switch_core_timer_sync(&context->timer);
		fdata->pts = context->timer.samplecount;
		switch_mutex_unlock(context->audio_mutex);
		
		img_copy->user_priv = (void *) fdata;
		switch_queue_push(context->video_queue, img_copy);

		if (!size) { /* was empty before this push */
			if (switch_mutex_trylock(context->video_mutex) == SWITCH_STATUS_SUCCESS) {
				switch_thread_cond_signal(context->cond);
				switch_mutex_unlock(context->video_mutex);
			}
		}
	}


	return SWITCH_STATUS_SUCCESS;

}

SWITCH_STANDARD_APP(capture_video_function)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_memory_pool_t *pool = switch_core_session_get_pool(session);
	switch_codec_t *write_vid_codec;
	switch_payload_t pt = 0;
	switch_dtmf_t dtmf = { 0 };
	switch_codec_implementation_t read_impl = { 0 };
	vlc_video_context_t *context;
	char *path = (char *)data;
	libvlc_instance_t *inst_out;
	switch_status_t status;
	switch_frame_t *read_frame;
	switch_vid_params_t vid_params = { 0 };
	int64_t pts = 0;
	char *imem_main, *imem_slave;
	libvlc_state_t vlc_status;
    unsigned char audio_data_buf[SWITCH_RECOMMENDED_BUFFER_SIZE] = { 0 };
	void *audio_data;
	switch_size_t audio_datalen;
	uint32_t offset = 500000;
	const char *tmp;
	const char * opts[25] = {
		*vlc_args,
		switch_core_session_sprintf(session, "--sout=%s", path + 6)
	};
	int argc = 2;

	context = switch_core_session_alloc(session, sizeof(vlc_video_context_t));
	switch_assert(context);
	memset(context, 0, sizeof(vlc_file_context_t));

	if ((tmp = switch_channel_get_variable(channel, "vlc_capture_offset"))) {
		int x = atoi(tmp);
		if (x > 0) offset = x;
	}

	switch_channel_pre_answer(channel);
	switch_core_session_get_read_impl(session, &read_impl);

	if ((write_vid_codec = switch_core_session_get_video_read_codec(session))) {
		pt = write_vid_codec->agreed_pt;
	}

	context->pt = pt;
	context->channels = read_impl.number_of_channels;

	context->session = session;
	context->channel = channel;
	context->pool = pool;
	context->playing = 0;
	context->samplerate = read_impl.actual_samples_per_second;
	vid_params.width = 640;
	vid_params.height = 480;

	switch_queue_create(&context->video_queue, SWITCH_CORE_QUEUE_LEN, switch_core_session_get_pool(session));
	switch_core_session_set_video_read_callback(session, video_read_callback, (void *)context);

	switch_buffer_create_dynamic(&(context->audio_buffer), VLC_BUFFER_SIZE, VLC_BUFFER_SIZE * 8, 0);
	switch_mutex_init(&context->audio_mutex, SWITCH_MUTEX_NESTED, context->pool);
	switch_mutex_init(&context->video_mutex, SWITCH_MUTEX_NESTED, context->pool);
	switch_thread_cond_create(&context->cond, context->pool);

	switch_core_timer_init(&context->timer, "soft", 1, 1000, context->pool);

	switch_channel_set_flag(channel, CF_VIDEO_DECODED_READ);	
	switch_channel_wait_for_flag(channel, CF_VIDEO_READY, SWITCH_TRUE, 10000, NULL);
	switch_core_media_get_vid_params(session, &vid_params);
	switch_channel_set_flag(channel, CF_VIDEO_ECHO);
	switch_core_session_raw_read(session);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "VLC open %s for writing\n", path);

	opts[argc++] = switch_core_session_sprintf(session, "--imem-get=%ld", vlc_write_video_imem_get_callback);
	opts[argc++] = switch_core_session_sprintf(session, "--imem-release=%ld", vlc_write_video_imem_release_callback);
	opts[argc++] = switch_core_session_sprintf(session, "--imem-data=%ld", context);

	inst_out = libvlc_new(argc, opts);
	
	imem_main = switch_core_session_sprintf(session,
											"imem://cookie=video:"
											"fps=15.0/1:"
											"width=%d:"
											"height=%d:"
											"codec=YUYV:"
											"cat=2:"
											"id=2:"
											"caching=0",
											vid_params.width, vid_params.height);

	imem_slave = switch_core_session_sprintf(session,
											 ":input-slave=imem://cookie=audio:"
											 "cat=1:"
											 "codec=s16l:"
											 "samplerate=%d:"
											 "channels=%d:"
											 "id=1:"
											 "caching=0", 
											 context->samplerate, context->channels);
	
	context->m = libvlc_media_new_location(inst_out, imem_main);
	
	libvlc_media_add_option_flag( context->m, imem_slave, libvlc_media_option_trusted );                                                                       

	if ( context->m == NULL ) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC error opening %s for reading\n", data);
		switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
		return ;
	}
	
	context->mp = libvlc_media_player_new_from_media(context->m);

	context->samples = 0;
	context->pts = 0;
	context->playing = 1;

	if (libvlc_media_player_play(context->mp) == -1) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC error playing %s\n", path);
		switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
	};


	switch_channel_audio_sync(channel);

	if (offset) {
		uint32_t off_frames = offset / read_impl.microseconds_per_packet;
		int i = 0;

		switch_mutex_lock(context->audio_mutex);
		switch_core_timer_sync(&context->timer);
		pts = context->timer.samplecount;
		switch_buffer_write(context->audio_buffer, &pts, sizeof(pts));

		audio_data = audio_data_buf;
		audio_datalen = read_impl.decoded_bytes_per_packet;

		for (i = 0; i < off_frames; i++) {
			switch_buffer_write(context->audio_buffer, audio_data, audio_datalen);
		}
		switch_mutex_unlock(context->audio_mutex);
	}


	while (switch_channel_ready(channel)) {

		status = switch_core_session_read_frame(context->session, &read_frame, SWITCH_IO_FLAG_NONE, 0);

		if (!SWITCH_READ_ACCEPTABLE(status)) {
			break;
		}
		
		if (switch_test_flag(read_frame, SFF_CNG)) {
			audio_data = audio_data_buf;
			audio_datalen = read_impl.decoded_bytes_per_packet;
		} else {
			audio_data = read_frame->data;
			audio_datalen = read_frame->datalen;
		}

		switch_mutex_lock(context->audio_mutex);
		if (!switch_buffer_inuse(context->audio_buffer)) {
			switch_core_timer_sync(&context->timer);
			pts = context->timer.samplecount - offset;
			switch_buffer_write(context->audio_buffer, &pts, sizeof(pts));
		}
		switch_buffer_write(context->audio_buffer, audio_data, audio_datalen);
		switch_mutex_unlock(context->audio_mutex);


		if (switch_channel_test_flag(channel, CF_BREAK)) {
			switch_channel_clear_flag(channel, CF_BREAK);
			break;
		}

		switch_ivr_parse_all_events(session);

		if (switch_channel_has_dtmf(channel)) {
			const char * terminators = switch_channel_get_variable(channel, SWITCH_PLAYBACK_TERMINATORS_VARIABLE);
			switch_channel_dequeue_dtmf(channel, &dtmf);

			if (terminators && !strcasecmp(terminators, "none")) {
				terminators = NULL;
			}

			if (terminators && strchr(terminators, dtmf.digit)) {
				char sbuf[2] = {dtmf.digit, '\0'};
				switch_channel_set_variable(channel, SWITCH_PLAYBACK_TERMINATOR_USED, sbuf);
				break;
			}
		}
		
		vlc_status = libvlc_media_get_state(context->m);

		if (vlc_status == libvlc_Ended || vlc_status == libvlc_Error || vlc_status == libvlc_Stopped ) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "VLC done. status = %d\n", vlc_status);
			break;
		}		
	}

	switch_core_session_set_video_read_callback(session, NULL, NULL);
	context->ending = 1;

	if (switch_mutex_trylock(context->video_mutex) == SWITCH_STATUS_SUCCESS) {
		switch_thread_cond_signal(context->cond);
		switch_mutex_unlock(context->video_mutex);
	}

	while(switch_buffer_inuse(context->audio_buffer) || switch_queue_size(context->video_queue)) {
		libvlc_state_t status = libvlc_media_get_state(context->m);

		if (status == libvlc_Ended || status == libvlc_Error || status == libvlc_Stopped ) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "VLC done. status = %d\n", status);
			break;
		}

		switch_yield(10000);
	}

	context->playing = 0;


	if (context->mp) libvlc_media_player_stop(context->mp);
	if (context->m) libvlc_media_release(context->m);
	if (inst_out) libvlc_release(inst_out);



	switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "OK");

	switch_img_free(&context->img);

	switch_core_timer_destroy(&context->timer);

	if (context->audio_buffer) {
		switch_buffer_destroy(&context->audio_buffer);
	}

	switch_core_session_reset(session, SWITCH_TRUE, SWITCH_TRUE);
	switch_core_session_video_reset(session);
}

static switch_status_t channel_on_init(switch_core_session_t *session);
static switch_status_t channel_on_consume_media(switch_core_session_t *session);
static switch_status_t channel_on_destroy(switch_core_session_t *session);

static switch_call_cause_t vlc_outgoing_channel(switch_core_session_t *session, switch_event_t *var_event,
													switch_caller_profile_t *outbound_profile,
													switch_core_session_t **new_session,
													switch_memory_pool_t **pool,
													switch_originate_flag_t flags, switch_call_cause_t *cancel_cause);
static switch_status_t vlc_read_frame(switch_core_session_t *session, switch_frame_t **frame, switch_io_flag_t flags, int stream_id);
static switch_status_t vlc_write_frame(switch_core_session_t *session, switch_frame_t *frame, switch_io_flag_t flags, int stream_id);
static switch_status_t vlc_read_video_frame(switch_core_session_t *session, switch_frame_t **frame, switch_io_flag_t flags, int stream_id);
static switch_status_t vlc_receive_message(switch_core_session_t *session, switch_core_session_message_t *msg);
static switch_status_t vlc_kill_channel(switch_core_session_t *session, int sig);
static switch_status_t vlc_state_change(switch_core_session_t *session);

typedef struct {
    switch_core_session_t *session;
    switch_channel_t *channel;
    switch_codec_t read_codec, write_codec;
    switch_codec_t video_codec;
    switch_frame_t read_frame;
    switch_frame_t read_video_frame;
    void *audio_data[SWITCH_RECOMMENDED_BUFFER_SIZE];
    void *video_data[SWITCH_RECOMMENDED_BUFFER_SIZE * 20];
    const char *destination_number;
    vlc_video_context_t *context;
    switch_timer_t timer;
    switch_core_media_params_t mparams;
    switch_media_handle_t *media_handle;
	switch_codec_implementation_t read_impl;

} vlc_private_t;

switch_state_handler_table_t vlc_state_handlers = {
	/*on_init */ channel_on_init,
	/*on_routing */ NULL,
	/*on_execute */ NULL,
	/*on_hangup*/ NULL,
	/*on_exchange_media*/ NULL,
	/*on_soft_execute*/ NULL,
	/*on_consume_media*/ channel_on_consume_media,
	/*on_hibernate*/ NULL,
	/*on_reset*/ NULL,
	/*on_park*/ NULL,
	/*on_reporting*/ NULL,
	/*on_destroy*/ channel_on_destroy
};

switch_io_routines_t vlc_io_routines = {
	/*outgoing_channel*/ vlc_outgoing_channel,
	/*read_frame*/ vlc_read_frame,
	/*write_frame*/ vlc_write_frame,
	/*kill_channel*/ vlc_kill_channel,
	/*send_dtmf*/ NULL,
	/*receive_message*/ vlc_receive_message,
	/*receive_event*/ NULL,
	/*state_change*/ vlc_state_change,
	/*read_video_frame*/ vlc_read_video_frame,
	/*write_video_frame*/ NULL,
	/*state_run*/ NULL
};

static switch_status_t setup_tech_pvt(switch_core_session_t *osession, switch_core_session_t *session, const char *path)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_memory_pool_t *pool = switch_core_session_get_pool(session);
	// switch_dtmf_t dtmf = { 0 };
	vlc_video_context_t *context;
	vlc_private_t *tech_pvt;
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	tech_pvt = switch_core_session_alloc(session, sizeof *tech_pvt);
	switch_assert(tech_pvt);
	memset(tech_pvt, 0, sizeof(*tech_pvt));


	if (osession) {
		switch_core_session_get_read_impl(osession, &tech_pvt->read_impl);
	} else {
		/* hard coded values, could also be set according to var_event if we want to support HD audio or stereo */
		tech_pvt->read_impl.microseconds_per_packet = 20000;
		tech_pvt->read_impl.samples_per_packet = 8000;
		tech_pvt->read_impl.actual_samples_per_second = tech_pvt->read_impl.samples_per_packet;
		tech_pvt->read_impl.number_of_channels = 1;
		tech_pvt->read_impl.iananame = "L16";
		tech_pvt->read_impl.decoded_bytes_per_packet = 320 * tech_pvt->read_impl.number_of_channels;
	}

	tech_pvt->session = session;
	tech_pvt->channel = channel;
	tech_pvt->destination_number = switch_core_session_strdup(session, path);
	tech_pvt->mparams.external_video_source = SWITCH_TRUE;
	switch_media_handle_create(&tech_pvt->media_handle, session, &tech_pvt->mparams);
	switch_core_session_set_private(session, tech_pvt);

	context = switch_core_session_alloc(session, sizeof(vlc_video_context_t));
	switch_assert(context);
	memset(context, 0, sizeof(vlc_file_context_t));
	tech_pvt->context = context;


	switch_buffer_create_dynamic(&(context->audio_buffer), VLC_BUFFER_SIZE, VLC_BUFFER_SIZE * 8, 0);
	switch_queue_create(&context->video_queue, SWITCH_CORE_QUEUE_LEN, switch_core_session_get_pool(session));

	if (switch_core_timer_init(&tech_pvt->timer, "soft", tech_pvt->read_impl.microseconds_per_packet / 1000,
							   tech_pvt->read_impl.samples_per_packet, pool) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Timer Activation Fail\n");
		status = SWITCH_STATUS_FALSE;
		goto fail;
	}

	context->channels = tech_pvt->read_impl.number_of_channels;

	context->session = session;
	context->pool = pool;
	context->aud_frame = &tech_pvt->read_frame;
	context->vid_frame = &tech_pvt->read_video_frame;
	context->vid_frame->packet = context->video_packet;
	context->vid_frame->data = context->video_packet + 12;

	context->playing = 0;
	// context->err = 0;

	switch_mutex_init(&context->audio_mutex, SWITCH_MUTEX_NESTED, context->pool);
	switch_mutex_init(&context->video_mutex, SWITCH_MUTEX_NESTED, context->pool);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC open %s for reading\n", path);

	/* Determine if this is a url or a path */
	/* TODO: Change this so that it tries local files first, and then if it fails try location. */
	if(! strncmp(path, "http", 4)){
		context->m = libvlc_media_new_location(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is http %s\n", path);
	} else if (! strncmp(path, "rtp", 3)){
		context->m = libvlc_media_new_path(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is rtp %s\n", path);
	} else if (! strncmp(path, "mms", 3)){
		context->m = libvlc_media_new_path(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is mms %s\n", path);
	} else if (! strncmp(path, "rtsp", 3)){
		context->m = libvlc_media_new_path(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is rtsp %s\n", path);
	} else if (! strncmp(path, "/", 1)){
		context->m = libvlc_media_new_path(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is file %s\n", path);
	} else {
		context->m = libvlc_media_new_location(read_inst, path);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "VLC Path is unknown type %s\n", path);
	}

	if ( context->m == NULL ) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC error opening %s for reading\n", path);
		switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
		goto fail;
	}

	context->mp = libvlc_media_player_new_from_media(context->m);

	libvlc_audio_set_format(context->mp, "S16N", tech_pvt->read_impl.actual_samples_per_second, tech_pvt->read_impl.number_of_channels);
	libvlc_audio_set_callbacks(context->mp, vlc_play_audio_callback, NULL,NULL,NULL,NULL, (void *) context);

	
	libvlc_video_set_format_callbacks(context->mp, video_format_setup_callback, video_format_clean_callback);
	libvlc_video_set_callbacks(context->mp, vlc_video_lock_callback, vlc_video_channel_unlock_callback, vlc_video_display_callback, context);


	return SWITCH_STATUS_SUCCESS;

fail:

	return status;
}

static switch_status_t channel_on_init(switch_core_session_t *session)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	//vlc_private_t *tech_pvt = switch_core_session_get_private(session);

	switch_channel_set_state(channel, CS_CONSUME_MEDIA);

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t channel_on_consume_media(switch_core_session_t *session)
{
	// switch_channel_t *channel = switch_core_session_get_channel(session);
	vlc_private_t *tech_pvt = switch_core_session_get_private(session);

	// return SWITCH_STATUS_SUCCESS;

	switch_assert(tech_pvt && tech_pvt->context);

	// // start play
	// if (-1 == libvlc_media_player_play(tech_pvt->context->mp)) {
	// 	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC error playing %s\n", tech_pvt->destination_number);
	// 	return SWITCH_STATUS_FALSE;
	// };

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t channel_on_destroy(switch_core_session_t *session)
{
	vlc_private_t *tech_pvt = switch_core_session_get_private(session);

	switch_assert(tech_pvt && tech_pvt->context);

	if ((tech_pvt = switch_core_session_get_private(session))) {

		if (tech_pvt->read_codec.implementation) {
			switch_core_codec_destroy(&tech_pvt->read_codec);
		}

		if (tech_pvt->write_codec.implementation) {
			switch_core_codec_destroy(&tech_pvt->write_codec);
		}

		switch_media_handle_destroy(session);
	}

	switch_yield(50000);

	if (tech_pvt->context->mp) libvlc_media_player_stop(tech_pvt->context->mp);
	if (tech_pvt->context->m) libvlc_media_release(tech_pvt->context->m);

	tech_pvt->context->playing = 0;

	if (tech_pvt->context->audio_buffer) {
		switch_buffer_destroy(&tech_pvt->context->audio_buffer);
	}

	if (tech_pvt->context->video_queue) {
		void *pop;

		while (switch_queue_trypop(tech_pvt->context->video_queue, &pop) == SWITCH_STATUS_SUCCESS && pop) {
			switch_image_t *img = (switch_image_t *) pop;			
			switch_img_free(&img);
		}
	}

	if (tech_pvt->timer.interval) {
		switch_core_timer_destroy(&tech_pvt->timer);
	}

	switch_img_free(&tech_pvt->read_video_frame.img);

	return SWITCH_STATUS_SUCCESS;
}

static switch_call_cause_t vlc_outgoing_channel(switch_core_session_t *session, switch_event_t *var_event,
													switch_caller_profile_t *outbound_profile,
													switch_core_session_t **new_session,
													switch_memory_pool_t **pool,
													switch_originate_flag_t flags, switch_call_cause_t *cancel_cause)
{
	switch_channel_t *channel;
	char name[256];
	vlc_private_t *tech_pvt = NULL;
	switch_caller_profile_t *caller_profile;
	const char *codec_str = NULL;

	switch_assert(vlc_endpoint_interface);

	if (session) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "session: %s\n", switch_core_session_get_uuid(session));
		switch_channel_answer(switch_core_session_get_channel(session));
	}

	if (!(*new_session = switch_core_session_request(vlc_endpoint_interface, SWITCH_CALL_DIRECTION_OUTBOUND, 0, pool))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't request session.\n");
		goto fail;
	}

	channel = switch_core_session_get_channel(*new_session);
	snprintf(name, sizeof(name), "vlc/%s", outbound_profile->destination_number);
	switch_channel_set_name(channel, name);
	switch_channel_set_flag(channel, CF_VIDEO);

	if (setup_tech_pvt(session, *new_session, outbound_profile->destination_number) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error steup tech_pvt!\n");
		goto fail;
	}

	tech_pvt = switch_core_session_get_private(*new_session);
	caller_profile = switch_caller_profile_clone(*new_session, outbound_profile);
	switch_channel_set_caller_profile(channel, caller_profile);

	if (switch_core_codec_init(&tech_pvt->read_codec,
							   "L16",
							   NULL,
							   tech_pvt->read_impl.actual_samples_per_second,
							   tech_pvt->read_impl.microseconds_per_packet / 1000,
							   tech_pvt->read_impl.number_of_channels,
							   /*SWITCH_CODEC_FLAG_ENCODE |*/ SWITCH_CODEC_FLAG_DECODE,
							   NULL, switch_core_session_get_pool(tech_pvt->session)) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Can't load codec?\n");
		goto fail;
	}

	if (switch_core_codec_init(&tech_pvt->write_codec,
							   tech_pvt->read_impl.iananame,
							   NULL,
							   tech_pvt->read_impl.actual_samples_per_second,
							   tech_pvt->read_impl.microseconds_per_packet / 1000,
							   tech_pvt->read_impl.number_of_channels,
							   SWITCH_CODEC_FLAG_ENCODE /*| SWITCH_CODEC_FLAG_DECODE*/,
							   NULL, switch_core_session_get_pool(tech_pvt->session)) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Can't load codec?\n");
		goto fail;
	}

	codec_str = switch_event_get_header(var_event, "absolute_codec_string");

	if (!codec_str && session) {
		switch_codec_t *codec = switch_core_session_get_video_read_codec(session);
		if (codec) {
			codec_str = codec->implementation->iananame;
		}
	}

	if (!codec_str) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "No video codec set, default to VP8\n");
		codec_str = "VP8";
	}

	if (switch_core_codec_init(&tech_pvt->video_codec,
				codec_str,
				NULL,
				90000,
				0,
				1, SWITCH_CODEC_FLAG_ENCODE | SWITCH_CODEC_FLAG_DECODE,
				NULL, switch_core_session_get_pool(tech_pvt->session)) == SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(*new_session), SWITCH_LOG_DEBUG, "Video Codec Activation Success\n");
	} else {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(*new_session), SWITCH_LOG_ERROR, "Video Codec Activation Fail\n");
		goto fail;
	}

	if (switch_core_session_set_read_codec(*new_session, &tech_pvt->read_codec) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Can't set read codec?\n");
		goto fail;
	}

	if (switch_core_session_set_write_codec(*new_session, &tech_pvt->write_codec) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Can't set write codec?\n");
		goto fail;
	}

	if (switch_core_session_set_video_read_codec(*new_session, &tech_pvt->video_codec) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Can't set read codec?\n");
		goto fail;
	}

	if (switch_core_session_set_video_write_codec(*new_session, &tech_pvt->video_codec) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Can't set write codec?\n");
		goto fail;
	}

	switch_core_session_start_video_thread(*new_session);
	switch_channel_set_state(channel, CS_INIT);

	switch_channel_mark_answered(channel);

	// start play
	if (-1 == libvlc_media_player_play(tech_pvt->context->mp)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "VLC error playing %s\n", tech_pvt->destination_number);
		goto fail;
	};

	return SWITCH_CAUSE_SUCCESS;

fail:
	if (tech_pvt) {
		if (tech_pvt->read_codec.implementation) {
			switch_core_codec_destroy(&tech_pvt->read_codec);
		}

		if (tech_pvt->write_codec.implementation) {
			switch_core_codec_destroy(&tech_pvt->write_codec);
		}

		if (tech_pvt->video_codec.implementation) {
			switch_core_codec_destroy(&tech_pvt->video_codec);
		}

		switch_media_handle_destroy(*new_session);
	}

	if (*new_session) {
		switch_core_session_destroy(new_session);
	}

	return SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER;
}

static switch_status_t vlc_read_frame(switch_core_session_t *session, switch_frame_t **frame, switch_io_flag_t flags, int stream_id)
{
	vlc_private_t *tech_pvt;
	switch_channel_t *channel;
	switch_status_t status = SWITCH_STATUS_SUCCESS;
	libvlc_state_t vlc_status;
	vlc_video_context_t *context;
	switch_size_t audio_datalen;

	channel = switch_core_session_get_channel(session);
	assert(channel != NULL);

	tech_pvt = switch_core_session_get_private(session);
	assert(tech_pvt != NULL);

	switch_yield(tech_pvt->read_impl.microseconds_per_packet);

	audio_datalen = tech_pvt->read_impl.decoded_bytes_per_packet;

	context = tech_pvt->context;
	switch_assert(context);

	vlc_status = libvlc_media_get_state(context->m);

	if (vlc_status == libvlc_Ended || vlc_status == libvlc_Error || vlc_status == libvlc_Stopped ) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "VLC done. status = %d\n", status);
		switch_channel_hangup(channel, SWITCH_CAUSE_SUCCESS);
		return SWITCH_STATUS_SUCCESS;
	}

	switch_mutex_lock(context->audio_mutex);

	if (switch_buffer_inuse(context->audio_buffer) >= audio_datalen) {
		tech_pvt->read_frame.data = tech_pvt->audio_data;
		switch_buffer_read(context->audio_buffer, tech_pvt->read_frame.data, audio_datalen);
		tech_pvt->read_frame.datalen = audio_datalen;
		tech_pvt->read_frame.buflen = audio_datalen;
		tech_pvt->read_frame.flags &= ~SFF_CNG;
		tech_pvt->read_frame.codec = &tech_pvt->read_codec;
		*frame = &tech_pvt->read_frame;
		switch_mutex_unlock(context->audio_mutex);
		return SWITCH_STATUS_SUCCESS;
	}

	switch_mutex_unlock(context->audio_mutex);

	*frame = &tech_pvt->read_frame;
	tech_pvt->read_frame.codec = &tech_pvt->read_codec;
	tech_pvt->read_frame.flags |= SFF_CNG;
	tech_pvt->read_frame.datalen = 0;

	//switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "read cng frame\n");
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t vlc_write_frame(switch_core_session_t *session, switch_frame_t *frame, switch_io_flag_t flags, int stream_id)
{
	return	SWITCH_STATUS_SUCCESS;
}

static switch_status_t vlc_read_video_frame(switch_core_session_t *session, switch_frame_t **frame, switch_io_flag_t flags, int stream_id)
{
	switch_channel_t *channel;
	vlc_private_t *tech_pvt;
	void *pop;
	
	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	switch_assert(tech_pvt->context);
	
	switch_img_free(&tech_pvt->read_video_frame.img);

	if (tech_pvt->context->video_queue && switch_queue_pop(tech_pvt->context->video_queue, &pop) == SWITCH_STATUS_SUCCESS) {
		switch_image_t *img = (switch_image_t *) pop;			
		if (!img) return SWITCH_STATUS_FALSE;
		
		tech_pvt->read_video_frame.img = img;
		*frame = &tech_pvt->read_video_frame;
		switch_set_flag(*frame, SFF_RAW_RTP);
		switch_clear_flag(*frame, SFF_CNG);
		(*frame)->codec = &tech_pvt->video_codec;
	} else {
		*frame = &tech_pvt->read_frame;
		tech_pvt->read_frame.codec = &tech_pvt->video_codec;
		tech_pvt->read_frame.flags |= SFF_CNG;
		tech_pvt->read_frame.datalen = 0;
	}

	// switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "read video %d\n", (*frame)->packetlen);
	return SWITCH_STATUS_SUCCESS;

}

static switch_status_t vlc_receive_message(switch_core_session_t *session, switch_core_session_message_t *msg)
{
	vlc_private_t *tech_pvt = switch_core_session_get_private(session);

	assert(tech_pvt != NULL);

	switch (msg->message_id) {
		case SWITCH_MESSAGE_INDICATE_DEBUG_MEDIA:
			break;
		case SWITCH_MESSAGE_INDICATE_AUDIO_SYNC:
			break;
		case SWITCH_MESSAGE_INDICATE_JITTER_BUFFER:
			break;
		case SWITCH_MESSAGE_INDICATE_VIDEO_REFRESH_REQ:
			//switch_core_media_gen_key_frame(session);
			break;
		default:
			break;
	}
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t vlc_state_change(switch_core_session_t *session)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_channel_state_t state = switch_channel_get_state(channel);

	if (state == CS_HANGUP || (state == CS_ROUTING && switch_channel_test_flag(channel, CF_BRIDGED))) {
		switch_core_session_video_reset(session);
	}

	return SWITCH_STATUS_SUCCESS;
}


static switch_status_t vlc_kill_channel(switch_core_session_t *session, int sig)
{
	vlc_private_t *tech_pvt = switch_core_session_get_private(session);
	switch_channel_t *channel = switch_core_session_get_channel(session);

	if (!tech_pvt) return SWITCH_STATUS_FALSE;

	if (!tech_pvt->context) return SWITCH_STATUS_FALSE;

	switch (sig) {
	case SWITCH_SIG_BREAK:
	case SWITCH_SIG_KILL:
		if (switch_channel_test_flag(channel, CF_VIDEO)) {
			switch_queue_push(tech_pvt->context->video_queue, NULL);
		}
		break;
	default:
		break;
	}

	return SWITCH_STATUS_SUCCESS;
}

/* Macro expands to: switch_status_t mod_vlc_load(switch_loadable_module_interface_t **module_interface, switch_memory_pool_t *pool) */
SWITCH_MODULE_LOAD_FUNCTION(mod_vlc_load)
{
	switch_file_interface_t *file_interface;
	switch_application_interface_t *app_interface;

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	vlc_file_supported_formats[0] = "vlc";

	file_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_FILE_INTERFACE);
	file_interface->interface_name = modname;
	file_interface->extens = vlc_file_supported_formats;
	file_interface->file_open = vlc_file_open;
	file_interface->file_close = vlc_file_close;
	file_interface->file_read = vlc_file_read;
	file_interface->file_write = vlc_file_write;

	vlc_endpoint_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_ENDPOINT_INTERFACE);
	vlc_endpoint_interface->interface_name = "vlc";
	vlc_endpoint_interface->io_routines = &vlc_io_routines;
	vlc_endpoint_interface->state_handler = &vlc_state_handlers;

	/* load the vlc engine. */
	read_inst = libvlc_new(sizeof(vlc_args)/sizeof(char *), vlc_args);

	if ( ! read_inst ) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "FAILED TO LOAD\n");
			return SWITCH_STATUS_GENERR;
	}

	SWITCH_ADD_APP(app_interface, "play_video", "play a videofile", "play a video file", play_video_function, "<file>", SAF_NONE);
	SWITCH_ADD_APP(app_interface, "capture_video", "capture a videofile", "capture a video file", capture_video_function, "<file>", SAF_NONE);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Initialized VLC instance\n");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/*
  Called when the system shuts down
  Macro expands to: switch_status_t mod_vlc_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_vlc_shutdown)
{
	if ( read_inst != NULL )
		libvlc_release(read_inst);
	return SWITCH_STATUS_SUCCESS;
}


/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet
 */
