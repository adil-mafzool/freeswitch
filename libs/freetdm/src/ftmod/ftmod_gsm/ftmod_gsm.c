/*
 * Copyright (c) 2011, Sangoma Technologies 
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * * Neither the name of the original author; nor the names of any contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contributors: 
 *
 * Gideon Sadan <gsadan@sangoma.com>
 * Moises Silva <moy@sangoma.com>
 *
 */

#include <stdio.h>
#include <libwat.h>
#include <freetdm.h>
#include <private/ftdm_core.h>

/* span monitor thread */
static void *ftdm_gsm_run(ftdm_thread_t *me, void *obj);

/* IO interface for the command API */
static ftdm_io_interface_t g_ftdm_gsm_interface;

static FIO_CHANNEL_OUTGOING_CALL_FUNCTION(gsm_outgoing_call)
{
	ftdm_log_chan_msg(ftdmchan, FTDM_LOG_CRIT, "GSM place call not implemented yet!\n");
	return FTDM_FAIL;
}

static ftdm_status_t ftdm_gsm_start(ftdm_span_t *span)
{
	return ftdm_thread_create_detached(ftdm_gsm_run, span);
}

static ftdm_status_t ftdm_gsm_stop(ftdm_span_t *span)
{
	ftdm_log(FTDM_LOG_CRIT, "STOP not implemented yet!\n");
	return FTDM_FAIL;
}

static FIO_CHANNEL_GET_SIG_STATUS_FUNCTION(ftdm_gsm_get_channel_sig_status)
{
	ftdm_log_chan_msg(ftdmchan, FTDM_LOG_CRIT, "get sig status not implemented yet!\n");
	return FTDM_FAIL;
}

static FIO_CHANNEL_SET_SIG_STATUS_FUNCTION(ftdm_gsm_set_channel_sig_status)
{
	ftdm_log_chan_msg(ftdmchan, FTDM_LOG_CRIT, "set sig status not implemented yet!\n");
	return FTDM_FAIL;
}

static FIO_SPAN_GET_SIG_STATUS_FUNCTION(ftdm_gsm_get_span_sig_status)
{
	ftdm_log(FTDM_LOG_CRIT, "span get sig status not implemented yet!\n");
	return FTDM_FAIL;
}

static FIO_SPAN_SET_SIG_STATUS_FUNCTION(ftdm_gsm_set_span_sig_status)
{
	ftdm_log(FTDM_LOG_CRIT, "span set sig status not implemented yet!\n");
	return FTDM_FAIL;
}

static ftdm_state_map_t gsm_state_map = {
	{
		{
			ZSD_INBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_ANY_STATE, FTDM_END},
			{FTDM_CHANNEL_STATE_RESET, FTDM_END}
		},
		{
			ZSD_INBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_RESET, FTDM_END},
			{FTDM_CHANNEL_STATE_DOWN, FTDM_END}
		},
		{
			ZSD_INBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_DOWN, FTDM_END},
			{FTDM_CHANNEL_STATE_RING, FTDM_END}
		},
		{
			ZSD_INBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_RING, FTDM_END},
			{FTDM_CHANNEL_STATE_HANGUP, FTDM_CHANNEL_STATE_TERMINATING, FTDM_CHANNEL_STATE_PROGRESS_MEDIA, FTDM_CHANNEL_STATE_UP, FTDM_END}
		},
		{
			ZSD_INBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_HANGUP, FTDM_END},
			{FTDM_CHANNEL_STATE_DOWN, FTDM_END},
		},
		{
			ZSD_INBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_TERMINATING, FTDM_END},
			{FTDM_CHANNEL_STATE_HANGUP, FTDM_END},
		},
		{
			ZSD_INBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_PROGRESS_MEDIA, FTDM_END},
			{FTDM_CHANNEL_STATE_HANGUP, FTDM_CHANNEL_STATE_TERMINATING, FTDM_CHANNEL_STATE_UP, FTDM_END},
		},
		{
			ZSD_INBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_UP, FTDM_END},
			{FTDM_CHANNEL_STATE_HANGUP, FTDM_CHANNEL_STATE_TERMINATING, FTDM_END},
		},
		
		/* Outbound states */
		
		{
			ZSD_OUTBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_ANY_STATE, FTDM_END},
			{FTDM_CHANNEL_STATE_RESET, FTDM_END}
		},

		{
			ZSD_OUTBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_RESET, FTDM_END},
			{FTDM_CHANNEL_STATE_DOWN, FTDM_END}
		},

		{
			ZSD_OUTBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_DOWN, FTDM_END},
			{FTDM_CHANNEL_STATE_DIALING, FTDM_END}
		},

		{
			ZSD_OUTBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_DIALING, FTDM_END},
			{FTDM_CHANNEL_STATE_HANGUP, FTDM_CHANNEL_STATE_TERMINATING, FTDM_CHANNEL_STATE_PROGRESS_MEDIA, FTDM_END}
		},

		{
			ZSD_OUTBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_HANGUP, FTDM_END},
			{FTDM_CHANNEL_STATE_DOWN, FTDM_END}
		},

		{
			ZSD_OUTBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_TERMINATING, FTDM_END},
			{FTDM_CHANNEL_STATE_HANGUP, FTDM_END}
		},

		{
			ZSD_OUTBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_PROGRESS_MEDIA, FTDM_END},
			{FTDM_CHANNEL_STATE_HANGUP, FTDM_CHANNEL_STATE_TERMINATING, FTDM_CHANNEL_STATE_UP, FTDM_END}
		},

		{
			ZSD_OUTBOUND,
			ZSM_UNACCEPTABLE,
			{FTDM_CHANNEL_STATE_UP, FTDM_END},
			{FTDM_CHANNEL_STATE_HANGUP, FTDM_CHANNEL_STATE_TERMINATING, FTDM_END}
		},
	}
};

