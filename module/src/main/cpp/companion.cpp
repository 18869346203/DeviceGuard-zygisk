#include <android/log.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <dirent.h>
#include <fcntl.h>

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "DeviceGuard", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "DeviceGuard", __VA_ARGS__)

using namespace std::chrono_literals;

static std::vector<std::string> PACKAGES;
static std::string ANO_TMP_PATTERN = "/data/user/0/%s/files/ano_tmp/custom_cache";
static std::string PERSIST_STOPPED_PERM = "771";
static int CLEAN_DELAY_SECONDS = 30;
static std::mutex config_mutex;

static std::map<std::string, bool> running_state;
static std::map<int, std::string> wd_to_package;
static int inotify_fd = -1;

enum class CompanionCommand {
    APP_STARTED = 1,
    CONFIG_CHANGED = 2,
};

static void load_config_from_file() {
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
        } else if (line.rfind("ANO_TMP_PATTERN=", 0) == 0) {
            size_t eq = line.find('=');
            std::string val = line.substr(eq + 1);
            if (!val.empty() && (val.front() == '"' || val.front() == '\''))
                val = val.substr(1, val.size() - 2);
            if (!val.empty()) ANO_TMP_PATTERN = val;
        } else if (line.rfind("PERSIST_STOPPED_PERM=", 0) == 0) {
            size_t eq = line.find('=');
            std::string val = line.substr(eq + 1);
            if (!val.empty() && (val.front() == '"' || val.front() == '\''))
                val = val.substr(1, val.size() - 2);
            if (!val.empty()) PERSIST_STOPPED_PERM = val;
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
    LOGD("companion: set persist perm to %s", perm.c_str());
}

static void clean_ano_tmp(const std::string& pkg) {
    char path[256];
    snprintf(path, sizeof(path), ANO_TMP_PATTERN.c_str(), pkg.c_str());
    std::string cmd = "rm -rf ";
    cmd += path;
    system(cmd.c_str());
    LOGD("companion: cleaned ano_tmp for %s", pkg.c_str());
}

static bool setup_inotify() {
    inotify_fd = inotify_init1(IN_NONBLOCK);
    return inotify_fd != -1;
}

static void watch_process(int pid, const std::string& pkg) {
    if (inotify_fd == -1) return;
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
    int wd = inotify_add_watch(inotify_fd, proc_path, IN_DELETE_SELF);
    if (wd != -1) {
        std::lock_guard<std::mutex> lock(config_mutex);
        wd_to_package[wd] = pkg;
        LOGD("companion: watching %s (pid %d)", pkg.c_str(), pid);
    }
}

static void event_loop() {
    const size_t BUF_LEN = 1024 * (sizeof(struct inotify_event) + 16);
    char buffer[BUF_LEN];
    while (true) {
        int len = read(inotify_fd, buffer, BUF_LEN);
        if (len == -1 && errno != EAGAIN) {
            std::this_thread::sleep_for(100ms);
            continue;
        }
        for (char* ptr = buffer; ptr < buffer + len; ) {
            struct inotify_event* event = (struct inotify_event*)ptr;
            if (event->mask & IN_DELETE_SELF) {
                std::string pkg;
                {
                    std::lock_guard<std::mutex> lock(config_mutex);
                    auto it = wd_to_package.find(event->wd);
                    if (it != wd_to_package.end()) {
                        pkg = it->second;
                        wd_to_package.erase(it);
                    }
                }
                if (!pkg.empty()) {
                    LOGD("companion: process died: %s", pkg.c_str());
                    {
                        std::lock_guard<std::mutex> lock(config_mutex);
                        running_state[pkg] = false;
                        bool all_exited = true;
                        for (const auto& p : PACKAGES) {
                            if (running_state[p]) all_exited = false;
                        }
                        if (all_exited) {
                            set_persist_perm(PERSIST_STOPPED_PERM);
                        }
                    }
                    std::thread([pkg]() {
                        std::this_thread::sleep_for(std::chrono::seconds(CLEAN_DELAY_SECONDS));
                        clean_ano_tmp(pkg);
                    }).detach();
                }
            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
    }
}

static bool is_module_enabled() {
    return access("/data/local/tmp/deviceguard_enabled", F_OK) == 0;
}

static void init_existing_processes() {
    DIR* dir = opendir("/proc");
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;
        char cmdline_path[64];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);
        std::ifstream cmdline_file(cmdline_path);
        if (!cmdline_file.is_open()) continue;
        std::string cmdline;
        std::getline(cmdline_file, cmdline, '\0');
        cmdline_file.close();
        for (const auto& pkg : PACKAGES) {
            if (cmdline == pkg) {
                running_state[pkg] = true;
                watch_process(pid, pkg);
                break;
            }
        }
    }
    closedir(dir);
}

void DeviceGuardModule::companion_handler(int socket) {
    LOGD("companion process started");
    if (!setup_inotify()) {
        LOGE("inotify init failed");
        return;
    }
    load_config_from_file();
    init_existing_processes();
    std::thread event_thread(event_loop);
    event_thread.detach();

    while (true) {
        int cmd;
        if (read(socket, &cmd, sizeof(cmd)) != sizeof(cmd)) break;
        switch ((CompanionCommand)cmd) {
            case CompanionCommand::APP_STARTED: {
                int len;
                if (read(socket, &len, sizeof(l