/*
 * zte_stats_info.c - Export per-task statistics to userland
 *
 */

#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/cgroupstats.h>
#include <linux/cgroup.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pid_namespace.h>
#include <net/genetlink.h>
#include <linux/atomic.h>
#include <linux/sched/cputime.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/module.h>
#include "zte_stats_info.h"


/*
 * Maximum length of a cpumask that can be specified in
 * the ZTE_TASKSTATS_CMD_ATTR_REGISTER/DEREGISTER_CPUMASK attribute
 */
#define ZTE_TASKSTATS_CPUMASK_MAXLEN	(100+6*NR_CPUS)

static DEFINE_PER_CPU(__u32, zte_taskstats_seqnum);
static int zte_family_registered;

static struct genl_family zte_family;

static const struct nla_policy zte_taskstats_cmd_get_policy[ZTE_TASKSTATS_CMD_ATTR_MAX+1] = {
	[ZTE_TASKSTATS_CMD_ATTR_PID]  = { .type = NLA_U32 },
	[ZTE_TASKSTATS_CMD_ATTR_TGID] = { .type = NLA_U32 },
	[ZTE_TASKSTATS_CMD_ATTR_REGISTER_CPUMASK] = { .type = NLA_STRING },
	[ZTE_TASKSTATS_CMD_ATTR_DEREGISTER_CPUMASK] = { .type = NLA_STRING },};

/*
 * We have to use TASKSTATS_CMD_ATTR_MAX here, it is the maxattr in the family.
 * Make sure they are always aligned.
 */
static const struct nla_policy zte_cgroupstats_cmd_get_policy[ZTE_TASKSTATS_CMD_ATTR_MAX+1] = {
	[CGROUPSTATS_CMD_ATTR_FD] = { .type = NLA_U32 },
};

struct listener {
	struct list_head list;
	pid_t pid;
	char valid;
};

struct listener_list {
	struct rw_semaphore sem;
	struct list_head list;
};
static DEFINE_PER_CPU(struct listener_list, zte_listener_array);

enum actions {
	REGISTER,
	DEREGISTER,
	CPU_DONT_CARE
};

int zte___nla_validate_parse(const struct nlattr *head, int len, int maxtype,
		   const struct nla_policy *policy,
		   unsigned int validate,
		   struct netlink_ext_ack *extack,
		   struct nlattr **tb);


