/*
 * Hooking kernel functions using ftrace framework
 *
 * Copyright (c) 2018 ilammy
 */

#define DEBUG

#define pr_fmt(fmt) "ftrace_hook: " fmt

#include "ftrace_hook.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,11,0)
#define FTRACE_OPS_FL_RECURSION FTRACE_OPS_FL_RECURSION_SAFE
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,11,0)
#define ftrace_regs pt_regs

static __always_inline struct pt_regs *ftrace_get_regs(struct ftrace_regs *fregs)
{
	return fregs;
}
#endif

/*
 * There are two ways of preventing vicious recursive loops when hooking:
 * - detect recusion using function return address (USE_FENTRY_OFFSET = 0)
 * - avoid recusion by jumping over the ftrace call (USE_FENTRY_OFFSET = 1)
 */
#define USE_FENTRY_OFFSET 0

static int fh_resolve_hook_address(struct ftrace_hook *hook)
{
	hook->address = lookup_name(hook->name);

	if (!hook->address) {
		pr_debug("unresolved symbol: %s\n", hook->name);
		return -ENOENT;
	}

#if USE_FENTRY_OFFSET
	*((unsigned long*) hook->original) = hook->address + MCOUNT_INSN_SIZE;
#else
	*((unsigned long*) hook->original) = hook->address;
#endif

	return 0;
}

static void notrace fh_ftrace_thunk(unsigned long ip, unsigned long parent_ip,
		struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);
	struct ftrace_hook *hook = container_of(ops, struct ftrace_hook, ops);

#if USE_FENTRY_OFFSET
#ifdef CONFIG_ARM64
	regs->pc = (unsigned long)hook->function;
#else
	regs->ip = (unsigned long)hook->function;
#endif
#else
	if (!within_module(parent_ip, THIS_MODULE))
#ifdef CONFIG_ARM64
		regs->pc = (unsigned long)hook->function;
#else
		regs->ip = (unsigned long)hook->function;
#endif
#endif
}

/**
 * fh_install_hooks() - register and enable a single hook
 * @hook: a hook to install
 *
 * Returns: zero on success, negative error code otherwise.
 */
int fh_install_hook(struct ftrace_hook *hook)
{
	int err;

	err = fh_resolve_hook_address(hook);
	if (err)
		return err;


  pr_debug("address to hook: %s=0x%lx", hook->name, hook->address);

	/*
	 * We're going to modify %rip register so we'll need IPMODIFY flag
	 * and SAVE_REGS as its prerequisite. ftrace's anti-recursion guard
	 * is useless if we change %rip so disable it with RECURSION.
	 * We'll perform our own checks for trace function reentry.
	 */
	hook->ops.func = fh_ftrace_thunk;

#ifdef CONFIG_ARM64
	hook->ops.flags =  FTRACE_OPS_FL_SAVE_REGS_IF_SUPPORTED
					| FTRACE_OPS_FL_RECURSION_SAFE
					| FTRACE_OPS_FL_IPMODIFY;
#else
	hook->ops.flags = FTRACE_OPS_FL_SAVE_REGS
	                | FTRACE_OPS_FL_RECURSION
	                | FTRACE_OPS_FL_IPMODIFY;
#endif

    err = ftrace_set_filter(&hook->ops, (unsigned char *)hook->name, strlen(hook->name), 0);
	if (err) {
		pr_debug("ftrace_set_filter() failed: %d\n", err);
		return err;
	}

	err = register_ftrace_function(&hook->ops);
	if (err) {
		pr_debug("register_ftrace_function() failed: %d\n", err);
		ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
		return err;
	}

	return 0;
}

/**
 * fh_remove_hooks() - disable and unregister a single hook
 * @hook: a hook to remove
 */
void fh_remove_hook(struct ftrace_hook *hook)
{
	int err;

	err = unregister_ftrace_function(&hook->ops);
	if (err) {
		pr_debug("unregister_ftrace_function() failed: %d\n", err);
	}

	err = ftrace_set_filter(&hook->ops,  NULL, 0, 1);
	if (err) {
		pr_debug("ftrace_set_filter() failed: %d\n", err);
	}
}

/**
 * fh_install_hooks() - register and enable multiple hooks
 * @hooks: array of hooks to install
 * @count: number of hooks to install
 *
 * If some hooks fail to install then all hooks will be removed.
 *
 * Returns: zero on success, negative error code otherwise.
 */
int fh_install_hooks(struct ftrace_hook *hooks, size_t count)
{
	int err;
	size_t i;

	for (i = 0; i < count; i++) {
		err = fh_install_hook(&hooks[i]);
		if (err)
			goto error;
	}

	return 0;

error:
	while (i != 0) {
		fh_remove_hook(&hooks[--i]);
	}

	return err;
}

/**
 * fh_remove_hooks() - disable and unregister multiple hooks
 * @hooks: array of hooks to remove
 * @count: number of hooks to remove
 */
