/*
 * dnssec.c
 * Some DNSSEC helper function are defined here
 * and tracing is done
 * (c) 2005 NLnet Labs
 *
 * See the file LICENSE for the license
 *
 */

#include "drill.h"
#include <ldns/ldns.h>

/* get rr_type from a server from a server */
ldns_rr_list *
get_rr(ldns_resolver *res, ldns_rdf *zname, ldns_rr_type t, ldns_rr_class c)
{
	/* query, retrieve, extract and return */
	ldns_pkt *p;
	ldns_rr_list *found;

	p = ldns_pkt_new();
	found = NULL;

	if (ldns_resolver_send(&p, res, zname, t, c, 0) != LDNS_STATUS_OK) {
		/* oops */
		return NULL;
	} else {
		found = ldns_pkt_rr_list_by_type(p, t, LDNS_SECTION_ANY_NOQUESTION);
	}
	return found;
}

void
drill_pkt_print(FILE *fd, ldns_resolver *r, ldns_pkt *p)
{
	ldns_rr_list *new_nss;
	ldns_rr_list *hostnames;

	if (verbosity < 5) {
		return;
	}

	hostnames = ldns_get_rr_list_name_by_addr(r, ldns_pkt_answerfrom(p), 0, 0);

	new_nss = ldns_pkt_rr_list_by_type(p,
			LDNS_RR_TYPE_NS, LDNS_SECTION_ANSWER);
	ldns_rr_list_print(fd, new_nss);

	/* new_nss can be empty.... */

	fprintf(fd, ";; Received %d bytes from %s#%d(",
			(int) ldns_pkt_size(p),
			ldns_rdf2str(ldns_pkt_answerfrom(p)),
			(int) ldns_resolver_port(r));
	/* if we can resolve this print it, other print the ip again */
	if (hostnames) {
		ldns_rdf_print(fd,
				ldns_rr_rdf(ldns_rr_list_rr(hostnames, 0), 0));
		ldns_rr_list_deep_free(hostnames);
	} else {
		fprintf(fd, "%s", ldns_rdf2str(ldns_pkt_answerfrom(p)));
	}
	fprintf(fd, ") in %u ms\n\n", (unsigned int)ldns_pkt_querytime(p));
}

void
drill_pkt_print_footer(FILE *fd, ldns_resolver *r, ldns_pkt *p)
{
	ldns_rr_list *hostnames;

	if (verbosity < 5) {
		return;
	}

	hostnames = ldns_get_rr_list_name_by_addr(r, ldns_pkt_answerfrom(p), 0, 0);

	fprintf(fd, ";; Received %d bytes from %s#%d(",
			(int) ldns_pkt_size(p),
			ldns_rdf2str(ldns_pkt_answerfrom(p)),
			(int) ldns_resolver_port(r));
	/* if we can resolve this print it, other print the ip again */
	if (hostnames) {
		ldns_rdf_print(fd,
				ldns_rr_rdf(ldns_rr_list_rr(hostnames, 0), 0));
		ldns_rr_list_deep_free(hostnames);
	} else {
		fprintf(fd, "%s", ldns_rdf2str(ldns_pkt_answerfrom(p)));
	}
	fprintf(fd, ") in %u ms\n\n", (unsigned int)ldns_pkt_querytime(p));
}
/*
 * generic function to get some RRset from a nameserver
 * and possible some signatures too (that would be the day...)
 */
