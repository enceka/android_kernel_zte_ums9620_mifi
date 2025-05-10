#ifndef __SQC_PLATFORM_H__
#define __SQC_PLATFORM_H__

enum sqc_boot_mode {
	SQC_BOOT_NORMAL = 0,
	SQC_BOOT_CHARGE = 1,

};

int sqc_get_boot_mode(void);

#endif
