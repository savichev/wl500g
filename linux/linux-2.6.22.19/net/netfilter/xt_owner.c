/*
 * Kernel module to match various things tied to sockets associated with
 * locally generated outgoing packets.
 *
 * (C) 2000 Marc Boucher <marc@mbsi.ca>
 *
 * Copyright Â© CC Computer Consultants GmbH, 2007 - 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <net/sock.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_owner.h>

#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter.h>

static bool
owner_mt(const struct sk_buff *skb, const struct net_device *in,
         const struct net_device *out, const struct xt_match *match,
         const void *matchinfo, int offset, unsigned int protoff,
         bool *hotdrop)
{
	const struct xt_owner_match_info *info = matchinfo;
	const struct file *filp;

	if (skb->sk == NULL || skb->sk->sk_socket == NULL)
		return (info->match ^ info->invert) == 0;
	else if (info->match & info->invert & XT_OWNER_SOCKET)
		/*
		 * Socket exists but user wanted ! --socket-exists.
		 * (Single ampersands intended.)
		 */
		return false;

	filp = skb->sk->sk_socket->file;
	if (filp == NULL)
		return ((info->match ^ info->invert) &
		       (XT_OWNER_UID | XT_OWNER_GID)) == 0;

	if (info->match & XT_OWNER_UID)
		if ((filp->f_uid >= info->uid_min &&
		    filp->f_uid <= info->uid_max) ^
		    !(info->invert & XT_OWNER_UID))
			return false;

	if (info->match & XT_OWNER_GID)
		if ((filp->f_gid >= info->gid_min &&
		    filp->f_gid <= info->gid_max) ^
		    !(info->invert & XT_OWNER_GID))
			return false;

	return true;
}

static struct xt_match owner_mt_reg[] __read_mostly = {
	{
		.name       = "owner",
		.revision   = 1,
		.family     = AF_INET,
		.match      = owner_mt,
		.matchsize  = sizeof(struct xt_owner_match_info),
		.hooks      = (1 << NF_IP_LOCAL_OUT) |
		              (1 << NF_IP_POST_ROUTING),
		.me         = THIS_MODULE,
	},
	{
		.name       = "owner",
		.revision   = 1,
		.family     = AF_INET6,
		.match      = owner_mt,
		.matchsize  = sizeof(struct xt_owner_match_info),
		.hooks      = (1 << NF_IP6_LOCAL_OUT) |
		              (1 << NF_IP6_POST_ROUTING),
		.me         = THIS_MODULE,
	},
};

static int __init owner_mt_init(void)
{
	return xt_register_matches(owner_mt_reg, ARRAY_SIZE(owner_mt_reg));
}

static void __exit owner_mt_exit(void)
{
	xt_unregister_matches(owner_mt_reg, ARRAY_SIZE(owner_mt_reg));
}

module_init(owner_mt_init);
module_exit(owner_mt_exit);
MODULE_AUTHOR("Jan Engelhardt <jengelh@computergmbh.de>");
MODULE_DESCRIPTION("netfilter \"owner\" match module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_owner");
MODULE_ALIAS("ip6t_owner");