ldns_pkt_type
get_dnssec_rr(ldns_pkt *p, ldns_rdf *name, ldns_rr_type t, 
	ldns_rr_list **rrlist, ldns_rr_list **sig)
{
	ldns_pkt_type pt = LDNS_PACKET_UNKNOWN;
	ldns_rr_list *rr = NULL;
	ldns_rr_list *sigs = NULL;
	size_t i;

	if (!p) {
		if (rrlist) {
			*rrlist = NULL;
		}
		return LDNS_PACKET_UNKNOWN;
	}

	pt = ldns_pkt_reply_type(p);
	if (name) {
		rr = ldns_pkt_rr_list_by_name_and_type(p, name, t, LDNS_SECTION_ANSWER);
		if (!rr) {
			rr = ldns_pkt_rr_list_by_name_and_type(p, name, t, LDNS_SECTION_AUTHORITY);
		}
		sigs = ldns_pkt_rr_list_by_name_and_type(p, name, LDNS_RR_TYPE_RRSIG, 
				LDNS_SECTION_ANSWER);
		if (!sigs) {
		sigs = ldns_pkt_rr_list_by_name_and_type(p, name, LDNS_RR_TYPE_RRSIG, 
				LDNS_SECTION_AUTHORITY);
		}
	} else {
               /* A DS-referral - get the DS records if they are there */
               rr = ldns_pkt_rr_list_by_type(p, t, LDNS_SECTION_AUTHORITY);
               sigs = ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_RRSIG,
                               LDNS_SECTION_AUTHORITY);
	}
	if (sig) {
		*sig = ldns_rr_list_new();
		for (i = 0; i < ldns_rr_list_rr_count(sigs); i++) {
			/* only add the sigs that cover this type */
			if (ldns_rdf2rr_type(ldns_rr_rrsig_typecovered(ldns_rr_list_rr(sigs, i))) ==
			    t) {
			 	ldns_rr_list_push_rr(*sig, ldns_rr_clone(ldns_rr_list_rr(sigs, i)));   
			}
		}
	}
	ldns_rr_list_deep_free(sigs);
	if (rrlist) {
		*rrlist = rr;
	}

	if (pt == LDNS_PACKET_NXDOMAIN || pt == LDNS_PACKET_NODATA) {
		return pt;
	} else {
		return LDNS_PACKET_ANSWER;
	}
}


ldns_status
ldns_verify_denial(ldns_pkt *pkt, ldns_rdf *name, ldns_rr_type type, ldns_rr_list **nsec_rrs, ldns_rr_list **nsec_rr_sigs)
{
	uint16_t nsec_i;

	ldns_rr_list *nsecs;
	ldns_status result;
	
	if (verbosity >= 5) {
		printf("VERIFY DENIAL FROM:\n");
		ldns_pkt_print(stdout, pkt);
	}

	result = LDNS_STATUS_CRYPTO_NO_RRSIG;
	/* Try to see if there are NSECS in the packet */
	nsecs = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_NSEC, LDNS_SECTION_ANY_NOQUESTION);
	if (nsecs) {
		for (nsec_i = 0; nsec_i < ldns_rr_list_rr_count(nsecs); nsec_i++) {
			/* there are four options:
			 * - name equals ownername and is covered by the type bitmap
			 * - name equals ownername but is not covered by the type bitmap
			 * - name falls within nsec coverage but is not equal to the owner name
			 * - name falls outside of nsec coverage
			 */
			if (ldns_dname_compare(ldns_rr_owner(ldns_rr_list_rr(nsecs, nsec_i)), name) == 0) {
				/*
				printf("CHECKING NSEC:\n");
				ldns_rr_print(stdout, ldns_rr_list_rr(nsecs, nsec_i));
				printf("DAWASEM\n");
				*/
				if (ldns_nsec_bitmap_covers_type(
					   ldns_nsec_get_bitmap(ldns_rr_list_rr(nsecs,
													nsec_i)),
					   type)) {
					/* Error, according to the nsec this rrset is signed */
					result = LDNS_STATUS_CRYPTO_NO_RRSIG;
				} else {
					/* ok nsec denies existence */
					if (verbosity >= 3) {
						printf(";; Existence of data set with this type denied by NSEC\n");
					}
						/*printf(";; Verifiably insecure.\n");*/
						if (nsec_rrs && nsec_rr_sigs) {
							(void) get_dnssec_rr(pkt, ldns_rr_owner(ldns_rr_list_rr(nsecs, nsec_i)), LDNS_RR_TYPE_NSEC, nsec_rrs, nsec_rr_sigs);
						}
						ldns_rr_list_deep_free(nsecs);
						return LDNS_STATUS_OK;
				}
			} else if (ldns_nsec_covers_name(ldns_rr_list_rr(nsecs, nsec_i), name)) {
				if (verbosity >= 3) {
					printf(";; Existence of data set with this name denied by NSEC\n");
				}
				if (nsec_rrs && nsec_rr_sigs) {
					(void) get_dnssec_rr(pkt, ldns_rr_owner(ldns_rr_list_rr(nsecs, nsec_i)), LDNS_RR_TYPE_NSEC, nsec_rrs, nsec_rr_sigs);
				}
				ldns_rr_list_deep_free(nsecs);
				return LDNS_STATUS_OK;
			} else {
				/* nsec has nothing to do with this data */
			}
		}
		ldns_rr_list_deep_free(nsecs);
	} else if( (nsecs = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_NSEC3, LDNS_SECTION_ANY_NOQUESTION)) ) {
                ldns_rr_list* sigs = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_RRSIG, LDNS_SECTION_ANY_NOQUESTION);
                ldns_rr* q = ldns_rr_new();
                if(!sigs) return LDNS_STATUS_MEM_ERR;
                if(!q) return LDNS_STATUS_MEM_ERR;
                ldns_rr_set_question(q, 1);
                ldns_rr_set_ttl(q, 0);
                ldns_rr_set_owner(q, ldns_rdf_clone(name));
                if(!ldns_rr_owner(q)) return LDNS_STATUS_MEM_ERR;
                ldns_rr_set_type(q, type);
                
                result = ldns_dnssec_verify_denial_nsec3(q, nsecs, sigs, ldns_pkt_get_rcode(pkt), type, ldns_pkt_ancount(pkt) == 0);
                ldns_rr_free(q);
		ldns_rr_list_deep_free(nsecs);
		ldns_rr_list_deep_free(sigs);
        }
	return result;
}

