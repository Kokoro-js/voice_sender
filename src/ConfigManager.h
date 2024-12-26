// ConfigManager.h
#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <gflags/gflags.h>
#include <thread> // 包含以使用 std::thread::hardware_concurrency()

// 配置结构体
struct Config {
    unsigned int hardware_threads = std::thread::hardware_concurrency();
    int num_threads = 0; // 初始值为0，表示未设置

    std::string log_level = "INFO"; // 其他配置项示例
    int max_connections = 100; // 新配置项，默认值为 100
    // 添加更多配置项...
};

// 配置项基类
struct ConfigItemBase {
    std::string env_key;
    std::string flag_name; // gflags 名称
    virtual void apply(const std::string& prefix) = 0;
    virtual ~ConfigItemBase() {}
};

// 配置项模板类
template<typename T>
struct ConfigItem : ConfigItemBase {
    T default_value;
    T* config_member;
    std::function<T(const std::string&)> parser;

    ConfigItem(const std::string& key, const std::string& flag, T* member, const T& default_val, std::function<T(const std::string&)> parse_func)
        : default_value(default_val), config_member(member), parser(parse_func) {
        env_key = key;
        flag_name = flag;
    }

    void apply(const std::string& prefix) override {
        // 检查 gflag 是否被设置
        std::string flag_full_name = flag_name;
        std::string flag_value_str;
        bool flag_set = gflags::GetCommandLineOption(flag_full_name.c_str(), &flag_value_str);

        if (flag_set) {
            // 使用 gflag 的值
            try {
                *config_member = parser(flag_value_str);
            } catch (const std::exception& e) {
                std::cerr << "Invalid gflag value for " << flag_full_name << ": " << flag_value_str
                          << ". Using default: " << default_value << std::endl;
                *config_member = default_value;
            }
        } else {
            // 尝试从环境变量获取
            std::string full_env_key = prefix + env_key;
            const char* env_val = std::getenv(full_env_key.c_str());
            if (env_val) {
                try {
                    *config_member = parser(std::string(env_val));
                } catch (const std::exception& e) {
                    std::cerr << "Invalid value for " << full_env_key << ": " << env_val
                              << ". Using default: " << default_value << std::endl;
                    *config_member = default_value;
                }
            } else {
                // 使用默认值
                *config_member = default_value;
            }
        }
    }
};

// 配置管理器类
class ConfigManager {
public:
    // 获取单例实例
    static ConfigManager& getInstance();

    // 初始化配置
    void initialize(int argc, char* argv[], const std::string& env_file_path = "");

    // 获取当前配置
    const Config& getConfig() const;

    // 打印所有配置项
    void printConfig() const;

    // 析构函数
    ~ConfigManager();

private:
    // 私有构造函数
    ConfigManager();

    // 禁用拷贝构造和赋值
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    Config config_;

    // 配置项列表
    std::vector<std::unique_ptr<ConfigItemBase>> config_items_;

    // 注册配置项
    template<typename T>
    void registerConfigItem(const std::string& env_key, const std::string& flag_name, T* config_member, const T& default_val, std::function<T(const std::string&)> parser);
};

#endif // CONFIG_MANAGER_H
