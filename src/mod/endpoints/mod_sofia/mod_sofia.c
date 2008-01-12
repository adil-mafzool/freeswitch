/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005/2006, Anthony Minessale II <anthmct@yahoo.com>
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
 * Anthony Minessale II <anthmct@yahoo.com>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * 
 * Anthony Minessale II <anthmct@yahoo.com>
 * Ken Rice, Asteria Solutions Group, Inc <ken@asteriasgi.com>
 * Paul D. Tinsley <pdt at jackhammer.org>
 * Bret McDanel <trixter AT 0xdecafbad.com>
 *
 *
 * mod_sofia.c -- SOFIA SIP Endpoint
 *
 */

/* Best viewed in a 160 x 60 VT100 Terminal or so the line below at least fits across your screen*/
/*************************************************************************************************************************************************************/
#include "mod_sofia.h"

SWITCH_MODULE_LOAD_FUNCTION(mod_sofia_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_sofia_shutdown);
SWITCH_MODULE_DEFINITION(mod_sofia, mod_sofia_load, mod_sofia_shutdown, NULL);

struct mod_sofia_globals mod_sofia_globals;
switch_endpoint_interface_t *sofia_endpoint_interface;
static switch_frame_t silence_frame = { 0 };
static char silence_data[13] = "";

#define STRLEN 15

static switch_memory_pool_t *module_pool = NULL;

static switch_status_t sofia_on_init(switch_core_session_t *session);

static switch_status_t sofia_on_loopback(switch_core_session_t *session);
static switch_status_t sofia_on_transmit(switch_core_session_t *session);
static switch_call_cause_t sofia_outgoing_channel(switch_core_session_t *session,
												  switch_caller_profile_t *outbound_profile, switch_core_session_t **new_session,
												  switch_memory_pool_t **pool, switch_originate_flag_t flags);
static switch_status_t sofia_read_frame(switch_core_session_t *session, switch_frame_t **frame, int timeout, switch_io_flag_t flags, int stream_id);
static switch_status_t sofia_write_frame(switch_core_session_t *session, switch_frame_t *frame, int timeout, switch_io_flag_t flags, int stream_id);
static switch_status_t sofia_read_video_frame(switch_core_session_t *session, switch_frame_t **frame, int timeout, switch_io_flag_t flags, int stream_id);
static switch_status_t sofia_write_video_frame(switch_core_session_t *session, switch_frame_t *frame, int timeout, switch_io_flag_t flags, int stream_id);
static switch_status_t sofia_kill_channel(switch_core_session_t *session, int sig);

/* BODY OF THE MODULE */
/*************************************************************************************************************************************************************/