static ftdm_status_t ftdm_gsm_state_advance(ftdm_channel_t *ftdmchan)
{
	ftdm_log_chan(ftdmchan, FTDM_LOG_DEBUG, "Executing state handler for %s\n", ftdm_channel_state2str(ftdmchan->state));
	return FTDM_SUCCESS;
}

static FIO_CONFIGURE_SPAN_SIGNALING_FUNCTION(ftdm_gsm_configure_span_signaling)
{
	unsigned paramindex = 0;
	const char *var = NULL;
	const char *val = NULL;

	ftdm_assert_return(sig_cb != NULL, FTDM_FAIL, "No signaling cb provided\n");

	if (span->signal_type) {
		snprintf(span->last_error, sizeof(span->last_error), "Span is already configured for signalling.");
		return FTDM_FAIL;
	}

	for (; ftdm_parameters[paramindex].var; paramindex++) {
		var = ftdm_parameters[paramindex].var;
		val = ftdm_parameters[paramindex].val;
		ftdm_log(FTDM_LOG_DEBUG, "Reading GSM parameter %s for span %d\n", var, span->span_id);
		if (!strcasecmp(var, "moduletype")) {
			if (!val) {
				break;
			}
			if (ftdm_strlen_zero_buf(val)) {
				ftdm_log(FTDM_LOG_NOTICE, "Ignoring empty moduletype parameter\n");
				continue;
			}
			ftdm_log(FTDM_LOG_DEBUG, "Configuring GSM span %d for moduletype %s\n", span->span_id, val);
		} else {
			snprintf(span->last_error, sizeof(span->last_error), "Unknown GSM parameter [%s]", var);
			return FTDM_FAIL;
		}
	}

	/* Bind function pointers for control operations */
	span->start = ftdm_gsm_start;
	span->stop = ftdm_gsm_stop;
	span->sig_read = NULL;
	span->sig_write = NULL;

	span->signal_cb = sig_cb;
	span->signal_type = FTDM_SIGTYPE_GSM;
	span->signal_data = NULL; /* Gideon, plz fill me with gsm span specific data (you allocate and free) */
	span->outgoing_call = gsm_outgoing_call;
	span->get_span_sig_status = ftdm_gsm_get_span_sig_status;
	span->set_span_sig_status = ftdm_gsm_set_span_sig_status;
	span->get_channel_sig_status = ftdm_gsm_get_channel_sig_status;
	span->set_channel_sig_status = ftdm_gsm_set_channel_sig_status;

	span->state_map = &gsm_state_map;
	span->state_processor = ftdm_gsm_state_advance;

	/* use signals queue */
	ftdm_set_flag(span, FTDM_SPAN_USE_SIGNALS_QUEUE);

	/* we can skip states (going straight from RING to UP) */
	ftdm_set_flag(span, FTDM_SPAN_USE_SKIP_STATES);

#if 0
	/* setup the scheduler (create if needed) */
	snprintf(schedname, sizeof(schedname), "ftmod_r2_%s", span->name);
	ftdm_assert(ftdm_sched_create(&r2data->sched, schedname) == FTDM_SUCCESS, "Failed to create schedule!\n");
	spanpvt->sched = r2data->sched;
#endif

	return FTDM_SUCCESS;

}

