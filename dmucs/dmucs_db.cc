/*
 * dmucs_db.cc: the DMUCS database object
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

#include "dmucs_db.h"
#include <algorithm>
#include <stdio.h>
#include <exception>
#include <string>
#include <vector>
#include <sstream>


DmucsDb *DmucsDb::instance_ = NULL;
pthread_mutex_t DmucsDb::mutex_;
pthread_mutexattr_t DmucsDb::attr_;

class MutexMonitor
{
 public:
    MutexMonitor(pthread_mutex_t *m) : m_(m)
    {
	pthread_mutex_lock(m_);
    }
    ~MutexMonitor()
    {
	pthread_mutex_unlock(m_);
    }
 private:
    pthread_mutex_t *m_;
};


DmucsDb::DmucsDb()
{
    pthread_mutexattr_init(&attr_);
    if (pthread_mutexattr_settype(&attr_, PTHREAD_MUTEX_RECURSIVE) < 0) {
	throw std::bad_alloc();
    }
    pthread_mutex_init(&mutex_, &attr_);
}


DmucsDb *
DmucsDb::getInstance()
{
    if (instance_ == NULL) {
	instance_ = new DmucsDb();
    }
    return instance_;
}


/*
 * getHost: search for the DmucsHost object in the various sets, and return
 * it when it is found.  If it is not found, throw a DmucsHostNotFound
 * exception. 
 */
DmucsHost *
DmucsDb::getHost(const struct in_addr &ipAddr)
{
    MutexMonitor m(&mutex_);
    DmucsHost host(ipAddr, 0, 0);
    dmucs_host_set_iter_t hptr = allHosts_.find(&host);
    if (hptr == allHosts_.end()) {
	throw DmucsHostNotFound(/* ipaddr */);
    }
    return *hptr;
}


bool
DmucsDb::haveHost(const struct in_addr &ipAddr)
{
    MutexMonitor m(&mutex_);
    DmucsHost host(ipAddr, 0, 0);
    return (allHosts_.find(&host) != allHosts_.end());
}


/*
 * return the IP address of a randomly-selected, highest-tier available cpu
 */
unsigned int
DmucsDb::getBestAvailCpu()
{
    MutexMonitor m(&mutex_);
    unsigned int result = 0UL;
    for (dmucs_avail_cpus_riter_t itr = availCpus_.rbegin();
	 itr != availCpus_.rend(); ++itr) {
	if (itr->second.empty()) {
	    continue;
	}
	srandom((unsigned int) time(NULL));
	int n = random() % itr->second.size();
	dmucs_cpus_iter_t itr2 = itr->second.begin();
	for (int i = 0; i < n; ++itr2, i++) ;
	result = *itr2;	// get the IP address of the nth element in the list
	/* Remove the nth element from the list. */
	itr->second.erase(itr2);
	return result;
    }
    return result;		// no more cpus available 
}


void
DmucsDb::assignCpuToClient(const unsigned int hostIp,
			   const unsigned int clientIp)
{
    MutexMonitor m(&mutex_);
    // NOTE: we don't currently do anything with the clientIp.
    assignedCpus_.push_front(hostIp);

    numAssignedCpus_++;

    int t;
    if ((t = assignedCpus_.size()) > numConcurrentAssigned_) {
	numConcurrentAssigned_ = t;
    }
}


void
DmucsDb::releaseCpu(const unsigned int hostIp)
{
    MutexMonitor m(&mutex_);
    for (dmucs_cpus_iter_t itr = assignedCpus_.begin();
	 itr != assignedCpus_.end(); ++itr) {
	if (*itr == hostIp) {
	    assignedCpus_.erase(itr);
	    struct in_addr in;
	    in.s_addr = hostIp;
	    try {
		DmucsHost *host = getHost(in);
		/* The host may be marked unavailable while one of the cpus
		   was assigned.  In this case, don't add the cpu back. */
		if (host->getStateAsInt() == STATUS_AVAILABLE) {
		    int tier = host->getTier();
		    addCpusToTier(tier, hostIp, 1);
		}
	    } catch (DmucsHostNotFound &e) {
		/* The host may have been removed from the db while a cpu
		   was assigned.  In this case, just don't add the cpu back
		   to the availCpus_ db table. */
	    }
	    return;
	}
    }
    struct in_addr in;
    in.s_addr = hostIp;
    fprintf(stderr, "releaseCpu(%s): not found in assignedCpus list!!\n",
	    inet_ntoa(in));
}


