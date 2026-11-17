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

#include "libvirt_helper.h"

#include <cstring>
#include <thread>

#include "conf.h"
#include "cpu_helper.h"
#include "def.h"
#include "error.h"
#include "logger.h"
#include "vasd_arg_parse.h"

namespace vas::sched::acquire {
std::mutex LibvirtHelper::dataMutex_{};
VmInfoMap LibvirtHelper::vmInfoMapCache{};

/**
 * Initialization
 * @return VasRet
 */
VasRet LibvirtHelper::Init()
{
    auto ret = RegisterEventDefaultImpl();
    if (isVasRetFail(ret)) {
        return VAS_ERROR;
    }

    return Connect();
}

/**
 * DeInitialization
 */
void LibvirtHelper::DeInit()
{
    CloseConn();
}

/**
 * Collect full vm information for this node
 * @param vmInfoMap [OUT] vm information list
 * @return VasRet
 */
VasRet LibvirtHelper::GetVmInfoList(VmInfoMap &vmInfoMap)
{
    if (isVasRetFail(CheckWithReconnect())) {
        return VAS_ERROR;
    }

    // Retrieve the vm list
    virDomainPtr *domains = nullptr;
    int numDomains = 0;
    auto ret = GetDomainList(domains, numDomains);
    if (isVasRetFail(ret)) {
        return ret;
    }

    VmInfoMap skipVmInfoMap{};
    for (int i = 0; i < numDomains; ++i) {
        VmInfo vmInfo{};
        ret = GetVmInfo(domains[i], vmInfo);
        if (ret == VAS_ERROR_NULLPTR) {
            break;
        } else if (ret == VAS_WARN) {
            skipVmInfoMap[vmInfo.uuid] = vmInfo;
            LOG_DEBUG("Get skip domain. uuid=" + vmInfo.uuid + ", name=" + vmInfo.name);
        } else if (isVasRetFail(ret)) {
            LOG_WARN("Get domain info failed. uuid=" + vmInfo.uuid + ", name=" + vmInfo.name);
        } else {
            vmInfoMap[vmInfo.uuid] = vmInfo;
            LOG_DEBUG("Get domain info success. uuid=" + vmInfo.uuid + ", name=" + vmInfo.name);
        }
    }
    // free domains
    FreeDomains(domains, numDomains);
    // flush pid info
    FlushVmsPidInfo(vmInfoMap);
    skipVmInfoMap.insert(vmInfoMap.begin(), vmInfoMap.end());
    if (!skipVmInfoMap.empty()) {
        LOG_DEBUG("Get " + std::to_string(skipVmInfoMap.size()) + " vm. " + std::to_string(vmInfoMap.size()) +
                  " vms need sched.");
    }
    UpdateCache(skipVmInfoMap);
    return VAS_OK;
}

/**
 * Obtain the connection pointer of the vm through UUID.
 * @param uuid domain uuid
 * @param domainConn domain connect
 * @return VasRet
 */
VasRet LibvirtHelper::GetDomainConnByUUID(const std::string &uuid, virDomainPtr &domainConn) const
{
    std::lock_guard<std::mutex> lock(virConnectMutex_);
    if (!virConnect) {
        LOG_ERROR("Libvirt connect is not ready. uuid=" + uuid);
        return VAS_ERROR;
    }
    domainConn = virDomainLookupByUUIDString(virConnect, uuid.c_str());
    if (!domainConn) {
        LOG_ERROR("Get domain connect by uuid failed. uuid=" + uuid);
        return VAS_ERROR;
    }
    return VAS_OK;
}

/**
 * Get vm information for one domain
 * @param domain vm domain
 * @param vmInfo vm information
 * @return VasRet
 */
VasRet LibvirtHelper::GetVmInfo(virDomainPtr domain, VmInfo &vmInfo)
{
    auto ret = GetVmName(domain, vmInfo.name);
    if (isVasRetFail(ret)) {
        LOG_ERROR("Get domain name failed.");
        return ret;
    }
    ret = GetVmUUID(domain, vmInfo.uuid);
    if (isVasRetFail(ret)) {
        LOG_ERROR("Get domain uuid failed. name=" + vmInfo.name);
        return ret;
    }
    ret = GetVmVcpuMap(domain, vmInfo);
    if (isVasRetFail(ret)) {
        LOG_ERROR("Get domain vcpu info failed. name=" + vmInfo.name + ". uuid=" + vmInfo.uuid);
        return ret;
    }
    if (IsReschedSkippedDomain(domain)) {
        LOG_WARN("Skip domain, name=" + vmInfo.name + ", uuid=" + vmInfo.uuid);
        return VAS_WARN;
    }
    LOG_DEBUG("Get domain info end. name=" + vmInfo.name + ". uuid=" + vmInfo.uuid);
    return ret;
}

/**
 * Monitor VM Life Cycle Events
 * @param EventCallback callback function
 * @return VasRet
 */
VasRet LibvirtHelper::RunEventDefaultImpl(const virConnectDomainEventCallback &EventCallback)
{
    if (const auto ret = RegisterDomainEvent(EventCallback); isVasRetFail(ret)) {
        return ret;
    }
    while (!Conf::exitFlag.load()) {
        if (!IsConnectAlive()) {
            LOG_WARN("Libvirt connect is not alive, try to Reconnect.");
            if (isVasRetFail(Reconnect())) {
                LOG_WARN("Reconnect libvirt failed. Please check environment. Wait for reconnect...");
                std::this_thread::sleep_for(std::chrono::milliseconds(retryInterval));
                continue;
            }
            if (const auto ret = RegisterDomainEvent(EventCallback); isVasRetFail(ret)) {
                return ret;
            }
        }
        if (virEventRunDefaultImpl() < 0) {
            LOG_ERROR("Failed to run event loop");
            std::this_thread::sleep_for(std::chrono::milliseconds(retryInterval));
        }
    }
    LOG_INFO("Exit flag is true. virEventRunDefaultImpl loop exit.");
    return VAS_OK;
}

/**
 * Register VM Event Listener
 * @param EventCallback callback function
 * @return VasRet
 */
VasRet LibvirtHelper::RegisterDomainEvent(const virConnectDomainEventCallback &EventCallback) const
{
    std::lock_guard<std::mutex> lock(virConnectMutex_);
    if (!virConnect) {
        LOG_ERROR("Libvirt connect is not ready, domain event registry failed.");
        return VAS_ERROR;
    }
    void *opaque = nullptr;
    if (const int virRet = virConnectDomainEventRegister(virConnect, EventCallback, opaque, nullptr);
        isIntInvalid(virRet)) {
        LOG_ERROR("Domain event registry failed.");
        GetLastError();
        return VAS_ERROR;
    }

    return VAS_OK;
}

void LibvirtHelper::UpdateCache(const VmInfoMap &vmInfoMap)
{
    std::unique_lock lock(dataMutex_);
    for (auto &[uuid, vmInfo] : vmInfoMap) {
        if (vmInfoMapCache.find(uuid) == vmInfoMapCache.end()) {
            vmInfoMapCache[uuid] = vmInfo;
        }
    }
    for (auto it = vmInfoMapCache.begin(); it != vmInfoMapCache.end();) {
        if (vmInfoMap.find(it->first) == vmInfoMap.end()) {
            it = vmInfoMapCache.erase(it);
        } else {
            ++it;
        }
    }
}

/**
 * Retrieve the last error message.
 */
void LibvirtHelper::GetLastError()
{
    const auto virError = virGetLastError();
    if (!virError || !virError->message) {
        return;
    }
    LOG_ERROR("Last libvirt error code: " + std::to_string(virError->code) +
              ", domain: " + std::to_string(virError->domain) + ", msg: " + std::string(virError->message));
}

/**
 * Free libvirt domain
 * @param domain pointer of domain
 */
void LibvirtHelper::FreeDomain(virDomainPtr domain)
{
    if (!domain) {
        LOG_ERROR("Domains ptr is already empty.");
        return;
    }
    virDomainFree(domain);
}

void LibvirtHelper::GetCache(VmInfoMap &vmInfoMap)
{
    std::unique_lock lock(dataMutex_);
    vmInfoMap = vmInfoMapCache;
}

/**
 * Refreshes PIDs of VM threads.
 * @param vmVcpuPidList
 * @param uuid2PidMap
 * @param vmInfo
 */
void LibvirtHelper::FlushVmPidInfo(Vm2VcpuMap &vmVcpuPidList, Uuid2PidMap &uuid2PidMap, VmInfo &vmInfo)
{
    // check if the UUID exists in the uuid2PidMap
    if (uuid2PidMap.find(vmInfo.uuid) == uuid2PidMap.end()) {
        LOG_ERROR("UUID " + vmInfo.uuid + " not found in uuid2PidMap");
        return;
    }
    // flush tgid
    vmInfo.tgid = uuid2PidMap[vmInfo.uuid];
    // flush vcpu pid
    for (auto &[vcpuId, vcpuInfo] : vmInfo.vcpuMap) {
        vcpuInfo.pid = vmVcpuPidList[vmInfo.uuid][vcpuId];
    }
}

void LibvirtHelper::AddVmInfoToCache(const VmInfo &vmInfo)
{
    std::unique_lock lock(dataMutex_);
    vmInfoMapCache[vmInfo.uuid] = vmInfo;
}

void LibvirtHelper::DelVmInfoToCache(const std::string &uuid)
{
    std::unique_lock lock(dataMutex_);
    vmInfoMapCache.erase(uuid);
}

/**
 * free domain pointers
 * @param domains array pointer of domain pointers
 * @param domainNums domain numbers
 */
void LibvirtHelper::FreeDomains(virDomainPtr *domains, const size_t &domainNums)
{
    if (!domains) {
        LOG_ERROR("Domains ptr is already empty.");
        return;
    }
    for (size_t i = 0; i < domainNums; i++) {
        if (domains[i]) {
            virDomainFree(domains[i]);
            domains[i] = nullptr;
        }
    }
    free(domains);
}

/**
 * Get domain name
 * @param domain domain pointer
 * @param name vm name
 * @return VasRet
 */
VasRet LibvirtHelper::GetVmName(virDomainPtr domain, std::string &name)
{
    const auto c = virDomainGetName(domain);
    if (!c) {
        LOG_ERROR("Get vm name failed.");
        GetLastError();
        return VAS_ERROR;
    }
    name = std::string(c);
    return VAS_OK;
}

/**
 * Get domain uuid
 * @param domain domain pointer
 * @param uuid vm uuid
 * @return VasRet
 */
VasRet LibvirtHelper::GetVmUUID(virDomainPtr domain, std::string &uuid)
{
    char c[VM_UUID_LEN];
    if (const auto virRet = virDomainGetUUIDString(domain, c); virRet != 0) {
        LOG_ERROR("Get vm uuid failed.");
        GetLastError();
        return VAS_ERROR;
    }
    uuid = std::string(c);
    return VAS_OK;
}

/**
 * Is vm domain need to be skipped during cpu re-scheduling
 * @param domain vm domain
 * @return bool
 */
bool LibvirtHelper::IsReschedSkippedDomain(virDomainPtr domain)
{
    virDomainInfo virDomainInfos{};
    // Get vcpu count
    auto ret = GetDomainInfo(domain, virDomainInfos);
    if (isVasRetFail(ret)) {
        return true;
    }
    std::map<uint16_t, DynamicBitset> vcpuMaps;
    // Get vcpu cpuMap
    ret = GetVmVcpuInfo(domain, virDomainInfos.nrVirtCpu, vcpuMaps);
    if (isVasRetFail(ret)) {
        return true;
    }
    // Filter vcpu bind by 1:1
    for (auto &[vcpuId, cpuMap] : vcpuMaps) {
        auto locationSet = Bitset::GetDynamicBitsetAreaSet(cpuMap);
        if (locationSet.size() == 1) {
            LOG_DEBUG("vCPU are bound in one-to-one mode.");
            return true;
        }
        // Filter vcpu bind by numa
        if (!VasdArgParse::IsRangeAffinity() && !IsVcpuPinNuma(locationSet)) {
            LOG_DEBUG("The vCPU range spans NUMA.");
            return true;
        }
    }
    return false;
}

/**
 * Get vm id
 * @param domain vm domain
 * @param id vm id
 * @return VasRet
 */
VasRet LibvirtHelper::GetVmID(virDomainPtr domain, uint16_t &id)
{
    const auto ret = virDomainGetID(domain);
    if (isIntInvalid(static_cast<int>(ret))) {
        LOG_ERROR("Get VM ID failed.");
        return VAS_ERROR;
    }
    id = ret;
    return VAS_OK;
}

/**
 * Filter vcpu bind by 1:1. Get vcpu bind by range
 * @param domain vm domain
 * @param vmInfo vm information
 * @return VasRet
 */
VasRet LibvirtHelper::GetVmVcpuMap(virDomainPtr domain, VmInfo &vmInfo)
{
    virDomainInfo virDomainInfos{};
    // Get vcpu count
    auto ret = GetDomainInfo(domain, virDomainInfos);
    if (isVasRetFail(ret)) {
        return ret;
    }
    std::map<uint16_t, DynamicBitset> vcpuMaps;
    // Get vcpu cpuMap
    ret = GetVmVcpuInfo(domain, virDomainInfos.nrVirtCpu, vcpuMaps);
    if (isVasRetFail(ret)) {
        return ret;
    }

    for (auto &[vcpuId, cpuMap] : vcpuMaps) {
        auto locationSet = Bitset::GetDynamicBitsetAreaSet(cpuMap);
        auto numaSet = GetCpuInNumaRange(locationSet);
        if (numaSet.empty()) {
            LOG_ERROR("No numaId found for vcpu, vcpuId=" + std::to_string(vcpuId));
            return VAS_ERROR;
        }
        if (vmInfo.vcpuMap.find(vcpuId) == vmInfo.vcpuMap.end()) {
            vmInfo.vcpuMap[vcpuId] = VcpuInfo{
                .pid = 0,
                .numaSet = numaSet,
                .cpuMaps = cpuMap,
            };
        } else {
            vmInfo.vcpuMap[vcpuId].numaSet = numaSet;
            vmInfo.vcpuMap[vcpuId].cpuMaps = cpuMap;
        }
    }
    return VAS_OK;
}

/**
 * Get domain basic info
 * @param domain vm domain
 * @param virDomainInfo virt domain info
 * @return VasRet
 */
VasRet LibvirtHelper::GetDomainInfo(virDomainPtr domain, virDomainInfo &virDomainInfos)
{
    if (const auto ret = virDomainGetInfo(domain, &virDomainInfos); isIntInvalid(ret)) {
        LOG_ERROR("Get domain info failed.");
        return VAS_ERROR;
    }
    return VAS_OK;
}

/**
 * Get domain vcpu-cpuMaps map
 * @param domain vm domain
 * @param nrVcpu vcpu number
 * @param vcpuMaps vcpu bitset map
 * @return VasRet
 */
VasRet LibvirtHelper::GetVmVcpuInfo(virDomainPtr domain, const int &nrVcpu, std::map<uint16_t, DynamicBitset> &vcpuMaps)
{
    if (nrVcpu <= 0 || static_cast<uint32_t>(nrVcpu) > MAX_VCPU_NUM) {
        LOG_WARN("Invalid vcpu number. nrVcpu=" + std::to_string(nrVcpu));
        return VAS_ERROR;
    }
    std::vector<virVcpuInfo> virVcpuInfos(nrVcpu);
    uint16_t cpuMapLen = (CpuHelper::MAX_CPU_NUM + 7) / BYTE;
    std::vector<unsigned char> cpuMaps(static_cast<size_t>(nrVcpu) * cpuMapLen);

    // Get vcpu list
    int vcpuCount = virDomainGetVcpus(domain, virVcpuInfos.data(), nrVcpu, cpuMaps.data(), cpuMapLen);
    if (isIntInvalid(vcpuCount) || vcpuCount != nrVcpu) {
        LOG_WARN("Get vcpus failed, vcpuCount=" + std::to_string(vcpuCount) + ", nrVcpu=" + std::to_string(nrVcpu));
        return VAS_ERROR;
    }
    cpuMaps = std::vector<unsigned char>(static_cast<size_t>(nrVcpu) * cpuMapLen);
    // Get vcpu pin info
    vcpuCount =
        virDomainGetVcpuPinInfo(domain, nrVcpu, cpuMaps.data(), cpuMapLen,
                                static_cast<unsigned int>(virDomainModificationImpact::VIR_DOMAIN_AFFECT_CURRENT));
    if (isIntInvalid(vcpuCount) || vcpuCount != nrVcpu) {
        LOG_WARN("Get vcpus pin failed, vcpuCount=" + std::to_string(vcpuCount) + ", nrVcpu=" + std::to_string(nrVcpu));
        return VAS_ERROR;
    }
    for (size_t i = 0; i < nrVcpu; ++i) {
        DynamicBitset dynamicBitset{};
        Bitset::CpuMaskToDynamicBitset(&cpuMaps[i * cpuMapLen], cpuMapLen, dynamicBitset);
        vcpuMaps[virVcpuInfos[i].number] = dynamicBitset;
    }
    return VAS_OK;
}

/**
 * Get numa set which vcpu in.
 * @param cpuSet
 * @return std::set<uint16_t> cpuIndex set
 */
std::set<uint16_t> LibvirtHelper::GetCpuInNumaRange(const std::set<uint16_t> &cpuSet)
{
    auto cpu2NumaIdMap = CpuHelper::GetInstance().GetCpu2NumaIdMap();
    std::set<uint16_t> numaSet;
    for (auto &cpu : cpuSet) {
        if (cpu2NumaIdMap.find(cpu) != cpu2NumaIdMap.end()) {
            numaSet.emplace(cpu2NumaIdMap[cpu]);
        }
    }
    return numaSet;
}

void LibvirtHelper::FlushVmsPidInfo(VmInfoMap &vmInfoMap)
{
    // get uuids info
    std::vector<std::string> uuids;
    uuids.reserve(vmInfoMap.size());
    for (const auto &[uuid, vmInfo] : vmInfoMap) {
        uuids.emplace_back(uuid);
    }
    // get vm process info
    auto vmVcpuPidList = ProcHelper::GetInstance().GetVmProcList(uuids);
    auto uuid2PidMap = ProcHelper::GetInstance().GetUuid2PidMap();
    // refresh the PID information in the vm details
    for (auto &[uuid, vmInfo] : vmInfoMap) {
        FlushVmPidInfo(vmVcpuPidList, uuid2PidMap, vmInfo);
    }
}

/**
 * Is cpu range of vcpu pin the numa cpu range
 * @param cpuSet cpu range of vcpu
 * @return bool
 */
bool LibvirtHelper::IsVcpuPinNuma(const std::set<uint16_t> &cpuSet)
{
    auto cpu2NumaIdMap = CpuHelper::GetInstance().GetCpu2NumaIdMap();
    auto numaCpusetMap = CpuHelper::GetInstance().GetNuma2CpusetMap();
    if (cpuSet.empty() || cpu2NumaIdMap.find(*cpuSet.begin()) == cpu2NumaIdMap.end()) {
        return false;
    }
    if (const uint16_t numaId = cpu2NumaIdMap[*cpuSet.begin()]; cpuSet != numaCpusetMap[numaId]) {
        return false;
    }
    return true;
}

/**
 * Register the default event implementation.
 * Note! The event implementation must be registered before the connection is opened.
 * @return VasRet
 */
VasRet LibvirtHelper::RegisterEventDefaultImpl()
{
    if (const auto virRet = virEventRegisterDefaultImpl(); virRet != 0) {
        LOG_ERROR("Registers default event implementation failed. " + std::to_string(virRet));
        return VAS_ERROR;
    }

    return VAS_OK;
}

/**
 * Connect libvirt
 * @return VasRet
 */
VasRet LibvirtHelper::Connect()
{
    std::lock_guard<std::mutex> lock(virConnectMutex_);
    return ConnectLocked();
}

VasRet LibvirtHelper::ConnectLocked()
{
    try {
        LOG_INFO("Start to get libvirt connect");
        virConnect = virConnectOpen("qemu:///system");
        if (!virConnect) {
            LOG_ERROR("Libvirt conn failed, please check the virsh environment. " + std::to_string(errno));
            return VAS_ERROR;
        }
        return VAS_OK;
    } catch (const std::exception &e) {
        LOG_ERROR("Connect libvirt failed. err: " + std::string(e.what()));
        return VAS_ERROR;
    }
}

/**
 * Close libvirt connection
 * @return VasRet
 */
void LibvirtHelper::CloseConn()
{
    std::lock_guard<std::mutex> lock(virConnectMutex_);
    CloseConnLocked();
}

void LibvirtHelper::CloseConnLocked()
{
    LOG_INFO("Start to close libvirt connect");
    if (!virConnect) {
        LOG_WARN("Libvirt connect is empty.");
        return;
    }
    virConnectClose(virConnect);
    virConnect = nullptr;
}

/**
 * Reconnect Libvirt
 * @return VasRet
 */
VasRet LibvirtHelper::Reconnect()
{
    std::lock_guard<std::mutex> lock(virConnectMutex_);
    CloseConnLocked();
    return ConnectLocked();
}

/**
 * Check if the libvirt connection is alive
 * @return bool
 */
bool LibvirtHelper::IsConnectAlive() const
{
    std::lock_guard<std::mutex> lock(virConnectMutex_);
    if (virConnect && virConnectIsAlive(virConnect) > 0) {
        return true;
    }
    LOG_ERROR("Libvirt connect is not alive.");
    return false;
}

/**
 * Confirm whether the connection exists; if the link is broken, attempt to reconnect
 * @return VasRet
 */
VasRet LibvirtHelper::CheckWithReconnect()
{
    if (!IsConnectAlive()) {
        LOG_WARN("Libvirt connect is not alive, try to Reconnect.");
        if (isVasRetFail(Reconnect())) {
            LOG_ERROR("Reconnect libvirt failed. Please check environment.");
            return VAS_ERROR;
        }
    }
    return VAS_OK;
}

/**
 * Retrieve the list and count of virtual machines
 * @param domains vm domains
 * @param numDomains number of domains
 * @return VasRet
 */
VasRet LibvirtHelper::GetDomainList(virDomainPtr *&domains, int &numDomains) const
{
    std::lock_guard<std::mutex> lock(virConnectMutex_);
    if (!virConnect) {
        LOG_ERROR("Libvirt connect is not ready, get vm domain list failed.");
        return VAS_ERROR;
    }
    numDomains =
        virConnectListAllDomains(virConnect, &domains, virConnectListAllDomainsFlags::VIR_CONNECT_LIST_DOMAINS_ACTIVE);
    if (numDomains < 0 || !domains) {
        LOG_ERROR("Get vmDomain infos failed by libvirt.");
        GetLastError();
        if (domains != nullptr) {
            free(domains);
        }
        return VAS_ERROR;
    }
    return VAS_OK;
}
} // namespace vas::sched::acquire
