// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/ioctl.h>
#include <linux/compat.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "../../../../../fs/proc/internal.h"

#include "../../../sched_assist/sa_common.h"
#include "../../../frame_boost/frame_boost.h"
#include "../../../frame_boost/frame_info.h"

#include "../frame_ioctl.h"
#include "../ua_ioctl_common.h"

#define FRAMEBOOST_PROC_NODE "oplus_frame_boost"
#define INVALID_VAL (INT_MIN)

static struct proc_dir_entry *frame_boost_proc;

static struct ofb_stune_data sys_stune_data = {
	.level = -1,
	.boost_freq = -1,
	.boost_migr = -1,
	.vutil_margin = 0xFF,
	.util_frame_rate = -1,
	.util_min_threshold = -1,
	.util_min_obtain_view = -1,
	.util_min_timeout = -1,
	.ed_task_boost_mid_duration = -1,
	.ed_task_boost_mid_util = -1,
	.ed_task_boost_max_duration = -1,
	.ed_task_boost_max_util = -1,
	.ed_task_boost_timeout_duration = -1,
	.boost_sf_freq_nongpu = -1,
	.boost_sf_migr_nongpu = -1,
	.boost_sf_freq_gpu = -1,
	.boost_sf_migr_gpu = -1,
};

static struct ofb_stune_data sf_stune_data = {
	.level = -1,
	.boost_freq = -1,
	.boost_migr = -1,
	.vutil_margin = 0xFF,
	.util_frame_rate = -1,
	.util_min_threshold = -1,
	.util_min_obtain_view = -1,
	.util_min_timeout = -1,
	.ed_task_boost_mid_duration = -1,
	.ed_task_boost_mid_util = -1,
	.ed_task_boost_max_duration = -1,
	.ed_task_boost_max_util = -1,
	.ed_task_boost_timeout_duration = -1,
	.boost_sf_freq_nongpu = -1,
	.boost_sf_migr_nongpu = -1,
	.boost_sf_freq_gpu = -1,
	.boost_sf_migr_gpu = -1,
};

static struct ofb_stune_data app_stune_data = {
	.level = -1,
	.boost_freq = -1,
	.boost_migr = -1,
	.vutil_margin = 0xFF,
	.util_frame_rate = -1,
	.util_min_threshold = -1,
	.util_min_obtain_view = -1,
	.util_min_timeout = -1,
	.ed_task_boost_mid_duration = -1,
	.ed_task_boost_mid_util = -1,
	.ed_task_boost_max_duration = -1,
	.ed_task_boost_max_util = -1,
	.ed_task_boost_timeout_duration = -1,
	.boost_sf_freq_nongpu = -1,
	.boost_sf_migr_nongpu = -1,
	.boost_sf_freq_gpu = -1,
	.boost_sf_migr_gpu = -1,
};

#ifdef CONFIG_ARCH_MEDIATEK
u64 curr_frame_start;
EXPORT_SYMBOL(curr_frame_start);
u64 prev_frame_start;
EXPORT_SYMBOL(prev_frame_start);
#endif /* CONFIG_ARCH_MEDIATEK */

static void crtl_update_refresh_rate(int pid, unsigned int vsyncNs)
{
	unsigned int frame_rate =  NSEC_PER_SEC / (unsigned int)(vsyncNs);
	bool is_sf = false;
	unsigned long im_flag = IM_FLAG_NONE;

	im_flag = oplus_get_im_flag(current);
	is_sf = (test_bit(IM_FLAG_SURFACEFLINGER, &im_flag));
	if (is_sf) {
		set_max_frame_rate(frame_rate);
		set_frame_group_window_size(vsyncNs);
		return;
	}

	if ((pid != current->pid) || (pid != get_frame_group_ui(DEFAULT_FRAME_GROUP_ID)))
		return;

	if (set_frame_rate(frame_rate))
		set_frame_group_window_size(vsyncNs);
}

/*********************************
 * frame boost ioctl proc
 *********************************/
