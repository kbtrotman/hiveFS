#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.arch = MODULE_ARCH_INIT,
};

#ifdef RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xf8cdd757, "module_layout" },
	{ 0xc4f0da12, "ktime_get_with_offset" },
	{ 0x754d539c, "strlen" },
	{ 0x306f7f60, "mount_bdev" },
	{ 0x66decfd5, "ns_to_timespec" },
	{ 0xe2d5255a, "strcmp" },
	{ 0xb44ad4b3, "_copy_to_user" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x8fc9a39, "__bread_gfp" },
	{ 0x27e1a049, "printk" },
	{ 0xfdc1bd61, "nla_put" },
	{ 0xfb2557c2, "kmem_cache_free" },
	{ 0x22afdba8, "netlink_unicast" },
	{ 0x5b3c48fd, "sync_dirty_buffer" },
	{ 0x1e7097e1, "__brelse" },
	{ 0xf159fe55, "kmem_cache_alloc" },
	{ 0x3c5dfeb0, "__alloc_skb" },
	{ 0xa916b694, "strnlen" },
	{ 0xdb7305a1, "__stack_chk_fail" },
	{ 0x67b4bbab, "kfree_skb" },
	{ 0x1036efcc, "kill_block_super" },
	{ 0x2ea2c95c, "__x86_indirect_thunk_rax" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0x77a5b2a6, "genlmsg_put" },
	{ 0xbafec02e, "__bforget" },
	{ 0xb7c36a6c, "d_make_root" },
	{ 0x7c46cf7f, "mark_buffer_dirty" },
	{ 0x84698f00, "new_inode" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0xe914e41e, "strcpy" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "BFC8798FE240BB27E243B22");
MODULE_INFO(rhelversion, "8.8");
