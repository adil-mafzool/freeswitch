/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2009, Anthony Minessale II <anthm@freeswitch.org>
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
 *
 * switch_cpp.cpp -- C++ wrapper
 *
 */

#include <switch.h>
#include <switch_cpp.h>

#ifdef _MSC_VER
#pragma warning(disable:4127 4003)
#endif

static void event_handler(switch_event_t *event)
{
	EventConsumer *E = (EventConsumer *) event->bind_user_data;
	switch_event_t *dup;
	
	switch_event_dup(&dup, event);

	if (switch_queue_trypush(E->events, dup) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Cannot queue any more events.....\n");
	}

}

SWITCH_DECLARE_CONSTRUCTOR EventConsumer::EventConsumer(const char *event_name, const char *subclass_name)
{
	switch_name_event(event_name, &e_event_id);
	switch_core_new_memory_pool(&pool);
	
	if (!switch_strlen_zero(subclass_name)) {
		e_subclass_name = switch_core_strdup(pool, subclass_name);
	} else {
		e_subclass_name = NULL;
	}

	switch_queue_create(&events, 5000, pool);
	
	if (switch_event_bind_removable(__FILE__, e_event_id, e_subclass_name, event_handler, this, &node) == SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "bound to %s %s\n", event_name, switch_str_nil(e_subclass_name));
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Cannot bind to %s %s\n", event_name, switch_str_nil(e_subclass_name));
	}

}


SWITCH_DECLARE(Event *) EventConsumer::pop(int block)
{
	void *pop = NULL;
	Event *ret = NULL;
	switch_event_t *event;
	
	if (block) {
		switch_queue_pop(events, &pop);
	} else {
		switch_queue_trypop(events, &pop);
	}

	if ((event = (switch_event_t *) pop)) {
		ret = new Event(event);
	}

	return ret;
}

SWITCH_DECLARE_CONSTRUCTOR EventConsumer::~EventConsumer()
{
	if (node) {
		switch_event_unbind(&node);
	}

	switch_core_destroy_memory_pool(&pool);
}

SWITCH_DECLARE_CONSTRUCTOR IVRMenu::IVRMenu(IVRMenu *main,
											const char *name,
											const char *greeting_sound,
											const char *short_greeting_sound,
											const char *invalid_sound,
											const char *exit_sound,
											const char *confirm_macro,
											const char *confirm_key,
											const char *tts_engine,
											const char *tts_voice,
											int confirm_attempts,
											int inter_timeout,
											int digit_len,
											int timeout,
											int max_failures,
											int max_timeouts)
{
	menu = NULL;
	switch_core_new_memory_pool(&pool);
	switch_assert(pool);
	if (switch_strlen_zero(name)) {
		name = "no name";
	}

	switch_ivr_menu_init(&menu, main ? main->menu : NULL, name, greeting_sound, short_greeting_sound, invalid_sound, 
						 exit_sound, confirm_macro, confirm_key, tts_engine, tts_voice, confirm_attempts, inter_timeout,
						 digit_len, timeout, max_failures, max_timeouts, pool);
	

}
											
SWITCH_DECLARE_CONSTRUCTOR IVRMenu::~IVRMenu()
{
	if (menu) {
		switch_ivr_menu_stack_free(menu);
	}
	switch_core_destroy_memory_pool(&pool);
}

SWITCH_DECLARE(void) IVRMenu::bindAction(char *action, const char *arg, const char *bind)
{
	switch_ivr_action_t ivr_action = SWITCH_IVR_ACTION_NOOP;

	this_check_void();
	
	if (switch_ivr_menu_str2action(action, &ivr_action) == SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "bind %s to %s(%s)\n", bind, action, arg);
		switch_ivr_menu_bind_action(menu, ivr_action, arg, bind);
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "invalid action %s\n", action);
	}
}

SWITCH_DECLARE(void) IVRMenu::execute(CoreSession *session, const char *name)
{
	this_check_void();
	switch_ivr_menu_execute(session->session, menu, (char *)name, NULL);
}

SWITCH_DECLARE_CONSTRUCTOR API::API()
{
	last_data = NULL;
}

SWITCH_DECLARE_CONSTRUCTOR API::~API()
{
	switch_safe_free(last_data);
}


SWITCH_DECLARE(const char *) API::execute(const char *cmd, const char *arg)
{
	switch_stream_handle_t stream = { 0 };
	this_check("");
	SWITCH_STANDARD_STREAM(stream);
	switch_api_execute(cmd, arg, NULL, &stream);
	last_data = (char *) stream.data;
	return last_data;
}


