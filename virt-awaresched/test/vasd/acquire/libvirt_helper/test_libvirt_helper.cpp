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
#include "test_libvirt_helper.h"

#include "conf.h"
#include "cpu_helper.h"
#include "libvirt_helper.h"
#include "vasd_arg_parse.h"

namespace vas::ut::acquire {
using namespace vas::sched::acquire;
using namespace vas::sched;
static const std::string VM_NAME = "vmName";
static const std::string UUID = "3faa6728-6027-4bd1-a858-6107d2221428";
static const std::string UUID2 = "3faa6728-6027-4bd1-a858-6107d2221429";
static const unsigned long MEMORY = 10240;
static const uint16_t DEFAULT_CPU_MAP_LEN = 4;

void TestLibvirtHelper::SetUp()
{
    Test::SetUp();
}

void TestLibvirtHelper::TearDown()
{
    GlobalMockObject::verify();
    Test::TearDown();
}

virConnectPtr TestVirConnectOpen(const char *str)
{
    return reinterpret_cast<virConnectPtr>(new char[1]);
}

virConnectPtr TestVirConnectOpenError(const char *str)
{
    return nullptr;
}

int TestVirEventRegisterDefaultImplError()
{
    return VAS_ERROR;
}

int TestVirEventRegisterDefaultImpl()
{
    return VAS_OK;
}

TEST_F(TestLibvirtHelper, RegisterEventDefaultImplTest)
{
    MOCKER(virEventRegisterDefaultImpl).stubs().will(invoke(TestVirEventRegisterDefaultImplError));
    EXPECT_EQ(LibvirtHelper::GetInstance().RegisterEventDefaultImpl(), VAS_ERROR);
    MOCKER(virEventRegisterDefaultImpl).reset();

    MOCKER(virEventRegisterDefaultImpl).stubs().will(invoke(TestVirEventRegisterDefaultImpl));
    EXPECT_EQ(LibvirtHelper::GetInstance().RegisterEventDefaultImpl(), VAS_OK);
    MOCKER(virEventRegisterDefaultImpl).reset();
}

int TestVirConnectIsAlive(virConnectPtr virConnect)
{
    return 1;
}

int TestVirConnectIsNotAlive(virConnectPtr virConnect)
{
    return 0;
}

TEST_F(TestLibvirtHelper, IsConnectAliveTest)
{
    MOCKER(virConnectOpen).stubs().will(invoke(TestVirConnectOpen));
    LibvirtHelper::GetInstance().Connect();
    MOCKER(virConnectIsAlive).stubs().will(invoke(TestVirConnectIsAlive));
    EXPECT_EQ(LibvirtHelper::GetInstance().IsConnectAlive(), true);
    MOCKER(virConnectIsAlive).reset();

    MOCKER(virConnectIsAlive).stubs().will(invoke(TestVirConnectIsNotAlive));
    EXPECT_EQ(LibvirtHelper::GetInstance().IsConnectAlive(), false);
    MOCKER(virConnectIsAlive).reset();
}

int TestVirConnectCloseReturnZero(virConnectPtr param)
{
    return 0;
}

TEST_F(TestLibvirtHelper, CheckWithReconnectTest)
{
    MOCKER(&LibvirtHelper::IsConnectAlive).stubs().will(returnValue(true));
    EXPECT_EQ(LibvirtHelper::GetInstance().CheckWithReconnect(), VAS_OK);
    MOCKER(&LibvirtHelper::IsConnectAlive).reset();

    // Reconnect holds virConnectMutex_ and calls the *Locked internals directly,
    // so mock the libvirt C layer instead of the CloseConn/Connect members.
    MOCKER(&LibvirtHelper::IsConnectAlive).stubs().will(returnValue(false));
    MOCKER(virConnectClose).stubs().will(invoke(TestVirConnectCloseReturnZero));
    MOCKER(virConnectOpen).stubs().will(invoke(TestVirConnectOpenError));
    EXPECT_EQ(LibvirtHelper::GetInstance().CheckWithReconnect(), VAS_ERROR);
    MOCKER(virConnectOpen).reset();
    MOCKER(virConnectClose).reset();
    // Restore a fake connection for later tests relying on a non-null virConnect.
    MOCKER(virConnectOpen).stubs().will(invoke(TestVirConnectOpen));
    EXPECT_EQ(LibvirtHelper::GetInstance().Connect(), VAS_OK);
    MOCKER(virConnectOpen).reset();
}

int TestVirConnectListAllDomains(virConnectPtr conn, virDomainPtr **domains, unsigned int flags)
{
    virDomainPtr mockDomain = reinterpret_cast<virDomainPtr>(0x1234);
    *domains = reinterpret_cast<virDomainPtr *>(malloc(sizeof(virDomainPtr)));
    (*domains)[0] = mockDomain;
    return 1;
}

int TestVirDomainGetVcpus(virDomainPtr domain, virVcpuInfo *info, int nrVcpu, unsigned char *cpuMaps, int flag)
{
    info->number = 1;
    nrVcpu = 1;
    return 1;
}

int TestVirDomainGetInfo(virDomainPtr vmDomain, virDomainInfo *info)
{
    auto *tempInfo = info;
    tempInfo->state = 1;
    tempInfo->maxMem = MEMORY;
    tempInfo->memory = MEMORY;
    tempInfo->nrVirtCpu = 1;
    return 1;
}

int TestVirDomainGetVcpuPinInfo(virDomainPtr domain, int nrVcpu, unsigned char *cpuMaps, int cpuMapLen,
                                unsigned int flag)
{
    return nrVcpu;
}

int TestVirDomainGetVcpusError(virDomainPtr domain, int nrVcpu, unsigned char *cpuMaps, int cpuMapLen,
                               unsigned int flag)
{
    return 0;
}

int TestVirDomainFree(virDomainPtr param)
{
    return 0;
}

VasRet MockGetVmInfoReturnWarn(virDomainPtr domain, VmInfo &vmInfo)
{
    vmInfo.uuid = UUID;
    return VAS_WARN;
}

VasRet MockGetVmInfoReturnError(virDomainPtr domain, VmInfo &vmInfo)
{
    vmInfo.uuid = UUID;
    return VAS_ERROR;
}

VasRet MockGetVmInfoReturnOk(virDomainPtr domain, VmInfo &vmInfo)
{
    vmInfo.uuid = UUID;
    return VAS_OK;
}

TEST_F(TestLibvirtHelper, GetVmInfoListTest)
{
    VmInfoMap vmInfoMap{};
    VmInfo vmInfo{};
    vmInfoMap[UUID2] = vmInfo;
    MOCKER(&LibvirtHelper::CheckWithReconnect).stubs().will(returnValue(VAS_ERROR));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmInfoList(vmInfoMap), VAS_ERROR);
    MOCKER(&LibvirtHelper::CheckWithReconnect).reset();

