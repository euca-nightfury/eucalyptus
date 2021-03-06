// -*- mode: C; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil -*-
// vim: set softtabstop=4 shiftwidth=4 tabstop=4 expandtab:

/*************************************************************************
 * Copyright 2009-2012 Eucalyptus Systems, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 *
 * Please contact Eucalyptus Systems, Inc., 6755 Hollister Ave., Goleta
 * CA 93117, USA or visit http://www.eucalyptus.com/licenses/ if you need
 * additional information or have any questions.
 *
 * This file may incorporate work covered under the following copyright
 * and permission notice:
 *
 *   Software License Agreement (BSD License)
 *
 *   Copyright (c) 2008, Regents of the University of California
 *   All rights reserved.
 *
 *   Redistribution and use of this software in source and binary forms,
 *   with or without modification, are permitted provided that the
 *   following conditions are met:
 *
 *     Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *     Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *   INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE. USERS OF THIS SOFTWARE ACKNOWLEDGE
 *   THE POSSIBLE PRESENCE OF OTHER OPEN SOURCE LICENSED MATERIAL,
 *   COPYRIGHTED MATERIAL OR PATENTED MATERIAL IN THIS SOFTWARE,
 *   AND IF ANY SUCH MATERIAL IS DISCOVERED THE PARTY DISCOVERING
 *   IT MAY INFORM DR. RICH WOLSKI AT THE UNIVERSITY OF CALIFORNIA,
 *   SANTA BARBARA WHO WILL THEN ASCERTAIN THE MOST APPROPRIATE REMEDY,
 *   WHICH IN THE REGENTS' DISCRETION MAY INCLUDE, WITHOUT LIMITATION,
 *   REPLACEMENT OF THE CODE SO IDENTIFIED, LICENSING OF THE CODE SO
 *   IDENTIFIED, OR WITHDRAWAL OF THE CODE CAPABILITY TO THE EXTENT
 *   NEEDED TO COMPLY WITH ANY SUCH LICENSES OR RIGHTS.
 ************************************************************************/

//!
//! @file net/euca_gni.c
//! Implementation of the global network interface
//!

/*----------------------------------------------------------------------------*\
 |                                                                            |
 |                                  INCLUDES                                  |
 |                                                                            |
\*----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/types.h>
#include <dirent.h>
#include <linux/limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>

#include <eucalyptus.h>
#include <misc.h>
#include <hash.h>
#include <euca_string.h>
#include <euca_network.h>
#include <atomic_file.h>

#include "ipt_handler.h"
#include "ips_handler.h"
#include "ebt_handler.h"
#include "dev_handler.h"
#include "euca_gni.h"
#include "euca_lni.h"
#include "eucanetd_util.h"

/*----------------------------------------------------------------------------*\
 |                                                                            |
 |                                  DEFINES                                   |
 |                                                                            |
\*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*\
 |                                                                            |
 |                                  TYPEDEFS                                  |
 |                                                                            |
\*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*\
 |                                                                            |
 |                                ENUMERATIONS                                |
 |                                                                            |
\*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*\
 |                                                                            |
 |                                 STRUCTURES                                 |
 |                                                                            |
\*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*\
 |                                                                            |
 |                             EXTERNAL VARIABLES                             |
 |                                                                            |
\*----------------------------------------------------------------------------*/

/* Should preferably be handled in header file */

/*----------------------------------------------------------------------------*\
 |                                                                            |
 |                              GLOBAL VARIABLES                              |
 |                                                                            |
\*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*\
 |                                                                            |
 |                              STATIC VARIABLES                              |
 |                                                                            |
\*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*\
 |                                                                            |
 |                              STATIC PROTOTYPES                             |
 |                                                                            |
\*----------------------------------------------------------------------------*/
#define TCP_PROTOCOL_NUMBER 6
#define UDP_PROTOCOL_NUMBER 17
#define ICMP_PROTOCOL_NUMBER 1
static boolean xml_initialized = FALSE;    //!< To determine if the XML library has been initialized

#ifndef XML_INIT                    // if compiling as a stand-alone binary (for unit testing)
#define XML_INIT() if (!xml_initialized) { xmlInitParser(); xml_initialized = TRUE; }
#endif
//! Static prototypes
static int map_proto_to_names(int proto_number, char *out_proto_name, int out_proto_len);

/*----------------------------------------------------------------------------*\
 |                                                                            |
 |                                   MACROS                                   |
 |                                                                            |
\*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*\
 |                                                                            |
 |                               IMPLEMENTATION                               |
 |                                                                            |
\*----------------------------------------------------------------------------*/

//!
//! Creates a unique IP table chain name for a given security group. This name, if successful
//! will have the form of EU_[hash] where [hash] is the 64 bit encoding of the resulting
//! "[account id]-[group name]" string from the given security group information.
//!
//! @param[in] gni a pointer to the global network information structure
//! @param[in] secgroup a pointer to the security group for which we compute the chain name
//! @param[out] outchainname a pointer to the string that will contain the computed name
//!
//! @return 0 on success or 1 on failure
//!
//! @see
//!
//! @pre
//!     The outchainname parameter Must not be NULL but it should point to a NULL value. If
//!     this does not point to NULL, the memory will be lost when replaced with the out value.
//!
//! @post
//!     On success, the outchainname will point to the resulting string. If a failure occur, any
//!     value pointed by outchainname is non-deterministic.
//!
//! @note
//!
int gni_secgroup_get_chainname(globalNetworkInfo * gni, gni_secgroup * secgroup, char **outchainname)
{
    char hashtok[16 + 128 + 1];
    char chainname[48];
    char *chainhash = NULL;

    if (!gni || !secgroup || !outchainname) {
        LOGWARN("invalid argument: cannot get chainname from NULL\n");
        return (1);
    }

    snprintf(hashtok, 16 + 128 + 1, "%s-%s", secgroup->accountId, secgroup->name);
    hash_b64enc_string(hashtok, &chainhash);
    if (chainhash) {
        snprintf(chainname, 48, "EU_%s", chainhash);
        *outchainname = strdup(chainname);
        EUCA_FREE(chainhash);
        return (0);
    }
    LOGERROR("could not create iptables compatible chain name for sec. group (%s)\n", secgroup->name);
    return (1);
}

//!
//! Searches and returns a pointer to the route table data structure given its name in the argument..
//!
//! @param[in] vpc a pointer to the vpc gni data structure
//! @param[in] tableName name of the route table of interest
//!
//! @return pointer to the gni route table of interest if found. NULL otherwise
//!
//! @see
//!
//! @pre
//!     gni data structure is assumed to be populated.
//!
//! @post
//!
//! @note
//!
gni_route_table *gni_vpc_get_routeTable(gni_vpc *vpc, const char *tableName) {
    int i = 0;
    boolean found = FALSE;
    gni_route_table *result = NULL;
    for (i = 0; i < vpc->max_routeTables && !found; i++) {
        if (strcmp(tableName, vpc->routeTables[i].name) == 0) {
            result = &(vpc->routeTables[i]);
            found = TRUE;
        }
    }
    return (result);
}

//!
//! Searches and returns a pointer to the VPC subnet data structure given its name in the argument.
//!
//! @param[in] vpc a pointer to the vpc gni data structure
//! @param[in] vpcsubnetName name of the subnet of interest
//!
//! @return pointer to the gni vpcsubnet of interest if found. NULL otherwise
//!
//! @see
//!
//! @pre
//!     gni data structure is assumed to be populated.
//!
//! @post
//!
//! @note
//!
gni_vpcsubnet *gni_vpc_get_vpcsubnet(gni_vpc *vpc, const char *vpcsubnetName) {
    int i = 0;
    boolean found = FALSE;
    gni_vpcsubnet *result = NULL;
    for (i = 0; i < vpc->max_subnets && !found; i++) {
        if (strcmp(vpcsubnetName, vpc->subnets[i].name) == 0) {
            result = &(vpc->subnets[i]);
            found = TRUE;
        }
    }
    return (result);
}

//!
//! Searches and returns an array of pointers to gni_instance data structures (holding
//! interface information) that are associated with the given VPC.
//!
//! @param[in]  gni a pointer to the global network information structure
//! @param[in]  vpc a pointer to the vpc gni data structure of interest
//! @param[out] out_interfaces a list of pointers to interfaces of interest
//! @param[out] max_out_interfaces number of interfaces found
//!
//! @return 0 if the search is successfully executed - 0 interfaces found is still
//!         a successful search. 1 otherwise. 
//!
//! @see
//!
//! @pre
//!     gni data structure is assumed to be populated.
//!     out_interfaces should be free of memory allocations.
//!
//! @post
//!     memory allocated to hold the resulting list of interfaces should be released
//!     by the caller.
//!
//! @note
//!
int gni_vpc_get_interfaces(globalNetworkInfo *gni, gni_vpc *vpc, gni_instance ***out_interfaces, int *max_out_interfaces) {
    gni_instance **result = NULL;
    int max_result = 0;
    int i = 0;

    if (!gni || !vpc || !out_interfaces || !max_out_interfaces) {
        LOGWARN("Invalid argument: failed to get vpc interfaces.\n");
        return (1);
    }
    LOGTRACE("Searching VPC interfaces.\n");
    for (i = 0; i < gni->max_ifs; i++) {
        if (strcmp(gni->ifs[i]->vpc, vpc->name)) {
            LOGTRACE("%s in %s: N\n", gni->ifs[i]->name, vpc->name);
        } else {
            LOGTRACE("%s in %s: Y\n", gni->ifs[i]->name, vpc->name);
            result = EUCA_REALLOC_C(result, max_result + 1, sizeof (gni_instance *));
            result[max_result] = gni->ifs[i];
            max_result++;
        }
    }
    *out_interfaces = result;
    *max_out_interfaces = max_result;
    return (0);
}

//!
//! Searches and returns an array of pointers to gni_instance data structures (holding
//! interface information) that are associated with the given VPC subnet.
//!
//! @param[in]  gni a pointer to the global network information structure
//! @param[in]  vpcsubnet a pointer to the vpcsubnet gni data structure of interest
//! @param[in]  vpcinterfaces a list of pointers to interfaces to search
//! @param[in]  max_vpcinterfaces number of interfaces found
//! @param[out] out_interfaces a list of pointers to interfaces of interest
//! @param[out] max_out_interfaces number of interfaces found
//!
//! @return 0 if the search is successfully executed - 0 interfaces found is still
//!         a successful search. 1 otherwise. 
//!
//! @see
//!
//! @pre
//!     gni data structure is assumed to be populated.
//!     out_interfaces should be free of memory allocations.
//!
//! @post
//!     memory allocated to hold the resulting list of interfaces should be released
//!     by the caller.
//!
//! @note
//!
int gni_vpcsubnet_get_interfaces(globalNetworkInfo *gni, gni_vpcsubnet *vpcsubnet,
        gni_instance **vpcinterfaces, int max_vpcinterfaces, gni_instance ***out_interfaces, int *max_out_interfaces) {
    gni_instance **result = NULL;
    int max_result = 0;
    int i = 0;

    if (!gni || !vpcsubnet || !vpcinterfaces || !out_interfaces || !max_out_interfaces) {
        if (max_vpcinterfaces == 0) {
            return (0);
        }
        LOGWARN("Invalid argument: failed to get subnet interfaces.\n");
        return (1);
    }

    LOGTRACE("Searching VPC subnet interfaces.\n");
    for (i = 0; i < max_vpcinterfaces; i++) {
        if (strcmp(vpcinterfaces[i]->subnet, vpcsubnet->name)) {
            LOGTRACE("%s in %s: N\n", vpcinterfaces[i]->name, vpcsubnet->name);
        } else {
            LOGTRACE("%s in %s: Y\n", vpcinterfaces[i]->name, vpcsubnet->name);
            result = EUCA_REALLOC_C(result, max_result + 1, sizeof (gni_instance *));
            result[max_result] = vpcinterfaces[i];
            max_result++;
        }
    }
    *out_interfaces = result;
    *max_out_interfaces = max_result;
    return (0);
}

//!
//! Looks up for the cluster for which we are assigned within a configured cluster list. We can
//! be the cluster itself or one of its node.
//!
//! @param[in] gni a pointer to the global network information structure
//! @param[out] outclusterptr a pointer to the associated cluster structure pointer
//!
//! @return 0 if a matching cluster structure is found or 1 if not found or a failure occured
//!
//! @see gni_is_self()
//!
//! @pre
//!
//! @post
//!     On success the value pointed by outclusterptr is valid. On failure, this value
//!     is non-deterministic.
//!
//! @note
//!
int gni_find_self_cluster(globalNetworkInfo * gni, gni_cluster ** outclusterptr)
{
    int i, j;
    char *strptra = NULL;

    if (!gni || !outclusterptr) {
        LOGWARN("invalid argument: cannot find cluster from NULL\n");
        return (1);
    }

    *outclusterptr = NULL;

    for (i = 0; i < gni->max_clusters; i++) {
        // check to see if local host is the enabled cluster controller
        strptra = hex2dot(gni->clusters[i].enabledCCIp);
        if (strptra) {
            if (!gni_is_self_getifaddrs(strptra)) {
                EUCA_FREE(strptra);
                *outclusterptr = &(gni->clusters[i]);
                return (0);
            }
            EUCA_FREE(strptra);
        }
        // otherwise, check to see if local host is a node in the cluster
        for (j = 0; j < gni->clusters[i].max_nodes; j++) {
            //      if (!strcmp(gni->clusters[i].nodes[j].name, outnodeptr->name)) {
            if (!gni_is_self_getifaddrs(gni->clusters[i].nodes[j].name)) {
                *outclusterptr = &(gni->clusters[i]);
                return (0);
            }
        }
    }
    return (1);
}

//!
//! Looks up for the cluster for which we are assigned within a configured cluster list. We can
//! be the cluster itself or one of its node.
//!
//! @param[in] gni a pointer to the global network information structure
//! @param[in] psGroupId a pointer to a constant string containing the Security-Group ID we're looking for
//! @param[out] pSecGroup a pointer to the associated security group structure pointer
//!
//! @return 0 if a matching security group structure is found or 1 if not found or a failure occured
//!
//! @see
//!
//! @pre
//!     All the provided parameter must be valid and non-NULL.
//!
//! @post
//!     On success the value pointed by pSecGroup is valid. On failure, this value
//!     is non-deterministic.
//!
//! @note
//!
int gni_find_secgroup(globalNetworkInfo * gni, const char *psGroupId, gni_secgroup ** pSecGroup)
{
    int i = 0;

    if (!gni || !psGroupId || !pSecGroup) {
        LOGWARN("invalid argument: cannot find secgroup from NULL\n");
        return (1);
    }
    // Initialize to NULL
    (*pSecGroup) = NULL;

    // Go through our security group list and look for that group
    for (i = 0; i < gni->max_secgroups; i++) {
        if (!strcmp(psGroupId, gni->secgroups[i].name)) {
            (*pSecGroup) = &(gni->secgroups[i]);
            return (0);
        }
    }
    return (1);
}

/**
 * Searches for the given instance name and returns the associated gni_instance structure.
 * @param gni [in] pointer to the global network information structure.
 * @param psInstanceId [in] the ID string of the instance of interest.
 * @param pInstance [out] pointer to the gni_instance structure of interest.
 * @return 0 on success. Positive integer otherwise.
 */
int gni_find_instance(globalNetworkInfo * gni, const char *psInstanceId, gni_instance ** pInstance) {
    if (!gni || !psInstanceId || !pInstance) {
        LOGWARN("invalid argument: cannot find instance from NULL\n");
        return (1);
    }
    // Initialize to NULL
    (*pInstance) = NULL;

    LOGTRACE("attempting search for instance id %s in gni\n", psInstanceId);

    // binary search - instances should be already sorted in GNI (uncomment below if not sorted)
/*
    if (gni->sorted_instances == FALSE) {
        qsort(gni->instances, gni->max_instances,
                sizeof (gni_instance *), compare_gni_instance_name);
        gni->sorted_instances = TRUE;
    }
*/
    gni_instance inst;
    snprintf(inst.name, INTERFACE_ID_LEN, "%s", psInstanceId);
    gni_instance *pinst = &inst;
    gni_instance **res = (gni_instance **) bsearch(&pinst,
            gni->instances, gni->max_instances,
            sizeof (gni_instance *), compare_gni_instance_name);
    if (res) {
        *pInstance = *res;
        return (0);
    }

    // Go through our instance list and look for that instance
/*
    for (int i = 0; i < gni->max_instances; i++) {
        LOGEXTREME("attempting match between %s and %s\n", psInstanceId, gni->instances[i]->name);
        if (!strcmp(psInstanceId, gni->instances[i]->name)) {
            (*pInstance) = gni->instances[i];
            return (0);
        }
    }
*/

    return (1);
}

//!
//! Searches through the list of network interfaces in the gni  and returns all
//! non-primary interfaces for a given instance
//!
//! @param[in] gni a pointer to the global network information structure
//! @param[in] psInstanceId a pointer to instance ID identifier (i-XXXXXXX)
//! @param[out] pAInstances an array of network interface pointers
//! @param[out] size a pointer to the size of the array
//!
//! @return 0 if lookup is successful or 1 if a failure occurred
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_find_secondary_interfaces(globalNetworkInfo * gni, const char *psInstanceId, gni_instance * pAInstances[], int *size) {
    if (!size || !gni || !psInstanceId || !sizeof (pAInstances)) {
        LOGERROR("invalid argument: cannot find secondary interfaces for NULL\n");
        return EUCA_ERROR;
    }

    *size = 0;

    LOGTRACE("attempting search for interfaces for instance id %s in gni\n", psInstanceId);

    gni_instance *inst = NULL;
    int rc = 0;
    rc = gni_find_instance(gni, psInstanceId, &inst);
    if (!rc) {
        for (int i = 0; i < inst->max_interfaces; i++) {
            if (inst->interfaces[i]->deviceidx != 0) {
                pAInstances[*size] = inst->interfaces[i];
                (*size)++;
            }
        }
    }
/*
    for (int i = 0; i < gni->max_ifs; i++) {
        LOGEXTREME("attempting match between %s and %s\n", psInstanceId, gni->ifs[i]->instance_name.name);
        if (!strcmp(gni->ifs[i]->instance_name.name, psInstanceId) &&
                strcmp(gni->ifs[i]->name, psInstanceId)) {
            pAInstances[*size] = gni->ifs[i];
            (*size)++;
        }
    }
*/

    return EUCA_OK;
}

//!
//! Looks up through a list of configured node for the one that is associated with
//! this currently running instance.
//!
//! @param[in]  gni a pointer to the global network information structure
//! @param[out] outnodeptr a pointer to the associated node structure pointer
//!
//! @return 0 if a matching node structure is found or 1 if not found or a failure occured
//!
//! @see gni_is_self()
//!
//! @pre
//!
//! @post
//!     On success the value pointed by outnodeptr is valid. On failure, this value
//!     is non-deterministic.
//!
//! @note
//!
int gni_find_self_node(globalNetworkInfo * gni, gni_node ** outnodeptr)
{
    int i, j;

    if (!gni || !outnodeptr) {
        LOGERROR("invalid input\n");
        return (1);
    }

    *outnodeptr = NULL;

    for (i = 0; i < gni->max_clusters; i++) {
        for (j = 0; j < gni->clusters[i].max_nodes; j++) {
            if (!gni_is_self_getifaddrs(gni->clusters[i].nodes[j].name)) {
                *outnodeptr = &(gni->clusters[i].nodes[j]);
                return (0);
            }
        }
    }

    return (1);
}

//!
//! Validates if the given test_ip is a local IP address on this system
//!
//! @param[in] test_ip a string containing the IP to validate
//!
//! @return 0 if the test_ip is a local IP or 1 on failure or if not found locally
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_is_self(const char *test_ip)
{
    DIR *DH = NULL;
    struct dirent dent, *result = NULL;
    int max, rc, i;
    u32 *outips = NULL, *outnms = NULL;
    char *strptra = NULL;

    if (!test_ip) {
        LOGERROR("invalid input\n");
        return (1);
    }

    DH = opendir("/sys/class/net/");
    if (!DH) {
        LOGERROR("could not open directory /sys/class/net/ for read: check permissions\n");
        return (1);
    }

    rc = readdir_r(DH, &dent, &result);
    while (!rc && result) {
        if (strcmp(dent.d_name, ".") && strcmp(dent.d_name, "..")) {
            rc = getdevinfo(dent.d_name, &outips, &outnms, &max);
            for (i = 0; i < max; i++) {
                strptra = hex2dot(outips[i]);
                if (strptra) {
                    if (!strcmp(strptra, test_ip)) {
                        EUCA_FREE(strptra);
                        EUCA_FREE(outips);
                        EUCA_FREE(outnms);
                        closedir(DH);
                        return (0);
                    }
                    EUCA_FREE(strptra);
                }
            }
            EUCA_FREE(outips);
            EUCA_FREE(outnms);
        }
        rc = readdir_r(DH, &dent, &result);
    }
    closedir(DH);

    return (1);
}

//!
//! Validates if the given test_ip is a local IP address on this system. This function
//! is based on getifaddrs() call.
//! @param[in] test_ip a string containing the IP to validate
//!
//! @return 0 if the test_ip is a local IP or 1 on failure or if not found locally
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_is_self_getifaddrs(const char *test_ip) {
    struct ifaddrs *ifas = NULL;
    struct ifaddrs *elem = NULL;
    int found = 0;
    int rc = 0;

    if (!test_ip) {
        LOGERROR("invalid input: cannot check NULL IP\n");
        return (1);
    }

    rc = getifaddrs(&ifas);
    if (rc) {
        LOGERROR("unable to retrieve system IPv4 addresses.\n");
        freeifaddrs(ifas);
        return (1);
    }

    elem = ifas;
    while (elem && !found) {
        if (elem->ifa_addr && elem->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *saddr = (struct sockaddr_in *) elem->ifa_addr;
            if (!strcmp(test_ip, inet_ntoa(saddr->sin_addr))) {
                found = 1;
            }
        }
        elem = elem->ifa_next;
    }
    freeifaddrs(ifas);
    if (found) {
        return (0);
    }
    return (1);
}

//!
//! TODO: Function description.
//!
//! @param[in]  gni a pointer to the global network information structure
//! @param[in]  cluster_names
//! @param[in]  max_cluster_names
//! @param[out] out_cluster_names
//! @param[out] out_max_cluster_names
//! @param[out] out_clusters
//! @param[out] out_max_clusters
//!
//! @return
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_cloud_get_clusters(globalNetworkInfo * gni, char **cluster_names, int max_cluster_names, char ***out_cluster_names, int *out_max_cluster_names, gni_cluster ** out_clusters,
        int *out_max_clusters) {
    int ret = 0, getall = 0, i = 0, j = 0, retcount = 0, do_outnames = 0, do_outstructs = 0;
    gni_cluster *ret_clusters = NULL;
    char **ret_cluster_names = NULL;

    if (!cluster_names || max_cluster_names <= 0) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (out_cluster_names && out_max_cluster_names) {
        do_outnames = 1;
    }
    if (out_clusters && out_max_clusters) {
        do_outstructs = 1;
    }

    if (!do_outnames && !do_outstructs) {
        LOGEXTREME("nothing to do, both output variables are NULL\n");
        return (0);
    }

    if (do_outnames) {
        *out_cluster_names = NULL;
        *out_max_cluster_names = 0;
    }
    if (do_outstructs) {
        *out_clusters = NULL;
        *out_max_clusters = 0;
    }

    if (!strcmp(cluster_names[0], "*")) {
        getall = 1;
        if (do_outnames)
            *out_cluster_names = EUCA_ZALLOC_C(gni->max_clusters, sizeof (char *));
        if (do_outstructs)
            *out_clusters = EUCA_ZALLOC_C(gni->max_clusters, sizeof (gni_cluster));
    }

    if (do_outnames)
        ret_cluster_names = *out_cluster_names;
    if (do_outstructs)
        ret_clusters = *out_clusters;

    retcount = 0;
    for (i = 0; i < gni->max_clusters; i++) {
        if (getall) {
            if (do_outnames)
                ret_cluster_names[i] = strdup(gni->clusters[i].name);
            if (do_outstructs)
                memcpy(&(ret_clusters[i]), &(gni->clusters[i]), sizeof (gni_cluster));
            retcount++;
        } else {
            for (j = 0; j < max_cluster_names; j++) {
                if (!strcmp(cluster_names[j], gni->clusters[i].name)) {
                    if (do_outnames) {
                        *out_cluster_names = EUCA_REALLOC_C(*out_cluster_names, (retcount + 1), sizeof (char *));
                        ret_cluster_names = *out_cluster_names;
                        ret_cluster_names[retcount] = strdup(gni->clusters[i].name);
                    }
                    if (do_outstructs) {
                        *out_clusters = EUCA_REALLOC_C(*out_clusters, (retcount + 1), sizeof (gni_cluster));
                        ret_clusters = *out_clusters;
                        memcpy(&(ret_clusters[retcount]), &(gni->clusters[i]), sizeof (gni_cluster));
                    }
                    retcount++;
                }
            }
        }
    }
    if (do_outnames)
        *out_max_cluster_names = retcount;
    if (do_outstructs)
        *out_max_clusters = retcount;

    return (ret);
}

//!
//! Retrives the list of security groups configured under a cloud
//!
//! @param[in]  pGni a pointer to our global network view structure
//! @param[in]  psSecGroupNames a string pointer to the name of groups we're looking for
//! @param[in]  nbSecGroupNames the number of groups in the psSecGroupNames list
//! @param[out] psOutSecGroupNames a string pointer that will contain the list of group names we found (if non NULL)
//! @param[out] pOutNbSecGroupNames a pointer to the number of groups that matched in the psOutSecGroupNames list
//! @param[out] pOutSecGroups a pointer to the list of security group structures that match what we're looking for
//! @param[out] pOutNbSecGroups a pointer to the number of structures in the psOutSecGroups list
//!
//! @return 0 on success or 1 if any failure occured
//!
//! @see
//!
//! @pre  TODO:
//!
//! @post TODO:
//!
//! @note
//!
int gni_cloud_get_secgroups(globalNetworkInfo * pGni, char **psSecGroupNames, int nbSecGroupNames, char ***psOutSecGroupNames, int *pOutNbSecGroupNames,
        gni_secgroup ** pOutSecGroups, int *pOutNbSecGroups) {
    int ret = 0;
    int i = 0;
    int x = 0;
    int retCount = 0;
    char **psRetSecGroupNames = NULL;
    boolean getAll = FALSE;
    boolean doOutNames = FALSE;
    boolean doOutStructs = FALSE;
    gni_secgroup *pRetSecGroup = NULL;

    // Make sure our GNI pointer isn't NULL
    if (!pGni) {
        LOGERROR("invalid input\n");
        return (1);
    }
    // Are we building the name list?
    if (psOutSecGroupNames && pOutNbSecGroupNames) {
        doOutNames = TRUE;
        *psOutSecGroupNames = NULL;
        *pOutNbSecGroupNames = 0;
    }
    // Are we building the structure list?
    if (pOutSecGroups && pOutNbSecGroups) {
        doOutStructs = TRUE;
        *pOutSecGroups = NULL;
        *pOutNbSecGroups = 0;
    }
    // Are we doing anything?
    if (!doOutNames && !doOutStructs) {
        LOGEXTREME("nothing to do, both output variables are NULL\n");
        return (0);
    }
    // Do we have any groups?
    if (pGni->max_secgroups == 0)
        return (0);

    // If we do it all, allocate the memory now
    if (psSecGroupNames == NULL || !strcmp(psSecGroupNames[0], "*")) {
        getAll = TRUE;
        if (doOutNames)
            *psOutSecGroupNames = EUCA_ZALLOC_C(pGni->max_secgroups, sizeof (char *));
        if (doOutStructs)
            *pOutSecGroups = EUCA_ZALLOC_C(pGni->max_secgroups, sizeof (gni_secgroup));
    }
    // Setup our returning name list pointer
    if (doOutNames)
        psRetSecGroupNames = *psOutSecGroupNames;

    // Setup our returning structure list pointer
    if (doOutStructs)
        pRetSecGroup = *pOutSecGroups;

    // Go through the group list
    for (i = 0, retCount = 0; i < pGni->max_secgroups; i++) {
        if (getAll) {
            if (doOutNames)
                psRetSecGroupNames[retCount] = strdup(pGni->secgroups[i].name);

            if (doOutStructs)
                memcpy(&(pRetSecGroup[retCount]), &(pGni->secgroups[i]), sizeof (gni_secgroup));
            retCount++;
        } else {
            if (!strcmp(psSecGroupNames[x], pGni->secgroups[i].name)) {
                if (doOutNames) {
                    *psOutSecGroupNames = EUCA_REALLOC_C(*psOutSecGroupNames, (retCount + 1), sizeof (char *));
                    psRetSecGroupNames = *psOutSecGroupNames;
                    psRetSecGroupNames[retCount] = strdup(pGni->secgroups[i].name);
                }

                if (doOutStructs) {
                    *pOutSecGroups = EUCA_REALLOC_C(*pOutSecGroups, (retCount + 1), sizeof (gni_instance));
                    pRetSecGroup = *pOutSecGroups;
                    memcpy(&(pRetSecGroup[retCount]), &(pGni->secgroups[i]), sizeof (gni_secgroup));
                }
                retCount++;
            }
        }
    }

    if (doOutNames)
        *pOutNbSecGroupNames = retCount;

    if (doOutStructs)
        *pOutNbSecGroups = retCount;

    return (ret);
}