/* we have to do this as a string because swig and languages can't find an embedded way to pass a big int */
SWITCH_DECLARE(char *) API::getTime(void)
{
	switch_time_t now = switch_micro_time_now() / 1000;
	snprintf(time_buf, sizeof(time_buf), "%" SWITCH_TIME_T_FMT, now);
	return time_buf;
}



SWITCH_DECLARE(const char *) API::executeString(const char *cmd)
{
	char *arg;
	switch_stream_handle_t stream = { 0 };
	char *mycmd = strdup(cmd);

	switch_assert(mycmd);

	this_check("");

	if ((arg = strchr(mycmd, ' '))) {
		*arg++ = '\0';
	}

	switch_safe_free(last_data);

	SWITCH_STANDARD_STREAM(stream);
	switch_api_execute(mycmd, arg, NULL, &stream);
	last_data = (char *) stream.data;
	switch_safe_free(mycmd);
	return last_data;
}

SWITCH_DECLARE_CONSTRUCTOR Event::Event(const char *type, const char *subclass_name)
{
	switch_event_types_t event_id;
	
	if (switch_name_event(type, &event_id) != SWITCH_STATUS_SUCCESS) {
		event_id = SWITCH_EVENT_MESSAGE;
	}

	if (!switch_strlen_zero(subclass_name) && event_id != SWITCH_EVENT_CUSTOM) {
		switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_WARNING, "Changing event type to custom because you specified a subclass name!\n");
		event_id = SWITCH_EVENT_CUSTOM;
	}

	if (switch_event_create_subclass(&event, event_id, subclass_name) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Failed to create event!\n");
		event = NULL;
	}

	serialized_string = NULL;
	mine = 1;
}

SWITCH_DECLARE_CONSTRUCTOR Event::Event(switch_event_t *wrap_me, int free_me)
{
	event = wrap_me;
	mine = free_me;
	serialized_string = NULL;
}

SWITCH_DECLARE_CONSTRUCTOR Event::~Event()
{

	if (serialized_string) {
		free(serialized_string);
	}

	if (event && mine) {
		switch_event_destroy(&event);
	}
}


SWITCH_DECLARE(const char *)Event::serialize(const char *format)
{
	int isxml = 0;

	this_check("");


	switch_safe_free(serialized_string);
	
	if (!event) {
		return "";
	}

	if (format && !strcasecmp(format, "xml")) {
		isxml++;
	}

	if (isxml) {
		switch_xml_t xml;
		if ((xml = switch_event_xmlize(event, SWITCH_VA_NONE))) {
			serialized_string = switch_xml_toxml(xml, SWITCH_FALSE);
			switch_xml_free(xml);
			return serialized_string;
		} else {
			return "";
		}
	} else {
		if (switch_event_serialize(event, &serialized_string, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
			char *new_serialized_string = switch_mprintf("'%s'", serialized_string);
			free(serialized_string);
			serialized_string = new_serialized_string;
			return serialized_string;
		}
	}
	
	return "";

}

SWITCH_DECLARE(bool) Event::fire(void)
{

	this_check(false);

	if (!mine) {
		switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Not My event!\n");
		return false;
	}

	if (event) {
		switch_event_t *new_event;
		if (switch_event_dup(&new_event, event) == SWITCH_STATUS_SUCCESS) {
			if (switch_event_fire(&new_event) != SWITCH_STATUS_SUCCESS) {
				switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Failed to fire the event!\n");
				switch_event_destroy(&new_event);
				return false;
			}
			return true;
		} else {
			switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Failed to dup the event!\n");
		}
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Trying to fire an event that does not exist!\n");
	}
	return false;
}

SWITCH_DECLARE(bool) Event::setPriority(switch_priority_t priority)
{
	this_check(false);

	if (event) {
        switch_event_set_priority(event, priority);
		return true;
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Trying to setPriority an event that does not exist!\n");
    }
	return false;
}

SWITCH_DECLARE(const char *)Event::getHeader(char *header_name)
{
	this_check("");

	if (event) {
		return switch_event_get_header(event, header_name);
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Trying to getHeader an event that does not exist!\n");
	}
	return NULL;
}

SWITCH_DECLARE(bool) Event::addHeader(const char *header_name, const char *value)
{
	this_check(false);

	if (event) {
		return switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, header_name, value) == SWITCH_STATUS_SUCCESS ? true : false;
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Trying to addHeader an event that does not exist!\n");
	}

	return false;
}

SWITCH_DECLARE(bool) Event::delHeader(const char *header_name)
{
	this_check(false);

	if (event) {
		return switch_event_del_header(event, header_name) == SWITCH_STATUS_SUCCESS ? true : false;
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Trying to delHeader an event that does not exist!\n");
	}

	return false;
}


