#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

#define PORT "5555"

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s -n <addr> [-i <ifname> | -a]\n", prog_name);
    fprintf(stderr, "  -n addr    : adresse IP de la machine distante\n");
    fprintf(stderr, "  -i ifname  : affiche les préfixes de l'interface spécifiée\n");
    fprintf(stderr, "  -a         : affiche toutes les interfaces avec leurs préfixes\n");
}

int connect_to_agent(const char *addr_str) {
    struct addrinfo hints, *res, *rp;
    int sock = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(addr_str, PORT, &hints, &res) != 0) {
        fprintf(stderr, "Erreur getaddrinfo pour %s\n", addr_str);
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1)
            continue;

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);

    if (sock == -1) {
        fprintf(stderr, "Impossible de se connecter à %s:%s\n", addr_str, PORT);
    }

    return sock;
}

int main(int argc, char *argv[]) {
    int opt;
    char *addr = NULL;
    char *ifname = NULL;
    int show_all = 0;

    while ((opt = getopt(argc, argv, "n:i:ah")) != -1) {
        switch (opt) {
            case 'n':
                addr = optarg;
                break;
            case 'i':
                ifname = optarg;
                break;
            case 'a':
                show_all = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!addr) {
        fprintf(stderr, "Erreur : l'option -n <addr> est obligatoire\n");
        print_usage(argv[0]);
        return 1;
    }

    if (ifname && show_all) {
        fprintf(stderr, "Erreur : choisir -i ou -a, pas les deux\n");
        print_usage(argv[0]);
        return 1;
    }

    if (!ifname && !show_all) {
        fprintf(stderr, "Erreur : préciser -i <ifname> ou -a\n");
        print_usage(argv[0]);
        return 1;
    }

    int sock = connect_to_agent(addr);
    if (sock < 0) {
        return 1;
    }

    FILE *s = fdopen(sock, "r+");
    if (!s) {
        perror("fdopen");
        close(sock);
        return 1;
    }

    // Envoi de la commande
    if (ifname) {
        fprintf(s, "IFNAME %s\n", ifname);
    } else {
        fprintf(s, "ALL\n");
    }
    fflush(s);

    // Lecture et affichage de la réponse
    char buf[1024];
    while (fgets(buf, sizeof(buf), s)) {
        fputs(buf, stdout);
    }

    fclose(s);
    return 0;
}