std::string
DmucsDb::serialize()
{
    /*
     * We will encode the database this way: it will be a big long string
     * with newlines in it.  The lines will look like this:
     * H: <ip-addr> <int>      // a host, its ip address, and its state.
     * C <tier>: <ipaddr>/<#cpus>
     *
     * o The state is represented by an integer representing the
     *   host_status_t enum value.
     * o The entire string will end with a \0 (end-of-string) character.
     */
    std::ostringstream result;
    
    MutexMonitor m(&mutex_);
    for (dmucs_host_set_iter_t itr = allHosts_.begin();
	 itr != allHosts_.end(); ++itr) {
	struct in_addr in;
	in.s_addr = (*itr)->getIpAddrInt();
	result << "H: " << inet_ntoa(in) << " " << (*itr)->getStateAsInt() << "\n";
    }

    for (dmucs_avail_cpus_riter_t itr = availCpus_.rbegin();
	 itr != availCpus_.rend(); ++itr) {
	if (itr->second.empty()) {
	    continue;
	}

	result << "C " << itr->first << ": ";

	std::vector<std::pair<unsigned int, int> > uniqIps;
	int ct = 0;
	unsigned int curr = 0;	// holds an ip address
	dmucs_cpus_t tmpList(itr->second);
	tmpList.sort();
	for (dmucs_cpus_iter_t itr2 = tmpList.begin(); itr2 != tmpList.end();
	     ++itr2) {
	    if (curr == *itr2) {
		ct++;
	    } else {
		if (curr != 0) {
		    uniqIps.push_back(std::pair<unsigned int, int>(curr, ct));
		}
		curr = *itr2;
		ct = 1;
	    }
	}
	if (ct != 0) {
	    uniqIps.push_back(std::pair<unsigned int, int>(curr, ct));
	}

	for (std::vector<std::pair<unsigned int, int> >::iterator i =
		 uniqIps.begin(); i != uniqIps.end(); ++i) {
	    struct in_addr t;
	    t.s_addr = i->first;
	    result << inet_ntoa(t) << "/" << i->second << " ";
	}
	result << '\n';
    }
#if 0
    fprintf(stderr, "Serialize: -->%s<--\n", result.str().c_str());
#endif
    return result.str();
}


void
DmucsDb::addNewHost(DmucsHost *host)
{
    MutexMonitor m(&mutex_);
    /*
     * Add the host to the allHosts_ set and then also to the availHosts_
     * sub-set.
     */
    addToHostSet(&allHosts_, host);
    addToAvailDb(host);
}


void
DmucsDb::addToHostSet(dmucs_host_set_t *theSet, DmucsHost *host)
{
    MutexMonitor m(&mutex_);
    std::pair<dmucs_host_set_iter_t, bool> status = theSet->insert(host);
    if (!status.second) {
	fprintf(stderr, "%s: Waaaaaah!!!!\n", __func__);
    }
}


void
DmucsDb::delFromHostSet(dmucs_host_set_t *theSet, DmucsHost *host)
{
    MutexMonitor m(&mutex_);
    dmucs_host_set_iter_t itr = theSet->find(host);

    if (itr == theSet->end()) {
	fprintf(stderr, "%s: Waaaaaah!!!!\n", __func__);
	return;
    }
    theSet->erase(host);
}


void
DmucsDb::addToAvailDb(DmucsHost *host)
{
    MutexMonitor m(&mutex_);
    addToHostSet(&availHosts_, host);
    addCpusToTier(host->getTier(), host->getIpAddrInt(), host->getNumCpus());
}


void
DmucsDb::delFromAvailDb(DmucsHost *host)
{
    MutexMonitor m(&mutex_);
    dmucs_avail_cpus_iter_t itr = availCpus_.find(host->getTier());
    if (itr == availCpus_.end()) {
	fprintf(stderr, "%s: could not find tier in avail cpu db\n", __func__);
	return;
    }
    itr->second.remove(host->getIpAddrInt());

    delFromHostSet(&availHosts_, host);
}


void
DmucsDb::addToOverloadedDb(DmucsHost *host)
{
    MutexMonitor m(&mutex_);
    addToHostSet(&overloadedHosts_, host);
}


void
DmucsDb::delFromOverloadedDb(DmucsHost *host)
{
    MutexMonitor m(&mutex_);
    delFromHostSet(&overloadedHosts_, host);
}


void
DmucsDb::addToSilentDb(DmucsHost *host)
{
    MutexMonitor m(&mutex_);
    addToHostSet(&silentHosts_, host);
}


void
DmucsDb::delFromSilentDb(DmucsHost *host)
{
    MutexMonitor m(&mutex_);
    delFromHostSet(&silentHosts_, host);
}


void
DmucsDb::addToUnavailDb(DmucsHost *host)
{
    MutexMonitor m(&mutex_);
    addToHostSet(&unavailHosts_, host);
}


void
DmucsDb::delFromUnavailDb(DmucsHost *host)
{
    MutexMonitor m(&mutex_);
    delFromHostSet(&unavailHosts_, host);
}