SWITCH_DECLARE(bool) Event::addBody(const char *value)
{
	this_check(false);

	if (event) {
		return switch_event_add_body(event, "%s", value) == SWITCH_STATUS_SUCCESS ? true : false;
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Trying to addBody an event that does not exist!\n");
	}
	
	return false;
}

SWITCH_DECLARE(char *)Event::getBody(void)
{
	
	this_check((char *)"");

	if (event) {
		return switch_event_get_body(event);
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Trying to getBody an event that does not exist!\n");
	}
	
	return NULL;
}

SWITCH_DECLARE(const char *)Event::getType(void)
{
	this_check("");

	if (event) {
		return switch_event_name(event->event_id);
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG,SWITCH_LOG_ERROR, "Trying to getType an event that does not exist!\n");
	}
	
	return (char *) "invalid";
}


SWITCH_DECLARE_CONSTRUCTOR DTMF::DTMF(char idigit, uint32_t iduration)
{
	digit = idigit;

	if (iduration == 0) {
		iduration = SWITCH_DEFAULT_DTMF_DURATION;
	}

	duration = iduration;
}

SWITCH_DECLARE_CONSTRUCTOR DTMF::~DTMF()
{
	
}


SWITCH_DECLARE_CONSTRUCTOR Stream::Stream()
{
	SWITCH_STANDARD_STREAM(mystream);
	stream_p = &mystream;
	mine = 1;
}

SWITCH_DECLARE_CONSTRUCTOR Stream::Stream(switch_stream_handle_t *sp)
{
	stream_p = sp;
	mine = 0;
}


SWITCH_DECLARE_CONSTRUCTOR Stream::~Stream()
{
	if (mine) {
		switch_safe_free(mystream.data);
	}
}

SWITCH_DECLARE(void) Stream::write(const char *data)
{
	this_check_void();
	stream_p->write_function(stream_p, "%s", data);
}

SWITCH_DECLARE(const char *)Stream::get_data()
{
	this_check("");

	return stream_p ? (const char *)stream_p->data : NULL;
}


SWITCH_DECLARE_CONSTRUCTOR CoreSession::CoreSession()
{
	init_vars();
}

SWITCH_DECLARE_CONSTRUCTOR CoreSession::CoreSession(char *nuuid, CoreSession *a_leg)
{
	init_vars();

	if (!strchr(nuuid, '/') && (session = switch_core_session_locate(nuuid))) {
		uuid = strdup(nuuid);
		channel = switch_core_session_get_channel(session);
		allocated = 1;
    } else {
		switch_call_cause_t cause;
		if (switch_ivr_originate(a_leg ? a_leg->session : NULL, &session, &cause, nuuid, 60, NULL, NULL, NULL, NULL, NULL, SOF_NONE) 
			== SWITCH_STATUS_SUCCESS) {
			channel = switch_core_session_get_channel(session);
			allocated = 1;
			switch_set_flag(this, S_HUP);
			uuid = strdup(switch_core_session_get_uuid(session));
			switch_channel_set_state(switch_core_session_get_channel(session), CS_SOFT_EXECUTE);
		}
	}
}

SWITCH_DECLARE_CONSTRUCTOR CoreSession::CoreSession(switch_core_session_t *new_session)
{
	init_vars();

	if (new_session) {
		session = new_session;
		channel = switch_core_session_get_channel(session);
		allocated = 1;
		switch_core_session_read_lock(session);
		uuid = strdup(switch_core_session_get_uuid(session));
	}
}

SWITCH_DECLARE_CONSTRUCTOR CoreSession::~CoreSession()
{
	this_check_void();
	destroy();
}

SWITCH_DECLARE(char *) CoreSession::getXMLCDR()
{
	
	switch_xml_t cdr;

	this_check((char *)"");
	sanity_check((char *)"");

	switch_safe_free(xml_cdr_text);

	if (switch_ivr_generate_xml_cdr(session, &cdr) == SWITCH_STATUS_SUCCESS) {
		xml_cdr_text = switch_xml_toxml(cdr, SWITCH_FALSE);
		switch_xml_free(cdr);
	}

	return (char *) (xml_cdr_text ? xml_cdr_text : "");
}

SWITCH_DECLARE(void) CoreSession::setEventData(Event *e)
{
	this_check_void();
	sanity_check_noreturn;
	
	if (channel && e->event) {
		switch_channel_event_set_data(channel, e->event);
	}
}

SWITCH_DECLARE(int) CoreSession::answer()
{
    switch_status_t status;
	this_check(-1);
	sanity_check(-1);
    status = switch_channel_answer(channel);
    return status == SWITCH_STATUS_SUCCESS ? 1 : 0;
}

