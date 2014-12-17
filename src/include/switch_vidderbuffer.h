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
 * Anthony Minessale II <anthm@freeswitch.org>
 *
 * switch_vidderbuffer.h -- Video Buffer
 *
 */


#ifndef SWITCH_VIDDERBUFFER_H
#define SWITCH_VIDDERBUFFER_H

SWITCH_BEGIN_EXTERN_C
SWITCH_DECLARE(switch_status_t) switch_vb_create(switch_vb_t **vbp, uint32_t min_frame_len, uint32_t max_frame_len, switch_bool_t timer_compensation);
SWITCH_DECLARE(switch_status_t) switch_vb_destroy(switch_vb_t **vbp);
SWITCH_DECLARE(void) switch_vb_reset(switch_vb_t *vb, switch_bool_t flush);
SWITCH_DECLARE(void) switch_vb_debug_level(switch_vb_t *vb, uint8_t level);
SWITCH_DECLARE(int) switch_vb_frame_count(switch_vb_t *vb);
SWITCH_DECLARE(int) switch_vb_poll(switch_vb_t *vb);
SWITCH_DECLARE(switch_status_t) switch_vb_put_packet(switch_vb_t *vb, switch_rtp_packet_t *packet, switch_size_t len);
SWITCH_DECLARE(switch_status_t) switch_vb_get_packet(switch_vb_t *vb, switch_rtp_packet_t *packet, switch_size_t *len);

SWITCH_END_EXTERN_C
#endif

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