void fh_remove_hooks(struct ftrace_hook *hooks, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		fh_remove_hook(&hooks[i]);
}

// /*
//  * Tail call optimization can interfere with recursion detection based on
//  * return address on the stack. Disable it to avoid machine hangups.
//  */
// #if !USE_FENTRY_OFFSET
// #pragma GCC optimize("-fno-optimize-sibling-calls")
// #endif
//
// #ifdef PTREGS_SYSCALL_STUBS
// static asmlinkage long (*real_sys_clone)(struct pt_regs *regs);
//
// static asmlinkage long fh_sys_clone(struct pt_regs *regs)
// {
// 	long ret;
//
// 	pr_info("clone() before\n");
//
// 	ret = real_sys_clone(regs);
//
// 	pr_info("clone() after: %ld\n", ret);
//
// 	return ret;
// }
// #else
// static asmlinkage long (*real_sys_clone)(unsigned long clone_flags,
// 		unsigned long newsp, int __user *parent_tidptr,
// 		int __user *child_tidptr, unsigned long tls);
//
// static asmlinkage long fh_sys_clone(unsigned long clone_flags,
// 		unsigned long newsp, int __user *parent_tidptr,
// 		int __user *child_tidptr, unsigned long tls)
// {
// 	long ret;
//
// 	pr_info("clone() before\n");
//
// 	ret = real_sys_clone(clone_flags, newsp, parent_tidptr,
// 		child_tidptr, tls);
//
// 	pr_info("clone() after: %ld\n", ret);
//
// 	return ret;
// }
// #endif
//
// static char *duplicate_filename(const char __user *filename)
// {
// 	char *kernel_filename;
//
// 	kernel_filename = kmalloc(4096, GFP_KERNEL);
// 	if (!kernel_filename)
// 		return NULL;
//
// 	if (strncpy_from_user(kernel_filename, filename, 4096) < 0) {
// 		kfree(kernel_filename);
// 		return NULL;
// 	}
//
// 	return kernel_filename;
// }
//
// #ifdef PTREGS_SYSCALL_STUBS
// static asmlinkage long (*real_sys_execve)(struct pt_regs *regs);
//
// static asmlinkage long fh_sys_execve(struct pt_regs *regs)
// {
// 	long ret;
// 	char *kernel_filename;
//
// 	kernel_filename = duplicate_filename((void*) regs->di);
//
// 	pr_info("execve() before: %s\n", kernel_filename);
//
// 	kfree(kernel_filename);
//
// 	ret = real_sys_execve(regs);
//
// 	pr_info("execve() after: %ld\n", ret);
//
// 	return ret;
// }
// #else
// static asmlinkage long (*real_sys_execve)(const char __user *filename,
// 		const char __user *const __user *argv,
// 		const char __user *const __user *envp);
//
// static asmlinkage long fh_sys_execve(const char __user *filename,
// 		const char __user *const __user *argv,
// 		const char __user *const __user *envp)
// {
// 	long ret;
// 	char *kernel_filename;
//
// 	kernel_filename = duplicate_filename(filename);
//
// 	pr_info("execve() before: %s\n", kernel_filename);
//
// 	kfree(kernel_filename);
//
// 	ret = real_sys_execve(filename, argv, envp);
//
// 	pr_info("execve() after: %ld\n", ret);
//
// 	return ret;
// }
// #endif
//
// /*
//  * x86_64 kernels have a special naming convention for syscall entry points in newer kernels.
//  * That's what you end up with if an architecture has 3 (three) ABIs for system calls.
//  */
// #ifdef PTREGS_SYSCALL_STUBS
// #ifdef CONFIG_ARM64
// #define SYSCALL_NAME(name) ("__arm64_" name)
// #else
// #define SYSCALL_NAME(name) ("__x64_" name)
// #endif
// #else
// #ifdef CONFIG_ARM64
// #define SYSCALL_NAME(name) ("__arm64_" name)
// #else
// #define SYSCALL_NAME(name) (name)
// #endif
// #endif
//
// #define HOOK(_name, _function, _original)	\
// 	{					\
// 		.name = SYSCALL_NAME(_name),	\
// 		.function = (_function),	\
// 		.original = (_original),	\
// 	}
//
// static struct ftrace_hook demo_hooks[] = {
// 	HOOK("sys_clone",  fh_sys_clone,  &real_sys_clone),
// 	HOOK("sys_execve", fh_sys_execve, &real_sys_execve),
// };
//
// static int fh_init(void)
// {
// 	int err;
//
// 	err = fh_install_hooks(demo_hooks, ARRAY_SIZE(demo_hooks));
// 	if (err)
// 		return err;
//
// 	pr_info("module loaded\n");
//
// 	return 0;
// }
// module_init(fh_init);
//
// static void fh_exit(void)
// {
// 	fh_remove_hooks(demo_hooks, ARRAY_SIZE(demo_hooks));
//
// 	pr_info("module unloaded\n");
// }
// module_exit(fh_exit);
