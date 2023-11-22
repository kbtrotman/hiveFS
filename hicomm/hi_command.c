/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <libpq-fe.h>

#include "hi_command.h"



void handle_netlink_msg(int sock_fd) 
{
    struct nlmsghdr *nlh;
    struct nlattr *attrs[HIFS_NETL_ATB_MAX + 1];
    struct genlmsghdr *gnlh;
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
        gnlh = nlmsg_data(nlh);
        nla_parse(attrs, HIFS_NETL_ATB_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

        if (attrs[HIFS_NETL_ATB_IMODE]) {
            printf("Received i_mode: %u\n", nla_get_u32(attrs[HIFS_NETL_ATB_IMODE]));
        }

        if (attrs[HIFS_NETL_ATB_IUID]) {
            printf("Received i_uid: %u\n", nla_get_u32(attrs[HIFS_NETL_ATB_IUID]));
        }

        if (attrs[HIFS_NETL_ATB_IGID]) {
            printf("Received i_gid: %u\n", nla_get_u32(attrs[HIFS_NETL_ATB_IGID]));
        }

        if (attrs[HIFS_NETL_ATB_ISIZE]) {
            printf("Received i_size: %lu\n", nla_get_u64(attrs[HIFS_NETL_ATB_ISIZE]));
        }
    }

    free(nlh);
}

int main()
{
    struct sockaddr_nl src_addr;
    int sock_fd;

    // Initialize netlink socket
    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK__HIVE__USER);
    if (sock_fd < 0) return -1;

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