#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ifaddrs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [-i ifname] [-a]\n", prog_name);
    fprintf(stderr, "  -i ifname : affiche les préfixes IPv4 et IPv6 de l'interface spécifiée\n");
    fprintf(stderr, "  -a        : affiche toutes les interfaces avec leurs préfixes IPv4 et IPv6\n");
    fprintf(stderr, "  sans option : affiche toutes les interfaces réseau\n");
}

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

int show_interface_prefixes(const char *ifname) {
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
            printf("%s/%d\n", addr_str, prefix);
        }
        else if (ifa->ifa_addr->sa_family == AF_INET6) {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ifa->ifa_addr;
            struct sockaddr_in6 *netmask = (struct sockaddr_in6 *)ifa->ifa_netmask;
            
            inet_ntop(AF_INET6, &addr->sin6_addr, addr_str, INET6_ADDRSTRLEN);
            int prefix = calculate_prefix_ipv6(netmask);
            printf("%s/%d\n", addr_str, prefix);
        }
    }
    
    freeifaddrs(ifaddr);
    
    if (!found) {
        fprintf(stderr, "Interface '%s' non trouvée\n", ifname);
        return -1;
    }
    
    return 0;
}

int show_all_interfaces() {
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
        printf("%s\n", unique_ifs[i]);
        free(unique_ifs[i]);
    }
    free(unique_ifs);
    
    return 0;
}

int show_all_with_addresses() {
    struct ifaddrs *ifaddr, *ifa;
    char addr_str[INET6_ADDRSTRLEN];
    char **unique_ifs = NULL;
    int if_count = 0;
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }
    
    // Première passe : collecter les noms uniques d'interfaces
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
    
    // Deuxième passe : pour chaque interface, afficher toutes ses adresses
    for (int i = 0; i < if_count; i++) {
        printf("%s:\n", unique_ifs[i]);
        
        // Parcourir toutes les adresses de cette interface
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
                printf("  %s/%d\n", addr_str, prefix);
            }
            else if (ifa->ifa_addr->sa_family == AF_INET6) {
                struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ifa->ifa_addr;
                struct sockaddr_in6 *netmask = (struct sockaddr_in6 *)ifa->ifa_netmask;
                
                inet_ntop(AF_INET6, &addr->sin6_addr, addr_str, INET6_ADDRSTRLEN);
                int prefix = calculate_prefix_ipv6(netmask);
                printf("  %s/%d\n", addr_str, prefix);
            }
        }
        
        // Ligne vide entre les interfaces (sauf pour la dernière)
        if (i < if_count - 1)
            printf("\n");
    }
    
    // Libération mémoire
    for (int i = 0; i < if_count; i++) {
        free(unique_ifs[i]);
    }
    free(unique_ifs);
    freeifaddrs(ifaddr);
    
    return 0;
}

int main(int argc, char *argv[]) {
    int opt;
    char *ifname = NULL;
    int show_all_flag = 0;
    
    while ((opt = getopt(argc, argv, "i:ah")) != -1) {
        switch (opt) {
            case 'i':
                ifname = optarg;
                break;
            case 'a':
                show_all_flag = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    if (show_all_flag) {
        return show_all_with_addresses();
    } else if (ifname != NULL) {
        return show_interface_prefixes(ifname);
    } else {
        return show_all_interfaces();
    }
}
