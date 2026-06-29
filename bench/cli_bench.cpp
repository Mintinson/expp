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

    [[nodiscard]] std::vector<const CommandMock*> visibleSubcommandsRanges() const {
        auto is_visible = [](const auto& pair) { return !pair.second->isHidden(); };
        auto get_ptr = [](const auto& pair) { return pair.second.get(); };

        return subcommands_ | std::views::filter(is_visible) | std::views::transform(get_ptr) | std::ranges::to<std::vector<const CommandMock*>>();

    }

private:
    std::map<std::string, std::shared_ptr<CommandMock>> subcommands_;
};

// --- 基准测试开始 ---
TEST_CASE("Benchmark visibleSubcommands: Loop vs Ranges", "[benchmark][command]") {
    CommandRegistry registry;

    for (int i = 0; i < 100000; ++i) {
        bool is_hidden = (i % 5 == 0);
        registry.addCommand("cmd_" + std::to_string(i), is_hidden);
    }

    BENCHMARK("Raw For Loop") {
        return registry.visibleSubcommandsLoop();
    };

    BENCHMARK("C++20 Ranges") {
        return registry.visibleSubcommandsRanges();
    };
}