static long ofb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct ofb_ctrl_data data;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != OFB_MAGIC)
		return -EINVAL;

	if (_IOC_NR(cmd) >= CMD_ID_MAX)
		return -EINVAL;

	if (copy_from_user(&data, uarg, sizeof(data))) {
		ofb_err("invalid address");
		return -EFAULT;
	}

	switch (cmd) {
	case CMD_ID_SET_FPS:
		if (data.vsyncNs <= 0)
			return -EINVAL;
		crtl_update_refresh_rate(data.pid, (unsigned int)data.vsyncNs);
		break;
	case CMD_ID_BOOST_HIT:
		/* App which is not our frame boost target may request frame vsync(like systemui),
		 * just ignore hint from them! Return zero to avoid too many androd error log
		 */
		if ((data.pid != current->tgid) ||
				((data.pid != get_frame_group_ui(DEFAULT_FRAME_GROUP_ID)) &&
				 (data.pid != get_frame_group_ui(INPUTMETHOD_FRAME_GROUP_ID))))
			return ret;

		if (data.stage == BOOST_FRAME_START) {
#ifdef CONFIG_ARCH_MEDIATEK
			u64 frame_start_time = ktime_get_ns();

			if (curr_frame_start != frame_start_time)
					prev_frame_start = curr_frame_start;
			curr_frame_start = frame_start_time;
#endif /* CONFIG_ARCH_MEDIATEK */

			set_frame_state(FRAME_START);
			rollover_frame_group_window(DEFAULT_FRAME_GROUP_ID);
			default_group_update_cpufreq();
		}

		if (data.stage == BOOST_OBTAIN_VIEW) {
			if (get_frame_rate() >= fbg_get_stune_boost(BOOST_UTIL_FRAME_RATE)) {
				set_frame_util_min(fbg_get_stune_boost(BOOST_UTIL_MIN_OBTAIN_VIEW), true);
				default_group_update_cpufreq();
			}
		}

		if (data.stage == BOOST_SET_RENDER_THREAD)
			set_render_thread(DEFAULT_FRAME_GROUP_ID, data.pid, data.tid);

		if (data.stage == BOOST_FRAME_TIMEOUT) {
			if (get_frame_rate() >= fbg_get_stune_boost(BOOST_UTIL_FRAME_RATE) && check_putil_over_thresh(fbg_get_stune_boost(BOOST_UTIL_MIN_THRESHOLD))) {
				set_frame_util_min(fbg_get_stune_boost(BOOST_UTIL_MIN_TIMEOUT), true);
				default_group_update_cpufreq();
			}
		}

		if (data.stage == BOOST_INPUT_START)
			inputmethod_set_boost_start();

		break;
	case CMD_ID_END_FRAME:
		/* ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid); */
		break;
	case CMD_ID_SF_FRAME_MISSED:
		/* ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid); */
		break;
	case CMD_ID_SF_COMPOSE_HINT:
		/* ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid); */
		break;
	case CMD_ID_IS_HWUI_RT:
		/* ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid); */
		break;
	case CMD_ID_SET_TASK_TAGGING:
		/* ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid); */
		break;
	default:
		/* ret = -EINVAL; */
		break;
	}

	return ret;
}

static int fbg_set_task_preferred_cluster(void __user *uarg)
{
	struct ofb_ctrl_cluster data;

	if (uarg == NULL)
		return -EINVAL;

	if (copy_from_user(&data, uarg, sizeof(data)))
		return -EFAULT;

	return __fbg_set_task_preferred_cluster(data.tid, data.cluster_id);
}

static long fbg_add_task_to_group(void __user *uarg)
{
	struct ofb_key_thread_info info;
	unsigned int thread_num;
	unsigned int i;

	if (uarg == NULL)
		return -EINVAL;

	if (copy_from_user(&info, uarg, sizeof(struct ofb_key_thread_info))) {
		ofb_debug("%s: copy_from_user fail\n", __func__);
		return -EFAULT;
	}

	/* sanity check a loop boundary */
	thread_num = info.thread_num;
	if (thread_num > MAX_KEY_THREAD_NUM)
		thread_num = MAX_KEY_THREAD_NUM;

	for (i = 0; i < thread_num; i++)
		add_task_to_game_frame_group(info.tid[i], info.add);

	return 0;
}