//!
//! TODO: Function description.
//!
//! @param[in]  gni a pointer to the global network information structure
//! @param[in]  cluster
//! @param[in]  node_names
//! @param[in]  max_node_names
//! @param[out] out_node_names
//! @param[out] out_max_node_names
//! @param[out] out_nodes
//! @param[out] out_max_nodes
//!
//! @return
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_cluster_get_nodes(globalNetworkInfo * gni, gni_cluster * cluster, char **node_names, int max_node_names, char ***out_node_names, int *out_max_node_names,
        gni_node ** out_nodes, int *out_max_nodes) {
    int ret = 0, rc = 0, getall = 0, i = 0, j = 0, retcount = 0, do_outnames = 0, do_outstructs = 0, out_max_clusters = 0;
    gni_node *ret_nodes = NULL;
    gni_cluster *out_clusters = NULL;
    char **ret_node_names = NULL, **cluster_names = NULL;

    if (!gni) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (out_node_names && out_max_node_names) {
        do_outnames = 1;
    }
    if (out_nodes && out_max_nodes) {
        do_outstructs = 1;
    }

    if (!do_outnames && !do_outstructs) {
        LOGEXTREME("nothing to do, both output variables are NULL\n");
        return (0);
    }

    if (do_outnames) {
        *out_node_names = NULL;
        *out_max_node_names = 0;
    }
    if (do_outstructs) {
        *out_nodes = NULL;
        *out_max_nodes = 0;
    }

    cluster_names = EUCA_ZALLOC_C(1, sizeof (char *));
    cluster_names[0] = cluster->name;
    rc = gni_cloud_get_clusters(gni, cluster_names, 1, NULL, NULL, &out_clusters, &out_max_clusters);
    if (rc || out_max_clusters <= 0) {
        LOGWARN("nothing to do, no matching cluster named '%s' found\n", cluster->name);
        EUCA_FREE(cluster_names);
        EUCA_FREE(out_clusters);
        return (0);
    }

    if ((node_names == NULL) || !strcmp(node_names[0], "*")) {
        getall = 1;
        if (do_outnames)
            *out_node_names = EUCA_ZALLOC_C(cluster->max_nodes, sizeof (char *));
        if (do_outstructs)
            *out_nodes = EUCA_ZALLOC_C(cluster->max_nodes, sizeof (gni_node));
    }

    if (do_outnames)
        ret_node_names = *out_node_names;

    if (do_outstructs)
        ret_nodes = *out_nodes;

    retcount = 0;
    for (i = 0; i < cluster->max_nodes; i++) {
        if (getall) {
            if (do_outnames)
                ret_node_names[i] = strdup(out_clusters[0].nodes[i].name);

            if (do_outstructs)
                memcpy(&(ret_nodes[i]), &(out_clusters[0].nodes[i]), sizeof (gni_node));

            retcount++;
        } else {
            for (j = 0; j < max_node_names; j++) {
                if (!strcmp(node_names[j], out_clusters[0].nodes[i].name)) {
                    if (do_outnames) {
                        *out_node_names = EUCA_REALLOC_C(*out_node_names, (retcount + 1), sizeof (char *));
                        ret_node_names = *out_node_names;
                        ret_node_names[retcount] = strdup(out_clusters[0].nodes[i].name);
                    }
                    if (do_outstructs) {
                        *out_nodes = EUCA_REALLOC_C(*out_nodes, (retcount + 1), sizeof (gni_node));
                        ret_nodes = *out_nodes;
                        memcpy(&(ret_nodes[retcount]), &(out_clusters[0].nodes[i]), sizeof (gni_node));
                    }
                    retcount++;
                }
            }
        }
    }

    if (do_outnames)
        *out_max_node_names = retcount;

    if (do_outstructs)
        *out_max_nodes = retcount;

    EUCA_FREE(cluster_names);
    EUCA_FREE(out_clusters);
    return (ret);
}

//!
//! TODO: Function description.
//!
//! @param[in]  pGni a pointer to the global network information structure
//! @param[in]  pCluster
//! @param[in]  psInstanceNames
//! @param[in]  nbInstanceNames
//! @param[out] psOutInstanceNames
//! @param[out] pOutNbInstanceNames
//! @param[out] pOutInstances
//! @param[out] pOutNbInstances
//!
//! @return
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_cluster_get_instances(globalNetworkInfo * pGni, gni_cluster * pCluster, char **psInstanceNames, int nbInstanceNames,
        char ***psOutInstanceNames, int *pOutNbInstanceNames, gni_instance ** pOutInstances, int *pOutNbInstances) {
    int ret = 0;
    int i = 0;
    int k = 0;
    int x = 0;
    int y = 0;
    int retCount = 0;
    int nbInstances = 0;
    char **psRetInstanceNames = NULL;
    boolean getAll = FALSE;
    boolean doOutNames = FALSE;
    boolean doOutStructs = FALSE;
    gni_instance *pRetInstances = NULL;

    if (!pGni || !pCluster) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (psOutInstanceNames && pOutNbInstanceNames) {
        doOutNames = TRUE;
    }
    if (pOutInstances && pOutNbInstances) {
        doOutStructs = TRUE;
    }

    if (!doOutNames && !doOutStructs) {
        LOGEXTREME("nothing to do, both output variables are NULL\n");
        return (0);
    }

    if (doOutNames) {
        *psOutInstanceNames = NULL;
        *pOutNbInstanceNames = 0;
    }
    if (doOutStructs) {
        *pOutInstances = NULL;
        *pOutNbInstances = 0;
    }
    // Do we have any nodes?
    if (pCluster->max_nodes == 0)
        return (0);

    for (i = 0, nbInstances = 0; i < pCluster->max_nodes; i++) {
        nbInstances += pCluster->nodes[i].max_instance_names;
    }

    // Do we have any instances?
    if (nbInstances == 0)
        return (0);

    if (psInstanceNames == NULL || !strcmp(psInstanceNames[0], "*")) {
        getAll = TRUE;
        if (doOutNames)
            *psOutInstanceNames = EUCA_ZALLOC_C(nbInstances, sizeof (char *));
        if (doOutStructs)
            *pOutInstances = EUCA_ZALLOC_C(nbInstances, sizeof (gni_instance));
    }

    if (doOutNames)
        psRetInstanceNames = *psOutInstanceNames;

    if (doOutStructs)
        pRetInstances = *pOutInstances;

    for (i = 0, retCount = 0; i < pCluster->max_nodes; i++) {
        for (k = 0; k < pCluster->nodes[i].max_instance_names; k++) {
            if (getAll) {
                if (doOutNames)
                    psRetInstanceNames[retCount] = strdup(pCluster->nodes[i].instance_names[k].name);

                if (doOutStructs) {
                    for (x = 0; x < pGni->max_instances; x++) {
                        if (!strcmp(pGni->instances[x]->name, pCluster->nodes[i].instance_names[k].name)) {
                            memcpy(&(pRetInstances[retCount]), pGni->instances[x], sizeof (gni_instance));
                            break;
                        }
                    }
                }
                retCount++;
            } else {
                for (x = 0; x < nbInstanceNames; x++) {
                    if (!strcmp(psInstanceNames[x], pCluster->nodes[i].instance_names[k].name)) {
                        if (doOutNames) {
                            *psOutInstanceNames = EUCA_REALLOC_C(*psOutInstanceNames, (retCount + 1), sizeof (char *));
                            psRetInstanceNames = *psOutInstanceNames;
                            psRetInstanceNames[retCount] = strdup(pCluster->nodes[i].instance_names[k].name);
                        }

                        if (doOutStructs) {
                            for (y = 0; y < pGni->max_instances; y++) {
                                if (!strcmp(pGni->instances[y]->name, pCluster->nodes[i].instance_names[k].name)) {
                                    *pOutInstances = EUCA_REALLOC_C(*pOutInstances, (retCount + 1), sizeof (gni_instance));
                                    pRetInstances = *pOutInstances;
                                    memcpy(&(pRetInstances[retCount]), pGni->instances[y], sizeof (gni_instance));
                                    break;
                                }
                            }
                        }
                        retCount++;
                    }
                }
            }
        }
    }

    if (doOutNames)
        *pOutNbInstanceNames = retCount;

    if (doOutStructs)
        *pOutNbInstances = retCount;

    return (ret);
}

//!
//! Retrives the list of security groups configured and active on a given cluster
//!
//! @param[in]  pGni a pointer to our global network view structure
//! @param[in]  pCluster a pointer to the cluster we're building the security group list for
//! @param[in]  psSecGroupNames a string pointer to the name of groups we're looking for
//! @param[in]  nbSecGroupNames the number of groups in the psSecGroupNames list
//! @param[out] psOutSecGroupNames a string pointer that will contain the list of group names we found (if non NULL)
//! @param[out] pOutNbSecGroupNames a pointer to the number of groups that matched in the psOutSecGroupNames list
//! @param[out] pOutSecGroups a pointer to the list of security group structures that match what we're looking for
//! @param[out] pOutNbSecGroups a pointer to the number of structures in the psOutSecGroups list
//!
//! @return 0 on success or 1 if any failure occured
//!
//! @see
//!
//! @pre  TODO:
//!
//! @post TODO:
//!
//! @note
//!
int gni_cluster_get_secgroup(globalNetworkInfo * pGni, gni_cluster * pCluster, char **psSecGroupNames, int nbSecGroupNames, char ***psOutSecGroupNames, int *pOutNbSecGroupNames,
        gni_secgroup ** pOutSecGroups, int *pOutNbSecGroups) {
    int ret = 0;
    int i = 0;
    int k = 0;
    int x = 0;
    int retCount = 0;
    int nbInstances = 0;
    char **psRetSecGroupNames = NULL;
    boolean found = FALSE;
    boolean getAll = FALSE;
    boolean doOutNames = FALSE;
    boolean doOutStructs = FALSE;
    gni_instance *pInstances = NULL;
    gni_secgroup *pRetSecGroup = NULL;

    // Make sure our GNI and Cluster pointers are valid
    if (!pGni || !pCluster) {
        LOGERROR("invalid input\n");
        return (1);
    }
    // We will need to get the instances that are running under this cluster
    if (gni_cluster_get_instances(pGni, pCluster, NULL, 0, NULL, NULL, &pInstances, &nbInstances)) {
        LOGERROR("Failed to retrieve instances for cluster '%s'\n", pCluster->name);
        return (1);
    }
    // Are we building the name list?
    if (psOutSecGroupNames && pOutNbSecGroupNames) {
        doOutNames = TRUE;
        *psOutSecGroupNames = NULL;
        *pOutNbSecGroupNames = 0;
    }
    // Are we building the structure list?
    if (pOutSecGroups && pOutNbSecGroups) {
        doOutStructs = TRUE;
        *pOutSecGroups = NULL;
        *pOutNbSecGroups = 0;
    }
    // Are we doing anything?
    if (!doOutNames && !doOutStructs) {
        LOGEXTREME("nothing to do, both output variables are NULL\n");
        EUCA_FREE(pInstances);
        return (0);
    }
    // Do we have any instances?
    if (nbInstances == 0) {
        EUCA_FREE(pInstances);
        return (0);
    }

    // Do we have any groups?
    if (pGni->max_secgroups == 0) {
        EUCA_FREE(pInstances);
        return (0);
    }
    // Allocate memory for all the groups if there is no search criteria
    if ((psSecGroupNames == NULL) || !strcmp(psSecGroupNames[0], "*")) {
        getAll = TRUE;
        if (doOutNames)
            *psOutSecGroupNames = EUCA_ZALLOC_C(pGni->max_secgroups, sizeof (char *));

        if (doOutStructs)
            *pOutSecGroups = EUCA_ZALLOC_C(pGni->max_secgroups, sizeof (gni_secgroup));
    }
    // Setup our returning name pointer
    if (doOutNames)
        psRetSecGroupNames = *psOutSecGroupNames;

    // Setup our returning structure pointer
    if (doOutStructs)
        pRetSecGroup = *pOutSecGroups;

    // Scan all our groups
    for (i = 0, retCount = 0; i < pGni->max_secgroups; i++) {
        if (getAll) {
            // Check if this we have any instance using this group
            for (k = 0, found = FALSE; ((k < nbInstances) && !found); k++) {
                for (x = 0; ((x < pInstances[k].max_secgroup_names) && !found); x++) {
                    if (!strcmp(pGni->secgroups[i].name, pInstances[k].secgroup_names[x].name)) {
                        found = TRUE;
                    }
                }
            }

            // If we have any instance using this group, then copy it
            if (found) {
                if (doOutNames)
                    psRetSecGroupNames[retCount] = strdup(pGni->secgroups[i].name);

                if (doOutStructs)
                    memcpy(&(pRetSecGroup[retCount]), &(pGni->secgroups[i]), sizeof (gni_secgroup));
                retCount++;
            }
        } else {
            if (!strcmp(psSecGroupNames[x], pGni->secgroups[i].name)) {
                // Check if this we have any instance using this group
                for (k = 0, found = FALSE; ((k < nbInstances) && !found); k++) {
                    for (x = 0; ((x < pInstances[k].max_secgroup_names) && !found); x++) {
                        if (!strcmp(pGni->secgroups[i].name, pInstances[k].secgroup_names[x].name)) {
                            found = TRUE;
                        }
                    }
                }

                // If we have any instance using this group, then copy it
                if (found) {
                    if (doOutNames) {
                        *psOutSecGroupNames = EUCA_REALLOC_C(*psOutSecGroupNames, (retCount + 1), sizeof (char *));
                        psRetSecGroupNames = *psOutSecGroupNames;
                        psRetSecGroupNames[retCount] = strdup(pGni->secgroups[i].name);
                    }

                    if (doOutStructs) {
                        *pOutSecGroups = EUCA_REALLOC_C(*pOutSecGroups, (retCount + 1), sizeof (gni_instance));
                        pRetSecGroup = *pOutSecGroups;
                        memcpy(&(pRetSecGroup[retCount]), &(pGni->secgroups[i]), sizeof (gni_secgroup));
                    }
                    retCount++;
                }
            }
        }
    }

    if (doOutNames)
        *pOutNbSecGroupNames = retCount;

    if (doOutStructs)
        *pOutNbSecGroups = retCount;

    EUCA_FREE(pInstances);
    return (ret);
}

//!
//! TODO: Function description.
//!
//! @param[in]  gni a pointer to the global network information structure
//! @param[in]  node
//! @param[in]  instance_names
//! @param[in]  max_instance_names
//! @param[out] out_instance_names
//! @param[out] out_max_instance_names
//! @param[out] out_instances
//! @param[out] out_max_instances
//!
//! @return
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_node_get_instances(globalNetworkInfo * gni, gni_node * node, char **instance_names, int max_instance_names, char ***out_instance_names, int *out_max_instance_names,
        gni_instance ** out_instances, int *out_max_instances) {
    int ret = 0, getall = 0, i = 0, j = 0, k = 0, retcount = 0, do_outnames = 0, do_outstructs = 0;
    gni_instance *ret_instances = NULL;
    char **ret_instance_names = NULL;

    if (!gni) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (out_instance_names && out_max_instance_names) {
        do_outnames = 1;
    }
    if (out_instances && out_max_instances) {
        do_outstructs = 1;
    }

    if (!do_outnames && !do_outstructs) {
        LOGEXTREME("nothing to do, both output variables are NULL\n");
        return (0);
    }

    if (do_outnames) {
        *out_instance_names = NULL;
        *out_max_instance_names = 0;
    }
    if (do_outstructs) {
        *out_instances = NULL;
        *out_max_instances = 0;
    }

    if (instance_names == NULL || !strcmp(instance_names[0], "*")) {
        getall = 1;
        if (do_outnames)
            *out_instance_names = EUCA_ZALLOC_C(node->max_instance_names, sizeof (char *));
        if (do_outstructs)
            *out_instances = EUCA_ZALLOC_C(node->max_instance_names, sizeof (gni_instance));
    }

    if (do_outnames)
        ret_instance_names = *out_instance_names;
    if (do_outstructs)
        ret_instances = *out_instances;

    retcount = 0;
    for (i = 0; i < node->max_instance_names; i++) {
        if (getall) {
            if (do_outnames)
                ret_instance_names[i] = strdup(node->instance_names[i].name);
            if (do_outstructs) {
                for (k = 0; k < gni->max_instances; k++) {
                    if (!strcmp(gni->instances[k]->name, node->instance_names[i].name)) {
                        memcpy(&(ret_instances[i]), gni->instances[k], sizeof (gni_instance));
                        break;
                    }
                }
            }
            retcount++;
        } else {
            for (j = 0; j < max_instance_names; j++) {
                if (!strcmp(instance_names[j], node->instance_names[i].name)) {
                    if (do_outnames) {
                        *out_instance_names = EUCA_REALLOC_C(*out_instance_names, (retcount + 1), sizeof (char *));
                        ret_instance_names = *out_instance_names;
                        ret_instance_names[retcount] = strdup(node->instance_names[i].name);
                    }
                    if (do_outstructs) {
                        for (k = 0; k < gni->max_instances; k++) {
                            if (!strcmp(gni->instances[k]->name, node->instance_names[i].name)) {
                                *out_instances = EUCA_REALLOC_C(*out_instances, (retcount + 1), sizeof (gni_instance));
                                ret_instances = *out_instances;
                                memcpy(&(ret_instances[retcount]), gni->instances[k], sizeof (gni_instance));
                                break;
                            }
                        }
                    }
                    retcount++;
                }
            }
        }
    }
    if (do_outnames)
        *out_max_instance_names = retcount;
    if (do_outstructs)
        *out_max_instances = retcount;

    return (ret);
}

//!
//! Retrives the list of security groups configured and active on a given cluster
//!
//! @param[in]  pGni a pointer to our global network view structure
//! @param[in]  pNode a pointer to the node we're building the security group list for
//! @param[in]  psSecGroupNames a string pointer to the name of groups we're looking for
//! @param[in]  nbSecGroupNames the number of groups in the psSecGroupNames list
//! @param[out] psOutSecGroupNames a string pointer that will contain the list of group names we found (if non NULL)
//! @param[out] pOutNbSecGroupNames a pointer to the number of groups that matched in the psOutSecGroupNames list
//! @param[out] pOutSecGroups a pointer to the list of security group structures that match what we're looking for
//! @param[out] pOutNbSecGroups a pointer to the number of structures in the psOutSecGroups list
//!
//! @return 0 on success or 1 if any failure occured
//!
//! @see
//!
//! @pre  TODO:
//!
//! @post TODO:
//!
//! @note
//!
int gni_node_get_secgroup(globalNetworkInfo * pGni, gni_node * pNode, char **psSecGroupNames, int nbSecGroupNames, char ***psOutSecGroupNames, int *pOutNbSecGroupNames,
        gni_secgroup ** pOutSecGroups, int *pOutNbSecGroups) {
    int ret = 0;
    int i = 0;
    int k = 0;
    int x = 0;
    int retCount = 0;
    int nbInstances = 0;
    char **psRetSecGroupNames = NULL;
    boolean found = FALSE;
    boolean getAll = FALSE;
    boolean doOutNames = FALSE;
    boolean doOutStructs = FALSE;
    gni_instance *pInstances = NULL;
    gni_secgroup *pRetSecGroup = NULL;

    // Make sure our GNI and Cluster pointers are valid
    if (!pGni || !pNode) {
        LOGERROR("invalid input\n");
        return (1);
    }
    // We will need to get the instances that are running under this cluster
    if (gni_node_get_instances(pGni, pNode, NULL, 0, NULL, NULL, &pInstances, &nbInstances)) {
        LOGERROR("Failed to retrieve instances for node '%s'\n", pNode->name);
        return (1);
    }
    // Are we building the name list?
    if (psOutSecGroupNames && pOutNbSecGroupNames) {
        doOutNames = TRUE;
        *psOutSecGroupNames = NULL;
        *pOutNbSecGroupNames = 0;
    }
    // Are we building the structure list?
    if (pOutSecGroups && pOutNbSecGroups) {
        doOutStructs = TRUE;
        *pOutSecGroups = NULL;
        *pOutNbSecGroups = 0;
    }
    // Are we doing anything?
    if (!doOutNames && !doOutStructs) {
        LOGEXTREME("nothing to do, both output variables are NULL\n");
        EUCA_FREE(pInstances);
        return (0);
    }
    // Do we have any instances?
    if (nbInstances == 0) {
        EUCA_FREE(pInstances);
        return (0);
    }
    // Do we have any groups?
    if (pGni->max_secgroups == 0) {
        EUCA_FREE(pInstances);
        return (0);
    }
    // Allocate memory for all the groups if there is no search criteria
    if ((psSecGroupNames == NULL) || !strcmp(psSecGroupNames[0], "*")) {
        getAll = TRUE;
        if (doOutNames)
            *psOutSecGroupNames = EUCA_ZALLOC_C(pGni->max_secgroups, sizeof (char *));

        if (doOutStructs)
            *pOutSecGroups = EUCA_ZALLOC_C(pGni->max_secgroups, sizeof (gni_secgroup));
    }
    // Setup our returning name pointer
    if (doOutNames)
        psRetSecGroupNames = *psOutSecGroupNames;

    // Setup our returning structure pointer
    if (doOutStructs)
        pRetSecGroup = *pOutSecGroups;

    // Scan all our groups
    for (i = 0, retCount = 0; i < pGni->max_secgroups; i++) {
        if (getAll) {
            // Check if this we have any instance using this group
            for (k = 0, found = FALSE; ((k < nbInstances) && !found); k++) {
                for (x = 0; ((x < pInstances[k].max_secgroup_names) && !found); x++) {
                    if (!strcmp(pGni->secgroups[i].name, pInstances[k].secgroup_names[x].name)) {
                        found = TRUE;
                    }
                }
            }

            // If we have any instance using this group, then copy it
            if (found) {
                if (doOutNames)
                    psRetSecGroupNames[retCount] = strdup(pGni->secgroups[i].name);

                if (doOutStructs)
                    memcpy(&(pRetSecGroup[retCount]), &(pGni->secgroups[i]), sizeof (gni_secgroup));
                retCount++;
            }
        } else {
            if (!strcmp(psSecGroupNames[x], pGni->secgroups[i].name)) {
                // Check if this we have any instance using this group
                for (k = 0, found = FALSE; ((k < nbInstances) && !found); k++) {
                    for (x = 0; ((x < pInstances[k].max_secgroup_names) && !found); x++) {
                        if (!strcmp(pGni->secgroups[i].name, pInstances[k].secgroup_names[x].name)) {
                            found = TRUE;
                        }
                    }
                }

                // If we have any instance using this group, then copy it
                if (found) {
                    if (doOutNames) {
                        *psOutSecGroupNames = EUCA_REALLOC_C(*psOutSecGroupNames, (retCount + 1), sizeof (char *));
                        psRetSecGroupNames = *psOutSecGroupNames;
                        psRetSecGroupNames[retCount] = strdup(pGni->secgroups[i].name);
                    }

                    if (doOutStructs) {
                        *pOutSecGroups = EUCA_REALLOC_C(*pOutSecGroups, (retCount + 1), sizeof (gni_instance));
                        pRetSecGroup = *pOutSecGroups;
                        memcpy(&(pRetSecGroup[retCount]), &(pGni->secgroups[i]), sizeof (gni_secgroup));
                    }
                    retCount++;
                }
            }
        }
    }

    if (doOutNames)
        *pOutNbSecGroupNames = retCount;

    if (doOutStructs)
        *pOutNbSecGroups = retCount;

    EUCA_FREE(pInstances);
    return (ret);
}

//!
//! TODO: Function description.
//!
//! @param[in]  gni a pointer to the global network information structure
//! @param[in]  instance
//! @param[in]  secgroup_names
//! @param[in]  max_secgroup_names
//! @param[out] out_secgroup_names
//! @param[out] out_max_secgroup_names
//! @param[out] out_secgroups
//! @param[out] out_max_secgroups
//!
//! @return
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_instance_get_secgroups(globalNetworkInfo * gni, gni_instance * instance, char **secgroup_names, int max_secgroup_names, char ***out_secgroup_names,
        int *out_max_secgroup_names, gni_secgroup ** out_secgroups, int *out_max_secgroups) {
    int ret = 0, getall = 0, i = 0, j = 0, k = 0, retcount = 0, do_outnames = 0, do_outstructs = 0;
    gni_secgroup *ret_secgroups = NULL;
    char **ret_secgroup_names = NULL;

    if (!gni || !instance) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (out_secgroup_names && out_max_secgroup_names) {
        do_outnames = 1;
    }
    if (out_secgroups && out_max_secgroups) {
        do_outstructs = 1;
    }

    if (!do_outnames && !do_outstructs) {
        LOGEXTREME("nothing to do, both output variables are NULL\n");
        return (0);
    }

    if (do_outnames) {
        *out_secgroup_names = NULL;
        *out_max_secgroup_names = 0;
    }
    if (do_outstructs) {
        *out_secgroups = NULL;
        *out_max_secgroups = 0;
    }

    if ((secgroup_names == NULL) || !strcmp(secgroup_names[0], "*")) {
        getall = 1;
        if (do_outnames)
            *out_secgroup_names = EUCA_ZALLOC_C(instance->max_secgroup_names, sizeof (char *));
        if (do_outstructs)
            *out_secgroups = EUCA_ZALLOC_C(instance->max_secgroup_names, sizeof (gni_secgroup));
    }

    if (do_outnames)
        ret_secgroup_names = *out_secgroup_names;
    if (do_outstructs)
        ret_secgroups = *out_secgroups;

    retcount = 0;
    for (i = 0; i < instance->max_secgroup_names; i++) {
        if (getall) {
            if (do_outnames)
                ret_secgroup_names[i] = strdup(instance->secgroup_names[i].name);
            if (do_outstructs) {
                for (k = 0; k < gni->max_secgroups; k++) {
                    if (!strcmp(gni->secgroups[k].name, instance->secgroup_names[i].name)) {
                        memcpy(&(ret_secgroups[i]), &(gni->secgroups[k]), sizeof (gni_secgroup));
                        break;
                    }
                }
            }
            retcount++;
        } else {
            for (j = 0; j < max_secgroup_names; j++) {
                if (!strcmp(secgroup_names[j], instance->secgroup_names[i].name)) {
                    if (do_outnames) {
                        *out_secgroup_names = EUCA_REALLOC_C(*out_secgroup_names, (retcount + 1), sizeof (char *));
                        ret_secgroup_names = *out_secgroup_names;
                        ret_secgroup_names[retcount] = strdup(instance->secgroup_names[i].name);
                    }
                    if (do_outstructs) {
                        for (k = 0; k < gni->max_secgroups; k++) {
                            if (!strcmp(gni->secgroups[k].name, instance->secgroup_names[i].name)) {
                                *out_secgroups = EUCA_REALLOC_C(*out_secgroups, (retcount + 1), sizeof (gni_secgroup));
                                ret_secgroups = *out_secgroups;
                                memcpy(&(ret_secgroups[retcount]), &(gni->secgroups[k]), sizeof (gni_secgroup));
                                break;
                            }
                        }
                    }
                    retcount++;
                }
            }
        }
    }
    if (do_outnames)
        *out_max_secgroup_names = retcount;
    if (do_outstructs)
        *out_max_secgroups = retcount;

    return (ret);

}

