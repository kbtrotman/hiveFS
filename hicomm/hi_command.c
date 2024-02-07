/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <libpq-fe.h>
#include <netlink/socket.h>
#include <netlink/netlink.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>

#include "hi_command.h"


static int send_echo_msg(struct nl_sock *sk, int fam)
{
	int	       err = 0;
	struct nl_msg *msg = nlmsg_alloc();
	if (!msg) {
		return -ENOMEM;
	}

	/* Put the genl header inside message buffer */
	void *hdr = genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, fam, 0, 0,
				HIFS_GENL_CDM_SEND_INODE_ONLY, HIFS_GENL_VERSION);
	if (!hdr) {
		return -EMSGSIZE;
	}

	/* Put the string inside the message. */
	err = nla_put_string(msg, HIFS_GENL_ATB_INAME, "Hello from User Space, Netlink!");
	if (err < 0) {
		return -err;
	}
	printf("message sent\n");

	/* Send the message. */
	err = nl_send_auto(sk, msg);
	err = err >= 0 ? 0 : err;

	nlmsg_free(msg);

	return err;
}


static int echo_reply_handler(struct nl_msg *msg, void *arg)
{
    int err	 = 0;
    struct nlattr *tb[HIFS_GENL_ATB_MAX + 1];
 	struct genlmsghdr *genlhdr = nlmsg_data(nlmsg_hdr(msg));

    // Set up message structure
    //memset(&iov, 0, sizeof(iov));
    //iov.iov_base = (void *)nlh;
    //iov.iov_len = nlh->nlmsg_len;
    //memset(&msg, 0, sizeof(msg));
    //msg.msg_name = (void *)&dest_addr;
    //msg.msg_namelen = sizeof(dest_addr);
    //msg.msg_iov = &iov;
    //msg.msg_iovlen = 1;

	/* Parse the attributes */
	err = nla_parse(tb, HIFS_GENL_ATB_ISIZE, genlmsg_attrdata(genlhdr, 0),
			genlmsg_attrlen(genlhdr, 0), NULL);
	if (err) {
		prerr("unable to parse message: %s\n", strerror(-err));
		return NL_SKIP;
    } else {
        nla_parse(tb, HIFS_GENL_ATB_MAX, genlmsg_attrdata(genlhdr, 0), genlmsg_attrlen(genlhdr, 0), NULL);

        if (tb[HIFS_GENL_ATB_IMODE]) {
            printf("Received i_mode: %u\n", nla_get_u32(tb[HIFS_GENL_ATB_IMODE]));
        }

        if (tb[HIFS_GENL_ATB_IUID]) {
            printf("Received i_uid: %u\n", nla_get_u32(tb[HIFS_GENL_ATB_IUID]));
        }

        if (tb[HIFS_GENL_ATB_IGID]) {
            printf("Received i_gid: %u\n", nla_get_u32(tb[HIFS_GENL_ATB_IGID]));
        }

        if (tb[HIFS_GENL_ATB_ISIZE]) {
            printf("Received i_size: %lu\n", nla_get_u64(tb[HIFS_GENL_ATB_ISIZE]));
        }
    }
    return 0;
}


/* Modify the callback for replies to handle all received messages */
static inline int set_cb(struct nl_sock *sk)
{
	return -nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, echo_reply_handler, NULL);
}


static int conn(struct nl_sock **sk)
{
	*sk = nl_socket_alloc();
	if (!sk) {
		return -ENOMEM;
	}

	return genl_connect(*sk);
}


/* Disconnect and release socket */
static void disconn(struct nl_sock *sk)
{
	nl_close(sk);
	nl_socket_free(sk);
}


int main()
{
    struct nl_sock *ucsk, *mcsk;
    int ret;

    // Initialize netlink sockets
    if ((ret = conn(&ucsk)) || (ret = conn(&mcsk))) {
	    prerr("failed to connect to generic netlink\n");
	    goto out;
    }

    int fam = genl_ctrl_resolve(ucsk, HIFS_GENL_NAME);
    if (fam < 0) {
	    prerr("failed to resolve generic netlink family: %s\n", strerror(-fam));
	    goto out;
    }

    if ((ret = set_cb(ucsk)) || (ret = set_cb(mcsk))) {
	    prerr("failed to set callback: %s\n", strerror(-ret));
	    goto out;
    }

    /* Send unicast message and listen for response. */
    if ((ret = send_echo_msg(ucsk, fam))) {
	    prerr("failed to send message: %s\n", strerror(-ret));
    }

    printf("listening for messages\n");
    while (1) {
        nl_recvmsgs_default(ucsk);
    }

out:
    disconn(ucsk);
    disconn(mcsk);
    return 0;
}