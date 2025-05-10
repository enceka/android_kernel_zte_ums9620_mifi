#if IS_ENABLED(CONFIG_VENDOR_ZTE_LOG_EXCEPTION)
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/ktime.h>
#include <linux/rtc.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include "zlog_common_base.h"

#define ZLOG_BUILTIN_RECORDS_MAX_NUMS 10
#define LOG_BUFFER_LEN 256
#define CONTENT_DATA_MAX_LEN 128
#define TIME_BUFFER_LEN 32

#define MAGIC_NUM 0x3b97ac12
struct exception_builtin_records_type {
	unsigned int magic;
	struct zlog_mod_info *modinfo;
	int error_no;
	char data[CONTENT_DATA_MAX_LEN];

};

static DECLARE_WAIT_QUEUE_HEAD(exception_wait);
static struct exception_builtin_records_type exception_builtin_records[ZLOG_BUILTIN_RECORDS_MAX_NUMS];
static DEFINE_SPINLOCK(exception_report_lock);
static char time_buffer[TIME_BUFFER_LEN];
static char log_buffer[LOG_BUFFER_LEN];
static int exception_log_available = 0;

static void exception_work_func(struct work_struct *work)
{
	wake_up_interruptible(&exception_wait);
}

static DECLARE_DELAYED_WORK(exception_work, exception_work_func);

int zte_exception_record_and_notify_builtin(struct zlog_mod_info *modinfo, char *content_buf, int error_no)
{
	int i;
	unsigned long irqflags;

	if (!modinfo)
		return -1;

	spin_lock_irqsave(&exception_report_lock, irqflags);
	for (i = 0; i < ZLOG_BUILTIN_RECORDS_MAX_NUMS; i++) {
		if (exception_builtin_records[i].magic == MAGIC_NUM)
			continue;
		exception_builtin_records[i].magic = MAGIC_NUM;
		exception_builtin_records[i].modinfo = modinfo;
		exception_builtin_records[i].error_no = error_no;
		if (content_buf)
			snprintf(exception_builtin_records[i].data, CONTENT_DATA_MAX_LEN, "%s", content_buf);
		else
			exception_builtin_records[i].data[0] = '\0';
		exception_log_available++;
		break;
	}
	spin_unlock_irqrestore(&exception_report_lock, irqflags);
	schedule_delayed_work(&exception_work, msecs_to_jiffies(10));
	return 0;
}

static int get_time_str(char *output)
{
	struct  timespec64 now;
	struct rtc_time tm;

	ktime_get_real_ts64(&now);

	now.tv_sec -= 60*sys_tz.tz_minuteswest;

	/* Calculate the ktime such as YYMMDD in tm */
	rtc_time64_to_tm(now.tv_sec, &tm);

	return snprintf(output, TIME_BUFFER_LEN, "[%04d-%02d-%02d %02d:%02d:%02d]"
	                , tm.tm_year + 1900
	                , tm.tm_mon + 1
	                , tm.tm_mday
	                , tm.tm_hour
	                , tm.tm_min
	                , tm.tm_sec);
}

// return the number of characters printed, including the null byte used to end output to strings)
static int format_msg(char *dstbuf, int len, struct zlog_mod_info *modinfo, char *data_buf, int error_no)
{
	int buff_used = 0;
	int ret_len = 0;

	memset(dstbuf, 0, len);

	ret_len = get_time_str(time_buffer);

	buff_used += snprintf(dstbuf + buff_used, len - buff_used, "MOD_NO: %d; ", modinfo->module_no);

	buff_used += snprintf(dstbuf + buff_used, len - buff_used,
	                      "ENO: 0x%x; ", ZLOG_COMB_MODULE_ID(modinfo->module_no) | error_no);

	if(ret_len > 0) {
		time_buffer[ret_len] = '\0';
		buff_used += snprintf(dstbuf + buff_used, len - buff_used, "TIME: %s; ", time_buffer);
	}

	if (modinfo->name)
		buff_used += snprintf(dstbuf + buff_used, len - buff_used, "CLT_NAME: %s; ", modinfo->name);

	if (modinfo->device_name)
		buff_used += snprintf(dstbuf + buff_used, len - buff_used, "DEV_NAME: %s; ", modinfo->device_name);

	if (modinfo->module_name)
		buff_used += snprintf(dstbuf + buff_used, len - buff_used, "MOD_NAME: %s; ", modinfo->module_name);

	if (modinfo->ic_name)
		buff_used += snprintf(dstbuf + buff_used, len - buff_used, "IC_NAME: %s; ", modinfo->ic_name);

	if (data_buf)
		buff_used += snprintf(dstbuf + buff_used, len - buff_used, "CONTENT: %s", data_buf);

	return buff_used + 1;
}

static int exception_record_format_builtin(char __user *buff, size_t size, int used, struct exception_builtin_records_type *record)
{
	int len = format_msg(log_buffer, sizeof(log_buffer), record->modinfo, record->data, record->error_no);

	if (used + len > size)
		return -EFAULT;

	if (copy_to_user(buff+used, log_buffer, len))
		return -EFAULT;

	return len;
}

static ssize_t exception_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
{
	unsigned long irqflags;
	static int read_index = 0;
	int i = read_index;
	int used = 0;

	if (size < LOG_BUFFER_LEN)
		return -EINVAL;
	do {
		spin_lock_irqsave(&exception_report_lock, irqflags);
		if (exception_builtin_records[i].magic !=  MAGIC_NUM) {
			spin_unlock_irqrestore(&exception_report_lock, irqflags);
		} else {
			int ret;
			spin_unlock_irqrestore(&exception_report_lock, irqflags);
			ret = exception_record_format_builtin(buff, size, used, &(exception_builtin_records[i]));
			if (ret < 0) // when ret < 0, this record is not be send  to userspace, so keep it in array
				break;

			used += ret;
			spin_lock_irqsave(&exception_report_lock, irqflags);
			exception_log_available--;
			exception_builtin_records[i].magic = 0;
			spin_unlock_irqrestore(&exception_report_lock, irqflags);
		}
		i = (i + 1) % ZLOG_BUILTIN_RECORDS_MAX_NUMS;
	} while ( i != read_index);
	read_index = i;
	return used;
}

static __poll_t exception_poll(struct file *file, poll_table *wait)
{
	__poll_t ret = 0;
	unsigned long irqflags;

	spin_lock_irqsave(&exception_report_lock, irqflags);
	if (exception_log_available > 0) {
		ret = EPOLLIN;
		goto out;
	}
	spin_unlock_irqrestore(&exception_report_lock, irqflags);

	poll_wait(file, &exception_wait, wait);

	spin_lock_irqsave(&exception_report_lock, irqflags);
	if (exception_log_available > 0)
		ret = EPOLLIN;
out:
	spin_unlock_irqrestore(&exception_report_lock, irqflags);
	return ret;
}

static const struct file_operations exception_log_proc_ops = {
	.owner = THIS_MODULE,
	.read = exception_read,
	.poll = exception_poll,
};

static int __init exception_builtin_init(void)
{
	struct proc_dir_entry *res;
	res = proc_create("driver/exception_report", 0440, NULL, &exception_log_proc_ops);
	if (!res)
		return -ENOMEM;

	return 0;
}

module_init(exception_builtin_init);
MODULE_LICENSE("GPL");
#endif
