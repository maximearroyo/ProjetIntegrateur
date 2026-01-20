# ifnetshow : la commande pour lister les interfaces réseaux d'une machine distante

Cette documentation a pour but d'expliquer le script `ifnetshow`.

---

### Prérequis

- Compilateur fonctionnel
- Code fonctionnel `ifshow`
- Machine de développement
-  Deux machines de test
- Avoir fini les mini-projets 1,2 et 3
- Gestion de versions par Git

---

## 6. *ifnetshow* : la commande pour lister les interfaces réseaux d'une machine distante

`ifnetshow` est une commande en ligne à développer en langage C sous Linux. Elle affiche la liste des interfaces réseaux d'une machine distante et leurs caractéristiques. Le comportement de la commande dépend des paramètres qui lui sont associées :

- `ifnetshow -n addr -i ifname` : affiche la liste des préfixes d'adresses IPv4 et IPv6 associées à l'interfaces nommée `ifname` de la machine ayant une interface réseau d’adresse IP `addr` ;
- `ifnetshow -n addr -a` : affiche la liste des noms des interfaces réseaux, et leurs préfixes d'adresses IPv4 et IPv6 de la machine ayant une interafce réseau d'adresse IP `addr`.

`ifnetshow` nécessite le développement d'un agent à installer sur machines distantes. L'agent devrait être persistant (il ne devrait pas être exécuté après chaque appel de `ifnetshow`).