static void get_frame_util_info(struct ofb_frame_util_info *info)
{
	memset(info, 0, sizeof(struct ofb_frame_util_info));
	fbg_get_frame_scale(&info->frame_scale);
	fbg_get_frame_busy(&info->frame_busy);
}

static long fbg_notify_frame_start(void __user *uarg)
{
	struct ofb_frame_util_info info;

	if (uarg == NULL)
		return -EINVAL;

	set_frame_state(FRAME_START);
	rollover_frame_group_window(GAME_FRAME_GROUP_ID);

	get_frame_util_info(&info);
	if (copy_to_user(uarg, &info, sizeof(struct ofb_frame_util_info))) {
		ofb_debug("%s: copy_to_user fail\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static bool is_ofb_extra_cmd(unsigned int cmd)
{
	return _IOC_TYPE(cmd) == OFB_EXTRA_MAGIC;
}

static long handle_ofb_extra_cmd(unsigned int cmd, void __user *uarg)
{
	switch (cmd) {
	case CMD_ID_SET_TASK_PREFERED_CLUSTER:
		return fbg_set_task_preferred_cluster(uarg);
	case CMD_ID_ADD_TASK_TO_GROUP:
		return fbg_add_task_to_group(uarg);
	case CMD_ID_NOTIFY_FRAME_START:
		return fbg_notify_frame_start(uarg);
	default:
		return -ENOTTY;
	}

	return 0;
}

static void merge_stune_data(struct ofb_stune_data *stune_data_src, struct ofb_stune_data *stune_data_dst) {
	if ((stune_data_src->boost_freq >= 0) && (stune_data_src->boost_freq <= 100)) {
		if (stune_data_src->boost_freq > stune_data_dst->boost_freq) {
			stune_data_dst->boost_freq = stune_data_src->boost_freq;
		}
	}

	if ((stune_data_src->boost_migr >= 0) && (stune_data_src->boost_migr <= 100)) {
		if (stune_data_src->boost_migr > stune_data_dst->boost_migr) {
			stune_data_dst->boost_migr = stune_data_src->boost_migr;
		}
	}

	if ((stune_data_src->util_frame_rate >= 0) && (stune_data_src->util_frame_rate <= 240)) {
		if (stune_data_src->util_frame_rate > stune_data_dst->util_frame_rate) {
			stune_data_dst->util_frame_rate = stune_data_src->util_frame_rate;
		}
	}

	if ((stune_data_src->util_min_threshold >= 0) && (stune_data_src->util_min_threshold <= 1024)) {
		if (stune_data_src->util_min_threshold > stune_data_dst->util_min_threshold) {
			stune_data_dst->util_min_threshold = stune_data_src->util_min_threshold;
		}
	}

	if ((stune_data_src->util_min_obtain_view >= 0) && (stune_data_src->util_min_obtain_view <= 1024)) {
		if (stune_data_src->util_min_obtain_view > stune_data_dst->util_min_obtain_view) {
			stune_data_dst->util_min_obtain_view = stune_data_src->util_min_obtain_view;
		}
	}

	if ((stune_data_src->util_min_timeout >= 0) && (stune_data_src->util_min_timeout <= 1024)) {
		if (stune_data_src->util_min_timeout > stune_data_dst->util_min_timeout) {
			stune_data_dst->util_min_timeout = stune_data_src->util_min_timeout;
		}
	}

	if ((stune_data_src->vutil_margin >= -16) && (stune_data_src->vutil_margin <= 16)) {
		if (stune_data_src->vutil_margin < stune_data_dst->vutil_margin) {
			stune_data_dst->vutil_margin = stune_data_src->vutil_margin;
		}
	}

	if (stune_data_src->ed_task_boost_mid_duration >= 0) {
		if (stune_data_src->ed_task_boost_mid_duration > stune_data_dst->ed_task_boost_mid_duration) {
			stune_data_dst->ed_task_boost_mid_duration = stune_data_src->ed_task_boost_mid_duration;
		}
	}

	if ((stune_data_src->ed_task_boost_mid_util >= 0) && (stune_data_src->ed_task_boost_mid_util <= 1024)) {
		if (stune_data_src->ed_task_boost_mid_util > stune_data_dst->ed_task_boost_mid_util) {
			stune_data_dst->ed_task_boost_mid_util = stune_data_src->ed_task_boost_mid_util;
		}
	}

	if (stune_data_src->ed_task_boost_max_duration >= 0) {
		if (stune_data_src->ed_task_boost_max_duration >= stune_data_dst->ed_task_boost_max_duration) {
			stune_data_dst->ed_task_boost_max_duration = stune_data_src->ed_task_boost_max_duration;
		}
	}

	if ((stune_data_src->ed_task_boost_max_util >= 0) && (stune_data_src->ed_task_boost_max_util <= 1024)) {
		if (stune_data_src->ed_task_boost_max_util > stune_data_dst->ed_task_boost_max_util) {
			stune_data_dst->ed_task_boost_max_util = stune_data_src->ed_task_boost_max_util;
		}
	}

	if (stune_data_src->ed_task_boost_timeout_duration >= 0) {
		if (stune_data_src->ed_task_boost_timeout_duration > stune_data_dst->ed_task_boost_timeout_duration) {
			stune_data_dst->ed_task_boost_timeout_duration = stune_data_src->ed_task_boost_timeout_duration;
		}
	}

	if ((stune_data_src->boost_sf_freq_nongpu >= 0) && (stune_data_src->boost_sf_freq_nongpu <= 100)) {
		if (stune_data_src->boost_sf_freq_nongpu > stune_data_dst->boost_sf_freq_nongpu) {
			stune_data_dst->boost_sf_freq_nongpu = stune_data_src->boost_sf_freq_nongpu;
		}
	}

	if ((stune_data_src->boost_sf_migr_nongpu >= 0) && (stune_data_src->boost_sf_migr_nongpu <= 100)) {
		if (stune_data_src->boost_sf_migr_nongpu > stune_data_dst->boost_sf_migr_nongpu) {
			stune_data_dst->boost_sf_migr_nongpu = stune_data_src->boost_sf_migr_nongpu;
		}
	}

	if ((stune_data_src->boost_sf_freq_gpu >= 0) && (stune_data_src->boost_sf_freq_gpu <= 100)) {
		if (stune_data_src->boost_sf_freq_gpu > stune_data_dst->boost_sf_freq_gpu) {
			stune_data_dst->boost_sf_freq_gpu = stune_data_src->boost_sf_freq_gpu;
		}
	}

	if ((stune_data_src->boost_sf_migr_gpu >= 0) && (stune_data_src->boost_sf_migr_gpu <= 100)) {
		if (stune_data_src->boost_sf_migr_gpu > stune_data_dst->boost_sf_migr_gpu) {
			stune_data_dst->boost_sf_migr_gpu = stune_data_src->boost_sf_migr_gpu;
		}
	}
}

static void setup_stune_data(struct ofb_stune_data *stune_data) {
	if ((stune_data->boost_freq >= 0) && (stune_data->boost_freq <= 100)) {
		fbg_set_stune_boost(stune_data->boost_freq, BOOST_DEF_FREQ);
	}

	if ((stune_data->boost_migr >= 0) && (stune_data->boost_migr <= 100)) {
		fbg_set_stune_boost(stune_data->boost_migr, BOOST_DEF_MIGR);
	}

	if ((stune_data->util_frame_rate >= 0) && (stune_data->util_frame_rate <= 240)) {
		fbg_set_stune_boost(stune_data->util_frame_rate, BOOST_UTIL_FRAME_RATE);
	}

	if ((stune_data->util_min_threshold >= 0) && (stune_data->util_min_threshold <= 1024)) {
		fbg_set_stune_boost(stune_data->util_min_threshold, BOOST_UTIL_MIN_THRESHOLD);
	}

	if ((stune_data->util_min_obtain_view >= 0) && (stune_data->util_min_obtain_view <= 1024)) {
		fbg_set_stune_boost(stune_data->util_min_obtain_view, BOOST_UTIL_MIN_OBTAIN_VIEW);
	}

	if ((stune_data->util_min_timeout >= 0) && (stune_data->util_min_timeout <= 1024)) {
		fbg_set_stune_boost(stune_data->util_min_timeout, BOOST_UTIL_MIN_TIMEOUT);
	}

	if ((stune_data->vutil_margin >= -16) && (stune_data->vutil_margin <= 16))
		set_frame_margin(stune_data->vutil_margin);

	if (stune_data->ed_task_boost_mid_duration >= 0) {
		fbg_set_stune_boost(stune_data->ed_task_boost_mid_duration, BOOST_ED_TASK_MID_DURATION);
		fbg_update_ed_task_duration(BOOST_ED_TASK_MID_DURATION);
	}

	if ((stune_data->ed_task_boost_mid_util >= 0) && (stune_data->ed_task_boost_mid_util <= 1024)) {
		fbg_set_stune_boost(stune_data->ed_task_boost_mid_util, BOOST_ED_TASK_MID_UTIL);
	}

	if (stune_data->ed_task_boost_max_duration >= 0) {
		fbg_set_stune_boost(stune_data->ed_task_boost_max_duration, BOOST_ED_TASK_MAX_DURATION);
		fbg_update_ed_task_duration(BOOST_ED_TASK_MAX_DURATION);
	}

	if ((stune_data->ed_task_boost_max_util >= 0) && (stune_data->ed_task_boost_max_util <= 1024)) {
		fbg_set_stune_boost(stune_data->ed_task_boost_max_util, BOOST_ED_TASK_MAX_UTIL);
	}

	if (stune_data->ed_task_boost_timeout_duration >= 0) {
		fbg_set_stune_boost(stune_data->ed_task_boost_timeout_duration, BOOST_ED_TASK_TIME_OUT_DURATION);
		fbg_update_ed_task_duration(BOOST_ED_TASK_TIME_OUT_DURATION);
	}

	if ((stune_data->boost_sf_freq_nongpu >= 0) && (stune_data->boost_sf_freq_nongpu <= 100)) {
		fbg_set_stune_boost(stune_data->boost_sf_freq_nongpu, BOOST_SF_FREQ_NONGPU);
	}

	if ((stune_data->boost_sf_migr_nongpu >= 0) && (stune_data->boost_sf_migr_nongpu <= 100)) {
		fbg_set_stune_boost(stune_data->boost_sf_migr_nongpu, BOOST_SF_MIGR_NONGPU);
	}

	if ((stune_data->boost_sf_freq_gpu >= 0) && (stune_data->boost_sf_freq_gpu <= 100)) {
		fbg_set_stune_boost(stune_data->boost_sf_freq_gpu, BOOST_SF_FREQ_GPU);
	}

	if ((stune_data->boost_sf_migr_gpu >= 0) && (stune_data->boost_sf_migr_gpu <= 100)) {
		fbg_set_stune_boost(stune_data->boost_sf_migr_gpu, BOOST_SF_MIGR_GPU);
	}
}

static long ofb_sys_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	int grp_id = 0;
	struct ofb_ctrl_data data;
	struct ofb_stune_data stune_data;
	void __user *uarg = (void __user *)arg;

	if (is_ofb_extra_cmd(cmd)) {
		return handle_ofb_extra_cmd(cmd, uarg);
	}

	if (_IOC_TYPE(cmd) != OFB_MAGIC)
		return -EINVAL;

	if (_IOC_NR(cmd) >= CMD_ID_MAX)
		return -EINVAL;

	switch (cmd) {
	case CMD_ID_BOOST_HIT:
		if (copy_from_user(&data, uarg, sizeof(data))) {
			ofb_debug("invalid address");
			return -EFAULT;
		}

		if (data.stage == BOOST_MOVE_FG) {
			grp_id = DEFAULT_FRAME_GROUP_ID;
			set_ui_thread(grp_id, data.pid, data.tid);
			set_render_thread(grp_id, data.pid, data.tid);
			set_frame_state(FRAME_END);
			rollover_frame_group_window(grp_id);
		}

		/* input method app move to top */
		if (data.stage == BOOST_MOVE_FG_IMS) {
			grp_id = INPUTMETHOD_FRAME_GROUP_ID;
			set_ui_thread(grp_id, data.pid, data.tid);
			set_render_thread(grp_id, data.pid, data.tid);
			/* bypass hwui setting for stability issue
			set_hwui_thread(grp_id, data.pid, data.hwtid1, data.hwtid2);
			*/
			fbg_set_group_policy_util(grp_id, fbg_get_stune_boost(BOOST_UTIL_MIN_TIMEOUT));
		}

		if (data.stage == BOOST_ADD_FRAME_TASK) {
			add_rm_related_frame_task(DEFAULT_FRAME_GROUP_ID, data.pid, data.tid, data.capacity_need,
				data.related_depth, data.related_width);
		}

		if (data.stage == BOOST_ADD_FRAME_TASK_IMS) {
			add_rm_related_frame_task(INPUTMETHOD_FRAME_GROUP_ID, data.pid, data.tid, data.capacity_need,
				data.related_depth, data.related_width);
		}

		break;
	case CMD_ID_SET_SF_MSG_TRANS:
		if (copy_from_user(&data, uarg, sizeof(data))) {
			ofb_debug("invalid address");
			return -EFAULT;
		}

		if (data.stage == BOOST_MSG_TRANS_START)
			rollover_frame_group_window(SF_FRAME_GROUP_ID);

		if (data.stage == BOOST_SF_EXECUTE) {
			if (data.pid == data.tid) {
				set_sf_thread(data.pid, data.tid);
			} else {
				set_renderengine_thread(data.pid, data.tid);
			}
		}

		break;
	case CMD_ID_BOOST_STUNE: {
		int uid;
		if (copy_from_user(&stune_data, uarg, sizeof(stune_data))) {
			ofb_debug("invalid address");
			return -EFAULT;
		}

		uid = task_uid(current).val;
		if (SYSTEM_UID == uid) {
			if (STUNE_DEF == stune_data.boost_freq) {
				/* non stune data from sf */
				memset(&sf_stune_data, -1, sizeof(struct ofb_stune_data));
			} else if (STUNE_SF == stune_data.boost_freq) {
				/* stune data from sf */
				memcpy(&sf_stune_data, &stune_data, sizeof(struct ofb_stune_data));
			} else {
				/* stune data from system_server */
				memcpy(&sys_stune_data, &stune_data, sizeof(struct ofb_stune_data));
			}
		} else {
			if (STUNE_DEF == stune_data.boost_freq) {
				/* stune data is from app and its data is default */
				memset(&app_stune_data, -1, sizeof(struct ofb_stune_data));
			} else {
				/* stune data from app */
				memcpy(&app_stune_data, &stune_data, sizeof(struct ofb_stune_data));
			}
		}
		stune_data = sys_stune_data;
		merge_stune_data(&app_stune_data, &stune_data);
		merge_stune_data(&sf_stune_data, &stune_data);
		setup_stune_data(&stune_data);
		}
		break;
	case CMD_ID_BOOST_STUNE_GPU: {
		bool boost_allow = true;

		if (copy_from_user(&stune_data, uarg, sizeof(stune_data))) {
			ofb_debug("invalid address");
			return -EFAULT;
		}

		/* This frame is not using client composition if data.level is zero.
		* But we still keep client composition setting with one frame extension.
		*/
		if (check_last_compose_time(stune_data.level) && !stune_data.level)
			boost_allow = false;

		if (!stune_data.level) {
			boost_allow = false;
		}

		fbg_set_stune_boost(boost_allow, BOOST_SF_IN_GPU);
		}

		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ofb_ctrl_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return ofb_ioctl(file, cmd, (unsigned long)(compat_ptr(arg)));
}

static long ofb_sys_ctrl_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return ofb_sys_ioctl(file, cmd, (unsigned long)(compat_ptr(arg)));
}
#endif

static int ofb_ctrl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int ofb_ctrl_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct proc_ops ofb_ctrl_fops = {
	.proc_ioctl	= ofb_ioctl,
	.proc_open	= ofb_ctrl_open,
	.proc_release	= ofb_ctrl_release,
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl	= ofb_ctrl_compat_ioctl,
#endif
	.proc_lseek		= default_llseek,
};

static const struct proc_ops ofb_sys_ctrl_fops = {
	.proc_ioctl	= ofb_sys_ioctl,
	.proc_open	= ofb_ctrl_open,
	.proc_release	= ofb_ctrl_release,
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl	= ofb_sys_ctrl_compat_ioctl,
#endif
	.proc_lseek 	= default_llseek,
};

static ssize_t proc_stune_boost_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	int i = 0, err;
	char buffer[256];
	char *temp_str, *token;
	char str_val[BOOST_MAX_TYPE][8];

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';
	temp_str = strstrip(buffer);
	while ((token = strsep(&temp_str, " ")) && *token && (i < BOOST_MAX_TYPE)) {
		int boost_val = 0;

		strlcpy(str_val[i], token, sizeof(str_val[i]));
		err = kstrtoint(strstrip(str_val[i]), 10, &boost_val);
		if (err)
			ofb_err("failed to write boost param (i=%d str=%s)\n", i, str_val[i]);

		if (boost_val >= 0 && i < BOOST_MAX_TYPE) {
			if (i == BOOST_UTIL_FRAME_RATE) {
				fbg_set_stune_boost(max(0, min(boost_val, 240)), i);
			} else if ((i == BOOST_UTIL_MIN_THRESHOLD) || (i == BOOST_UTIL_MIN_OBTAIN_VIEW) || (i == BOOST_UTIL_MIN_TIMEOUT)) {
				fbg_set_stune_boost(max(0, min(boost_val, 1024)), i);
			} else if ((i == BOOST_ED_TASK_MID_DURATION) || (i == BOOST_ED_TASK_MID_UTIL) ||
				(i == BOOST_ED_TASK_MAX_DURATION) || (i == BOOST_ED_TASK_MAX_UTIL) || (i == BOOST_ED_TASK_TIME_OUT_DURATION)) {
				fbg_set_stune_boost(boost_val, i);
				fbg_update_ed_task_duration(i);
			} else {
				fbg_set_stune_boost(min(boost_val, 100), i);
			}
			ofb_debug("write boost param=%d, i=%d\n", boost_val, i);
		}

		i++;
	}

	return count;
}

static char * get_stune_boost_name(int type) {
	switch(type) {
	case BOOST_DEF_MIGR:
		return "migr";
	case BOOST_DEF_FREQ:
		return "freq";
	case BOOST_UTIL_FRAME_RATE:
		return "fps";
	case BOOST_UTIL_MIN_THRESHOLD:
		return "min_threshold";
	case BOOST_UTIL_MIN_OBTAIN_VIEW:
		return "min_obtain_view";
	case BOOST_UTIL_MIN_TIMEOUT:
		return "min_timeout";
	case BOOST_SF_IN_GPU:
		return "sf_in_gpu";
	case BOOST_SF_MIGR_NONGPU:
		return "sf_migr_nongpu";
	case BOOST_SF_FREQ_NONGPU:
		return "sf_freq_nongpu";
	case BOOST_SF_MIGR_GPU:
		return "sf_migr_gpu";
	case BOOST_SF_FREQ_GPU:
		return "sf_freq_gpu";
	case BOOST_ED_TASK_MID_DURATION:
		return "ed_min_duration";
	case BOOST_ED_TASK_MID_UTIL:
		return "ed_min_util";
	case BOOST_ED_TASK_MAX_DURATION:
		return "ed_max_duration";
	case BOOST_ED_TASK_MAX_UTIL:
		return "ed_max_util";
	case BOOST_ED_TASK_TIME_OUT_DURATION:
		return "ed_timeout";
	default:
		return "unknown";
	}
}

static ssize_t proc_stune_boost_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char *buffer;
	ssize_t ret;
	int i;
	size_t len = 0;

	buffer = kmalloc(1024, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	for (i = 0; i < BOOST_MAX_TYPE; ++i)
		len += snprintf(buffer + len, 1024 - len, "%s:%d, ", get_stune_boost_name(i), fbg_get_stune_boost(i));

	len += snprintf(buffer + len, 1024 - len, "\n");

	ret = simple_read_from_buffer(buf, count, ppos, buffer, len);
	kfree(buffer);
	return ret;
}

static const struct proc_ops ofb_stune_boost_fops = {
	.proc_write		= proc_stune_boost_write,
	.proc_read		= proc_stune_boost_read,
	.proc_lseek		= default_llseek,
};

static int info_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, info_show, NULL);
}

