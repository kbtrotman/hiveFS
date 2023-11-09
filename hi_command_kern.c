#include <net/genetlink.h>

// Define the attributes (payload)

enum {
    DOC_EXMPL_A_UNSPEC,
    DOC_EXMPL_A_MSG,
    __DOC_EXMPL_A_MAX,
};
#define DOC_EXMPL_A_MAX (__DOC_EXMPL_A_MAX - 1)

// Define the commands
enum {
    DOC_EXMPL_C_UNSPEC,
    DOC_EXMPL_C_ECHO,
    __DOC_EXMPL_C_MAX,
};
#define DOC_EXMPL_C_MAX (__DOC_EXMPL_C_MAX - 1)

// Define the policy
static struct nla_policy doc_exmpl_genl_policy[DOC_EXMPL_A_MAX + 1] = {
    [DOC_EXMPL_A_MSG] = { .type = NLA_NUL_STRING, .len = 128-1 },
};

// Define the family
static struct genl_family doc_exmpl_gnl_family = {
    .hdrsize = 0,
    .name = "DOC_EXMPL",
    .version = 1,
    .maxattr = DOC_EXMPL_A_MAX,
};

// Function to send SQL request
void send_sql_req(char *sql) {
    struct sk_buff *skb;
    void *msg_head;
    int rc;

    // Allocate a new message
    skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (skb == NULL)
        return;

    // Add the message headers
    msg_head = genlmsg_put(skb, 0, 0, &doc_exmpl_gnl_family, 0, DOC_EXMPL_C_ECHO);
    if (msg_head == NULL) {
        rc = -ENOMEM;
        goto failure;
    }

    // Add the SQL request as an attribute
    rc = nla_put_string(skb, DOC_EXMPL_A_MSG, sql);
    if (rc != 0)
        goto failure;

    // Finalize the message
    genlmsg_end(skb, msg_head);

    // Send the message
    rc = genlmsg_unicast(genl_info_net(info), skb, info->snd_portid);
    if (rc != 0)
        goto failure;

    return;

failure:
    // Free the message on failure
    nlmsg_free(skb);
}