/* NSEC3 draft -07 */
/*return hash name match*/
ldns_rr *
ldns_nsec3_exact_match(ldns_rdf *qname, ldns_rr_type qtype, ldns_rr_list *nsec3s) {
	uint8_t algorithm;
	uint32_t iterations;
	uint8_t salt_length;
	uint8_t *salt;
	
	ldns_rdf *sname, *hashed_sname;
	
	size_t nsec_i;
	ldns_rr *nsec;
	ldns_rr *result = NULL;
	
	ldns_status status;
	
	const ldns_rr_descriptor *descriptor;
	
	ldns_rdf *zone_name;
	
	if (verbosity >= 4) {
		printf(";; finding exact match for ");
		descriptor = ldns_rr_descript(qtype);
		if (descriptor && descriptor->_name) {
			printf("%s ", descriptor->_name);
		} else {
			printf("TYPE%d ", qtype);
		}
		ldns_rdf_print(stdout, qname);
		printf("\n");
	}
	
	if (!qname || !nsec3s || ldns_rr_list_rr_count(nsec3s) < 1) {
		if (verbosity >= 4) {
			printf("no qname, nsec3s or list empty\n");
		}
		return NULL;
	}

	nsec = ldns_rr_list_rr(nsec3s, 0);
	algorithm = ldns_nsec3_algorithm(nsec);
	salt_length = ldns_nsec3_salt_length(nsec);
	salt = ldns_nsec3_salt_data(nsec);
	iterations = ldns_nsec3_iterations(nsec);

	sname = ldns_rdf_clone(qname);

	if (verbosity >= 4) {
		printf(";; owner name hashes to: ");
	}
	hashed_sname = ldns_nsec3_hash_name(sname, algorithm, iterations, salt_length, salt);

	zone_name = ldns_dname_left_chop(ldns_rr_owner(nsec));
	status = ldns_dname_cat(hashed_sname, zone_name);
	
	if (verbosity >= 4) {
		ldns_rdf_print(stdout, hashed_sname);
		printf("\n");
	}

	for (nsec_i = 0; nsec_i < ldns_rr_list_rr_count(nsec3s); nsec_i++) {
		nsec = ldns_rr_list_rr(nsec3s, nsec_i);
		
		/* check values of iterations etc! */
		
		/* exact match? */
		if (ldns_dname_compare(ldns_rr_owner(nsec), hashed_sname) == 0) {
			result = nsec;
			goto done;
		}
		
	}

done:
	ldns_rdf_deep_free(zone_name);
	ldns_rdf_deep_free(sname);
	ldns_rdf_deep_free(hashed_sname);
	LDNS_FREE(salt);
	
	if (verbosity >= 4) {
		if (result) {
			printf(";; Found.\n");
		} else {
			printf(";; Not foud.\n");
		}
	}
	return result;
}