//!
//! TODO: Function description.
//!
//! @param[in]  gni a pointer to the global network information structure
//! @param[in]  secgroup
//! @param[in]  instance_names
//! @param[in]  max_instance_names
//! @param[out] out_instance_names
//! @param[out] out_max_instance_names
//! @param[out] out_instances
//! @param[out] out_max_instances
//!
//! @return
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_secgroup_get_instances(globalNetworkInfo * gni, gni_secgroup * secgroup, char **instance_names, int max_instance_names, char ***out_instance_names,
        int *out_max_instance_names, gni_instance ** out_instances, int *out_max_instances) {
    int ret = 0, getall = 0, i = 0, j = 0, retcount = 0, do_outnames = 0, do_outstructs = 0;
    gni_instance *ret_instances = NULL;
    char **ret_instance_names = NULL;

    if (!gni || !secgroup) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (out_instance_names && out_max_instance_names) {
        do_outnames = 1;
    }
    if (out_instances && out_max_instances) {
        do_outstructs = 1;
    }

    if (out_max_instance_names) {
        *out_max_instance_names = 0;
    }
    if (out_max_instances) {
        *out_max_instances = 0;
    }

    if (!do_outnames && !do_outstructs) {
        LOGEXTREME("nothing to do, both output variables are NULL\n");
        return (0);
    }
    if (secgroup->max_instances == 0) {
        LOGEXTREME("nothing to do, no instance associated with %s\n", secgroup->name);
        return (0);
    }
    if (do_outnames) {
        *out_instance_names = EUCA_ZALLOC_C(secgroup->max_instances, sizeof (char *));
        ret_instance_names = *out_instance_names;
    }
    if (do_outstructs) {
        *out_instances = EUCA_ZALLOC_C(secgroup->max_instances, sizeof (gni_instance));
        ret_instances = *out_instances;
    }

    if ((instance_names == NULL) || (!strcmp(instance_names[0], "*"))) {
        getall = 1;
    }

    retcount = 0;
    for (i = 0; i < secgroup->max_instances; i++) {
        if (getall) {
            if (do_outnames)
                ret_instance_names[i] = strdup(secgroup->instances[i]->name);
            if (do_outstructs) {
                memcpy(&(ret_instances[i]), secgroup->instances[i], sizeof (gni_instance));
            }
            retcount++;
        } else {
            for (j = 0; j < max_instance_names; j++) {
                if (!strcmp(instance_names[j], secgroup->instances[i]->name)) {
                    if (do_outnames) {
                        ret_instance_names[retcount] = strdup(secgroup->instances[i]->name);
                    }
                    if (do_outstructs) {
                        memcpy(&(ret_instances[retcount]), secgroup->instances[i], sizeof (gni_instance));
                    }
                    retcount++;
                }
            }
        }
    }
    if (do_outnames)
        *out_max_instance_names = retcount;
    if (do_outstructs)
        *out_max_instances = retcount;

    return (ret);
}

//!
//! Retrieve the interfaces that are members of the given security group.
//!
//! @param[in]  gni a pointer to the global network information structure
//! @param[in]  secgroup a pointer to the gni_secgroup structure of the SG of interest
//! @param[in]  interface_names restrict the search to this list of interface names
//! @param[in]  max_interface_names number of interfaces specified
//! @param[out] out_interface_names array of interface names that were found
//! @param[out] out_max_interface_names number of interfaces found
//! @param[out] out_interfaces array of found interface structure pointers
//! @param[out] out_max_interfaces number of found interfaces
//!
//! @return
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_secgroup_get_interfaces(globalNetworkInfo * gni, gni_secgroup * secgroup,
        char **interface_names, int max_interface_names, char ***out_interface_names,
        int *out_max_interface_names, gni_instance *** out_interfaces, int *out_max_interfaces) {
    int ret = 0, getall = 0, i = 0, j = 0, retcount = 0, do_outnames = 0, do_outstructs = 0;
    gni_instance **ret_interfaces = NULL;
    char **ret_interface_names = NULL;

    if (!gni || !secgroup) {
        LOGERROR("Invalid argument: gni or secgroup is NULL - cannot get interfaces\n");
        return (1);
    }

    if (out_interface_names && out_max_interface_names) {
        do_outnames = 1;
    }
    if (out_interfaces && out_max_interfaces) {
        do_outstructs = 1;
    }
    if (!do_outnames && !do_outstructs) {
        LOGEXTREME("nothing to do, both output variables are NULL\n");
        return (0);
    }
    if (secgroup->max_interfaces == 0) {
        LOGEXTREME("nothing to do, no instances/interfaces associated with %s\n", secgroup->name);
        return (0);
    }

    if (do_outnames) {
        *out_interface_names = EUCA_ZALLOC_C(secgroup->max_interfaces, sizeof (char *));
        if (*out_interface_names == NULL) {
            LOGFATAL("out of memory: failed to allocate out_interface_names\n");
            do_outnames = 0;
        }
        *out_max_interface_names = 0;
    }
    if (do_outstructs) {
        *out_interfaces = EUCA_ZALLOC_C(secgroup->max_interfaces, sizeof (gni_instance *));
        if (*out_interfaces == NULL) {
            LOGFATAL("out of memory: failed to allocate out_interfaces\n");
            do_outstructs = 0;
        }
        *out_max_interfaces = 0;
    }

    if ((interface_names == NULL) || (!strcmp(interface_names[0], "*"))) {
        getall = 1;
    }

    if (do_outnames)
        ret_interface_names = *out_interface_names;
    if (do_outstructs)
        ret_interfaces = *out_interfaces;

    retcount = 0;
    for (i = 0; i < secgroup->max_interfaces; i++) {
        if (getall) {
            if (do_outnames)
                ret_interface_names[i] = strdup(secgroup->interfaces[i]->name);
            if (do_outstructs) {
                ret_interfaces[i] = secgroup->interfaces[i];
            }
            retcount++;
        } else {
            for (j = 0; j < max_interface_names; j++) {
                if (!strcmp(interface_names[j], secgroup->interfaces[i]->name)) {
                    if (do_outnames) {
                        ret_interface_names[retcount] = strdup(secgroup->interfaces[i]->name);
                    }
                    if (do_outstructs) {
                        ret_interfaces[retcount] = secgroup->interfaces[i];
                    }
                    retcount++;
                }
            }
        }
    }
    if (do_outnames)
        *out_max_interface_names = retcount;
    if (do_outstructs)
        *out_max_interfaces = retcount;

    return (ret);
}

/**
 * Invoke and parse results of xmlXPathEvalExpression()
 * @param ctxptr [in] pointer to xmlXPathContext
 * @param doc [in] the xml document of interest
 * @param startnode [in] xmlNodePtr where the search should start
 * @param expression [in] expression to evaluate. Path should be relative to startnode
 * @param results [out] parsed results (array of strings)
 * @param max_results [out] number of elements in results
 * @param resultnodeset [out] pointer to xmlNodeSet from the query result
 * @return 0 on success. 1 on failure.
 */
int evaluate_xpath_property(xmlXPathContextPtr ctxptr, xmlDocPtr doc, xmlNodePtr startnode, char *expression, char ***results, int *max_results) {
    int i, max_nodes = 0, result_count = 0;
    xmlXPathObjectPtr objptr;
    char **retresults;
    int res = 0;

    *max_results = 0;

    if ((!ctxptr) || (!doc) || (!results)) {
        LOGWARN("Invalid argument: null xmlXPathContext, xmlDoc, or results\n");
        res = 1;
    } else {
        ctxptr->node = startnode;
        ctxptr->doc = doc;
        objptr = xmlXPathEvalExpression((unsigned char *) expression, ctxptr);
        if (objptr == NULL) {
            LOGERROR("unable to evaluate xpath expression '%s'\n", expression);
            res = 1;
        } else {
            if (objptr->nodesetval) {
                max_nodes = (int) objptr->nodesetval->nodeNr;
                *results = EUCA_ZALLOC_C(max_nodes, sizeof (char *));
                retresults = *results;
                for (i = 0; i < max_nodes; i++) {
                    if (objptr->nodesetval->nodeTab[i] && objptr->nodesetval->nodeTab[i]->children && objptr->nodesetval->nodeTab[i]->children->content) {
                        retresults[result_count] = strdup((char *) objptr->nodesetval->nodeTab[i]->children->content);
                        result_count++;
                    }
                }
                *max_results = result_count;

                LOGEXTREME("%d results after evaluated expression %s\n", *max_results, expression);
                for (i = 0; i < *max_results; i++) {
                    LOGEXTREME("\tRESULT %d: %s\n", i, retresults[i]);
                }
            }
        }
        xmlXPathFreeObject(objptr);
    }

    return (res);
}

/**
 * Invoke and parse results of xmlXPathEvalExpression()
 * @param ctxptr [in] pointer to xmlXPathContext
 * @param doc [in] the xml document of interest
 * @param startnode [in] xmlNodePtr where the search should start
 * @param expression [in] expression to evaluate. Path should be relative to startnode
 * @param results [out] parsed results (array of strings)
 * @param max_results [out] number of elements in results
 * @param resultnodeset [out] pointer to xmlNodeSet from the query result
 * @return 0 on success. 1 on failure.
 */
int evaluate_xpath_element(xmlXPathContextPtr ctxptr, xmlDocPtr doc, xmlNodePtr startnode, char *expression, char ***results, int *max_results) {
    int i, max_nodes = 0, result_count = 0;
    xmlXPathObjectPtr objptr;
    char **retresults;
    int res = 0;

    *max_results = 0;

    if ((!ctxptr) || (!doc) || (!results)) {
        LOGERROR("Invalid argument: NULL xpath context, xmlDoc, or results\n");
        res = 1;
    } else {
        ctxptr->node = startnode;
        ctxptr->doc = doc;
        objptr = xmlXPathEvalExpression((unsigned char *) expression, ctxptr);
        if (objptr == NULL) {
            LOGERROR("unable to evaluate xpath expression '%s'\n", expression);
            res = 1;
        } else {
            if (objptr->nodesetval) {
                max_nodes = (int) objptr->nodesetval->nodeNr;
                *results = EUCA_ZALLOC_C(max_nodes, sizeof (char *));
                retresults = *results;
                for (i = 0; i < max_nodes; i++) {
                    if (objptr->nodesetval->nodeTab[i] && objptr->nodesetval->nodeTab[i]->properties && objptr->nodesetval->nodeTab[i]->properties->children
                            && objptr->nodesetval->nodeTab[i]->properties->children->content) {
                        retresults[result_count] = strdup((char *) objptr->nodesetval->nodeTab[i]->properties->children->content);
                        result_count++;
                    }
                }
                *max_results = result_count;

                LOGTRACE("%d results after evaluated expression %s\n", *max_results, expression);
                for (i = 0; i < *max_results; i++) {
                    LOGTRACE("\tRESULT %d: %s\n", i, retresults[i]);
                }
            }
        }
        xmlXPathFreeObject(objptr);
    }

    return (res);
}

/**
 * Evaluates XPATH and retrieves the xmlNodeSet of the query.
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] a pointer to the XML document
 * @param startnode [in] xmlNodePtr where the search should start
 * @param expression [in] expression a string pointer to the expression we want to evaluate
 * @param nodeset [out] xmlNodeSetPtr of the query.
 * @return 0 on success. 1 otherwise.
 */
int evaluate_xpath_nodeset(xmlXPathContextPtr ctxptr, xmlDocPtr doc, xmlNodePtr startnode, char *expression, xmlNodeSetPtr nodeset) {
    xmlXPathObjectPtr objptr;
    int res = 0;

    if (!nodeset) {
        LOGWARN("cannot return nodeset to NULL\n");
        return (1);
    }
    bzero(nodeset, sizeof (xmlNodeSet));
    if ((!ctxptr) || (!doc) || (!nodeset)) {
        LOGERROR("Invalid argument: NULL xpath context, xmlDoc, or nodeset\n");
        res = 1;
    } else {
        ctxptr->node = startnode;
        objptr = xmlXPathEvalExpression((unsigned char *) expression, ctxptr);
        if (objptr == NULL) {
            LOGERROR("unable to evaluate xpath expression '%s'\n", expression);
            res = 1;
        } else {
            if (objptr->nodesetval) {
                nodeset->nodeNr = objptr->nodesetval->nodeNr;
                nodeset->nodeMax = objptr->nodesetval->nodeMax;
                nodeset->nodeTab = EUCA_ZALLOC_C(nodeset->nodeMax, sizeof (xmlNodePtr));
                memcpy(nodeset->nodeTab, objptr->nodesetval->nodeTab, nodeset->nodeMax * sizeof (xmlNodePtr));
            }
        }
        xmlXPathFreeObject(objptr);
    }
    return (res);
}

/**
 * Allocates and initializes a new globalNetworkInfo structure.
 * @return A pointer to the newly allocated structure or NULL if any failure occurred.
 */
globalNetworkInfo *gni_init() {
    globalNetworkInfo *gni = NULL;
    gni = EUCA_ZALLOC_C(1, sizeof (globalNetworkInfo));

    gni->init = 1;
    return (gni);
}

/**
 * Populates a given globalNetworkInfo structure from the content of an XML file
 * @param gni [in] a pointer to the global network information structure
 * @param host_info [in] a pointer to the hostname info data structure (only relevant to VPCMIDO - to be deprecated)
 * @param xmlpath [in] path to the XML file to be used to populate
 * @return 0 on success or 1 on failure
 */
int gni_populate(globalNetworkInfo *gni, gni_hostname_info *host_info, char *xmlpath) {
    return (gni_populate_v(GNI_POPULATE_ALL, gni, host_info, xmlpath));
}

/**
 * Populates a given globalNetworkInfo structure from the content of an XML file
 * @param mode [in] mode what to populate GNI_POPULATE_ALL || GNI_POPULATE_CONFIG || GNI_POPULATE_NONE
 * @param gni [in] a pointer to the global network information structure
 * @param host_info [in] a pointer to the hostname info data structure (only relevant to VPCMIDO - to be deprecated)
 * @param xmlpath [in] path to the XML file to be used to populate
 * @return 0 on success or 1 on failure
 */
int gni_populate_v(int mode, globalNetworkInfo *gni, gni_hostname_info *host_info, char *xmlpath) {
    int rc = 0;
    xmlDocPtr docptr;
    xmlXPathContextPtr ctxptr;
    struct timeval tv, ttv;
    xmlNode * gni_nodes[GNI_XPATH_INVALID] = {0};

    if (mode == GNI_POPULATE_NONE) {
        return (0);
    }

    eucanetd_timer_usec(&ttv);
    eucanetd_timer_usec(&tv);
    if (!gni) {
        LOGERROR("invalid input\n");
        return (1);
    }

    gni_clear(gni);
    LOGTRACE("gni cleared in %ld us.\n", eucanetd_timer_usec(&tv));

    XML_INIT();
    LIBXML_TEST_VERSION
    docptr = xmlParseFile(xmlpath);
    if (docptr == NULL) {
        LOGERROR("unable to parse XML file (%s)\n", xmlpath);
        return (1);
    }

    ctxptr = xmlXPathNewContext(docptr);
    if (ctxptr == NULL) {
        LOGERROR("unable to get new xml context\n");
        xmlFreeDoc(docptr);
        return (1);
    }
    LOGTRACE("xml Xpath context - %ld us.\n", eucanetd_timer_usec(&tv));

    eucanetd_timer_usec(&tv);
    rc = gni_populate_xpathnodes(docptr, gni_nodes);

    LOGTRACE("begin parsing XML into data structures\n");

    // GNI version
    rc = gni_populate_gnidata(gni, gni_nodes[GNI_XPATH_CONFIGURATION], ctxptr, docptr);
    LOGTRACE("gni version populated in %ld us.\n", eucanetd_timer_usec(&tv));

    if (mode == GNI_POPULATE_ALL) {
        // Instances
        rc = gni_populate_instances(gni, gni_nodes[GNI_XPATH_INSTANCES], ctxptr, docptr);
        LOGTRACE("gni instances populated in %ld us.\n", eucanetd_timer_usec(&tv));

        // Security Groups
        rc = gni_populate_sgs(gni, gni_nodes[GNI_XPATH_SECURITYGROUPS], ctxptr, docptr);
        LOGTRACE("gni sgs populated in %ld us.\n", eucanetd_timer_usec(&tv));

        // VPCs
        rc = gni_populate_vpcs(gni, gni_nodes[GNI_XPATH_VPCS], ctxptr, docptr);
        LOGTRACE("gni vpcs populated in %ld us.\n", eucanetd_timer_usec(&tv));
        
        // Internet Gateways
        rc = gni_populate_internetgateways(gni, gni_nodes[GNI_XPATH_INTERNETGATEWAYS], ctxptr, docptr);
        LOGTRACE("gni Internet Gateways populated in %ld us.\n", eucanetd_timer_usec(&tv));

        // DHCP Option Sets
        rc = gni_populate_dhcpos(gni, gni_nodes[GNI_XPATH_DHCPOPTIONSETS], ctxptr, docptr);
        LOGTRACE("gni DHCP Option Sets populated in %ld us.\n", eucanetd_timer_usec(&tv));
    }

    // Configuration
    rc = gni_populate_configuration(gni, host_info, gni_nodes[GNI_XPATH_CONFIGURATION], ctxptr, docptr);
    LOGTRACE("gni configuration populated in %ld us.\n", eucanetd_timer_usec(&tv));

    xmlXPathFreeContext(ctxptr);
    xmlFreeDoc(docptr);

    if (mode == GNI_POPULATE_ALL) {
        // Find VPC and subnet interfaces
        for (int i = 0; i < gni->max_vpcs; i++) {
            gni_vpc *vpc = &(gni->vpcs[i]);
            rc = gni_vpc_get_interfaces(gni, vpc, &(vpc->interfaces), &(vpc->max_interfaces));
            if (rc) {
                LOGWARN("Failed to populate gni %s interfaces.\n", vpc->name);
            }
            vpc->dhcpOptionSet = gni_get_dhcpos(gni, vpc->dhcpOptionSet_name, NULL);
            for (int j = 0; j < vpc->max_subnets; j++) {
                gni_vpcsubnet *gnisubnet = &(vpc->subnets[j]);
                rc = gni_vpcsubnet_get_interfaces(gni, gnisubnet, vpc->interfaces, vpc->max_interfaces,
                        &(gnisubnet->interfaces), &(gnisubnet->max_interfaces));
                if (rc) {
                    LOGWARN("Failed to populate gni %s interfaces.\n", gnisubnet->name);
                }
                gnisubnet->networkAcl = gni_get_networkacl(vpc, gnisubnet->networkAcl_name, NULL);
            }
        }
    }
    LOGTRACE("end parsing XML into data structures\n");

    eucanetd_timer_usec(&tv);
    rc = gni_validate(gni);
    if (rc) {
        LOGDEBUG("could not validate GNI after XML parse: check network config\n");
        return (1);
    }
    LOGDEBUG("gni validated in %ld us.\n", eucanetd_timer_usec(&tv));

    LOGINFO("gni populated in %.2f ms.\n", eucanetd_timer_usec(&ttv) / 1000.0);

/*
    for (int i = 0; i < gni->max_instances; i++) {
        gni_instance_interface_print(&(gni->instances[i]), EUCA_LOG_INFO);
    }
    for (int i = 0; i < gni->max_interfaces; i++) {
        gni_instance_interface_print(&(gni->interfaces[i]), EUCA_LOG_INFO);
    }
    for (int j = 0; j < gni->max_secgroups; j++) {
        gni_sg_print(&(gni->secgroups[j]), EUCA_LOG_INFO);
    }
    for (int i = 0; i < gni->max_vpcs; i++) {
        gni_vpc_print(&(gni->vpcs[i]), EUCA_LOG_INFO);
    }
    for (int i = 0; i < gni->max_vpcIgws; i++) {
        gni_internetgateway_print(&(gni->vpcIgws[i]), EUCA_LOG_INFO);
    }
    for (int i = 0; i < gni->max_dhcpos; i++) {
        gni_dhcpos_print(&(gni->dhcpos[i]), EUCA_LOG_INFO);
    }
*/

    return (0);
}

/**
 * Retrieve pointers to xmlNode of GNI top level nodes (i.e., configuration, vpcs,
 * instances, dhcpOptionSets, internetGateways, securityGroups).
 * @param doc [in] xml document to be used
 * @param gni_nodes [in] an array of pointers to xmlNode (sufficient space for all
 * gni_xpath_node_type is expected).
 * @return 0 on success (array of pointers can have NULL elements if not found).
 * 1 on error.
 */
int gni_populate_xpathnodes(xmlDocPtr doc, xmlNode **gni_nodes) {
    xmlNodePtr node = NULL;

    if (doc && doc->children) {
        node = doc->children;
    } else {
        LOGERROR("Cannot populate from NULL xml ctx\n");
        return (1);
    }

    if (!node) {
        LOGERROR("Cannot populate from empty xml\n");
        return (1);
    }

    if (xmlStrcmp(node->name, (const xmlChar *) "network-data")) {
        LOGERROR("network-data node not found in GNI xml\n");
        return (1);
    }

    node = node->children;
    if (!node) {
        LOGTRACE("Empty xml ctx\n");
        return (0);
    }

    while (node) {
        int nodetype = gni_xmlstr2type(node->name);
        if (nodetype == GNI_XPATH_INVALID) {
            LOGTRACE("Unknown GNI xml node %s\n", node->name);
            node = node->next;
            continue;
        }
        gni_nodes[nodetype] = node;
        node = node->next;
    }
    return (0);
}

/**
 * Converts an xml node name to a numeric representation.
 * @param nodename [in] xml node name of interest.
 * @return numeric representation of the xml node of interest.
 */
gni_xpath_node_type gni_xmlstr2type(const xmlChar *nodename) {
    if (!xmlStrcmp(nodename, (const xmlChar *) "configuration")) {
        return (GNI_XPATH_CONFIGURATION);
    }
    if (!xmlStrcmp(nodename, (const xmlChar *) "vpcs")) {
        return (GNI_XPATH_VPCS);
    }
    if (!xmlStrcmp(nodename, (const xmlChar *) "instances")) {
        return (GNI_XPATH_INSTANCES);
    }
    if (!xmlStrcmp(nodename, (const xmlChar *) "dhcpOptionSets")) {
        return (GNI_XPATH_DHCPOPTIONSETS);
    }
    if (!xmlStrcmp(nodename, (const xmlChar *) "internetGateways")) {
        return (GNI_XPATH_INTERNETGATEWAYS);
    }
    if (!xmlStrcmp(nodename, (const xmlChar *) "securityGroups")) {
        return (GNI_XPATH_SECURITYGROUPS);
    }
    return (GNI_XPATH_INVALID);
}