static const struct proc_ops ofb_frame_group_info_fops = {
	.proc_open	= info_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static ssize_t proc_game_ed_info_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[128] = {0};
	int ret;
	int ed_duration;
	int ed_user_pid;

	ret = simple_write_to_buffer(buffer, sizeof(buffer), ppos, buf, count);
	if (ret <= 0)
		return ret;

	ret = sscanf(buffer, "%d %d", &ed_duration, &ed_user_pid);
	if (ret != 2)
		return -EINVAL;

	fbg_game_set_ed_info(ed_duration, ed_user_pid);

	return count;
}

static ssize_t proc_game_ed_info_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[128];
	size_t len;
	int ed_duration;
	int ed_user_pid;

	fbg_game_get_ed_info(&ed_duration, &ed_user_pid);
	len = snprintf(buffer, sizeof(buffer), "ed_duration = %d ns, ed_user_pid = %d\n",
		ed_duration, ed_user_pid);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static const struct proc_ops ofb_game_ed_info_fops = {
	.proc_write		= proc_game_ed_info_write,
	.proc_read		= proc_game_ed_info_read,
	.proc_lseek		= default_llseek,
};

#define GLOBAL_SYSTEM_UID KUIDT_INIT(1000)
#define GLOBAL_SYSTEM_GID KGIDT_INIT(1000)
int frame_ioctl_init(void)
{
	int ret = 0;
	struct proc_dir_entry *pentry;

	frame_boost_proc = proc_mkdir(FRAMEBOOST_PROC_NODE, NULL);

	pentry = proc_create("ctrl", S_IRWXUGO, frame_boost_proc, &ofb_ctrl_fops);
	if (!pentry)
		goto ERROR_INIT;

	pentry = proc_create("sys_ctrl", (S_IRWXU|S_IRWXG), frame_boost_proc, &ofb_sys_ctrl_fops);
	if (!pentry) {
		goto ERROR_INIT;
	} else {
		pentry->uid = GLOBAL_SYSTEM_UID;
		pentry->gid = GLOBAL_SYSTEM_GID;
	}

	pentry = proc_create("stune_boost", (S_IRUGO|S_IWUSR|S_IWGRP), frame_boost_proc, &ofb_stune_boost_fops);
	if (!pentry)
		goto ERROR_INIT;

	pentry = proc_create("info", S_IRUGO, frame_boost_proc, &ofb_frame_group_info_fops);
	if (!pentry)
		goto ERROR_INIT;

	pentry = proc_create("game_ed_info", (S_IRUGO|S_IWUSR|S_IWGRP), frame_boost_proc, &ofb_game_ed_info_fops);
	if (!pentry)
		goto ERROR_INIT;

	return ret;

ERROR_INIT:
	remove_proc_entry(FRAMEBOOST_PROC_NODE, NULL);
	return -ENOENT;
}

int frame_ioctl_exit(void)
{
	int ret = 0;

	if (frame_boost_proc != NULL)
		remove_proc_entry(FRAMEBOOST_PROC_NODE, NULL);

	return ret;
}
