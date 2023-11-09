#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <libpq-fe.h>

#define NETLINK_USER 31

// Now that I've already written two C files for this, do I want to convert it to C++?
// Being object based would have some good advantages, ehh......

void handle_netlink_msg(int sock_fd) {
    struct nlmsghdr *nlh;
    struct sockaddr_nl dest_addr;
    struct iovec iov;
    struct msghdr msg;
    int ret;

    // Allocate memory for message buffer
    nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(1024));
    memset(nlh, 0, NLMSG_SPACE(1024));
    nlh->nlmsg_len = NLMSG_SPACE(1024);
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 0;

    // Set up message structure
    memset(&iov, 0, sizeof(iov));
    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    // Read message from socket
    ret = recvmsg(sock_fd, &msg, 0);
    if (ret < 0) {
        printf("Error receiving message.\n");
    } else {
        printf("Received message: %s\n", NLMSG_DATA(nlh));
        execute_sql(NLMSG_DATA(nlh));
    }

    free(nlh);
}

void execute_sql(char* sql) {
    PGconn *conn = PQconnectdb("dbname=test user=postgres password=secret hostaddr=127.0.0.1 port=5432");
    if (PQstatus(conn) == CONNECTION_BAD) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        return;
    }

    PGresult *res = PQexec(conn, sql);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "SQL execution failed: %s\n", PQerrorMessage(conn));
        PQclear(res);
        PQfinish(conn);
        return;
    }

    PQclear(res);
    PQfinish(conn);
}

int main() {
    struct sockaddr_nl src_addr;
    int sock_fd;

    // Initialize netlink socket
    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_USER);
    if (sock_fd < 0)
        return -1;

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid();

    bind(sock_fd, (struct sockaddr*)&src_addr, sizeof(src_addr));

    while (1) {
        // Wait for incoming connections
        handle_netlink_msg(sock_fd);
    }

    close(sock_fd);
    return 0;
}