On apprendra à développer une application communicante basée sur les sockets, en mode client/serveur (`ifnetshow` étant le client, et l'agent le serveur). On utilisera le réseau du mini-projet 5 pour valider le fonctionnement de `ifnetshow`. On réutilisera impérativement le code de `ifshow`. On apprendra à structurer votre programme pour le réutiliser.

---

### 6.1 Développer la commande *ifnetshow* et son agent sur le système hôte (persistant ou non). Utiliser le système de socket pour établir les communications entres les machines

### Agent Serveur `ifnetshowd.c`

On crée un agent serveur `ifnetshowd.c`.

- écoute en IPv4 et IPv6
- redirige stdout/stderr vers la socket
- appelle `run_show()`

```c
// ifnetshowd.c
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#define IFSHOW_NO_MAIN
#include "ifshow.c"

#define PORT_STR "9090"
#define LINE_MAX 256

static int read_line(int fd, char *buf, size_t cap){
  size_t i = 0;
  while(i + 1 < cap){
    char c;
    ssize_t r = recv(fd, &c, 1, 0);
    if(r == 0) break;              // EOF
    if(r < 0){
      if(errno == EINTR) continue;
      return -1;
    }
    if(c == '\n') break;
    buf[i++] = c;
  }
  buf[i] = '\0';
  return (int)i;
}

static int ifname_ok(const char *s){
  // simple: autorise lettres/chiffres/._- (évite injection et trucs bizarres)
  if(!s || !*s) return 0;
  for(const unsigned char *p=(const unsigned char*)s; *p; p++){
    if(!(isalnum(*p) || *p=='_' || *p=='-' || *p=='.')) return 0;
  }
  return 1;
}

static void send_err(int fd, const char *msg){
  (void)send(fd, msg, strlen(msg), 0);
}

static void handle_client(int cfd){
  char line[LINE_MAX];
  int n = read_line(cfd, line, sizeof(line));
  if(n <= 0){
    return;
  }

  // trim espaces
  while(n > 0 && (line[n-1] == '\r' || line[n-1] == ' ' || line[n-1] == '\t')) line[--n] = '\0';

  int mode_all = 0;
  const char *ifname = NULL;

  if(strcmp(line, "ALL") == 0){
    mode_all = 1;
  } else if(strncmp(line, "IF ", 3) == 0){
    ifname = line + 3;
    while(*ifname == ' ' || *ifname == '\t') ifname++;
    if(!ifname_ok(ifname)){
      send_err(cfd, "ERR bad-ifname\n");
      return;
    }
  } else {
    send_err(cfd, "ERR bad-request\n");
    return;
  }

  // Redirige stdout + stderr vers le client
  int old_out = dup(STDOUT_FILENO);
  int old_err = dup(STDERR_FILENO);
  if(old_out < 0 || old_err < 0){
    send_err(cfd, "ERR internal\n");
    return;
  }

  if(dup2(cfd, STDOUT_FILENO) < 0 || dup2(cfd, STDERR_FILENO) < 0){
    send_err(cfd, "ERR internal\n");
    close(old_out); close(old_err);
    return;
  }

  // Optionnel: rendre stdout non bufferisé pour que ça parte direct
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  // Réutilisation directe de ton code ifshow
  if(mode_all){
    run_show(NULL, 1);
  } else {
    run_show(ifname, 0);
  }

  fflush(NULL);

  // Restore stdout/stderr
  dup2(old_out, STDOUT_FILENO);
  dup2(old_err, STDERR_FILENO);
  close(old_out);
  close(old_err);
}

int main(void){
  signal(SIGPIPE, SIG_IGN);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET6;       // socket IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags    = AI_PASSIVE;

  struct addrinfo *res = NULL;
  int rc = getaddrinfo(NULL, PORT_STR, &hints, &res);
  if(rc != 0){
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
    return 1;
  }

  int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if(s < 0){ perror("socket"); freeaddrinfo(res); return 1; }

  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  // accepte IPv4-mapped aussi
  int v6only = 0;
  setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));

  if(bind(s, res->ai_addr, res->ai_addrlen) < 0){
    perror("bind");
    freeaddrinfo(res);
    close(s);
    return 1;
  }
  freeaddrinfo(res);

  if(listen(s, 16) < 0){
    perror("listen");
    close(s);
    return 1;
  }

  fprintf(stderr, "ifnetshowd listening on port %s (TCP)\n", PORT_STR);

  while(1){
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int cfd = accept(s, (struct sockaddr*)&ss, &slen);
    if(cfd < 0){
      if(errno == EINTR) continue;
      perror("accept");
      continue;
    }
    handle_client(cfd);
    close(cfd);
  }
}
```

#### Explication

##### Inclusion et réutilisation de `ifshow`

```c
#define IFSHOW_NO_MAIN
#include "ifshow.c"
```

- `#define IFSHOW_NO_MAIN` désactive le `main()` contenu dans `ifshow.c`
- `#include "ifshow.c"` injecte les fonctions de `ifshow`, notamment `run_show()`.
- L'agent peut donc appeler `run_show()` sans dupliquer le code.



##### Constantes utilisées

```c
#define PORT_STR "9090"
#define LINE_MAX 256
```

- `PORT_STR` : port d'écoute du serveur.
- `LINE_MAX` : taille max d'une requête lue



##### Fonction `read_line` : lire une requête texte

```c
static int read_line(int fd, char *buf, size_t cap){
  size_t i = 0;
  while(i + 1 < cap){
    char c;
    ssize_t r = recv(fd, &c, 1, 0);
    if(r == 0) break;              // EOF
    if(r < 0){
      if(errno == EINTR) continue;
      return -1;
    }
    if(c == '\n') break;
    buf[i++] = c;
  }
  buf[i] = '\0';
  return (int)i;
} 
```

- Lit la requête **caractère par caractère**  jusqu'à `\n`
- Lit une seule commande à la fois
- Retourne la longueur lue, ou `-1` en cas d'erreur



##### Fonction `ifname_ok` : sécuriser le nom d'interface

```c
static int ifname_ok(const char *s){
  if(!s || !*s) return 0;
  for(const unsigned char *p=(const unsigned char*)s; *p; p++){
    if(!(isalnum(*p) || *p=='_' || *p=='-' || *p=='.')) return 0;
  }
  return 1;
}
```

- Vérifie que `ifname` contient seulement des caractères simples :
   lettres, chiffres, `_`, `-`, `.`
- Objectif : éviter des noms “bizarres” ou dangereux.
- Si invalide → la requête est refusée (`ERR bad-ifname`).



##### Fonction `handle_client` : traiter une connexion client

###### 5.1 Lecture et validation de la requête

```c
int n = read_line(cfd, line, sizeof(line));
if(n <= 0) return;
```

- Lit la ligne envoyée par le client
- Si rien n'est reçu -> on arrête 



**Suppression des fins de ligne / espaces**

```c
while(n > 0 && (line[n-1] == '\r' || line[n-1] == ' ' || line[n-1] == '\t'))
  line[--n] = '\0';
```

- Permet d'accepter `ALL\r\n` ou `ALL `



**Analyse de la commande :**

```c
if(strcmp(line, "ALL") == 0){
  mode_all = 1;
} else if(strncmp(line, "IF ", 3) == 0){
  ifname = line + 3;
  ...
} else {
  send_err(cfd, "ERR bad-request\n");
  return;
}
```

- `ALL` : mode toute interfaces
- `IF <ifname>` : mode interface unique
- sinon : erreur protocole



##### 5.2 Redirection de la sortie vers la socket

On souhaite réutiliser `ifshow` tel que en envoyant ça au client.

###### **Sauvegarde des sorties actuelles**

```c
int old_out = dup(STDOUT_FILENO);
int old_err = dup(STDERR_FILENO);
```

- On garde les descripteurs actuels pour pouvoir les restaurer après



###### Redirection

```c
dup2(cfd, STDOUT_FILENO);
dup2(cfd, STDERR_FILENO);
```

- Tout ce qui est écrit sur `stdout` / `stderr` sera envoyé sur la socket
- Les `printf()` de `ifshow` partent donc directement vers le client



###### Désactivation du buffering

```c
setvbuf(stdout, NULL, _IONBF, 0);
setvbuf(stderr, NULL, _IONBF, 0);
```

- Permet que l'affichage parte immédiatement vers le client



###### Appel de `run_show`

```c
if(mode_all){
  run_show(NULL, 1);
} else {
  run_show(ifname, 0);
}
```

- `run_show(NULL, 1)` = mode `-a`
- `run_show(ifname, 0)` = mode `-i ifname`

On réutilise donc **impérativement le code**



###### Restauration des sorties

```c
dup2(old_out, STDOUT_FILENO);
dup2(old_err, STDERR_FILENO);
close(old_out);
close(old_err);
```

- Remet stdout/stderr comme avant.
- Évite de casser le serveur après une requête.



##### Fonction `main` : création du serveur TCP

###### Résolution d'adresse + création de socket

```c
hints.ai_family = AF_INET6;
hints.ai_socktype = SOCK_STREAM;
hints.ai_flags = AI_PASSIVE;

getaddrinfo(NULL, PORT_STR, &hints, &res);
socket(...);
```

- On crée une socket serveur TCP.
- `AF_INET6` : écoute en IPv6.



###### Options de socket

```c
setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
```

- Permet de relancer rapidement le serveur sans attendre

```c
setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
```

- `v6only=0` : accepte aussi les connexions IPv4 via IPv4-mapped



###### Bind / Listen

```c
bind(s, res->ai_addr, res->ai_addrlen);
listen(s, 16);
```

- `bind` attache le port `9090` 
- `listen` met la socket en écoute.



###### Boucle serveur

```c
while(1){
  int cfd = accept(s, ...);
  handle_client(cfd);
  close(cfd);
}
```

- Attend un client
- Traite sa requête
- Ferme la connexion
- Recommence



#### Resumé Rapide

- `ifnetshowd` écoute sur TCP/9090
- Reçoit `ALL` ou `IF <ifname>`
- Redirige stdout/stderr vers la socket
- Appelle `run_show()` (code de `ifshow`) pour générer la réponse
- Renvoie la sortie au client et ferme la connexion

---

### Client `ifnetshow.c`

`ifnetshow` est la commande côté utilisateur.

Elle contacte une machine distante (adresse `addr`) sur le port TCP (ici `9090`), envoie une requête simple, puis affiche la réponse renvoyée par l'agent `ifnetshowd`.

 **Commandes supportées**

- `ifnetshow -n <addr> -a` : demande la liste complète (équivalent distant de `ifshow -a`)

- `ifnsetshow -n <addr> -i <ifname>` : demande les préfixes d'une interfaces équivalent distant de `ifshow -i ifname`

```c
// ifnetshow.c
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#define PORT_STR "9090"

static void usage(const char *p){
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  %s -n <addr> -a\n", p);
  fprintf(stderr, "  %s -n <addr> -i <ifname>\n", p);
}

static int connect_to(const char *addr){
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *res = NULL;
  int rc = getaddrinfo(addr, PORT_STR, &hints, &res);
  if(rc != 0){
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
    return -1;
  }

  int s = -1;
  for(struct addrinfo *p = res; p; p = p->ai_next){
    s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if(s < 0) continue;
    if(connect(s, p->ai_addr, p->ai_addrlen) == 0){
      freeaddrinfo(res);
      return s;
    }
    close(s);
    s = -1;
  }

  freeaddrinfo(res);
  return -1;
}

int main(int argc, char **argv){
  int opt, show_all = 0;
  const char *addr = NULL;
  const char *ifname = NULL;

  while((opt = getopt(argc, argv, "n:ai:")) != -1){
    if(opt == 'n') addr = optarg;
    else if(opt == 'a') show_all = 1;
    else if(opt == 'i') ifname = optarg;
    else { usage(argv[0]); return 2; }
  }

  if(!addr || (show_all && ifname) || (!show_all && !ifname)){
    usage(argv[0]);
    return 2;
  }

  int s = connect_to(addr);
  if(s < 0){
    perror("connect");
    return 1;
  }

  char req[256];
  if(show_all) snprintf(req, sizeof(req), "ALL\n");
  else snprintf(req, sizeof(req), "IF %s\n", ifname);

  if(send(s, req, strlen(req), 0) < 0){
    perror("send");
    close(s);
    return 1;
  }

  // Lire jusqu’à EOF
  char buf[2048];
  while(1){
    ssize_t r = recv(s, buf, sizeof(buf), 0);
    if(r == 0) break;
    if(r < 0){
      if(errno == EINTR) continue;
      perror("recv");
      close(s);
      return 1;
    }
    fwrite(buf, 1, (size_t)r, stdout);
  }

  close(s);
  return 0;
}
```

#### Explication

##### Constante du port

```c
#define PORT_STR "9090"
```

- Port TCP de l'agent serveur
- Le client se connecte à `<addr>:9090`



##### Fonction `usage` : aide d'utilisation

```c
static void usage(const char *p){
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  %s -n <addr> -a\n", p);
  fprintf(stderr, "  %s -n <addr> -i <ifname>\n", p);
}
```

- Affiche les deux syntaxes valides
- Appelée quand les deux options sont invalides (manque `-n`, mélange `-a` et `-i`)



##### Fonction `connect_to` : se connecter à la machine distante

```bash
static int connect_to(const char *addr){
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
```

- `AF_UNSPEC` : autorise IPv4 et IPv6
- `SOCK_STREAM` : TCP



**Résolution DNS / IP**

```c
int rc = getaddrinfo(addr, PORT_STR, &hints, &res);
```

- `getaddrinfo` retourne une liste d’adresses possibles (IPv4/IPv6)
- Le client essaie de se connecter à chacune jusqu’à réussite



**Boucle de connexion**

```c
for(struct addrinfo *p = res; p; p = p->ai_next){
  s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
  if(s < 0) continue;
  if(connect(s, p->ai_addr, p->ai_addrlen) == 0){
    freeaddrinfo(res);
    return s;
  }
  close(s);
  s = -1;
}
```

- Créer un socket
- Tente `connect()`
- Si échec : ferme et tente l'adresse suivante
- Si succès : retourne le descripteur de socket



**Si aucune connexion ne marche**

```c
freeaddrinfo(res);
return -1;
```



##### Fonction `main` : parsing + requête + affichage

###### Variables de contrôle

```c
int opt, show_all = 0;
const char *addr = NULL;
const char *ifname = NULL;
```

- `addr` : valeur de `-n` (adresse de la machine distante)
- `show_all` : activé si `-a``
- ``ifname` : valeur si `-i ifname`



###### Parsing des options 

```c
while((opt = getopt(argc, argv, "n:ai:")) != -1){
  if(opt == 'n') addr = optarg;
  else if(opt == 'a') show_all = 1;
  else if(opt == 'i') ifname = optarg;
  else { usage(argv[0]); return 2; }
}
```

- `"n:ai:"` :
  - `-n` nécessite un argument (`addr`)
  - `-a` sans argument
  - `-i` nécessite un argument (`ifname`)
- `optarg` contient la valeur associée à `-n` ou `-i`.



###### Validation des combinaisons

```c
if(!addr || (show_all && ifname) || (!show_all && !ifname)){
  usage(argv[0]);
  return 2;
}
```

- Interdit : pas de `-n`
- Interdit : `-a` et `-i` ensemble
- Interdit : ni `-a` ni `-i`
- Valide : `-n addr -a` **ou** `-n addr -i ifname`



###### Connexion au serveur

```c
int s = connect_to(addr);
if(s < 0){
  perror("connect");
  return 1;
}
```

- Se connecte à `addr:9090`.
- Si échec : message d’erreur.



##### Construction et envoi de la requête 

```c
char req[256];
if(show_all) snprintf(req, sizeof(req), "ALL\n");
else snprintf(req, sizeof(req), "IF %s\n", ifname);
```

- Si `-a` : envoie la commande protocole `ALL\n` 
- Si `-i ifname` : envoie `IF ifname\n`



**Envoi TCP**

```c
if(send(s, req, strlen(req), 0) < 0){
  perror("send");
  close(s);
  return 1;
}
```

- Envoie la requête au serveur



##### Réception de la réponse et affichage

```c
char buf[2048];
while(1){
  ssize_t r = recv(s, buf, sizeof(buf), 0);
  if(r == 0) break;
  if(r < 0){
    if(errno == EINTR) continue;
    perror("recv");
    close(s);
    return 1;
  }
  fwrite(buf, 1, (size_t)r, stdout);
}
```

- Lit en boucle la réponse jusqu’à la fermeture de la connexion (EOF)
- Chaque chunk reçu est écrit immédiatement sur stdout 
- La fin de réponse est naturellement indiquée par `recv == 0` (connexion fermée par le serveur)



**Fermeture**

```c
close(s);
return 0;
```

- Ferme la socket
- Retourne 0 si succès



#### Resume

- Parse `-n addr` et `-a`/`-i`.
- Se connecte à `addr:9090` (IPv4 ou IPv6 grâce à `getaddrinfo`).
- Envoie `ALL\n` ou `IF ifname\n`.
- Lit la réponse jusqu’à EOF et l’affiche telle quelle.

---

### Code `ifshow.c`

```c
// ifshow.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

static int bitcount8(unsigned char v){
  int n=0;
  while(v){
    n += v & 1u;   // compte le bit courant
    v >>= 1u;      // passe au bit suivant
  }
  return n;
}

static int netmask_prefix(const struct sockaddr *mask){
  if(!mask) return 0; // si pas de masque

  if(mask->sa_family == AF_INET){
    const unsigned char *m = (const unsigned char*)&((const struct sockaddr_in*)mask)->sin_addr;
    // calcule /prefix IPv4 en comptant les bits à 1
    return bitcount8(m[0]) + bitcount8(m[1]) + bitcount8(m[2]) + bitcount8(m[3]);
  }

  if(mask->sa_family == AF_INET6){
    const unsigned char *m = (const unsigned char*)&((const struct sockaddr_in6*)mask)->sin6_addr;
    // calcule /prefix IPv6 en comptant les bits à 1
    int p=0;
    for(int i=0;i<16;i++) p += bitcount8(m[i]);
    return p;
  }

  return 0;
}

static void print_ip_line(struct ifaddrs *e, int grouped){
  int af = e->ifa_addr->sa_family;   // IPv4 ou IPv6
  char ip[INET6_ADDRSTRLEN];         // buffer pour l'adresse en texte

  // récupère l'adresse (IPv4 ou IPv6)
  void *addr = (af==AF_INET)
    ? (void*)&((struct sockaddr_in*)e->ifa_addr)->sin_addr
    : (void*)&((struct sockaddr_in6*)e->ifa_addr)->sin6_addr;

  // conversion binaire -> texte
  if(!inet_ntop(af, addr, ip, sizeof(ip))) return;

  if(grouped) printf("  "); // indentation en mode -a
  printf("%s %s/%d\n", (af==AF_INET)?"IPv4":"IPv6", ip, netmask_prefix(e->ifa_netmask));
}

/* 1. retirer static pour que l’agent puisse appeler cette fonction */
void run_show(const char *filter, int show_all){
  struct ifaddrs *list=NULL;

  // récupère la liste des interfaces/adresses
  if(getifaddrs(&list)==-1){ perror("getifaddrs"); exit(1); }

  const char *prev=NULL; // dernière interface affichée (mode -a)

  for(struct ifaddrs *cur=list; cur; cur=cur->ifa_next){
    if(!cur->ifa_addr) continue; // entrée sans adresse

    int af = cur->ifa_addr->sa_family;
    if(af!=AF_INET && af!=AF_INET6) continue; // seulement IPv4/IPv6

    if(filter && strcmp(cur->ifa_name, filter)!=0) continue; // filtre -i

    // affiche le nom de l'interface une seule fois en -a
    if(show_all && (!prev || strcmp(prev, cur->ifa_name)!=0)){
      printf("%s:\n", cur->ifa_name);
      prev = cur->ifa_name;
    }

    // affiche l'adresse de cette entrée
    print_ip_line(cur, show_all);
  }

  freeifaddrs(list); // libère la mémoire
}

/* 2. entourer main pour pouvoir inclure ifshow.c dans l’agent */
#ifndef IFSHOW_NO_MAIN
int main(int argc, char *argv[]){
  int opt, show_all=0;
  const char *ifname=NULL;

  // lit -a ou -i <ifname>
  while((opt=getopt(argc, argv, "ai:"))!=-1){
    if(opt=='a') show_all=1;
    else if(opt=='i') ifname=optarg;
    else { fprintf(stderr,"Usage: %s -a | %s -i <ifname>\n", argv[0], argv[0]); return 2; }
  }

  // refuse les cas invalides (aucune option, ou -a et -i ensemble)
  if((show_all && ifname) || (!show_all && !ifname)){
    fprintf(stderr,"Usage: %s -a | %s -i <ifname>\n", argv[0], argv[0]);
    return 2;
  }

  run_show(ifname, show_all); // exécute l'affichage
  return 0;
}
#endif
```

#### Explications

##### Modification de `run_show` : suppression de `static`

**Avant**

```c
static void run_show(const char *filter, int show_all){
  ...
}
```

- `static` permettait que `run_show()` ne soit visible que dans `ifshow.c`

- de ce fait un autre fichier ne pourrait pas faire appel à `run_show()`



**Après**

```c
void run_show(const char *filter, int show_all){
  ...
}
```

- on retire `static`
- la fonction devient visible
- le service `ifnetshowd` peut appeler `run_show()` pour afficher les informations demandées

---

##### Ajout autour de `main` : compilation conditionnelle avec `IFSHOW_NO_MAIN`

Le service `ifnetshowd` inclut `ifshow.c` directement.

```c
#define IFSHOW_NO_MAIN
#include "ifshow.c"
```

**Problème**

Si `ifshow.c` contient un `main()`, alors le programme `ifnetshowd` aurait deux fonctions `main()` :

- celle de `ifnetshowd.c`
- celle de `ifshow.c`

Ce qui créera une erreur de compilation.



**Solution**

On entoure `main` avec un `#ifndef`

```c
#ifndef IFSHOW_NO_MAIN
int main(int argc, char *argv[]){
  ...
}
#endif
```

- `#ifndef IFSHOW_NO_MAIN` permet de compiler le bloc si et seulement si `IFSHOW_NO_MAIN` n'est pas défini



**Cas 1 : compilation normale de `ifshow`**

- `IFSHOW_NO_MAIN` n’est pas défini
- le `main()` est compilé
- `ifshow` fonctionne comme avant



**Cas 2 : compilation de `ifnetshowd`**

- `ifnetshowd.c` fait `#define IFSHOW_NO_MAIN` puis `#include "ifshow.c"`
- `IFSHOW_NO_MAIN` est défini
- le `main()` de `ifshow.c` n’est **pas** compilé
- `ifnetshowd` récupère uniquement les fonctions utiles (dont `run_show`) sans conflit

---

### 6.2 Intégrer la commande *ifnetshow* et son agent (persistant ou non) au système VyOS du mini-projet 1

#### Préparer les binaires

Sur la machine de développement on compile :

```shell
gcc -Wall -Wextra -O2 ifnetshow.c  -o ifnetshow
gcc -Wall -Wextra -O2 ifnetshowd.c -o ifnetshowd
```

> VyOS étant basé sur Debian, copier des binaires compilés sur Debian x86_64 marche généralement bien

---

#### Créer l'emplacement persistant

Sur VyOS :

```shell
mkdir -p /config/scripts/tools
```

---

#### Copier les binaires avec HTTP

Sur la machine de développement :

```shell
python3 -m http.server 8000
```



Sur VyOS :

```
curl -L http://10.0.2.8:8000/ifnetshow  -o /config/scripts/tools/ifnetshow
curl -L http://10.0.2.8:8000/ifnetshowd -o /config/scripts/tools/ifnetshowd
chmod 755 /config/scripts/tools/ifnetshow /config/scripts/tools/ifnetshowd
```

---

#### Créer le script de boot

On édite le fichier `/config/scripts/vyos-postconfig-bootup.script` :

```shell
#!/bin/sh
# Projet 06 - intégration ifnetshow/ifnetshowd

# 1. Binaires exécutables
install -m 0755 /config/scripts/tools/ifnetshow  /usr/local/bin/ifnetshow
install -m 0755 /config/scripts/tools/ifnetshowd /usr/local/bin/ifnetshowd

# 2. Démarrer l'agent si il n'est pas lancé
if ! pgrep -x ifnetshowd >/dev/null 2>&1; then
  nohup /usr/local/bin/ifnetshowd >/tmp/ifnetshowd.log 2>&1 &
fi
```

On le rend ensuite exécutable :

```shell
sudo chmod +x /config/scripts/vyos-postconfig-bootup.script
```

---

#### Test immédiat

```shell
sudo /config/scripts/vyos-postconfig-bootup.script
```



On vérifie que l'agent tourne :

```shell
pgrep -a ifnetshowd
sudo ss -lntp | grep 9090 || sudo netstat -lntp | grep 9090
```



On vérifie que les commandes sont bien disponibles :

```shell
which ifnetshow
which ifnetshowd
```

---

#### Tests locaux

Depuis VyOS :

```shell
ifnetshow -n 127.0.0.1 -a
ifnetshow -n 127.0.0.1 -i eth0
```



Depuis la machine de développement :

```shell
ifnetshow -n 10.0.2.9 -a
ifnetshow -n 10.0.2.9 -i eth0
```

Tout fonctionne.

---

### 6.3 Intégrer la commande *ifnetshow* et son agent (persistant ou non) au système Alpine du mini-projet 2

#### Récupérer les scripts

Sur la machine de développement :

```shell
python3 -m http.server 8000
```



Sur Alpine :

```shell
mkdir -p /root/tools
cd /root/tools

wget http://10.0.2.8:8000/ifshow.c -o ifshow.c
wget http://10.0.2.8:8000/ifnetshow.c -o ifnetshow.c
wget http://10.0.2.8:8000/ifnetshowd.c -o ifnetshowd.c
```

---

#### Installer les outils de compilation

Sur Alpine :

```shell
apk update
apk add build-base
```

---

#### Compilation

```shell
gcc -Wall -Wextra -O2 ifnetshow.c  -o ifnetshow
gcc -Wall -Wextra -O2 ifnetshowd.c -o ifnetshowd
```

---

#### Test

##### Lancer l'agent

```shell
./ifnetshowd
```



##### Lancer l'agent en arrière plan

```shell
nohup ./ifnetshowd >/tmp/ifnetshowd.log 2>&1 &
```



##### Test du client en local

```shell
./ifnetshow -n 127.0.0.1 -a
./ifnetshow -n 127.0.0.1 -i eth0
```

---

#### Intégrer dans le système

On installe les binaires dans `/usr/local/bin` :

```shell
install -m 0755 ifnetshow  /usr/local/bin/ifnetshow
install -m 0755 ifnetshowd /usr/local/bin/ifnetshowd
```

---

#### Démarrage automatique (OpenRC)

Alpine utilise **OpenRC**. De ce fait on crée un service `ifnetshowd`.

`cat >/etc/init.d/ifnetshowd <<'EOF'`

```shell
#!/sbin/openrc-run

name="ifnetshowd"
description="Agent projet 06 (ifnetshowd)"
command="/usr/local/bin/ifnetshowd"
command_background="yes"
pidfile="/run/ifnetshowd.pid"
output_log="/var/log/ifnetshowd.log"
error_log="/var/log/ifnetshowd.log"

depend() {
  need net
}

start_pre() {
  checkpath -f -m 0644 -o root:root /var/log/ifnetshowd.log
}
EOF
```

`chmod +x /etc/init.d/ifnetshowd`

#### Activer au boot

```shell
rc-update add ifnetshowd default
rc-service ifnetshowd start
```

---

#### Test depuis une autre machine

Depuis la machine de développement :

```shell
ifnetshow -n 10.0.2.14 -a
ifnetshow -n 10.0.2.14 -i eth0
```

---

### 6.4 Intégrer la commande *ifnetshow* et son agent (persistant ou non) au système MicroCore du mini-projet 3

#### Récupérer les scripts

Sur la machine de développement :

```shell
python3 -m http.server 8000
```

 

Sur Microcore :

```shell
cd /home/tc
wget http://10.0.2.15:8000/ifshow.c     -O ifshow.c
wget http://10.0.2.15:8000/ifnetshow.c  -O ifnetshow.c
wget http://10.0.2.15:8000/ifnetshowd.c -O ifnetshowd.c
```

---

#### Installer l'environnement graphique de compilation

```shell
tce-load -wi compiletc
```

----

#### Compiler

```shell
gcc -std=gnu99 -Wall -Wextra -O2 ifnetshow.c  -o ifnetshow
gcc -std=gnu99 -Wall -Wextra -O2 ifnetshowd.c -o ifnetshowd
```

----

#### Test

```shell
./ifnetshowd
```

L'agent écoute bien sur le port 9090.

---

#### Test en local

```shell
./ifnetshow -n 127.0.0.1 -a
./ifnetshow -n 127.0.0.1 -i eth0
```

----

#### Intégration

```shell
mkdir -p /home/tc/bin
cp /home/tc/ifnetshow  /home/tc/bin/ifnetshow
cp /home/tc/ifnetshowd /home/tc/bin/ifnetshowd
chmod 755 /home/tc/bin/ifnetshow /home/tc/bin/ifnetshowd
```



Ajout de `/home/tc/bin` au PATH :

```shell
echo 'export PATH=$PATH:/home/tc/bin' >> /home/tc/.profile
. /home/tc/.profile
```



**Test**

```shell
ifnetshow -n 127.0.0.1 -a
```

---

#### Démarrage automatique de l'agent au boot

`sudo vi /opt/bootlocal.sh`

```shell
#!/bin/sh

export PATH=$PATH:/home/tc/bin

if ! ps | grep -q "[i]fnetshowd"; then
  nohup /home/tc/bin/ifnetshowd >/tmp/ifnetshowd.log 2>&1 &
fi
```

`sudo chmod +x /opt/bootlocal.sh`

---

#### Persistance

On sauvegarde :

```shell
filetool.sh -b
```

---

#### Test

Depuis la machine de développement :

```shell
ifnetshow -n 10.0.2.16 -a
ifnetshow -n 10.0.2.16 -i eth0
```

Tout fonctionne.

---

Ce mini projet est terminé.
