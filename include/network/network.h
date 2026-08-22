#ifndef LSM_NETWORK_NETWORK_H
#define LSM_NETWORK_NETWORK_H

#include <stddef.h>
#include <stdint.h>

#include "core/status.h"

/*
 * Phase 6 — Network Monitor.
 *
 * Source: /proc/net/dev for cumulative traffic counters (kept as the
 * primary source — its columnar format is stable since ~2.2 and trivial
 * to parse), augmented with /sys/class/net/<iface>/ for operational
 * state and link speed, which /proc/net/dev does not expose. Netlink
 * (rtnetlink, NETLINK_ROUTE) is the more "modern" way to obtain this
 * same data and will be considered if/when this project needs live
 * event-driven updates (interface up/down notifications) rather than
 * polling — see docs/ARCHITECTURE.md roadmap. For a polling snapshot
 * tool, parsing two files is simpler and equally correct, so we do not
 * reach for netlink prematurely (project rule: complexity only where it
 * earns its keep).
 *
 * Like CPU ticks and disk sectors, these are cumulative counters since
 * the interface was brought up (or since boot for most interfaces); a
 * throughput rate requires two time-separated samples, computed by the
 * caller (see main.c's disk/cpu sampling for the established pattern).
 */
typedef struct lsm_network_interface {
    char name[16];             /* e.g. "eth0", "wlan0", "lo"; IFNAMSIZ is 16 */
    char state[16];             /* /sys/class/net/<if>/operstate: "up", "down",
                                  * "unknown" (common for interfaces with no
                                  * carrier-detection, e.g. some virtual ones) */
    int is_loopback;             /* name == "lo"; excluded from some summaries
                                   * by convention (callers may want it or not) */

    uint64_t rx_bytes;
    uint64_t rx_packets;
    uint64_t rx_errors;
    uint64_t rx_dropped;

    uint64_t tx_bytes;
    uint64_t tx_packets;
    uint64_t tx_errors;
    uint64_t tx_dropped;
} lsm_network_interface_t;

typedef struct lsm_network_list {
    lsm_network_interface_t *items; /* heap-allocated, owned by this struct */
    size_t count;
    size_t capacity;
} lsm_network_list_t;

/*
 * Enumerates network interfaces from /proc/net/dev and augments each
 * with operational state from /sys/class/net/. `list` must be
 * zero-initialized or previously freed. An interface whose /sys entry
 * disappears mid-read (hot-unplugged USB NIC, etc.) is still reported
 * with its /proc/net/dev counters and state left as "unknown", rather
 * than being dropped — the traffic counters are still valid data.
 */
lsm_status_t lsm_network_collect(lsm_network_list_t *list);

/* Releases the array owned by `list` and zeroes it. Safe to call twice. */
void lsm_network_free(lsm_network_list_t *list);

#endif /* LSM_NETWORK_NETWORK_H */
