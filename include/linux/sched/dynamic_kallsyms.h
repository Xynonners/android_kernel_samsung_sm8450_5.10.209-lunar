/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * dynamic_kallsyms.h - Generic macros for dynamic symbol resolution via kallsyms.
 *
 * After calling your resolver function
 * in your module init, the symbols declared in that header can be used as if
 * they were normally declared.
 */

#ifndef DYNAMIC_KALLSYMS_H
#define DYNAMIC_KALLSYMS_H

#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/version.h>
#include <linux/kprobes.h>
#include <linux/kallsyms.h>
#include <linux/module.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0) && defined(MODULE)
static unsigned long lookup_name(const char *name)
{
	struct kprobe kp = {
		.symbol_name = name
	};
	unsigned long retval;

	if (register_kprobe(&kp) < 0) return 0;
	retval = (unsigned long) kp.addr;
	unregister_kprobe(&kp);
	return retval;
}
#else
static unsigned long lookup_name(const char *name)
{
	return kallsyms_lookup_name(name);
}
#endif

/* --- Macros for variables --- */
#define KSYM_VAR(type, sym)                                  \
    static type *__##sym;                                    \
    static inline int __resolve_##sym(void) {                \
        __##sym = (type *)lookup_name(#sym);        \
        if (!__##sym) {                                      \
            pr_err("Failed to resolve symbol: %s\n", #sym);  \
            return -ENOENT;                                  \
        }                                                    \
        return 0;                                            \
    }

#define KSYM_VAR_RM(type, sym)                                 \
    static type *__##sym __read_mostly;                           \
    static inline int __resolve_##sym(void) {                    \
        __##sym = (type *)lookup_name(#sym);             \
        if (!__##sym) {                                                 \
            pr_err("Failed to resolve symbol: %s\n", #sym);             \
            return -ENOENT;                                             \
        }                                                               \
        return 0;                                                       \
    }

#define KSYM_ARRAY_2D(type, sym, dim2)                              \
    static type (*__##sym)[dim2];                                   \
    static inline int __resolve_##sym(void) {                       \
        __##sym = (type (*)[dim2])lookup_name(#sym);       \
        if (!__##sym) {                                                 \
            pr_err("Failed to resolve symbol: %s\n", #sym);             \
            return -ENOENT;                                             \
        }                                                               \
        return 0;                                                       \
    }

#define KSYM_FUNC_VOID(sym, proto, call_args)           \
    static void (*__##sym) proto;                       \
    static inline void sym proto {                      \
        __##sym call_args;                              \
        return;                                         \
    }                                                   \
    static inline int __resolve_##sym(void) {           \
        __##sym = (void (*) proto)lookup_name(#sym);  \
        if (!__##sym) {                                 \
            pr_err("Failed to resolve symbol: %s\n", #sym);   \
            return -ENOENT;                             \
        }                                               \
        return 0;                                       \
    }

#define KSYM_FUNC(ret, sym, proto, call_args)                     \
    static ret (*__##sym) proto;                                   \
    static inline ret sym proto {                                  \
        return __##sym call_args;                                  \
    }                                                              \
    static inline int __resolve_##sym(void) {                      \
        __##sym = (ret (*) proto)lookup_name(#sym);       \
        if (!__##sym) {                                            \
            pr_err("Failed to resolve symbol: %s\n", #sym);        \
            return -ENOENT;                                        \
        }                                                          \
        return 0;                                                  \
    }

/* --- Resolver macro --- */
/*
 * RSYM_RESOLVER(local_func, sym1, sym2, …, symN)
 * Creates a static inline function 'local_func' that calls each resolver
 * (e.g. __resolve_sym1(), __resolve_sym2(), …) in order.
 */
struct resolver_entry {
    int (*resolver)(void);
    const char *name;
};

#define RSYM(sym) { __resolve_##sym, #sym }

#define RSYM_RESOLVER(func, ...)                                 \
    static inline int func(void) {                                 \
        int ret, i;                                              \
        struct resolver_entry _resolvers[] = { __VA_ARGS__ };      \
        for (i = 0; i < ARRAY_SIZE(_resolvers); i++) {           \
            ret = _resolvers[i].resolver();                      \
            pr_info("Resolved symbol %s: returned %d\n",          \
                    _resolvers[i].name, ret);                     \
            if (ret)                                             \
                return ret;                                      \
        }                                                        \
        return 0;                                                \
    }                                                            \
    static inline int func##_once(void) {                        \
        static bool resolved;                                    \
        int ret;                                                 \
        if (resolved)                                           \
            return 0;                                           \
        ret = func();                                           \
        if (!ret)                                               \
            resolved = true;                                    \
        return ret;                                             \
    }

#define MODULE_DEP(modname)                                                   \
    static struct module *__##modname_module;                         \
    static inline int ensure_##modname(void) {                                \
        __##modname_module = find_module(#modname);                    \
        if (!__##modname_module) {                                     \
            pr_err(#modname " module not found. Required for functionality.\n"); \
            return -ENOENT;                                                   \
        }                                                                     \
        if (!try_module_get(__##modname_module)) {                     \
            pr_err("Failed to acquire " #modname " module reference.\n");      \
            return -ENOENT;                                                   \
        }                                                                     \
        pr_info("Found " #modname " module dependency.\n");                   \
        return 0;                                                             \
    }                                                                         \
    static inline void release_##modname(void) {                              \
        if (__##modname_module) {                                      \
            module_put(__##modname_module);                            \
        }                                                                        \
    }                                                                            \
    static inline int ensure_##modname##_once(void) {                            \
        static bool loaded;                                \
        int ret;                                             \
        if (loaded)                                    \
            return 0;                                                            \
        ret = ensure_##modname();                                            \
        if (!ret)                                                                \
            loaded = true;                             \
        return ret;                                                              \
    }


#endif /* DYNAMIC_KALLSYMS_H */