/**
 * Populates globalNetworkInfo data from the content of an XML
 * file (xmlXPathContext is expected).
 *
 * @param gni [in] a pointer to the global network information structure
 * @param xmlnode [in] pointer to the "configuration" xmlNode
 * @param ctxptr [in] pointer to xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_gnidata(globalNetworkInfo *gni, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0;
    int i = 0;

    if ((gni == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: gni or doc is NULL.\n");
        return (1);
    }
    if (gni->init == FALSE) {
        LOGERROR("Invalid argument: gni is not initialized.\n");
        return (1);
    }

    if (xmlnode && xmlnode->name) {
        snprintf(expression, 2048, "./property[@name='mode']/value");
        rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
        for (i = 0; i < max_results; i++) {
            LOGTRACE("after function: %d: %s\n", i, results[i]);
            snprintf(gni->sMode, NETMODE_LEN, results[i]);
            gni->nmCode = euca_netmode_atoi(gni->sMode);
            EUCA_FREE(results[i]);
        }
        EUCA_FREE(results);
    }

    // get version and applied version
    snprintf(expression, 2048, "/network-data/@version");
    rc += evaluate_xpath_property(ctxptr, doc, NULL, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(gni->version, 32, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "/network-data/@applied-version");
    rc += evaluate_xpath_property(ctxptr, doc, NULL, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(gni->appliedVersion, 32, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    return (0);
}

/**
 * Populates globalNetworkInfo eucanetd configuration from the content of an XML
 * file (xmlXPathContext is expected). Relevant sections of globalNetworkInfo
 * structure is expected to be empty/clean.
 *
 * @param gni [in] a pointer to the global network information structure
 * @param host_info [in] pointer to hostname_info structure (populated as needed) - deprecated (EUCA-11997)
 * @param xmlnode [in] pointer to the "configuration" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_configuration(globalNetworkInfo *gni, gni_hostname_info *host_info, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048], *strptra = NULL;
    char **results = NULL;
    int max_results = 0, i, j, k, l;
    xmlNodeSet nodeset = {0};
    xmlNodePtr startnode;

    if ((gni == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: gni or doc is NULL.\n");
        return (1);
    }
    if ((gni->init == FALSE)) {
        LOGERROR("Invalid argument: gni is not initialized or instances section is not empty.\n");
        return (1);
    }
    if (!xmlnode || !xmlnode->name) {
        LOGERROR("Invalid argument: configuration xml node is required\n");
        return (1);
    }

    snprintf(expression, 2048, "./property[@name='enabledCLCIp']/value");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        gni->enabledCLCIp = dot2hex(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./property[@name='instanceDNSDomain']/value");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(gni->instanceDNSDomain, HOSTNAME_LEN, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

#ifdef USE_IP_ROUTE_HANDLER
    snprintf(expression, 2048, "./property[@name='publicGateway']/value");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        gni->publicGateway = dot2hex(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);
#endif /* USE_IP_ROUTE_HANDLER */

    if (IS_NETMODE_VPCMIDO(gni)) {
        snprintf(expression, 2048, "./property[@name='mido']");
        rc = evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
        if (nodeset.nodeNr == 1) {
            startnode = nodeset.nodeTab[0];

            snprintf(expression, 2048, "./property[@name='eucanetdHost']/value");
            rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
            for (i = 0; i < max_results; i++) {
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                snprintf(gni->EucanetdHost, HOSTNAME_LEN, "%s", results[i]);
                EUCA_FREE(results[i]);
            }
            EUCA_FREE(results);

            snprintf(expression, 2048, "./property[@name='publicNetworkCidr']/value");
            rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
            for (i = 0; i < max_results; i++) {
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                snprintf(gni->PublicNetworkCidr, HOSTNAME_LEN, "%s", results[i]);
                EUCA_FREE(results[i]);
            }
            EUCA_FREE(results);

            snprintf(expression, 2048, "./property[@name='publicGatewayIP']/value");
            rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
            for (i = 0; i < max_results; i++) {
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                snprintf(gni->PublicGatewayIP, HOSTNAME_LEN, "%s", results[i]);
                EUCA_FREE(results[i]);
            }
            EUCA_FREE(results);

            char gwtoks[6][2048];
            int good = 1, max_gws = 0;
            xmlNodeSet gwnodeset = {0};

            snprintf(expression, 2048, "./property[@name='gateways']/gateway");
            rc = evaluate_xpath_nodeset(ctxptr, doc, startnode, expression, &gwnodeset);
            LOGTRACE("Found %d gateways\n", gwnodeset.nodeNr);

            max_gws = gwnodeset.nodeNr;
            for (j = 0; j < max_gws; j++) {
                startnode = gwnodeset.nodeTab[j];
                snprintf(expression, 2048, "./property[@name='gatewayHost']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                for (i = 0; i < max_results; i++) {
                    LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                    bzero(gwtoks[j], 2048);
                    snprintf(gwtoks[j], 2048, "%s", results[i]);
                    EUCA_FREE(results[i]);
                }
                EUCA_FREE(results);

                snprintf(expression, 2048, "./property[@name='gatewayIP']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                for (i = 0; i < max_results; i++) {
                    LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                    euca_strncat(gwtoks[j], ",", 2048);
                    euca_strncat(gwtoks[j], results[i], 2048);
                    EUCA_FREE(results[i]);
                }
                EUCA_FREE(results);

                snprintf(expression, 2048, "./property[@name='gatewayInterface']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                for (i = 0; i < max_results; i++) {
                    LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                    euca_strncat(gwtoks[j], ",", 2048);
                    euca_strncat(gwtoks[j], results[i], 2048);
                    EUCA_FREE(results[i]);
                }
                EUCA_FREE(results);
            }
            EUCA_FREE(gwnodeset.nodeTab);

            if (!good || max_gws <= 0) {
                LOGERROR("Invalid mido gateway(s) detected. Check network configuration.\n");
            } else {
                for (i = 0; i < max_gws; i++) {
                    euca_strncat(gni->GatewayHosts, gwtoks[i], HOSTNAME_LEN * 3 * 33);
                    euca_strncat(gni->GatewayHosts, " ", HOSTNAME_LEN * 3 * 33);
                }
            }
        } else {
            LOGTRACE("mido section not found in GNI\n");
        }
        EUCA_FREE(nodeset.nodeTab);
    }

    snprintf(expression, 2048, "./property[@name='instanceDNSServers']/value");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    gni->instanceDNSServers = EUCA_ZALLOC(max_results, sizeof (u32));
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        gni->instanceDNSServers[i] = dot2hex(results[i]);
        EUCA_FREE(results[i]);
    }
    gni->max_instanceDNSServers = max_results;
    EUCA_FREE(results);

    snprintf(expression, 2048, "./property[@name='publicIps']/value");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    if (results && max_results) {
        rc += gni_serialize_iprange_list(results, max_results, &(gni->public_ips), &(gni->max_public_ips));
        for (i = 0; i < max_results; i++) {
            LOGTRACE("after function: %d: %s\n", i, results[i]);
            EUCA_FREE(results[i]);
        }
    }
    EUCA_FREE(results);

    // Do we have any managed subnets?
    snprintf(expression, 2048, "./property[@name='managedSubnet']/managedSubnet");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        LOGTRACE("Found %d managed subnets\n", nodeset.nodeNr);
        gni->managedSubnet = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_managedsubnet));
        gni->max_managedSubnets = nodeset.nodeNr;

        for (j = 0; j < gni->max_managedSubnets; j++) {
            startnode = nodeset.nodeTab[j];
            if (startnode && startnode->properties && startnode->properties->children &&
                    startnode->properties->children->content) {
                LOGTRACE("after function: %d: %s\n", j, startnode->properties->children->content);
                gni->managedSubnet[j].subnet = dot2hex((char *) startnode->properties->children->content);

                // Get the netmask
                snprintf(expression, 2048, "./property[@name='netmask']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                for (i = 0; i < max_results; i++) {
                    LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                    gni->managedSubnet[j].netmask = dot2hex(results[i]);
                    EUCA_FREE(results[i]);
                }
                EUCA_FREE(results);

                // Now get the minimum VLAN index
                snprintf(expression, 2048, "./property[@name='minVlan']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                for (i = 0; i < max_results; i++) {
                    LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                    gni->managedSubnet[j].minVlan = atoi(results[i]);
                    EUCA_FREE(results[i]);
                }
                EUCA_FREE(results);

                // Now get the maximum VLAN index
                snprintf(expression, 2048, "./property[@name='maxVlan']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                for (i = 0; i < max_results; i++) {
                    LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                    gni->managedSubnet[j].maxVlan = atoi(results[i]);
                    EUCA_FREE(results[i]);
                }
                EUCA_FREE(results);

                // Now get the segment size
                snprintf(expression, 2048, "./property[@name='segmentSize']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                for (i = 0; i < max_results; i++) {
                    LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                    gni->managedSubnet[j].segmentSize = atoi(results[i]);
                    EUCA_FREE(results[i]);
                }
                EUCA_FREE(results);
            } else {
                LOGWARN("invalid managed subnet at idx %d\n", j);
            }
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    // Do we have any global subnets?
    snprintf(expression, 2048, "./property[@name='subnets']/subnet");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        gni->subnets = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_subnet));
        gni->max_subnets = nodeset.nodeNr;

        for (j = 0; j < gni->max_subnets; j++) {
            startnode = nodeset.nodeTab[j];
            if (startnode && startnode->properties && startnode->properties->children &&
                    startnode->properties->children->content) {
                LOGTRACE("after function: %d: %s\n", j, startnode->properties->children->content);
                gni->subnets[j].subnet = dot2hex((char *) startnode->properties->children->content);

                // Get the netmask
                snprintf(expression, 2048, "./property[@name='netmask']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                for (i = 0; i < max_results; i++) {
                    LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                    gni->subnets[j].netmask = dot2hex(results[i]);
                    EUCA_FREE(results[i]);
                }
                EUCA_FREE(results);

                strptra = hex2dot(gni->subnets[j].subnet);
                snprintf(expression, 2048, "./property[@name='gateway']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                for (i = 0; i < max_results; i++) {
                    LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                    gni->subnets[j].gateway = dot2hex(results[i]);
                    EUCA_FREE(results[i]);
                }
                EUCA_FREE(results);
                EUCA_FREE(strptra);
            } else {
                LOGWARN("invalid global subnet at idx %d\n", j);
            }
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    // Clusters
    snprintf(expression, 2048, "./property[@name='clusters']/cluster");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        gni->clusters = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_cluster));
        gni->max_clusters = nodeset.nodeNr;

        for (j = 0; j < gni->max_clusters; j++) {
            startnode = nodeset.nodeTab[j];
            if (startnode && startnode->properties && startnode->properties->children &&
                    startnode->properties->children->content) {
                LOGTRACE("after function: %d: %s\n", j, startnode->properties->children->content);
                snprintf(gni->clusters[j].name, HOSTNAME_LEN, "%s", (char *) startnode->properties->children->content);

                snprintf(expression, 2048, "./property[@name='enabledCCIp']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                for (i = 0; i < max_results; i++) {
                    LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                    gni->clusters[j].enabledCCIp = dot2hex(results[i]);
                    EUCA_FREE(results[i]);
                }
                EUCA_FREE(results);

                snprintf(expression, 2048, "./property[@name='macPrefix']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                for (i = 0; i < max_results; i++) {
                    LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                    snprintf(gni->clusters[j].macPrefix, ENET_MACPREFIX_LEN, "%s", results[i]);
                    EUCA_FREE(results[i]);
                }
                EUCA_FREE(results);

                snprintf(expression, 2048, "./property[@name='privateIps']/value");
                rc += evaluate_xpath_property(ctxptr, doc, startnode, expression, &results, &max_results);
                if (results && max_results) {
                    rc += gni_serialize_iprange_list(results, max_results, &(gni->clusters[j].private_ips), &(gni->clusters[j].max_private_ips));
                    for (i = 0; i < max_results; i++) {
                        LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
                        EUCA_FREE(results[i]);
                    }
                }
                EUCA_FREE(results);

                xmlNodeSet snnodeset = {0};
                xmlNodePtr snstartnode = NULL;
                snprintf(expression, 2048, "./subnet");
                rc += evaluate_xpath_nodeset(ctxptr, doc, startnode, expression, &snnodeset);
                if (snnodeset.nodeNr > 0) {
                    snstartnode = snnodeset.nodeTab[0];
                    if (snstartnode && snstartnode->properties && snstartnode->properties->children &&
                            snstartnode->properties->children->content) {
                        LOGTRACE("\t\tafter function: %d: %s\n", j, snstartnode->properties->children->content);
                        gni->clusters[j].private_subnet.subnet = dot2hex((char *) snstartnode->properties->children->content);

                        snprintf(expression, 2048, "./property[@name='netmask']/value");
                        rc += evaluate_xpath_property(ctxptr, doc, snstartnode, expression, &results, &max_results);
                        for (i = 0; i < max_results; i++) {
                            LOGTRACE("\t\tafter function: %d: %s\n", i, results[i]);
                            gni->clusters[j].private_subnet.netmask = dot2hex(results[i]);
                            EUCA_FREE(results[i]);
                        }
                        EUCA_FREE(results);

                        snprintf(expression, 2048, "./property[@name='gateway']/value");
                        rc += evaluate_xpath_property(ctxptr, doc, snstartnode, expression, &results, &max_results);
                        for (i = 0; i < max_results; i++) {
                            LOGTRACE("\t\tafter function: %d: %s\n", i, results[i]);
                            gni->clusters[j].private_subnet.gateway = dot2hex(results[i]);
                            EUCA_FREE(results[i]);
                        }
                        EUCA_FREE(results);
                    }
                }
                EUCA_FREE(snnodeset.nodeTab);

                xmlNodeSet nnodeset = {0};
                xmlNodePtr nstartnode = NULL;
                snprintf(expression, 2048, "./property[@name='nodes']/node");
                rc += evaluate_xpath_nodeset(ctxptr, doc, startnode, expression, &nnodeset);
                if (nnodeset.nodeNr > 0) {
                    gni->clusters[j].nodes = EUCA_ZALLOC_C(nnodeset.nodeNr, sizeof (gni_node));
                    gni->clusters[j].max_nodes = nnodeset.nodeNr;

                    for (k = 0; k < nnodeset.nodeNr; k++) {
                        nstartnode = nnodeset.nodeTab[k];
                        if (nstartnode && nstartnode->properties && nstartnode->properties->children &&
                                nstartnode->properties->children->content) {
                            LOGTRACE("\t\tafter function: %d: %s\n", j, nstartnode->properties->children->content);
                            snprintf(gni->clusters[j].nodes[k].name, HOSTNAME_LEN, "%s", (char *) nstartnode->properties->children->content);
                        }

                        snprintf(expression, 2048, "./instanceIds/value");
                        rc += evaluate_xpath_property(ctxptr, doc, nstartnode, expression, &results, &max_results);
                        gni->clusters[j].nodes[k].instance_names = EUCA_ZALLOC_C(max_results, sizeof (gni_name));
                        for (i = 0; i < max_results; i++) {
                            LOGTRACE("\t\t\tafter function: %d: %s\n", i, results[i]);
                            snprintf(gni->clusters[j].nodes[k].instance_names[i].name, 1024, "%s", results[i]);
                            EUCA_FREE(results[i]);

                            if (IS_NETMODE_VPCMIDO(gni)) {
                                for (l = 0; l < gni->max_instances; l++) {
                                    if (!strcmp(gni->instances[l]->name, gni->clusters[j].nodes[k].instance_names[i].name)) {
                                        snprintf(gni->instances[l]->node, HOSTNAME_LEN, "%s", gni->clusters[j].nodes[k].name);
                                    }
                                }
/*
                                for (l = 0; l < gni->max_interfaces; l++) {
                                    if (!strcmp(gni->interfaces[l].instance_name.name, gni->clusters[j].nodes[k].instance_names[i].name)) {
                                        snprintf(gni->interfaces[l].node, HOSTNAME_LEN, "%s", gni->clusters[j].nodes[k].name);
                                    }
                                }
*/
                                for (l = 0; l < gni->max_ifs; l++) {
                                    if (!strcmp(gni->ifs[l]->instance_name.name, gni->clusters[j].nodes[k].instance_names[i].name)) {
                                        snprintf(gni->ifs[l]->node, HOSTNAME_LEN, "%s", gni->clusters[j].nodes[k].name);
                                    }
                                }
                            }
                        }
                        gni->clusters[j].nodes[k].max_instance_names = max_results;
                        EUCA_FREE(results);
                    }
                }
                EUCA_FREE(nnodeset.nodeTab);

            } else {
                LOGWARN("invalid cluster at idx %d\n", j);
            }
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    return (0);
}

/**
 * Populates globalNetworkInfo instances structure from the content of an XML
 * file (xmlXPathContext is expected). The instances section of globalNetworkInfo
 * structure is expected to be empty/clean.
 *
 * @param gni [in] a pointer to the global network information structure
 * @param xmlnode [in] pointer to the "instances" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_instances(globalNetworkInfo *gni, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    int i;

    if ((gni == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: gni or ctxptr is NULL.\n");
        return (1);
    }
    if ((gni->init == FALSE) || (gni->max_instances != 0)) {
        LOGERROR("Invalid argument: gni is not initialized or instances section is not empty.\n");
        return (1);
    }
    xmlNodeSet nodeset = {0};
    snprintf(expression, 2048, "./instance");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        gni->instances = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_instance *));
        gni->max_instances = nodeset.nodeNr;
    }
    LOGTRACE("Found %d instances\n", gni->max_instances);
    for (i = 0; i < gni->max_instances; i++) {
        if (nodeset.nodeTab[i]) {
            gni->instances[i] = EUCA_ZALLOC_C(1, sizeof (gni_instance));
            gni_populate_instance_interface(gni->instances[i], nodeset.nodeTab[i], ctxptr, doc);
            //gni_instance_interface_print(gni->instances[i], EUCA_LOG_INFO);
            gni_populate_interfaces(gni, gni->instances[i], nodeset.nodeTab[i], ctxptr, doc);
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    return 0;
}

/**
 * Populates globalNetworkInfo interfaces structure. Appends the interfaces of the
 * instance specified in xmlnode..
 *
 * @param gni [in] a pointer to the global network information structure
 * @param instance [in] instance that has the interfaces of interest
 * @param xmlnode [in] pointer to the "configuration" xmlNode (if NULL, full path search is performed)
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_interfaces(globalNetworkInfo *gni, gni_instance *instance, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    int i;

    if ((gni == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: gni or ctxptr is NULL.\n");
        return (1);
    }
    // interfaces are only relevant in VPCMIDO mode
    if (!IS_NETMODE_VPCMIDO(gni)) {
        return (0);
    }
    if (gni->init == FALSE) {
        LOGERROR("Invalid argument: gni is not initialized.\n");
        return (1);
    }

    xmlNodeSet nodeset = {0};
    snprintf(expression, 2048, "./networkInterfaces/networkInterface");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        instance->interfaces = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_instance *));
        instance->max_interfaces = nodeset.nodeNr;
        //gni->interfaces = EUCA_REALLOC_C(gni->interfaces, gni->max_interfaces + nodeset.nodeNr, sizeof (gni_instance));
        //memset(&(gni->interfaces[gni->max_interfaces]), 0, nodeset.nodeNr * sizeof (gni_instance));
        gni->ifs = EUCA_REALLOC_C(gni->ifs, gni->max_ifs + nodeset.nodeNr, sizeof (gni_instance *));
        memset(&(gni->ifs[gni->max_ifs]), 0, nodeset.nodeNr * sizeof (gni_instance *));
        LOGTRACE("Found %d interfaces\n", nodeset.nodeNr);
        for (i = 0; i < nodeset.nodeNr; i++) {
            if (nodeset.nodeTab[i]) {
                //snprintf(gni->interfaces[gni->max_interfaces + i].instance_name.name, 1024, instance->name);
                //gni_populate_instance_interface(&(gni->interfaces[gni->max_interfaces + i]), nodeset.nodeTab[i], ctxptr, doc);
                gni->ifs[gni->max_ifs + i] = EUCA_ZALLOC_C(1, sizeof (gni_instance));
                snprintf(gni->ifs[gni->max_ifs + i]->instance_name.name, 1024, instance->name);
                gni_populate_instance_interface(gni->ifs[gni->max_ifs + i], nodeset.nodeTab[i], ctxptr, doc);
                instance->interfaces[i] = gni->ifs[gni->max_ifs + i];
                //gni_instance_interface_print(gni->ifs[gni->max_ifs + i]), EUCA_LOG_INFO);
            }
        }
        //gni->max_interfaces += nodeset.nodeNr;
        gni->max_ifs += nodeset.nodeNr;
    }
    EUCA_FREE(nodeset.nodeTab);

    return (0);
}

/**
 * Populates globalNetworkInfo instance structure from the content of an XML
 * file (xmlXPathContext is expected). The target instance structure is assumed
 * to be clean.
 *
 * @param instance [in] a pointer to the global network information instance structure
 * @param xmlnode [in] pointer to the "configuration" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_instance_interface(gni_instance *instance, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0, i;
    boolean is_instance = TRUE;

    if ((instance == NULL) || (xmlnode == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: instance or ctxptr is NULL.\n");
        return (1);
    }

    if (xmlnode && xmlnode->properties && xmlnode->properties->children &&
            xmlnode->properties->children->content) {
        LOGTRACE("going to populate gni: %s\n", xmlnode->properties->children->content);
        snprintf(instance->name, INTERFACE_ID_LEN, "%s", (char *) xmlnode->properties->children->content);
    }

    if ((instance->name == NULL) || (strlen(instance->name) == 0)) {
        LOGERROR("Invalid argument: invalid instance name.\n");
    }

    if (strstr(instance->name, "eni-")) {
        is_instance = FALSE;
    } else {
        is_instance = TRUE;
    }
    snprintf(expression, 2048, "./ownerId");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
        snprintf(instance->accountId, 128, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./macAddress");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
        mac2hex(results[i], instance->macAddress);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./publicIp");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
        instance->publicIp = dot2hex(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./privateIp");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
        instance->privateIp = dot2hex(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./vpc");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
        snprintf(instance->vpc, 16, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./subnet");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
        snprintf(instance->subnet, 16, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./securityGroups/value");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    instance->secgroup_names = EUCA_ZALLOC_C(max_results, sizeof (gni_name));
    instance->gnisgs = EUCA_ZALLOC_C(max_results, sizeof (gni_secgroup *));
    for (i = 0; i < max_results; i++) {
        LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
        snprintf(instance->secgroup_names[i].name, 1024, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    instance->max_secgroup_names = max_results;
    EUCA_FREE(results);

    snprintf(expression, 2048, "./attachmentId");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
        snprintf(instance->attachmentId, ENI_ATTACHMENT_ID_LEN, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    if (is_instance) {
        // Populate interfaces.
/*
        snprintf(expression, 2048, "./networkInterfaces/networkInterface");
        rc += evaluate_xpath_element(ctxptr, doc, xmlnode, expression, &results, &max_results);
        instance->interface_names = EUCA_REALLOC_C(instance->interface_names, instance->max_interface_names + max_results, sizeof (gni_name));
        bzero(&(instance->interface_names[instance->max_interface_names]), max_results * sizeof (gni_name));
        for (i = 0; i < max_results; i++) {
            LOGTRACE("\t\tafter function: %d: %s\n", i, results[i]);
            snprintf(instance->interface_names[instance->max_interface_names + i].name, 1024, "%s", results[i]);
            EUCA_FREE(results[i]);
        }
        instance->max_interface_names += max_results;
        EUCA_FREE(results);
*/
    }
    if (!is_instance) {
        snprintf(expression, 2048, "./sourceDestCheck");
        rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
        for (i = 0; i < max_results; i++) {
            LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
            euca_strtolower(results[i]);
            if (!strcmp(results[i], "true")) {
                instance->srcdstcheck = TRUE;
            } else {
                instance->srcdstcheck = FALSE;
            }
            EUCA_FREE(results[i]);
        }
        EUCA_FREE(results);

        snprintf(expression, 2048, "./deviceIndex");
        rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
        for (i = 0; i < max_results; i++) {
            LOGTRACE("\tafter function: %d: %s\n", i, results[i]);
            instance->deviceidx = atoi(results[i]);
            EUCA_FREE(results[i]);
        }
        EUCA_FREE(results);
        // Use the instance name for primary interfaces
        snprintf(instance->ifname, INTERFACE_ID_LEN, "%s", instance->name);
        if (instance->deviceidx == 0) {
            snprintf(instance->name, INTERFACE_ID_LEN, "%s", instance->instance_name.name);
        }
    }
    return (0);
}