/* Add "numCpus" copies of the ipaddress to the list in the given tier. */
void
DmucsDb::addCpusToTier(int tierNum,
		       const unsigned int ipAddr, const int numCpus)
{
    MutexMonitor m(&mutex_);
    /*
     * If a tier, doesn't exist yet, create one.
     */
    dmucs_avail_cpus_iter_t itr = availCpus_.find(tierNum);
    if (itr == availCpus_.end()) {
	std::pair<dmucs_avail_cpus_iter_t, bool> status =
	    availCpus_.insert(std::make_pair(tierNum, dmucs_cpus_t()));
	if (!status.second) {
	    fprintf(stderr, "%s: Waaaaaah!!!!\n", __func__);
	    return;
	}
	itr = status.first;
    }

    itr->second.insert(itr->second.end(), numCpus, ipAddr);
}

void
DmucsDb::moveCpus(DmucsHost *host, int oldTier, int newTier)
{
    MutexMonitor m(&mutex_);
    int numCpusDel = delCpusFromTier(oldTier, host->getIpAddrInt());
    addCpusToTier(newTier, host->getIpAddrInt(), numCpusDel);
}


/* Return the number of cpus removed from the tier. */
int
DmucsDb::delCpusFromTier(int tier, unsigned int ipAddr)
{
    MutexMonitor m(&mutex_);
    dmucs_avail_cpus_iter_t itr = availCpus_.find(tier);
    if (itr == availCpus_.end()) {
	fprintf(stderr, "OOOOUCCCH: shouldn't happen\n");
	return 0;
    }
    int count = 0;
    for (dmucs_cpus_iter_t itr2 = itr->second.begin();
	 itr2 != itr->second.end();) {
	if (*itr2 == ipAddr) {
	    itr2 = itr->second.erase(itr2);
	    count++;
	} else {
	    ++itr2;
	}
    }
    return count;
}


void
DmucsDb::handleSilentHosts()
{
    MutexMonitor m(&mutex_);

    for (dmucs_host_set_iter_t itr = allHosts_.begin();
	 itr != allHosts_.end(); ++itr) {
	if ((*itr)->seemsDown()) {
	    (*itr)->silent();
	}
    }
}


void
DmucsDb::dump()
{
    MutexMonitor m(&mutex_);
    fprintf(stderr, "ALLHOSTS:\n");
    for (dmucs_host_set_iter_t itr = allHosts_.begin();
	 itr != allHosts_.end(); ++itr) {
	(*itr)->dump();
    }

    fprintf(stderr, "AVAIL HOSTS:\n");
    for (dmucs_host_set_iter_t itr = availHosts_.begin();
	 itr != availHosts_.end(); ++itr) {
	(*itr)->dump();
    }

    fprintf(stderr, "AVAIL CPUS:\n");
    for (dmucs_avail_cpus_iter_t itr = availCpus_.begin();
	 itr != availCpus_.end(); ++itr) {
	if (itr->second.empty()) {
	    continue;
	}
	fprintf(stderr, "Tier %d: ", itr->first);
	for (dmucs_cpus_iter_t itr2 = itr->second.begin();
	     itr2 != itr->second.end(); ++itr2) {
	    struct in_addr t;
	    t.s_addr = *itr2;
	    fprintf(stderr, "%s ", inet_ntoa(t));
	}
	fprintf(stderr, "\n");
    }
    fprintf(stderr, "ASSIGNED CPUS:\n");
    for (dmucs_cpus_iter_t itr = assignedCpus_.begin();
	 itr != assignedCpus_.end(); ++itr) {
	struct in_addr t;
	t.s_addr = *itr;
	fprintf(stderr, "%s ", inet_ntoa(t));
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "OVERLOADED HOSTS:\n");
    for (dmucs_host_set_iter_t itr = overloadedHosts_.begin();
	 itr != overloadedHosts_.end(); ++itr) {
	(*itr)->dump();
    }
    fprintf(stderr, "SILENT HOSTS:\n");
    for (dmucs_host_set_iter_t itr = silentHosts_.begin();
	 itr != silentHosts_.end(); ++itr) {
	(*itr)->dump();
    }
    fprintf(stderr, "UNAVAIL HOSTS:\n");
    for (dmucs_host_set_iter_t itr = unavailHosts_.begin();
	 itr != unavailHosts_.end(); ++itr) {
	(*itr)->dump();
    }
}


/*
 * Return some stats from the database usage:
 * o served: the number of cpus served to clients in the last time period.
 * o max: the maximum number of cpus assigned to clients at one time, during
 *     the last period.
 * o totalCpus: the total number of cpus available at this time.
 *
 * NOTE: this function also clears the stats back to 0: i.e., starts a new
 * collection period.
 */
void
DmucsDb::getStatsFromDb(int *served, int *max, int *totalCpus)
{
    MutexMonitor m(&mutex_);
    *served = numAssignedCpus_;
    numAssignedCpus_ = 0;
    *max = numConcurrentAssigned_;
    numConcurrentAssigned_ = 0;
    *totalCpus = 0;
    for (dmucs_avail_cpus_iter_t itr = availCpus_.begin();
	 itr != availCpus_.end(); ++itr) {
	*totalCpus += itr->second.size();
    }
    *totalCpus += assignedCpus_.size();
}