/* 
   State methods they get called when the state changes to the specific state 
   returning SWITCH_STATUS_SUCCESS tells the core to execute the standard state method next
   so if you fully implement the state you can return SWITCH_STATUS_FALSE to skip it.
*/
static switch_status_t sofia_on_init(switch_core_session_t *session)
{
	switch_channel_t *channel = NULL;
	private_object_t *tech_pvt;
	
	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	tech_pvt->read_frame.buflen = SWITCH_RTP_MAX_BUF_LEN;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%s SOFIA INIT\n", switch_channel_get_name(channel));
	if (switch_channel_test_flag(channel, CF_BYPASS_MEDIA)) {
		sofia_glue_tech_absorb_sdp(tech_pvt);
	}

	if (switch_test_flag(tech_pvt, TFLAG_OUTBOUND)) {
		if (sofia_glue_do_invite(session) != SWITCH_STATUS_SUCCESS) {
			switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
			return SWITCH_STATUS_FALSE;
		}
	}

	/* Move Channel's State Machine to RING */
	switch_channel_set_state(channel, CS_RING);
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_on_ring(switch_core_session_t *session)
{
	switch_channel_t *channel = NULL;
	private_object_t *tech_pvt = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	switch_clear_flag_locked(tech_pvt, TFLAG_SIP_HOLD);
	
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%s SOFIA RING\n", switch_channel_get_name(channel));

	return SWITCH_STATUS_SUCCESS;
}


static switch_status_t sofia_on_reset(switch_core_session_t *session)
{
	switch_channel_t *channel = NULL;
	private_object_t *tech_pvt = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	switch_clear_flag_locked(tech_pvt, TFLAG_SIP_HOLD);
	
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%s SOFIA RESET\n", switch_channel_get_name(channel));

	return SWITCH_STATUS_SUCCESS;
}


static switch_status_t sofia_on_hibernate(switch_core_session_t *session)
{
	switch_channel_t *channel = NULL;
	private_object_t *tech_pvt = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	switch_clear_flag_locked(tech_pvt, TFLAG_SIP_HOLD);
	
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%s SOFIA HIBERNATE\n", switch_channel_get_name(channel));

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_on_execute(switch_core_session_t *session)
{
	switch_channel_t *channel = NULL;
	private_object_t *tech_pvt = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	switch_clear_flag_locked(tech_pvt, TFLAG_SIP_HOLD);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%s SOFIA EXECUTE\n", switch_channel_get_name(channel));

	return SWITCH_STATUS_SUCCESS;
}

/* map QSIG cause codes to SIP from RFC4497 section 8.4.1 */
static int hangup_cause_to_sip(switch_call_cause_t cause)
{		
	switch (cause) {
	case SWITCH_CAUSE_NO_ROUTE_TRANSIT_NET:
	case SWITCH_CAUSE_NO_ROUTE_DESTINATION:
		return 404;
	case SWITCH_CAUSE_USER_BUSY:
		return 486;
	case SWITCH_CAUSE_NO_USER_RESPONSE:
		return 408;
	case SWITCH_CAUSE_NO_ANSWER:
		return 480;
	case SWITCH_CAUSE_SUBSCRIBER_ABSENT:
		return 480;
	case SWITCH_CAUSE_CALL_REJECTED:
		return 603;
	case SWITCH_CAUSE_NUMBER_CHANGED:
	case SWITCH_CAUSE_REDIRECTION_TO_NEW_DESTINATION:
		return 410;
	case SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER:
		return 502;
	case SWITCH_CAUSE_INVALID_NUMBER_FORMAT:
		return 484;
	case SWITCH_CAUSE_FACILITY_REJECTED:
		return 501;
	case SWITCH_CAUSE_NORMAL_UNSPECIFIED:
		return 480;
	case SWITCH_CAUSE_REQUESTED_CHAN_UNAVAIL:
	case SWITCH_CAUSE_NORMAL_CIRCUIT_CONGESTION:
	case SWITCH_CAUSE_NETWORK_OUT_OF_ORDER:
	case SWITCH_CAUSE_NORMAL_TEMPORARY_FAILURE:
	case SWITCH_CAUSE_SWITCH_CONGESTION:
		return 503;
	case SWITCH_CAUSE_OUTGOING_CALL_BARRED:
	case SWITCH_CAUSE_INCOMING_CALL_BARRED:
	case SWITCH_CAUSE_BEARERCAPABILITY_NOTAUTH:
		return 403;
	case SWITCH_CAUSE_BEARERCAPABILITY_NOTAVAIL:
		return 503;
	case SWITCH_CAUSE_BEARERCAPABILITY_NOTIMPL:
	case SWITCH_CAUSE_INCOMPATIBLE_DESTINATION:
		return 488;
	case SWITCH_CAUSE_FACILITY_NOT_IMPLEMENTED:
	case SWITCH_CAUSE_SERVICE_NOT_IMPLEMENTED:
		return 501;
	case SWITCH_CAUSE_RECOVERY_ON_TIMER_EXPIRE:
		return 504;
	case SWITCH_CAUSE_ORIGINATOR_CANCEL:
		return 487;
	case SWITCH_CAUSE_EXCHANGE_ROUTING_ERROR:
		return 483;
	default:
		return 480;
	}
}

switch_status_t sofia_on_hangup(switch_core_session_t *session)
{
	switch_core_session_t *a_session;
	private_object_t *tech_pvt;
	switch_channel_t *channel = NULL;
	switch_call_cause_t cause;
	int sip_cause;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);
	
	if (tech_pvt->profile->rtpip && tech_pvt->local_sdp_audio_port) {
		switch_rtp_release_port(tech_pvt->profile->rtpip, tech_pvt->local_sdp_audio_port);
	}

	cause = switch_channel_get_cause(channel);

	if (switch_test_flag(tech_pvt, TFLAG_SIP_HOLD) && cause != SWITCH_CAUSE_ATTENDED_TRANSFER) {
		const char *buuid;
        switch_core_session_t *bsession;
        switch_channel_t *bchannel;
        const char *lost_ext;

		if (tech_pvt->max_missed_packets) {
			switch_rtp_set_max_missed_packets(tech_pvt->rtp_session, tech_pvt->max_missed_packets);
		}
		switch_channel_presence(tech_pvt->channel, "unknown", "unhold");

		if ((buuid = switch_channel_get_variable(channel, SWITCH_SIGNAL_BOND_VARIABLE))) {
            if ((bsession = switch_core_session_locate(buuid))) {
                bchannel = switch_core_session_get_channel(bsession);
                if (switch_channel_test_flag(bchannel, CF_BROADCAST)) {
                    if ((lost_ext = switch_channel_get_variable(bchannel, "left_hanging_extension"))) {
                        switch_ivr_session_transfer(bsession, lost_ext, NULL, NULL);
                    }
					switch_channel_stop_broadcast(bchannel);
                }
                switch_core_session_rwunlock(bsession);
            }
        }

		switch_clear_flag_locked(tech_pvt, TFLAG_SIP_HOLD);
		
	}

	sip_cause = hangup_cause_to_sip(cause);

	sofia_glue_deactivate_rtp(tech_pvt);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Channel %s hanging up, cause: %s\n",
					  switch_channel_get_name(channel), switch_channel_cause2str(cause));

	if (tech_pvt->hash_key) {
		switch_core_hash_delete(tech_pvt->profile->chat_hash, tech_pvt->hash_key);
	}

	if (session) {
		const char *call_id = tech_pvt->call_id;
		char *sql;
		sql = switch_mprintf("delete from sip_dialogs where call_id='%q'", call_id);
		switch_assert(sql);
        sofia_glue_execute_sql(tech_pvt->profile, SWITCH_FALSE, sql, tech_pvt->profile->ireg_mutex);
		free(sql);
	}

	if (tech_pvt->kick && (a_session = switch_core_session_locate(tech_pvt->kick))) {
		switch_channel_t *a_channel = switch_core_session_get_channel(a_session);
		switch_channel_hangup(a_channel, switch_channel_get_cause(channel));
		switch_core_session_rwunlock(a_session);
	}

	switch_mutex_lock(tech_pvt->profile->flag_mutex);

	if (tech_pvt->nh  && !switch_test_flag(tech_pvt, TFLAG_BYE)) {
		if (switch_test_flag(tech_pvt, TFLAG_ANS)) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Sending BYE to %s\n", switch_channel_get_name(channel));
			nua_bye(tech_pvt->nh, TAG_END());
		} else {
			if (switch_test_flag(tech_pvt, TFLAG_OUTBOUND)) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Sending CANCEL to %s\n", switch_channel_get_name(channel));
				nua_cancel(tech_pvt->nh, TAG_END());
			} else {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Responding to INVITE with: %d\n", sip_cause);
				nua_respond(tech_pvt->nh, sip_cause, NULL, TAG_END());
			}
		}
		switch_set_flag(tech_pvt, TFLAG_BYE);
	}

	switch_clear_flag(tech_pvt, TFLAG_IO);
	tech_pvt->profile->inuse--;
	switch_mutex_unlock(tech_pvt->profile->flag_mutex);

	if (tech_pvt->sofia_private) {
		*tech_pvt->sofia_private->uuid = '\0';
	}

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_on_loopback(switch_core_session_t *session)
{
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "SOFIA LOOPBACK\n");
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_on_transmit(switch_core_session_t *session)
{
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "SOFIA TRANSMIT\n");
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_answer_channel(switch_core_session_t *session)
{
	private_object_t *tech_pvt;
	switch_channel_t *channel = NULL;
	switch_status_t status;
	uint32_t session_timeout = 0;
	const char *val;

	switch_assert(session != NULL);

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);

	if (switch_test_flag(tech_pvt, TFLAG_ANS) || switch_channel_test_flag(channel, CF_OUTBOUND)) {
		return SWITCH_STATUS_SUCCESS;
	}

	switch_set_flag_locked(tech_pvt, TFLAG_ANS);

	if (switch_channel_test_flag(channel, CF_BYPASS_MEDIA)) {
		const char *sdp = NULL;
		if ((sdp = switch_channel_get_variable(channel, SWITCH_B_SDP_VARIABLE))) {
			tech_pvt->local_sdp_str = switch_core_session_strdup(session, sdp);
		}
	} else {
		if (switch_test_flag(tech_pvt, TFLAG_LATE_NEGOTIATION)) {
			switch_clear_flag_locked(tech_pvt, TFLAG_LATE_NEGOTIATION);
			if (!switch_channel_test_flag(tech_pvt->channel, CF_OUTBOUND)) {
				const char *r_sdp = switch_channel_get_variable(channel, SWITCH_R_SDP_VARIABLE);
				tech_pvt->num_codecs = 0;
				sofia_glue_tech_prepare_codecs(tech_pvt);
				if (sofia_glue_tech_media(tech_pvt, r_sdp) != SWITCH_STATUS_SUCCESS) {
					switch_channel_set_variable(channel, SWITCH_ENDPOINT_DISPOSITION_VARIABLE, "CODEC NEGOTIATION ERROR");
					nua_respond(tech_pvt->nh, SIP_488_NOT_ACCEPTABLE, TAG_END());
					return SWITCH_STATUS_FALSE;
				}
			}
		}

		if ((status = sofia_glue_tech_choose_port(tech_pvt)) != SWITCH_STATUS_SUCCESS) {
			switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
			return status;
		}

		sofia_glue_set_local_sdp(tech_pvt, NULL, 0, NULL, 0);
		if (sofia_glue_activate_rtp(tech_pvt, 0) != SWITCH_STATUS_SUCCESS) {
			switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
		}

		if (tech_pvt->nh) {
			if (tech_pvt->local_sdp_str) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Local SDP %s:\n%s\n", switch_channel_get_name(channel),
								  tech_pvt->local_sdp_str);
			}
		}
	}


	if ((val = switch_channel_get_variable(channel, SOFIA_SESSION_TIMEOUT))) {
		int v_session_timeout = atoi(val);
		if (v_session_timeout >= 0) {
			session_timeout = v_session_timeout;
		}
	}

	nua_respond(tech_pvt->nh, SIP_200_OK,
				NUTAG_AUTOANSWER(0),
				NUTAG_SESSION_TIMER(session_timeout),
				SIPTAG_CONTACT_STR(tech_pvt->reply_contact),
				SOATAG_USER_SDP_STR(tech_pvt->local_sdp_str),
				SOATAG_AUDIO_AUX("cn telephone-event"),
				NUTAG_INCLUDE_EXTRA_SDP(1),
				TAG_END());

	return SWITCH_STATUS_SUCCESS;
}



