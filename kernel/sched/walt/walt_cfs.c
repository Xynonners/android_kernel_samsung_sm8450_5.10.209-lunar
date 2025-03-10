// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "walt_cfs: " fmt

#include <trace/hooks/sched.h>
#include <trace/hooks/binder.h>

#include <linux/errno.h>
#include <linux/energy_model.h>
#include <linux/sched/walt_kallsyms.h>
#include "walt_kallsyms.h"
#include "ftrace_hook.h"

static inline unsigned long
cpu_util_next_walt(int cpu, struct task_struct *p, int dst_cpu)
{
	struct walt_rq *wrq = (struct walt_rq *) cpu_rq(cpu)->android_vendor_data1;
	unsigned long util = wrq->walt_stats.cumulative_runnable_avg_scaled;
	bool queued = task_on_rq_queued(p);

	/*
	 * When task is queued,
	 * (a) The evaluating CPU (cpu) is task's current CPU. If the
	 * task is migrating, discount the task contribution from the
	 * evaluation cpu.
	 * (b) The evaluating CPU (cpu) is task's current CPU. If the
	 * task is NOT migrating, nothing to do. The contribution is
	 * already present on the evaluation CPU.
	 * (c) The evaluating CPU (cpu) is not task's current CPU. But
	 * the task is migrating to the evaluating CPU. So add the
	 * task contribution to it.
	 * (d) The evaluating CPU (cpu) is neither the current CPU nor
	 * the destination CPU. don't care.
	 *
	 * When task is NOT queued i.e waking. Task contribution is not
	 * present on any CPU.
	 *
	 * (a) If the evaluating CPU is the destination CPU, add the task
	 * contribution.
	 * (b) The evaluation CPU is not the destination CPU, don't care.
	 */
	if (unlikely(queued)) {
		if (task_cpu(p) == cpu) {
			if (dst_cpu != cpu)
				util = max_t(long, util - task_util(p), 0);
		} else if (dst_cpu == cpu) {
			util += task_util(p);
		}
	} else if (dst_cpu == cpu) {
		util += task_util(p);
	}

	return min_t(unsigned long, util, capacity_orig_of(cpu));
}

static inline u64
cpu_util_next_walt_prs(int cpu, struct task_struct *p, int dst_cpu, bool prev_dst_same_cluster,
											u64 *prs)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
	long util = prs[cpu];

	if (wts->prev_window) {
		if (!prev_dst_same_cluster) {
			/* intercluster migration of non rtg task - mimic fixups */
			util -= wts->prev_window_cpu[cpu];
			if (util < 0)
				util = 0;
			if (cpu == dst_cpu)
				util += wts->prev_window;
		}
	} else {
		if (cpu == dst_cpu)
			util += wts->demand;
	}

	return util;
}

/**
 * walt_em_cpu_energy() - Estimates the energy consumed by the CPUs of a
		performance domain
 * @pd		: performance domain for which energy has to be estimated
 * @max_util	: highest utilization among CPUs of the domain
 * @sum_util	: sum of the utilization of all CPUs in the domain
 *
 * This function must be used only for CPU devices. There is no validation,
 * i.e. if the EM is a CPU type and has cpumask allocated. It is called from
 * the scheduler code quite frequently and that is why there is not checks.
 *
 * Return: the sum of the energy consumed by the CPUs of the domain assuming
 * a capacity state satisfying the max utilization of the domain.
 */
static inline unsigned long walt_em_cpu_energy(struct em_perf_domain *pd,
				unsigned long max_util, unsigned long sum_util,
				struct compute_energy_output *output, unsigned int x)
{
	unsigned long scale_cpu;
	int cpu;
	struct walt_rq *wrq;

#if defined(CONFIG_OPLUS_UAG_USE_TL)
	struct cpufreq_policy policy;
	unsigned long raw_util = max_util;
#endif

	if (!sum_util)
		return 0;

	/*
	 * In order to predict the capacity state, map the utilization of the
	 * most utilized CPU of the performance domain to a requested frequency,
	 * like schedutil.
	 */
	cpu = cpumask_first(to_cpumask(pd->cpus));
	scale_cpu = arch_scale_cpu_capacity(cpu);

#if defined(CONFIG_OPLUS_UAG_USE_TL)
	if (!cpufreq_get_policy(&policy, cpu))
		trace_android_vh_map_util_freq(max_util, max_util, max_util, &max_util, &policy, NULL);
	if (max_util == raw_util)
		max_util = max_util + (max_util >> 2); /* account  for TARGET_LOAD usually 80 */
#else /* !CONFIG_OPLUS_UAG_USE_TL */
	max_util = max_util + (max_util >> 2); /* account  for TARGET_LOAD usually 80 */
#endif
	max_util = max(max_util,
			(arch_scale_freq_capacity(cpu) * scale_cpu) >>
			SCHED_CAPACITY_SHIFT);

