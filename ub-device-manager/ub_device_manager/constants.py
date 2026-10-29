##########################################################################################################
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# ub-device-manager is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
##########################################################################################################

NPU_LIST_CONTEXT_KEY = "NPU_LIST_CONTEXT_KEY"
DELETE_VM_CONTEXT_KEY = "DELETE_VM_CONTEXT_KEY"
CREATE_VM_REQUEST_CONTEXT_KEY = "CREATE_VM_REQUEST_CONTEXT_KEY"
VM_XML_CONTEXT_KEY = "vm_xml"
BIND_REQUEST_CONTEXT_KEY = "BIND_REQUEST_CONTEXT_KEY"    # Input for the binding task
BIND_RESULT_CONTEXT_KEY = "BIND_RESULT_CONTEXT_KEY"      # Binding result
BOUND_DEVICES_CONTEXT_KEY = "BOUND_DEVICES_CONTEXT_KEY"  # Bound devices (with GUID and type) used to build VM XML

UNBIND_REQUEST_CONTEXT_KEY = "UNBIND_REQUEST_CONTEXT_KEY"

# SSU-related: task chain context keys
SSU_LIST_CONTEXT_KEY = "SSU_LIST_CONTEXT_KEY"                          # Storage space list result
SSU_SHOW_REQUEST_CONTEXT_KEY = "SSU_SHOW_REQUEST_CONTEXT_KEY"          # Input for querying a storage space by name
SSU_SHOW_RESULT_CONTEXT_KEY = "SSU_SHOW_RESULT_CONTEXT_KEY"            # Result of querying a storage space by name
SSU_FREE_REQUEST_CONTEXT_KEY = "SSU_FREE_REQUEST_CONTEXT_KEY"          # Input for freeing a storage space
SSU_ALLOC_REQUEST_CONTEXT_KEY = "SSU_ALLOC_REQUEST_CONTEXT_KEY"        # Input for allocating a storage space
SSU_ALLOC_RESULT_CONTEXT_KEY = "SSU_ALLOC_RESULT_CONTEXT_KEY"          # Result of allocating a storage space
SSU_PERM_REQUEST_CONTEXT_KEY = "SSU_PERM_REQUEST_CONTEXT_KEY"          # Input for adding/removing access permissions
SSU_NS_STATUS_REQUEST_CONTEXT_KEY = "SSU_NS_STATUS_REQUEST_CONTEXT_KEY"    # Input for a namespace status query
SSU_NS_STATUS_RESULT_CONTEXT_KEY = "SSU_NS_STATUS_RESULT_CONTEXT_KEY"      # Namespace status result
SSU_NS_CONNECT_INFO_REQUEST_CONTEXT_KEY = "SSU_NS_CONNECT_INFO_REQUEST_CONTEXT_KEY"  # Input for a connection info query
SSU_NS_CONNECT_INFO_RESULT_CONTEXT_KEY = "SSU_NS_CONNECT_INFO_RESULT_CONTEXT_KEY"    # Connection info result
SSU_VFE_LIST_CONTEXT_KEY = "SSU_VFE_LIST_CONTEXT_KEY"                  # SSU-specific VFE list result
SSU_VFE_BIND_REQUEST_CONTEXT_KEY = "SSU_VFE_BIND_REQUEST_CONTEXT_KEY"  # Input for binding a VFE
SSU_VFE_UNBIND_REQUEST_CONTEXT_KEY = "SSU_VFE_UNBIND_REQUEST_CONTEXT_KEY"  # Input for unbinding a VFE

# SSU attach/detach-related: task chain context keys
SSU_SPACE_ATTACH_REQUEST_CONTEXT_KEY = "SSU_SPACE_ATTACH_REQUEST_CONTEXT_KEY"    # Input for a regular attach
SSU_SPACE_ATTACH_RESULT_CONTEXT_KEY = "SSU_SPACE_ATTACH_RESULT_CONTEXT_KEY"      # Result of a regular attach
SSU_SPACE_DETACH_REQUEST_CONTEXT_KEY = "SSU_SPACE_DETACH_REQUEST_CONTEXT_KEY"    # Input for a regular detach

GB_TO_B = 1024 * 1024 * 1024