static switch_status_t sofia_read_video_frame(switch_core_session_t *session, switch_frame_t **frame, int timeout, switch_io_flag_t flags, int stream_id)
{
	private_object_t *tech_pvt = NULL;
	switch_channel_t *channel = NULL;
	int payload = 0;
	
	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	if (switch_test_flag(tech_pvt, TFLAG_HUP)) {
		return SWITCH_STATUS_FALSE;
	}

	while (!(tech_pvt->video_read_codec.implementation && switch_rtp_ready(tech_pvt->video_rtp_session))) {
		if (switch_channel_ready(channel)) {
			switch_yield(10000);
		} else {
			return SWITCH_STATUS_GENERR;
		}
	}


	tech_pvt->video_read_frame.datalen = 0;


	if (switch_test_flag(tech_pvt, TFLAG_IO)) {
		switch_status_t status;

		if (!switch_test_flag(tech_pvt, TFLAG_RTP)) {
			return SWITCH_STATUS_GENERR;
		}

		switch_assert(tech_pvt->rtp_session != NULL);
		tech_pvt->video_read_frame.datalen = 0;

		while (switch_test_flag(tech_pvt, TFLAG_IO) && tech_pvt->video_read_frame.datalen == 0) {
			tech_pvt->video_read_frame.flags = SFF_NONE;

			status = switch_rtp_zerocopy_read_frame(tech_pvt->video_rtp_session, &tech_pvt->video_read_frame);
			if (status != SWITCH_STATUS_SUCCESS && status != SWITCH_STATUS_BREAK) {
				if (status == SWITCH_STATUS_TIMEOUT) {
					switch_channel_hangup(tech_pvt->channel, SWITCH_CAUSE_MEDIA_TIMEOUT);
				}
				return status;
			}
			
			payload = tech_pvt->video_read_frame.payload;

			if (tech_pvt->video_read_frame.datalen > 0) {
				break;
			}
		}
	}

	if (tech_pvt->video_read_frame.datalen == 0) {
		*frame = NULL;
		return SWITCH_STATUS_GENERR;
	}

	*frame = &tech_pvt->video_read_frame;

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_write_video_frame(switch_core_session_t *session, switch_frame_t *frame, int timeout, switch_io_flag_t flags, int stream_id)
{
	private_object_t *tech_pvt;
	switch_channel_t *channel = NULL;
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	while (!(tech_pvt->video_read_codec.implementation && switch_rtp_ready(tech_pvt->video_rtp_session))) {
		if (switch_channel_ready(channel)) {
			switch_yield(10000);
		} else {
			return SWITCH_STATUS_GENERR;
		}
	}

	if (switch_test_flag(tech_pvt, TFLAG_HUP)) {
		return SWITCH_STATUS_FALSE;
	}

	if (!switch_test_flag(tech_pvt, TFLAG_RTP)) {
		return SWITCH_STATUS_GENERR;
	}

	if (!switch_test_flag(tech_pvt, TFLAG_IO)) {
		return SWITCH_STATUS_SUCCESS;
	}

	if (!switch_test_flag(frame, SFF_CNG)) {
		switch_rtp_write_frame(tech_pvt->video_rtp_session, frame);
	}
	
	return status;
}


static switch_status_t sofia_read_frame(switch_core_session_t *session, switch_frame_t **frame, int timeout, switch_io_flag_t flags, int stream_id)
{
	private_object_t *tech_pvt = NULL;
	switch_channel_t *channel = NULL;
	int payload = 0;
	
	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	if (!(tech_pvt->profile->pflags & PFLAG_RUNNING)) {
		switch_channel_hangup(tech_pvt->channel, SWITCH_CAUSE_NORMAL_CLEARING);
		return SWITCH_STATUS_FALSE;
	}

	if (switch_test_flag(tech_pvt, TFLAG_HUP)) {
		return SWITCH_STATUS_FALSE;
	}

	while (!(tech_pvt->read_codec.implementation && switch_rtp_ready(tech_pvt->rtp_session))) {
		if (switch_channel_ready(channel)) {
			switch_yield(10000);
		} else {
			return SWITCH_STATUS_GENERR;
		}
	}

	tech_pvt->read_frame.datalen = 0;
	switch_set_flag_locked(tech_pvt, TFLAG_READING);

	if (switch_test_flag(tech_pvt, TFLAG_IO)) {
		switch_status_t status;
		
		if (!switch_test_flag(tech_pvt, TFLAG_RTP)) {
			return SWITCH_STATUS_GENERR;
		}

		switch_assert(tech_pvt->rtp_session != NULL);
		tech_pvt->read_frame.datalen = 0;


		while (switch_test_flag(tech_pvt, TFLAG_IO) && tech_pvt->read_frame.datalen == 0) {
			tech_pvt->read_frame.flags = SFF_NONE;

			status = switch_rtp_zerocopy_read_frame(tech_pvt->rtp_session, &tech_pvt->read_frame);
			if (status != SWITCH_STATUS_SUCCESS && status != SWITCH_STATUS_BREAK) {
				if (status == SWITCH_STATUS_TIMEOUT) {
					switch_channel_hangup(tech_pvt->channel, SWITCH_CAUSE_MEDIA_TIMEOUT);
				}
				return status;
			}
			
			payload = tech_pvt->read_frame.payload;

			if (switch_rtp_has_dtmf(tech_pvt->rtp_session)) {
				switch_dtmf_t dtmf = {0};
				switch_rtp_dequeue_dtmf(tech_pvt->rtp_session, &dtmf);
				switch_channel_queue_dtmf(channel, &dtmf);
			}


			if (tech_pvt->read_frame.datalen > 0) {
				size_t bytes = 0;
				int frames = 1;

				if (!switch_test_flag((&tech_pvt->read_frame), SFF_CNG)) {
					if ((bytes = tech_pvt->read_codec.implementation->encoded_bytes_per_frame)) {
						frames = (tech_pvt->read_frame.datalen / bytes);
					}
					tech_pvt->read_frame.samples = (int) (frames * tech_pvt->read_codec.implementation->samples_per_frame);
				}
				break;
			}
		}
	}

	switch_clear_flag_locked(tech_pvt, TFLAG_READING);

	if (tech_pvt->read_frame.datalen == 0) {
		*frame = NULL;
		return SWITCH_STATUS_GENERR;
	}

	*frame = &tech_pvt->read_frame;

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_write_frame(switch_core_session_t *session, switch_frame_t *frame, int timeout, switch_io_flag_t flags, int stream_id)
{
	private_object_t *tech_pvt;
	switch_channel_t *channel = NULL;
	switch_status_t status = SWITCH_STATUS_SUCCESS;
	int bytes = 0, samples = 0, frames = 0;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	while (!(tech_pvt->read_codec.implementation && switch_rtp_ready(tech_pvt->rtp_session))) {
		if (switch_channel_ready(channel)) {
			switch_yield(10000);
		} else {
			return SWITCH_STATUS_GENERR;
		}
	}

	if (!tech_pvt->read_codec.implementation) {
		return SWITCH_STATUS_GENERR;
	}

	if (switch_test_flag(tech_pvt, TFLAG_HUP)) {
		return SWITCH_STATUS_FALSE;
	}

	if (!switch_test_flag(tech_pvt, TFLAG_RTP)) {
		return SWITCH_STATUS_GENERR;
	}

	if (!switch_test_flag(tech_pvt, TFLAG_IO)) {
		return SWITCH_STATUS_SUCCESS;
	}

	switch_set_flag_locked(tech_pvt, TFLAG_WRITING);

	if (!switch_test_flag(frame, SFF_CNG)) {
		if (tech_pvt->read_codec.implementation->encoded_bytes_per_frame) {
			bytes = tech_pvt->read_codec.implementation->encoded_bytes_per_frame;
			frames = ((int) frame->datalen / bytes);
		} else
			frames = 1;

		samples = frames * tech_pvt->read_codec.implementation->samples_per_frame;
	}

	tech_pvt->timestamp_send += samples;
	switch_rtp_write_frame(tech_pvt->rtp_session, frame);

	switch_clear_flag_locked(tech_pvt, TFLAG_WRITING);
	return status;
}



static switch_status_t sofia_kill_channel(switch_core_session_t *session, int sig)
{
	private_object_t *tech_pvt;
	switch_channel_t *channel = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);


	switch (sig) {
	case SWITCH_SIG_BREAK:
		if (switch_rtp_ready(tech_pvt->rtp_session)) {
			switch_rtp_set_flag(tech_pvt->rtp_session, SWITCH_RTP_FLAG_BREAK);
		}
		if (switch_rtp_ready(tech_pvt->video_rtp_session)) {
			switch_rtp_set_flag(tech_pvt->video_rtp_session, SWITCH_RTP_FLAG_BREAK);
		}
		break;
	case SWITCH_SIG_KILL:
	default:
		switch_clear_flag_locked(tech_pvt, TFLAG_IO);
		switch_set_flag_locked(tech_pvt, TFLAG_HUP);

		if (switch_rtp_ready(tech_pvt->rtp_session)) {
			switch_rtp_kill_socket(tech_pvt->rtp_session);
		}
		if (switch_rtp_ready(tech_pvt->video_rtp_session)) {
			switch_rtp_kill_socket(tech_pvt->video_rtp_session);
		}
		break;
	}
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_waitfor_read(switch_core_session_t *session, int ms, int stream_id)
{
	private_object_t *tech_pvt;
	switch_channel_t *channel = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_waitfor_write(switch_core_session_t *session, int ms, int stream_id)
{
	private_object_t *tech_pvt;
	switch_channel_t *channel = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_send_dtmf(switch_core_session_t *session, const switch_dtmf_t *dtmf)
{
	private_object_t *tech_pvt;
	char message[128] = "";

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	switch (tech_pvt->dtmf_type) {
	case DTMF_2833:
		return switch_rtp_queue_rfc2833(tech_pvt->rtp_session, dtmf);

	case DTMF_INFO:
		snprintf(message, sizeof(message), "Signal=%c\r\nDuration=%d\r\n", dtmf->digit, dtmf->duration);
		nua_info(tech_pvt->nh,
				 //NUTAG_WITH_THIS(tech_pvt->profile->nua),
				 SIPTAG_CONTENT_TYPE_STR("application/dtmf-relay"),
				 SIPTAG_PAYLOAD_STR(message),
				 TAG_END());
		break;
	default:
		break;
	}

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_receive_message(switch_core_session_t *session, switch_core_session_message_t *msg)
{
	switch_channel_t *channel;
	private_object_t *tech_pvt;
	switch_status_t status;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = (private_object_t *) switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	switch (msg->message_id) {
	case SWITCH_MESSAGE_INDICATE_BROADCAST: {
		const char *ip = NULL, *port = NULL;
		ip = switch_channel_get_variable(channel, SWITCH_REMOTE_MEDIA_IP_VARIABLE);
		port = switch_channel_get_variable(channel, SWITCH_REMOTE_MEDIA_PORT_VARIABLE);
		if (ip && port) {
			sofia_glue_set_local_sdp(tech_pvt, ip, atoi(port), msg->string_arg, 1);
		}
		nua_respond(tech_pvt->nh, SIP_200_OK,
					SIPTAG_CONTACT_STR(tech_pvt->reply_contact),
					SOATAG_USER_SDP_STR(tech_pvt->local_sdp_str),
					SOATAG_AUDIO_AUX("cn telephone-event"),
					NUTAG_INCLUDE_EXTRA_SDP(1),
					TAG_END());
		switch_channel_mark_answered(channel);
	}
		break;
	case SWITCH_MESSAGE_INDICATE_NOMEDIA: 
		{
			const char *uuid;
			switch_core_session_t *other_session;
			switch_channel_t *other_channel;
			const char *ip = NULL, *port = NULL;

			if (switch_channel_get_state(channel) >= CS_HANGUP) {
				return SWITCH_STATUS_FALSE;
			}

			switch_channel_set_flag(channel, CF_BYPASS_MEDIA);
			tech_pvt->local_sdp_str = NULL;
			if ((uuid = switch_channel_get_variable(channel, SWITCH_SIGNAL_BOND_VARIABLE))
				&& (other_session = switch_core_session_locate(uuid))) {
				other_channel = switch_core_session_get_channel(other_session);
				ip = switch_channel_get_variable(other_channel, SWITCH_REMOTE_MEDIA_IP_VARIABLE);
				port = switch_channel_get_variable(other_channel, SWITCH_REMOTE_MEDIA_PORT_VARIABLE);
				switch_core_session_rwunlock(other_session);
				if (ip && port) {
					sofia_glue_set_local_sdp(tech_pvt, ip, atoi(port), NULL, 1);
				}
			}
			if (!tech_pvt->local_sdp_str) {
				sofia_glue_tech_absorb_sdp(tech_pvt);
			}
			sofia_glue_do_invite(session);
		}
		break;

	case SWITCH_MESSAGE_INDICATE_MEDIA_REDIRECT:
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Sending media re-direct:\n%s\n", msg->string_arg);
			tech_pvt->local_sdp_str = switch_core_session_strdup(session, msg->string_arg);
			sofia_glue_do_invite(session);
		}
		break;

	case SWITCH_MESSAGE_INDICATE_MEDIA:
		{
			uint32_t count = 0;

			if (switch_channel_get_state(channel) >= CS_HANGUP) {
				return SWITCH_STATUS_FALSE;
			}

			switch_channel_clear_flag(channel, CF_BYPASS_MEDIA);
			tech_pvt->local_sdp_str = NULL;
			if (!switch_rtp_ready(tech_pvt->rtp_session)) {
				sofia_glue_tech_prepare_codecs(tech_pvt);
				if ((status = sofia_glue_tech_choose_port(tech_pvt)) != SWITCH_STATUS_SUCCESS) {
					switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
					return status;
				}
			}
			sofia_glue_set_local_sdp(tech_pvt, NULL, 0, NULL, 1);
			sofia_glue_do_invite(session);

			/* wait for rtp to start and first real frame to arrive */
			tech_pvt->read_frame.datalen = 0;
			while (switch_test_flag(tech_pvt, TFLAG_IO) && switch_channel_get_state(channel) < CS_HANGUP && !switch_rtp_ready(tech_pvt->rtp_session)) {
				if (++count > 1000) {
					return SWITCH_STATUS_FALSE;
				}
				if (!switch_rtp_ready(tech_pvt->rtp_session)) {
					switch_yield(1000);
					continue;
				}
				break;
			}
		}
		break;

	case SWITCH_MESSAGE_INDICATE_HOLD:
		{
			switch_set_flag_locked(tech_pvt, TFLAG_SIP_HOLD);
			sofia_glue_do_invite(session);
		}
		break;
		
	case SWITCH_MESSAGE_INDICATE_UNHOLD:
		{
			switch_clear_flag_locked(tech_pvt, TFLAG_SIP_HOLD);
			sofia_glue_do_invite(session);
		}
		break;
	case SWITCH_MESSAGE_INDICATE_BRIDGE:
		
		if (switch_test_flag(tech_pvt, TFLAG_XFER)) {
			switch_clear_flag_locked(tech_pvt, TFLAG_XFER);
			if (msg->pointer_arg) {
				switch_core_session_t *a_session, *b_session = msg->pointer_arg;

				if ((a_session = switch_core_session_locate(tech_pvt->xferto))) {
					private_object_t *a_tech_pvt = switch_core_session_get_private(a_session);
					private_object_t *b_tech_pvt = switch_core_session_get_private(b_session);

					switch_set_flag_locked(a_tech_pvt, TFLAG_REINVITE);
					a_tech_pvt->remote_sdp_audio_ip = switch_core_session_strdup(a_session, b_tech_pvt->remote_sdp_audio_ip);
					a_tech_pvt->remote_sdp_audio_port = b_tech_pvt->remote_sdp_audio_port;
					a_tech_pvt->local_sdp_audio_ip = switch_core_session_strdup(a_session, b_tech_pvt->local_sdp_audio_ip);
					a_tech_pvt->local_sdp_audio_port = b_tech_pvt->local_sdp_audio_port;
					if (sofia_glue_activate_rtp(a_tech_pvt, 0) != SWITCH_STATUS_SUCCESS) {
						switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
					}
					b_tech_pvt->kick = switch_core_session_strdup(b_session, tech_pvt->xferto);
					switch_core_session_rwunlock(a_session);
				}

				msg->pointer_arg = NULL;
				return SWITCH_STATUS_FALSE;
			}
		}
		/*
		if (tech_pvt->rtp_session && switch_test_flag(tech_pvt, TFLAG_TIMER)) {
			switch_rtp_clear_flag(tech_pvt->rtp_session, SWITCH_RTP_FLAG_USE_TIMER);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "De-activate timed RTP!\n");
		}
		*/
		break;
	case SWITCH_MESSAGE_INDICATE_UNBRIDGE:
		/*
		if (tech_pvt->rtp_session && switch_test_flag(tech_pvt, TFLAG_TIMER)) {
			switch_rtp_set_flag(tech_pvt->rtp_session, SWITCH_RTP_FLAG_USE_TIMER);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Re-activate timed RTP!\n");
		}
		*/
		break;
	case SWITCH_MESSAGE_INDICATE_REDIRECT:
		if (msg->string_arg) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Re-directing to %s\n", msg->string_arg);
			nua_respond(tech_pvt->nh, SIP_302_MOVED_TEMPORARILY, SIPTAG_CONTACT_STR(msg->string_arg), TAG_END());
		}
		break;

	case SWITCH_MESSAGE_INDICATE_DEFLECT:
		{
			char ref_to[128] = "";

			if (!strstr(msg->string_arg, "sip:")) {
				switch_snprintf(ref_to, sizeof(ref_to), "sip:%s@%s", msg->string_arg, tech_pvt->profile->sipip);
			} else {
				switch_set_string(ref_to, msg->string_arg);
			}

			nua_refer(tech_pvt->nh, SIPTAG_REFER_TO_STR(ref_to), SIPTAG_REFERRED_BY_STR(tech_pvt->contact_url), TAG_END());
		}
		break;

	case SWITCH_MESSAGE_INDICATE_RESPOND:
		if (msg->numeric_arg || msg->string_arg) {
			int code = msg->numeric_arg;
			const char *reason = NULL;

			if (code) {
				reason = msg->string_arg;
			} else {
				if (!switch_strlen_zero(msg->string_arg)){
					code = atoi(msg->string_arg);
					if ((reason = strchr(msg->string_arg, ' '))) {
						reason++;
					}
				}
			}

			if (!reason && code != 407) {
				reason = "Call Refused";
			}

			if (!code) {
				code = 488;
			}

			if (code == 407) {
				const char *to_uri = switch_channel_get_variable(channel, "sip_to_uri");
				const char *to_host = reason;
					
				if (switch_strlen_zero(to_host)) {
					to_host = switch_channel_get_variable(channel, "sip_to_host");
				}
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Challenging call %s\n", to_uri);
				sofia_reg_auth_challange(NULL, tech_pvt->profile, tech_pvt->nh, REG_INVITE, to_host, 0); 
				switch_channel_hangup(channel, SWITCH_CAUSE_USER_CHALLENGE);
			} else {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Responding with %d %s\n", code, reason);
				nua_respond(tech_pvt->nh, code, reason, TAG_END());
			}
			
		}
		break;
	case SWITCH_MESSAGE_INDICATE_RINGING:
		if (!switch_channel_test_flag(channel, CF_RING_READY) && 
			!switch_channel_test_flag(channel, CF_EARLY_MEDIA) && !switch_channel_test_flag(channel, CF_ANSWERED)) {
			nua_respond(tech_pvt->nh, SIP_180_RINGING, SIPTAG_CONTACT_STR(tech_pvt->profile->url), TAG_END());
			switch_channel_mark_ring_ready(channel);
		}
		break;
	case SWITCH_MESSAGE_INDICATE_ANSWER:
		sofia_answer_channel(session);
		break;
	case SWITCH_MESSAGE_INDICATE_PROGRESS:{
			if (!switch_test_flag(tech_pvt, TFLAG_ANS)) {
				
				switch_set_flag_locked(tech_pvt, TFLAG_EARLY_MEDIA);
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Asked to send early media by %s\n", msg->from);

				/* Transmit 183 Progress with SDP */
				if (switch_channel_test_flag(channel, CF_BYPASS_MEDIA)) {
					const char *sdp = NULL;
					if ((sdp = switch_channel_get_variable(channel, SWITCH_B_SDP_VARIABLE))) {
						tech_pvt->local_sdp_str = switch_core_session_strdup(session, sdp);
					}
				} else {
					if (switch_test_flag(tech_pvt, TFLAG_LATE_NEGOTIATION)) {
						switch_clear_flag_locked(tech_pvt, TFLAG_LATE_NEGOTIATION);
						if (!switch_channel_test_flag(tech_pvt->channel, CF_OUTBOUND)) {
							const char *r_sdp = switch_channel_get_variable(channel, SWITCH_R_SDP_VARIABLE);
							tech_pvt->num_codecs = 0;
							sofia_glue_tech_prepare_codecs(tech_pvt);
							if (sofia_glue_tech_media(tech_pvt, r_sdp) != SWITCH_STATUS_SUCCESS) {
								switch_channel_set_variable(channel, SWITCH_ENDPOINT_DISPOSITION_VARIABLE, "CODEC NEGOTIATION ERROR");
								nua_respond(tech_pvt->nh, SIP_488_NOT_ACCEPTABLE, TAG_END());
								return SWITCH_STATUS_FALSE;
							}
						}
					}

					if ((status = sofia_glue_tech_choose_port(tech_pvt)) != SWITCH_STATUS_SUCCESS) {
						switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
						return status;
					}
					sofia_glue_set_local_sdp(tech_pvt, NULL, 0, NULL, 0);
					if (sofia_glue_activate_rtp(tech_pvt, 0) != SWITCH_STATUS_SUCCESS) {
						switch_channel_hangup(channel, SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER);
					}
					if (tech_pvt->local_sdp_str) {
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Ring SDP:\n%s\n", tech_pvt->local_sdp_str);
					}
				}
				switch_channel_mark_pre_answered(channel);
				nua_respond(tech_pvt->nh,
							SIP_183_SESSION_PROGRESS,
							NUTAG_AUTOANSWER(0),
							SIPTAG_CONTACT_STR(tech_pvt->reply_contact),
							SOATAG_USER_SDP_STR(tech_pvt->local_sdp_str), SOATAG_AUDIO_AUX("cn telephone-event"), TAG_END());
			}
		}
		break;
	default:
		break;
	}

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t sofia_receive_event(switch_core_session_t *session, switch_event_t *event)
{
	switch_channel_t *channel;
	struct private_object *tech_pvt;
	char *body;
	nua_handle_t *msg_nh;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	tech_pvt = switch_core_session_get_private(session);
	switch_assert(tech_pvt != NULL);

	if (!(body = switch_event_get_body(event))) {
		body = "";
	}

	if (tech_pvt->hash_key) {
		msg_nh = nua_handle(tech_pvt->profile->nua, NULL,
							SIPTAG_FROM_STR(tech_pvt->chat_from),
							NUTAG_URL(tech_pvt->chat_to), SIPTAG_TO_STR(tech_pvt->chat_to), SIPTAG_CONTACT_STR(tech_pvt->profile->url), TAG_END());

		nua_message(msg_nh, SIPTAG_CONTENT_TYPE_STR("text/html"), SIPTAG_PAYLOAD_STR(body), TAG_END());
	}

	return SWITCH_STATUS_SUCCESS;
}

typedef switch_status_t (*sofia_command_t) (char **argv, int argc, switch_stream_handle_t *stream);

static const char *sofia_state_names[] = { "UNREGED",
										   "TRYING",
										   "REGISTER",
										   "REGED",
										   "UNREGISTER",
										   "FAILED",
										   "EXPIRED",
										   "NOREG",
										   NULL};

struct cb_helper {
	sofia_profile_t *profile;
	switch_stream_handle_t *stream;
};

#define switch_time_from_sec(sec)   ((switch_time_t)(sec) * 1000000)

static int show_reg_callback(void *pArg, int argc, char **argv, char **columnNames)
{
	struct cb_helper *cb = (struct cb_helper *) pArg;
	char exp_buf[128] = "";
	switch_time_exp_t tm;

	if (argv[6]) {
		switch_time_t etime = atoi(argv[6]);
		switch_size_t retsize;

		switch_time_exp_lt(&tm, switch_time_from_sec(etime));
		switch_strftime(exp_buf, &retsize, sizeof(exp_buf), "%Y-%m-%d %T", &tm);
	}

	cb->stream->write_function(cb->stream, 
							   "Call-ID \t%s\n"
							   "User    \t%s@%s\n"
							   "Contact \t%s\n"
							   "Agent   \t%s\n"
							   "Status  \t%s(%s) EXP(%s)\n\n", 
							   switch_str_nil(argv[0]), switch_str_nil(argv[1]), switch_str_nil(argv[2]), switch_str_nil(argv[3]), 
							   switch_str_nil(argv[7]), 
							   switch_str_nil(argv[4]), 
							   switch_str_nil(argv[5]), 
							   exp_buf);
	return 0;
}

static switch_status_t cmd_status(char **argv, int argc, switch_stream_handle_t *stream)
{
	sofia_profile_t *profile = NULL;
	sofia_gateway_t *gp;
	switch_hash_index_t *hi;
	void *val;
	const void *vvar;
	int c = 0;
	int ac = 0;
	const char *line = "=================================================================================================";

	if (argc > 0) {
		if (argc == 1) {
			stream->write_function(stream, "Invalid Syntax!\n");
			return SWITCH_STATUS_SUCCESS;
		}
		if (!strcasecmp(argv[0], "gateway")) {
			if ((gp = sofia_reg_find_gateway(argv[1]))) {
				switch_assert(gp->state < REG_STATE_LAST);
				
				stream->write_function(stream, "%s\n", line);
				stream->write_function(stream, "Name    \t%s\n", switch_str_nil(gp->name));
				stream->write_function(stream, "Scheme  \t%s\n", switch_str_nil(gp->register_scheme));
				stream->write_function(stream, "Realm   \t%s\n", switch_str_nil(gp->register_realm));
				stream->write_function(stream, "Username\t%s\n", switch_str_nil(gp->register_username));
				stream->write_function(stream, "Password\t%s\n", switch_strlen_zero(gp->register_password) ? "no" : "yes");
				stream->write_function(stream, "From    \t%s\n", switch_str_nil(gp->register_from));
				stream->write_function(stream, "Contact \t%s\n", switch_str_nil(gp->register_contact));
				stream->write_function(stream, "To      \t%s\n", switch_str_nil(gp->register_to));
				stream->write_function(stream, "Proxy   \t%s\n", switch_str_nil(gp->register_proxy));
				stream->write_function(stream, "Context \t%s\n", switch_str_nil(gp->register_context));
				stream->write_function(stream, "Expires \t%s\n", switch_str_nil(gp->expires_str));
				stream->write_function(stream, "Freq    \t%d\n", gp->freq);
				stream->write_function(stream, "State   \t%s\n", sofia_state_names[gp->state]);
				stream->write_function(stream, "%s\n", line);
				sofia_reg_release_gateway(gp);
			} else {
				stream->write_function(stream, "Invalid Gateway!\n");
			}
		} else if (!strcasecmp(argv[0], "profile")) {
			struct cb_helper cb;

			if ((profile = sofia_glue_find_profile(argv[1]))) {
				stream->write_function(stream, "%s\n", line);
				stream->write_function(stream, "Name       \t%s\n", switch_str_nil(argv[1]));
				if (strcasecmp(argv[1], profile->name)) {
				stream->write_function(stream, "Alias Of   \t%s\n", switch_str_nil(profile->name));
				}
				stream->write_function(stream, "DBName     \t%s\n", switch_str_nil(profile->dbname));
				stream->write_function(stream, "Dialplan   \t%s\n", switch_str_nil(profile->dialplan));
				stream->write_function(stream, "RTP-IP     \t%s\n", switch_str_nil(profile->rtpip));
				if (profile->extrtpip) {
				stream->write_function(stream, "Ext-RTP-IP \t%s\n", profile->extrtpip);
				}
				
				stream->write_function(stream, "SIP-IP     \t%s\n", switch_str_nil(profile->sipip));
				if (profile->extsipip) {
				stream->write_function(stream, "Ext-SIP-IP \t%s\n", profile->extsipip);
				}
				stream->write_function(stream, "URL        \t%s\n", switch_str_nil(profile->url));
				stream->write_function(stream, "BIND-URL   \t%s\n", switch_str_nil(profile->bindurl));
				stream->write_function(stream, "HOLD-MUSIC \t%s\n", switch_str_nil(profile->hold_music));
				stream->write_function(stream, "CODECS     \t%s\n", switch_str_nil(profile->codec_string));
				stream->write_function(stream, "TEL-EVENT  \t%d\n", profile->te);
				stream->write_function(stream, "CNG        \t%d\n", profile->cng_pt);
				stream->write_function(stream, "SESSION-TO \t%d\n", profile->session_timeout);
				stream->write_function(stream, "MAX-DIALOG \t%d\n", profile->max_proceeding);
				stream->write_function(stream, "\nRegistrations:\n%s\n", line);

				cb.profile = profile;
				cb.stream = stream;
				
				sofia_glue_execute_sql_callback(profile, SWITCH_FALSE, profile->ireg_mutex,
												"select * from sip_registrations",
												show_reg_callback, &cb);

				stream->write_function(stream, "%s\n", line);

				sofia_glue_release_profile(profile);
			} else {
				stream->write_function(stream, "Invalid Profile!\n");
			}
		} else {
			stream->write_function(stream, "Invalid Syntax!\n");
		}

		return SWITCH_STATUS_SUCCESS;
	}

	stream->write_function(stream, "%25s\t%s\t  %32s\t%s\n", "Name", "   Type", "Data", "State");
	stream->write_function(stream, "%s\n", line);
	switch_mutex_lock(mod_sofia_globals.hash_mutex);
	for (hi = switch_hash_first(NULL, mod_sofia_globals.profile_hash); hi; hi = switch_hash_next(hi)) {
		switch_hash_this(hi, &vvar, NULL, &val);
		profile = (sofia_profile_t *) val;
		if (sofia_test_pflag(profile, PFLAG_RUNNING)) {
			
			if (strcmp(vvar, profile->name)) {
				ac++;
				stream->write_function(stream, "%25s\t%s\t  %32s\t%s\n", vvar, "  alias", profile->name, "ALIASED");
			} else {
				stream->write_function(stream, "%25s\t%s\t  %32s\t%s (%u)\n", profile->name, "profile", profile->url,
									   sofia_test_pflag(profile, PFLAG_RUNNING) ? "RUNNING" : "DOWN", profile->inuse);
				c++;

				for (gp = profile->gateways; gp; gp = gp->next) {
					switch_assert(gp->state < REG_STATE_LAST);
					stream->write_function(stream, "%25s\t%s\t  %32s\t%s", gp->name, "gateway", gp->register_to, sofia_state_names[gp->state]);
					if (gp->state == REG_STATE_FAILED || gp->state == REG_STATE_TRYING) {
						stream->write_function(stream, " (retry: %ds)", gp->retry - switch_timestamp(NULL));
					}
					stream->write_function(stream, "\n");
				}
			}
		}
	}
	switch_mutex_unlock(mod_sofia_globals.hash_mutex);
	stream->write_function(stream, "%s\n", line);
	stream->write_function(stream, "%d profile%s %d alias%s\n", c, c == 1 ? "" : "s", ac, ac == 1 ? "" : "es");
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t cmd_profile(char **argv, int argc, switch_stream_handle_t *stream)
{
	sofia_profile_t *profile = NULL;
	char *profile_name = argv[0];
	const char *err;
	switch_xml_t xml_root;

	if (argc < 2) {
		stream->write_function(stream, "Invalid Args!\n");
		return SWITCH_STATUS_SUCCESS;
	}

	if (!strcasecmp(argv[1], "start")) {
		if (argc > 2 && !strcasecmp(argv[2], "reloadxml")) {
			if ((xml_root = switch_xml_open_root(1, &err))) {
				switch_xml_free(xml_root);
			}
			stream->write_function(stream, "Reload XML [%s]\n", err);
		}
		if (config_sofia(1, argv[0]) == SWITCH_STATUS_SUCCESS) {
			stream->write_function(stream, "%s started successfully\n", argv[0]);
		} else {
			stream->write_function(stream, "Failure starting %s\n", argv[0]);
		}
		return SWITCH_STATUS_SUCCESS;
	}
	
	if (switch_strlen_zero(profile_name) || !(profile = sofia_glue_find_profile(profile_name))) {
		stream->write_function(stream, "Invalid Profile [%s]", switch_str_nil(profile_name));
		return SWITCH_STATUS_SUCCESS;
	}

	if (!strcasecmp(argv[1], "flush_inbound_reg")) {
		sofia_reg_check_expire(profile, 0);
		stream->write_function(stream, "+OK\n");
		goto done;
	}

	if (!strcasecmp(argv[1], "register")) {
		char *gname = argv[2];
		sofia_gateway_t *gateway_ptr;

		if (switch_strlen_zero(gname)) {
			stream->write_function(stream, "No gateway name provided!\n");
			goto done;
		}

		if (!strcasecmp(gname, "all")) {
			for (gateway_ptr = profile->gateways; gateway_ptr; gateway_ptr = gateway_ptr->next) {
				gateway_ptr->retry = 0;
				gateway_ptr->state = REG_STATE_UNREGED;
			}
			stream->write_function(stream, "+OK\n");
		} else if ((gateway_ptr = sofia_reg_find_gateway(gname))) {
			gateway_ptr->retry = 0;
			gateway_ptr->state = REG_STATE_UNREGED;
			stream->write_function(stream, "+OK\n");
			sofia_reg_release_gateway(gateway_ptr);
		} else {
			stream->write_function(stream, "Invalid gateway!\n");
		}
		
		goto done;
	}

	if (!strcasecmp(argv[1], "unregister")) {
		char *gname = argv[2];
		sofia_gateway_t *gateway_ptr;

		if (switch_strlen_zero(gname)) {
			stream->write_function(stream, "No gateway name provided!\n");
			goto done;
		}

		if (!strcasecmp(gname, "all")) {
			for (gateway_ptr = profile->gateways; gateway_ptr; gateway_ptr = gateway_ptr->next) {
				gateway_ptr->retry = 0;
				gateway_ptr->state = REG_STATE_UNREGISTER;
			}
			stream->write_function(stream, "+OK\n");
		} else if ((gateway_ptr = sofia_reg_find_gateway(gname))) {
			gateway_ptr->retry = 0;
			gateway_ptr->state = REG_STATE_UNREGISTER;
			stream->write_function(stream, "+OK\n");
			sofia_reg_release_gateway(gateway_ptr);
		} else {
			stream->write_function(stream, "Invalid gateway!\n");
		}
		goto done;
	}

	if (!strcasecmp(argv[1], "stop") || !strcasecmp(argv[1], "restart")) {
		int rsec = 3;
		int diff = (int) (switch_timestamp(NULL) - profile->started);
		int remain = rsec - diff;
		if (diff < rsec) {
			stream->write_function(stream, "Profile %s must be up for at least %d seconds to stop/restart.\nPlease wait %d second%s\n", 
								   profile->name, rsec, remain, remain == 1 ? "" : "s");
		} else {

			if (argc > 2 && !strcasecmp(argv[2], "reloadxml")) {
				if ((xml_root = switch_xml_open_root(1, &err))) {
					switch_xml_free(xml_root);
				}
				stream->write_function(stream, "Reload XML [%s]\n", err);
			}

			if (!strcasecmp(argv[1], "stop")) {
				sofia_clear_pflag_locked(profile, PFLAG_RUNNING);
				stream->write_function(stream, "stopping: %s", profile->name);
			} else {
				sofia_set_pflag_locked(profile, PFLAG_RESPAWN);
				sofia_clear_pflag_locked(profile, PFLAG_RUNNING);
				stream->write_function(stream, "restarting: %s", profile->name);
			}
		}
		goto done;
	}

	stream->write_function(stream, "-ERR Unknown command!\n");

 done:
	if (profile) {
		sofia_glue_release_profile(profile);
	}

	return SWITCH_STATUS_SUCCESS;
}

static int contact_callback(void *pArg, int argc, char **argv, char **columnNames)
{
	struct cb_helper *cb = (struct cb_helper *) pArg;
	char *contact;

	if (!switch_strlen_zero(argv[0]) && (contact = sofia_glue_get_url_from_contact(argv[0], 1))) {
		cb->stream->write_function(cb->stream, "sofia/%s/%s,", cb->profile->name, contact + 4);
		free(contact);
	}

	return 0;
}

SWITCH_STANDARD_API(sofia_contact_function)
{
	char *data;
	char *user = NULL;
	char *domain = NULL;
	char *profile_name = NULL;
	char *p;

	if (!cmd) {
		stream->write_function(stream, "%s", "");
		return SWITCH_STATUS_SUCCESS;
	}

	data = strdup(cmd);
	switch_assert(data);

	if ((p = strchr(data, '/'))) {
		profile_name = data;
		*p++ = '\0';
		user = p;
	} else {
		user = data;
	}

	if ((domain = strchr(user, '@'))) {
		*domain++ = '\0';
	}
	
	if (!profile_name && domain) {
		profile_name = domain;
	}

	if (user && profile_name) {
		char *sql;
		sofia_profile_t *profile;
		
		if (!(profile = sofia_glue_find_profile(profile_name))) {
			profile_name = domain;
			domain = NULL;
		}

		if (!profile && profile_name) {
			profile = sofia_glue_find_profile(profile_name);
		}

		if (profile) {
			struct cb_helper cb;
			switch_stream_handle_t mystream = { 0 };
			if (!domain || !strchr(domain, '.')) {
				domain = profile->name;
			}

			SWITCH_STANDARD_STREAM(mystream);
			cb.profile = profile;
			cb.stream = &mystream;
			
			sql = switch_mprintf("select contact from sip_registrations where sip_user='%q' and sip_host='%q'", user, domain);
			switch_assert(sql);
			sofia_glue_execute_sql_callback(profile, SWITCH_FALSE, profile->ireg_mutex, sql, contact_callback, &cb);
			switch_safe_free(sql);
			if (mystream.data) {
				char *str = mystream.data;
				*(str + (strlen(str) - 1)) = '\0';
			}
			stream->write_function(stream, "%s", mystream.data);
			switch_safe_free(mystream.data);
			goto end;
		}
	}
	
	stream->write_function(stream, "%s", "");

end:
	switch_safe_free(data);
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_STANDARD_API(sofia_function)
{
	char *argv[1024] = { 0 };
	int argc = 0;
	char *mycmd = NULL;
	switch_status_t status = SWITCH_STATUS_SUCCESS;
	sofia_command_t func = NULL;
	int lead = 1;
	const char *usage_string = "USAGE:\n"
		"--------------------------------------------------------------------------------\n"
		"sofia help\n"
		"sofia profile <profile_name> [start|stop|restart|flush_inbound_reg|[register|unregister] [<gateway name>|all]] [reloadxml]\n"
		"sofia status [[profile | gateway] <name>]\n"
		"sofia loglevel [0-9]\n"
		"--------------------------------------------------------------------------------\n";
		
	if (session) {
		return SWITCH_STATUS_FALSE;
	}
	
	if (switch_strlen_zero(cmd)) {
		stream->write_function(stream, "%s", usage_string);
		goto done;
	}

	if (!(mycmd = strdup(cmd))) {
		status = SWITCH_STATUS_MEMERR;
		goto done;
	}

	if (!(argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])))) || !argv[0]) {
		stream->write_function(stream, "%s", usage_string);
		goto done;
	}
	
	if (!strcasecmp(argv[0], "profile")) {
		func = cmd_profile;
	} else if (!strcasecmp(argv[0], "status")) {
		func = cmd_status;
	} else if (!strcasecmp(argv[0], "loglevel")) {
		if (argc > 1 && argv[1]) {
			int level;
			level = atoi(argv[1]);
			if (level >= 0 && level <= 9) {
				su_log_set_level(NULL, atoi(argv[1]));
				stream->write_function(stream, "Sofia-sip log level set to [%d]", level);
			} else {
				stream->write_function(stream, "%s", usage_string);
			}
		} else {
			stream->write_function(stream, "%s", usage_string);
		}
		goto done;
	} else if (!strcasecmp(argv[0], "help")) {
		stream->write_function(stream, "%s", usage_string);
		goto done;
	}

	if (func) {
		status = func(&argv[lead], argc - lead, stream);
	} else {
		stream->write_function(stream, "Unknown Command [%s]\n", argv[0]);
	}

  done:
	switch_safe_free(mycmd);
	return status;
}

switch_io_routines_t sofia_io_routines = {
	/*.outgoing_channel */ sofia_outgoing_channel,
	/*.read_frame */ sofia_read_frame,
	/*.write_frame */ sofia_write_frame,
	/*.kill_channel */ sofia_kill_channel,
	/*.waitfor_read */ sofia_waitfor_read,
	/*.waitfor_read */ sofia_waitfor_write,
	/*.send_dtmf */ sofia_send_dtmf,
	/*.receive_message */ sofia_receive_message,
	/*.receive_event */ sofia_receive_event,
	/*.state_change*/ NULL,
	/*.read_video_frame*/ sofia_read_video_frame,
	/*.write_video_frame*/ sofia_write_video_frame
};

switch_state_handler_table_t sofia_event_handlers = {
	/*.on_init */ sofia_on_init,
	/*.on_ring */ sofia_on_ring,
	/*.on_execute */ sofia_on_execute,
	/*.on_hangup */ sofia_on_hangup,
	/*.on_loopback */ sofia_on_loopback,
	/*.on_transmit */ sofia_on_transmit,
	/*.on_hold */ NULL,
	/*.on_hibernate*/ sofia_on_hibernate,
	/*.on_reset*/ sofia_on_reset
};

static switch_status_t sofia_manage(char *relative_oid, switch_management_action_t action, char *data, switch_size_t datalen)
{
	return SWITCH_STATUS_SUCCESS;
}


static switch_call_cause_t sofia_outgoing_channel(switch_core_session_t *session,
												  switch_caller_profile_t *outbound_profile, switch_core_session_t **new_session,
												  switch_memory_pool_t **pool, switch_originate_flag_t flags)
{
	switch_call_cause_t cause = SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER;
	switch_core_session_t *nsession = NULL;
	char *data, *profile_name, *dest;
	sofia_profile_t *profile = NULL;
	switch_caller_profile_t *caller_profile = NULL;
	private_object_t *tech_pvt = NULL;
	switch_channel_t *nchannel;
	char *host, *dest_to;

	*new_session = NULL;

	if (!(nsession = switch_core_session_request(sofia_endpoint_interface, pool))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Error Creating Session\n");
		goto error;
	}

	if (!(tech_pvt = (struct private_object *) switch_core_session_alloc(nsession, sizeof(*tech_pvt)))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Error Creating Session\n");
		goto error;
	}
	switch_mutex_init(&tech_pvt->flag_mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(nsession));

	data = switch_core_session_strdup(nsession, outbound_profile->destination_number);
	profile_name = data;

	nchannel = switch_core_session_get_channel(nsession);

	if (!strncasecmp(profile_name, "gateway", 7)) {
		char *gw;
		sofia_gateway_t *gateway_ptr = NULL;

		if (!(gw = strchr(profile_name, '/'))) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Invalid URL\n");
			cause = SWITCH_CAUSE_INVALID_NUMBER_FORMAT;
			goto error;
		}

		*gw++ = '\0';

		if (!(dest = strchr(gw, '/'))) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Invalid URL\n");
			cause = SWITCH_CAUSE_INVALID_NUMBER_FORMAT;
			goto error;
		}

		*dest++ = '\0';

		if (!(gateway_ptr = sofia_reg_find_gateway(gw))) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Invalid Gateway\n");
			cause = SWITCH_CAUSE_INVALID_NUMBER_FORMAT;
			goto error;
		}

		profile = gateway_ptr->profile;
		tech_pvt->gateway_name = switch_core_session_strdup(nsession, gateway_ptr->name);
		switch_channel_set_variable(nchannel, "sip_gateway_name", gateway_ptr->name);
		
		if (!switch_test_flag(gateway_ptr, REG_FLAG_CALLERID)) {
			tech_pvt->gateway_from_str = switch_core_session_strdup(nsession, gateway_ptr->register_from);
		}
		if (!strchr(dest, '@')) {
			tech_pvt->dest = switch_core_session_sprintf(nsession, "sip:%s@%s", dest, gateway_ptr->register_proxy + 4);
		} else {
			tech_pvt->dest = switch_core_session_sprintf(nsession, "sip:%s", dest);
		}
		tech_pvt->invite_contact = switch_core_session_strdup(nsession, gateway_ptr->register_contact);
	} else {
		if (!(dest = strchr(profile_name, '/'))) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Invalid URL\n");
			cause = SWITCH_CAUSE_INVALID_NUMBER_FORMAT;
			goto error;
		}
		*dest++ = '\0';

		if (!(profile = sofia_glue_find_profile(profile_name))) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Invalid Profile\n");
			cause = SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER;
			goto error;
		}

		if ((dest_to = strchr(dest, '^'))) {
			*dest_to++ = '\0';
			tech_pvt->dest_to = switch_core_session_alloc(nsession, strlen(dest_to) + 5);
			switch_snprintf(tech_pvt->dest_to, strlen(dest_to) + 5, "sip:%s", dest_to);
		}

		if ((host = strchr(dest, '%'))) {
			char buf[128];
			*host = '@';
			tech_pvt->e_dest = switch_core_session_strdup(nsession, dest);
			*host++ = '\0';
			if (sofia_reg_find_reg_url(profile, dest, host, buf, sizeof(buf))) {
				tech_pvt->dest = switch_core_session_strdup(nsession, buf);
				tech_pvt->local_url = switch_core_session_sprintf(nsession, "%s@%s", dest, host);
			} else {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Cannot locate registered user %s@%s\n", dest, host);
				cause = SWITCH_CAUSE_NO_ROUTE_DESTINATION;
				goto error;
			}
		} else if (!strchr(dest, '@')) {
			char buf[128];
			tech_pvt->e_dest = switch_core_session_strdup(nsession, dest);
			if (sofia_reg_find_reg_url(profile, dest, profile_name, buf, sizeof(buf))) {
				tech_pvt->dest = switch_core_session_strdup(nsession, buf);
				tech_pvt->local_url = switch_core_session_sprintf(nsession, "%s@%s", dest, profile_name);
			} else {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Cannot locate registered user %s@%s\n", dest, profile_name);
				cause = SWITCH_CAUSE_NO_ROUTE_DESTINATION;
				goto error;
			}
		} else {
			tech_pvt->dest = switch_core_session_alloc(nsession, strlen(dest) + 5);
			switch_snprintf(tech_pvt->dest, strlen(dest) + 5, "sip:%s", dest);
		}
	}

	if (!tech_pvt->dest_to) {
		tech_pvt->dest_to = tech_pvt->dest;
	}

	sofia_glue_attach_private(nsession, profile, tech_pvt, dest);

	if (tech_pvt->local_url) {
		switch_channel_set_variable(nchannel, "sip_local_url", tech_pvt->local_url);
		if (sofia_test_pflag(profile, PFLAG_PRESENCE)) {
			switch_channel_set_variable(nchannel, "presence_id", tech_pvt->local_url);
		}
	}
	switch_channel_set_variable(nchannel, "sip_destination_url", tech_pvt->dest);
	
	caller_profile = switch_caller_profile_clone(nsession, outbound_profile);
	caller_profile->destination_number = switch_core_strdup(caller_profile->pool, dest);
	switch_channel_set_caller_profile(nchannel, caller_profile);
	switch_channel_set_flag(nchannel, CF_OUTBOUND);
	switch_set_flag_locked(tech_pvt, TFLAG_OUTBOUND);
	switch_clear_flag_locked(tech_pvt, TFLAG_LATE_NEGOTIATION);
	switch_channel_set_state(nchannel, CS_INIT);
	tech_pvt->caller_profile = caller_profile;
	*new_session = nsession;
	cause = SWITCH_CAUSE_SUCCESS;

	if (session) {
		switch_ivr_transfer_variable(session, nsession, SOFIA_REPLACES_HEADER);
		switch_ivr_transfer_variable(session, nsession, "sip_auto_answer");
		switch_ivr_transfer_variable(session, nsession, SOFIA_SIP_HEADER_PREFIX_T);
		if (switch_core_session_compare(session, nsession)) {
			/* It's another sofia channel! so lets cache what they use as a pt for telephone event so 
			   we can keep it the same
			 */
			private_object_t *ctech_pvt;
			ctech_pvt = switch_core_session_get_private(session);
			switch_assert(ctech_pvt != NULL);
			tech_pvt->bte = ctech_pvt->te;
			tech_pvt->bcng_pt = ctech_pvt->cng_pt;
		}
	}

	goto done;

  error:
	if (nsession) {
		switch_core_session_destroy(&nsession);
	}
	*pool = NULL;

  done:
	if (profile) {
		sofia_glue_release_profile(profile);
	}
	return cause;
}

