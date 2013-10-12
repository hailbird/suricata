/* Copyright (C) 2007-2013 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Giuseppe Longo <giuseppelng@gmail.com>
 *
 * Example config:
 * defrag:
 *    memcap: 32mb
 *    hash-size: 65536
 *    trackers: 65535
 *    max-frags: 65535
 *    prealloc: yes
 *
 *    default-config:
 *       timeout: 40
 *
 *    host-config:
 *
 *      - dmz:
 *          timeout: 30
 *          address: [192.168.1.0/24, 127.0.0.0/8, 1.1.1.0/24, 2.2.2.0/24, "1.1.1.1", "2.2.2.2", "::1"]
 *
 *      - lan:
 *          timeout: 45
 *          address:
 *            - 192.168.0.0/24
 *            - 192.168.10.0/24
 *            - 172.16.14.0/24
 */

#include "suricata-common.h"
#include "queue.h"
#include "suricata.h"
#include "conf.h"
#include "util-debug.h"
#include "util-misc.h"
#include "defrag-config.h"

static SCRadixTree *defrag_tree = NULL;

static int default_timeout = 0;

static void DefragPolicyAddHostInfo(char *host_ip_range, uintmax_t *timeout)
{
    uintmax_t *user_data = timeout;

    if (strchr(host_ip_range, ':') != NULL) {
        SCLogDebug("adding ipv6 host %s", host_ip_range);
        if (SCRadixAddKeyIPV6String(host_ip_range, defrag_tree, user_data) == NULL) {
            SCLogWarning(SC_ERR_INVALID_VALUE,
                        "failed to add ipv6 host %s", host_ip_range);
        }
    } else {
        SCLogDebug("adding ipv4 host %s", host_ip_range);
        if (SCRadixAddKeyIPV4String(host_ip_range, defrag_tree, user_data) == NULL) {
            SCLogWarning(SC_ERR_INVALID_VALUE,
                        "failed to add ipv4 host %s", host_ip_range);
        }
    }
}

static int DefragPolicyGetIPv4HostTimeout(uint8_t *ipv4_addr)
{
    SCRadixNode *node = SCRadixFindKeyIPV4BestMatch(ipv4_addr, defrag_tree);

    if (node == NULL)
        return -1;
    else
        return *((int *)node->prefix->user_data_result);
}

static int DefragPolicyGetIPv6HostTimeout(uint8_t *ipv6_addr)
{
    SCRadixNode *node = SCRadixFindKeyIPV6BestMatch(ipv6_addr, defrag_tree);
    if (node == NULL)
        return -1;
    else
        return *((int *)node->prefix->user_data_result);
}

int DefragPolicyGetHostTimeout(Packet *p)
{
    int timeout = 0;

    if (PKT_IS_IPV4(p))
        timeout = DefragPolicyGetIPv4HostTimeout((uint8_t *)GET_IPV4_DST_ADDR_PTR(p));
    else if (PKT_IS_IPV6(p))
        timeout = DefragPolicyGetIPv6HostTimeout((uint8_t *)GET_IPV6_DST_ADDR(p));
    else
        timeout = default_timeout;

    return timeout;
}

static void DefragParseParameters(ConfNode *n)
{
    ConfNode *si;
    uintmax_t timeout = 0;

    TAILQ_FOREACH(si, &n->head, next) {
        if (strcasecmp("timeout", si->name) == 0) {
                SCLogDebug("timeout value  %s", si->val);
                if (ParseSizeStringU64(si->val, &timeout) < 0) {
                    SCLogError(SC_ERR_SIZE_PARSE, "Error parsing timeout "
                               "from conf file");
                }
            }
            if (strcasecmp("address", si->name) == 0) {
                ConfNode *pval;
                TAILQ_FOREACH(pval, &si->head, next) {
                    DefragPolicyAddHostInfo(pval->val, &timeout);
                }
            }
        }
}

void DefragSetDefaultTimeout(intmax_t timeout)
{
    default_timeout = timeout;
    SCLogDebug("default timeout %d", default_timeout);
}

void DefragPolicyLoadFromConfig(void)
{
    SCEnter();

    defrag_tree = SCRadixCreateRadixTree(NULL, NULL);
    if (defrag_tree == NULL) {
        SCLogError(SC_ERR_MEM_ALLOC,
            "Can't alloc memory for the defrag config tree.");
        exit(EXIT_FAILURE);
    }

    ConfNode *server_config = ConfGetNode("defrag.host-config");
    if (server_config == NULL) {
        SCLogDebug("failed to read host config");
        SCReturn;
    }

    SCLogDebug("configuring host config %p", server_config);
    ConfNode *sc;

    TAILQ_FOREACH(sc, &server_config->head, next) {
        ConfNode *p = NULL;

        TAILQ_FOREACH(p, &sc->head, next) {
            SCLogDebug("parsing configuration for %s", p->name);
            DefragParseParameters(p);
        }
    }
}