SWITCH_DECLARE(int) CoreSession::preAnswer()
{
    switch_status_t status;
	this_check(-1);
	sanity_check(-1);
    status = switch_channel_pre_answer(channel);
    return status == SWITCH_STATUS_SUCCESS ? 1 : 0;
}

SWITCH_DECLARE(void) CoreSession::hangup(const char *cause)
{
	this_check_void();
	sanity_check_noreturn;	
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "CoreSession::hangup\n");
	this->begin_allow_threads();
    switch_channel_hangup(channel, switch_channel_str2cause(cause));
	this->end_allow_threads();
}

SWITCH_DECLARE(void) CoreSession::setPrivate(char *var, void *val)
{
	this_check_void();
	sanity_check_noreturn;
    switch_channel_set_private(channel, var, val);
}

SWITCH_DECLARE(void *)CoreSession::getPrivate(char *var)
{
	this_check(NULL);
	sanity_check(NULL);
    return switch_channel_get_private(channel, var);
}

SWITCH_DECLARE(void) CoreSession::setVariable(char *var, char *val)
{
	this_check_void();
	sanity_check_noreturn;
    switch_channel_set_variable(channel, var, val);
}

SWITCH_DECLARE(const char *)CoreSession::getVariable(char *var)
{
	this_check("");
	sanity_check("");
    return switch_channel_get_variable(channel, var);
}

SWITCH_DECLARE(void) CoreSession::execute(const char *app, const char *data)
{
	this_check_void();
	sanity_check_noreturn;

	begin_allow_threads();
	switch_core_session_execute_application(session, app, data);
	end_allow_threads();
}

SWITCH_DECLARE(void) CoreSession::setDTMFCallback(void *cbfunc, char *funcargs) {

	this_check_void();
	sanity_check_noreturn;

	cb_state.funcargs = funcargs;
	cb_state.function = cbfunc;

	args.buf = &cb_state; 
	args.buflen = sizeof(cb_state);  // not sure what this is used for, copy mod_spidermonkey

    switch_channel_set_private(channel, "CoreSession", this);
        
	// we cannot set the actual callback to a python function, because
	// the callback is a function pointer with a specific signature.
	// so, set it to the following c function which will act as a proxy,
	// finding the python callback in the args callback args structure
	args.input_callback = dtmf_callback;  
	ap = &args;


}

SWITCH_DECLARE(void) CoreSession::sendEvent(Event *sendME)
{
	this_check_void();
	sanity_check_noreturn;

	if (sendME->event) {
		switch_event_t *new_event;
		if (switch_event_dup(&new_event, sendME->event) == SWITCH_STATUS_SUCCESS) {
			switch_core_session_receive_event(session, &new_event);
		}
	}
}

SWITCH_DECLARE(int) CoreSession::speak(char *text)
{
    switch_status_t status;

	this_check(-1);
	sanity_check(-1);

	if (!tts_name) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "No TTS engine specified\n");
		return SWITCH_STATUS_FALSE;
	}

	if (!voice_name) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "No TTS voice specified\n");
		return SWITCH_STATUS_FALSE;
	}


	begin_allow_threads();
	status = switch_ivr_speak_text(session, tts_name, voice_name, text, ap);
	end_allow_threads();
    return status == SWITCH_STATUS_SUCCESS ? 1 : 0;
}

SWITCH_DECLARE(void) CoreSession::set_tts_parms(char *tts_name_p, char *voice_name_p)
{
	this_check_void();
	sanity_check_noreturn;
	switch_safe_free(tts_name);
	switch_safe_free(voice_name);
    tts_name = strdup(tts_name_p);
    voice_name = strdup(voice_name_p);
}



SWITCH_DECLARE(int) CoreSession::collectDigits(int timeout) {
	this_check(-1);
	sanity_check(-1);
    begin_allow_threads();
	switch_ivr_collect_digits_callback(session, ap, timeout);
    end_allow_threads();
    return SWITCH_STATUS_SUCCESS;
} 

SWITCH_DECLARE(char *) CoreSession::getDigits(int maxdigits, char *terminators, int timeout)
{
    return getDigits(maxdigits, terminators, timeout, 0);
}

