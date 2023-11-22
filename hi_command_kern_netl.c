/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#include <linux/string.h>
#include "hifs.h"

// Test Data
struct hifs_inode  test_inode = 
{

    .i_version = 1,	    /* inode version */
	.i_flags = 100,	    /* inode flags: TYPE */
	.i_mode = 777,		/* File mode */
	.i_ino = 1,		    /* inode number */
	.i_uid = 10,		    /* owner's user id */
	.i_hrd_lnk = 1,	    /* number of hard links */
	.i_ctime = 23333333,	/* Creation time */
	.i_mtime = 23333333,	/* Modification time*/
	.i_size = 12,	
    .i_name = "test",
    .i_addrb = {4},
    .i_addre = {5},
};


// Define the policy
static struct nla_policy hifs_netl_genl_policy[__HIFS_NETL_ATB_MAX + 1] = 
{
    [HIFS_NETL_ATB_IVERS] = { .type = NLA_U8 },
    [HIFS_NETL_ATB_IFLAGS] = { .type = NLA_U8 },
    [HIFS_NETL_ATB_IMODE] = { .type = NLA_U32 },
    [HIFS_NETL_ATB_I_ID] = { .type = NLA_U32 },
    [HIFS_NETL_ATB_IUID] = { .type = NLA_U16 },
    [HIFS_NETL_ATB_IGID] = { .type = NLA_U16 },
    [HIFS_NETL_ATB_IHRD_LNK] = { .type = NLA_U16 },
    [HIFS_NETL_ATB_ICTIME] = { .type = NLA_U32 },
    [HIFS_NETL_ATB_IMTIME] = { .type = NLA_U32 },
    [HIFS_NETL_ATB_ISIZE] = { .type = NLA_U32 },
    [HIFS_NETL_ATB_INAME] = { .type = NLA_STRING, .len = 50 },
    [HIFS_NETL_ATB_IADDRB] = { .type = NLA_U32 },
    [HIFS_NETL_ATB_IADDRE] = { .type = NLA_U32 },
};

// Define the operations
static struct genl_ops hifs_netl_ops[] = 
{
    {
        .cmd = HIFS_NETL_COM_SEND_ACK,
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
    struct genl_info *info; /*  The Generic Netlink Info  */
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
    rc = nla_put_u8(skb, HIFS_NETL_ATB_IVERS, test_inode.i_version);
    if (rc != 0) goto failure;
    
    rc = nla_put_u8(skb, HIFS_NETL_ATB_IFLAGS, test_inode.i_flags);
    if (rc != 0) goto failure;

    rc = nla_put_u32(skb,  HIFS_NETL_ATB_IMODE, test_inode.i_mode);
    if (rc != 0) goto failure;

    rc = nla_put_u32(skb, HIFS_NETL_ATB_I_ID, test_inode.i_ino);
    if (rc != 0) goto failure;

    rc = nla_put_u16(skb,  HIFS_NETL_ATB_IUID, test_inode.i_uid);
    if (rc != 0) goto failure;

    rc = nla_put_u16(skb,  HIFS_NETL_ATB_IGID, test_inode.i_gid);
    if (rc != 0) goto failure;

    rc = nla_put_u16(skb, HIFS_NETL_ATB_IHRD_LNK, test_inode.i_hrd_lnk);
    if (rc != 0) goto failure;

    rc = nla_put_u32(skb,  HIFS_NETL_ATB_ICTIME, test_inode.i_ctime);
    if (rc != 0) goto failure;

    rc = nla_put_u32(skb,  HIFS_NETL_ATB_IMTIME, test_inode.i_mtime);
    if (rc != 0) goto failure;

    rc = nla_put_u32(skb,  HIFS_NETL_ATB_ISIZE, test_inode.i_size);
    if (rc != 0) goto failure;

    rc = nla_put_string(skb, HIFS_NETL_ATB_INAME, test_inode.i_name);
    if (rc != 0) goto failure;

    rc = nla_put(skb, HIFS_NETL_ATB_IADDRB, sizeof(test_inode.i_addrb), test_inode.i_addrb);
    if (rc != 0) goto failure;

    rc = nla_put(skb, HIFS_NETL_ATB_IADDRB, sizeof(test_inode.i_addre), test_inode.i_addre);
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

int hifs_netl_rcv_acknowledge_command(struct sk_buff *skb, struct genl_info *info)
{
    // Check if the acknowledgement attribute is present
    if (info->attrs[HIFS_NETL_COM_SEND_ACK]) {
        printk(KERN_INFO "Recieved Acknowledgement\n");
    }

    return 0;
}