    MOCKER(&LibvirtHelper::CheckWithReconnect).stubs().will(returnValue(VAS_OK));
    MOCKER(&LibvirtHelper::GetDomainList).stubs().will(returnValue(VAS_ERROR));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmInfoList(vmInfoMap), VAS_ERROR);
    MOCKER(&LibvirtHelper::GetDomainList).reset();

    MOCKER(virConnectListAllDomains).stubs().will(invoke(TestVirConnectListAllDomains));
    MOCKER(LibvirtHelper::GetVmInfo).stubs().will(returnValue(VAS_ERROR_NULLPTR));
    MOCKER(virDomainFree).stubs().will(invoke(TestVirDomainFree));
    MOCKER(LibvirtHelper::FlushVmsPidInfo).stubs();
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmInfoList(vmInfoMap), VAS_OK);
    MOCKER(LibvirtHelper::GetVmInfo).reset();

    MOCKER(LibvirtHelper::GetVmInfo).stubs().will(invoke(MockGetVmInfoReturnWarn));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmInfoList(vmInfoMap), VAS_OK);
    MOCKER(LibvirtHelper::GetVmInfo).reset();

    MOCKER(LibvirtHelper::GetVmInfo).stubs().will(invoke(MockGetVmInfoReturnError));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmInfoList(vmInfoMap), VAS_OK);
    MOCKER(LibvirtHelper::GetVmInfo).reset();

    MOCKER(LibvirtHelper::GetVmInfo).stubs().will(invoke(MockGetVmInfoReturnOk));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmInfoList(vmInfoMap), VAS_OK);
    MOCKER(LibvirtHelper::GetVmInfo).reset();
}

virDomainPtr VirDomainLookupByUUIDStringFuncError(virConnectPtr param1, const char *param2)
{
    return nullptr;
}

virDomainPtr MockVirDomainLookupByUUIDStringFunc(virConnectPtr param1, const char *uuid)
{
    return reinterpret_cast<virDomainPtr>(new char[1]);
}

