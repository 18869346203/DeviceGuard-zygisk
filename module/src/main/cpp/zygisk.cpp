#include <android/log.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <jni.h>
#include "zygisk.hpp"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "DeviceGuard", __VA_ARGS__)

static std::vector<std::string> PACKAGES;
static std::string PERSIST_RUNNING_PERM = "000";
static std::string PERSIST_STOPPED_PERM = "771";
static std::string EXTRA_KILL_PROCESSES;
static int CLEAN_DELAY_SECONDS = 30;
static std::mutex config_mutex;

static void load_config() {
    std::lock_guard<std::mutex> lock(config_mutex);
    PACKAGES.clear();
    std::ifstream conf("/data/adb/modules/DeviceGuard_Zygisk/config.sh");
    if (!conf.is_open()) {
        PACKAGES = {"com.tencent.tmgp.pubgmhd", "com.tencent.tmgp.sgame"};
        return;
    }
    std::string line;
    while (std::getline(conf, line)) {
        if (line.rfind("PACKAGES=", 0) == 0) {
            size_t eq = line.find('=');
            std::string val = line.substr(eq + 1);
            if (!val.empty() && (val.front() == '"' || val.front() == '\''))
                val = val.substr(1, val.size() - 2);
            std::istringstream iss(val);
            std::string pkg;
            while (iss >> pkg) if (!pkg.empty()) PACKAGES.push_back(pkg);
        } else if (line.rfind("PERSIST_RUNNING_PERM=", 0) == 0) {
            size_t eq = line.find('=');
            std::string val = line.substr(eq + 1);
            if (!val.empty() && (val.front() == '"' || val.front() == '\''))
                val = val.substr(1, val.size() - 2);
            if (!val.empty()) PERSIST_RUNNING_PERM = val;
        } else if (line.rfind("PERSIST_STOPPED_PERM=", 0) == 0) {
            size_t eq = line.find('=');
            std::string val = line.substr(eq + 1);
            if (!val.empty() && (val.front() == '"' || val.front() == '\''))
                val = val.substr(1, val.size() - 2);
            if (!val.empty()) PERSIST_STOPPED_PERM = val;
        } else if (line.rfind("EXTRA_KILL_PROCESSES=", 0) == 0) {
            size_t eq = line.find('=');
            std::string val = line.substr(eq + 1);
            if (!val.empty() && (val.front() == '"' || val.front() == '\''))
                val = val.substr(1, val.size() - 2);
            EXTRA_KILL_PROCESSES = val;
        } else if (line.rfind("CLEAN_DELAY_SECONDS=", 0) == 0) {
            size_t eq = line.find('=');
            std::string val = line.substr(eq + 1);
            CLEAN_DELAY_SECONDS = std::stoi(val);
        }
    }
    conf.close();
}

static void set_persist_perm(const std::string& perm) {
    mode_t mode = strtol(perm.c_str(), nullptr, 8);
    chmod("/mnt/vendor/persist", mode);
    LOGD("set persist perm to %s", perm.c_str());
}

static void kill_process(const std::string& proc) {
    std::string cmd = "pkill -f '^" + proc + "'";
    system(cmd.c_str());
    LOGD("killed %s", proc.c_str());
}

static bool is_enabled() {
    return access("/data/local/tmp/deviceguard_enabled", F_OK) == 0;
}

static bool should_reload() {
    if (access("/data/local/tmp/deviceguard_reload", F_OK) == 0) {
        unlink("/data/local/tmp/deviceguard_reload");
        return true;
    }
    return false;
}

enum class CompanionCommand {
    APP_STARTED = 1,
    CONFIG_CHANGED = 2,
};

class DeviceGuardModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
        load_config();
        LOGD("DeviceGuard module loaded");
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        if (!is_enabled() || !args || !args->nice_name) return;
        const char* pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!pkg) return;

        for (const auto& target : PACKAGES) {
            if (target == pkg) {
                LOGD("target starting: %s", pkg);
                set_persist_perm(PERSIST_RUNNING_PERM);
                if (!EXTRA_KILL_PROCESSES.empty()) {
                    std::istringstream iss(EXTRA_KILL_PROCESSES);
                    std::string proc;
                    while (iss >> proc) kill_process(proc);
                }
                if (api->connectCompanion()) {
                    int cmd = (int)CompanionCommand::APP_STARTED;
                    api->writeCompanion(&cmd, sizeof(cmd));
                    int len = strlen(pkg);
                    api->writeCompanion(&len, sizeof(len));
                    api->writeCompanion(pkg, len);
                    api->closeCompanion();
                }
                break;
            }
        }
        env->ReleaseStringUTFChars(args->nice_name, pkg);
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs* args) override {
        api->connectCompanion(companion_handler);
    }

private:
    zygisk::Api* api;
    JNIEnv* env;
    static void companion_handler(int socket);
};

REGISTER_ZYGISK_MODULE(DeviceGuardModule)