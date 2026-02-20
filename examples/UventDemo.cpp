#include <chrono>
#include <iostream>
#include <string>

#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>

namespace {

usub::uvent::task::Awaitable<void> ticker(std::string name, int ticks, std::chrono::milliseconds period) {
    for (int i = 1; i <= ticks; ++i) {
        std::cout << "[" << name << "] tick " << i << '/' << ticks << '\n';
        co_await usub::uvent::system::this_coroutine::sleep_for(period);
    }
    std::cout << "[" << name << "] complete\n";
    co_return;
}

usub::uvent::task::Awaitable<void> orchestrate(usub::Uvent &runtime) {
    usub::uvent::system::co_spawn(ticker("fast", 8, std::chrono::milliseconds(120)));
    usub::uvent::system::co_spawn(ticker("slow", 4, std::chrono::milliseconds(300)));
    usub::uvent::system::co_spawn(ticker("pulse", 6, std::chrono::milliseconds(180)));

    co_await usub::uvent::system::this_coroutine::sleep_for(std::chrono::seconds(2));
    std::cout << "[orchestrator] stopping runtime\n";
    runtime.stop();
    co_return;
}

} // namespace

int main() {
    usub::Uvent runtime{2};

    usub::uvent::system::co_spawn(orchestrate(runtime));
    runtime.run();

    std::cout << "uvent runtime demo finished\n";
    return 0;
}
