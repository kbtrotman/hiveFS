
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/genetlink.h>
#include <linux/string.h>

// Define the payload
enum {
    HIFS_NETL_A_UNSPEC,
    HIFS_NETL_A_IMODE,
    HIFS_NETL_A_IUID,
    HIFS_NETL_A_IGID,
    HIFS_NETL_A_ISIZE,
    __HIFS_NETL_A_MAX,
};
#define HIFS_NETL_A_MAX (__HIFS_NETL_A_MAX - 1)

// Define the policy
static struct nla_policy hifs_netl_genl_policy[HIFS_NETL_A_MAX + 1] = {
    [HIFS_NETL_A_IMODE] = { .type = NLA_U32 },
    [HIFS_NETL_A_IUID] = { .type = NLA_U32 },
    [HIFS_NETL_A_IGID] = { .type = NLA_U32 },
    [HIFS_NETL_A_ISIZE] = { .type = NLA_U64 },
};
// Define the commands
enum {
    HIFS_NETL_C_UNSPEC,
    HIFS_NETL_C_ECHO,
    __HIFS_NETL_C_MAX,
};

// Define the operations
static struct genl_ops hifs_netl_ops[] = {
    {
        .cmd = HIFS_NETL_C_ACK,
        .flags = 0,
        .policy = hifs_netl_genl_policy,
        .doit = hifs_netl_recv_ack,
        .dumpit = NULL,
    },
    // Other operations...
};

#define HIFS_NETL_C_MAX (__HIFS_NETL_C_MAX - 1)

// Define the family
static struct genl_family hifs_netl_gnl_family = {
    .hdrsize = 0,
    .name = "HIFS_NETL",
    .version = 1,
    .maxattr = HIFS_NETL_A_MAX,
    .ops = hifs_netl_ops,
    .n_ops = ARRAY_SIZE(hifs_netl_ops),
};

// Function to send SQL request
void send_sql_req(char *payload) {
    struct sk_buff *skb;
    void *msg_head;
    int rc;

    // Allocate a new message
    skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (skb == NULL)
        return;

    // Add the message headers
    msg_head = genlmsg_put(skb, 0, 0, &hifs_netl_gnl_family, 0, HIFS_NETL_C_ECHO);
    if (msg_head == NULL) {
        rc = -ENOMEM;
        goto failure;
    }

// Add the inode attributes
    rc = nla_put_u32(skb, HIFS_NETL_A_IMODE, inode->i_mode);
    if (rc != 0)
        goto failure;

    rc = nla_put_u32(skb, HIFS_NETL_A_IUID, inode->i_uid);
    if (rc != 0)
        goto failure;

    rc = nla_put_u32(skb, HIFS_NETL_A_IGID, inode->i_gid);
    if (rc != 0)
        goto failure;

    rc = nla_put_u64(skb, HIFS_NETL_A_ISIZE, inode->i_size);
    if (rc != 0)
        goto failure;

    // Finalize the message
    genlmsg_end(skb, msg_head);

    // Send the message
    rc = genlmsg_unicast(genl_info_net(info), skb, info->snd_portid);
    if (rc != 0)
        goto failure;

    // Send the message
    rc = genlmsg_unicast(genl_info_net(info), skb, info->snd_portid);
    if (rc != 0)
        goto failure;

    return;

failure:
    // Free the message on failure
    nlmsg_free(skb);
}

// Define the callback function
int hifs_netl_recv_ack(struct sk_buff *skb, struct genl_info *info) {
    // Check if the acknowledgement attribute is present
    if (info->attrs[HIFS_NETL_A_ACK]) {
        // Process the acknowledgement...
    }

    return 0;
}