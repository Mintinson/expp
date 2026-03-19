
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

using namespace ftxui;

// --- 全局或类的成员变量 ---
std::mutex notify_mutex;
std::string notify_text = "";
bool show_notify = false;
std::atomic<uint64_t> current_notify_id{0};  // 防冲突令牌

// 传入 screen 指针是为了调用 PostEvent
void ShowNotification(ScreenInteractive* screen, const std::string& message, int duration_ms = 3000) {
    // 1. 更新 UI 状态
    {
        std::lock_guard<std::mutex> lock(notify_mutex);
        notify_text = message;
        show_notify = true;
    }

    // 2. 生成一个新的唯一 ID
    uint64_t my_id = ++current_notify_id;

    // 3. 立刻通知主线程重绘，把弹窗显示出来
    screen->PostEvent(Event::Custom);

    // 4. 开启后台倒计时线程
    std::thread([screen, my_id, duration_ms]() {
        // 线程休眠指定时间
        std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));

        // 醒来后检查：当前的全局 ID 还是我吗？
        // 如果不是，说明在我睡觉的时候，有新的通知弹出来了，我没有资格去关闭它。
        if (current_notify_id == my_id) {
            {
                std::lock_guard<std::mutex> lock(notify_mutex);
                show_notify = false;  // 只有当前 ID 匹配才隐藏
            }
            // 通知主线程把弹窗收起来
            screen->PostEvent(Event::Custom);
        }
    }).detach();  // 分离线程，让它自己在后台跑完销毁
}

int main() {
    auto screen = ScreenInteractive::Fullscreen();

    // 假设这是你辛辛苦苦写的文件浏览器主界面
    auto main_file_browser_ui = Renderer([] {
        return vbox({text("文件浏览器主界面") | center, text("按 'n' 触发一个通知，按 'q' 退出。") | center}) | border;
    });

    // 构建带通知的顶层渲染器
    auto root_renderer = Renderer(main_file_browser_ui, [&] {
        // 1. 获取主界面的 DOM 树
        Element document = main_file_browser_ui->Render();

        // 2. 检查是否需要渲染通知
        std::lock_guard<std::mutex> lock(notify_mutex);
        if (show_notify) {
            // 构建通知 UI
            Element toast = text(notify_text) | bold | color(Color::Black) | bgcolor(Color::GreenLight) | border |
                            clear_under;  // 极其关键：清除底层的字符，防止底层文字透视上来

            // 利用 filler() 将通知推到右下角
            Element toast_layer = vbox({filler(),        // 把下方的 hbox 挤到最底部
                                        hbox({filler(),  // 把右侧的 toast 挤到最右边
                                              toast})});

            // 使用 dbox 将 main_ui (底层) 和 toast_layer (顶层) 叠起来
            document = dbox({document, toast_layer});
        }

        return document;
    });

    // 捕捉按键测试
    auto event_handler = CatchEvent(root_renderer, [&](Event event) {
        if (event == Event::Character('q')) {
            screen.Exit();
            return true;
        }
        if (event == Event::Character('n')) {
            // 触发通知！不阻塞！
            ShowNotification(&screen, "✅ 复制了 15 个文件 (240MB)", 2500);
            return true;
        }
        return false;
    });

    screen.Loop(event_handler);
    return 0;
}