TEST_F(TestLibvirtHelper, GetDomainConnByUUIDTest)
{
    virDomainPtr domainConn = nullptr;

    MOCKER(virDomainLookupByUUIDString).stubs().will(invoke(VirDomainLookupByUUIDStringFuncError));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetDomainConnByUUID(UUID, domainConn), VAS_ERROR);
    MOCKER(virDomainLookupByUUIDString).reset();

    MOCKER(virDomainLookupByUUIDString).stubs().will(invoke(MockVirDomainLookupByUUIDStringFunc));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetDomainConnByUUID(UUID, domainConn), VAS_OK);
    delete domainConn;
}

TEST_F(TestLibvirtHelper, GetVmInfoTest)
{
    VmInfo vmInfo{};
    virDomainPtr domain = nullptr;
    MOCKER(LibvirtHelper::GetVmName).stubs().will(returnValue(VAS_ERROR));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmInfo(domain, vmInfo), VAS_ERROR);
    MOCKER(LibvirtHelper::GetVmName).reset();

    MOCKER(LibvirtHelper::GetVmName).stubs().will(returnValue(VAS_OK));
    MOCKER(LibvirtHelper::GetVmUUID).stubs().will(returnValue(VAS_ERROR));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmInfo(domain, vmInfo), VAS_ERROR);
    MOCKER(LibvirtHelper::GetVmUUID).reset();

    MOCKER(LibvirtHelper::GetVmUUID).stubs().will(returnValue(VAS_OK));
    MOCKER(LibvirtHelper::GetVmVcpuMap).stubs().will(returnValue(VAS_ERROR));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmInfo(domain, vmInfo), VAS_ERROR);
    MOCKER(LibvirtHelper::GetVmVcpuMap).reset();

    MOCKER(LibvirtHelper::GetVmVcpuMap).stubs().will(returnValue(VAS_OK));
    MOCKER(LibvirtHelper::IsReschedSkippedDomain).stubs().will(returnValue(true));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmInfo(domain, vmInfo), VAS_WARN);
    MOCKER(LibvirtHelper::IsReschedSkippedDomain).reset();

    MOCKER(LibvirtHelper::IsReschedSkippedDomain).stubs().will(returnValue(false));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmInfo(domain, vmInfo), VAS_OK);
}

int VirEventRunDefaultImplError()
{
    return -1;
}

TEST_F(TestLibvirtHelper, RunEventDefaultImplTest)
{
    virConnectDomainEventCallback func;
    MOCKER(&LibvirtHelper::RegisterDomainEvent).stubs().will(returnValue(VAS_ERROR));
    EXPECT_EQ(LibvirtHelper::GetInstance().RunEventDefaultImpl(func), VAS_ERROR);
    MOCKER(&LibvirtHelper::RegisterDomainEvent).reset();

    MOCKER(&LibvirtHelper::RegisterDomainEvent)
        .stubs()
        .will(returnValue(VAS_OK))
        .then(returnValue(VAS_OK))
        .then(returnValue(VAS_ERROR));
    MOCKER(virEventRunDefaultImpl).stubs().will(invoke(VirEventRunDefaultImplError));
    MOCKER(&LibvirtHelper::IsConnectAlive).stubs().will(returnValue(false));
    // Reconnect calls the *Locked internals: mock the libvirt C layer
    // (1st open fails -> retry loop, later opens succeed).
    MOCKER(virConnectClose).stubs().will(invoke(TestVirConnectCloseReturnZero));
    MOCKER(virConnectOpen).stubs().will(invoke(TestVirConnectOpenError)).then(invoke(TestVirConnectOpen));
    EXPECT_EQ(LibvirtHelper::GetInstance().RunEventDefaultImpl(func), VAS_ERROR);
    MOCKER(virConnectClose).reset();
    MOCKER(virConnectOpen).reset();

    MOCKER(&LibvirtHelper::RegisterDomainEvent).reset();
    MOCKER(&LibvirtHelper::RegisterDomainEvent).stubs().will(returnValue(VAS_OK));
    Conf::exitFlag.store(true);
    EXPECT_EQ(LibvirtHelper::GetInstance().RunEventDefaultImpl(func), VAS_OK);
    Conf::exitFlag.store(false);
}

TEST_F(TestLibvirtHelper, RegisterDomainEventTest)
{
    virConnectDomainEventCallback func;

    MOCKER(virConnectDomainEventRegister).stubs().will(returnValue(-1));
    MOCKER(LibvirtHelper::GetLastError).stubs();
    EXPECT_EQ(LibvirtHelper::GetInstance().RegisterDomainEvent(func), VAS_ERROR);
    MOCKER(virConnectDomainEventRegister).reset();

    MOCKER(virConnectDomainEventRegister).stubs().will(returnValue(0));
    EXPECT_EQ(LibvirtHelper::GetInstance().RegisterDomainEvent(func), VAS_OK);
}

