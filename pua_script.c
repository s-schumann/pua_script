/*
 *
 * pua_script module - publish presence states directly from the script
 *
 * Copyright (C) 2006 Voice Sistem S.R.L.
 * Copyright (C) 2008 Sebastian Schumann
 *
 * This file is part of openser, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * openser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 * --------
 *  2008-10-10  initial version (s_schumann) v0.1
 *  2008-11-12	modifications for production use v0.2
 */

/* header files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <time.h>

#include "../../sr_module.h"
#include "../../parser/parse_expires.h"
#include "../../dprint.h"
#include "../../mem/shm_mem.h"
#include "../../parser/msg_parser.h"
#include "../../str.h"
#include "../../mem/mem.h"
#include "../../pt.h"
#include "../tm/tm_load.h"
#include "../pua/pua_bind.h"

#include "../../mod_fix.h"
#include "../pua/pua.h"
#include "../../statistics.h" /* required for stats */

/* macro for check of correct version of running OpenSER */
MODULE_VERSION

/* defined variables */
#define EVENTS		3
#define TUPLE_ID_SIZE	8
#define RAND_LETTERS	26
#define RAND_NUMBERS	10

/* structure for using pua api */
pua_api_t pua; /* pua/pua_bind.h */
send_publish_t pua_send_publish; /* pua/send_publish.h */
send_subscribe_t pua_send_subscribe; /* pua/send_subscribe.h */

/* module functions */
static int mod_init(void);
static int child_init(int);
static void destroy(void);

/* fixup function */
static int fixup_w_script_publish(void** param, int param_no);
/* wrapper function */
static int w_script_publish(struct sip_msg*, char*, char*);
/* support functions */
static char random_letter(int);
static char random_number(void);
static void random_string(int, char*);
void print_publ(publ_info_t);

/* exported script commands, structure see sr_module.h */
static cmd_export_t cmds[]={
   {"script_publish" /*name */, (cmd_function) w_script_publish /*name*/, 0 /*params*/, 0                      /*fix_up*/, 0, REQUEST_ROUTE | BRANCH_ROUTE | FAILURE_ROUTE | ONREPLY_ROUTE },
   {"script_publish" /*name */, (cmd_function) w_script_publish /*name*/, 1 /*params*/, fixup_w_script_publish /*fix_up*/, 0, REQUEST_ROUTE | BRANCH_ROUTE | FAILURE_ROUTE | ONREPLY_ROUTE },
   {"script_publish" /*name */, (cmd_function) w_script_publish /*name*/, 2 /*params*/, fixup_w_script_publish /*fix_up*/, 0, REQUEST_ROUTE | BRANCH_ROUTE | FAILURE_ROUTE | ONREPLY_ROUTE },
   {0,0,0,0,0,0}
};

/* script parameter, structure see sr_module.h */
static int send_msg = 0; /*not on all platforms by default 0, so we set it here implicitly */
static char *default_expire = NULL;
static char *default_event = NULL;
static char *default_contenttype = NULL;
static char *default_etag = NULL;
static char *outbound_proxy = NULL;

static param_export_t params[]={
	{"send_msg", INT_PARAM, &send_msg},
	{"default_expire", STR_PARAM, &default_expire},
	{"default_event", STR_PARAM, &default_event},
	{"default_contenttype", STR_PARAM, &default_contenttype},
	{"default_etag", STR_PARAM, &default_etag},
	{"outbound_proxy", STR_PARAM, &outbound_proxy},
	{0,0,0}
};

/* export statistics, structure see sr_module.h */
stat_var *publ_cnt;

static stat_export_t mod_stats[] = {
	{"publ_cnt", 0, &publ_cnt},
	{0,0,0}
};

/** module exports */
struct module_exports exports= {
	"pua_script",		/* module name */
	DEFAULT_DLFLAGS,	/* dlopen flags */
	cmds,			/* exported functions */
	params,			/* exported parameters */
	mod_stats,			/* exported statistics */
	0,			/* exported MI functions */
	0,			/* exported pseudo-variables */
	0,			/* extra processes */
	mod_init,		/* module initialization function */
	(response_function) 0,	/* response handling function */
 	destroy,		/* destroy function */
	child_init		/* per-child init function */
};
	
