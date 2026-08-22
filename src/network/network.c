#include "network/network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/fileutils.h"
#include "utils/strutils.h"

/* /proc/net/dev: two header lines, then one line per interface. Even a
 * machine with many virtual interfaces (containers, VPNs, bridges)
 * rarely exceeds a few dozen lines of ~150 bytes each. */
#define NET_DEV_BUF_SIZE 16384

static lsm_status_t ensure_capacity(lsm_network_list_t *list)
{
    if (list->count < list->capacity)
        return LSM_OK;

    size_t new_capacity = (list->capacity == 0) ? 16 : list->capacity * 2;
    lsm_network_interface_t *grown = realloc(list->items, new_capacity * sizeof(*grown));
    if (grown == NULL)
        return LSM_ERR_NOMEM;

    list->items = grown;
    list->capacity = new_capacity;
    return LSM_OK;
}

/*
 * /sys/class/net/<iface>/operstate is a single word: "up", "down",
 * "unknown", "dormant", etc. Many virtual interfaces (bridges, some
 * tunnels) legitimately report "unknown" because they have no concept of
 * carrier detection — that is expected, not a fallback for an error.
 */
static void read_operstate(const char *iface, char *out, size_t out_size)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", iface);

    char buf[32];
    lsm_status_t status = lsm_read_file(path, buf, sizeof(buf), NULL);
    if (status != LSM_OK) {
        lsm_strlcpy(out, "unknown", out_size);
        return;
    }
    lsm_strlcpy(out, lsm_str_trim(buf), out_size);
}

lsm_status_t lsm_network_collect(lsm_network_list_t *list)
{
    if (list == NULL)
        return LSM_ERR_INVALID_ARG;

    lsm_network_free(list);

    char *buf = malloc(NET_DEV_BUF_SIZE);
    if (buf == NULL)
        return LSM_ERR_NOMEM;

    lsm_status_t status = lsm_read_file("/proc/net/dev", buf, NET_DEV_BUF_SIZE, NULL);
    if (status != LSM_OK) {
        free(buf);
        return status;
    }

    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    int line_number = 0;
    while (line != NULL) {
        line_number++;
        if (line_number <= 2) {
            /* The two fixed header lines ("Inter-|..." and " face |..."). */
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        char *colon = strchr(line, ':');
        if (colon == NULL) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        *colon = '\0';
        char *name = lsm_str_trim(line);
        char *fields = colon + 1;

        unsigned long long rx_bytes, rx_packets, rx_errors, rx_dropped;
        unsigned long long tx_bytes, tx_packets, tx_errors, tx_dropped;
        int n = sscanf(fields, "%llu %llu %llu %llu %*u %*u %*u %*u %llu %llu %llu %llu",
                        &rx_bytes, &rx_packets, &rx_errors, &rx_dropped,
                        &tx_bytes, &tx_packets, &tx_errors, &tx_dropped);
        if (n != 8) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        if (ensure_capacity(list) != LSM_OK) {
            free(buf);
            lsm_network_free(list);
            return LSM_ERR_NOMEM;
        }

        lsm_network_interface_t *entry = &list->items[list->count];
        memset(entry, 0, sizeof(*entry));
        lsm_strlcpy(entry->name, name, sizeof(entry->name));
        entry->is_loopback = (strcmp(name, "lo") == 0);
        entry->rx_bytes = rx_bytes;
        entry->rx_packets = rx_packets;
        entry->rx_errors = rx_errors;
        entry->rx_dropped = rx_dropped;
        entry->tx_bytes = tx_bytes;
        entry->tx_packets = tx_packets;
        entry->tx_errors = tx_errors;
        entry->tx_dropped = tx_dropped;
        /* Pass the already-truncated, fixed-size entry->name (not the
         * unbounded `name` pointer) so the compiler can prove the
         * snprintf() inside read_operstate() never truncates. */
        read_operstate(entry->name, entry->state, sizeof(entry->state));

        list->count++;
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(buf);
    return LSM_OK;
}

void lsm_network_free(lsm_network_list_t *list)
{
    if (list == NULL)
        return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}
