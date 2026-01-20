#ifndef IFSHOW_LIB_H
#define IFSHOW_LIB_H

#include <stdio.h>

// Affiche les préfixes IPv4/IPv6 de l'interface spécifiée
// Retourne 0 si OK, -1 si erreur
int show_interface_prefixes(const char *ifname, FILE *out);

// Affiche toutes les interfaces avec leurs adresses
int show_all_with_addresses(FILE *out);

// Affiche juste les noms d'interfaces
int show_all_interfaces(FILE *out);

// Récupère le hostname de la machine
int get_hostname(char *buf, size_t len);

#endif