SWITCH_MODULE_LOAD_FUNCTION(mod_sofia_load)
{
	switch_chat_interface_t *chat_interface;
	switch_api_interface_t *api_interface;
	switch_management_interface_t *management_interface;

	silence_frame.data = silence_data;
	silence_frame.datalen = sizeof(silence_data);
	silence_frame.buflen = sizeof(silence_data);
	silence_frame.flags = SFF_CNG;

	module_pool = pool;

	memset(&mod_sofia_globals, 0, sizeof(mod_sofia_globals));
	switch_mutex_init(&mod_sofia_globals.mutex, SWITCH_MUTEX_NESTED, module_pool);

	switch_find_local_ip(mod_sofia_globals.guess_ip, sizeof(mod_sofia_globals.guess_ip), AF_INET);

	switch_core_hash_init(&mod_sofia_globals.profile_hash, module_pool);
	switch_core_hash_init(&mod_sofia_globals.gateway_hash, module_pool);
	switch_mutex_init(&mod_sofia_globals.hash_mutex, SWITCH_MUTEX_NESTED, module_pool);
	
	if (config_sofia(0, NULL) != SWITCH_STATUS_SUCCESS) {
		return SWITCH_STATUS_GENERR;
	}

	switch_mutex_lock(mod_sofia_globals.mutex);
	mod_sofia_globals.running = 1;
	switch_mutex_unlock(mod_sofia_globals.mutex);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Waiting for profiles to start\n");
	switch_yield(1500000);

	if (switch_event_bind(modname, SWITCH_EVENT_CUSTOM, MULTICAST_EVENT, event_handler, NULL) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't bind!\n");
		return SWITCH_STATUS_TERM;
	}

	if (switch_event_bind(modname, SWITCH_EVENT_PRESENCE_IN, SWITCH_EVENT_SUBCLASS_ANY, sofia_presence_event_handler, NULL)
		!= SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't bind!\n");
		return SWITCH_STATUS_GENERR;
	}

	if (switch_event_bind(modname, SWITCH_EVENT_PRESENCE_OUT, SWITCH_EVENT_SUBCLASS_ANY, sofia_presence_event_handler, NULL)
		!= SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't bind!\n");
		return SWITCH_STATUS_GENERR;
	}

	if (switch_event_bind(modname, SWITCH_EVENT_PRESENCE_PROBE, SWITCH_EVENT_SUBCLASS_ANY, sofia_presence_event_handler, NULL)
		!= SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't bind!\n");
		return SWITCH_STATUS_GENERR;
	}

	if (switch_event_bind(modname, SWITCH_EVENT_ROSTER, SWITCH_EVENT_SUBCLASS_ANY, sofia_presence_event_handler, NULL)
		!= SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't bind!\n");
		return SWITCH_STATUS_GENERR;
	}

	if (switch_event_bind(modname, SWITCH_EVENT_MESSAGE_WAITING, SWITCH_EVENT_SUBCLASS_ANY, sofia_presence_mwi_event_handler, NULL)
		!= SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't bind!\n");
		return SWITCH_STATUS_GENERR;
	}

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);
	sofia_endpoint_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_ENDPOINT_INTERFACE);
	sofia_endpoint_interface->interface_name = "sofia";
	sofia_endpoint_interface->io_routines = &sofia_io_routines;
	sofia_endpoint_interface->state_handler = &sofia_event_handlers;

	management_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_MANAGEMENT_INTERFACE);
	management_interface->relative_oid = "1";
	management_interface->management_function = sofia_manage;

	SWITCH_ADD_API(api_interface, "sofia", "Sofia Controls", sofia_function, "<cmd> <args>");
	SWITCH_ADD_API(api_interface, "sofia_contact", "Sofia Contacts", sofia_contact_function, "[profile/]<user>@<domain>");
	SWITCH_ADD_CHAT(chat_interface, SOFIA_CHAT_PROTO, sofia_presence_chat_send);

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_sofia_shutdown)
{
	int sanity = 0;
	
	switch_mutex_lock(mod_sofia_globals.mutex);
	if (mod_sofia_globals.running == 1) {
		mod_sofia_globals.running = 0;
	}
	switch_mutex_unlock(mod_sofia_globals.mutex);

	while (mod_sofia_globals.threads) {
		switch_yield(1000);
		if (++sanity >= 5000) {
			break;
		}
	}

	su_deinit();

	switch_core_hash_destroy(&mod_sofia_globals.profile_hash);
	switch_core_hash_destroy(&mod_sofia_globals.gateway_hash);

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
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 expandtab:
 */
