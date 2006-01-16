
/*
 * dmucs_host.cc: a DmucsHost is a representation of a compilation host.
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

#include "dmucs_host.h"
#include "dmucs_db.h"
#include "dmucs_hosts_file.h"
#include "dmucs_host_state.h"
#include <netinet/in.h>
#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


DmucsHost::DmucsHost(const struct in_addr &ipAddr,
		     const int numCpus, const int powerIndex) :
    ipAddr_(ipAddr), ncpus_(numCpus), pindex_(powerIndex),
    ldavg1_(0), ldavg5_(0), ldavg10_(0),
    lastUpdate_(0)
{
    state_ = DmucsHostStateAvail::getInstance();
}


DmucsHost *
DmucsHost::createHost(const struct in_addr &ipAddr,
		      const std::string &hostsInfoFile)
{
    /*
     * Read the hosts file, and find the entry in it for this host.
     */
    DmucsHostsFile *hostsFile = DmucsHostsFile::getInstance(hostsInfoFile);
    int numCpus = 1;
    int powerIndex = 1;
    hostsFile->getDataForHost(ipAddr, &numCpus, &powerIndex);
    DmucsHost *newHost = new DmucsHost(ipAddr, numCpus, powerIndex);

    DmucsDb::getInstance()->addNewHost(newHost);

    return newHost;
}

const int
DmucsHost::getStateAsInt() const
{
    return state_->asInt();
}

int
DmucsHost::getTier() const
{
    return calcTier(ldavg1_, ldavg5_, ldavg10_, pindex_);
}

int
DmucsHost::calcTier(float ldavg1, float ldavg5, float ldavg10,
		    int pindex) const
{
    if (ldavg1 < 0.9) {
	return pindex;
    } else if (ldavg5 < 0.7) {
	return pindex;
    } else if (ldavg10 < 0.8) {
	return pindex - 1;
    } else {
	return 0;	// 0 means don't use this host.
    }
}


void
DmucsHost::updateTier(float ldAvg1, float ldAvg5, float ldAvg10)
{
    ldAvg1 /= (float) ncpus_;
    ldAvg5 /= (float) ncpus_;
    ldAvg10 /= (float) ncpus_;
    int newTier = calcTier(ldAvg1, ldAvg5, ldAvg10, pindex_);
    int oldTier = getTier();

    if (newTier != oldTier) {
	if (newTier == 0) {
	    /* This host is completely overloaded: remove the CPU objects
	       from their current tier, and move this host object to the
	       overloaded state. */
	    overloaded();
	    ldavg1_ = ldAvg1; ldavg5_ = ldAvg5; ldavg10_ = ldAvg10;
	} else if (oldTier == 0) {
	    /* This host was overloaded, but now it is not. 
	       Move the cpu objects from one tier to another. */
	    DmucsDb::getInstance()->moveCpus(this, oldTier, newTier);
	    ldavg1_ = ldAvg1; ldavg5_ = ldAvg5; ldavg10_ = ldAvg10;
	} else {
	    /* Move the cpu objects from one tier to another. */
	    DmucsDb::getInstance()->moveCpus(this, oldTier, newTier);
	    ldavg1_ = ldAvg1; ldavg5_ = ldAvg5; ldavg10_ = ldAvg10;
	}
    } else {
	ldavg1_ = ldAvg1; ldavg5_ = ldAvg5; ldavg10_ = ldAvg10;
    }
    lastUpdate_ = time(0);
}



void
DmucsHost::avail()
{
    state_->avail(this);
}


void
DmucsHost::unavail()
{
    state_->unavail(this);
}


void
DmucsHost::silent()
{
    state_->silent(this);
}


void
DmucsHost::overloaded()
{
    state_->overloaded(this);
}


void
DmucsHost::changeState(DmucsHostState *state)
{
    state_ = state;
    state_->addToDb(this);
}


bool
DmucsHost::seemsDown() const
{
    return (time(0) - lastUpdate_ > DMUCS_HOST_SILENT_TIME);
}


bool
DmucsHost::isUnavailable() const
{
    return (state_->asInt() == STATUS_UNAVAILABLE);
}


void
DmucsHost::dump()
{
    fprintf(stderr, "Host: %20.20s    State: %s    Pindex: %d Ncpus %d\n",
	    inet_ntoa(ipAddr_), state_->dump(),
	    pindex_, ncpus_);
}


std::string
DmucsHost::getName()
{
    if (! resolvedName_.empty()) {
	return resolvedName_;
    }
    int myerrno;
    int res8 = 0;
    char buffer[128];
    struct hostent he, *res = 0;


#if HAVE_GETHOSTBYADDR_R
#if HAVE_GETHOSTBYADDR_R_7_ARGS
    res = gethostbyaddr_r((char *)&(ipAddr_.s_addr), sizeof(ipAddr_.s_addr),
			  AF_INET, &he, buffer, 128, &myerrno);
#elif HAVE_GETHOSTBYADDR_R_8_ARGS
    res8 = gethostbyaddr_r((char *)&(ipAddr_.s_addr), sizeof(ipAddr_.s_addr),
			   AF_INET, &he, buffer, 128, &res, &myerrno);
#else
#error HELP -- do not know how to compile gethostbyaddr_r
#endif /* HAVE_GETHOSTBYADDR_R_X_ARGS */
#elif HAVE_GETHOSTBYADDR
    pthread_mutex_lock(&gethost_mutex);
    res = gethostbyaddr((char *)&(ipAddr_.s_addr), sizeof(ipAddr_.s_addr),
			AF_INET);
    strncpy(buffer, res->h_name, sizeof(buffer));
    buffer[sizeof(buffer)] = 0;
    res->h_name = buffer;
    pthread_mutex_unlock(&gethost_mutex);
#endif

    resolvedName_ = (res == NULL || res8 != 0) ?
	inet_ntoa(ipAddr_) : he.h_name;
    return resolvedName_;
}


/*
 * Given in IP address, find the host in the host database.  If its name has
 * already been found, then return it.  Otherwise, resolve it and store it
 * in the host and return the string.
 */
std::string
DmucsHost::resolveIp2Name(unsigned int ipAddr)
{
    /* Search for DmucsHost, based on Ip Address */
    struct in_addr c;
    c.s_addr = ipAddr;
    return DmucsDb::getInstance()->getHost(c)->getName();
}
