// ConfigManager.cpp
#include "ConfigManager.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <gflags/gflags.h>

// 定义 gflags 标志
DEFINE_int32(num_threads, -1, "Number of threads for the thread pool");
DEFINE_string(log_level, "", "Logging level for the application");
DEFINE_int32(max_connections, -1, "Maximum number of connections");

// 获取单例实例
ConfigManager &ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

// 私有构造函数
ConfigManager::ConfigManager() {}

// 析构函数
ConfigManager::~ConfigManager() {}

// 从文件读取内容
std::string ConfigManager::readFileContent(const std::string &file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Error: Could not open the TOML file!");
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// 更新配置字段值（根据 gflags）
template<typename T>
void ConfigManager::updateConfigWithFlag(const char *flag, T &config_field) {
    if constexpr (std::is_same_v<T, int>) {
        if (FLAGS_num_threads != -1) {  // 检查是否通过命令行提供了值
            config_field = FLAGS_num_threads;  // 使用命令行提供的值
        }
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (!FLAGS_log_level.empty()) {
            config_field = FLAGS_log_level;  // 使用命令行提供的值
        }
    }
    // 对于其他类型也可以类似处理
}


// 初始化配置
void ConfigManager::initialize(int argc, char *argv[], const std::string &config_file_path) {
    // 解析命令行标志
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 使用 figcone 读取配置文件
    figcone::ConfigReader cfgReader;
    try {
        // 从文件加载配置
        config_ = cfgReader.readToml<Config>(readFileContent(config_file_path));
    }
    catch (const std::exception &e) {
        std::cerr << "Error reading config file: " << e.what() << std::endl;
        std::cerr << "Using default configuration." << std::endl;
    }

    // 使用 gflags 配置覆盖文件中的值
    updateConfigWithFlag("num_threads", config_.num_threads);
    updateConfigWithFlag("log_level", config_.log_level);

    // 处理动态默认值
    if (config_.num_threads == 0) {
        if (hardware_threads_ > 0) {
            config_.num_threads = static_cast<int>(hardware_threads_);  // 如果 num_threads 未设置，则使用硬件线程数
        } else {
            config_.num_threads = 4;  // 默认值
        }
    }
}

// 获取当前配置
const Config &ConfigManager::getConfig() const {
    return config_;
}

// 获取硬件线程数
unsigned int ConfigManager::getHardwareThreads() const {
    return hardware_threads_;
}

// 打印所有配置项
void ConfigManager::printConfig() const {
    std::cout << "当前配置:" << std::endl;
    std::cout << "hardware_threads: " << hardware_threads_ << std::endl;
    std::cout << "num_threads: " << config_.num_threads << std::endl;
    std::cout << "log_level: " << config_.log_level << std::endl;
    std::cout << "max_connections: " << config_.max_connections << std::endl;
}

// 显式实例化模板函数
template void ConfigManager::updateConfigWithFlag<int>(const char *, int &);

template void ConfigManager::updateConfigWithFlag<std::string>(const char *, std::string &);
