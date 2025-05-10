mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
mkfile_dir := $(dir $(mkfile_path))

PRODUCT_COPY_FILES += \
    -I$(mkfile_dir)/config/aw9620x/aw96205/aw9620x_reg_0.bin:vendor/firmware/aw9620x_0.bin \
    -I$(mkfile_dir)/config/aw9620x/aw96205/aw9620x_reg_1.bin:vendor/firmware/aw9620x_1.bin

obj-$(CONFIG_SKEY_SENSOR_AW9620X) += zte_skey.o
zte_skey-objs := ./comm/aw_sar_comm_interface.o aw_sar.o ./aw9610x/aw9610x.o ./aw9620x/aw9620x.o ./aw9620x/aw_update_fw.o ./aw963xx/aw963xx.o