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
 * Seven Du <dujinfang@gmail.com>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 *
 * mod_yuv.c -- YUV (raw) Video Codec
 *
 */

#include <switch.h>

SWITCH_MODULE_LOAD_FUNCTION(mod_yuv_load);
SWITCH_MODULE_DEFINITION(mod_yuv, mod_yuv_load, NULL, NULL);

static switch_status_t switch_yuv_init(switch_codec_t *codec, switch_codec_flag_t flags, const switch_codec_settings_t *codec_settings)
{
	int encoding, decoding;

	encoding = (flags & SWITCH_CODEC_FLAG_ENCODE);
	decoding = (flags & SWITCH_CODEC_FLAG_DECODE);

	if (!(encoding || decoding)) {
		return SWITCH_STATUS_FALSE;
	} else {
		if (codec->fmtp_in) {
			codec->fmtp_out = switch_core_strdup(codec->memory_pool, codec->fmtp_in);
		}
		return SWITCH_STATUS_SUCCESS;
	}
}

static switch_status_t switch_yuv_encode(switch_codec_t *codec,
										  switch_image_t *img,
										  void *encoded_data, uint32_t *encoded_data_len,
										  unsigned int *flag)
{
	/* yuv encode is unclear, so return 0 for now */
	*encoded_data_len = 0;
	*flag |= SFF_MARKER;
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t switch_yuv_decode(switch_codec_t *codec,
										  switch_frame_t *frame,
										  switch_image_t **img,
										  unsigned int *flag)
{
	switch_assert(frame);

	*img = (switch_image_t *)frame->user_data;
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t switch_yuv_destroy(switch_codec_t *codec)
{
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_LOAD_FUNCTION(mod_yuv_load)
{
	switch_codec_interface_t *codec_interface;
	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);
	SWITCH_ADD_CODEC(codec_interface, "YUV I420 Video (raw)");

	switch_core_codec_add_video_implementation(pool, codec_interface, 99, "I420", NULL,
											   switch_yuv_init, switch_yuv_encode, switch_yuv_decode, NULL, switch_yuv_destroy);

	/* indicate that the module should continue to be loaded */
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
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
 */