SWITCH_DECLARE(char *) CoreSession::getDigits(int maxdigits, 
											  char *terminators, 
											  int timeout,
											  int interdigit)
{
    switch_status_t status;
	this_check((char *)"");
	sanity_check((char *)"");
	begin_allow_threads();
	char terminator;

	memset(dtmf_buf, 0, sizeof(dtmf_buf));
    status = switch_ivr_collect_digits_count(session, 
											 dtmf_buf,
											 sizeof(dtmf_buf),
											 maxdigits, 
											 terminators, 
											 &terminator, 
											 (uint32_t) timeout, (uint32_t)interdigit, 0);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "getDigits dtmf_buf: %s\n", dtmf_buf);
	end_allow_threads();
    return dtmf_buf;
}

SWITCH_DECLARE(int) CoreSession::transfer(char *extension, char *dialplan, char *context)
{
    switch_status_t status;
	this_check(-1);
	sanity_check(-1);
    begin_allow_threads();
    status = switch_ivr_session_transfer(session, extension, dialplan, context);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "transfer result: %d\n", status);
    end_allow_threads();
    return status == SWITCH_STATUS_SUCCESS ? 1 : 0;
}


SWITCH_DECLARE(char *) CoreSession::read(int min_digits,
										 int max_digits,
										 const char *prompt_audio_file,
										 int timeout,
										 const char *valid_terminators)
{
	this_check((char *)"");
	sanity_check((char *)"");
	if (min_digits < 1) {
		min_digits = 1;
	}

	if (max_digits < 1) {
		max_digits = 1;
	}

	if (timeout < 1) {
		timeout = 1;
	}

	switch_ivr_read(session, min_digits, max_digits, prompt_audio_file, NULL, dtmf_buf, sizeof(dtmf_buf), timeout, valid_terminators);
	return dtmf_buf;
}

SWITCH_DECLARE(char *) CoreSession::playAndGetDigits(int min_digits, 
													 int max_digits, 
													 int max_tries, 
													 int timeout, 
													 char *terminators, 
													 char *audio_files, 
													 char *bad_input_audio_files,
													 char *digits_regex,
													 const char *var_name)
{
    switch_status_t status;
	sanity_check((char *)"");
	this_check((char *)"");
	begin_allow_threads();
	memset(dtmf_buf, 0, sizeof(dtmf_buf));
    status = switch_play_and_get_digits( session, 
										 (uint32_t) min_digits,
										 (uint32_t) max_digits,
										 (uint32_t) max_tries, 
										 (uint32_t) timeout, 
										 terminators, 
										 audio_files, 
										 bad_input_audio_files,
										 var_name,
										 dtmf_buf, 
										 sizeof(dtmf_buf), 
										 digits_regex);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "playAndGetDigits dtmf_buf: %s\n", dtmf_buf);

	end_allow_threads();
	return dtmf_buf;
}

SWITCH_DECLARE(void) CoreSession::say(const char *tosay, const char *module_name, const char *say_type, const char *say_method) 
{
	this_check_void();
	sanity_check_noreturn;
	if (!(tosay && module_name && say_type && say_method)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error! invalid args.\n");
		return;
	}
	begin_allow_threads();
	switch_ivr_say(session, tosay, module_name, say_type, say_method, ap);
    end_allow_threads();
}

SWITCH_DECLARE(void) CoreSession::sayPhrase(const char *phrase_name, const char *phrase_data, const char *phrase_lang) 
{
	this_check_void();
	sanity_check_noreturn;
	
	if (!(phrase_name)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error! invalid args.\n");
		return;
	}

	begin_allow_threads();
	switch_ivr_phrase_macro(session, phrase_name, phrase_data, phrase_lang, ap);
    end_allow_threads();
}

SWITCH_DECLARE(int) CoreSession::streamFile(char *file, int starting_sample_count) {

    switch_status_t status;
    //switch_file_handle_t fh = { 0 };
	const char *prebuf;

	this_check(-1);
    sanity_check(-1);
	
	memset(&local_fh, 0, sizeof(local_fh));
	fhp = &local_fh;
    local_fh.samples = starting_sample_count;


	if ((prebuf = switch_channel_get_variable(this->channel, "stream_prebuffer"))) {
        int maybe = atoi(prebuf);
        if (maybe > 0) {
            local_fh.prebuf = maybe;
        }
	}

    begin_allow_threads();
    status = switch_ivr_play_file(session, fhp, file, ap);
    end_allow_threads();

	fhp = NULL;
	
    return status == SWITCH_STATUS_SUCCESS ? 1 : 0;

}

SWITCH_DECLARE(int) CoreSession::sleep(int ms, int sync) {

    switch_status_t status;

	this_check(-1);
    sanity_check(-1);
	
    begin_allow_threads();
    status = switch_ivr_sleep(session, ms, (switch_bool_t) sync, ap);
    end_allow_threads();

    return status == SWITCH_STATUS_SUCCESS ? 1 : 0;

}

