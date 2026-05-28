#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

// #include <expp/app/cli.hpp>

// using namespace expp::app::cli;

class CommandMock {
public:
    CommandMock(bool hidden) : hidden_(hidden) {}

    [[nodiscard]] bool isHidden() const { return hidden_; }

private:
    bool hidden_;
};

class CommandRegistry {
public:
    void addCommand(const std::string& name, bool hidden) {
        subcommands_[name] = std::make_shared<CommandMock>(hidden);
    }

    // 版本 1：你当前的原生 for 循环
    [[nodiscard]] std::vector<const CommandMock*> visibleSubcommandsLoop() const {
        std::vector<const CommandMock*> result;
        // // 预分配内存是一个好习惯，这可能会影响性能对比
        result.reserve(subcommands_.size());
        for (const auto& [name, cmd] : subcommands_) {
            if (!cmd->isHidden()) {
                result.push_back(cmd.get());
            }
        }
        result.shrink_to_fit();
        return result;
    }

    // 版本 2：C++20 Ranges (filter | transform | to)
    [[nodiscard]] std::vector<const CommandMock*> visibleSubcommandsRanges() const {
        auto is_visible = [](const auto& pair) { return !pair.second->isHidden(); };
        auto get_ptr = [](const auto& pair) { return pair.second.get(); };

        return subcommands_ | std::views::filter(is_visible) | std::views::transform(get_ptr) | std::ranges::to<std::vector<const CommandMock*>>();

        // 注意：C++23 才有 std::ranges::to，如果是 C++20 需要手动构造
        // return std::vector<const CommandMock*>(view.begin(), view.end());
    }

private:
    std::map<std::string, std::shared_ptr<CommandMock>> subcommands_;
};

// --- 基准测试开始 ---
TEST_CASE("Benchmark visibleSubcommands: Loop vs Ranges", "[benchmark][command]") {
    CommandRegistry registry;

    // 1. 准备测试数据 (Setup phase)
    // 构造 1000 个子命令，其中 20% 是隐藏的
    for (int i = 0; i < 100000; ++i) {
        bool is_hidden = (i % 5 == 0);
        registry.addCommand("cmd_" + std::to_string(i), is_hidden);
    }

    // 2. 运行基准测试
    BENCHMARK("Raw For Loop") {
        // 重要：必须 return 结果！
        // 这样可以防止现代 C++ 编译器发现你没使用返回值，从而把整个函数调用优化（删除）掉。
        return registry.visibleSubcommandsLoop();
    };

    BENCHMARK("C++20 Ranges") {
        return registry.visibleSubcommandsRanges();
    };
}