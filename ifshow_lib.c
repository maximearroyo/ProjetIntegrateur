#include "ifshow_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

int calculate_prefix_ipv4(struct sockaddr_in *netmask) {
    int prefix = 0;
    uint32_t mask = ntohl(netmask->sin_addr.s_addr);
    while (mask) {
        prefix += mask & 1;
        mask >>= 1;
    }
    return prefix;
}

int calculate_prefix_ipv6(struct sockaddr_in6 *netmask) {
    int prefix = 0;
    uint8_t *mask = (uint8_t *)&netmask->sin6_addr;
    for (int i = 0; i < 16; i++) {
        uint8_t byte = mask[i];
        for (int j = 0; j < 8; j++) {
            if (byte & (0x80 >> j))
                prefix++;
            else
                return prefix;
        }
    }
    return prefix;
}

int show_interface_prefixes(const char *ifname, FILE *out) {
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;
    char addr_str[INET6_ADDRSTRLEN];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        if (strcmp(ifa->ifa_name, ifname) != 0)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET &&
            ifa->ifa_addr->sa_family != AF_INET6)
            continue;

        found = 1;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            struct sockaddr_in *netmask = (struct sockaddr_in *)ifa->ifa_netmask;
            inet_ntop(AF_INET, &addr->sin_addr, addr_str, INET_ADDRSTRLEN);
            int prefix = calculate_prefix_ipv4(netmask);
            fprintf(out, "%s/%d\n", addr_str, prefix);
        }
        else if (ifa->ifa_addr->sa_family == AF_INET6) {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ifa->ifa_addr;
            struct sockaddr_in6 *netmask = (struct sockaddr_in6 *)ifa->ifa_netmask;
            inet_ntop(AF_INET6, &addr->sin6_addr, addr_str, INET6_ADDRSTRLEN);
            int prefix = calculate_prefix_ipv6(netmask);
            fprintf(out, "%s/%d\n", addr_str, prefix);
        }
    }

    freeifaddrs(ifaddr);

    if (!found) {
        fprintf(stderr, "Interface '%s' non trouvée\n", ifname);
        return -1;
    }

    return 0;
}

int show_all_interfaces(FILE *out) {
    struct ifaddrs *ifaddr, *ifa;
    char **unique_ifs = NULL;
    int count = 0;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_name == NULL)
            continue;

        int already_exists = 0;
        for (int i = 0; i < count; i++) {
            if (strcmp(unique_ifs[i], ifa->ifa_name) == 0) {
                already_exists = 1;
                break;
            }
        }

        if (!already_exists) {
            unique_ifs = realloc(unique_ifs, (count + 1) * sizeof(char *));
            unique_ifs[count] = strdup(ifa->ifa_name);
            count++;
        }
    }

    freeifaddrs(ifaddr);

    for (int i = 0; i < count; i++) {
        fprintf(out, "%s\n", unique_ifs[i]);
        free(unique_ifs[i]);
    }

    free(unique_ifs);
    return 0;
}

int show_all_with_addresses(FILE *out) {
    struct ifaddrs *ifaddr, *ifa;
    char addr_str[INET6_ADDRSTRLEN];
    char **unique_ifs = NULL;
    int if_count = 0;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_name == NULL)
            continue;

        int already_exists = 0;
        for (int i = 0; i < if_count; i++) {
            if (strcmp(unique_ifs[i], ifa->ifa_name) == 0) {
                already_exists = 1;
                break;
            }
        }

        if (!already_exists) {
            unique_ifs = realloc(unique_ifs, (if_count + 1) * sizeof(char *));
            unique_ifs[if_count] = strdup(ifa->ifa_name);
            if_count++;
        }
    }

    for (int i = 0; i < if_count; i++) {
        fprintf(out, "%s:\n", unique_ifs[i]);

        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL)
                continue;
            if (strcmp(ifa->ifa_name, unique_ifs[i]) != 0)
                continue;
            if (ifa->ifa_addr->sa_family != AF_INET &&
                ifa->ifa_addr->sa_family != AF_INET6)
                continue;

            if (ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                struct sockaddr_in *netmask = (struct sockaddr_in *)ifa->ifa_netmask;
                inet_ntop(AF_INET, &addr->sin_addr, addr_str, INET_ADDRSTRLEN);
                int prefix = calculate_prefix_ipv4(netmask);
                fprintf(out, "  %s/%d\n", addr_str, prefix);
            }
            else if (ifa->ifa_addr->sa_family == AF_INET6) {
                struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ifa->ifa_addr;
                struct sockaddr_in6 *netmask = (struct sockaddr_in6 *)ifa->ifa_netmask;
                inet_ntop(AF_INET6, &addr->sin6_addr, addr_str, INET6_ADDRSTRLEN);
                int prefix = calculate_prefix_ipv6(netmask);
                fprintf(out, "  %s/%d\n", addr_str, prefix);
            }
        }

        if (i < if_count - 1)
            fprintf(out, "\n");
    }

    for (int i = 0; i < if_count; i++) {
        free(unique_ifs[i]);
    }
    free(unique_ifs);
    freeifaddrs(ifaddr);
    return 0;
}

int get_hostname(char *buf, size_t len) {
    return gethostname(buf, len);
}
