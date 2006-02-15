/*
 * main.cc: the DMUCS server -- to assign compilation hosts' cpus to
 * requestors, listen for load average messages and monitoring requests,
 * etc.
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
#include "dmucs_hosts_file.h"
#include "dmucs_host.h"
#include "dmucs_db.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fstream>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include "COSMIC/HDR/sockets.h"


#ifndef FD_COPY
#define FD_COPY(src, dest) memcpy(dest, src, sizeof(fd_set))
#endif

static void getHostForClient(Socket *sock);
static void spawn_stats_thread();
static void spawn_silent_thread();
static void *doSilentSearch(void *bogus);
static void *updateStats(void *bogus);
static void usage(const char *prog);
static void handleReq(Socket *server, DmucsDb *db);
static char* peer2buf(const Socket *server, char *buf);
static void addFd(Socket *sock);
static void removeFd(Socket *sock);

bool debugMode = false;
static std::string hostsInfoFile = HOSTS_INFO_FILE;

#ifndef HAVE_GETHOSTBYADDR_R
#ifdef HAVE_GETHOSTBYADDR
static pthread_mutex_t gethost_mutex;  
#endif /* HAVE_GETHOSTBYADDR */
#endif /* !HAVE_GETHOSTBYADDR_R */

static std::list<Socket *> fdList;
static fd_set fdMask;

int
main(int argc, char *argv[])
{
    /*
     * Open a socket on which we will:
     *   o receive requests for hosts
     *	   o fork a child process in which we:
     *	     o respond with the highest-tier available cpu, and move the cpu
     *	       into the assignedCpus set.
     *       o wait for the client to close the (slave) socket, indicating
     *	       that the compilation host is done.
     *       o place the compilation cpu back into the db of available cpus
     *   o receive load average messages from hosts
     *	   o recompute the new tier value for the host.  If it is different
     *       from the current tier, move the host in the availHosts map to
     *	     the new tier.
     *   o receive status messages (available|unavailable) from hosts
     *	   o if available, get the host information from the hosts-info
     *	     file and add the host to the availHosts data structure.
     *	   o if unavailable, remove the host from whatever set it is in.
     *   o receive monitoring requests from the monitoring clients.
     * 	   o package up the data structures and send the info in the reply.
     * 
     * Command-line arguments:
     *   o -D: display debugging output.  (Assumes -s.) Optional.
     *   o -p <port>: use <port>.  Required.
     *   o -s: do not fork as a daemon.
     */


    /*
     * Process command-line arguments:
     *
     * -p <port>, --port <port>: the port number to listen on (default: 9714).
     * -D, --debug: debug mode (default: off)
     * -H, --hosts-info-file <filename>: specify the hosts info file location.
     */

    int serverPortNum = SERVER_PORT_NUM;
    fd_set rmask;	// read mask
    fd_set emask;	// exception mask

#ifndef HAVE_GETHOSTBYADDR_R
#ifdef HAVE_GETHOSTBYADDR
       pthread_mutex_init(&gethost_mutex, NULL);
#endif /* HAVE_GETHOSTBYADDR */
#endif /* !HAVE_GETHOSTBYADDR_R */


    for (int i = 1; i < argc; i++) {
	if (strequ("-p", argv[i]) || strequ("--port", argv[i])) {
	    if (++i >= argc) {
		usage(argv[0]);
		return -1;
	    }
	    serverPortNum = atoi(argv[i]);
	} else if (strequ("-D", argv[i]) || strequ("--debug", argv[i])) {
	    debugMode = true;
	} else if (strequ("-H", argv[i]) ||
		   strequ("--hosts-info-file", argv[i])) {
	    if (++i >= argc) {
		usage(argv[0]);
		return -1;
	    }
	    hostsInfoFile = argv[i];
	} else {
	    usage(argv[0]);
	    return -1;
	}
    }


    /*
     * Make the database.
     */
    DmucsDb *db = DmucsDb::getInstance();

    /*
     * Open the socket.
     */
    char svrstr[16];
    sprintf(svrstr, "s%d", serverPortNum);
    Socket *server = Sopen(NULL, svrstr);
    if (!server) {
	fprintf(stderr, "Could not open server on port 9714.\n");
	return -1;
    }

    /*
     * Spawn a thread to periodically search the database for hosts
     * that have been silent.  Move these hosts to the SILENT state.
     */
    spawn_silent_thread();

    /*
     * Spawn a thread to periodically collect statistics and print them
     * out.
     */
    spawn_stats_thread();

    FD_ZERO(&fdMask);

    /* Process requests, forever!!!  Bwa, ha, ha! */
    while (1) {
	FD_COPY(&fdMask, &rmask);
	FD_COPY(&fdMask, &emask);
	FD_SET(server->skt, &rmask);
	DMUCS_DEBUG((stderr, "\n------- Server: calling select ---------\n"));
               
	int result = select(FD_SETSIZE, &rmask, NULL, &emask, NULL);
	DMUCS_DEBUG((stderr, "select returned %d\n", result));

	std::list<Socket*>::const_iterator it;
	for (it = fdList.begin(); it != fdList.end(); ++it) {
	    if (FD_ISSET(((Socket*)*it)->skt, &rmask)) {
		DMUCS_DEBUG((stderr,
			     "\n----- Server: Handle client request -----\n"));
		handleReq(*it, db);
		/* handleReq could change the list so we have to jump out
		   here. */
		break;
	    }
	}

	if (FD_ISSET(server->skt, &rmask)) {
	    DMUCS_DEBUG((stderr, "\n------- Server: Calling Saccept -----\n"));
       
	    Socket *sock_req = Saccept(server);
	    addFd(sock_req);
	    handleReq(sock_req, db);
	}
    }

#ifndef HAVE_GETHOSTBYADDR_R
#ifdef HAVE_GETHOSTBYADDR
    pthread_mutex_destroy(&gethost_mutex);
#endif /* HAVE_GETHOSTBYADDR */
#endif /* !HAVE_GETHOSTBYADDR_R */
}


