/*
 * Hooking kernel functions using ftrace framework
 *
 * Copyright (c) 2018 ilammy
 */

#ifndef FTRACE_HOOK_H
#define FTRACE_HOOK_H

#include <linux/ftrace.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/kprobes.h>
#include <linux/sched/dynamic_kallsyms.h>

#if !defined(CONFIG_X86_64) && !defined(CONFIG_ARM64)
#error Currently only x86_64 and arm64 architecture are supported
#endif

/**
 * struct ftrace_hook - describes a single hook to install
 *
 * @name:     name of the function to hook
 *
 * @function: pointer to the function to execute instead
 *
 * @original: pointer to the location where to save a pointer
 *            to the original function
 *
 * @address:  kernel address of the function entry
 *
 * @ops:      ftrace_ops state for this function hook
 *
 * The user should fill in only &name, &hook, &orig fields.
 * Other fields are considered implementation details.
 */
struct ftrace_hook {
	const char *name;
	void *function;
	void *original;

	unsigned long address;
	struct ftrace_ops ops;
};

extern int fh_install_hook(struct ftrace_hook *hook);
extern void fh_remove_hook(struct ftrace_hook *hook);
extern int fh_install_hooks(struct ftrace_hook *hooks, size_t count);
extern void fh_remove_hooks(struct ftrace_hook *hooks, size_t count);

/* Architecture-specific adjustments */
#if defined(CONFIG_X86_64) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0))
#define PTREGS_SYSCALL_STUBS 1
#endif

#ifdef PTREGS_SYSCALL_STUBS
#ifdef CONFIG_ARM64
#define SYSCALL_NAME(name) ("__arm64_" name)
#else
#define SYSCALL_NAME(name) ("__x64_" name)
#endif
#else
#ifdef CONFIG_ARM64
#define SYSCALL_NAME(name) ("__arm64_" name)
#else
#define SYSCALL_NAME(name) (name)
#endif
#endif

#define HOOK_SYSCALL(_name, _function, _original)  \
    {                                      \
        .name = SYSCALL_NAME(_name),       \
        .function = (_function),           \
        .original = (_original),           \
    }

#define HOOK(_name, _function, _original)  \
    {                                      \
        .name = (_name),       \
        .function = (_function),           \
        .original = (_original),           \
    }

#endif /* FTRACE_HOOK_H */