SWITCH_DECLARE(bool) CoreSession::ready() {

	this_check(false);
	sanity_check(false);	
	return switch_channel_ready(channel) != 0;
}

SWITCH_DECLARE(bool) CoreSession::mediaReady() {

	this_check(false);
	sanity_check(false);	
	return switch_channel_media_ready(channel) != 0;
}

SWITCH_DECLARE(bool) CoreSession::answered() {

	this_check(false);
	sanity_check(false);	
	return switch_channel_test_flag(channel, CF_ANSWERED) != 0;
}

SWITCH_DECLARE(void) CoreSession::destroy(void)
{
	this_check_void();

	switch_safe_free(xml_cdr_text);
	switch_safe_free(uuid);	
	switch_safe_free(tts_name);
	switch_safe_free(voice_name);

	if (session) {
		if (!channel) {
			channel = switch_core_session_get_channel(session);
		}

		if (channel) {
			switch_channel_set_private(channel, "CoreSession", NULL);
		}
		
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "destroy/unlink session from object\n");

        if (switch_channel_up(channel) && switch_test_flag(this, S_HUP) && !switch_channel_test_flag(channel, CF_TRANSFER)) {
            switch_channel_hangup(channel, SWITCH_CAUSE_NORMAL_CLEARING);
        }
        switch_core_session_rwunlock(session);
		session = NULL;
		channel = NULL;
    }

	allocated = 0;
	
}

SWITCH_DECLARE(int) CoreSession::originate(CoreSession *a_leg_session, char *dest, int timeout)
{

	switch_core_session_t *aleg_core_session = NULL;
	switch_call_cause_t cause;

	this_check(0);

	cause = SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER;

	if (a_leg_session != NULL) {
		aleg_core_session = a_leg_session->session;
	}

	// this session has no valid switch_core_session_t at this point, and therefore
	// no valid channel.  since the threadstate is stored in the channel, and there 
	// is none, if we try to call begin_alllow_threads it will fail miserably.
	// use the 'a leg session' to do the thread swapping stuff.
    if (a_leg_session) a_leg_session->begin_allow_threads();

	if (switch_ivr_originate(aleg_core_session, 
							 &session, 
							 &cause, 
							 dest, 
							 timeout,
							 NULL, 
							 NULL, 
							 NULL, 
							 &caller_profile,
							 NULL,
							 SOF_NONE) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Error Creating Outgoing Channel! [%s]\n", dest);
		goto failed;

	}

    if (a_leg_session) a_leg_session->end_allow_threads();
	channel = switch_core_session_get_channel(session);
	allocated = 1;
	switch_channel_set_state(switch_core_session_get_channel(session), CS_SOFT_EXECUTE);

	return SWITCH_STATUS_SUCCESS;

 failed:
    if (a_leg_session) a_leg_session->end_allow_threads();
	return SWITCH_STATUS_FALSE;
}

SWITCH_DECLARE(int) CoreSession::recordFile(char *file_name, int time_limit, int silence_threshold, int silence_hits) 
{
	switch_status_t status;

	this_check(-1);
	sanity_check(-1);

	memset(&local_fh, 0, sizeof(local_fh));
	fhp = &local_fh;
	local_fh.thresh = silence_threshold;
	local_fh.silence_hits = silence_hits;

	begin_allow_threads();
	status = switch_ivr_record_file(session, &local_fh, file_name, &args, time_limit);
	end_allow_threads();

	fhp = NULL;

    return status == SWITCH_STATUS_SUCCESS ? 1 : 0;

}