static void
getHostForClient(Socket *sock)
{
    DMUCS_DEBUG((stderr, "thread: top\n"));
    DmucsDb *db = DmucsDb::getInstance();
    DMUCS_DEBUG((stderr, "thread: got instance\n"));

    try {
	DMUCS_DEBUG((stderr, "thread: hi\n"));
	unsigned int cpuIpAddr = db->getBestAvailCpu();
	std::string resolved_name = DmucsHost::resolveIp2Name(cpuIpAddr);

	fprintf(stderr, "Giving out %s\n", resolved_name.c_str());

	/* getBestAvailCpu() might return 0, when there are
	   no more available CPUs.  We send 0.0.0.0 to the client
	   but we don't record it as an assigned cpu. */
	if (cpuIpAddr != 0UL) {
	    db->assignCpuToClient(cpuIpAddr, (unsigned int) sock);
	}
	struct in_addr c;
	c.s_addr = cpuIpAddr;
	Sputs(inet_ntoa(c), sock);

#if 0
	fprintf(stderr, "The databases are now:\n");
	db->dump();
#endif

    } catch (DmucsNoMoreHosts &e) {
	fprintf(stderr, "!!!!!      Out of hosts    !!!!!\n");
    } catch (...) {
	fprintf(stderr, "!!!!!  Some other error  !!!!!\n");
    }
}


static void
spawn_silent_thread()
{
    pthread_attr_t tattr;
    pthread_attr_init(&tattr);
    /* We don't care about joining up this thread with its parent -- it
       won't matter because both will die off together -- when the
       server is killed. */
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
    pthread_t thread_id;
    if (pthread_create(&thread_id, &tattr, doSilentSearch,
		       (void *) NULL) != 0) {
	perror("pthread_create");
	return;
    }
}

static void
spawn_stats_thread()
{
    pthread_attr_t tattr;
    pthread_attr_init(&tattr);
    /* We don't care about joining up this thread with its parent -- it
       won't matter because both will die off together -- when the
       server is killed. */
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
    pthread_t thread_id;
    if (pthread_create(&thread_id, &tattr, updateStats, (void *) NULL) != 0) {
	perror("pthread_create");
	return;
    }
}

static void *
doSilentSearch(void *bogus /* not used */)
{
    while (1) {
	struct timeval t = { 60L, 0L };		// 60 seconds.
	select(0, NULL, NULL, NULL, &t);
	DmucsDb::getInstance()->handleSilentHosts();
    }
}