virError *VirGetLastError()
{
    auto virErrors = new virError{};
    virErrors->code = 1;
    virErrors->domain = 1;
    virErrors->message = "error message";
    return virErrors;
}

TEST_F(TestLibvirtHelper, GetLastErrorTest)
{
    MOCKER(virGetLastError).stubs().will(invoke(VirGetLastError));
    EXPECT_NO_THROW(LibvirtHelper::GetLastError());
}

TEST_F(TestLibvirtHelper, FreeDomainTest)
{
    virDomainPtr domain = nullptr;
    LibvirtHelper::FreeDomain(domain);

    domain = reinterpret_cast<virDomainPtr>(new char[1]);

    MOCKER(virDomainFree).stubs().will(invoke(TestVirDomainFree));
    LibvirtHelper::FreeDomain(domain);
    delete domain;
}

TEST_F(TestLibvirtHelper, FreeDomainsTest)
{
    virDomainPtr *domains = nullptr;
    EXPECT_NO_THROW(LibvirtHelper::FreeDomains(domains, 1));

    MOCKER(virDomainFree).stubs().will(invoke(TestVirDomainFree));
    domains = static_cast<virDomainPtr *>(malloc(sizeof(virDomainPtr)));
    EXPECT_NO_THROW(LibvirtHelper::FreeDomains(domains, 1));
}

const char *VirDomainGetNameError(virDomainPtr param)
{
    return nullptr;
}

const char *TestVirDomainGetNameFunc(virDomainPtr param)
{
    return VM_NAME.c_str();
}

TEST_F(TestLibvirtHelper, GetVmNameTest)
{
    virDomainPtr domain = reinterpret_cast<virDomainPtr>(0x1234);
    std::string name;

    MOCKER(virDomainGetName).stubs().will(invoke(VirDomainGetNameError));
    MOCKER(LibvirtHelper::GetLastError).stubs();
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmName(domain, name), VAS_ERROR);
    MOCKER(virDomainGetName).reset();

    MOCKER(virDomainGetName).stubs().will(invoke(TestVirDomainGetNameFunc));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmName(domain, name), VAS_OK);
    EXPECT_EQ(name, VM_NAME);
}

int TestVirDomainGetUUIDStringError(virDomainPtr vmDomain, char *uuid)
{
    return 1;
}

int TestVirDomainGetUUIDString(virDomainPtr vmDomain, char *uuid)
{
    std::copy_n(UUID.begin(), UUID.length(), uuid);
    uuid[UUID.length()] = '\0';
    return 0;
}

TEST_F(TestLibvirtHelper, GetVmUUIDTest)
{
    virDomainPtr domain = reinterpret_cast<virDomainPtr>(0x1234);
    std::string uuid;

    MOCKER(virDomainGetUUIDString).stubs().will(invoke(TestVirDomainGetUUIDStringError));
    MOCKER(LibvirtHelper::GetLastError).stubs();
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmUUID(domain, uuid), VAS_ERROR);
    MOCKER(virDomainGetUUIDString).reset();

    MOCKER(virDomainGetUUIDString).stubs().will(invoke(TestVirDomainGetUUIDString));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmUUID(domain, uuid), VAS_OK);
    EXPECT_EQ(uuid, UUID);
}

TEST_F(TestLibvirtHelper, IsReschedSkippedDomainTest)
{
    virDomainPtr domain = reinterpret_cast<virDomainPtr>(0x1234);
    MOCKER(virDomainGetInfo).stubs().will(invoke(TestVirDomainGetInfo));
    MOCKER(virDomainGetVcpus).stubs().will(invoke(TestVirDomainGetVcpus));
    MOCKER(virDomainGetVcpuPinInfo).stubs().will(invoke(TestVirDomainGetVcpusError));
    EXPECT_EQ(LibvirtHelper::GetInstance().IsReschedSkippedDomain(domain), true);
    MOCKER(virDomainGetVcpuPinInfo).reset();

    MOCKER(virDomainGetVcpuPinInfo).stubs().will(invoke(TestVirDomainGetVcpuPinInfo));
    MOCKER(Bitset::GetDynamicBitsetAreaSet).stubs().will(returnValue(std::set<uint16_t>({0})));
    EXPECT_EQ(LibvirtHelper::GetInstance().IsReschedSkippedDomain(domain), true);
    MOCKER(Bitset::GetDynamicBitsetAreaSet).reset();

    VasdArgParse::rangeAffinity = false;
    MOCKER(LibvirtHelper::IsVcpuPinNuma).stubs().will(returnValue(false));
    EXPECT_EQ(LibvirtHelper::GetInstance().IsReschedSkippedDomain(domain), true);
    VasdArgParse::rangeAffinity = true;
    EXPECT_EQ(LibvirtHelper::GetInstance().IsReschedSkippedDomain(domain), false);
}

