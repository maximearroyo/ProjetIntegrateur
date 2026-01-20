// agent_ifshow.c
int main(void) {
    int listen_fd = socket(AF_INET6, SOCK_STREAM, 0);   // ou AF_INET
    // bind sur :: ou 0.0.0.0, port 5555, puis listen()

    for (;;) {
        int client_fd = accept(listen_fd, NULL, NULL);

        FILE *in  = fdopen(client_fd, "r");
        FILE *out = fdopen(dup(client_fd), "w");

        char cmd[256], ifname[IFNAMSIZ];

        if (fscanf(in, "%255s", cmd) != 1) {
            fclose(in); fclose(out); close(client_fd);
            continue;
        }

        if (strcmp(cmd, "IFNAME") == 0) {
            if (fscanf(in, "%s", ifname) == 1) {
                show_interface_prefixes(ifname, out);
            }
        } else if (strcmp(cmd, "ALL") == 0) {
            show_all_with_addresses(out);
        }

        fflush(out);
        fclose(in);
        fclose(out);
        close(client_fd);
    }
}