/**
 * Populates globalNetworkInfo security groups structure from the content of an XML
 * file (xmlXPathContext is expected). The security groups section of globalNetworkInfo
 * structure is expected to be empty/clean.
 *
 * @param gni [in] a pointer to the global network information structure
 * @param xmlnode [in] pointer to the "securityGroups" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_sgs(globalNetworkInfo *gni, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0, i, j, k, l;

    if ((gni == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: gni or ctxptr is NULL.\n");
        return (1);
    }
    if ((gni->init == FALSE) || (gni->max_secgroups != 0)) {
        LOGERROR("Invalid argument: gni is not initialized or sgs section is not empty.\n");
        return (1);
    }

    xmlNodeSet nodeset = {0};
    snprintf(expression, 2048, "./securityGroup");
    rc = evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        gni->secgroups = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_secgroup));
        gni->max_secgroups = nodeset.nodeNr;
    }
    LOGTRACE("Found %d security groups\n", gni->max_secgroups);
    for (j = 0; j < gni->max_secgroups; j++) {
        if (nodeset.nodeTab[j]) {
            xmlNodePtr sgnode = nodeset.nodeTab[j];
            gni_secgroup *gsg = &(gni->secgroups[j]);
            if (sgnode && sgnode->properties && sgnode->properties->children &&
                    sgnode->properties->children->content) {
                snprintf(gsg->name, SECURITY_GROUP_ID_LEN, "%s", (char *) sgnode->properties->children->content);
            }

            // populate secgroup's instance_names
            gni_instance *gi = NULL;
            gsg->max_instances = 0;
            for (k = 0; k < gni->max_instances; k++) {
                for (l = 0; l < gni->instances[k]->max_secgroup_names; l++) {
                    gi = gni->instances[k];
                    if (!strcmp(gi->secgroup_names[l].name, gsg->name)) {
                        gsg->instances = EUCA_REALLOC_C(gsg->instances, gsg->max_instances + 1, sizeof (gni_instance *));
                        gsg->instances[gsg->max_instances] = gi;
                        gsg->max_instances++;
                    }
                }
            }
            // populate secgroup's interface_names
            if (IS_NETMODE_VPCMIDO(gni)) {
                gsg->max_interfaces = 0;
                for (k = 0; k < gni->max_ifs; k++) {
                    gi = gni->ifs[k];
                    for (l = 0; l < gi->max_secgroup_names; l++) {
                        if (!strcmp(gi->secgroup_names[l].name, gsg->name)) {
                            gsg->interfaces = EUCA_REALLOC_C(gsg->interfaces, gsg->max_interfaces + 1, sizeof (gni_instance *));
                            gi->gnisgs[l] = gsg;
                            gsg->interfaces[gsg->max_interfaces] = gi;
                            gsg->max_interfaces++;
                        }
                    }
                }
            }

            snprintf(expression, 2048, "./ownerId");
            rc = evaluate_xpath_property(ctxptr, doc, sgnode, expression, &results, &max_results);
            for (i = 0; i < max_results; i++) {
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                snprintf(gni->secgroups[j].accountId, 128, "%s", results[i]);
                EUCA_FREE(results[i]);
            }
            EUCA_FREE(results);

            snprintf(expression, 2048, "./rules/value");
            rc = evaluate_xpath_property(ctxptr, doc, sgnode, expression, &results, &max_results);
            gni->secgroups[j].grouprules = EUCA_ZALLOC_C(max_results, sizeof (gni_name));
            for (i = 0; i < max_results; i++) {
                char newrule[2048];
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                rc = ruleconvert(results[i], newrule);
                if (!rc) {
                    snprintf(gni->secgroups[j].grouprules[i].name, 1024, "%s", newrule);
                }
                EUCA_FREE(results[i]);
            }
            gni->secgroups[j].max_grouprules = max_results;
            EUCA_FREE(results);

            // ingress rules
            xmlNodeSet ingressNodeset = {0};
            snprintf(expression, 2048, "./ingressRules/rule");
            rc = evaluate_xpath_nodeset(ctxptr, doc, sgnode, expression, &ingressNodeset);
            if (ingressNodeset.nodeNr > 0) {
                gni->secgroups[j].ingress_rules = EUCA_ZALLOC_C(ingressNodeset.nodeNr, sizeof (gni_rule));
                gni->secgroups[j].max_ingress_rules = ingressNodeset.nodeNr;
            }
            LOGTRACE("\tFound %d ingress rules\n", gni->secgroups[j].max_ingress_rules);
            for (k = 0; k < ingressNodeset.nodeNr; k++) {
                if (ingressNodeset.nodeTab[k]) {
                    gni_populate_rule(&(gni->secgroups[j].ingress_rules[k]),
                            ingressNodeset.nodeTab[k], ctxptr, doc);
                }
            }
            EUCA_FREE(ingressNodeset.nodeTab);

            // egress rules
            xmlNodeSet egressNodeset = {0};
            snprintf(expression, 2048, "./egressRules/rule");
            rc = evaluate_xpath_nodeset(ctxptr, doc, sgnode, expression, &egressNodeset);
            if (egressNodeset.nodeNr > 0) {
                gni->secgroups[j].egress_rules = EUCA_ZALLOC_C(egressNodeset.nodeNr, sizeof (gni_rule));
                gni->secgroups[j].max_egress_rules = egressNodeset.nodeNr;
            }
            LOGTRACE("\tFound %d egress rules\n", gni->secgroups[j].max_egress_rules);
            for (k = 0; k < egressNodeset.nodeNr; k++) {
                if (egressNodeset.nodeTab[k]) {
                    gni_populate_rule(&(gni->secgroups[j].egress_rules[k]),
                            egressNodeset.nodeTab[k], ctxptr, doc);
                }
            }
            EUCA_FREE(egressNodeset.nodeTab);
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    return (0);
}

/**
 * Populates globalNetworkInfo security group rule structure from the content of an XML
 * file (xmlXPathContext is expected). The target rule structure is assumed
 * to be clean.
 *
 * @param rule [in] a pointer to the global network information rule structure
 * @param xmlnode [in] pointer to the "rule" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_rule(gni_rule *rule, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0, i;

    if ((rule == NULL) || (xmlnode == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: rule or ctxptr is NULL.\n");
        return (1);
    }

    snprintf(expression, 2048, "./protocol");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        rule->protocol = atoi(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./groupId");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(rule->groupId, SECURITY_GROUP_ID_LEN, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./groupOwnerId");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(rule->groupOwnerId, 16, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./cidr");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        char *scidrnetaddr = NULL;
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(rule->cidr, NETWORK_ADDR_LEN, "%s", results[i]);
        cidrsplit(rule->cidr, &scidrnetaddr, &(rule->cidrSlashnet));
        rule->cidrNetaddr = dot2hex(scidrnetaddr);
        EUCA_FREE(scidrnetaddr);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./fromPort");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        rule->fromPort = atoi(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./toPort");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        rule->toPort = atoi(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./icmpType");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        rule->icmpType = atoi(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./icmpCode");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        rule->icmpCode = atoi(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    return (0);
}

/**
 * Populates globalNetworkInfo vpcs structure from the content of an XML
 * file (xmlXPathContext is expected). The vps section of globalNetworkInfo
 * structure is expected to be empty/clean.
 *
 * @param gni [in] a pointer to the global network information structure
 * @param xmlnode [in] pointer to the "vpcs" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_vpcs(globalNetworkInfo *gni, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    int j;

    if ((gni == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: gni or ctxptr is NULL.\n");
        return (1);
    }
    if ((gni->init == FALSE) || (gni->max_vpcs != 0)) {
        LOGERROR("Invalid argument: gni is not initialized or vpcs section is not empty.\n");
        return (1);
    }

    xmlNodeSet nodeset = {0};
    snprintf(expression, 2048, "./vpc");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        gni->vpcs = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_vpc));
        gni->max_vpcs = nodeset.nodeNr;
    }
    LOGTRACE("Found %d vpcs\n", gni->max_vpcs);
    for (j = 0; j < gni->max_vpcs; j++) {
        if (nodeset.nodeTab[j]) {
            xmlNodePtr vpcnode = nodeset.nodeTab[j];
            gni_vpc *gvpc = &(gni->vpcs[j]);
            if (vpcnode && vpcnode->properties && vpcnode->properties->children &&
                    vpcnode->properties->children->content) {
                snprintf(gvpc->name, 16, "%s", (char *) vpcnode->properties->children->content);
            }

            gni_populate_vpc(gvpc, vpcnode, ctxptr, doc);
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    return (0);
}

/**
 * Populates globalNetworkInfo VPC structure from the content of an XML
 * file (xmlXPathContext is expected). The target vpc structure is assumed
 * to be clean.
 *
 * @param vpc [in] a pointer to the global network information vpc structure
 * @param xmlnode [in] pointer to the "vpc" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_vpc(gni_vpc *vpc, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0;

    if ((vpc == NULL) || (xmlnode == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: vpc or ctxptr is NULL.\n");
        return (1);
    }

    snprintf(expression, 2048, "./ownerId");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (int i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(vpc->accountId, 128, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./cidr");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (int i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(vpc->cidr, 24, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./dhcpOptionSet");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (int i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(vpc->dhcpOptionSet_name, 16, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    xmlNodeSet nodeset;
    bzero(&nodeset, sizeof (xmlNodeSet));
    snprintf(expression, 2048, "./routeTables/routeTable");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        vpc->routeTables = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_route_table));
        vpc->max_routeTables = nodeset.nodeNr;
    }
    LOGTRACE("\tFound %d vpc route tables\n", vpc->max_routeTables);
    for (int j = 0; j < vpc->max_routeTables; j++) {
        if (nodeset.nodeTab[j]) {
            xmlNodePtr rtbnode = nodeset.nodeTab[j];
            gni_route_table *groutetb = &(vpc->routeTables[j]);
            if (rtbnode && rtbnode->properties && rtbnode->properties->children &&
                    rtbnode->properties->children->content) {
                snprintf(groutetb->name, 16, "%s", (char *) rtbnode->properties->children->content);
            }

            gni_populate_routetable(vpc, groutetb, rtbnode, ctxptr, doc);
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    bzero(&nodeset, sizeof (xmlNodeSet));
    snprintf(expression, 2048, "./subnets/subnet");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        vpc->subnets = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_vpcsubnet));
        vpc->max_subnets = nodeset.nodeNr;
    }
    LOGTRACE("\tFound %d vpc subnets\n", vpc->max_subnets);
    for (int j = 0; j < vpc->max_subnets; j++) {
        if (nodeset.nodeTab[j]) {
            xmlNodePtr vpcsnnode = nodeset.nodeTab[j];
            gni_vpcsubnet *gvpcsn = &(vpc->subnets[j]);
            if (vpcsnnode && vpcsnnode->properties && vpcsnnode->properties->children &&
                    vpcsnnode->properties->children->content) {
                snprintf(gvpcsn->name, 16, "%s", (char *) vpcsnnode->properties->children->content);
            }

            gni_populate_vpcsubnet(vpc, gvpcsn, vpcsnnode, ctxptr, doc);
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    snprintf(expression, 2048, "./internetGateways/value");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    vpc->internetGatewayNames = EUCA_ZALLOC_C(max_results, sizeof (gni_name));
    for (int i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(vpc->internetGatewayNames[i].name, 16, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    vpc->max_internetGatewayNames = max_results;
    EUCA_FREE(results);

    // NAT Gateways
    bzero(&nodeset, sizeof (xmlNodeSet));
    snprintf(expression, 2048, "./natGateways/natGateway");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        vpc->natGateways = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_nat_gateway));
        vpc->max_natGateways = nodeset.nodeNr;
    }
    LOGTRACE("\tFound %d vpc nat gateways\n", vpc->max_natGateways);
    for (int j = 0; j < vpc->max_natGateways; j++) {
        if (nodeset.nodeTab[j]) {
            xmlNodePtr ngnode = nodeset.nodeTab[j];
            gni_nat_gateway *gninatg = &(vpc->natGateways[j]);
            if (ngnode && ngnode->properties && ngnode->properties->children &&
                    ngnode->properties->children->content) {
                snprintf(gninatg->name, 32, "%s", (char *) ngnode->properties->children->content);
            }

            gni_populate_natgateway(gninatg, ngnode, ctxptr, doc);
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    // Network ACLs
    bzero(&nodeset, sizeof (xmlNodeSet));
    snprintf(expression, 2048, "./networkAcls/networkAcl");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        vpc->networkAcls = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_network_acl));
        vpc->max_networkAcls = nodeset.nodeNr;
    }
    LOGTRACE("\tFound %d vpc network acls\n", vpc->max_networkAcls);
    for (int j = 0; j < vpc->max_networkAcls; j++) {
        if (nodeset.nodeTab[j]) {
            xmlNodePtr aclnode = nodeset.nodeTab[j];
            gni_network_acl *gniacl = &(vpc->networkAcls[j]);
            if (aclnode && aclnode->properties && aclnode->properties->children &&
                    aclnode->properties->children->content) {
                snprintf(gniacl->name, NETWORK_ACL_ID_LEN, "%s", (char *) aclnode->properties->children->content);
            }

            gni_populate_networkacl(gniacl, aclnode, ctxptr, doc);
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    return (0);
}

/**
 * Populates globalNetworkInfo VPC route table structure from the content of an XML
 * file (xmlXPathContext is expected). The target route_table structure is assumed
 * to be clean.
 *
 * @param vpc [in] a pointer to the global network information vpc structure
 * @param routetable [in] a pointer to the global network information route_table structure
 * @param xmlnode [in] pointer to the "routeTable" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_routetable(gni_vpc *vpc, gni_route_table *routetable, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0, i;

    if ((routetable == NULL) || (xmlnode == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: route table or ctxptr is NULL.\n");
        return (1);
    }

    snprintf(expression, 2048, "./ownerId");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(routetable->accountId, 128, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    xmlNodeSet nodeset;
    bzero(&nodeset, sizeof (xmlNodeSet));
    snprintf(expression, 2048, "./routes/route");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        routetable->entries = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_route_entry));
        routetable->max_entries = nodeset.nodeNr;
    }
    LOGTRACE("\t\tFound %d vpc route table entries\n", routetable->max_entries);
    for (int j = 0; j < routetable->max_entries; j++) {
        if (nodeset.nodeTab[j]) {
            xmlNodePtr routenode = nodeset.nodeTab[j];
            gni_route_entry *gre = &(routetable->entries[j]);
            gni_populate_route(gre, routenode, ctxptr, doc);
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    return (0);
}

/**
 * Populates globalNetworkInfo route table route_entry structure from the content of an XML
 * file (xmlXPathContext is expected). The target route_entry structure is assumed
 * to be clean.
 *
 * @param route [in] a pointer to the global network information route_entry structure
 * @param xmlnode [in] pointer to the "rule" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_route(gni_route_entry *route, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0, i;

    if ((route == NULL) || (xmlnode == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: route or ctxptr is NULL.\n");
        return (1);
    }

    snprintf(expression, 2048, "./destinationCidr");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(route->destCidr, 16, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);
    snprintf(expression, 2048, "./gatewayId");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(route->target, 32, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);
    if (max_results == 0) {
        // Check if the target is a network interface
        snprintf(expression, 2048, "./networkInterfaceId");
        rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
        for (i = 0; i < max_results; i++) {
            LOGTRACE("after function: %d: %s\n", i, results[i]);
            snprintf(route->target, 32, "%s", results[i]);
            EUCA_FREE(results[i]);
        }
        EUCA_FREE(results);
    }
    if (max_results == 0) {
        // Check if the target is a nat gateway
        snprintf(expression, 2048, "./natGatewayId");
        rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
        for (i = 0; i < max_results; i++) {
            LOGTRACE("after function: %d: %s\n", i, results[i]);
            snprintf(route->target, 32, "%s", results[i]);
            EUCA_FREE(results[i]);
        }
        EUCA_FREE(results);
    }

    return (0);
}

/**
 * Populates globalNetworkInfo VPC subnet structure from the content of an XML
 * file (xmlXPathContext is expected). The target vpcsubnet structure is assumed
 * to be clean.
 *
 * @param vpc [in] a pointer to the global network information vpc structure
 * @param vpcsubnet [in] a pointer to the global network information vpcsubnet structure
 * @param xmlnode [in] pointer to the "vpcsubnet" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_vpcsubnet(gni_vpc *vpc, gni_vpcsubnet *vpcsubnet, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0, i;

    if ((vpcsubnet == NULL) || (xmlnode == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: subnet or ctxptr is NULL.\n");
        return (1);
    }

    snprintf(expression, 2048, "./ownerId");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(vpcsubnet->accountId, 128, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./cidr");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(vpcsubnet->cidr, 24, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./cluster");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(vpcsubnet->cluster_name, HOSTNAME_LEN, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./networkAcl");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(vpcsubnet->networkAcl_name, 16, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./routeTable");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(vpcsubnet->routeTable_name, 16, "%s", results[i]);
        vpcsubnet->routeTable = gni_vpc_get_routeTable(vpc, results[i]);
        if (vpcsubnet->routeTable == NULL) {
            LOGWARN("Failed to find GNI %s for %s\n", results[i], vpcsubnet->name)
        }
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    return (0);
}

/**
 * Populates globalNetworkInfo VPC NAT gateway structure from the content of an XML
 * file (xmlXPathContext is expected). The target nat_gateway structure is assumed
 * to be clean.
 *
 * @param natg [in] a pointer to the global network information nat_gateway structure
 * @param xmlnode [in] pointer to the "vpcsubnet" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_natgateway(gni_nat_gateway *natg, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0, i;

    if ((natg == NULL) || (xmlnode == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: subnet or ctxptr is NULL.\n");
        return (1);
    }

    snprintf(expression, 2048, "./ownerId");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(natg->accountId, 128, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./macAddress");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        mac2hex(results[i], natg->macAddress);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./publicIp");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        natg->publicIp = dot2hex(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./privateIp");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        natg->privateIp = dot2hex(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./vpc");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(natg->vpc, 16, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./subnet");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(natg->subnet, 16, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    return (0);
}

/**
 * Populates globalNetworkInfo VPC network acl structure from the content of an XML
 * file (xmlXPathContext is expected). The target networkAcl structure is assumed
 * to be clean.
 *
 * @param netacl [in] a pointer to the global network information route_table structure
 * @param xmlnode [in] pointer to the "networkAcl" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_networkacl(gni_network_acl *netacl, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0, i;

    if ((netacl == NULL) || (xmlnode == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: route table or ctxptr is NULL.\n");
        return (1);
    }

    snprintf(expression, 2048, "./ownerId");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(netacl->accountId, 128, "%s", results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    xmlNodeSet nodeset;
    bzero(&nodeset, sizeof (xmlNodeSet));
    snprintf(expression, 2048, "./ingressEntries/entry");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        netacl->ingress = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_acl_entry));
        netacl->max_ingress = nodeset.nodeNr;
    }
    LOGTRACE("\t\tFound %d ingress entries\n", netacl->max_ingress);
    for (int j = 0; j < netacl->max_ingress; j++) {
        if (nodeset.nodeTab[j]) {
            xmlNodePtr aclnode = nodeset.nodeTab[j];
            gni_acl_entry *gaclentry = &(netacl->ingress[j]);
            gni_populate_aclentry(gaclentry, aclnode, ctxptr, doc);
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    bzero(&nodeset, sizeof (xmlNodeSet));
    snprintf(expression, 2048, "./egressEntries/entry");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        netacl->egress = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_acl_entry));
        netacl->max_egress = nodeset.nodeNr;
    }
    LOGTRACE("\t\tFound %d egress entries\n", netacl->max_egress);
    for (int j = 0; j < netacl->max_egress; j++) {
        if (nodeset.nodeTab[j]) {
            xmlNodePtr aclnode = nodeset.nodeTab[j];
            gni_acl_entry *gaclentry = &(netacl->egress[j]);
            gni_populate_aclentry(gaclentry, aclnode, ctxptr, doc);
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    return (0);
}

/**
 * Populates globalNetworkInfo network acl entry structure from the content of an XML
 * file (xmlXPathContext is expected). The target acl_entry structure is assumed
 * to be clean.
 *
 * @param aclentry [in] a pointer to the global network information acl_entry structure
 * @param xmlnode [in] pointer to the "entry" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_aclentry(gni_acl_entry *aclentry, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0, i;

    if ((aclentry == NULL) || (xmlnode == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: aclentry or ctxptr is NULL.\n");
        return (1);
    }

    if (xmlnode && xmlnode->properties && xmlnode->properties->children &&
            xmlnode->properties->children->content) {
        aclentry->number = atoi((char *) xmlnode->properties->children->content);
    }

    snprintf(expression, 2048, "./action");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        if (!strcmp(results[i], "allow")) {
            aclentry->allow = 1;
        }
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./protocol");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        aclentry->protocol = atoi(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./cidr");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        char *scidrnetaddr = NULL;
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        snprintf(aclentry->cidr, NETWORK_ADDR_LEN, "%s", results[i]);
        cidrsplit(aclentry->cidr, &scidrnetaddr, &(aclentry->cidrSlashnet));
        aclentry->cidrNetaddr = dot2hex(scidrnetaddr);
        EUCA_FREE(scidrnetaddr);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./portRangeFrom");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        aclentry->fromPort = atoi(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./portRangeTo");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        aclentry->toPort = atoi(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./icmpType");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        aclentry->icmpType = atoi(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    snprintf(expression, 2048, "./icmpCode");
    rc += evaluate_xpath_property(ctxptr, doc, xmlnode, expression, &results, &max_results);
    for (i = 0; i < max_results; i++) {
        LOGTRACE("after function: %d: %s\n", i, results[i]);
        aclentry->icmpCode = atoi(results[i]);
        EUCA_FREE(results[i]);
    }
    EUCA_FREE(results);

    return (0);
}

/**
 * Populates globalNetworkInfo internet gateway structure from the content of an XML
 * file (xmlXPathContext is expected). The internet_gateway section of globalNetworkInfo
 * structure is expected to be empty/clean.
 *
 * @param gni [in] a pointer to the global network information structure
 * @param xmlnode [in] pointer to the "internetGateways" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_internetgateways(globalNetworkInfo *gni, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0, i, j;

    if ((gni == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: gni or ctxptr is NULL.\n");
        return (1);
    }
    if ((gni->init == FALSE) || (gni->max_vpcIgws != 0)) {
        LOGERROR("Invalid argument: gni is not initialized or internet gateways section is not empty.\n");
        return (1);
    }

    xmlNodeSet nodeset = {0};
    snprintf(expression, 2048, "./internetGateway");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        gni->vpcIgws = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_internet_gateway));
        gni->max_vpcIgws = nodeset.nodeNr;
    }
    LOGTRACE("Found %d Internet Gateways\n", gni->max_vpcIgws);
    for (j = 0; j < gni->max_vpcIgws; j++) {
        if (nodeset.nodeTab[j]) {
            xmlNodePtr ignode = nodeset.nodeTab[j];
            gni_internet_gateway *gig = &(gni->vpcIgws[j]);
            if (ignode && ignode->properties && ignode->properties->children &&
                    ignode->properties->children->content) {
                snprintf(gig->name, 16, "%s", (char *) ignode->properties->children->content);
            }

            snprintf(expression, 2048, "./ownerId");
            rc += evaluate_xpath_property(ctxptr, doc, ignode, expression, &results, &max_results);
            for (i = 0; i < max_results; i++) {
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                snprintf(gig->accountId, 128, "%s", results[i]);
                EUCA_FREE(results[i]);
            }
            EUCA_FREE(results);
        }
    }
    EUCA_FREE(nodeset.nodeTab);

    return (0);
}

/**
 * Populates globalNetworkInfo dhcp option set structure from the content of an XML
 * file (xmlXPathContext is expected). The dhcp_os section of globalNetworkInfo
 * structure is expected to be empty/clean.
 *
 * @param gni [in] a pointer to the global network information structure
 * @param xmlnode [in] pointer to the "dhcpOptionSets" xmlNode
 * @param ctxptr [in] pointer to the xmlXPathContext
 * @param doc [in] xml document to be used to populate
 *
 * @return 0 on success or 1 on failure
 */
int gni_populate_dhcpos(globalNetworkInfo *gni, xmlNodePtr xmlnode, xmlXPathContextPtr ctxptr, xmlDocPtr doc) {
    int rc = 0;
    char expression[2048];
    char **results = NULL;
    int max_results = 0, i, j;

    if ((gni == NULL) || (ctxptr == NULL) || (doc == NULL)) {
        LOGERROR("Invalid argument: gni or ctxptr is NULL.\n");
        return (1);
    }
    if ((gni->init == FALSE) || (gni->max_dhcpos != 0)) {
        LOGERROR("Invalid argument: gni is not initialized or DHCP option sets section is not empty.\n");
        return (1);
    }

    xmlNodeSet nodeset = {0};
    snprintf(expression, 2048, "./dhcpOptionSet");
    rc += evaluate_xpath_nodeset(ctxptr, doc, xmlnode, expression, &nodeset);
    if (nodeset.nodeNr > 0) {
        gni->dhcpos = EUCA_ZALLOC_C(nodeset.nodeNr, sizeof (gni_dhcp_os));
        gni->max_dhcpos = nodeset.nodeNr;
    }
    LOGTRACE("Found %d DHCP Option Sets\n", gni->max_dhcpos);
    for (j = 0; j < gni->max_dhcpos; j++) {
        if (nodeset.nodeTab[j]) {
            xmlNodePtr dhnode = nodeset.nodeTab[j];
            gni_dhcp_os *gdh = &(gni->dhcpos[j]);
            if (dhnode && dhnode->properties && dhnode->properties->children &&
                    dhnode->properties->children->content) {
                snprintf(gdh->name, DHCP_OS_ID_LEN, "%s", (char *) dhnode->properties->children->content);
            }

            snprintf(expression, 2048, "./ownerId");
            rc += evaluate_xpath_property(ctxptr, doc, dhnode, expression, &results, &max_results);
            for (i = 0; i < max_results; i++) {
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                snprintf(gdh->accountId, 128, "%s", results[i]);
                EUCA_FREE(results[i]);
            }
            EUCA_FREE(results);

            snprintf(expression, 2048, "./property[@name='domain-name']/value");
            rc += evaluate_xpath_property(ctxptr, doc, dhnode, expression, &results, &max_results);
            gdh->domains = EUCA_ZALLOC_C(max_results, sizeof (gni_name));
            for (i = 0; i < max_results; i++) {
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                snprintf(gdh->domains[i].name, 1024, "%s", results[i]);
                EUCA_FREE(results[i]);
            }
            gdh->max_domains = max_results;
            EUCA_FREE(results);

            snprintf(expression, 2048, "./property[@name='domain-name-servers']/value");
            rc += evaluate_xpath_property(ctxptr, doc, dhnode, expression, &results, &max_results);
            gdh->dns = EUCA_ZALLOC_C(max_results, sizeof (u32));
            for (i = 0; i < max_results; i++) {
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                gdh->dns[i] = dot2hex(results[i]);
                EUCA_FREE(results[i]);
            }
            gdh->max_dns = max_results;
            EUCA_FREE(results);

            snprintf(expression, 2048, "./property[@name='ntp-servers']/value");
            rc += evaluate_xpath_property(ctxptr, doc, dhnode, expression, &results, &max_results);
            gdh->ntp = EUCA_ZALLOC_C(max_results, sizeof (u32));
            for (i = 0; i < max_results; i++) {
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                gdh->ntp[i] = dot2hex(results[i]);
                EUCA_FREE(results[i]);
            }
            gdh->max_ntp = max_results;
            EUCA_FREE(results);

            snprintf(expression, 2048, "./property[@name='netbios-name-servers']/value");
            rc += evaluate_xpath_property(ctxptr, doc, dhnode, expression, &results, &max_results);
            gdh->netbios_ns = EUCA_ZALLOC_C(max_results, sizeof (u32));
            for (i = 0; i < max_results; i++) {
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                gdh->netbios_ns[i] = dot2hex(results[i]);
                EUCA_FREE(results[i]);
            }
            gdh->max_ntp = max_results;
            EUCA_FREE(results);

            snprintf(expression, 2048, "./property[@name='netbios-node-type']/value");
            rc += evaluate_xpath_property(ctxptr, doc, dhnode, expression, &results, &max_results);
            for (i = 0; i < max_results; i++) {
                LOGTRACE("after function: %d: %s\n", i, results[i]);
                gdh->netbios_type = atoi(results[i]);
                EUCA_FREE(results[i]);
            }
            EUCA_FREE(results);

        }
    }
    EUCA_FREE(nodeset.nodeTab);

    return (0);
}

//!
//! TODO: Describe
//!
//! @param[in]  inlist
//! @param[in]  inmax
//! @param[out] outlist
//! @param[out] outmax
//!
//! @return 0 on success or 1 on failure
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_serialize_iprange_list(char **inlist, int inmax, u32 ** outlist, int *outmax)
{
    int i = 0;
    int ret = 0;
    int outidx = 0;
    u32 *outlistbuf = NULL;
    int max_outlistbuf = 0;

    if (!inlist || inmax < 0 || !outlist || !outmax) {
        LOGERROR("invalid input\n");
        return (1);
    }
    *outlist = NULL;
    *outmax = 0;

    for (i = 0; i < inmax; i++) {
        char *range = NULL;
        char *tok = NULL;
        char *start = NULL;
        char *end = NULL;
        int numi = 0;

        LOGTRACE("parsing input range: %s\n", inlist[i]);

        range = strdup(inlist[i]);
        tok = strchr(range, '-');
        if (tok) {
            *tok = '\0';
            tok++;
            if (strlen(tok)) {
                start = strdup(range);
                end = strdup(tok);
            } else {
                LOGERROR("empty end range from input '%s': check network config\n", inlist[i]);
                start = NULL;
                end = NULL;
            }
        } else {
            start = strdup(range);
            end = strdup(range);
        }
        EUCA_FREE(range);

        if (start && end) {
            uint32_t startb, endb, idxb, localhost;

            LOGTRACE("start=%s end=%s\n", start, end);
            localhost = dot2hex("127.0.0.1");
            startb = dot2hex(start);
            endb = dot2hex(end);
            if ((startb <= endb) && (startb != localhost) && (endb != localhost)) {
                numi = (int) (endb - startb) + 1;
                outlistbuf = EUCA_REALLOC_C(outlistbuf, (max_outlistbuf + numi), sizeof (u32));
                outidx = max_outlistbuf;
                max_outlistbuf += numi;
                for (idxb = startb; idxb <= endb; idxb++) {
                    outlistbuf[outidx] = idxb;
                    outidx++;
                }
            } else {
                LOGERROR("end range '%s' is smaller than start range '%s' from input '%s': check network config\n", end, start, inlist[i]);
                ret = 1;
            }
        } else {
            LOGERROR("couldn't parse range from input '%s': check network config\n", inlist[i]);
            ret = 1;
        }

        EUCA_FREE(start);
        EUCA_FREE(end);
    }

    if (max_outlistbuf > 0) {
        *outmax = max_outlistbuf;
        *outlist = EUCA_ZALLOC_C(*outmax, sizeof (u32));
        memcpy(*outlist, outlistbuf, sizeof (u32) * max_outlistbuf);
    }
    EUCA_FREE(outlistbuf);

    return (ret);
}

//!
//! Iterates through a given globalNetworkInfo structure and execute the
//! given operation mode.
//!
//! @param[in] gni a pointer to the global network information structure
//! @param[in] mode the iteration mode: GNI_ITERATE_PRINT or GNI_ITERATE_FREE
//!
//! @return Always return 0
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_iterate(globalNetworkInfo * gni, int mode)
{
    int i, j;
    char *strptra = NULL;

    strptra = hex2dot(gni->enabledCLCIp);
    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("enabledCLCIp: %s\n", SP(strptra));
    EUCA_FREE(strptra);

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("instanceDNSDomain: %s\n", gni->instanceDNSDomain);

#ifdef USE_IP_ROUTE_HANDLER
    if (mode == GNI_ITERATE_PRINT) {
        strptra = hex2dot(gni->publicGateway);
        LOGTRACE("publicGateway: %s\n", SP(strptra));
        EUCA_FREE(strptra);
    }
#endif /* USE_IP_ROUTE_HANDLER */

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("instanceDNSServers: \n");
    for (i = 0; i < gni->max_instanceDNSServers; i++) {
        strptra = hex2dot(gni->instanceDNSServers[i]);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\tdnsServer %d: %s\n", i, SP(strptra));
        EUCA_FREE(strptra);
    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->instanceDNSServers);
    }

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("publicIps: \n");
    for (i = 0; i < gni->max_public_ips; i++) {
        strptra = hex2dot(gni->public_ips[i]);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\tip %d: %s\n", i, SP(strptra));
        EUCA_FREE(strptra);
    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->public_ips);
    }

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("subnets: \n");
    for (i = 0; i < gni->max_subnets; i++) {

        strptra = hex2dot(gni->subnets[i].subnet);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\tsubnet %d: %s\n", i, SP(strptra));
        EUCA_FREE(strptra);

        strptra = hex2dot(gni->subnets[i].netmask);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\tnetmask: %s\n", SP(strptra));
        EUCA_FREE(strptra);

        strptra = hex2dot(gni->subnets[i].gateway);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\tgateway: %s\n", SP(strptra));
        EUCA_FREE(strptra);

    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->subnets);
    }

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("managed_subnets: \n");
    for (i = 0; i < gni->max_subnets; i++) {
        strptra = hex2dot(gni->managedSubnet[i].subnet);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\tmanaged_subnet %d: %s\n", i, SP(strptra));
        EUCA_FREE(strptra);

        strptra = hex2dot(gni->managedSubnet[i].netmask);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\tnetmask: %s\n", SP(strptra));
        EUCA_FREE(strptra);

        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\tminVlan: %d\n", gni->managedSubnet[i].minVlan);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\tmaxVlan: %d\n", gni->managedSubnet[i].minVlan);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\tsegmentSize: %d\n", gni->managedSubnet[i].segmentSize);
    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->managedSubnet);
    }

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("clusters: \n");
    for (i = 0; i < gni->max_clusters; i++) {
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\tcluster %d: %s\n", i, gni->clusters[i].name);
        strptra = hex2dot(gni->clusters[i].enabledCCIp);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\tenabledCCIp: %s\n", SP(strptra));
        EUCA_FREE(strptra);

        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\tmacPrefix: %s\n", gni->clusters[i].macPrefix);

        strptra = hex2dot(gni->clusters[i].private_subnet.subnet);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\tsubnet: %s\n", SP(strptra));
        EUCA_FREE(strptra);

        strptra = hex2dot(gni->clusters[i].private_subnet.netmask);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\t\tnetmask: %s\n", SP(strptra));
        EUCA_FREE(strptra);

        strptra = hex2dot(gni->clusters[i].private_subnet.gateway);
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\t\tgateway: %s\n", SP(strptra));
        EUCA_FREE(strptra);

        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\tprivate_ips \n");
        for (j = 0; j < gni->clusters[i].max_private_ips; j++) {
            strptra = hex2dot(gni->clusters[i].private_ips[j]);
            if (mode == GNI_ITERATE_PRINT)
                LOGTRACE("\t\t\tip %d: %s\n", j, SP(strptra));
            EUCA_FREE(strptra);
        }
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\t\tnodes \n");
        for (j = 0; j < gni->clusters[i].max_nodes; j++) {
            if (mode == GNI_ITERATE_PRINT)
                LOGTRACE("\t\t\tnode %d: %s\n", j, gni->clusters[i].nodes[j].name);
            if (mode == GNI_ITERATE_FREE) {
                gni_node_clear(&(gni->clusters[i].nodes[j]));
            }
        }
        if (mode == GNI_ITERATE_FREE) {
            EUCA_FREE(gni->clusters[i].nodes);
            gni_cluster_clear(&(gni->clusters[i]));
        }
    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->clusters);
    }

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("instances: \n");
    for (i = 0; i < gni->max_instances; i++) {
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\tid: %s\n", gni->instances[i]->name);
        if (mode == GNI_ITERATE_FREE) {
            gni_instance_clear(gni->instances[i]);
            EUCA_FREE(gni->instances[i]);
        }
    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->instances);
    }

