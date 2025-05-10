
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "omnivision_tcm_testing.h"

#define STRTOL_LEN 10

static void copy_this_line(char *dest, char *src)
{
	char *copy_from;
	char *copy_to;

	copy_from = src;
	copy_to = dest;
	do {
		*copy_to = *copy_from;
		copy_from++;
		copy_to++;
	} while((*copy_from != '\n') && (*copy_from != '\r') && (*copy_from != '\0'));
	*copy_to = '\0';
}

static void goto_next_line(char **ptr)
{
	do {
		*ptr = *ptr + 1;
	} while (**ptr != '\n' && **ptr != '\0');
	if (**ptr == '\0') {
		return;
	}
	*ptr = *ptr + 1;
}

static void parse_valid_data(char *buf_start, loff_t buf_size,
				char *ptr, int32_t* data, int rows)
{
	int i = 0;
	int j = 0;
	char *token = NULL;
	char *tok_ptr = NULL;
	char row_data[512] = {0};

	if(!ptr) {
		ovt_info(INFO_LOG, "ovt tcm csv parser:  %s, ptr is NULL\n", __func__);
		return;
	}
	if (!data) {
		ovt_info(INFO_LOG, "ovt tcm csv parser:  %s, data is NULL\n", __func__);
		return;
	}

	for (i = 0; i < rows; i++) {
		// copy this line to row_data buffer
		memset(row_data, 0, sizeof(row_data));
		copy_this_line(row_data, ptr);
		tok_ptr = row_data;
		while ((token = strsep(&tok_ptr,", \t\n\r\0"))) {
			if (strlen(token) == 0)
				continue;

			data[j] = (int32_t)simple_strtol(token, NULL, STRTOL_LEN);
			j ++;
		}
		goto_next_line(&ptr);				//next row
		if(!ptr || (0 == strlen(ptr))) {
			ovt_info(INFO_LOG, "ovt tcm csv parser: invalid ptr, return\n");
			break;
		}
	}

	return;
}

static void print_data(char* target_name, int32_t* data, int rows, int columns)
{
	int i,j;

    ovt_info(INFO_LOG, "ovt tcm csv parser:  print data %s\n", target_name);
	if(NULL == data) {
		ovt_info(INFO_LOG, "ovt tcm csv parser:  rawdata is NULL\n");
		return;
	}

	for (i = 0; i < rows; i++) {
		pr_cont("ovt_info[%2d]", (i + 1));
		for (j = 0; j < columns; j++) {
			pr_cont("%5d,", data[i*columns + j]);
		}
		pr_cont("\n");
	}

	return;
}

int ovt_tcm_parse_csvfile(struct ovt_tcm_hcd *tcm_hcd, char *target_name, int32_t  *data, int rows, int columns)
{

	int ret = 0;
	char *buf = NULL;
	char *ptr = NULL;	
	const struct firmware *fw = NULL;
	char fwname[50] = { 0 };

	if(target_name == NULL) {
		ovt_info(ERR_LOG, "ovt tcm csv parser:  target path pointer is NULL\n");
		return -EPERM;
	}
	get_ovt_tcm_module_info_from_lcd();
	snprintf(fwname, sizeof(fwname), "%s%s.csv", OVT_TCM_CSV_NAME, ovt_tcm_vendor_name);
	ret = request_firmware(&fw, fwname, tcm_hcd->pdev->dev.parent);
	if (ret == 0) {
		ovt_info(INFO_LOG,"firmware request success");
		buf = (char *)kzalloc(fw->size + 1, GFP_KERNEL);
		ovt_info(INFO_LOG,"firmware size is:%d", fw->size);
		if (!buf) {
			ovt_info(ERR_LOG,"buffer kzalloc fail");
			release_firmware(fw);
			return -EPERM;
		}
		memcpy(buf, fw->data, fw->size);
		release_firmware(fw);
	} else {
		ovt_info(ERR_LOG,"firmware request fail");
		return -EPERM;
	}
	if (fw->size > 0) {
		//buf[fw->size] = '\0';
		ptr = buf;
		ptr = strstr(ptr, target_name);
		if (ptr == NULL) {
			ovt_info(INFO_LOG, "ovt tcm csv parser:  %s: load %s failed 1!\n", __func__,target_name);
			ret = -EINTR;
			goto exit_free;
		}

	    /* walk thru this line */
		goto_next_line(&ptr);
		if ((NULL == ptr) || (0 == strlen(ptr))) {
			ovt_info(INFO_LOG, "ovt tcm csv parser:  %s: load %s failed 2!\n", __func__,target_name);
			ret = -EIO;
			goto exit_free;
		}

		/*  analyze the data */
		if (data) {
			parse_valid_data(buf, fw->size, ptr, data, rows);
			print_data(target_name, data,  rows, columns);
		} else {
			ovt_info(INFO_LOG, "ovt tcm csv parser:  %s: load %s failed 3!\n", __func__,target_name);
			ret = -EINTR;
			goto exit_free;
		}
	}	else {
		ovt_info(INFO_LOG, "ovt tcm csv parser:  %s: ret=%d,fw->size=%d\n", __func__, ret, fw->size);
		ret = -ENXIO;
		goto exit_free;
	}
	ret = 0;
exit_free:
	ovt_info(INFO_LOG, "ovt tcm csv parser: %s exit free\n", __func__);
	if(buf) {
		ovt_info(INFO_LOG, "ovt tcm csv parser: kfree buf\n");
		kfree(buf);
		buf = NULL;
	}
	return ret;
}

void ovt_tcm_store_to_file(struct file *fp, char* format, ...)
{
#if 0
    va_list args;
    char buf[TMP_STRING_LEN_FOR_CSV] = {0};
    // mm_segment_t fs;  
    loff_t pos;
	// struct file *fp;

    va_start(args, format);
    vsnprintf(buf, TMP_STRING_LEN_FOR_CSV, format, args);
    va_end(args);


    // fp = filp_open(file_path, O_RDWR | O_CREAT, 0666);  
    // if (IS_ERR(fp)) {  
    //     ovt_info(INFO_LOG, "ovt tcm create file error\n");  
    //     return;  
    // } 

    // fs = get_fs();  
    // set_fs(KERNEL_DS);


	buf[TMP_STRING_LEN_FOR_CSV - 1] = 0;

	pos = fp->f_pos;
    vfs_write(fp, buf, strlen(buf), &pos);  
    fp->f_pos = pos;

    // set_fs(fs);

    // filp_close(fp, NULL);  
    return;  
#endif
}