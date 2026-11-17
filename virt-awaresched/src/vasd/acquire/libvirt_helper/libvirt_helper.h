/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * VSched is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef LIBVIRT_HELPER_H
#define LIBVIRT_HELPER_H

#include <map>
#include <mutex>

#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

#include "cluster_vm.h"
#include "def.h"
#include "dynamic_bitset.h"
#include "error.h"
#include "proc_helper.h"

namespace vas::sched::acquire {
using namespace vas::common;

using virDomainEventDetailType = uint8_t;

const fs::path vmCgroupPrefix = "/sys/fs/cgroup/cpuset/machine.slice";
constexpr size_t VM_UUID_LEN = 37; // 36 (UUID length) + 1 (\0)
enum class VmEventType
{
    UNKNOWN = 1,
    START,
    SHUTDOWN,
};

struct VmEventInfo {
    VmEventInfo() = default;

    virDomainEventType type{};
    virDomainEventDetailType detailType{};
    VmEventType eventType = VmEventType::UNKNOWN;
    VmInfo vmInfo{};
};

const std::map<std::pair<virDomainEventType, virDomainEventDetailType>, VmEventType> VM_EVENT_MAP = {
    {{virDomainEventType::VIR_DOMAIN_EVENT_STARTED,
      static_cast<virDomainEventDetailType>(virDomainEventStartedDetailType::VIR_DOMAIN_EVENT_STARTED_BOOTED)},
     VmEventType::START},
    {{virDomainEventType::VIR_DOMAIN_EVENT_STOPPED,
      static_cast<virDomainEventDetailType>(virDomainEventStoppedDetailType::VIR_DOMAIN_EVENT_STOPPED_DESTROYED)},
     VmEventType::SHUTDOWN},
    {{virDomainEventType::VIR_DOMAIN_EVENT_STOPPED,
      static_cast<virDomainEventDetailType>(virDomainEventStoppedDetailType::VIR_DOMAIN_EVENT_STOPPED_SHUTDOWN)},
     VmEventType::SHUTDOWN},
    {{virDomainEventType::VIR_DOMAIN_EVENT_SHUTDOWN,
      static_cast<virDomainEventDetailType>(virDomainEventShutdownDetailType::VIR_DOMAIN_EVENT_SHUTDOWN_HOST)},
     VmEventType::SHUTDOWN},
};

class LibvirtHelper {
public:
    static LibvirtHelper &GetInstance()
    {
        static LibvirtHelper instance;
        return instance;
    }
    static VasRet GetVmInfo(virDomainPtr domain, VmInfo &vmInfo);
    static VasRet GetVmID(virDomainPtr domain, uint16_t &id);
    static VasRet GetVmUUID(virDomainPtr domain, std::string &uuid);
    static void FreeDomain(virDomainPtr domain);
    static void GetCache(VmInfoMap &vmInfoMap);
    static void FlushVmPidInfo(Vm2VcpuMap &vmVcpuPidList, Uuid2PidMap &uuid2PidMap, VmInfo &vmInfo);
    static void AddVmInfoToCache(const VmInfo &vmInfo);
    static void DelVmInfoToCache(const std::string &uuid);

    VasRet Init();
    void DeInit();
    VasRet GetVmInfoList(VmInfoMap &vmInfoMap);
    VasRet GetDomainConnByUUID(const std::string &uuid, virDomainPtr &domainConn) const;
    VasRet RunEventDefaultImpl(const virConnectDomainEventCallback &EventCallback);

private:
    static constexpr uint16_t retryInterval = 5 * MSECS_PER_SEC; // The interval for timed reconnection is 5 seconds.
    static constexpr uint32_t MAX_VCPU_NUM = 4096;
    static std::mutex dataMutex_;
    static VmInfoMap vmInfoMapCache;

    static void UpdateCache(const VmInfoMap &vmInfoMap);
    static void GetLastError();
    static void FreeDomains(virDomainPtr *domains, const size_t &domainNums);
    static VasRet GetVmName(virDomainPtr domain, std::string &name);
    static VasRet GetVmVcpuMap(virDomainPtr domain, VmInfo &vmInfo);
    static VasRet GetDomainInfo(virDomainPtr domain, virDomainInfo &virDomainInfo);
    static VasRet GetVmVcpuInfo(virDomainPtr domain, const int &nrVcpu, std::map<uint16_t, DynamicBitset> &vcpuMaps);
    static std::set<uint16_t> GetCpuInNumaRange(const std::set<uint16_t> &cpuSet);
    static void FlushVmsPidInfo(VmInfoMap &vmInfoMap);
    static bool IsVcpuPinNuma(const std::set<uint16_t> &cpuSet);
    static bool IsReschedSkippedDomain(virDomainPtr domain);
    static VasRet RegisterEventDefaultImpl();

    VasRet Connect();
    void CloseConn();
    VasRet Reconnect();
    bool IsConnectAlive() const;
    VasRet CheckWithReconnect();
    VasRet GetDomainList(virDomainPtr *&domains, int &numDomains) const;
    VasRet RegisterDomainEvent(const virConnectDomainEventCallback &EventCallback) const;
    VasRet ConnectLocked();
    void CloseConnLocked();

    mutable std::mutex virConnectMutex_;
    virConnectPtr virConnect{};
};
} // namespace vas::sched::acquire

#endif // LIBVIRT_HELPER_H