/*return the owner name of the closest encloser for name from the list of rrs */
/* this is NOT the hash, but the original name! */
ldns_rdf *
ldns_nsec3_closest_encloser(ldns_rdf *qname, ldns_rr_type qtype, ldns_rr_list *nsec3s)
{
	/* remember parameters, they must match */
	uint8_t algorithm;
	uint32_t iterations;
	uint8_t salt_length;
	uint8_t *salt;

	ldns_rdf *sname, *hashed_sname, *tmp;
	ldns_rr *ce;
	bool flag;
	
	bool exact_match_found;
	bool in_range_found;
	
	ldns_status status;
	ldns_rdf *zone_name;
	
	size_t nsec_i;
	ldns_rr *nsec;
	ldns_rdf *result = NULL;
	
	if (!qname || !nsec3s || ldns_rr_list_rr_count(nsec3s) < 1) {
		return NULL;
	}

	if (verbosity >= 4) {
		printf(";; finding closest encloser for type %d ", qtype);
		ldns_rdf_print(stdout, qname);
		printf("\n");
	}

	nsec = ldns_rr_list_rr(nsec3s, 0);
	algorithm = ldns_nsec3_algorithm(nsec);
	salt_length = ldns_nsec3_salt_length(nsec);
	salt = ldns_nsec3_salt_data(nsec);
	iterations = ldns_nsec3_iterations(nsec);

	sname = ldns_rdf_clone(qname);

	ce = NULL;
	flag = false;
	
	zone_name = ldns_dname_left_chop(ldns_rr_owner(nsec));

	/* algorithm from nsec3-07 8.3 */
	while (ldns_dname_label_count(sname) > 0) {
		exact_match_found = false;
		in_range_found = false;
		
		if (verbosity >= 3) {
			printf(";; ");
			ldns_rdf_print(stdout, sname);
			printf(" hashes to: ");
		}
		hashed_sname = ldns_nsec3_hash_name(sname, algorithm, iterations, salt_length, salt);

		status = ldns_dname_cat(hashed_sname, zone_name);

		if (verbosity >= 3) {
			ldns_rdf_print(stdout, hashed_sname);
			printf("\n");
		}

		for (nsec_i = 0; nsec_i < ldns_rr_list_rr_count(nsec3s); nsec_i++) {
			nsec = ldns_rr_list_rr(nsec3s, nsec_i);
			
			/* check values of iterations etc! */
			
			/* exact match? */
			if (ldns_dname_compare(ldns_rr_owner(nsec), hashed_sname) == 0) {
				if (verbosity >= 4) {
					printf(";; exact match found\n");
				}
			 	exact_match_found = true;
			} else if (ldns_nsec_covers_name(nsec, hashed_sname)) {
				if (verbosity >= 4) {
					printf(";; in range of an nsec\n");
				}
				in_range_found = true;
			}
			
		}
		if (!exact_match_found && in_range_found) {
			flag = true;
		} else if (exact_match_found && flag) {
			result = ldns_rdf_clone(sname);
		} else if (exact_match_found && !flag) {
			// error!
			if (verbosity >= 4) {
				printf(";; the closest encloser is the same name (ie. this is an exact match, ie there is no closest encloser)\n");
			}
			ldns_rdf_deep_free(hashed_sname);
			goto done;
		} else {
			flag = false;
		}
		
		ldns_rdf_deep_free(hashed_sname);
		tmp = sname;
		sname = ldns_dname_left_chop(sname);
		ldns_rdf_deep_free(tmp);
	}

	done:
	LDNS_FREE(salt);
	ldns_rdf_deep_free(zone_name);
	ldns_rdf_deep_free(sname);

	if (!result) {
		if (verbosity >= 4) {
			printf(";; no closest encloser found\n");
		}
	}
	
	/* todo checks from end of 6.2. here or in caller? */
	return result;
}