/* init module function */
static int mod_init(void) {
	LM_DBG("...\n");
	bind_pua_t bind_pua;
	
	bind_pua= (bind_pua_t)find_export("bind_pua", 1,0);
	if (!bind_pua) {
		LM_ERR("Can't bind pua\n");
		return -1;
	}
	
	if (bind_pua(&pua) < 0) {
		LM_ERR("Can't bind pua\n");
		return -1;
	}
	if(pua.send_publish == NULL) {
		LM_ERR("Could not import send_publish\n");
		return -1;
	}
	pua_send_publish= pua.send_publish;

	if(pua.send_subscribe == NULL) {
		LM_ERR("Could not import send_subscribe\n");
		return -1;
	}
	pua_send_subscribe= pua.send_subscribe;
	
	return 0;
}

/* init child function */
static int child_init(int rank) {
	LM_DBG("child [%d]  pid [%d]\n", rank, getpid());
	return 0;
}	

/* destroy function */
static void destroy(void) {	
	LM_DBG("destroying module ...\n");
	return ;
}

/* main wrapper function */
static int w_script_publish(struct sip_msg *req, char *param_uri, char *param_event) {

	LM_DBG("Starting publisher\n");

	/* Get presentity URI from function parameter */
	LM_DBG("Passed function parameter URI: %s (string)\n", param_uri);
	str pres_uri = {0, 0};
	if(fixup_get_svalue(req, (gparam_p)param_uri, &pres_uri)!=0 || pres_uri.len<=0) {
		LM_ERR("invalid uri parameter\n");
		return -1;
	}
/*	if (extract_aor(&ex_uri, &aor) < 0) {
 *		LM_ERR("failed to extract Address Of Record\n");
 *		return -1;
 *	}
 */	LM_DBG("Function parameter URI: %s (string)\n", pres_uri.s);

	/* Get event number from function parameter */
        LM_DBG("Passed function parameter Event: %s (string)\n", param_event);
	int evt;
	evt = atoi(param_event);
	if(evt == 0) {
		LM_ERR("Wrong event parameter\n");
		return E_CFG;
	} else if(evt > EVENTS) {
		LM_ERR("Unknown event number: %d\n",evt);
		return -1;
	}
        LM_DBG("Function parameter Event: %d (int)\n", evt);

	/* Variable declarations */
	int exp;
	str expires;
	str body= {0, 0};
	str body_start, body_middle, body_end;
	struct sip_uri uri;
	publ_info_t publ;
	str event;
	str content_type;
	str etag;
	str proxy;
	int result;
	int sign= 1;

	char *s;
	char *t;
	char *p;
	int n;

	/* check expires script parameter */
	if(default_expire == NULL) {
		LM_ERR("Missing expires parameter\n");
		return E_CFG;
	}
	expires.s = default_expire;
	expires.len = strlen(default_expire);

	/* check event script parameter */
	if(default_event == NULL) {
		LM_ERR("Missing default_event parameter\n");
		return E_CFG;
	}
	event.s = default_event;
	event.len = strlen(default_event);

	/* check content-tyoe script parameter */
	if(default_contenttype == NULL) {
		LM_ERR("Missing default_contenttype parameter\n");
		return E_CFG;
	}
	content_type.s = default_contenttype;
	content_type.len = strlen(default_contenttype);

	/* check etag script parameter */
	if(default_etag == NULL) {
		LM_ERR("Missing default_etag parameter\n");
		return E_CFG;
	}
	etag.s = default_etag;
	etag.len = strlen(default_etag);
	
	/* check outbound proxy script parameter */
	if(outbound_proxy == NULL) {
		LM_ERR("Missing outbound_proxy parameter\n");
		return E_CFG;
	}
	proxy.s = outbound_proxy;
	proxy.len = strlen(outbound_proxy);
	
	/* create tuple_id */
	char tuple[TUPLE_ID_SIZE + 1];
	srand((unsigned)time(NULL));
	tuple[TUPLE_ID_SIZE] = '\0';
	random_string(TUPLE_ID_SIZE, tuple);
	LM_DBG("tuple id (%d characters) created: [%s]\n", TUPLE_ID_SIZE, tuple);

	/* Select XML body elements */
	if(evt == 1) {
		/* open, unknown */
		body_start.s = "<?xml version='1.0' encoding='UTF-8'?><presence xmlns='urn:ietf:params:xml:ns:pidf' xmlns:dm='urn:ietf:params:xml:ns:pidf:data-model' xmlns:r='urn:ietf:params:xml:ns:pidf:rpid' xmlns:c='urn:ietf:params:xml:ns:pidf:cipid' entity='";
		body_middle.s = "'><tuple id='";
		body_end.s = "'><status><basic>open</basic></status></tuple><dm:person id='openseruser'><r:activities><r:unknown/></r:activities></dm:person></presence>";
	} else if(evt == 2) {
		/* closed */
		body_start.s = "<?xml version='1.0' encoding='UTF-8'?><presence xmlns='urn:ietf:params:xml:ns:pidf' entity='";
		body_middle.s = "'><tuple id='";
		body_end.s = "'><status><basic>closed</basic></status></tuple></presence>";
	} else if(evt == 3) {
		/* open, busy */
		body_start.s = "<?xml version='1.0' encoding='UTF-8'?><presence xmlns='urn:ietf:params:xml:ns:pidf' xmlns:dm='urn:ietf:params:xml:ns:pidf:data-model' xmlns:r='urn:ietf:params:xml:ns:pidf:rpid' xmlns:c='urn:ietf:params:xml:ns:pidf:cipid' entity='";
		body_middle.s = "'><tuple id='";
		body_end.s = "'><status><basic>open</basic></status></tuple><dm:person id='openseruser'><r:activities><r:on-the-phone/><r:busy/></r:activities></dm:person></presence>";
	} else {
		LM_ERR("Wrong event passed, PIDF for user X closed\n");
		body_start.s = "<?xml version='1.0' encoding='UTF-8'?><presence xmlns='urn:ietf:params:xml:ns:pidf' entity='";
		body_middle.s = "'><tuple id='";
		body_end.s = "'><status><basic>closed</basic></status></tuple></presence>";
	}

	body_start.len = strlen(body_start.s);
	body_middle.len = strlen(body_middle.s);
	body_end.len = strlen(body_end.s);

	/* Allocate memory for body and create it */
	n = body_start.len + pres_uri.len + body_middle.len + TUPLE_ID_SIZE + body_end.len;
	s = (char *)pkg_malloc(n); /* as pointers are used must not be freed after last use of any reference */
	LM_DBG("Allocated pkg memory for body\n");

	if(s==NULL) {
		LM_ERR("no more pkg mem for body (%d)\n", n);
		return -1;
	}

	p=s;
	memcpy(p, body_start.s, body_start.len);
	p += body_start.len;
	memcpy(p, pres_uri.s, pres_uri.len);
	p += pres_uri.len;
	memcpy(p, body_middle.s, body_middle.len);
	p += body_middle.len;

	/* create tuple id */
	int i;
	for(i=0;i<TUPLE_ID_SIZE;i++) {
		*(p++) = tuple[i];
	}

	memcpy(p, body_end.s, body_end.len);
	p += body_end.len;
	body.s = s;
	body.len = (int)(p-s);
	
	if(body.len > n) {
		LM_ERR("Buffer size overflow for body\n");
		pkg_free(s);
		return -1;
	}

	/* process presence URI */
	if(pres_uri.s == NULL || pres_uri.s== 0) {
		LM_ERR("empty presentity uri\n");
	}
	if(parse_uri(pres_uri.s, pres_uri.len, &uri)<0 ) {
		LM_ERR("bad presentity uri\n");
	}
	LM_DBG("pres_uri '%.*s'\n", pres_uri.len, pres_uri.s);

	/* process expires */
	if(expires.s== NULL || expires.len== 0) {
		LM_ERR("empty expires parameter\n");
	}
	if(expires.s[0]== '-') {
		sign= -1;
		expires.s++;
		expires.len--;
	}
	if( str2int(&expires, (unsigned int*) &exp)< 0) {
		LM_ERR("invalid expires parameter\n" );
		goto error;
	}
	exp= exp* sign;

	LM_DBG("expires '%d'\n", exp);

	/* process event */
	if(event.s== NULL || event.len== 0) {
		LM_ERR("empty event parameter\n");
	}
	LM_DBG("event '%.*s'\n",
		event.len, event.s);

	/* process content type */
	if(content_type.s== NULL || content_type.len== 0) {
		LM_ERR("empty content type\n");
	}
	LM_DBG("content type '%.*s'\n",
		content_type.len, content_type.s);

	/* process etag */
	if(etag.s== NULL || etag.len== 0) {
		LM_ERR("empty etag parameter\n");
	}
	LM_DBG("etag '%.*s'\n",
		etag.len, etag.s);
		
	/* process proxy */
	if(proxy.s== NULL || proxy.len== 0) {
		LM_ERR("empty proxy parameter\n");
	}
	LM_DBG("proxy '%.*s'\n",
		proxy.len, proxy.s);


	/* process body */
	if(body.s == NULL || body.s== 0) {
		LM_ERR("empty body parameter\n");
	}
	
	LM_DBG("body '%.*s'\n",
		body.len, body.s);
	
	/* Create the publ_info_t structure */
	memset(&publ, 0, sizeof(publ_info_t)); /* assure to have the whole publ set to 0 */
	
	publ.pres_uri= &pres_uri;
	if(body.s) {
		publ.body= &body;
	}
	
	publ.event= get_event_flag(&event);
	if(publ.event< 0) {
		LM_ERR("unkown event\n");
	}
	if(content_type.len!= 1) {
		publ.content_type= content_type;
	}	
	
	if(! (etag.len== 1 && etag.s[0]== '.')) {
		publ.etag= &etag;
	} 
	publ.expires= exp;

	publ.outbound_proxy = proxy;

	/* Create ID for update requests */
	n = 15 + pres_uri.len;
	t = (char *)pkg_malloc(n);
	LM_DBG("Allocated pkg memory for id\n");
	if(t==NULL) {
		LM_ERR("no more pkg mem for id (%d)\n", n);
		return -1;
	}
	p=t;
	memcpy(p, "SCRIPT_PUBLISH.", 15);
	p += 15;
	memcpy(p, pres_uri.s, pres_uri.len);
	p += pres_uri.len;

	publ.id.s = t;
	publ.id.len = (int)(p-t);

	if(publ.id.len > n) {
		LM_ERR("Buffer size overflow for id\n");
		pkg_free(s);
		return -1;
	}

	/* Set flags */
	publ.flag|= UPDATE_TYPE;
	publ.source_flag|= SCRIPT_PUBLISH; /* must be in pua/hash.h */

	print_publ(publ); /* prints only in DBG mode */
	
	if(send_msg == 1) { /* script parameter */
		result = pua_send_publish(&publ);
		update_stat(publ_cnt, +1);
		LM_INFO("Sending PUBLISH called via script (%.*s)\n", publ.pres_uri->len, publ.pres_uri->s);
	} else {
		result = -1;
		LM_INFO("NO PUBLISH SENT, activate send_msg module parameter!\n");
	}

	if(result< 0) {
		LM_ERR("Sending publish failed\n");
	}	
	if(result== 418) {
		LM_ERR("Wrong Etag\n");	
	}
   	
	/* free memory, as PUSLISH has been sent */
	pkg_free(s);
	LM_DBG("Freed pkg memory for body\n");
	pkg_free(t);
	LM_DBG("Freed pkg memory for id\n");

	return 1; /* success */
error:
	pkg_free(s);
	LM_DBG("Freed pkg memory for body\n");
	
	return 0; /* error */
}