static int zte_prepare_reply(struct genl_info *info, u8 cmd, struct sk_buff **skbp,
				size_t size)
{
	struct sk_buff *skb;
	void *reply;

	/*
	 * If new attributes are added, please revisit this allocation
	 */
	skb = genlmsg_new(size, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	if (!info) {
		int seq = this_cpu_inc_return(zte_taskstats_seqnum) - 1;

		reply = genlmsg_put(skb, 0, seq, &zte_family, 0, cmd);
	} else
		reply = genlmsg_put_reply(skb, info, &zte_family, 0, cmd);
	if (reply == NULL) {
		nlmsg_free(skb);
		return -EINVAL;
	}

	*skbp = skb;
	return 0;
}

/*
 * Send taskstats data in @skb to listener with nl_pid @pid
 */
static int zte_send_reply(struct sk_buff *skb, struct genl_info *info)
{
	struct genlmsghdr *genlhdr = nlmsg_data(nlmsg_hdr(skb));
	void *reply = genlmsg_data(genlhdr);

	genlmsg_end(skb, reply);

	return genlmsg_reply(skb, info);
}

static int zte_delayacct_add_tsk(struct zte_taskstats *d, struct task_struct *tsk)
{
	u64 utime, stime, stimescaled, utimescaled;
	unsigned long long t2, t3;
	unsigned long t1;
	s64 tmp;

	task_cputime(tsk, &utime, &stime);
	tmp = (s64)d->cpu_run_real_total;
	tmp += utime + stime;
	d->cpu_run_real_total = (tmp < (s64)d->cpu_run_real_total) ? 0 : tmp;

	task_cputime_scaled(tsk, &utimescaled, &stimescaled);
	tmp = (s64)d->cpu_scaled_run_real_total;
	tmp += utimescaled + stimescaled;
	d->cpu_scaled_run_real_total =
		(tmp < (s64)d->cpu_scaled_run_real_total) ? 0 : tmp;

	/*
	 * No locking available for sched_info (and too expensive to add one)
	 * Mitigate by taking snapshot of values
	 */
	t1 = tsk->sched_info.pcount;
	t2 = tsk->sched_info.run_delay;
	t3 = tsk->se.sum_exec_runtime;

	d->cpu_count += t1;

	tmp = (s64)d->cpu_delay_total + t2;
	d->cpu_delay_total = (tmp < (s64)d->cpu_delay_total) ? 0 : tmp;

	tmp = (s64)d->cpu_run_virtual_total + t3;
	d->cpu_run_virtual_total =
		(tmp < (s64)d->cpu_run_virtual_total) ?	0 : tmp;

	/* zero XXX_total, non-zero XXX_count implies XXX stat overflowed */

	return 0;
}

/*
 * fill in basic accounting fields
 */
static void zte_bacct_add_tsk(struct user_namespace *user_ns,
		   struct pid_namespace *pid_ns,
		   struct zte_taskstats *stats, struct task_struct *tsk)
{
	const struct cred *tcred;
	u64 utime, stime, utimescaled, stimescaled;
	u64 delta;
	time64_t btime;

	BUILD_BUG_ON(TS_COMM_LEN < TASK_COMM_LEN);

	/* calculate task elapsed time in nsec */
	delta = ktime_get_ns() - tsk->start_time;
	/* Convert to micro seconds */
	do_div(delta, NSEC_PER_USEC);
	stats->ac_etime = delta;
	/* Convert to seconds for btime (note y2106 limit) */
	btime = ktime_get_real_seconds() - div_u64(delta, USEC_PER_SEC);
	stats->ac_btime = clamp_t(time64_t, btime, 0, U32_MAX);
	stats->ac_btime64 = btime;

	if (tsk->flags & PF_EXITING)
		stats->ac_exitcode = tsk->exit_code;
	if (thread_group_leader(tsk) && (tsk->flags & PF_FORKNOEXEC))
		stats->ac_flag |= AFORK;
	if (tsk->flags & PF_SUPERPRIV)
		stats->ac_flag |= ASU;
	if (tsk->flags & PF_DUMPCORE)
		stats->ac_flag |= ACORE;
	if (tsk->flags & PF_SIGNALED)
		stats->ac_flag |= AXSIG;
	stats->ac_nice	 = task_nice(tsk);
	stats->ac_sched	 = tsk->policy;
	stats->ac_pid	 = task_pid_nr_ns(tsk, pid_ns);
	rcu_read_lock();
	tcred = __task_cred(tsk);
	stats->ac_uid	 = from_kuid(user_ns, tcred->uid);
	stats->ac_gid	 = from_kgid(user_ns, tcred->gid);
	stats->ac_ppid	 = pid_alive(tsk) ?
		task_tgid_nr_ns(rcu_dereference(tsk->real_parent), pid_ns) : 0;
	rcu_read_unlock();

	task_cputime(tsk, &utime, &stime);
	stats->ac_utime = div_u64(utime, NSEC_PER_USEC);
	stats->ac_stime = div_u64(stime, NSEC_PER_USEC);

	task_cputime_scaled(tsk, &utimescaled, &stimescaled);
	stats->ac_utimescaled = div_u64(utimescaled, NSEC_PER_USEC);
	stats->ac_stimescaled = div_u64(stimescaled, NSEC_PER_USEC);

	stats->ac_minflt = tsk->min_flt;
	stats->ac_majflt = tsk->maj_flt;

	strncpy(stats->ac_comm, tsk->comm, sizeof(stats->ac_comm));
}

#define KB 1024
#define MB (1024*KB)
#define KB_MASK (~(KB-1))
/*
 * fill in extended accounting fields
 */
static void zte_xacct_add_tsk(struct zte_taskstats *stats, struct task_struct *p)
{
	struct mm_struct *mm;

	/* convert pages-nsec/1024 to Mbyte-usec, see __acct_update_integrals */
	stats->coremem = p->acct_rss_mem1 * PAGE_SIZE;
	do_div(stats->coremem, 1000 * KB);
	stats->virtmem = p->acct_vm_mem1 * PAGE_SIZE;
	do_div(stats->virtmem, 1000 * KB);
	mm = get_task_mm(p);
	if (mm) {
		/* adjust to KB unit */
		stats->hiwater_rss   = get_mm_hiwater_rss(mm) * PAGE_SIZE / KB;
		stats->hiwater_vm    = get_mm_hiwater_vm(mm)  * PAGE_SIZE / KB;
		mmput(mm);
	}
	stats->read_char	= p->ioac.rchar & KB_MASK;
	stats->write_char	= p->ioac.wchar & KB_MASK;
	stats->read_syscalls	= p->ioac.syscr & KB_MASK;
	stats->write_syscalls	= p->ioac.syscw & KB_MASK;
#ifdef CONFIG_TASK_IO_ACCOUNTING
	stats->read_bytes	= p->ioac.read_bytes & KB_MASK;
	stats->write_bytes	= p->ioac.write_bytes & KB_MASK;
	stats->cancelled_write_bytes = p->ioac.cancelled_write_bytes & KB_MASK;
#else
	stats->read_bytes	= 0;
	stats->write_bytes	= 0;
	stats->cancelled_write_bytes = 0;
#endif
}
#undef KB
#undef MB

static void zte_fill_stats(struct user_namespace *user_ns,
		       struct pid_namespace *pid_ns,
		       struct task_struct *tsk, struct zte_taskstats *stats)
{
	memset(stats, 0, sizeof(*stats));
	/*
	 * Each accounting subsystem adds calls to its functions to
	 * fill in relevant parts of struct taskstsats as follows
	 *
	 *	per-task-foo(stats, tsk);
	 */

	zte_delayacct_add_tsk(stats, tsk);

	/* fill in basic acct fields */
	stats->version = ZTE_TASKSTATS_VERSION;
	stats->nvcsw = tsk->nvcsw;
	stats->nivcsw = tsk->nivcsw;
	zte_bacct_add_tsk(user_ns, pid_ns, stats, tsk);

	/* fill in extended acct fields */
	zte_xacct_add_tsk(stats, tsk);
}

struct pid_namespace *zte_task_active_pid_ns(struct task_struct *tsk)
{
    struct pid_namespace *ns = NULL;
	if (tsk->thread_pid)
		ns = tsk->thread_pid->numbers[tsk->thread_pid->level].ns;
    return ns;
}			   

struct pid *zte_find_pid_ns(int nr, struct pid_namespace *ns)
{
  return idr_find(&ns->idr, nr);
}

struct task_struct *zte_get_task_by_vpid(pid_t vnr)
{
	return get_pid_task(zte_find_pid_ns(vnr, zte_task_active_pid_ns(current)), PIDTYPE_PID);
}


static int zte_fill_stats_for_pid(pid_t pid, struct zte_taskstats *stats)
{
	struct task_struct *tsk;

	tsk = zte_get_task_by_vpid(pid);

	if (!tsk)
		return -ESRCH;

	zte_fill_stats(current_user_ns(), zte_task_active_pid_ns(current), tsk, stats);
	put_task_struct(tsk);
	return 0;
}

//test begin
struct sighand_struct *__zte_lock_task_sighand(struct task_struct *tsk,
					   unsigned long *flags)
{
	struct sighand_struct *sighand;

	rcu_read_lock();
	for (;;) {
		sighand = rcu_dereference(tsk->sighand);
		if (unlikely(sighand == NULL))
			break;

		/*
		 * This sighand can be already freed and even reused, but
		 * we rely on SLAB_TYPESAFE_BY_RCU and sighand_ctor() which
		 * initializes ->siglock: this slab can't go away, it has
		 * the same object type, ->siglock can't be reinitialized.
		 *
		 * We need to ensure that tsk->sighand is still the same
		 * after we take the lock, we can race with de_thread() or
		 * __exit_signal(). In the latter case the next iteration
		 * must see ->sighand == NULL.
		 */
		spin_lock_irqsave(&sighand->siglock, *flags);
		if (likely(sighand == rcu_access_pointer(tsk->sighand)))
			break;
		spin_unlock_irqrestore(&sighand->siglock, *flags);
	}
	rcu_read_unlock();

	return sighand;
}

static inline struct sighand_struct *zte_lock_task_sighand(struct task_struct *task,
						       unsigned long *flags)
{
	struct sighand_struct *ret;

	ret = __zte_lock_task_sighand(task, flags);
	(void)__cond_lock(&task->sighand->siglock, ret);
	return ret;
}

static inline void zte_unlock_task_sighand(struct task_struct *task,
						unsigned long *flags)
{
	spin_unlock_irqrestore(&task->sighand->siglock, *flags);
}
//test end

static int zte_fill_stats_for_tgid(pid_t tgid, struct zte_taskstats *stats)
{
	struct task_struct *tsk, *first;
	unsigned long flags;
	int rc = -ESRCH;
	u64 delta, utime, stime;
	u64 start_time;

	/*
	 * Add additional stats from live tasks except zombie thread group
	 * leaders who are already counted with the dead tasks
	 */

	first = zte_get_task_by_vpid(tgid);

	if (!first || !zte_lock_task_sighand(first, &flags))
		goto out;

	if (first->signal->stats)
		memcpy(stats, first->signal->stats, sizeof(*stats));
	else
		memset(stats, 0, sizeof(*stats));

	tsk = first;
	start_time = ktime_get_ns();
	do {
		if (tsk->exit_state)
			continue;
		/*
		 * Accounting subsystem can call its functions here to
		 * fill in relevant parts of struct taskstsats as follows
		 *
		 *	per-task-foo(stats, tsk);
		 */
		zte_delayacct_add_tsk(stats, tsk);

		/* calculate task elapsed time in nsec */
		delta = start_time - tsk->start_time;
		/* Convert to micro seconds */
		do_div(delta, NSEC_PER_USEC);
		stats->ac_etime += delta;

		task_cputime(tsk, &utime, &stime);
		stats->ac_utime += div_u64(utime, NSEC_PER_USEC);
		stats->ac_stime += div_u64(stime, NSEC_PER_USEC);

		stats->nvcsw += tsk->nvcsw;
		stats->nivcsw += tsk->nivcsw;
	} while_each_thread(first, tsk);

	zte_unlock_task_sighand(first, &flags);
	put_task_struct(first);
	rc = 0;
out:

	stats->version = ZTE_TASKSTATS_VERSION;
	/*
	 * Accounting subsystems can also add calls here to modify
	 * fields of taskstats.
	 */

	return rc;
}

static int zte_add_del_listener(pid_t pid, const struct cpumask *mask, int isadd)
{
	struct listener_list *listeners;
	struct listener *s, *tmp, *s2;
	unsigned int cpu;
	int ret = 0;

	if (!cpumask_subset(mask, cpu_possible_mask))
		return -EINVAL;

	if (current_user_ns() != &init_user_ns)
		return -EINVAL;

	//if (zte_task_active_pid_ns(current) != &init_pid_ns)
	//	return -EINVAL;

	if (isadd == REGISTER) {
		for_each_cpu(cpu, mask) {
			s = kmalloc_node(sizeof(struct listener),
					GFP_KERNEL, cpu_to_node(cpu));
			if (!s) {
				ret = -ENOMEM;
				goto cleanup;
			}
			s->pid = pid;
			s->valid = 1;

			listeners = &per_cpu(zte_listener_array, cpu);
			down_write(&listeners->sem);
			list_for_each_entry(s2, &listeners->list, list) {
				if (s2->pid == pid && s2->valid)
					goto exists;
			}
			list_add(&s->list, &listeners->list);
			s = NULL;
exists:
			up_write(&listeners->sem);
			kfree(s); /* nop if NULL */
		}
		return 0;
	}

	/* Deregister or cleanup */
cleanup:
	for_each_cpu(cpu, mask) {
		listeners = &per_cpu(zte_listener_array, cpu);
		down_write(&listeners->sem);
		list_for_each_entry_safe(s, tmp, &listeners->list, list) {
			if (s->pid == pid) {
				list_del(&s->list);
				kfree(s);
				break;
			}
		}
		up_write(&listeners->sem);
	}
	return ret;
}

static int zte_parse(struct nlattr *na, struct cpumask *mask)
{
	char *data;
	int len;
	int ret;

	if (na == NULL)
		return 1;
	len = nla_len(na);
	if (len > ZTE_TASKSTATS_CPUMASK_MAXLEN)
		return -E2BIG;
	if (len < 1)
		return -EINVAL;
	data = kmalloc(len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	nla_strlcpy(data, na, len);
	ret = cpulist_parse(data, mask);
	kfree(data);
	return ret;
}

struct nlattr *zte__nla_reserve(struct sk_buff *skb, int attrtype, int attrlen)
{
  struct nlattr *nla;

  nla = skb_put(skb, nla_total_size(attrlen));
  nla->nla_type = attrtype;
  nla->nla_len = nla_attr_size(attrlen);

  memset((unsigned char *) nla + nla->nla_len, 0, nla_padlen(attrlen));

  return nla;
}

struct nlattr *zte__nla_reserve_64bit(struct sk_buff *skb, int attrtype,
				 int attrlen, int padattr)
{
  if (nla_need_padding_for_64bit(skb))
	  nla_align_64bit(skb, padattr);

  return zte__nla_reserve(skb, attrtype, attrlen);
}

struct nlattr *zte_nla_reserve_64bit(struct sk_buff *skb, int attrtype, int attrlen,
			   int padattr)
{
  size_t len;

  if (nla_need_padding_for_64bit(skb))
	  len = nla_total_size_64bit(attrlen);
  else
	  len = nla_total_size(attrlen);
  if (unlikely(skb_tailroom(skb) < len))
	  return NULL;

  return zte__nla_reserve_64bit(skb, attrtype, attrlen, padattr);
}


static struct zte_taskstats *zte_mk_reply(struct sk_buff *skb, int type, u32 pid)
{
	struct nlattr *na, *ret;
	int aggr;

	aggr = (type == ZTE_TASKSTATS_TYPE_PID)
			? ZTE_TASKSTATS_TYPE_AGGR_PID
			: ZTE_TASKSTATS_TYPE_AGGR_TGID;

	na = nla_nest_start_noflag(skb, aggr);
	if (!na)
		goto err;

	if (nla_put(skb, type, sizeof(pid), &pid) < 0) {
		nla_nest_cancel(skb, na);
		goto err;
	}
	ret = zte_nla_reserve_64bit(skb, ZTE_TASKSTATS_TYPE_STATS,
				sizeof(struct zte_taskstats), ZTE_TASKSTATS_TYPE_NULL);
	if (!ret) {
		nla_nest_cancel(skb, na);
		goto err;
	}
	nla_nest_end(skb, na);

	return nla_data(ret);
err:
	return NULL;
}

static int zte_cgroupstats_user_cmd(struct sk_buff *skb, struct genl_info *info)
{
	int rc = 0;
#if 0
	struct sk_buff *rep_skb;
	struct cgroupstats *stats;
	struct nlattr *na;
	size_t size;
	u32 fd;
	struct fd f;

	na = info->attrs[CGROUPSTATS_CMD_ATTR_FD];
	if (!na)
		return -EINVAL;

	fd = nla_get_u32(info->attrs[CGROUPSTATS_CMD_ATTR_FD]);
	f = fdget(fd);
	if (!f.file)
		return 0;

	size = nla_total_size(sizeof(struct cgroupstats));

	rc = zte_prepare_reply(info, CGROUPSTATS_CMD_NEW, &rep_skb,
				size);
	if (rc < 0)
		goto err;

	na = nla_reserve(rep_skb, CGROUPSTATS_TYPE_CGROUP_STATS,
				sizeof(struct cgroupstats));
	if (na == NULL) {
		nlmsg_free(rep_skb);
		rc = -EMSGSIZE;
		goto err;
	}

	stats = nla_data(na);
	memset(stats, 0, sizeof(*stats));

	rc = cgroupstats_build(stats, f.file->f_path.dentry);
	if (rc < 0) {
		nlmsg_free(rep_skb);
		goto err;
	}

	rc = zte_send_reply(rep_skb, info);

err:
	fdput(f);
#endif
	return rc;
}

static int zte_cmd_attr_register_cpumask(struct genl_info *info)
{
	cpumask_var_t mask;
	int rc;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;
	rc = zte_parse(info->attrs[ZTE_TASKSTATS_CMD_ATTR_REGISTER_CPUMASK], mask);
	if (rc < 0)
		goto out;
	rc = zte_add_del_listener(info->snd_portid, mask, REGISTER);
out:
	free_cpumask_var(mask);
	return rc;
}

static int zte_cmd_attr_deregister_cpumask(struct genl_info *info)
{
	cpumask_var_t mask;
	int rc;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;
	rc = zte_parse(info->attrs[ZTE_TASKSTATS_CMD_ATTR_DEREGISTER_CPUMASK], mask);
	if (rc < 0)
		goto out;
	rc = zte_add_del_listener(info->snd_portid, mask, DEREGISTER);
out:
	free_cpumask_var(mask);
	return rc;
}

static size_t zte_taskstats_packet_size(void)
{
	size_t size;

	size = nla_total_size(sizeof(u32)) +
		nla_total_size_64bit(sizeof(struct zte_taskstats)) +
		nla_total_size(0);

	return size;
}

static int zte_cmd_attr_pid(struct genl_info *info)
{
	struct zte_taskstats *stats;
	struct sk_buff *rep_skb;
	size_t size;
	u32 pid;
	int rc;

	size = zte_taskstats_packet_size();

	rc = zte_prepare_reply(info, ZTE_TASKSTATS_CMD_NEW, &rep_skb, size);
	if (rc < 0)
		return rc;

	rc = -EINVAL;
	pid = nla_get_u32(info->attrs[ZTE_TASKSTATS_CMD_ATTR_PID]);
	stats = zte_mk_reply(rep_skb, ZTE_TASKSTATS_TYPE_PID, pid);
	if (!stats)
		goto err;

	rc = zte_fill_stats_for_pid(pid, stats);
	if (rc < 0)
		goto err;
	return zte_send_reply(rep_skb, info);
err:
	nlmsg_free(rep_skb);
	return rc;
}

static int zte_cmd_attr_tgid(struct genl_info *info)
{
	struct zte_taskstats *stats;
	struct sk_buff *rep_skb;
	size_t size;
	u32 tgid;
	int rc;

	size = zte_taskstats_packet_size();

	rc = zte_prepare_reply(info, ZTE_TASKSTATS_CMD_NEW, &rep_skb, size);
	if (rc < 0)
		return rc;

	rc = -EINVAL;
	tgid = nla_get_u32(info->attrs[ZTE_TASKSTATS_CMD_ATTR_TGID]);
	stats = zte_mk_reply(rep_skb, ZTE_TASKSTATS_TYPE_TGID, tgid);
	if (!stats)
		goto err;

	rc = zte_fill_stats_for_tgid(tgid, stats);
	if (rc < 0)
		goto err;
	return zte_send_reply(rep_skb, info);
err:
	nlmsg_free(rep_skb);
	return rc;
}

static int zte_taskstats_user_cmd(struct sk_buff *skb, struct genl_info *info)
{
	if (info->attrs[ZTE_TASKSTATS_CMD_ATTR_REGISTER_CPUMASK])
		return zte_cmd_attr_register_cpumask(info);
	else if (info->attrs[ZTE_TASKSTATS_CMD_ATTR_DEREGISTER_CPUMASK])
		return zte_cmd_attr_deregister_cpumask(info);
	else if (info->attrs[ZTE_TASKSTATS_CMD_ATTR_PID])
		return zte_cmd_attr_pid(info);
	else if (info->attrs[ZTE_TASKSTATS_CMD_ATTR_TGID])
		return zte_cmd_attr_tgid(info);
	else
		return -EINVAL;
}


static const struct genl_ops zte_taskstats_ops[] = {
	{
		.cmd		= ZTE_TASKSTATS_CMD_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit		= zte_taskstats_user_cmd,
		/* policy enforced later */

		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_HASPOL,
	},
	{
		.cmd		= CGROUPSTATS_CMD_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit		= zte_cgroupstats_user_cmd,
		/* policy enforced later */
		.flags		= GENL_CMD_CAP_HASPOL,
	},
};

static const u8 zte_nla_attr_len[NLA_TYPE_MAX+1] = {
  [NLA_U8]	  = sizeof(u8),
  [NLA_U16]   = sizeof(u16),
  [NLA_U32]   = sizeof(u32),
  [NLA_U64]   = sizeof(u64),
  [NLA_S8]	  = sizeof(s8),
  [NLA_S16]   = sizeof(s16),
  [NLA_S32]   = sizeof(s32),
  [NLA_S64]   = sizeof(s64),
};

static const u8 zte_nla_attr_minlen[NLA_TYPE_MAX+1] = {
  [NLA_U8]	  = sizeof(u8),
  [NLA_U16]   = sizeof(u16),
  [NLA_U32]   = sizeof(u32),
  [NLA_U64]   = sizeof(u64),
  [NLA_MSECS] = sizeof(u64),
  [NLA_NESTED]	  = NLA_HDRLEN,
  [NLA_S8]	  = sizeof(s8),
  [NLA_S16]   = sizeof(s16),
  [NLA_S32]   = sizeof(s32),
  [NLA_S64]   = sizeof(s64),
};

static int zte_nla_validate_int_range(const struct nla_policy *pt,
				 const struct nlattr *nla,
				 struct netlink_ext_ack *extack)
 {
   bool validate_min, validate_max;
   s64 value;
 
   validate_min = pt->validation_type == NLA_VALIDATE_RANGE ||
			  pt->validation_type == NLA_VALIDATE_MIN;
   validate_max = pt->validation_type == NLA_VALIDATE_RANGE ||
			  pt->validation_type == NLA_VALIDATE_MAX;
 
   switch (pt->type) {
   case NLA_U8:
	   value = nla_get_u8(nla);
	   break;
   case NLA_U16:
	   value = nla_get_u16(nla);
	   break;
   case NLA_U32:
	   value = nla_get_u32(nla);
	   break;
   case NLA_S8:
	   value = nla_get_s8(nla);
	   break;
   case NLA_S16:
	   value = nla_get_s16(nla);
	   break;
   case NLA_S32:
	   value = nla_get_s32(nla);
	   break;
   case NLA_S64:
	   value = nla_get_s64(nla);
	   break;
   case NLA_U64:
	   /* treat this one specially, since it may not fit into s64 */
	   if ((validate_min && nla_get_u64(nla) < pt->min) ||
		   (validate_max && nla_get_u64(nla) > pt->max)) {
		   NL_SET_ERR_MSG_ATTR(extack, nla,
					   "integer out of range");
		   return -ERANGE;
	   }
	   return 0;
   default:
	   WARN_ON(1);
	   return -EINVAL;
   }
 
   if ((validate_min && value < pt->min) ||
	   (validate_max && value > pt->max)) {
	   NL_SET_ERR_MSG_ATTR(extack, nla,
				   "integer out of range");
	   return -ERANGE;
   }
 
   return 0;
 }

int zte___nla_validate(const struct nlattr *head, int len, int maxtype,
  const struct nla_policy *policy, unsigned int validate,
  struct netlink_ext_ack *extack)
{
 return zte___nla_validate_parse(head, len, maxtype, policy, validate,
	 extack, NULL);
}

static int zte_nla_validate_array(const struct nlattr *head, int len, int maxtype,
			 const struct nla_policy *policy,
			 struct netlink_ext_ack *extack,
			 unsigned int validate)
{
  const struct nlattr *entry;
  int rem;

  nla_for_each_attr(entry, head, len, rem) {
   int ret;

   if (nla_len(entry) == 0)
	   continue;

   if (nla_len(entry) < NLA_HDRLEN) {
	   NL_SET_ERR_MSG_ATTR(extack, entry,
				   "Array element too short");
	   return -ERANGE;
   }

   ret = zte___nla_validate(nla_data(entry), nla_len(entry),
				maxtype, policy, validate, extack);
   if (ret < 0)
	   return ret;
  }

  return 0;
}


static int zte_validate_nla_bitfield32(const struct nlattr *nla,
			  const u32 *valid_flags_mask)
{
  const struct nla_bitfield32 *bf = nla_data(nla);

  if (!valid_flags_mask)
   return -EINVAL;

  /*disallow invalid bit selector */
  if (bf->selector & ~*valid_flags_mask)
   return -EINVAL;

  /*disallow invalid bit values */
  if (bf->value & ~*valid_flags_mask)
   return -EINVAL;

  /*disallow valid bit values that are not selected*/
  if (bf->value & ~bf->selector)
   return -EINVAL;

  return 0;
}


static int zte_validate_nla(const struct nlattr *nla, int maxtype,
	   const struct nla_policy *policy, unsigned int validate,
	   struct netlink_ext_ack *extack)
{
  u16 strict_start_type = policy[0].strict_start_type;
  const struct nla_policy *pt;
  int minlen = 0, attrlen = nla_len(nla), type = nla_type(nla);
  int err = -ERANGE;

  if (strict_start_type && type >= strict_start_type)
   validate |= NL_VALIDATE_STRICT;

  if (type <= 0 || type > maxtype)
   return 0;

  pt = &policy[type];

  BUG_ON(pt->type > NLA_TYPE_MAX);

  if ((zte_nla_attr_len[pt->type] && attrlen != zte_nla_attr_len[pt->type]) ||
   (pt->type == NLA_EXACT_LEN_WARN && attrlen != pt->len)) {
   pr_warn_ratelimited("netlink: '%s': attribute type %d has an invalid length.\n",
			   current->comm, type);
   if (validate & NL_VALIDATE_STRICT_ATTRS) {
	   NL_SET_ERR_MSG_ATTR(extack, nla,
				   "invalid attribute length");
	   return -EINVAL;
   }
  }

  if (validate & NL_VALIDATE_NESTED) {
   if ((pt->type == NLA_NESTED || pt->type == NLA_NESTED_ARRAY) &&
	   !(nla->nla_type & NLA_F_NESTED)) {
	   NL_SET_ERR_MSG_ATTR(extack, nla,
				   "NLA_F_NESTED is missing");
	   return -EINVAL;
   }
   if (pt->type != NLA_NESTED && pt->type != NLA_NESTED_ARRAY &&
	   pt->type != NLA_UNSPEC && (nla->nla_type & NLA_F_NESTED)) {
	   NL_SET_ERR_MSG_ATTR(extack, nla,
				   "NLA_F_NESTED not expected");
	   return -EINVAL;
   }
  }

  switch (pt->type) {
  case NLA_EXACT_LEN:
   if (attrlen != pt->len)
	   goto out_err;
   break;

  case NLA_REJECT:
   if (extack && pt->validation_data) {
	   NL_SET_BAD_ATTR(extack, nla);
	   extack->_msg = pt->validation_data;
	   return -EINVAL;
   }
   err = -EINVAL;
   goto out_err;

  case NLA_FLAG:
   if (attrlen > 0)
	   goto out_err;
   break;

  case NLA_BITFIELD32:
   if (attrlen != sizeof(struct nla_bitfield32))
	   goto out_err;

   err = zte_validate_nla_bitfield32(nla, pt->validation_data);
   if (err)
	   goto out_err;
   break;

  case NLA_NUL_STRING:
   if (pt->len)
	   minlen = min_t(int, attrlen, pt->len + 1);
   else
	   minlen = attrlen;

   if (!minlen || memchr(nla_data(nla), '\0', minlen) == NULL) {
	   err = -EINVAL;
	   goto out_err;
   }
   /* fall through */

  case NLA_STRING:
   if (attrlen < 1)
	   goto out_err;

   if (pt->len) {
	   char *buf = nla_data(nla);

	   if (buf[attrlen - 1] == '\0')
		   attrlen--;

	   if (attrlen > pt->len)
		   goto out_err;
   }
   break;

  case NLA_BINARY:
   if (pt->len && attrlen > pt->len)
	   goto out_err;
   break;

  case NLA_NESTED:
   /* a nested attributes is allowed to be empty; if its not,
	* it must have a size of at least NLA_HDRLEN.
	*/
   if (attrlen == 0)
	   break;
   if (attrlen < NLA_HDRLEN)
	   goto out_err;
   if (pt->validation_data) {
	   err = zte___nla_validate(nla_data(nla), nla_len(nla), pt->len,
					pt->validation_data, validate,
					extack);
	   if (err < 0) {
		   /*
			* return directly to preserve the inner
			* error message/attribute pointer
			*/
		   return err;
	   }
   }
   break;
  case NLA_NESTED_ARRAY:
   /* a nested array attribute is allowed to be empty; if its not,
	* it must have a size of at least NLA_HDRLEN.
	*/
   if (attrlen == 0)
	   break;
   if (attrlen < NLA_HDRLEN)
	   goto out_err;
   if (pt->validation_data) {
	   int err;

	   err = zte_nla_validate_array(nla_data(nla), nla_len(nla),
					pt->len, pt->validation_data,
					extack, validate);
	   if (err < 0) {
		   /*
			* return directly to preserve the inner
			* error message/attribute pointer
			*/
		   return err;
	   }
   }
   break;

  case NLA_UNSPEC:
   if (validate & NL_VALIDATE_UNSPEC) {
	   NL_SET_ERR_MSG_ATTR(extack, nla,
				   "Unsupported attribute");
	   return -EINVAL;
   }
   /* fall through */
  case NLA_MIN_LEN:
   if (attrlen < pt->len)
	   goto out_err;
   break;

 default:
   if (pt->len)
	   minlen = pt->len;
   else
	   minlen = zte_nla_attr_minlen[pt->type];

   if (attrlen < minlen)
	   goto out_err;
  }

  /* further validation */
  switch (pt->validation_type) {
  case NLA_VALIDATE_NONE:
   /* nothing to do */
   break;
  case NLA_VALIDATE_RANGE:
  case NLA_VALIDATE_MIN:
  case NLA_VALIDATE_MAX:
   err = zte_nla_validate_int_range(pt, nla, extack);
   if (err)
	   return err;
   break;
  case NLA_VALIDATE_FUNCTION:
   if (pt->validate) {
	   err = pt->validate(nla, extack);
	   if (err)
 	    return err;
   }
   break;
  }

  return 0;
out_err:
  NL_SET_ERR_MSG_ATTR(extack, nla, "Attribute failed policy validation");
  return err;
}

int zte___nla_validate_parse(const struct nlattr *head, int len, int maxtype,
		   const struct nla_policy *policy,
		   unsigned int validate,
		   struct netlink_ext_ack *extack,
		   struct nlattr **tb)
{
  const struct nlattr *nla;
  int rem;

  if (tb)
   memset(tb, 0, sizeof(struct nlattr *) * (maxtype + 1));

  nla_for_each_attr(nla, head, len, rem) {
   u16 type = nla_type(nla);

   if (type == 0 || type > maxtype) {
	   if (validate & NL_VALIDATE_MAXTYPE) {
		   NL_SET_ERR_MSG_ATTR(extack, nla,
					   "Unknown attribute type");
		   return -EINVAL;
	   }
	   continue;
   }
   if (policy) {
	   int err = zte_validate_nla(nla, maxtype, policy,
					  validate, extack);

	   if (err < 0)
		   return err;
   }

   if (tb)
	   tb[type] = (struct nlattr *)nla;
  }

  if (unlikely(rem > 0)) {
   pr_warn_ratelimited("netlink: %d bytes leftover after parsing attributes in process `%s'.\n",
			   rem, current->comm);
   NL_SET_ERR_MSG(extack, "bytes leftover after parsing attributes");
   if (validate & NL_VALIDATE_TRAILING)
	   return -EINVAL;
  }

  return 0;
}

static inline int zte_nlmsg_validate_deprecated(const struct nlmsghdr *nlh,
					  int hdrlen, int maxtype,
					  const struct nla_policy *policy,
					  struct netlink_ext_ack *extack)
{
  if (nlh->nlmsg_len < nlmsg_msg_size(hdrlen))
	  return -EINVAL;

  return zte___nla_validate(nlmsg_attrdata(nlh, hdrlen),
				nlmsg_attrlen(nlh, hdrlen), maxtype,
				policy, NL_VALIDATE_LIBERAL, extack);
}

static int zte_taskstats_pre_doit(const struct genl_ops *ops, struct sk_buff *skb,
			      struct genl_info *info)
{
	const struct nla_policy *policy = NULL;

	switch (ops->cmd) {
	case ZTE_TASKSTATS_CMD_GET:
		policy = zte_taskstats_cmd_get_policy;
		break;
	case CGROUPSTATS_CMD_GET:
		policy = zte_cgroupstats_cmd_get_policy;
		break;
	default:
		return -EINVAL;
	}

	return zte_nlmsg_validate_deprecated(info->nlhdr, GENL_HDRLEN,
					 ZTE_TASKSTATS_CMD_ATTR_MAX, policy,
					 info->extack);
}

static struct genl_family zte_family __ro_after_init = {
	.name		= ZTE_TASKSTATS_GENL_NAME,
	.version	= ZTE_TASKSTATS_GENL_VERSION,
	.maxattr	= ZTE_TASKSTATS_CMD_ATTR_MAX,
	.module		= THIS_MODULE,
	.ops		= zte_taskstats_ops,
	.n_ops		= ARRAY_SIZE(zte_taskstats_ops),
	.pre_doit	= zte_taskstats_pre_doit,
};

/* Needed early in initialization */
static void zte_taskstats_init_early(void)
{
	unsigned int i;

	for_each_possible_cpu(i) {
		INIT_LIST_HEAD(&(per_cpu(zte_listener_array, i).list));
		init_rwsem(&(per_cpu(zte_listener_array, i).sem));
	}
}

static int zte_taskstats_init(void)
{
	int rc;

	zte_taskstats_init_early();

	rc = genl_register_family(&zte_family);
	if (rc)
		return rc;

	zte_family_registered = 1;
	pr_info("registered zte_taskstats version %d\n", ZTE_TASKSTATS_GENL_VERSION);
	return 0;
}

/*
 * late initcall ensures initialization of statistics collection
 * mechanisms precedes initialization of the taskstats interface
 */
late_initcall(zte_taskstats_init);

MODULE_DESCRIPTION("zte stats info driver");
MODULE_LICENSE("GPL");

