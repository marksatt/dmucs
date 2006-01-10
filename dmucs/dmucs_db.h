#ifndef _DMUCS_DB_H_
#define _DMUCS_DB_H_ 1

/*
 * dmucs_db.h: the DMUCS database object definition
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

#include <set>
#include <map>
#include <list>
#include "dmucs_host.h"
#include <pthread.h>


class DmucsDb
{
private:
    struct DmucsHostCompare {
	bool operator () (DmucsHost *lhs, DmucsHost *rhs) const;
    };
    typedef std::set<DmucsHost *, DmucsHostCompare> dmucs_host_set_t;
    typedef dmucs_host_set_t::iterator dmucs_host_set_iter_t;

    /* Store a list of available cpu ipaddresses -- there will usually be
       more than one instance of a cpu (ipaddress) in the list, as even on
       a single cpu machine we can do more than one compile.

       Each list of IP addresses is a "tier" -- a set of cpus with
       approximately equivalent computational power.  We then have a map
       of these tiers (lists), indexed by an integer, where the lower the
       integer, the less powerful the cpus in that tier. */
    typedef std::list<unsigned int> dmucs_cpus_t;
    typedef dmucs_cpus_t::iterator dmucs_cpus_iter_t;
    typedef std::map<int, dmucs_cpus_t> dmucs_avail_cpus_t;
    typedef dmucs_avail_cpus_t::iterator dmucs_avail_cpus_iter_t;
    typedef dmucs_avail_cpus_t::reverse_iterator dmucs_avail_cpus_riter_t;

    /* 
     * Databases of hosts.
     * o a collection of available hosts, sorted by tier.
     * o a collection of assigned hosts.
     * o a collection of silent hosts.
     * o a collection of overloaded hosts.
     *
     * o a collectoin of available (unassigned) cpus.
     * o a collection of assigned cpus.
     */

    dmucs_host_set_t 	allHosts_;	// all known hosts are here.

    dmucs_host_set_t	availHosts_;	// avail hosts are also here.
    dmucs_host_set_t	unavailHosts_;	// unavail hosts are also here.
    dmucs_host_set_t 	silentHosts_;	// silent hosts are also here
    dmucs_host_set_t	overloadedHosts_;// overloaded hosts are here.

    dmucs_avail_cpus_t	availCpus_;	// unassigned cpus are here.
    dmucs_cpus_t	assignedCpus_;	// assigned cpus are here.

    /* Statistics */
    int numAssignedCpus_;	/* the # of assigned CPUs during a collection
				   period */
    int numConcurrentAssigned_; /* the max number of assigned CPUs at one
				   time. */
    
    DmucsDb();
    virtual ~DmucsDb() {}

    void addToHostSet(dmucs_host_set_t *theSet, DmucsHost *host);
    void delFromHostSet(dmucs_host_set_t *theSet, DmucsHost *host);
    void addCpusToTier(int tierNum,
		       const unsigned int ipAddr, const int numCpus);

    static DmucsDb *instance_;
    static pthread_mutexattr_t attr_;
    static pthread_mutex_t mutex_;

public:
    static DmucsDb *getInstance();

    DmucsHost * getHost(const struct in_addr &ipAddr);
    bool 	haveHost(const struct in_addr &ipAddr);
    unsigned int getBestAvailCpu();
    void	assignCpuToClient(const unsigned int clientIp,
				  const unsigned int cpuIp);
    void 	moveCpus(DmucsHost *host, int oldTier, int newTier);
    int 	delCpusFromTier(int tier, unsigned int ipAddr);

    void 	addNewHost(DmucsHost *host);
    void	releaseCpu(const unsigned int cpuIpAddr);

    void 	addToAvailDb(DmucsHost *host);
    void 	delFromAvailDb(DmucsHost *host);
    void	addToOverloadedDb(DmucsHost *host);
    void	delFromOverloadedDb(DmucsHost *host);
    void	addToSilentDb(DmucsHost *host);
    void 	delFromSilentDb(DmucsHost *host);
    void	addToUnavailDb(DmucsHost *host);
    void 	delFromUnavailDb(DmucsHost *host);

    void	handleSilentHosts();
    std::string	serialize();
    void	getStatsFromDb(int *served, int *max, int *totalCpus);
    void	dump();
};


inline bool
DmucsDb::DmucsHostCompare::operator() (DmucsHost *lhs, DmucsHost *rhs) const
{
    // Just do pointer comparison.
    return (lhs->getIpAddrInt() < rhs->getIpAddrInt());
}

#endif