/* fixup function */
static int fixup_w_script_publish(void** param, int param_no) {
	if (param_no == 1) {
		LM_DBG("Calling fixup param_no 1\n");
		return fixup_spve_null(param, 1);
	} else if (param_no == 2) {
		LM_DBG("param_no 2 does not need to be fixed\n");
	}
	return 0;
}

/* support functions */
static char random_letter(int is_cap) {
	int letter = (int)(RAND_LETTERS * (rand() / (RAND_MAX + 1.0)));
	return((char)((is_cap == 1) ? (letter + 65) : (letter + 97)));
}

static char random_number(void) {
	int number = (int)(RAND_NUMBERS * (rand() / (RAND_MAX + 1.0)));
	return((char)(number + 48));
}

static void random_string(int length, char *str) {
	int i;
	int char_type;

	for(i = 0; i < length; i++) {
		char_type = (int)(3 * (rand() / (RAND_MAX + 1.0)));

		switch(char_type) {
			case 0:
				str[i] = random_letter(0);
				break;
			case 1:
				str[i] = random_letter(1);
				break;
			case 2:
				str[i] = random_number();
				break;
			default:
				str[i] = random_number();
				break;
		}
	}  
}

void print_publ(publ_info_t p) {
	LM_DBG("publ:\n");
	LM_DBG("uri= %.*s\n", p.pres_uri->len, p.pres_uri->s);
	LM_DBG("id= %.*s\n", p.id.len, p.id.s);
	LM_DBG("expires= %d\n", p.expires);
}
