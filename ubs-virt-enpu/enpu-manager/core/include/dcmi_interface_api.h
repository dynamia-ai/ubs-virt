/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * enpu-manager is licensed under Mulan PSL v2.
 */

#ifndef __DCMI_INTERFACE_API_H__
#define __DCMI_INTERFACE_API_H__

#if defined(__cplusplus)
extern "C" {
#endif

struct dcmi_proc_mem_info {
    int proc_id;
    unsigned long proc_mem_usage;
};

#define DCMI_OK 0
#define DCMI_ERROR_CODE_BASE (-8000)
#define DCMI_ERR_CODE_INNER_ERR (DCMI_ERROR_CODE_BASE - 5)

int dcmi_init(void);

int dcmi_get_card_id_device_id_from_logicid(int *card_id, int *device_id, unsigned int device_logic_id);

int dcmi_get_device_resource_info(int card_id, int device_id, struct dcmi_proc_mem_info *proc_info, int *proc_num);

int dcmi_get_device_utilization_rate(int card_id, int device_id, int input_type, unsigned int *utilization_rate);

int dcmi_get_card_id_device_id_from_phyid(int *card_id, int *device_id, unsigned int device_phy_id);

int __attribute__((weak)) dcmiv2_init(void);

int __attribute__((weak))
dcmiv2_get_device_utilization_rate(int dev_id, int input_type, unsigned int *utilization_rate);

int __attribute__((weak))
dcmiv2_get_device_proc_mem_info(int dev_id, struct dcmi_proc_mem_info *proc_info, int *proc_num);

int __attribute__((weak)) dcmiv2_get_device_list(int *device_list, int *device_count, int list_len);

enum dcmi_die_type
{
    NDIE,
    VDIE,
    DDIE,
    INVALID_DIE,
};

#define DIE_ID_COUNT 5

struct dcmi_die_id {
    unsigned int soc_die[DIE_ID_COUNT];
};

int dcmi_get_device_die_v2(int card_id, int device_id, enum dcmi_die_type input_type, struct dcmi_die_id *die_id);

int __attribute__((weak))
dcmiv2_get_device_die_id(int dev_id, enum dcmi_die_type input_type, struct dcmi_die_id *die_id);

int dcmi_get_all_device_count(int *all_device_count);

int __attribute__((weak)) dcmiv2_get_all_device_count(int *all_device_count);

int dcmi_get_device_logic_id(int *device_logic_id, int card_id, int device_id);

struct dcmi_hbm_info {
    unsigned long long memory_size; /* 总 HBM，字节 */
    unsigned int freq;
    unsigned long long memory_usage; /* 已用，字节 */
    int temp;
    unsigned int bandwidth_util_rate;
};

int dcmi_get_device_hbm_info(int card_id, int device_id, struct dcmi_hbm_info *hbm_info);

int __attribute__((weak)) dcmiv2_get_device_hbm_info(int dev_id, struct dcmi_hbm_info *hbm_info);

#define MAX_CHIP_NAME_LEN 32

struct dcmi_chip_info {
    unsigned char chip_type[MAX_CHIP_NAME_LEN];
    unsigned char chip_name[MAX_CHIP_NAME_LEN];
    unsigned char chip_ver[MAX_CHIP_NAME_LEN];
    unsigned int aicore_cnt;
};

struct dcmi_chip_info_v2 {
    unsigned char chip_type[MAX_CHIP_NAME_LEN];
    unsigned char chip_name[MAX_CHIP_NAME_LEN];
    unsigned char chip_ver[MAX_CHIP_NAME_LEN];
    unsigned int aicore_cnt;
    unsigned char npu_name[MAX_CHIP_NAME_LEN];
};

int dcmi_get_device_chip_info(int card_id, int device_id, struct dcmi_chip_info *chip_info);

int dcmi_get_device_chip_info_v2(int card_id, int device_id, struct dcmi_chip_info_v2 *chip_info);

int __attribute__((weak)) dcmiv2_get_device_chip_info(int dev_id, struct dcmi_chip_info_v2 *chip_info);

#if defined(__cplusplus)
}
#endif

#endif /* __DCMI_INTERFACE_API_H__ */