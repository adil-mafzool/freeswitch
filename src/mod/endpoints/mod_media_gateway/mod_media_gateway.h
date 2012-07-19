/*
* Copyright (c) 2012, Sangoma Technologies
* Mathieu Rene <mrene@avgs.ca>
* All rights reserved.
* 
* <Insert license here>
*/


#ifndef MOD_MEGACO_H
#define MOD_MEGACO_H

#include "sng_mg/sng_mg.h"
#include <switch.h>

#define MG_MAX_PEERS    5

#define MG_CONTEXT_MAX_TERMS 3

#define MEGACO_CLI_SYNTAX "profile|logging"
#define MEGACO_LOGGING_CLI_SYNTAX "logging [enable|disable]"
#define MEGACO_FUNCTION_SYNTAX "profile [name] [start | stop] [status] [xmlstatus] [peerxmlstatus]"

struct megaco_globals {
	switch_memory_pool_t 		*pool;
	switch_hash_t 			*profile_hash;
	switch_hash_t 			*peer_profile_hash;
	switch_thread_rwlock_t 		*profile_rwlock;
	switch_thread_rwlock_t 		*peer_profile_rwlock;
};
extern struct megaco_globals megaco_globals; /* < defined in mod_megaco.c */

typedef enum {
	PF_RUNNING = (1 << 0)
} megaco_profile_flags_t;

typedef enum {
	MEGACO_CODEC_PCMA,
	MEGACO_CODEC_PCMU,
	MEGACO_CODEC_G729,
	MEGACO_CODEC_G723_1,
	MEGACO_CODEC_ILBC,
} megaco_codec_t;

typedef struct mg_peer_profile_s{
	char 				*name;
	switch_memory_pool_t 		*pool;
	switch_thread_rwlock_t 		*rwlock; /* < Reference counting rwlock */
	megaco_profile_flags_t 		flags;
	char*  				ipaddr;      /* Peer IP  */
  	char* 				port;        /*Peer Port */
	char*       			mid;  	     /* Peer H.248 MID */
	char*       			transport_type; /* UDP/TCP */ 
	char*      			encoding_type; /* Encoding TEXT/Binary */
} mg_peer_profile_t;


typedef enum {
    MG_TERM_FREE = 0,
    MG_TERM_TDM,
    MG_TERM_RTP
} mg_termination_type_t;

typedef struct megaco_profile_s megaco_profile_t;
typedef struct mg_context_s mg_context_t;

/* RTP parameters understood by the controllable channel */
#define kLOCALADDR "local_addr"
#define kLOCALPORT "local_port"
#define kREMOTEADDR "remote_addr"
#define kREMOTEPORT "remote_port"
#define kCODEC "codec"
#define kPTIME "ptime"
#define kPT "pt"
#define kRFC2833PT "rfc2833_pt"
#define kMODE "mode"
#define kRATE "rate"

/* TDM parameters understood by the controllable channel */
#define kSPAN_ID "span"
#define kCHAN_ID "chan"

typedef struct mg_termination_s {
    mg_termination_type_t type;
    const char *name; /*!< Megaco Name */    
    const char *uuid; /*!< UUID of the associated FS channel, or NULL if it's not activated */
    mg_context_t *context; /*!< Context in which this termination is connected, or NULL */
    megaco_profile_t *profile; /*!< Parent MG profile */
    
    union {
        struct {
            /* The RTP termination will automatically operate as "sendonly" or "recvonly" as soon as
             * one of the network addresses are NULL */
            const char *local_addr; /*!< RTP Session's Local IP address  */
            switch_port_t local_port; /*!< RTP Session's Local IP address  */
            
            const char *remote_addr; /*!< RTP Session's Remote IP address  */
            switch_port_t remote_port; /*!< RTP Session's Remote IP address  */

            int ptime;  /*!< Packetization Interval, in miliseconds. The default is 20, but it has to be set */
            int pt;     /*!< Payload type */
            int rfc2833_pt; /*!< If the stream is using rfc2833 for dtmf events, this has to be set to its negotiated payload type */
            int rate;       /*!< Sampling rate */
            const char *codec; /*!< Codec to use, using the freeswitch nomenclature. This could be "PCMU" for G711.U, "PCMA" for G711.A, or "G729" for g729 */
        } rtp;
        
        struct {
            int span;
            int channel;
        } tdm;
    } u;
} mg_termination_t;


struct mg_context_s {
    uint32_t context_id;
    mg_termination_t *terminations[MG_CONTEXT_MAX_TERMS];
    megaco_profile_t *profile;
    mg_context_t *next;
    switch_memory_pool_t *pool;
};

#define MG_CONTEXT_MODULO 16
#define MG_MAX_CONTEXTS 32768


struct megaco_profile_s {
	char 				*name;
	switch_memory_pool_t 	*pool;
	switch_thread_rwlock_t 	*rwlock; /* < Reference counting rwlock */
	megaco_profile_flags_t 	flags;
	int 					idx;         /* Trillium MEGACO SAP identification*/
	char*					mid;  	     /* MG H.248 MID */
	char*					my_domain;   /* local domain name */
	char*					my_ipaddr;   /* local domain name */
	char*					port;              	     /* port */
	char*					protocol_type;    	     /* MEGACO/MGCP */
	int 					protocol_version;            /* Protocol supported version */
	int 					total_peers;
	megaco_codec_t			default_codec;
	char*					rtp_port_range;
	char*					rtp_termination_id_prefix;
	int						rtp_termination_id_len;
	char*                	peer_list[MG_MAX_PEERS];     /* MGC Peer ID LIST */
    
    switch_thread_rwlock_t  *contexts_rwlock;
    uint32_t next_context_id;
    uint8_t contexts_bitmap[MG_MAX_CONTEXTS/8]; /* Availability matrix, enough bits for a 32768 bitmap */    
    mg_context_t *contexts[MG_CONTEXT_MODULO];
    
    switch_hash_t *terminations;
    switch_thread_rwlock_t *terminations_rwlock;
};


megaco_profile_t *megaco_profile_locate(const char *name);
mg_peer_profile_t *megaco_peer_profile_locate(const char *name);
void megaco_profile_release(megaco_profile_t *profile);

switch_status_t megaco_profile_start(const char *profilename);
switch_status_t megaco_profile_destroy(megaco_profile_t **profile);

mg_context_t *megaco_get_context(megaco_profile_t *profile, uint32_t context_id);
mg_context_t *megaco_choose_context(megaco_profile_t *profile);
void megaco_release_context(mg_context_t *ctx);

megaco_profile_t*  megaco_get_profile_by_suId(SuId suId);
mg_context_t *megaco_find_context_by_suid(SuId suId, uint32_t context_id);

switch_status_t config_profile(megaco_profile_t *profile, switch_bool_t reload);
switch_status_t sng_mgco_start(megaco_profile_t* profile);
switch_status_t sng_mgco_stop(megaco_profile_t* profile);
switch_status_t mg_config_cleanup(megaco_profile_t* profile);
switch_status_t mg_peer_config_cleanup(mg_peer_profile_t* profile);
switch_status_t megaco_peer_profile_destroy(mg_peer_profile_t **profile); 
switch_status_t mg_process_cli_cmd(const char *cmd, switch_stream_handle_t *stream);


#endif /* MOD_MEGACO_H */


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