unsigned int VirDomainGetID(virDomainPtr param)
{
    return 0;
}

unsigned int VirDomainGetIDError(virDomainPtr param)
{
    return -1;
}

TEST_F(TestLibvirtHelper, GetVmIDTest)
{
    virDomainPtr domain = reinterpret_cast<virDomainPtr>(0x1234);
    uint16_t id;

    MOCKER(virDomainGetID).stubs().will(invoke(VirDomainGetID));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmID(domain, id), VAS_OK);
    MOCKER(virDomainGetID).reset();

    MOCKER(virDomainGetID).stubs().will(invoke(VirDomainGetIDError));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmID(domain, id), VAS_ERROR);
}

VasRet MockGetVmVcpuInfo(virDomainPtr domain, const int &nrVcpu, std::map<uint16_t, DynamicBitset> &vcpuMaps)
{
    vcpuMaps[0] = DynamicBitset{true, true, true};
    return VAS_OK;
}

TEST_F(TestLibvirtHelper, GetVmVcpuMapTest)
{
    virDomainPtr domain = reinterpret_cast<virDomainPtr>(0x1234);
    VmInfo vmInfo;
    MOCKER(LibvirtHelper::GetDomainInfo).stubs().will(returnValue(VAS_ERROR));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmVcpuMap(domain, vmInfo), VAS_ERROR);
    MOCKER(LibvirtHelper::GetDomainInfo).reset();

    MOCKER(LibvirtHelper::GetDomainInfo).stubs().will(returnValue(VAS_OK));
    MOCKER(LibvirtHelper::GetVmVcpuInfo).stubs().will(returnValue(VAS_ERROR));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmVcpuMap(domain, vmInfo), VAS_ERROR);
    MOCKER(LibvirtHelper::GetDomainInfo).reset();
    MOCKER(LibvirtHelper::GetVmVcpuInfo).reset();

    MOCKER(virDomainGetInfo).stubs().will(invoke(TestVirDomainGetInfo));
    MOCKER(LibvirtHelper::GetVmVcpuInfo).stubs().will(invoke(MockGetVmVcpuInfo));
    MOCKER(LibvirtHelper::GetCpuInNumaRange).stubs().will(returnValue(std::set<uint16_t>()));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmVcpuMap(domain, vmInfo), VAS_ERROR);
    MOCKER(LibvirtHelper::GetCpuInNumaRange).reset();

    MOCKER(LibvirtHelper::GetCpuInNumaRange).stubs().will(returnValue(std::set<uint16_t>({0})));
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmVcpuMap(domain, vmInfo), VAS_OK);
    EXPECT_EQ(LibvirtHelper::GetInstance().GetVmVcpuMap(domain, vmInfo), VAS_OK); // update vmInfo
}

TEST_F(TestLibvirtHelper, GetVmVcpuInfoInvalidNrVcpuTest)
{
    std::map<uint16_t, DynamicBitset> vcpuMaps;
    virDomainPtr domain = reinterpret_cast<virDomainPtr>(0x1234);
    // Regression for R4: nrVcpu is external data from libvirt and must be
    // rejected before it drives the buffer allocations.
    EXPECT_EQ(LibvirtHelper::GetVmVcpuInfo(domain, 0, vcpuMaps), VAS_ERROR);
    EXPECT_EQ(LibvirtHelper::GetVmVcpuInfo(domain, -1, vcpuMaps), VAS_ERROR);
    EXPECT_EQ(LibvirtHelper::GetVmVcpuInfo(domain, LibvirtHelper::MAX_VCPU_NUM + 1, vcpuMaps), VAS_ERROR);
    EXPECT_TRUE(vcpuMaps.empty());

    // The boundary value itself passes validation (fails later at the mocked
    // libvirt call, proving MAX_VCPU_NUM is not rejected).
    MOCKER(virDomainGetVcpus).stubs().will(returnValue(-1));
    EXPECT_EQ(LibvirtHelper::GetVmVcpuInfo(domain, LibvirtHelper::MAX_VCPU_NUM, vcpuMaps), VAS_ERROR);
    MOCKER(virDomainGetVcpus).reset();
    EXPECT_TRUE(vcpuMaps.empty());
}