SWITCH_DECLARE(int) CoreSession::flushEvents() 
{
	switch_event_t *event;
	switch_channel_t *channel;

	this_check(-1);
	sanity_check(-1);

	if (!session) {
		return SWITCH_STATUS_FALSE;
	}
	channel = switch_core_session_get_channel(session);

	while (switch_core_session_dequeue_event(session, &event, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
		switch_event_destroy(&event);
	}
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_DECLARE(int) CoreSession::flushDigits() 
{
	this_check(-1);
	sanity_check(-1);
	switch_channel_flush_dtmf(switch_core_session_get_channel(session));
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_DECLARE(int) CoreSession::setAutoHangup(bool val) 
{
	this_check(-1);
	sanity_check(-1);

	if (!session) {
		return SWITCH_STATUS_FALSE;
	}	
	if (val) {
		switch_set_flag(this, S_HUP);
	} else {
		switch_clear_flag(this, S_HUP);
	}
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_DECLARE(void) CoreSession::waitForAnswer(CoreSession *calling_session) 
{
	this_check_void();
	sanity_check_noreturn;
	
	switch_ivr_wait_for_answer(calling_session ? calling_session->session : NULL, session);

}

SWITCH_DECLARE(void) CoreSession::setCallerData(char *var, char *val) {

	this_check_void();
	sanity_check_noreturn;

	if (strcmp(var, "dialplan") == 0) {
		caller_profile.dialplan = val;
	}
	if (strcmp(var, "context") == 0) {
		caller_profile.context = val;
	}
	if (strcmp(var, "caller_id_name") == 0) {
		caller_profile.caller_id_name = val;
	}
	if (strcmp(var, "caller_id_number") == 0) {
		caller_profile.caller_id_number = val;
	}
	if (strcmp(var, "network_addr") == 0) {
		caller_profile.network_addr = val;
	}
	if (strcmp(var, "ani") == 0) {
		caller_profile.ani = val;
	}
	if (strcmp(var, "aniii") == 0) {
		caller_profile.aniii = val;
	}
	if (strcmp(var, "rdnis") == 0) {
		caller_profile.rdnis = val;
	}
	if (strcmp(var, "username") == 0) {
		caller_profile.username = val;
	}

}

SWITCH_DECLARE(void) CoreSession::setHangupHook(void *hangup_func) {

	this_check_void();
	sanity_check_noreturn;
	
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "CoreSession::seHangupHook, hangup_func: %p\n", hangup_func);
    on_hangup = hangup_func;
    switch_channel_t *channel = switch_core_session_get_channel(session);

    hook_state = switch_channel_get_state(channel);
    switch_channel_set_private(channel, "CoreSession", this);
    switch_core_event_hook_add_state_change(session, hanguphook);
}

/* ---- methods not bound to CoreSession instance ---- */

SWITCH_DECLARE(void) consoleLog(char *level_str, char *msg)
{
	return console_log(level_str, msg);
}

SWITCH_DECLARE(void) consoleCleanLog(char *msg)
{
	return console_clean_log(msg);
}

SWITCH_DECLARE(void) console_log(char *level_str, char *msg)
{
    switch_log_level_t level = SWITCH_LOG_DEBUG;
    if (level_str) {
        level = switch_log_str2level(level_str);
		if (level == SWITCH_LOG_INVALID) {
			level = SWITCH_LOG_DEBUG;
		}
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, level, "%s", switch_str_nil(msg));
}

SWITCH_DECLARE(void) console_clean_log(char *msg)
{
    switch_log_printf(SWITCH_CHANNEL_LOG_CLEAN,SWITCH_LOG_DEBUG, "%s", switch_str_nil(msg));
}


SWITCH_DECLARE(void) msleep(unsigned ms)
{
	switch_sleep(ms * 1000);
	return;
}

SWITCH_DECLARE(void) bridge(CoreSession &session_a, CoreSession &session_b)
{
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "bridge called, session_a uuid: %s\n", session_a.get_uuid());
	switch_input_callback_function_t dtmf_func = NULL;
	switch_input_args_t args;
	switch_channel_t *channel_a = NULL, *channel_b = NULL;
	const char *err = "Channels not ready\n";
	
	if (session_a.allocated && session_a.session && session_b.allocated && session_b.session) {
		channel_a = switch_core_session_get_channel(session_a.session);
		channel_b = switch_core_session_get_channel(session_b.session);

		if (switch_channel_ready(channel_a) && switch_channel_ready(channel_b)) {
			session_a.begin_allow_threads();
			if (!switch_channel_test_flag(channel_a, CF_OUTBOUND) && !switch_channel_media_ready(channel_a)) {
				switch_channel_pre_answer(channel_a);
			}

			if (switch_channel_ready(channel_a) && switch_channel_ready(channel_b)) {
				args = session_a.get_cb_args();  // get the cb_args data structure for session a
				dtmf_func = args.input_callback;   // get the call back function
				err = NULL;
				switch_ivr_multi_threaded_bridge(session_a.session, session_b.session, dtmf_func, args.buf, args.buf);
			}
			session_a.end_allow_threads();
		}
	}

	if (err) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "%s", err);
	}


}

SWITCH_DECLARE_NONSTD(switch_status_t) hanguphook(switch_core_session_t *session_hungup) 
{
	switch_channel_t *channel = switch_core_session_get_channel(session_hungup);
	CoreSession *coresession = NULL;
	switch_channel_state_t state = switch_channel_get_state(channel);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "hangup_hook called\n");
	

	if ((coresession = (CoreSession *) switch_channel_get_private(channel, "CoreSession"))) {
		if (coresession->hook_state != state) {
			coresession->hook_state = state;
			coresession->check_hangup_hook();
		}
	}

	return SWITCH_STATUS_SUCCESS;
}


