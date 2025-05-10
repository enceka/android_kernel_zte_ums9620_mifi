#ifndef CTS_OEM_H
#define CTS_OEM_H

/* zte_add */
#define MAX_FILE_NAME_LEN       64
#define MAX_NAME_LEN_20  20

struct chipone_ts_data;

extern int cts_oem_init(struct chipone_ts_data *cts_data);
extern int cts_oem_deinit(struct chipone_ts_data *cts_data);

#endif /* CTS_VENDOR_H */

