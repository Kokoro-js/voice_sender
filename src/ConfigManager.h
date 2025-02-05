// ConfigManager.h
#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <iostream>
#include <thread>
#include <gflags/gflags.h>
#include <figcone/figcone.h>
#include <filesystem>
#include <vector>

// 配置结构体
struct Config {
    int num_threads = 0; // 可选字段
    std::string log_level = "INFO"; // 可选字段
    int max_connections = 100; // 可选字段
    int default_buffer_size = 24 * 1024 * 1024;

    // 定义 traits 以指定哪些字段是可选的
    using traits = figcone::FieldTraits<
            figcone::OptionalField<&Config::num_threads>,
            figcone::OptionalField<&Config::log_level>,
            figcone::OptionalField<&Config::max_connections>,
            figcone::OptionalField<&Config::default_buffer_size>
    >;
};

// 配置管理器类声明
class ConfigManager {
public:
    // 获取单例实例
    static ConfigManager &getInstance();

    // 初始化配置
    void initialize(int argc, char *argv[], const std::string &config_file_path = "config.toml");

    // 获取当前配置
    const Config &getConfig() const;

    // 获取硬件线程数
    unsigned int getHardwareThreads() const;

    // 打印所有配置项
    void printConfig() const;

    // 析构函数
    ~ConfigManager();

private:
    // 私有构造函数
    ConfigManager();

    // 禁用拷贝构造和赋值
    ConfigManager(const ConfigManager &) = delete;

    ConfigManager &operator=(const ConfigManager &) = delete;

    // 当前配置实例
    Config config_;

    // 硬件线程数
    unsigned int hardware_threads_ = std::thread::hardware_concurrency();

    // 从文件读取内容
    std::string readFileContent(const std::string &file_path);

    // 根据 gflags 设置动态更新配置
    template<typename T>
    void updateConfigWithFlag(const char *flag, T &config_field);
};

#endif // CONFIG_MANAGER_H