	/*
	 * The capacity of a CPU in the domain at the performance state (ps)
	 * can be computed as:
	 *
	 *             ps->freq * scale_cpu
	 *   ps->cap = --------------------                          (1)
	 *                 cpu_max_freq
	 *
	 * So, ignoring the costs of idle states (which are not available in
	 * the EM), the energy consumed by this CPU at that performance state
	 * is estimated as:
	 *
	 *             ps->power * cpu_util
	 *   cpu_nrg = --------------------                          (2)
	 *                   ps->cap
	 *
	 * since 'cpu_util / ps->cap' represents its percentage of busy time.
	 *
	 *   NOTE: Although the result of this computation actually is in
	 *         units of power, it can be manipulated as an energy value
	 *         over a scheduling period, since it is assumed to be
	 *         constant during that interval.
	 *
	 * By injecting (1) in (2), 'cpu_nrg' can be re-expressed as a product
	 * of two terms:
	 *
	 *             ps->power * cpu_max_freq   cpu_util
	 *   cpu_nrg = ------------------------ * ---------          (3)
	 *                    ps->freq            scale_cpu
	 *
	 * The first term is static, and is stored in the em_perf_state struct
	 * as 'ps->cost'.
	 *
	 * Since all CPUs of the domain have the same micro-architecture, they
	 * share the same 'ps->cost', and the same CPU capacity. Hence, the
	 * total energy of the domain (which is the simple sum of the energy of
	 * all of its CPUs) can be factorized as:
	 *
	 *            ps->cost * \Sum cpu_util
	 *   pd_nrg = ------------------------                       (4)
	 *                  scale_cpu
	 */
	if (max_util >= 1024)
		max_util = 1023;

	wrq = (struct walt_rq *) cpu_rq(cpu)->android_vendor_data1;

	if (output) {
		output->cost[x] = wrq->cluster->util_to_cost[max_util];
		output->max_util[x] = max_util;
		output->sum_util[x] = sum_util;
	}
	return wrq->cluster->util_to_cost[max_util] * sum_util / scale_cpu;
}

/*
 * __walt_pd_compute_energy(): Estimates the energy that @pd would consume if @p was
 * migrated to @dst_cpu. compute_energy() predicts what will be the utilization
 * landscape of @pd's CPUs after the task migration, and uses the Energy Model
 * to compute what would be the energy if we decided to actually migrate that
 * task.
 */
static long
walt_pd_compute_energy(struct task_struct *p, int dst_cpu, struct perf_domain *pd, u64 *prs,
		struct compute_energy_output *output, unsigned int x)
{
	struct cpumask *pd_mask = perf_domain_span(pd);
	unsigned long max_util = 0, sum_util = 0;
	int cpu;
	unsigned long cpu_util;
	bool prev_dst_same_cluster = false;

	if (same_cluster(task_cpu(p), dst_cpu))
		prev_dst_same_cluster = true;

	/*
	 * The capacity state of CPUs of the current rd can be driven by CPUs
	 * of another rd if they belong to the same pd. So, account for the
	 * utilization of these CPUs too by masking pd with cpu_online_mask
	 * instead of the rd span.
	 *
	 * If an entire pd is outside of the current rd, it will not appear in
	 * its pd list and will not be accounted by compute_energy().
	 */
	for_each_cpu_and(cpu, pd_mask, cpu_online_mask) {
		sum_util += cpu_util_next_walt(cpu, p, dst_cpu);
		cpu_util = cpu_util_next_walt_prs(cpu, p, dst_cpu, prev_dst_same_cluster, prs);
		max_util = max(max_util, cpu_util);
	}

	max_util = scale_demand(max_util);

	if (output)
		output->cluster_first_cpu[x] = cpumask_first(pd_mask);

	return walt_em_cpu_energy(pd->em_pd, max_util, sum_util, output, x);
}

static long
__walt_compute_energy(struct task_struct *p, int dst_cpu, struct perf_domain *pd,
			cpumask_t *candidates, u64 *prs, struct compute_energy_output *output)
{
	long energy = 0;
	unsigned int x = 0;

	for (; pd; pd = pd->next) {
		struct cpumask *pd_mask = perf_domain_span(pd);

		if (cpumask_intersects(candidates, pd_mask)
				|| cpumask_test_cpu(task_cpu(p), pd_mask)) {
			energy += walt_pd_compute_energy(p, dst_cpu, pd, prs, output, x);
			x++;
		}
	}

	return energy;
}

RSYM_RESOLVER(resolve_walt_kallsyms_some,
	RSYM(walt_scale_demand_divisor),
);

static long
(*walt_compute_energy_orig)(struct task_struct *p, int dst_cpu, struct perf_domain *pd,
			cpumask_t *candidates, u64 *prs, struct compute_energy_output *output);

static struct ftrace_hook hook = HOOK("walt_compute_energy", __walt_compute_energy, &walt_compute_energy_orig);

int hook_walt_compute_energy(void)
{
	int ret;

    ret = resolve_walt_kallsyms_some_once();
    if (ret) {
        pr_err("Failed to resolve WALT symbols, aborting hook_walt_compute_energy.\n");
        return ret;
    }

    ret = fh_install_hook(&hook);
    if (ret) {
        pr_err("Failed to hook walt_compute_energy, error: %d\n", ret);
        return -ENOENT;
    }
    pr_info("walt_compute_energy successfully hooked\n");
    return 0;
}

int hook_walt_compute_energy_once(void)
{
	static bool hooked;
	int ret;
	if (hooked)
		return 0;
	ret = hook_walt_compute_energy();
	if (!ret)
		hooked = true;
	return ret;
}

void unhook_walt_compute_energy(void)
{
	fh_remove_hook(&hook);
    return;
}