SWITCH_DECLARE_NONSTD(switch_status_t) dtmf_callback(switch_core_session_t *session_cb, 
													 void *input, 
													 switch_input_type_t itype, 
													 void *buf,  
													 unsigned int buflen) {
	
	switch_channel_t *channel = switch_core_session_get_channel(session_cb);
	CoreSession *coresession = NULL;
	switch_status_t result;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "dtmf_callback called\n");


	//coresession = (CoreSession *) buf;
	coresession = (CoreSession *) switch_channel_get_private(channel, "CoreSession");

	if (!coresession) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Invalid CoreSession\n");		
		return SWITCH_STATUS_FALSE;
	}

	result = coresession->run_dtmf_callback(input, itype);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "process_callback_result returned\n");
	if (result) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "process_callback_result returned: %d\n", result);
	}
	return result;

}


SWITCH_DECLARE(switch_status_t) CoreSession::process_callback_result(char *result)
{
	
	this_check(SWITCH_STATUS_FALSE);
	sanity_check(SWITCH_STATUS_FALSE);
	
    if (switch_strlen_zero(result)) {
		return SWITCH_STATUS_SUCCESS;	
    }

	if (fhp) {
		if (!switch_test_flag(fhp, SWITCH_FILE_OPEN)) {
			return SWITCH_STATUS_FALSE;
		}

		if (!strncasecmp(result, "speed", 5)) {
			char *p;
		
			if ((p = strchr(result, ':'))) {
				p++;
				if (*p == '+' || *p == '-') {
					int step;
					if (!(step = atoi(p))) {
						step = 1;
					}
					fhp->speed += step;
				} else {
					int speed = atoi(p);
					fhp->speed = speed;
				}
				return SWITCH_STATUS_SUCCESS;
			}

			return SWITCH_STATUS_FALSE;

		} else if (!strncasecmp(result, "volume", 6)) {
			char *p;
			
			if ((p = strchr(result, ':'))) {
				p++;
				if (*p == '+' || *p == '-') {
					int step;
					if (!(step = atoi(p))) {
						step = 1;
					}
					fhp->vol += step;
				} else {
					int vol = atoi(p);
					fhp->vol = vol;
				}
				return SWITCH_STATUS_SUCCESS;
			}
			
			if (fhp->vol) {
				switch_normalize_volume(fhp->vol);
			}
			
			return SWITCH_STATUS_FALSE;
		} else if (!strcasecmp(result, "pause")) {
			if (switch_test_flag(fhp, SWITCH_FILE_PAUSE)) {
				switch_clear_flag(fhp, SWITCH_FILE_PAUSE);
			} else {
				switch_set_flag(fhp, SWITCH_FILE_PAUSE);
			}
			return SWITCH_STATUS_SUCCESS;
		} else if (!strcasecmp(result, "stop")) {
			return SWITCH_STATUS_FALSE;
		} else if (!strcasecmp(result, "restart")) {
			unsigned int pos = 0;
			fhp->speed = 0;
			switch_core_file_seek(fhp, &pos, 0, SEEK_SET);
			return SWITCH_STATUS_SUCCESS;
		} else if (!strncasecmp(result, "seek", 4)) {
			switch_codec_t *codec;
			unsigned int samps = 0;
			unsigned int pos = 0;
			char *p;
			codec = switch_core_session_get_read_codec(session);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "got codec\n");
			if ((p = strchr(result, ':'))) {
				p++;
				if (*p == '+' || *p == '-') {
					int step;
					if (!(step = atoi(p))) {
						step = 1000;
					}
					if (step > 0) {
						samps = step * (codec->implementation->samples_per_second / 1000);
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "going to seek\n");
						switch_core_file_seek(fhp, &pos, samps, SEEK_CUR);
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "done seek\n");
					} else {
						samps = step * (codec->implementation->samples_per_second / 1000);
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "going to seek\n");
						switch_core_file_seek(fhp, &pos, fhp->pos - samps, SEEK_SET);
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "done seek\n");
					}
				} else {
					samps = atoi(p) * (codec->implementation->samples_per_second / 1000);
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "going to seek\n");
					switch_core_file_seek(fhp, &pos, samps, SEEK_SET);
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "done seek\n");
				}
			}

			return SWITCH_STATUS_SUCCESS;
		}
	}

    if (!strcmp(result, "true") || !strcmp(result, "undefined")) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "return success\n");
		return SWITCH_STATUS_SUCCESS;
    }

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "no callback result match for '%s', return false\n", result);

    return SWITCH_STATUS_FALSE;
}

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4:
 */