static void *ftdm_gsm_run(ftdm_thread_t *me, void *obj)
{
	ftdm_channel_t *ftdmchan = NULL;
	ftdm_span_t *span = (ftdm_span_t *) obj;
	ftdm_iterator_t *chaniter = NULL;
	ftdm_iterator_t *citer = NULL;
	int waitms = 10;

	ftdm_log(FTDM_LOG_DEBUG, "GSM monitor thread for span %s started\n", span->name);

	chaniter = ftdm_span_get_chan_iterator(span, NULL);
	if (!chaniter) {
		ftdm_log(FTDM_LOG_CRIT, "Failed to allocate channel iterator for span %s!\n", span->name);
		goto done;
	}
	while (ftdm_running()) {

#if 0
		/* run any span timers */
		ftdm_sched_run(r2data->sched);

#endif
		/* deliver the actual channel events to the user now without any channel locking */
		ftdm_span_trigger_signals(span);
#if 0
		 /* figure out what event to poll each channel for. POLLPRI when the channel is down,
		  * POLLPRI|POLLIN|POLLOUT otherwise */
		memset(poll_events, 0, sizeof(short)*span->chan_count);
		citer = ftdm_span_get_chan_iterator(span, chaniter);
		if (!citer) {
			ftdm_log(FTDM_LOG_CRIT, "Failed to allocate channel iterator for span %s!\n", span->name);
			goto done;
		}
		for (i = 0; citer; citer = ftdm_iterator_next(citer), i++) {
			ftdmchan = ftdm_iterator_current(citer);
			r2chan = R2CALL(ftdmchan)->r2chan;
			poll_events[i] = FTDM_EVENTS;
			if (openr2_chan_get_read_enabled(r2chan)) {
				poll_events[i] |= FTDM_READ;
			}
		}
		status = ftdm_span_poll_event(span, waitms, poll_events);

		/* run any span timers */
		ftdm_sched_run(r2data->sched);
#endif
		ftdm_sleep(waitms);


		/* this main loop takes care of MF and CAS signaling during call setup and tear down
		 * for every single channel in the span, do not perform blocking operations here! */
		citer = ftdm_span_get_chan_iterator(span, chaniter);
		for ( ; citer; citer = ftdm_iterator_next(citer)) {
			ftdmchan = ftdm_iterator_current(citer);

			ftdm_channel_lock(ftdmchan);

			ftdm_channel_advance_states(ftdmchan);

			ftdm_channel_unlock(ftdmchan);
		}
	}

done:
	ftdm_iterator_free(chaniter);

	ftdm_log(FTDM_LOG_DEBUG, "GSM thread ending.\n");

	return NULL;
}

#define FT_SYNTAX "USAGE:\n" \
"--------------------------------------------------------------------------------\n" \
"ftdm gsm status <span_id|span_name>\n" \
"--------------------------------------------------------------------------------\n"
static FIO_API_FUNCTION(ftdm_gsm_api)
{
	char *mycmd = NULL, *argv[10] = { 0 };
	int argc = 0;

	if (data) {
		mycmd = ftdm_strdup(data);
		argc = ftdm_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}

	if (argc == 1) {
		if (!strcasecmp(argv[0], "version")) {
			stream->write_function(stream, "libwat GSM version: implement me!!\n");
			stream->write_function(stream, "+OK.\n");
			goto done;
		}
	}

	stream->write_function(stream, "%s", FT_SYNTAX);

done:

	ftdm_safe_free(mycmd);

	return FTDM_SUCCESS;

}

static FIO_IO_LOAD_FUNCTION(ftdm_gsm_io_init)
{
	assert(fio != NULL);
	memset(&g_ftdm_gsm_interface, 0, sizeof(g_ftdm_gsm_interface));

	g_ftdm_gsm_interface.name = "gsm";
	g_ftdm_gsm_interface.api = ftdm_gsm_api;

	*fio = &g_ftdm_gsm_interface;

	return FTDM_SUCCESS;
}

static FIO_SIG_LOAD_FUNCTION(ftdm_gsm_init)
{
	/* this is called on module load */
	return FTDM_SUCCESS;
}

static FIO_SIG_UNLOAD_FUNCTION(ftdm_gsm_destroy)
{
	/* this is called on module unload */
	return FTDM_SUCCESS;
}

EX_DECLARE_DATA ftdm_module_t ftdm_module = { 
	/* .name */ "gsm",
	/* .io_load */ ftdm_gsm_io_init,
	/* .io_unload */ NULL,
	/* .sig_load */ ftdm_gsm_init,
	/* .sig_configure */ NULL,
	/* .sig_unload */ ftdm_gsm_destroy,
	/* .configure_span_signaling */ ftdm_gsm_configure_span_signaling
};
	

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