static void *
updateStats(void *bogus /* not used */)
{
    while (1) {
	int served, max, avail;
	char buf[32];
	DmucsDb::getInstance()->getStatsFromDb(&served, &max, &avail);
	time_t t = time(NULL);
	(void) ctime_r(&t, buf);
	/* There is a newline on the end of buf -- remove it. */
	buf[strlen(buf) - 1] = '\0';
	fprintf(stderr, "[%s] Hosts Served: %d  Max/Avail: %d/%d\n",
		buf, served, max, avail);

	struct timeval sleepTime = { 60L, 0L };		// 60 seconds.
	select(0, NULL, NULL, NULL, &sleepTime);
    }
}


static void
usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-p|--port <port>] [-D|--debug] "
	    "[-H|--hosts-info-file <file>]\n\n", prog);
}


static void
handleReq(Socket *sock_req, DmucsDb *db)
{
    char buf[BUFSIZE];

    DMUCS_DEBUG((stderr, "New request from %s\n", peer2buf(sock_req, buf)));

    if (Sgets(buf, BUFSIZE, sock_req) == NULL) {
	DMUCS_DEBUG((stderr, "Socket closed\n"));
	db->releaseCpu((unsigned int)sock_req);
	removeFd(sock_req);
	return;
    }

    DmucsReq *req = DmucsReq::parseReq(sock_req, buf);
    if (req == NULL) {
	fprintf(stderr, "Got bad request on socket.  Continuing.\n");
	removeFd(sock_req);
	return;
    }

    switch (req->reqType) {
    case HOST_REQ: {
	DMUCS_DEBUG((stderr, "Got host request from %s\n",
		     inet_ntoa(req->clientIp)));
	getHostForClient(sock_req);
	break;
    }
    case LOAD_AVERAGE_INFORM: {
	DMUCS_DEBUG((stderr, "Got load average mesg\n"));
	try {
	    DmucsHost *host = db->getHost(req->u.ldAvgData.host);
	    /* If the host hasn't been explicitly made unavailable,
	       then make it available. */
	    if (! host->isUnavailable()) {
		host->avail();      // make sure the host is available
	    }
	    host->updateTier(req->u.ldAvgData.ldAvg1,
			     req->u.ldAvgData.ldAvg5,
			     req->u.ldAvgData.ldAvg10);
	} catch (DmucsHostNotFound &e) {
	    DmucsHost *h = DmucsHost::createHost(req->u.ldAvgData.host,
						 hostsInfoFile);
	    h->updateTier(req->u.ldAvgData.ldAvg1, req->u.ldAvgData.ldAvg5,
			  req->u.ldAvgData.ldAvg10);
	} catch (...) {
	}
	removeFd(sock_req);
	break;
    }
    case STATUS_INFORM: {
	if (req->u.statusData.status == STATUS_AVAILABLE) {
	    if (db->haveHost(req->u.statusData.host)) {
		/* Make it available (if it wasn't). */
		db->getHost(req->u.statusData.host)->avail();
	    } else {
		/* A new host is available! */
		DMUCS_DEBUG((stderr, "Creating new host %s\n",
			     inet_ntoa(req->u.statusData.host)));
		DmucsHost::createHost(req->u.statusData.host, hostsInfoFile);
	    }
	} else {    // status is unavailable.
	    db->getHost(req->u.statusData.host)->unavail();
	}
	removeFd(sock_req);
	break;
    }
    case MONITOR_REQ: {
	std::string str = db->serialize();
	Sputs((char *) str.c_str(), sock_req);
	removeFd(sock_req);
	break;
    }
    default: {
	fprintf(stderr, "Bad request: %s\n", buf);
	removeFd(sock_req);
    }
    } // switch

    delete req;
}


static char *
peer2buf(const Socket *sock, char *buf)
{
    struct sockaddr sck;
    socklen_t s = sizeof(sck);
    getpeername(sock->skt, &sck, &s);
    struct sockaddr_in *sin = (struct sockaddr_in *) &sck;
       
    sprintf(buf, "%s:%d", inet_ntoa(sin->sin_addr), sin->sin_port);
    return buf;     
}


static void
addFd(Socket *sock)
{
    fdList.push_back(sock);
    FD_SET(sock->skt, &fdMask);
}

static void
removeFd(Socket *sock)
{
    FD_CLR(sock->skt, &fdMask);
    fdList.remove(sock);
    Sclose(sock);
}