/* special case were there was a wildcard expansion match, the exact match must be disproven */
ldns_status
ldns_verify_denial_wildcard(ldns_pkt *pkt, ldns_rdf *name, ldns_rr_type type, ldns_rr_list **nsec_rrs, ldns_rr_list **nsec_rr_sigs)
{
	ldns_rdf *nsec3_ce = NULL;
	ldns_rr *nsec3_ex = NULL;
	ldns_rdf *wildcard_name = NULL;
	ldns_rdf *nsec3_wc_ce = NULL;
	ldns_rr *nsec3_wc_ex = NULL;
	ldns_rdf *chopped_dname = NULL;
	ldns_rr_list *nsecs;
	ldns_status result = LDNS_STATUS_ERR;

	nsecs = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_NSEC3, LDNS_SECTION_ANY_NOQUESTION);
	if (nsecs) {
		wildcard_name = ldns_dname_new_frm_str("*");
		chopped_dname = ldns_dname_left_chop(name);
		result = ldns_dname_cat(wildcard_name, chopped_dname);
		ldns_rdf_deep_free(chopped_dname);

		nsec3_ex = ldns_nsec3_exact_match(name, type, nsecs);
		nsec3_ce = ldns_nsec3_closest_encloser(name, type, nsecs);
		nsec3_wc_ce = ldns_nsec3_closest_encloser(wildcard_name, type, nsecs);				
		nsec3_wc_ex = ldns_nsec3_exact_match(wildcard_name, type, nsecs);
		
		if (nsec3_ex) {
			if (verbosity >= 3) {
				printf(";; Error, exact match for for name found, but should not exist (draft -07 section 8.8)\n");
			}
			result = LDNS_STATUS_NSEC3_ERR;
		} else if (!nsec3_ce) {
			if (verbosity >= 3) {
				printf(";; Error, closest encloser for exact match missing in wildcard response (draft -07 section 8.8)\n");
			}
			result = LDNS_STATUS_NSEC3_ERR;
/*
		} else if (!nsec3_wc_ex) {
			printf(";; Error, no wildcard nsec3 match: ");
			ldns_rdf_print(stdout, wildcard_name);
			printf(" (draft -07 section 8.8)\n");
			result = LDNS_STATUS_NSEC3_ERR;
*/
/*		} else if (!nsec */
		} else {
			if (verbosity >= 3) {
				printf(";; wilcard expansion proven\n");
			}
			result = LDNS_STATUS_OK;
		}
	} else {
		if (verbosity >= 3) {
			printf(";; Error: no NSEC or NSEC3 records in answer\n");
		}
		result = LDNS_STATUS_CRYPTO_NO_RRSIG;
	}
	
	if (nsecs && nsec_rrs && nsec_rr_sigs) {
		(void) get_dnssec_rr(pkt, ldns_rr_owner(ldns_rr_list_rr(nsecs, 0)), LDNS_RR_TYPE_NSEC3, nsec_rrs, nsec_rr_sigs);
	}
	return result;
}