TEST_F(TestLibvirtHelper, GetVmVcpuInfoNormalTest)
{
    std::map<uint16_t, DynamicBitset> vcpuMaps;
    virDomainPtr domain = reinterpret_cast<virDomainPtr>(0x1234);
    MOCKER(virDomainGetVcpus).stubs().will(invoke(TestVirDomainGetVcpus));
    MOCKER(virDomainGetVcpuPinInfo).stubs().will(invoke(TestVirDomainGetVcpuPinInfo));
    EXPECT_EQ(LibvirtHelper::GetVmVcpuInfo(domain, 1, vcpuMaps), VAS_OK);
    MOCKER(virDomainGetVcpus).reset();
    MOCKER(virDomainGetVcpuPinInfo).reset();
    EXPECT_EQ(vcpuMaps.size(), 1U);
    // TestVirDomainGetVcpus reports vcpu number 1; its zero cpumask expands
    // to a cpuMapLen * 8 bitset (non-empty).
    EXPECT_NE(vcpuMaps.find(1), vcpuMaps.end());
    EXPECT_FALSE(vcpuMaps[1].empty());
}

TEST_F(TestLibvirtHelper, FlushVmsPidInfoTest)
{
    VmInfoMap vmInfoMap{};
    vmInfoMap[UUID] = VmInfo{};
    MOCKER(&ProcHelper::GetVmProcList).stubs().will(returnValue(Vm2VcpuMap{}));
    MOCKER(LibvirtHelper::FlushVmPidInfo).stubs();
    EXPECT_NO_THROW(LibvirtHelper::FlushVmsPidInfo(vmInfoMap));
}

TEST_F(TestLibvirtHelper, GetDomainInfoTest)
{
    virDomainPtr domain = reinterpret_cast<virDomainPtr>(0x1234);
    virDomainInfo virDomainInfos{};

    MOCKER(virDomainGetInfo).stubs().will(returnValue(-1));
    EXPECT_EQ(LibvirtHelper::GetDomainInfo(domain, virDomainInfos), VAS_ERROR);
}

TEST_F(TestLibvirtHelper, GetDomainListTest)
{
    virDomainPtr *domains = nullptr;
    int numDomains = 0;

    MOCKER(virConnectListAllDomains).stubs().will(returnValue(-1));
    MOCKER(LibvirtHelper::GetLastError).stubs();
    EXPECT_EQ(LibvirtHelper::GetInstance().GetDomainList(domains, numDomains), VAS_ERROR);
}

TEST_F(TestLibvirtHelper, GetCpuInNumaRangeTest)
{
    std::set<uint16_t> cpuSet = {0, 1};
    Cpu2NumaIdMap cpu2NumaIdMap;
    cpu2NumaIdMap[0] = 0;
    cpu2NumaIdMap[1] = 0;
    MOCKER(&CpuHelper::GetCpu2NumaIdMap).stubs().will(returnValue(cpu2NumaIdMap));
    EXPECT_EQ(LibvirtHelper::GetCpuInNumaRange(cpuSet), std::set<uint16_t>({0}));
}

TEST_F(TestLibvirtHelper, IsVcpuPinNumaTest)
{
    std::set<uint16_t> cpuSet = {0, 1};
    MOCKER(&CpuHelper::GetCpu2NumaIdMap).stubs().will(returnValue(Cpu2NumaIdMap{{0, 0}, {1, 0}, {2, 1}, {3, 1}}));
    MOCKER(&CpuHelper::GetNuma2CpusetMap).stubs().will(returnValue(Numa2CpusetMap{{0, {0, 1}}, {1, {2, 3}}}));
    EXPECT_EQ(LibvirtHelper::GetInstance().IsVcpuPinNuma(cpuSet), true);
    MOCKER(&CpuHelper::GetNuma2CpusetMap).reset();

    MOCKER(&CpuHelper::GetNuma2CpusetMap).stubs().will(returnValue(Numa2CpusetMap{{0, {0, 1, 2}}, {1, {2, 3}}}));
    EXPECT_EQ(LibvirtHelper::GetInstance().IsVcpuPinNuma(cpuSet), false);
}
} // namespace vas::ut::acquire