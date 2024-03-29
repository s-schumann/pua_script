/*
 * $Id: mi_func.h 4472 2008-07-11 19:51:40Z bogdan_iancu $
 *
 * pua_mi module - MI pua module
 *
 * Copyright (C) 2006 Voice Sistem S.R.L.
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _PUA_SCRIPT
#define _PUA_SCRIPT

struct mi_root* mi_pua_publish(struct mi_root* cmd, void* param);
struct mi_root* mi_pua_subscribe(struct mi_root* cmd, void* param);
int mi_publ_rpl_cback(ua_pres_t* hentity, struct sip_msg* reply);

#endif	
