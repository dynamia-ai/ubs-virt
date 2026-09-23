/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "utils.h"

#include <cctype>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"

#include "log/ebpf_logger_macros.h"

namespace utils {

namespace fs = std::filesystem;
// Configuration file path
const char *CONFIG_PATH = "/usr/local/sbin/ubs-optimizer/config.json";

pid_t forkWithDir(const char *path)
{
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid > 0) {
        return pid;
    }
    if (setsid() < 0) {
        return -1;
    }
    umask(0);
    if (chdir(path) < 0) {
        return -1;
    }
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    return pid;
}

pid_t getDaemonPID(const char *path)
{
    char *pidPath = realpath(path, nullptr);
    if (pidPath == nullptr) {
        return 0;
    }
    std::ifstream pidFile(pidPath);
    pid_t pid;
    pidFile >> pid;
    free(pidPath);
    return pid;
}

bool checkProcessName(int pid, const char *name)
{
    std::string processName;
    std::string path = "/proc/" + std::to_string(pid) + "/comm";
    char *commPath = realpath(path.c_str(), nullptr);
    if (commPath == nullptr) {
        return false;
    }
    std::ifstream file(commPath);
    free(commPath);

    if (file.is_open()) {
        std::getline(file, processName);
        file.close();
        return processName.find(name) != std::string::npos;
    }
    return false;
}

bool checkRunning(const char *path, const char *name)
{
    if (!fs::exists(path)) {
        return false;
    }
    auto pid = utils::getDaemonPID(path);
    if (kill(pid, 0) != 0) {
        return false;
    }

    return checkProcessName(pid, name);
}

// Get the value in config.json.Normally,it returns a string of the value.Otherwise,it returns an empty string.
const rapidjson::Value getConfigValue(const std::string &prop_path)
{
    std::ifstream ifs(CONFIG_PATH);
    if (!ifs.is_open()) {
        EBPF_LOG_ERROR("Open config file failed: " + std::string(CONFIG_PATH));
        return rapidjson::Value();
    }
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string jsonStr = buffer.str();
    ifs.close();

    rapidjson::Document doc;
    rapidjson::ParseResult ok = doc.Parse(jsonStr.c_str());
    if (!ok) {
        EBPF_LOG_ERROR("Parse config file failed, due to: " + std::string(rapidjson::GetParseError_En(ok.Code())) +
                       " (offset: " + std::to_string(ok.Offset()) + ")");
        return rapidjson::Value();
    }

    size_t start = 0;
    size_t end = prop_path.find('.');
    const rapidjson::Value *current = &doc;

    while (end != std::string::npos) {
        std::string key = prop_path.substr(start, end - start);
        if (!current->IsObject() || !current->HasMember(key.c_str())) {
            EBPF_LOG_ERROR("Config key not found: " + key + " in path: " + prop_path);
            return rapidjson::Value();
        }
        current = &((*current)[key.c_str()]);
        start = end + 1;
        end = prop_path.find('.', start);
    }

    std::string final_key = prop_path.substr(start);
    if (!current->IsObject() || !current->HasMember(final_key.c_str())) {
        EBPF_LOG_ERROR("Config key not found: " + final_key + " in path: " + prop_path);
        return rapidjson::Value();
    }

    return rapidjson::Value(doc[final_key.c_str()], doc.GetAllocator());
}

bool isValidVmName(const std::string &vmName)
{
    if (vmName.empty()) {
        return false;
    }
    for (auto c : vmName) {
        if (!isalpha(static_cast<unsigned char>(c)) && !isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::string getVmName()
{
    const rapidjson::Value npuTypeName = getConfigValue("vm_name");
    if (npuTypeName.IsNull() || !npuTypeName.IsString()) {
        EBPF_LOG_ERROR("Parse config failed, vm_name is missing or not a string.");
        return "";
    }
    std::string vmName = npuTypeName.GetString();
    if (!isValidVmName(vmName)) {
        EBPF_LOG_ERROR("Invalid vm name, only English letters or numbers are supported.");
        return "";
    }
    return vmName;
}

void writeToDisk(const std::vector<std::string> &jsonBuffer, const std::string &outputPath)
{
    if (jsonBuffer.empty()) {
        return;
    }
    fs::path pathObj(outputPath);
    fs::path parentDir = pathObj.parent_path();
    if (!parentDir.empty() && !fs::exists(parentDir)) {
        fs::path curDir;
        for (const auto &part : parentDir) {
            curDir /= part;
            if (!fs::exists(curDir)) {
                fs::create_directories(curDir);
                fs::permissions(curDir, fs::perms::owner_all);
            }
        }
    }

    bool fileExits = fs::exists(pathObj);
    std::ofstream ofs(outputPath, std::ios::app);
    if (!ofs.is_open()) {
        EBPF_LOG_ERROR("Failed to open file: " + outputPath);
        return;
    }
    if (fileExits ^ fs::exists(pathObj)) {
        fs::permissions(outputPath, fs::perms::owner_read | fs::perms::owner_write);
    }
    for (const auto &jsonLine : jsonBuffer) {
        EBPF_LOG_DEBUG("Flushing " + jsonLine);
        ofs << jsonLine << "\n";
    }
}

NPUType getNPUType()
{
    const rapidjson::Value npuTypeName = utils::getConfigValue("npu_type");
    if (npuTypeName.IsNull() || !npuTypeName.IsString()) {
        EBPF_LOG_ERROR("Parse config failed, npu_type is missing or not a string.");
        return NPUType::UNKNOWN;
    }
    std::string typeStr = npuTypeName.GetString();
    if (typeStr == "d802") {
        return NPUType::ALLOWED_NPU_D802;
    }
    if (typeStr == "d803") {
        return NPUType::ALLOWED_NPU_D803;
    }
    EBPF_LOG_WARN("Parse config failed. Unknown NPU value '" + typeStr + "', only support 'd802' or 'd803'");
    return NPUType::UNKNOWN;
}

std::string NPUType2Str(NPUType npuType)
{
    switch (npuType) {
        case NPUType::ALLOWED_NPU_D802:
            return "d802";
        case NPUType::ALLOWED_NPU_D803:
            return "d803";
        default:
            return "";
    }
}
} // namespace utils