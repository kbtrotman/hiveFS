
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/genetlink.h>
#include <linux/string.h>
#include "hive_fs_defs.h"

// Test Data
struct inode 
{
    unsigned long id;
    char i_name[50];
    unsigned int i_mode;
    unsigned int i_uid;
    unsigned int i_gid;
    unsigned long long i_size;
} inode = {
    .id = 1,
    .i_name = "test",
    .i_mode = 0755,
    .i_uid = 1000,
    .i_gid = 1000,
    .i_size = 1024,
};


// Define the policy
static struct nla_policy hifs_netl_genl_policy[HIFS_NETL_ATB_MAX + 1] = 
{
    [hive_payload.HIFS_NETL_ATB_I_ID] = { .type = NLA_U64 },
    [hive_payload.HIFS_NETL_ATB_INAME] = { .type = char(50) },
    [hive_payload.HIFS_NETL_ATB_IMODE] = { .type = NLA_U32 },
    [hive_payload.HIFS_NETL_ATB_IUID] = { .type = NLA_U32 },
    [hive_payload.HIFS_NETL_ATB_IGID] = { .type = NLA_U32 },
    [hive_payload.HIFS_NETL_ATB_ISIZE] = { .type = NLA_U64 },
};

// Define the operations
static struct genl_ops hifs_netl_ops[] = 
{
    {
        .cmd = hive_commands.HIFS_NETL_COM_SEND_ACK,
        .flags = 0,
        .policy = hifs_netl_genl_policy,
        .doit = hifs_netl_rcv_acknowledge_command,
        .dumpit = NULL,
    },
};

// Define the family
static struct genl_family hifs_netl_gnl_family = 
{
    .hdrsize = 0,
    .name = "HIFS_NETL",
    .version = 1,
    .maxattr = HIFS_NETL_ATB_MAX,
    .ops = hifs_netl_ops,
    .n_ops = ARRAY_SIZE(hifs_netl_ops),
};

// Function to send request
void hifs_netl_send_command_req(char *hive_payload)
{
    struct sk_buff *skb;    /*  The Socket Buffer  */
    void *msg_head;
    int rc;                 /*  Return Code  */

    // Allocate a new message
    skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (skb == NULL) return;

    // Add the message headers
    msg_head = genlmsg_put(skb, 0, 0, &hifs_netl_gnl_family, 0, HIFS_NETL_COM_SET_LINK_PULSE);
    if (msg_head == NULL) {
        rc = -ENOMEM;
        goto failure;
    }

    // Add the inode attributes
    rc = nla_put_u32(skb, hive_payload.HIFS_NETL_ATB_I_ID, inode->id);
    if (rc != 0) goto failure;

        rc = nla_put_u32(skb,  hive_payload.HIFS_NETL_ATB_INAME, inode->i_name);
    if (rc != 0) goto failure;

    rc = nla_put_u32(skb,  hive_payload.HIFS_NETL_ATB_IMODE, inode->i_mode);
    if (rc != 0) goto failure;

    rc = nla_put_u32(skb,  hive_payload.HIFS_NETL_ATB_IUID, inode->i_uid);
    if (rc != 0) goto failure;

    rc = nla_put_u32(skb,  hive_payload.HIFS_NETL_ATB_IGID, inode->i_gid);
    if (rc != 0) goto failure;

    rc = nla_put_u64(skb,  hive_payload.HIFS_NETL_ATB_ISIZE, inode->i_size);
    if (rc != 0) goto failure;

    // Finalize the message
    genlmsg_end(skb, msg_head);

    // Send the message
    rc = genlmsg_unicast(genl_info_net(info), skb, info->snd_portid);
    if (rc != 0) goto failure;

    return;

failure:
    // Free the message on failure
    nlmsg_free(skb);
}

void hifs_netl_rcv_acknowledge_command(struct sk_buff *skb, struct genl_info *info)
{
    // Check if the acknowledgement attribute is present
    if (info->attrs[hive_payload.HIFS_NETL_ATB_ACK]) {
        printf("Recieved Acknowledgement\n");
    }

    return 0;
}