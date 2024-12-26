// ConfigManager.cpp
#include "ConfigManager.h"
#include <dotenv.h>
#include <sstream>

// 定义 gflags 标志，num_threads 的默认值设置为0
DEFINE_int32(num_threads, 0, "Number of threads for the thread pool");
DEFINE_string(log_level, "INFO", "Logging level for the application");
DEFINE_int32(max_connections, 100, "Maximum number of connections");

// 获取单例实例
ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

// 私有构造函数
ConfigManager::ConfigManager() {}

// 析构函数
ConfigManager::~ConfigManager() {
    // std::unique_ptr 会自动释放内存
}

// 注册配置项模板函数实现
template<typename T>
void ConfigManager::registerConfigItem(const std::string& env_key, const std::string& flag_name, T* config_member, const T& default_val, std::function<T(const std::string&)> parser) {
    // 创建配置项描述并添加到列表
    config_items_.emplace_back(std::make_unique<ConfigItem<T>>(env_key, flag_name, config_member, default_val, parser));
}

// 初始化配置
void ConfigManager::initialize(int argc, char* argv[], const std::string& env_file_path) {
    // 解析命令行标志
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 加载 .env 文件（如果提供了路径）
    if (!env_file_path.empty()) {
        dotenv::init(env_file_path.c_str());
    } else {
        // 尝试加载默认的 .env 文件
        dotenv::init();
    }

    // 设置环境变量前缀
    const std::string prefix = "MYAPP_";

    // 注册所有配置项
    // 注册 NUM_THREADS
    registerConfigItem<int>(
        "NUM_THREADS",
        "num_threads",
        &config_.num_threads,
        0, // 默认值设置为0，表示未设置
        [](const std::string& val) -> int {
            return std::stoi(val);
        }
    );

    // 注册 LOG_LEVEL
    registerConfigItem<std::string>(
        "LOG_LEVEL",
        "log_level",
        &config_.log_level,
        config_.log_level, // 使用 Config 结构体中初始化的默认值
        [](const std::string& val) -> std::string {
            return val;
        }
    );

    // 注册 MAX_CONNECTIONS
    registerConfigItem<int>(
        "MAX_CONNECTIONS",
        "max_connections",
        &config_.max_connections,
        config_.max_connections, // 使用 Config 结构体中初始化的默认值
        [](const std::string& val) -> int {
            return std::stoi(val);
        }
    );

    // 添加更多配置项注册...

    // 遍历所有配置项并进行初始化
    for (auto& item_base : config_items_) {
        item_base->apply(prefix);
    }

    // 处理动态默认值
    // 如果 num_threads 未通过 gflags 或环境变量设置，则使用 hardware_threads 或 4
    if (config_.num_threads == 0) {
        if (config_.hardware_threads > 0) {
            config_.num_threads = static_cast<int>(config_.hardware_threads);
        } else {
            config_.num_threads = 4; // 备选默认值
        }
    }
}

// 获取当前配置
const Config& ConfigManager::getConfig() const {
    return config_;
}

// 打印所有配置项
void ConfigManager::printConfig() const {
    std::cout << "Current Configuration:" << std::endl;
    std::cout << "NUM_THREADS: " << config_.num_threads << std::endl;
    std::cout << "LOG_LEVEL: " << config_.log_level << std::endl;
    std::cout << "MAX_CONNECTIONS: " << config_.max_connections << std::endl;
    // 打印其他配置项...
}

// 模板实例化（防止链接错误）
template void ConfigManager::registerConfigItem<int>(const std::string&, const std::string&, int*, const int&, std::function<int(const std::string&)>);
template void ConfigManager::registerConfigItem<std::string>(const std::string&, const std::string&, std::string*, const std::string&, std::function<std::string(const std::string&)>);
// 添加更多类型的模板实例化...