/*
    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("interfaces: \n");
    for (i = 0; i < gni->max_interfaces; i++) {
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\tid: %s\n", gni->interfaces[i].name);
        if (mode == GNI_ITERATE_FREE) {
            gni_instance_clear(&(gni->interfaces[i]));
        }
    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->interfaces);
    }
*/

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("interfaces: \n");
    for (i = 0; i < gni->max_ifs; i++) {
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\tid: %s\n", gni->ifs[i]->name);
        if (mode == GNI_ITERATE_FREE) {
            gni_instance_clear(gni->ifs[i]);
            EUCA_FREE(gni->ifs[i]);
        }
    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->ifs);
    }

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("secgroups: \n");
    for (i = 0; i < gni->max_secgroups; i++) {
        if (mode == GNI_ITERATE_PRINT)
            LOGTRACE("\tname: %s\n", gni->secgroups[i].name);
        if (mode == GNI_ITERATE_FREE) {
            gni_secgroup_clear(&(gni->secgroups[i]));
        }
    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->secgroups);
    }

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("vpcs: \n");
    for (i = 0; i < gni->max_vpcs; i++) {
        if (mode == GNI_ITERATE_PRINT) {
            LOGTRACE("\tname: %s\n", gni->vpcs[i].name);
            LOGTRACE("\taccountId: %s\n", gni->vpcs[i].accountId);
            LOGTRACE("\tsubnets: \n");
            for (j = 0; j < gni->vpcs[i].max_subnets; j++) {
                LOGTRACE("\t\tname: %s\n", gni->vpcs[i].subnets[j].name);
                LOGTRACE("\t\trouteTable: %s\n", gni->vpcs[i].subnets[j].routeTable_name);
            }
        }
        if (mode == GNI_ITERATE_FREE) {
            gni_vpc_clear(&(gni->vpcs[i]));
        }
    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->vpcs);
    }

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("Internet Gateways: \n");
    for (i = 0; i < gni->max_vpcIgws; i++) {
        if (mode == GNI_ITERATE_PRINT) {
            LOGTRACE("\tname: %s\n", gni->vpcIgws[i].name);
            LOGTRACE("\taccountId: %s\n", gni->vpcIgws[i].accountId);
        }
    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->vpcIgws);
    }

    if (mode == GNI_ITERATE_PRINT)
        LOGTRACE("DHCP Option Sets: \n");
    for (i = 0; i < gni->max_dhcpos; i++) {
        if (mode == GNI_ITERATE_PRINT) {
            LOGTRACE("\tname: %s\n", gni->dhcpos[i].name);
            LOGTRACE("\taccountId: %s\n", gni->dhcpos[i].accountId);
            char *dhcpdstr = NULL;
            char dhcpsstr[1024];
            dhcpsstr[0] = '\0';
            for (j = 0; j < gni->dhcpos[i].max_domains; j++) {
                strncat(dhcpsstr, gni->dhcpos[i].domains[j].name, 512);
                strncat(dhcpsstr, " ", 512);
            }
            if (gni->dhcpos[i].max_domains) {
                LOGTRACE("\t\tdomains: %s\n", dhcpsstr);
            }
            dhcpsstr[0] = '\0';
            for (j = 0; j < gni->dhcpos[i].max_dns; j++) {
                dhcpdstr = hex2dot(gni->dhcpos[i].dns[j]);
                strncat(dhcpsstr, dhcpdstr, 512);
                strncat(dhcpsstr, ", ", 512);
                EUCA_FREE(dhcpdstr);
            }
            if (gni->dhcpos[i].max_dns) {
                if (strlen(dhcpsstr) > 2) {
                    dhcpsstr[strlen(dhcpsstr) - 2] = '\0';
                }
                LOGTRACE("\t\tdns: %s\n", dhcpsstr);
            }
            dhcpsstr[0] = '\0';
            for (j = 0; j < gni->dhcpos[i].max_ntp; j++) {
                dhcpdstr = hex2dot(gni->dhcpos[i].ntp[j]);
                strncat(dhcpsstr, dhcpdstr, 512);
                strncat(dhcpsstr, ", ", 512);
                EUCA_FREE(dhcpdstr);
            }
            if (gni->dhcpos[i].max_ntp) {
                if (strlen(dhcpsstr) > 2) {
                    dhcpsstr[strlen(dhcpsstr) - 2] = '\0';
                }
                LOGTRACE("\t\tntp: %s\n", dhcpsstr);
            }
            for (j = 0; j < gni->dhcpos[i].max_netbios_ns; j++) {
                dhcpdstr = hex2dot(gni->dhcpos[i].netbios_ns[j]);
                strncat(dhcpsstr, dhcpdstr, 512);
                strncat(dhcpsstr, ", ", 512);
                EUCA_FREE(dhcpdstr);
            }
            if (gni->dhcpos[i].max_netbios_ns) {
                if (strlen(dhcpsstr) > 2) {
                    dhcpsstr[strlen(dhcpsstr) - 2] = '\0';
                }
                LOGTRACE("\t\tnetbios_ns: %s\n", dhcpsstr);
            }
            if (gni->dhcpos[i].netbios_type) {
                LOGTRACE("\t\tnetbios_type: %d\n", gni->dhcpos[i].netbios_type);
            }
        }
        if (mode == GNI_ITERATE_FREE) {
            gni_dhcpos_clear(&(gni->dhcpos[i]));
        }
    }
    if (mode == GNI_ITERATE_FREE) {
        EUCA_FREE(gni->dhcpos);
    }

    if (mode == GNI_ITERATE_FREE) {
        //bzero(gni, sizeof (globalNetworkInfo));
        gni->init = 1;
        gni->networkInfo[0] = '\0';
        char *version_addr = (char *) gni + (sizeof (gni->init) + sizeof (gni->networkInfo));
        memset(version_addr, 0, sizeof (globalNetworkInfo) - sizeof (gni->init) - sizeof (gni->networkInfo));
    }

    return (0);
}

//!
//! Clears a given globalNetworkInfo structure. This will free member's allocated memory and zero
//! out the structure itself.
//!
//! @param[in] gni a pointer to the global network information structure
//!
//! @return the result of the gni_iterate() call
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_clear(globalNetworkInfo * gni)
{
    return (gni_iterate(gni, GNI_ITERATE_FREE));
}

//!
//! Logs the content of a given globalNetworkInfo structure
//!
//! @param[in] gni a pointer to the global network information structure
//!
//! @return the result of the gni_iterate() call
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_print(globalNetworkInfo * gni)
{
    return (gni_iterate(gni, GNI_ITERATE_PRINT));
}

//!
//! Clears and free a given globalNetworkInfo structure.
//!
//! @param[in] gni a pointer to the global network information structure
//!
//! @return Always return 0
//!
//! @see gni_clear()
//!
//! @pre
//!
//! @post
//!
//! @note The caller should free the given pointer
//!
int gni_free(globalNetworkInfo * gni)
{
    if (!gni) {
        return (0);
    }
    gni_clear(gni);
    EUCA_FREE(gni);
    return (0);
}

//Maps the protocol number passed in, to the name 
static int map_proto_to_names(int proto_number, char *out_proto_name, int out_proto_len)
{
    struct protoent *proto = NULL;
    if (NULL == out_proto_name) {
        LOGERROR("Cannot map protocol number to name because arguments are null or not allocated enough buffers. Proto number=%d, out_proto_len=%d\n",
                proto_number, out_proto_len);
        return 1;
    }

    if (proto_number < 0 || proto_number > 255) {
        LOGERROR("Cannot map invalid protocol number: %d. Must be between 0 and 255 inclusive\n", proto_number);
        return 1;
    }

    //Explicitly map only tcp/udp/icmp
    if (TCP_PROTOCOL_NUMBER == proto_number ||
            UDP_PROTOCOL_NUMBER == proto_number ||
            ICMP_PROTOCOL_NUMBER == proto_number) {
        //Use libc to map number to name
        proto = getprotobynumber(proto_number);
    }
    if (NULL != proto) {
        //There is a name, use it
        if (NULL != proto->p_name && strlen(proto->p_name) > 0) {
            euca_strncpy(out_proto_name, proto->p_name, out_proto_len);
        }
    } else {
        //There is no name, just use the raw number
        snprintf(out_proto_name, out_proto_len, "%d", proto_number);
    }
    return 0;
}

//!
//! TODO: Define
//!
//! @param[in]  rulebuf a string containing the IP table rule to convert
//! @param[out] outrule a string containing the converted rule
//!
//! @return 0 on success or 1 on failure.
//!
//! @see
//!
//! @pre Both rulebuf and outrule MUST not be NULL
//!
//! @post \li uppon success the outrule contains the converted value
//!       \li uppon failure, outrule does not contain any valid data
//!       \li regardless of success or failure case, rulebuf will be modified by a strtok_r() call
//!
//! @note
//!
int ruleconvert(char *rulebuf, char *outrule)
{
    int ret = 0;
    //char proto[4]; //Protocol is always a 3-digit number in global network xml.
    int protocol_number = -1;
    int rc = EUCA_ERROR;
    char portrange[64], sourcecidr[64], icmptyperange[64], sourceowner[64], sourcegroup[64], newrule[4097], buf[2048];
    char proto[64]; //protocol name mapped for IPTABLES usage
    char *ptra = NULL, *toka = NULL, *idx = NULL;

    proto[0] = portrange[0] = sourcecidr[0] = icmptyperange[0] = newrule[0] = sourceowner[0] = sourcegroup[0] = '\0';

    if ((idx = strchr(rulebuf, '\n'))) {
        *idx = '\0';
    }

    toka = strtok_r(rulebuf, " ", &ptra);
    while (toka) {
        if (!strcmp(toka, "-P")) {
            toka = strtok_r(NULL, " ", &ptra);
            if (toka) {
                protocol_number = atoi(toka);
            }
        } else if (!strcmp(toka, "-p")) {
            toka = strtok_r(NULL, " ", &ptra);
            if (toka)
                snprintf(portrange, 64, "%s", toka);
            if ((idx = strchr(portrange, '-'))) {
                char minport[64], maxport[64];
                sscanf(portrange, "%[0-9]-%[0-9]", minport, maxport);
                if (!strcmp(minport, maxport)) {
                    snprintf(portrange, 64, "%s", minport);
                } else {
                    *idx = ':';
                }
            }
        } else if (!strcmp(toka, "-s")) {
            toka = strtok_r(NULL, " ", &ptra);
            if (toka)
                snprintf(sourcecidr, 64, "%s", toka);
            if (!strcmp(sourcecidr, "0.0.0.0/0")) {
                sourcecidr[0] = '\0';
            }
        } else if (!strcmp(toka, "-t")) {
            toka = strtok_r(NULL, " ", &ptra);
            if (toka)
                snprintf(icmptyperange, 64, "any");
        } else if (!strcmp(toka, "-o")) {
            toka = strtok_r(NULL, " ", &ptra);
            if (toka)
                snprintf(sourcegroup, 64, toka);
        } else if (!strcmp(toka, "-u")) {
            toka = strtok_r(NULL, " ", &ptra);
            if (toka)
                snprintf(sourceowner, 64, toka);
        }
        toka = strtok_r(NULL, " ", &ptra);
    }

    LOGTRACE("TOKENIZED RULE: PROTO: %d PORTRANGE: %s SOURCECIDR: %s ICMPTYPERANGE: %s SOURCEOWNER: %s SOURCEGROUP: %s\n", protocol_number, portrange, sourcecidr, icmptyperange,
            sourceowner, sourcegroup);

    // check if enough info is present to construct rule
    // Fix for EUCA-10031, no port range required. Ports should be limited and enforced at front-end
    // per AWS policy, not in the backend since IPTABLES doesn't care
    if (protocol_number >= 0) {
        //Handle protocol mapping first
        rc = map_proto_to_names(protocol_number, proto, 64);
        if (!rc && strlen(proto) > 0) {
            if (TCP_PROTOCOL_NUMBER == protocol_number || UDP_PROTOCOL_NUMBER == protocol_number) {
                //For tcp and udp add a module for port handling
                snprintf(buf, 2048, "-p %s -m %s ", proto, proto);
            } else {
                snprintf(buf, 2048, "-p %s ", proto);
            }
            strncat(newrule, buf, 2048);
        } else {
            LOGERROR("Error mapping protocol number %d to string for iptables rules\n", protocol_number);
            return 1;
        }

        if (strlen(sourcecidr)) {
            snprintf(buf, 2048, "-s %s ", sourcecidr);
            strncat(newrule, buf, 2048);
        }

        //Only allow port ranges for tcp and udp
        if ((TCP_PROTOCOL_NUMBER == protocol_number || UDP_PROTOCOL_NUMBER == protocol_number) && strlen(portrange)) {
            snprintf(buf, 2048, "--dport %s ", portrange);
            strncat(newrule, buf, 2048);
        }

        //Only allow icmp for proper icmp
        if (protocol_number == 1 && strlen(icmptyperange)) {
            snprintf(buf, 2048, "--icmp-type %s ", icmptyperange);
            strncat(newrule, buf, 2048);
        }

        while (newrule[strlen(newrule) - 1] == ' ') {
            newrule[strlen(newrule) - 1] = '\0';
        }

        snprintf(outrule, 2048, "%s", newrule);
        LOGTRACE("CONVERTED RULE: %s\n", outrule);
    } else {
        LOGWARN("not enough information in source rule to construct iptables rule: skipping\n");
        ret = 1;
    }

    return (ret);
}

//!
//! Creates an iptables rule using the source CIDR specified in the argument, and
//! based on the ingress rule entry in the argument.
//!
//! @param[in] scidr a string containing a CIDR to be used in the output iptables rule to match the source (can ba a single IP address).
//! If null, the source address within the ingress rule will be used.
//! @param[in] ingress_rule gni_rule structure containing an ingress rule.
//! @param[in] flags integer containing extra conditions that will be added to the output iptables rule.
//! If 0, no condition is added. If 1 the output iptables rule will allow traffic between VMs on the same NC (see EUCA-11083).
//! @param[out] outrule a string containing the converted rule. A buffer with at least 1024 chars is expected.
//!
//! @return 0 on success or 1 on failure.
//!
//! @see
//!
//! @pre ingress_rule and outrule pointers MUST not be NULL
//!
//! @post \li uppon success the outrule contains the converted iptables rule.
//!       \li uppon failure, outrule does not contain any valid data
//!
//! @note
//!
int ingress_gni_to_iptables_rule(char *scidr, gni_rule *ingress_rule, char *outrule, int flags) {
#define MAX_RULE_LEN     1024
#define MAX_NEWRULE_LEN  2049
    char newrule[MAX_NEWRULE_LEN], buf[MAX_RULE_LEN];
    char *strptr = NULL;
    struct protoent *proto_info = NULL;

    if (!ingress_rule || !outrule) {
        LOGERROR("Invalid pointer(s) to ingress_gni_rule and/or iptables rule buffer.\n");
        return 1;
    }

    // Check for protocol all (-1) - should not happen in non-VPC
    if (-1 != ingress_rule->protocol) {
        proto_info = getprotobynumber(ingress_rule->protocol);
        if (proto_info == NULL) {
            LOGWARN("Invalid protocol (%d) - cannot create iptables rule.", ingress_rule->protocol);
            return 1;
        }
    }

    newrule[0] = '\0';
    if (scidr) {
        strptr = scidr;
    } else {
        strptr = ingress_rule->cidr;
    }
    if (strptr && strlen(strptr)) {
        snprintf(buf, MAX_RULE_LEN, "-s %s ", strptr);
        strncat(newrule, buf, MAX_RULE_LEN);
    }
    switch (ingress_rule->protocol) {
        case 1: // ICMP
            snprintf(buf, MAX_RULE_LEN, "-p %s -m %s ", proto_info->p_name, proto_info->p_name);
            strncat(newrule, buf, MAX_RULE_LEN);
            if (ingress_rule->icmpType == -1) {
                snprintf(buf, MAX_RULE_LEN, "--icmp-type any ");
                strncat(newrule, buf, MAX_RULE_LEN);
            } else {
                snprintf(buf, MAX_RULE_LEN, "--icmp-type %d", ingress_rule->icmpType);
                strncat(newrule, buf, MAX_RULE_LEN);
                if (ingress_rule->icmpCode != -1) {
                    snprintf(buf, MAX_RULE_LEN, "/%d", ingress_rule->icmpCode);
                    strncat(newrule, buf, MAX_RULE_LEN);
                }
                snprintf(buf, MAX_RULE_LEN, " ");
                strncat(newrule, buf, MAX_RULE_LEN);
            }
            break;
        case 6: // TCP
        case 17: // UDP
            snprintf(buf, MAX_RULE_LEN, "-p %s -m %s ", proto_info->p_name, proto_info->p_name);
            strncat(newrule, buf, MAX_RULE_LEN);
            if (ingress_rule->fromPort) {
                snprintf(buf, MAX_RULE_LEN, "--dport %d", ingress_rule->fromPort);
                strncat(newrule, buf, MAX_RULE_LEN);
                if ((ingress_rule->toPort) && (ingress_rule->toPort > ingress_rule->fromPort)) {
                    snprintf(buf, MAX_RULE_LEN, ":%d", ingress_rule->toPort);
                    strncat(newrule, buf, MAX_RULE_LEN);
                }
                snprintf(buf, MAX_RULE_LEN, " ");
                strncat(newrule, buf, MAX_RULE_LEN);
            }
            break;
        default:
            // Protocols accepted by EC2 non-VPC are ICMP/TCP/UDP. Other protocols will default to numeric values on euca.
            // snprintf(buf, MAX_RULE_LEN, "-p %s ", proto_info->p_name);
            snprintf(buf, MAX_RULE_LEN, "-p %d ", proto_info->p_proto);
            strncat(newrule, buf, MAX_RULE_LEN);
            break;
    }

    switch (flags) {
        case 0: // no condition
            break;
        case 1: // Add condition to the rule to accept the packet if it would be SNATed (EDGE).
            snprintf(buf, MAX_RULE_LEN, "-m mark ! --mark 0x2a ");
            strncat(newrule, buf, MAX_RULE_LEN);
            break;
        case 2: // Add condition to the rule to accept the packet if it would be SNATed (MANAGED).
            snprintf(buf, MAX_RULE_LEN, "-m mark --mark 0x15 ");
            strncat(newrule, buf, MAX_RULE_LEN);
            break;
        case 4: // Add condition to the rule to accept the packet if it would NOT be SNATed (MANAGED).
            snprintf(buf, MAX_RULE_LEN, "-m mark ! --mark 0x15 ");
            strncat(newrule, buf, MAX_RULE_LEN);
            break;
        default:
            LOGINFO("Call with invalid flags: %d - ignored.\n", flags);
    }

    while (newrule[strlen(newrule) - 1] == ' ') {
        newrule[strlen(newrule) - 1] = '\0';
    }

    snprintf(outrule, MAX_RULE_LEN, "%s", newrule);
    LOGTRACE("IPTABLES RULE: %s\n", outrule);

    return 0;
}

//!
//! Clears a gni_cluster structure. This will free member's allocated memory and zero
//! out the structure itself.
//!
//! @param[in] cluster a pointer to the structure to clear
//!
//! @return This function always returns 0
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_cluster_clear(gni_cluster * cluster)
{
    if (!cluster) {
        return (0);
    }

    EUCA_FREE(cluster->private_ips);

    bzero(cluster, sizeof (gni_cluster));

    return (0);
}

//!
//! Clears a gni_node structure. This will free member's allocated memory and zero
//! out the structure itself.
//!
//! @param[in] node a pointer to the structure to clear
//!
//! @return This function always returns 0
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_node_clear(gni_node * node)
{
    if (!node) {
        return (0);
    }

    EUCA_FREE(node->instance_names);

    bzero(node, sizeof (gni_node));

    return (0);
}

//!
//! Clears a gni_instance structure. This will free member's allocated memory and zero
//! out the structure itself.
//!
//! @param[in] instance a pointer to the structure to clear
//!
//! @return This function always returns 0
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_instance_clear(gni_instance * instance)
{
    if (!instance) {
        return (0);
    }

    EUCA_FREE(instance->secgroup_names);
    //EUCA_FREE(instance->interface_names);
    EUCA_FREE(instance->gnisgs);
    EUCA_FREE(instance->interfaces);

    bzero(instance, sizeof (gni_instance));

    return (0);
}

//!
//! Clears a gni_secgroup structure. This will free member's allocated memory and zero
//! out the structure itself.
//!
//! @param[in] secgroup a pointer to the structure to clear
//!
//! @return This function always returns 0
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_secgroup_clear(gni_secgroup * secgroup)
{
    if (!secgroup) {
        return (0);
    }

    EUCA_FREE(secgroup->ingress_rules);
    EUCA_FREE(secgroup->egress_rules);
    EUCA_FREE(secgroup->grouprules);
    EUCA_FREE(secgroup->instances);
    EUCA_FREE(secgroup->interfaces);

    bzero(secgroup, sizeof (gni_secgroup));

    return (0);
}

//!
//! Zero out a VPC structure
//!
//! @param[in] vpc a pointer to the GNI VPC structure to reset
//!
//! @return Always return 0
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_vpc_clear(gni_vpc * vpc)
{
    int i = 0;
    if (!vpc) {
        return (0);
    }

    for (i = 0; i < vpc->max_subnets; i++) {
        EUCA_FREE(vpc->subnets[i].interfaces);
    }
    EUCA_FREE(vpc->subnets);
    for (i = 0; i < vpc->max_networkAcls; i++) {
        EUCA_FREE(vpc->networkAcls[i].ingress);
        EUCA_FREE(vpc->networkAcls[i].egress);
    }
    EUCA_FREE(vpc->networkAcls);
    for (i = 0; i < vpc->max_routeTables; i++) {
        EUCA_FREE(vpc->routeTables[i].entries);
    }
    EUCA_FREE(vpc->routeTables);
    EUCA_FREE(vpc->natGateways);
    EUCA_FREE(vpc->internetGatewayNames);
    EUCA_FREE(vpc->interfaces);

    bzero(vpc, sizeof (gni_vpc));

    return (0);
}

/**
 * Zero out a dhcp_os structure
 * @param dhcpos [in] a pointer to the GNI dhcp_os to reset
 * @return Always return 0
 */
int gni_dhcpos_clear(gni_dhcp_os *dhcpos) {
    if (!dhcpos) {
        return (0);
    }

    EUCA_FREE(dhcpos->dns);
    EUCA_FREE(dhcpos->domains);
    EUCA_FREE(dhcpos->netbios_ns);
    EUCA_FREE(dhcpos->ntp);

    bzero(dhcpos, sizeof (gni_dhcp_os));

    return (0);
}

/**
 * Searches and returns the VPC that matches the name in the argument, if found.
 * @param gni [in] globalNetworkInfo structure that holds the network state to search.
 * @param name [in] name of the VPC of interest.
 * @param startidx [i/o] start index to the array of VPCs in gni. If a matching VPC
 * is found, startidx is updated to aid subsequent searches (ordering of objects in
 * GNI is assumed).
 * @return pointer to the gni_vpc of interest when found. NULL otherwise.
 */
gni_vpc *gni_get_vpc(globalNetworkInfo *gni, char *name, int *startidx) {
    gni_vpc *vpcs = NULL;
    int start = 0;

    if ((gni == NULL) || (name == NULL)) {
        return NULL;
    }
    if (startidx) {
        start = *startidx;
    }
    vpcs = gni->vpcs;
    for (int i = start; i < gni->max_vpcs; i++) {
        if (!strcmp(name, vpcs[i].name)) {
            if (startidx) {
                *startidx = i + 1;
            }
            return &(vpcs[i]);
        }
    }
    return (NULL);
}

/**
 * Searches and returns the VPC subnet that matches the name in the argument, if found.
 * @param vpc [in] gni_vpc that contains the subnet to search.
 * @param name [in] name of the VPC subnet of interest.
 * @param startidx [i/o] start index to the array of VPC subnets in gni. If a matching VPC
 * subnet is found, startidx is updated to aid subsequent searches (ordering of objects in
 * GNI is assumed).
 * @return pointer to the gni_vpcsubnet of interest when found. NULL otherwise.
 */
gni_vpcsubnet *gni_get_vpcsubnet(gni_vpc *vpc, char *name, int *startidx) {
    gni_vpcsubnet *vpcsubnets = NULL;
    int start = 0;

    if ((vpc == NULL) || (name == NULL)) {
        return NULL;
    }
    if (startidx) {
        start = *startidx;
    }
    vpcsubnets = vpc->subnets;
    for (int i = start; i < vpc->max_subnets; i++) {
        if (!strcmp(name, vpcsubnets[i].name)) {
            if (startidx) {
                *startidx = i + 1;
            }
            return &(vpcsubnets[i]);
        }
    }
    return (NULL);
}

/**
 * Searches and returns the VPC subnet interface that matches the name in the argument, if found.
 * @param vpcsubnet [in] gni_vpcsubnet structure that contains the interface to search.
 * @param name [in] name of the interface of interest.
 * @param startidx [i/o] start index to the array of VPC subnets interfaces in gni. If a matching VPC
 * subnet interface is found, startidx is updated to aid subsequent searches (ordering of objects in
 * GNI is assumed).
 * @return pointer to the gni_vpcsubnet of interest when found. NULL otherwise.
 */
gni_instance *gni_get_interface(gni_vpcsubnet *vpcsubnet, char *name, int *startidx) {
    gni_instance **interfaces = NULL;
    int start = 0;

    if ((vpcsubnet == NULL) || (name == NULL)) {
        return NULL;
    }
    if (startidx) {
        start = *startidx;
    }
    interfaces = vpcsubnet->interfaces;
    for (int i = start; i < vpcsubnet->max_interfaces; i++) {
        if (!strcmp(name, interfaces[i]->name)) {
            if (startidx) {
                *startidx = i + 1;
            }
            return (interfaces[i]);
        }
    }
    return (NULL);
}

/**
 * Searches and returns the VPC natgateway that matches the name in the argument, if found.
 * @param vpc [in] gni_vpc that contains the natgateway to search.
 * @param name [in] name of the VPC natgateway of interest.
 * @param startidx [i/o] start index to the array of VPC natgateways in gni. If a matching VPC
 * natgateway is found, startidx is updated to aid subsequent searches (ordering of objects in
 * GNI is assumed).
 * @return pointer to the gni_vpcnatgateway of interest when found. NULL otherwise.
 */
gni_nat_gateway *gni_get_natgateway(gni_vpc *vpc, char *name, int *startidx) {
    gni_nat_gateway *vpcnatgateways = NULL;
    int start = 0;

    if ((vpc == NULL) || (name == NULL)) {
        return NULL;
    }
    if (startidx) {
        start = *startidx;
    }
    vpcnatgateways = vpc->natGateways;
    for (int i = start; i < vpc->max_natGateways; i++) {
        if (!strcmp(name, vpcnatgateways[i].name)) {
            if (startidx) {
                *startidx = i + 1;
            }
            return &(vpcnatgateways[i]);
        }
    }
    return (NULL);
}

/**
 * Searches and returns the VPC routetable that matches the name in the argument, if found.
 * @param vpc [in] gni_vpc that contains the routetable to search.
 * @param name [in] name of the VPC routetable of interest.
 * @param startidx [i/o] start index to the array of VPC routetables in gni. If a matching VPC
 * routetable is found, startidx is updated to aid subsequent searches (ordering of objects in
 * GNI is assumed).
 * @return pointer to the gni_vpcroutetable of interest when found. NULL otherwise.
 */
gni_route_table *gni_get_routetable(gni_vpc *vpc, char *name, int *startidx) {
    gni_route_table *vpcroutetables = NULL;
    int start = 0;

    if ((vpc == NULL) || (name == NULL)) {
        return NULL;
    }
    if (startidx) {
        start = *startidx;
    }
    vpcroutetables = vpc->routeTables;
    for (int i = start; i < vpc->max_routeTables; i++) {
        if (!strcmp(name, vpcroutetables[i].name)) {
            if (startidx) {
                *startidx = i + 1;
            }
            return &(vpcroutetables[i]);
        }
    }
    return (NULL);
}

