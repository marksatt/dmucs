/*
 * dmucs_pkt.cc: code to parse a packet coming into the DMUCS server.
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

#include "dmucs.h"
#include "dmucs_pkt.h"
#include <exception>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

class DmucsBadReq : public std::exception {};


DmucsReq *
DmucsReq::parseReq(Socket *sock, const char *buffer)
{
    DmucsReq *req = new DmucsReq();

    DMUCS_DEBUG((stderr, "req is %p\n", req));

    /*
     * The first word in the buffer must be one of: "host", "load",
     * "status", or "monitor".
     */
    if (strncmp(buffer, "host", 4) == 0) {
	req->reqType = HOST_REQ;
    } else if (strncmp(buffer, "load", 4) == 0) {
	req->reqType = LOAD_AVERAGE_INFORM;

	/* The buffer must hold:
	 * load <host-IP-address> <3 floating pt numbers>
	 */
	char machname[64];
	float ldavg1, ldavg5, ldavg10;
	if (sscanf(buffer, "load %s %f %f %f", machname, &ldavg1,
		   &ldavg5, &ldavg10) != 4) {
	    fprintf(stderr, "Got a bad load avg msg!!!\n");
	}
	req->u.ldAvgData.host.s_addr = inet_addr(machname);
	req->u.ldAvgData.ldAvg1 = ldavg1;
	req->u.ldAvgData.ldAvg5 = ldavg5;
	req->u.ldAvgData.ldAvg10 = ldavg10;
	DMUCS_DEBUG((stderr, "host %s: ldAvg1 %2.2f, ldAvg5 %2.2f, "
		     "ldAvg10 %2.2f\n", machname, ldavg1, ldavg5, ldavg10));
    } else if (strncmp(buffer, "status", 6) == 0) {
	req->reqType = STATUS_INFORM;
	/* The buffer must hold:
	 * status <host-IP-address> up|down
	 * NOTE: the host-IP-address MUST be in "dot-notation".
	 */
	char machname[64];
	char state[10];
	if (sscanf(buffer, "status %s %s", machname, state) != 2) {
	    fprintf(stderr, "Got a bad req!!!\n");
	}
	fprintf(stderr, "machname %s, state %s\n", machname, state);
	req->u.statusData.host.s_addr = inet_addr(machname);
	if (strncmp(state, "up", 2) == 0) {
	    req->u.statusData.status = STATUS_AVAILABLE;
	} else if (strncmp(state, "down", 4) == 0) {
	    req->u.statusData.status = STATUS_UNAVAILABLE;
	} else {
	    fprintf(stderr, "got unknown state %s\n", state);
	}
	
    } else if (strncmp(buffer, "monitor", 7) == 0) {
	req->reqType = MONITOR_REQ;
    } else {
	fprintf(stderr, "request not recognized\n");
	throw DmucsBadReq();
    }

    req->clientIp.s_addr = Speeraddr(sock);

    return req;
}
