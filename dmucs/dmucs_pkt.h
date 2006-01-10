#ifndef _DMUCS_PKT_H_
#define _DMUCS_PKT_H_

/*
 * dmucs_pkt.h: definition of a DMUCS packet received by the DMUCS server.
 *
 * Copyright (C) 2005, 2006  Victor T. Norman
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "COSMIC/HDR/sockets.h"

enum dmucs_req_t {
    HOST_REQ,
    LOAD_AVERAGE_INFORM,
    STATUS_INFORM,
    MONITOR_REQ
};


/*
 * Format of packets that come in to the dmucs server:
 *
 * o host request:   "host <client IP address>
 * o load average:   "load <host IP address> <3 floating pt numbers>"
 * o status message: "status <host IP address> up|down [n <numCpus>]
 *		[p <powerIndex>]"
 * o monistor req:   "monitor <client IP address>"
 */

#include "dmucs_host.h"

class DmucsReq {
public:
    struct in_addr clientIp;
    dmucs_req_t reqType;
    union {
	struct {
	    struct in_addr host;
	    host_status_t status;
	    int numCpus;
	    int powerIndex;
	} statusData;
	struct {
	    struct in_addr host;
	    float ldAvg1, ldAvg5, ldAvg10;
	} ldAvgData;
    } u;

    static DmucsReq *parseReq(Socket *sock, const char *buf);
};


#define BUFSIZE 1024	// largest info we will read from the socket.

#endif