/**
 * Searches and returns the security group that matches the name in the argument, if found.
 * @param gni [in] globalNetworkInfo structure that holds the network state to search.
 * @param name [in] name of the security group of interest.
 * @param startidx [i/o] start index to the array of SGs in gni. If a matching security group
 * is found, startidx is updated to aid subsequent searches (ordering of objects in
 * GNI is assumed).
 * @return pointer to the gni_secgroup of interest when found. NULL otherwise.
 */
gni_secgroup *gni_get_secgroup(globalNetworkInfo *gni, char *name, int *startidx) {
    gni_secgroup *secgroups = NULL;
    int start = 0;

    if ((gni == NULL) || (name == NULL)) {
        return NULL;
    }
    if (startidx) {
        start = *startidx;
    }
    secgroups = gni->secgroups;
    for (int i = start; i < gni->max_secgroups; i++) {
        if (!strcmp(name, secgroups[i].name)) {
            if (startidx) {
                *startidx = i + 1;
            }
            return &(secgroups[i]);
        }
    }
    return (NULL);
}

/**
 * Searches and returns the VPC networkacl that matches the name in the argument, if found.
 * @param vpc [in] gni_vpc that contains the networkacl to search.
 * @param name [in] name of the VPC networkacl of interest.
 * @param startidx [i/o] start index to the array of VPC networkacls in gni. If a matching VPC
 * networkacl is found, startidx is updated to aid subsequent searches (ordering of objects in
 * GNI is assumed).
 * @return pointer to the gni_network_acl of interest when found. NULL otherwise.
 */
gni_network_acl *gni_get_networkacl(gni_vpc *vpc, char *name, int *startidx) {
    gni_network_acl *netacls = NULL;
    int start = 0;

    if ((vpc == NULL) || (name == NULL)) {
        return NULL;
    }
    if (startidx) {
        start = *startidx;
    }
    netacls = vpc->networkAcls;
    for (int i = start; i < vpc->max_networkAcls; i++) {
        if (!strcmp(name, netacls[i].name)) {
            if (startidx) {
                *startidx = i + 1;
            }
            return &(netacls[i]);
        }
    }
    return (NULL);
}

/**
 * Searches and returns the DHCP Option Set that matches the name in the argument, if found.
 * @param gni [in] globalNetworkInfo structure that holds the network state to search.
 * @param name [in] name of the DHCP Option Set of interest.
 * @param startidx [i/o] start index to the array of DHCPOS in gni. If a matching dhcp_os
 * is found, startidx is updated to aid subsequent searches (ordering of objects in
 * GNI is assumed).
 * @return pointer to the gni_dhcp_os of interest when found. NULL otherwise.
 */
gni_dhcp_os *gni_get_dhcpos(globalNetworkInfo *gni, char *name, int *startidx) {
    gni_dhcp_os *dhcpos = NULL;
    int start = 0;

    if ((gni == NULL) || (name == NULL)) {
        return NULL;
    }
    if (startidx) {
        start = *startidx;
    }
    dhcpos = gni->dhcpos;
    for (int i = start; i < gni->max_dhcpos; i++) {
        if (!strcmp(name, dhcpos[i].name)) {
            if (startidx) {
                *startidx = i + 1;
            }
            return &(dhcpos[i]);
        }
    }
    return (NULL);
}

/**
 * Validates a given globalNetworkInfo structure and its content
 *
 * @param gni [in] a pointer to the Global Network Information structure to validate
 *
 * @return 0 if the structure is valid or 1 if it isn't
 *
 * @see gni_subnet_validate(), gni_cluster_validate(), gni_instance_validate(), gni_secgroup_validate()
 */
int gni_validate(globalNetworkInfo * gni) {
    int i = 0;
    int j = 0;

    // this is going to be messy, until we get XML validation in place
    if (!gni) {
        LOGERROR("invalid input\n");
        return (1);
    }
    // GNI should be initialized... but check just in case
    if (!gni->init) {
        LOGWARN("invalid input: gni is not initialized yet\n");
        return (1);
    }
    // Make sure we have a valid mode
    if (gni_netmode_validate(gni->sMode)) {
        return (1);
    }

    LOGTRACE("Validating XML for '%s' networking mode.\n", gni->sMode);

    // We need to know about which CLC is the enabled one. 0.0.0.0 means we don't know
    if (gni->enabledCLCIp == 0) {
        LOGWARN("no enabled CLC IP set: cannot validate XML\n");
        return (1);
    }
    // We should have some instance Domain Name information
    if (!strlen(gni->instanceDNSDomain)) {
        LOGWARN("no instanceDNSDomain set: cannot validate XML\n");
        return (1);
    }
    // We should have some instance Domain Name Server information
    if (!gni->max_instanceDNSServers || !gni->instanceDNSServers) {
        LOGWARN("no instanceDNSServers set: cannot validate XML\n");
        return (1);
    }
    // Validate that we don't have a corrupted list. All 0.0.0.0 addresses are invalid
    for (i = 0; i < gni->max_instanceDNSServers; i++) {
        if (gni->instanceDNSServers[i] == 0) {
            LOGWARN("empty instanceDNSServer set at idx %d: cannot validate XML\n", i);
            return (1);
        }
    }
    // We should have some public IPs set if not, we'll just warn the user
    // public IPs is irrelevant in VPCMIDO (see publicNetworkCidr in mido section)
    if (!IS_NETMODE_VPCMIDO(gni)) {
        if (!gni->max_public_ips || !gni->public_ips) {
            LOGTRACE("no public_ips set\n");
        } else {
            // Make sure none of the public IPs is 0.0.0.0
            for (i = 0; i < gni->max_public_ips; i++) {
                if (gni->public_ips[i] == 0) {
                    LOGWARN("empty public_ip set at idx %d: cannot validate XML\n", i);
                    return (1);
                }
            }
        }
    }

    // Now we have different behavior between managed and managed-novlan
    if (IS_NETMODE_MANAGED(gni) || IS_NETMODE_MANAGED_NOVLAN(gni)) {
        // We must have 1 managed subnet declaration
        if ((gni->max_managedSubnets != 1) || !gni->managedSubnet) {
            LOGWARN("invalid number of managed subnets set '%d'.\n", gni->max_managedSubnets);
            return (1);
        }
        // Validate our managed subnet
        if (gni_managed_subnet_validate(gni->managedSubnet)) {
            LOGWARN("invalid managed subnet: cannot validate XML\n");
            return (1);
        }
    }
    if (IS_NETMODE_EDGE(gni)) {
        //
        // This is for the EDGE case. We should have a valid list of subnets and our clusters
        // should be valid for an EDGE mode
        //
        if (!gni->max_subnets || !gni->subnets) {
            LOGTRACE("no subnets set\n");
        } else {
            for (i = 0; i < gni->max_subnets; i++) {
                if (gni_subnet_validate(&(gni->subnets[i]))) {
                    LOGWARN("invalid subnets set at idx %d: cannot validate XML\n", i);
                    return (1);
                }
            }
        }
    }

    // Validate the clusters
    if (!gni->max_clusters || !gni->clusters) {
        LOGTRACE("no clusters set\n");
    } else {
        for (i = 0; i < gni->max_clusters; i++) {
            if (gni_cluster_validate(&(gni->clusters[i]), gni->nmCode)) {
                LOGWARN("invalid clusters set at idx %d: cannot validate XML\n", i);
                return (1);
            }
        }
    }

    // If we have any instance provided, validate them
    if (!gni->max_instances || !gni->instances) {
        LOGTRACE("no instances set\n");
    } else {
        for (i = 0; i < gni->max_instances; i++) {
            if (gni_instance_validate(gni->instances[i])) {
                LOGWARN("invalid instances set at idx %d: cannot validate XML\n", i);
                return (1);
            }
        }
    }

    // Validate interfaces
    if (!gni->max_ifs || !gni->ifs) {
        LOGTRACE("no interfaces set\n");
    } else {
        for (i = 0; i < gni->max_ifs; i++) {
            if (gni_interface_validate(gni->ifs[i])) {
                LOGWARN("invalid instances set at idx %d: cannot validate XML\n", i);
                return (1);
            }
        }
    }

    // If we have any security group provided, we should be able to validate them
    if (!gni->max_secgroups || !gni->secgroups) {
        LOGTRACE("no secgroups set\n");
    } else {
        for (i = 0; i < gni->max_secgroups; i++) {
            if (gni_secgroup_validate(&(gni->secgroups[i]))) {
                LOGWARN("invalid secgroups set at idx %d: cannot validate XML\n", i);
                return (1);
            }
        }
    }

    // Validate VPCMIDO elements
    if (IS_NETMODE_VPCMIDO(gni)) {
        // Validate VPCs
        for (i = 0; i < gni->max_vpcs; i++) {
            if (gni_vpc_validate(&(gni->vpcs[i]))) {
                LOGWARN("invalid vpc set at idx %d\n", i);
                return (1);
            }
            // Validate subnets
            for (j = 0; j < gni->vpcs[i].max_subnets; j++) {
                if (gni_vpcsubnet_validate(&(gni->vpcs[i].subnets[j]))) {
                    LOGWARN("invalid vpcsubnet set at idx %d\n", i);
                    return (1);
                }
            }
            // Validate NAT gateways
            for (j = 0; j < gni->vpcs[i].max_natGateways; j++) {
                if (gni_nat_gateway_validate(&(gni->vpcs[i].natGateways[j]))) {
                    LOGWARN("invalid NAT gateway set at idx %d\n", i);
                    return (1);
                }
            }
            // Validate route tables
            for (j = 0; j < gni->vpcs[i].max_routeTables; j++) {
                if (gni_route_table_validate(&(gni->vpcs[i].routeTables[j]))) {
                    LOGWARN("invalid route table set at idx %d\n", i);
                    return (1);
                }
            }
            // Validate network acls
            for (j = 0; j < gni->vpcs[i].max_networkAcls; j++) {
                if (gni_networkacl_validate(&(gni->vpcs[i].networkAcls[j]))) {
                    LOGWARN("invalid network ACL set at idx %d\n", i);
                    return (1);
                }
            }
        }
    }
    return (0);
}

//!
//! Validate a networking mode provided in the GNI message. The only supported networking
//! mode strings are: EDGE, MANAGED and MANAGED-NOVLAN
//!
//! @param[in] psMode a string pointer to the network mode to validate
//!
//! @return 0 if the mode is valid or 1 if the mode isn't
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_netmode_validate(const char *psMode)
{
    int i = 0;

    if (!psMode) {
        LOGERROR("invalid input\n");
        return (1);
    }
    // Do we know anything about this mode?
    for (i = 0; asNetModes[i] != NULL; i++) {
        if (!strcmp(psMode, asNetModes[i])) {
            return (0);
        }
    }

    if (strlen(psMode) > 0) {
        LOGDEBUG("invalid network mode '%s'\n", psMode);
    } else {
        LOGTRACE("network mode is empty.\n");
    }
    return (1);
}

//!
//! Validate a gni_subnet structure content
//!
//! @param[in] pSubnet a pointer to the subnet structure to validate
//!
//! @return 0 if the structure is valid or 1 if the structure isn't
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_subnet_validate(gni_subnet * pSubnet)
{
    if (!pSubnet) {
        LOGERROR("invalid input\n");
        return (1);
    }
    // If any of the subnet, netmask or gateway is 0.0.0.0, this is invalid
    if ((pSubnet->subnet == 0) || (pSubnet->netmask == 0) || (pSubnet->gateway == 0)) {
        LOGWARN("invalid subnet: subnet=%d netmask=%d gateway=%d\n", pSubnet->subnet, pSubnet->netmask, pSubnet->gateway);
        return (1);
    }

    return (0);
}

//!
//! Validate a gni_subnet structure content
//!
//! @param[in] pSubnet a pointer to the subnet structure to validate
//!
//! @return 0 if the structure is valid or 1 if the structure isn't
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_managed_subnet_validate(gni_managedsubnet * pSubnet)
{
    // Make sure we didn't get a NULL pointer
    if (!pSubnet) {
        LOGERROR("invalid input\n");
        return (1);
    }
    // If any of the subnet or netmask is 0.0.0.0, this is invalid
    if ((pSubnet->subnet == 0) || (pSubnet->netmask == 0)) {
        LOGWARN("invalid managed subnet: subnet=%d netmask=%d\n", pSubnet->subnet, pSubnet->netmask);
        return (1);
    }
    // If the segment size is less than 16 or not a power of 2 than we have a problem
    if ((pSubnet->segmentSize < 16) || ((pSubnet->segmentSize & (pSubnet->segmentSize - 1)) != 0)) {
        LOGWARN("invalid managed subnet: segmentSize=%d\n", pSubnet->segmentSize);
        return (1);
    }
    // If minVlan is less than MIN_VLAN_EUCA or greater than MAX_VLAN_EUCA, we have a problem
    if ((pSubnet->minVlan < MIN_VLAN_EUCA) || (pSubnet->minVlan > MAX_VLAN_EUCA)) {
        LOGWARN("invalid managed subnet: minVlan=%d\n", pSubnet->minVlan);
        return (1);
    }
    // If maxVlan is less than MIN_VLAN_EUCA or greater than MAX_VLAN_EUCA, we have a problem
    if ((pSubnet->maxVlan < MIN_VLAN_EUCA) || (pSubnet->maxVlan > MAX_VLAN_EUCA)) {
        LOGWARN("invalid managed subnet: maxVlan=%d\n", pSubnet->maxVlan);
        return (1);
    }
    // If minVlan is greater than maxVlan, we have a problem too!!
    if (pSubnet->minVlan > pSubnet->maxVlan) {
        LOGWARN("invalid managed subnet: minVlan=%d, maxVlan=%d\n", pSubnet->minVlan, pSubnet->maxVlan);
        return (1);
    }
    return (0);
}

/**
 * Validate a gni_cluster structure content
 *
 * @param cluster [in] a pointer to the cluster structure to validate
 * @param nmode [in] valid euca_netmode enumeration value
 *
 * @return 0 if the structure is valid or 1 if it isn't
 *
 * @see gni_node_validate()
 */
int gni_cluster_validate(gni_cluster * cluster, euca_netmode nmode) {
    int i = 0;

    // Make sure our pointer is valid
    if (!cluster) {
        LOGERROR("invalid input\n");
        return (1);
    }
    // We must have a name
    if (!strlen(cluster->name)) {
        LOGWARN("no cluster name\n");
        return (1);
    }
    // The enabled CC IP must not be 0.0.0.0
    if (cluster->enabledCCIp == 0) {
        LOGWARN("cluster %s: no enabledCCIp\n", cluster->name);
        return (1);
    }
    // We must be provided with a MAC prefix
    if (strlen(cluster->macPrefix) == 0) {
        LOGWARN("cluster %s: no macPrefix\n", cluster->name);
        return (1);
    }
    //
    // For EDGE, we need to validate the subnet and the private IPs which
    // aren't provided in MANAGED mode
    //
    if (nmode == NM_EDGE) {
        // Validate the given private subnet
        if (gni_subnet_validate(&(cluster->private_subnet))) {
            LOGWARN("cluster %s: invalid cluster private_subnet\n", cluster->name);
            return (1);
        }
        // Validate the list of private IPs. We must have some.
        if (!cluster->max_private_ips || !cluster->private_ips) {
            LOGWARN("cluster %s: no private_ips\n", cluster->name);
            return (1);
        } else {
            // None of our private IPs should be 0.0.0.0
            for (i = 0; i < cluster->max_private_ips; i++) {
                if (cluster->private_ips[i] == 0) {
                    LOGWARN("cluster %s: empty private_ips set at idx %d\n", cluster->name, i);
                    return (1);
                }
            }
        }
    }
    // Do we have some nodes for this cluster?
    if (!cluster->max_nodes || !cluster->nodes) {
        LOGWARN("cluster %s: no nodes set\n", cluster->name);
    } else {
        // Validate each nodes
        for (i = 0; i < cluster->max_nodes; i++) {
            if (gni_node_validate(&(cluster->nodes[i]))) {
                LOGWARN("cluster %s: invalid nodes set at idx %d\n", cluster->name, i);
                return (1);
            }
        }
    }

    return (0);
}

//!
//! Validate a gni_node structure content
//!
//! @param[in] node a pointer to the node structure to validate
//!
//! @return 0 if the structure is valid or 1 if it isn't
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_node_validate(gni_node * node)
{
    int i;

    if (!node) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (!strlen(node->name)) {
        LOGWARN("no node name\n");
        return (1);
    }

    if (!node->max_instance_names || !node->instance_names) {
    } else {
        for (i = 0; i < node->max_instance_names; i++) {
            if (!strlen(node->instance_names[i].name)) {
                LOGWARN("node %s: empty instance_names set at idx %d\n", node->name, i);
                return (1);
            }
        }
    }

    return (0);
}

//!
//! Validates a given instance_interface structure content for a valid instance
//! description
//!
//! @param[in] instance a pointer to the instance_interface structure to validate
//!
//! @return 0 if the structure is valid or 1 if it isn't
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_instance_validate(gni_instance * instance)
{
    int i;

    if (!instance) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (!strlen(instance->name)) {
        LOGWARN("no instance name\n");
        return (1);
    }

    if (!strlen(instance->accountId)) {
        LOGWARN("instance %s: no accountId\n", instance->name);
        return (1);
    }

    if (!maczero(instance->macAddress)) {
        LOGWARN("instance %s: no macAddress\n", instance->name);
    }

    if (!instance->publicIp) {
        LOGTRACE("instance %s: no publicIp set (ignore if instance was run with private only addressing)\n", instance->name);
    }

    if (!instance->privateIp) {
        LOGWARN("instance %s: no privateIp\n", instance->name);
        return (1);
    }

    if (!instance->max_secgroup_names || !instance->secgroup_names) {
        LOGWARN("instance %s: no secgroups\n", instance->name);
        return (1);
    } else {
        for (i = 0; i < instance->max_secgroup_names; i++) {
            if (!strlen(instance->secgroup_names[i].name)) {
                LOGWARN("instance %s: empty secgroup_names set at idx %d\n", instance->name, i);
                return (1);
            }
        }
    }

    //gni_instance_interface_print(instance, EUCA_LOG_TRACE);
    return (0);
}

//!
//! Validates a given gni_instance_interface structure content for a valid interface
//! description.
//!
//! @param[in] interface a pointer to the instance_interface structure to validate
//!
//! @return 0 if the structure is valid or 1 if it isn't
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_interface_validate(gni_instance *interface)
{
    int i;

    if (!interface) {
        LOGERROR("Invalid argument: NULL\n");
        return (1);
    }

    if (!strlen(interface->name)) {
        LOGWARN("no instance name\n");
        return (1);
    }

    if (!strlen(interface->accountId)) {
        LOGWARN("instance %s: no accountId\n", interface->name);
        return (1);
    }

    if (!maczero(interface->macAddress)) {
        LOGWARN("instance %s: no macAddress\n", interface->name);
    }

    if (!interface->publicIp) {
        LOGTRACE("instance %s: no publicIp set (ignore if instance was run with private only addressing)\n", interface->name);
    }

    if (!interface->privateIp) {
        LOGWARN("instance %s: no privateIp\n", interface->name);
        return (1);
    }

    if (!interface->max_secgroup_names || !interface->secgroup_names) {
        LOGTRACE("instance %s: no secgroups\n", interface->name);
    } else {
        for (i = 0; i < interface->max_secgroup_names; i++) {
            if (!strlen(interface->secgroup_names[i].name)) {
                LOGWARN("instance %s: empty secgroup_names set at idx %d\n", interface->name, i);
                return (1);
            }
        }
    }

    // Validate properties specific to interfaces
    // TODO - validate srcdestcheck and deviceidx

    //gni_instance_interface_print(interface, EUCA_LOG_TRACE);
    return (0);
}

//!
//! Validates a given gni_secgroup structure content
//!
//! @param[in] secgroup a pointer to the security group structure to validate
//!
//! @return 0 if the structure is valid and 1 if the structure isn't
//!
//! @see gni_secgroup
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int gni_secgroup_validate(gni_secgroup * secgroup)
{
    int i;

    if (!secgroup) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (!strlen(secgroup->name)) {
        LOGWARN("no secgroup name\n");
        return (1);
    }

    if (!strlen(secgroup->accountId)) {
        LOGWARN("secgroup %s: no accountId\n", secgroup->name);
        return (1);
    }

    if (!secgroup->max_grouprules || !secgroup->grouprules) {
        LOGTRACE("secgroup %s: no secgroup rules\n", secgroup->name);
    } else {
        for (i = 0; i < secgroup->max_grouprules; i++) {
            if (!strlen(secgroup->grouprules[i].name)) {
                LOGWARN("secgroup %s: empty grouprules set at idx %d\n", secgroup->name, i);
                return (1);
            }
        }
    }

    if (!secgroup->max_instances || !secgroup->instances) {
        LOGTRACE("secgroup %s: no instances\n", secgroup->name);
    } else {
        for (i = 0; i < secgroup->max_instances; i++) {
            if (!strlen(secgroup->instances[i]->name)) {
                LOGWARN("secgroup %s: empty instance_name set at idx %d\n", secgroup->name, i);
                return (1);
            }
        }
    }

    if (!secgroup->max_interfaces || !secgroup->interfaces) {
        LOGTRACE("secgroup %s: no interfaces\n", secgroup->name);
    } else {
        for (i = 0; i < secgroup->max_interfaces; i++) {
            if (!strlen(secgroup->interfaces[i]->name)) {
                LOGWARN("secgroup %s: empty interface_name set at idx %d\n", secgroup->name, i);
                return (1);
            }
        }
    }

    //gni_sg_print(secgroup, EUCA_LOG_TRACE);
    return (0);
}

/**
 * Validates a given gni_vpc structure content
 *
 * @param vpc [in] a pointer to the vpc structure to validate
 *
 * @return 0 if the structure is valid and 1 if the structure isn't
 *
 */
int gni_vpc_validate(gni_vpc *vpc) {
    if (!vpc) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (!strlen(vpc->name)) {
        LOGWARN("no vpc name\n");
        return (1);
    }

    if (!strlen(vpc->accountId)) {
        LOGWARN("vpc %s: no accountId\n", vpc->name);
        return (1);
    }

    return (0);
}

/**
 * Validates a given gni_vpc structure content
 *
 * @param vpcsubnet [in] a pointer to the vpcsubnet structure to validate
 *
 * @return 0 if the structure is valid and 1 if the structure isn't
 *
 */
int gni_vpcsubnet_validate(gni_vpcsubnet *vpcsubnet) {
    if (!vpcsubnet) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (!strlen(vpcsubnet->name)) {
        LOGWARN("no vpc name\n");
        return (1);
    }

    if (!strlen(vpcsubnet->accountId)) {
        LOGWARN("vpc %s: no accountId\n", vpcsubnet->name);
        return (1);
    }

    return (0);
}

/**
 * Validates a given gni_vpc structure content
 *
 * @param natg [in] a pointer to the nat_gateway structure to validate
 *
 * @return 0 if the structure is valid and 1 if the structure isn't
 *
 */
int gni_nat_gateway_validate(gni_nat_gateway *natg) {
    if (!natg) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (!strlen(natg->name)) {
        LOGWARN("no natg name\n");
        return (1);
    }

    if (!strlen(natg->accountId)) {
        LOGWARN("natg %s: no accountId\n", natg->name);
        return (1);
    }

    if (!maczero(natg->macAddress)) {
        LOGWARN("natg %s: no macAddress\n", natg->name);
    }

    if (!natg->publicIp) {
        LOGTRACE("natg %s: no publicIp set\n", natg->name);
    }

    if (!natg->privateIp) {
        LOGWARN("natg %s: no privateIp\n", natg->name);
        return (1);
    }

    if (!strlen(natg->vpc)) {
        LOGWARN("natg %s: no vpc\n", natg->name);
        return (1);
    }

    if (!strlen(natg->subnet)) {
        LOGWARN("natg %s: no vpc subnet\n", natg->name);
        return (1);
    }

    return (0);
}

/**
 * Validates a given route_table structure content
 *
 * @param rtable [in] a pointer to the route_table structure to validate
 *
 * @return 0 if the structure is valid and 1 if the structure isn't
 *
 */
int gni_route_table_validate(gni_route_table *rtable) {
    if (!rtable) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (!strlen(rtable->name)) {
        LOGWARN("no route table name\n");
        return (1);
    }

    if (!strlen(rtable->accountId)) {
        LOGWARN("route table %s: no accountId\n", rtable->name);
        return (1);
    }

    for (int i = 0; i < rtable->max_entries; i++) {
        if (!strlen(rtable->entries[i].destCidr) || !strlen(rtable->entries[i].target)) {
            LOGWARN("route table %s: invalid route entry at idx %d\n", rtable->name, i);
            return (1);
        }
    }
    return (0);
}

/**
 * Validates a given gni_network_acl structure content
 *
 * @param acl [in] a pointer to the acl structure to validate
 *
 * @return 0 if the structure is valid and 1 if the structure isn't
 *
 */
int gni_networkacl_validate(gni_network_acl *acl) {
    if (!acl) {
        LOGERROR("invalid input\n");
        return (1);
    }

    if (!strlen(acl->name)) {
        LOGWARN("no network acl name\n");
        return (1);
    }

    if (!strlen(acl->accountId)) {
        LOGWARN("network acl %s: no accountId\n", acl->name);
        return (1);
    }

    for (int i = 0; i < acl->max_ingress; i++) {
        gni_acl_entry *entry = &(acl->ingress[i]);
        if (entry->number == 0) {
            LOGWARN("network acl %s: invalid ingress entry %d\n", acl->name, entry->number);
            return (1);
        }
        if (!strlen(entry->cidr)) {
            LOGWARN("network acl %s: invalid CIDR at entry %d\n", acl->name, entry->number);
            return (1);
        }
        if (entry->protocol == 0) {
            LOGWARN("network acl %s: invalid protocol at entry %d\n", acl->name, entry->number);
            return (1);
        }
    }

    for (int i = 0; i < acl->max_egress; i++) {
        gni_acl_entry *entry = &(acl->egress[i]);
        if (entry->number == 0) {
            LOGWARN("network acl %s: invalid egress entry %d\n", acl->name, entry->number);
            return (1);
        }
        if (!strlen(entry->cidr)) {
            LOGWARN("network acl %s: invalid CIDR at entry %d\n", acl->name, entry->number);
            return (1);
        }
        if (entry->protocol == 0) {
            LOGWARN("network acl %s: invalid protocol at entry %d\n", acl->name, entry->number);
            return (1);
        }
    }
    return (0);
}

/**
 * Logs the contents of an instance_interface structure.
 * @param inst [in] instance_interface of interest.
 * @param loglevel [in] valid value from log_level_e enumeration.
 */
void gni_instance_interface_print(gni_instance *inst, int loglevel) {
    char *mac = NULL;
    char *pubip = NULL;
    char *privip = NULL;
    hex2mac(inst->macAddress, &mac);
    pubip = hex2dot(inst->publicIp);
    privip = hex2dot(inst->privateIp);
    int i = 0;

    if (!inst) {
        EUCALOG(loglevel, "Invalid argument: NULL.\n");
        return;
    }
    EUCALOG(loglevel, "------ name = %s -----\n", inst->name);
    EUCALOG(loglevel, "\taccountId    = %s\n", inst->accountId);
    EUCALOG(loglevel, "\tmacAddress   = %s\n", mac);
    EUCALOG(loglevel, "\tpublicIp     = %s\n", pubip);
    EUCALOG(loglevel, "\tprivateIp    = %s\n", privip);
    EUCALOG(loglevel, "\tvpc          = %s\n", inst->vpc);
    EUCALOG(loglevel, "\tsubnet       = %s\n", inst->subnet);
    EUCALOG(loglevel, "\tnode         = %s\n", inst->node);
    //EUCALOG(loglevel, "\tnodehostname = %s\n", inst->nodehostname);
    EUCALOG(loglevel, "\tinstance     = %s\n", inst->instance_name.name);
    EUCALOG(loglevel, "\tifname       = %s\n", inst->ifname);
    EUCALOG(loglevel, "\tsrcdstcheck  = %s\n", inst->srcdstcheck ? "true" : "false");
    EUCALOG(loglevel, "\tdeviceidx    = %d\n", inst->deviceidx);
    EUCALOG(loglevel, "\tattachment   = %s\n", inst->attachmentId);
/*
    for (i = 0; i < inst->max_interface_names; i++) {
        EUCALOG(loglevel, "\tinterface[%d] = %s\n", i, inst->interface_names[i].name);
    }
*/
    for (i = 0; i < inst->max_interfaces; i++) {
        EUCALOG(loglevel, "\tinterface[%d] = %s idx %d\n", i, inst->interfaces[i]->name, inst->interfaces[i]->deviceidx);
    }
    for (i = 0; i < inst->max_secgroup_names; i++) {
        EUCALOG(loglevel, "\tsg[%d]        = %s\n", i, inst->secgroup_names[i].name);
    }
    EUCA_FREE(mac);
    EUCA_FREE(pubip);
    EUCA_FREE(privip);
}

/**
 * Logs the contents of an instance_interface structure.
 * @param sg [in] instance_interface of interest.
 * @param loglevel [in] valid value from log_level_e enumeration.
 */
void gni_sg_print(gni_secgroup *sg, int loglevel) {
    int i = 0;

    if (!sg) {
        EUCALOG(loglevel, "Invalid argument: NULL.\n");
        return;
    }
    EUCALOG(loglevel, "------ name = %s -----\n", sg->name);
    EUCALOG(loglevel, "\taccountId    = %s\n", sg->accountId);
    EUCALOG(loglevel, "\tgrouprules   = %d rules\n", sg->max_grouprules);
    for (i = 0; i < sg->max_grouprules; i++) {
        EUCALOG(loglevel, "\t\t%s\n", sg->grouprules[i].name);
    }
    EUCALOG(loglevel, "\tingress      = %d rules\n", sg->max_ingress_rules);
    for (i = 0; i < sg->max_ingress_rules; i++) {
        EUCALOG(loglevel, "\t\t%s %d %d %d %d %d %s\n", sg->ingress_rules[i].cidr,
                sg->ingress_rules[i].protocol, sg->ingress_rules[i].fromPort, sg->ingress_rules[i].toPort,
                sg->ingress_rules[i].icmpType, sg->ingress_rules[i].icmpCode, sg->ingress_rules[i].groupId);
    }
    EUCALOG(loglevel, "\tegress       = %d rules\n", sg->max_egress_rules);
    for (i = 0; i < sg->max_egress_rules; i++) {
        EUCALOG(loglevel, "\t\t%s %d %d %d %d %d %s\n", sg->egress_rules[i].cidr,
                sg->egress_rules[i].protocol, sg->egress_rules[i].fromPort, sg->egress_rules[i].toPort,
                sg->egress_rules[i].icmpType, sg->egress_rules[i].icmpCode, sg->egress_rules[i].groupId);
    }
    for (i = 0; i < sg->max_instances; i++) {
        EUCALOG(loglevel, "\tinstance[%d] = %s\n", i, sg->instances[i]->name);
    }
    for (i = 0; i < sg->max_interfaces; i++) {
        EUCALOG(loglevel, "\tinterface[%d] = %s\n", i, sg->interfaces[i]->name);
    }
}

/**
 * Logs the contents of a vpc structure.
 * @param vpc [in] VPC of interest
 * @param loglevel [in] valid value from log_level_e enumeration
 */
void gni_vpc_print(gni_vpc *vpc, int loglevel) {
    int i = 0;

    if (!vpc) {
        EUCALOG(loglevel, "Invalid argument: NULL.\n");
        return;
    }
    EUCALOG(loglevel, "------ name = %s -----\n", vpc->name);
    EUCALOG(loglevel, "\taccountId    = %s\n", vpc->accountId);
    EUCALOG(loglevel, "\tcidr         = %s\n", vpc->cidr);
    EUCALOG(loglevel, "\tdhcpOptionSet= %s %p\n", vpc->dhcpOptionSet_name, vpc->dhcpOptionSet);
    EUCALOG(loglevel, "\tsubnets      = %d\n", vpc->max_subnets);
    for (i = 0; i < vpc->max_subnets; i++) {
        gni_vpcsubnet *s = &(vpc->subnets[i]);
        EUCALOG(loglevel, "\t---- name = %s ----\n", s->name);
        EUCALOG(loglevel, "\t\taccountId = %s\n", s->accountId);
        EUCALOG(loglevel, "\t\tcidr      = %s\n", s->cidr);
        EUCALOG(loglevel, "\t\tcluster   = %s\n", s->cluster_name);
        EUCALOG(loglevel, "\t\tnetAcl    = %s %p\n", s->networkAcl_name, s->networkAcl);
        EUCALOG(loglevel, "\t\trouteTable= %s\n", s->routeTable_name);
        EUCALOG(loglevel, "\t\tinterfaces= %d\n", s->max_interfaces);
    }
    EUCALOG(loglevel, "\tnetworkAcl   = %d\n", vpc->max_networkAcls);
    for (i = 0; i < vpc->max_networkAcls; i++) {
        //EUCALOG(loglevel, "\t\t\n");
    }
    EUCALOG(loglevel, "\trouteTables  = %d\n", vpc->max_routeTables);
    for (i = 0; i < vpc->max_routeTables; i++) {
        gni_route_table *t = &(vpc->routeTables[i]);
        EUCALOG(loglevel, "\t---- name =  %s ----\n", t->name);
        EUCALOG(loglevel, "\t\taccountId = %s\n", t->accountId);
        EUCALOG(loglevel, "\t\troutes    = %d\n", t->max_entries);
        for (int j = 0; j < t->max_entries; j++) {
            gni_route_entry *e = &(t->entries[j]);
            EUCALOG(loglevel, "\t\t\t%s -> %s\n", e->destCidr, e->target);
        }
    }
    EUCALOG(loglevel, "\tnatGateways  = %d\n", vpc->max_natGateways);
    for (i = 0; i < vpc->max_natGateways; i++) {
        gni_nat_gateway *t = &(vpc->natGateways[i]);
        char *mac = NULL;
        char *pubip = hex2dot(t->publicIp);
        char *privip = hex2dot(t->privateIp);
        hex2mac(t->macAddress, &mac);
        EUCALOG(loglevel, "\t---- name = %s ----\n", t->name);
        EUCALOG(loglevel, "\t\taccountId = %s\n", t->accountId);
        EUCALOG(loglevel, "\t\tmac       = %s\n", mac);
        EUCALOG(loglevel, "\t\tpublicIp  = %s\n", pubip);
        EUCALOG(loglevel, "\t\tprivateIp = %s\n", privip);
        EUCALOG(loglevel, "\t\tvpc       = %s\n", t->vpc);
        EUCALOG(loglevel, "\t\tsubnet    = %s\n", t->subnet);
        EUCA_FREE(mac);
        EUCA_FREE(pubip);
        EUCA_FREE(privip);
    }
    EUCALOG(loglevel, "\tnetworkAcls  = %d\n", vpc->max_networkAcls);
    for (i = 0; i < vpc->max_networkAcls; i++) {
        gni_network_acl *t = &(vpc->networkAcls[i]);
        EUCALOG(loglevel, "\t\tingress      = %d rules\n", t->max_ingress);
        for (i = 0; i < t->max_ingress; i++) {
            EUCALOG(loglevel, "\t\t\t%d %s %d %d %d %d %d %s\n", t->ingress[i].number, t->ingress[i].cidr,
                    t->ingress[i].protocol, t->ingress[i].fromPort, t->ingress[i].toPort,
                    t->ingress[i].icmpType, t->ingress[i].icmpCode, t->ingress[i].allow ? "allow" : "deny");
        }
        EUCALOG(loglevel, "\t\tegress       = %d rules\n", t->max_egress);
        for (i = 0; i < t->max_egress; i++) {
            EUCALOG(loglevel, "\t\t\t%d %s %d %d %d %d %d %s\n", t->ingress[i].number, t->egress[i].cidr,
                    t->egress[i].protocol, t->egress[i].fromPort, t->egress[i].toPort,
                    t->egress[i].icmpType, t->egress[i].icmpCode, t->egress[i].allow ? "allow" : "deny");
        }
    }
    EUCALOG(loglevel, "\tIGNames      = %d\n", vpc->max_internetGatewayNames);
    char names[2048];
    names[0] = '\0';
    for (i = 0; i < vpc->max_internetGatewayNames; i++) {
        gni_name *t = &(vpc->internetGatewayNames[i]);
        strncat(names, t->name, 1024);
        strncat(names, " ", 1024);
    }
    if (strlen(names)) {
        EUCALOG(loglevel, "\t\t%s\n", names);
    }
    EUCALOG(loglevel, "\tinterfaces   = %d\n", vpc->max_interfaces);
    names[0] = '\0';
    for (i = 0; i < vpc->max_interfaces; i++) {
        gni_instance *t = vpc->interfaces[i];
        strncat(names, t->name, 1024);
        strncat(names, " ", 1024);
    }
    if (strlen(names)) {
        EUCALOG(loglevel, "\t\t%s\n", names);
    }
}

/**
 * Logs the contents of an internet_gateway structure.
 * @param ig [in] internet gateway of interest.
 * @param loglevel [in] valid value from log_level_e enumeration.
 */
void gni_internetgateway_print(gni_internet_gateway *ig, int loglevel) {
    if (!ig) {
        EUCALOG(loglevel, "Invalid argument: NULL.\n");
        return;
    }
    EUCALOG(loglevel, "------ name = %s -----\n", ig->name);
    EUCALOG(loglevel, "\taccountId    = %s\n", ig->accountId);
}

/**
 * Logs the contents of a dhcp_os structure.
 * @param dhcpos [in] dhcp_os structure of interest.
 * @param loglevel [in] valid value from log_level_e enumeration.
 */
void gni_dhcpos_print(gni_dhcp_os *dhcpos, int loglevel) {
    if (!dhcpos) {
        EUCALOG(loglevel, "Invalid argument: NULL.\n");
        return;
    }
    EUCALOG(loglevel, "------ name = %s -----\n", dhcpos->name);
    EUCALOG(loglevel, "\taccountId    = %s\n", dhcpos->accountId);

    char *dhcpdstr = NULL;
    char dhcpsstr[1024];
    dhcpsstr[0] = '\0';
    for (int j = 0; j < dhcpos->max_domains; j++) {
        strncat(dhcpsstr, dhcpos->domains[j].name, 512);
        strncat(dhcpsstr, " ", 512);
    }
    if (dhcpos->max_domains) {
        EUCALOG(loglevel, "\tdomains: %s\n", dhcpsstr);
    }
    dhcpsstr[0] = '\0';
    for (int j = 0; j < dhcpos->max_dns; j++) {
        dhcpdstr = hex2dot(dhcpos->dns[j]);
        strncat(dhcpsstr, dhcpdstr, 512);
        strncat(dhcpsstr, ", ", 512);
        EUCA_FREE(dhcpdstr);
    }
    if (dhcpos->max_dns) {
        if (strlen(dhcpsstr) > 2) {
            dhcpsstr[strlen(dhcpsstr) - 2] = '\0';
        }
        EUCALOG(loglevel, "\tdns: %s\n", dhcpsstr);
    }
    dhcpsstr[0] = '\0';
    for (int j = 0; j < dhcpos->max_ntp; j++) {
        dhcpdstr = hex2dot(dhcpos->ntp[j]);
        strncat(dhcpsstr, dhcpdstr, 512);
        strncat(dhcpsstr, ", ", 512);
        EUCA_FREE(dhcpdstr);
    }
    if (dhcpos->max_ntp) {
        if (strlen(dhcpsstr) > 2) {
            dhcpsstr[strlen(dhcpsstr) - 2] = '\0';
        }
        EUCALOG(loglevel, "\tntp: %s\n", dhcpsstr);
    }
    for (int j = 0; j < dhcpos->max_netbios_ns; j++) {
        dhcpdstr = hex2dot(dhcpos->netbios_ns[j]);
        strncat(dhcpsstr, dhcpdstr, 512);
        strncat(dhcpsstr, ", ", 512);
        EUCA_FREE(dhcpdstr);
    }
    if (dhcpos->max_netbios_ns) {
        if (strlen(dhcpsstr) > 2) {
            dhcpsstr[strlen(dhcpsstr) - 2] = '\0';
        }
        EUCALOG(loglevel, "\tnetbios_ns: %s\n", dhcpsstr);
    }
    if (dhcpos->netbios_type) {
        EUCALOG(loglevel, "\tnetbios_type: %d\n", dhcpos->netbios_type);
    }
}


gni_hostname_info *gni_init_hostname_info(void) {
    gni_hostname_info *hni = EUCA_ZALLOC_C(1, sizeof (gni_hostname_info));
    hni->max_hostnames = 0;
    return (hni);
}

int gni_hostnames_print(gni_hostname_info *host_info) {
    int i;

    LOGTRACE("Cached Hostname Info: \n");
    for (i = 0; i < host_info->max_hostnames; i++) {
        LOGTRACE("IP Address: %s Hostname: %s\n", inet_ntoa(host_info->hostnames[i].ip_address), host_info->hostnames[i].hostname);
    }
    return (0);
}

int gni_hostnames_free(gni_hostname_info *host_info) {
    if (!host_info) {
        return (0);
    }

    EUCA_FREE(host_info->hostnames);
    EUCA_FREE(host_info);
    return (0);
}

int gni_hostnames_get_hostname(gni_hostname_info *hostinfo, const char *ip_address, char **hostname) {
    struct in_addr addr;
    gni_hostname key;
    gni_hostname *bsearch_result;

    if (!hostinfo) {
        return (1);
    }

    if (inet_aton(ip_address, &addr)) {
        key.ip_address.s_addr = addr.s_addr; // search by ip
        bsearch_result = bsearch(&key, hostinfo->hostnames, hostinfo->max_hostnames, sizeof (gni_hostname), cmpipaddr);

        if (bsearch_result) {
            *hostname = strdup(bsearch_result->hostname);
            LOGTRACE("bsearch hit: %s\n", *hostname);
            return (0);
        }
    } else {
        LOGTRACE("INET_ATON FAILED FOR: %s\n", ip_address); // we were passed a hostname
    }
    return (1);
}

//
// Used for qsort and bsearch methods against gni_hostname_info
//

int cmpipaddr(const void *p1, const void *p2) {
    gni_hostname *hp1 = (gni_hostname *) p1;
    gni_hostname *hp2 = (gni_hostname *) p2;

    if (hp1->ip_address.s_addr == hp2->ip_address.s_addr)
        return 0;
    if (hp1->ip_address.s_addr < hp2->ip_address.s_addr)
        return -1;
    else
        return 1;
}

/**
 * Compares two globalNetworkInfo structures in the argument and search for
 * VPCMIDO configuration changes.
 * @param a [in] globalNetworkInfo structure of interest.
 * @param b [in] globalNetworkInfo structure of interest.
 * @return 0 if configuration parameters in a and b match. Non-zero otherwise. 
 */
int cmp_gni_vpcmido_config(globalNetworkInfo *a, globalNetworkInfo *b) {
    int ret = 0;
    if (a == b) {
        return (0);
    }
    if ((a == NULL) || (b == NULL)) {
        return (GNI_VPCMIDO_CONFIG_DIFF_OTHER);
    }
    if (a->enabledCLCIp != b->enabledCLCIp) {
        ret |= GNI_VPCMIDO_CONFIG_DIFF_ENABLEDCLCIP;
    }
    if (strcmp(a->instanceDNSDomain, b->instanceDNSDomain)) {
        ret |= GNI_VPCMIDO_CONFIG_DIFF_INSTANCEDNSDOMAIN;
    }
    if (a->max_instanceDNSServers != b->max_instanceDNSServers) {
        ret |= GNI_VPCMIDO_CONFIG_DIFF_INSTANCEDNSSERVERS;
    } else {
        for (int i = 0; i < a->max_instanceDNSServers; i++) {
            if (a->instanceDNSServers[i] != b->instanceDNSServers[i]) {
                ret |= GNI_VPCMIDO_CONFIG_DIFF_INSTANCEDNSSERVERS;
            }
        }
    }
    if (IS_NETMODE_VPCMIDO(a) && IS_NETMODE_VPCMIDO(b)) {
        if (strcmp(a->EucanetdHost, b->EucanetdHost)) {
            ret |= GNI_VPCMIDO_CONFIG_DIFF_EUCANETDHOST;
        }
        if (strcmp(a->PublicNetworkCidr, b->PublicNetworkCidr)) {
            ret |= GNI_VPCMIDO_CONFIG_DIFF_PUBLICNETWORKCIDR;
        }
        if (strcmp(a->PublicGatewayIP, b->PublicGatewayIP)) {
            ret |= GNI_VPCMIDO_CONFIG_DIFF_PUBLICGATEWAYIP;
        }
        if (strcmp(a->GatewayHosts, b->GatewayHosts)) {
            ret |= GNI_VPCMIDO_CONFIG_DIFF_GATEWAYHOSTS;
        }
    } else {
        ret |= GNI_VPCMIDO_CONFIG_DIFF_OTHER;
    }
    return (ret);
}

//!
//! Compares two gni_vpc structures in the argument.
//!
//! @param[in] a gni_vpc structure of interest.
//! @param[in] b gni_vpc structure of interest.
//! @return 0 if name and number of entries match. Non-zero otherwise.
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int cmp_gni_vpc(gni_vpc *a, gni_vpc *b) {
    if (a == b) {
        return (0);
    }
    if ((a == NULL) || (b == NULL)) {
        return (1);
    }
    if (strcmp(a->name, b->name)) {
        return (1);
    }
    if (strcmp(a->dhcpOptionSet_name, b->dhcpOptionSet_name)) {
        return (1);
    }
    if ((a->max_internetGatewayNames == b->max_internetGatewayNames) &&
            (a->max_natGateways == b->max_natGateways) &&
            (a->max_networkAcls == b->max_networkAcls) &&
            (a->max_routeTables == b->max_routeTables) &&
            (a->max_subnets == b->max_subnets)) {
        return (0);
    }
    return (1);
}

//!
//! Compares two gni_vpcsubnet structures in the argument.
//!
//! @param[in] a gni_vpcsubnet structure of interest.
//! @param[in] b gni_vpcsubnet structure of interest.
//! @return 0 if name, routeTable_name and networkAcl_name match. Non-zero otherwise.
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int cmp_gni_vpcsubnet(gni_vpcsubnet *a, gni_vpcsubnet *b) {
    if (a == b) {
        return (0);
    }
    if ((a == NULL) || (b == NULL)) {
        return (1);
    }
    if (strcmp(a->name, b->name)) {
        return (1);
    }
    if ((!strcmp(a->routeTable_name, b->routeTable_name)) &&
            (!strcmp(a->networkAcl_name, b->networkAcl_name)) &&
            (!cmp_gni_route_table(a->routeTable, b->routeTable))) {
        return (0);
    }
    return (1);
}

//!
//! Compares two gni_nat_gateway structures in the argument.
//!
//! @param[in] a gni_nat_gateway structure of interest.
//! @param[in] b gni_nat_gateway structure of interest.
//! @return 0 if name matches. Non-zero otherwise.
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int cmp_gni_nat_gateway(gni_nat_gateway *a, gni_nat_gateway *b) {
    if (a == b) {
        return (0);
    }
    if ((a == NULL) || (b == NULL)) {
        return (1);
    }
    if (!strcmp(a->name, b->name)) {
        return (0);
    }
    return (1);
}

//!
//! Compares two gni_route_table structures in the argument.
//!
//! @param[in] a gni_route_table structure of interest. Check for route entries
//!            applied flags.
//! @param[in] b gni_route_table structure of interest.
//! @return 0 if name and route entries match. Non-zero otherwise.
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note
//!
int cmp_gni_route_table(gni_route_table *a, gni_route_table *b) {
    if (a == b) {
        return (0);
    }
    if ((a == NULL) || (b == NULL)) {
        return (1);
    }
    if (strcmp(a->name, b->name)) {
        return (1);
    }
    if (a->max_entries != b->max_entries) {
        return (1);
    }
    for (int i = 0; i < a->max_entries; i++) {
        if (a->entries[i].applied == 0) {
            return (1);
        }
        if ((strcmp(a->entries[i].destCidr, b->entries[i].destCidr)) ||
                (strcmp(a->entries[i].target, b->entries[i].target))) {
            return (1);
        }
    }
    return (0);
}

//!
//! Compares two gni_secgroup structures in the argument.
//!
//! @param[in]  a gni_secgroup structure of interest.
//! @param[in]  b gni_secgroup structure of interest.
//! @param[out] ingress_diff set to 1 iff ingress rules of a and b differ.
//! @param[out] egress_diff set to 1 iff egress rules of a and b differ.
//! @param[out] interfaces_diff set to 1 iff member interfaces of a and b differ.
//! @return 0 if name and rule entries match. Non-zero otherwise.
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note order of rules are assumed to be the same for both a and b.
//!
int cmp_gni_secgroup(gni_secgroup *a, gni_secgroup *b, int *ingress_diff, int *egress_diff, int *interfaces_diff) {
    int abmatch = 1;
    if (a == b) {
        if (ingress_diff) *ingress_diff = 0;
        if (egress_diff) *egress_diff = 0;
        if (interfaces_diff) *interfaces_diff = 0;
        return (0);
    }

    if (ingress_diff) *ingress_diff = 1;
    if (egress_diff) *egress_diff = 1;
    if (interfaces_diff) *interfaces_diff = 1;

    if ((a == NULL) || (b == NULL)) {
        return (1);
    }
    if (strcmp(a->name, b->name)) {
        abmatch = 0;
    } else {
        if (a->max_ingress_rules == b->max_ingress_rules) {
            int diffound = 0;
            for (int i = 0; i < a->max_ingress_rules && !diffound; i++) {
                if ((a->ingress_rules[i].cidrNetaddr != b->ingress_rules[i].cidrNetaddr) ||
                        (a->ingress_rules[i].cidrSlashnet != b->ingress_rules[i].cidrSlashnet) ||
                        (a->ingress_rules[i].protocol != b->ingress_rules[i].protocol) ||
                        (a->ingress_rules[i].fromPort != b->ingress_rules[i].fromPort) ||
                        (a->ingress_rules[i].toPort != b->ingress_rules[i].toPort) ||
                        (a->ingress_rules[i].icmpCode != b->ingress_rules[i].icmpCode) ||
                        (a->ingress_rules[i].icmpType != b->ingress_rules[i].icmpType) ||
                        (strcmp(a->ingress_rules[i].groupId, b->ingress_rules[i].groupId))) {
                    diffound = 1;
                }
            }
            if (!diffound) {
                if (ingress_diff) *ingress_diff = 0;
            } else {
                abmatch = 0;
            }
        } else {
            abmatch = 0;
        }
        if (a->max_egress_rules == b->max_egress_rules) {
            int diffound = 0;
            for (int i = 0; i < a->max_egress_rules && !diffound; i++) {
                if ((a->egress_rules[i].cidrNetaddr != b->egress_rules[i].cidrNetaddr) ||
                        (a->egress_rules[i].cidrSlashnet != b->egress_rules[i].cidrSlashnet) ||
                        (a->egress_rules[i].protocol != b->egress_rules[i].protocol) ||
                        (a->egress_rules[i].fromPort != b->egress_rules[i].fromPort) ||
                        (a->egress_rules[i].toPort != b->egress_rules[i].toPort) ||
                        (a->egress_rules[i].icmpCode != b->egress_rules[i].icmpCode) ||
                        (a->egress_rules[i].icmpType != b->egress_rules[i].icmpType) ||
                        (strcmp(a->egress_rules[i].groupId, b->egress_rules[i].groupId))) {
                    diffound = 1;
                }
            }
            if (!diffound) {
                if (egress_diff) *egress_diff = 0;
            } else {
                abmatch = 0;
            }
        } else {
            abmatch = 0;
        }
        if (a->max_interfaces == b->max_interfaces) {
            int diffound = 0;
            for (int i = 0; i < a->max_interfaces && !diffound; i++) {
                if (strcmp(a->interfaces[i]->name, b->interfaces[i]->name)) {
                    diffound = 1;
                }
            }
            if (!diffound) {
                if (interfaces_diff) *interfaces_diff = 0;
            } else {
                abmatch = 0;
            }
        } else {
            abmatch = 0;
        }
    }

    if (abmatch) {
        return (0);
    }
    return (1);
}

//!
//! Compares two gni_interface structures in the argument.
//!
//! @param[in]  a gni_interface structure of interest.
//! @param[in]  b gni_interface structure of interest.
//! @param[out] pubip_diff set to 1 iff public IP of a and b differ.
//! @param[out] sdc_diff set to 1 iff src/dst check flag of a and b differ.
//! @param[out] host_diff set to 1 iff host/node of a and b differ.
//! @param[out] sg_diff set to 1 iff list of security group names of a and b differ.
//! @return 0 if name and other properties of a and b match. Non-zero otherwise.
//!
//! @see
//!
//! @pre
//!
//! @post
//!
//! @note order of rules are assumed to be the same for both a and b.
//!
int cmp_gni_interface(gni_instance *a, gni_instance *b, int *pubip_diff, int *sdc_diff, int *host_diff, int *sg_diff) {
    int abmatch = 1;
    int sgmatch = 1;
    if (a == b) {
        if (pubip_diff) *pubip_diff = 0;
        if (sdc_diff) *sdc_diff = 0;
        if (host_diff) *host_diff = 0;
        if (sg_diff) *sg_diff = 0;
        return (0);
    }

    if (pubip_diff) *pubip_diff = 1;
    if (sdc_diff) *sdc_diff = 1;
    if (host_diff) *host_diff = 1;
    if (sg_diff) *sg_diff = 1;

    if ((a == NULL) || (b == NULL)) {
        return (1);
    }
    if (strcmp(a->name, b->name)) {
        abmatch = 0;
    } else {
        if (a->srcdstcheck == b->srcdstcheck) {
            if (sdc_diff) *sdc_diff = 0;
        } else {
            abmatch = 0;
        }
        if (a->publicIp == b->publicIp) {
            if (pubip_diff) *pubip_diff = 0;
        } else {
            abmatch = 0;
        }
        if (!strcmp(a->node, b->node)) {
            if (host_diff) *host_diff = 0;
        } else {
            abmatch = 0;
        }
        if (a->max_secgroup_names != b->max_secgroup_names) {
            abmatch = 0;
            sgmatch = 0;
        } else {
            for (int i = 0; i < a->max_secgroup_names; i++) {
                if (strcmp(a->secgroup_names[i].name, b->secgroup_names[i].name)) {
                    abmatch = 0;
                    sgmatch = 0;
                    break;
                }
            }
        }
    }

    if (sg_diff && (sgmatch == 1)) {
        *sg_diff = 0;
    }
    if (abmatch) {
        return (0);
    }
    return (1);
}

/**
 * Comparator function for gni_instance structures. Comparison is base on name property.
 * @param p1 [in] pointer to gni_instance pointer 1.
 * @param p2 [in] pointer to gni_instance pointer 2.
 * @return 0 iff p1->.->name == p2->.->name. -1 iff p1->.->name < p2->.->name.
 * 1 iff p1->.->name > p2->.->name.
 * NULL is considered larger than a non-NULL string.
 */
int compare_gni_instance_name(const void *p1, const void *p2) {
    gni_instance **pp1 = NULL;
    gni_instance **pp2 = NULL;
    gni_instance *e1 = NULL;
    gni_instance *e2 = NULL;
    char *name1 = NULL;
    char *name2 = NULL;

    if ((p1 == NULL) || (p2 == NULL)) {
        LOGWARN("Invalid argument: cannot compare NULL gni_instance\n");
        return (0);
    }
    pp1 = (gni_instance **) p1;
    pp2 = (gni_instance **) p2;
    e1 = *pp1;
    e2 = *pp2;
    if (e1 && strlen(e1->name)) {
        name1 = e1->name;
    }
    if (e2 && strlen(e2->name)) {
        name2 = e2->name;
    }
    if (name1 == name2) {
        return (0);
    }
    if (name1 == NULL) {
        return (1);
    }
    if (name2 == NULL) {
        return (-1);
    }
    return (strcmp(name1, name2));
}

