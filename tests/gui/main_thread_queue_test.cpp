#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "prism/gui/main_thread_queue.hpp"

using prism::gui::MainThreadQueue;

// The threaded test runs under ThreadSanitizer in CI, which reports any unsynchronised access.

TEST(MainThreadQueue, StartsEmpty) {
    MainThreadQueue<int> queue;
    EXPECT_TRUE(queue.take_all().empty());
}

TEST(MainThreadQueue, TakeAllReturnsEverythingInOrderOnce) {
    MainThreadQueue<std::string> queue;
    queue.push("a.jpg");
    queue.push("b.png");
    queue.push("c.arw");
    EXPECT_EQ(queue.take_all(), (std::vector<std::string>{"a.jpg", "b.png", "c.arw"}));
    EXPECT_TRUE(queue.take_all().empty()) << "taken values are gone";

    queue.push("d.dng");
    EXPECT_EQ(queue.take_all(), (std::vector<std::string>{"d.dng"}));
}

TEST(MainThreadQueue, HoldsMoveOnlyValues) {
    MainThreadQueue<std::unique_ptr<int>> queue;
    queue.push(std::make_unique<int>(7));
    std::vector<std::unique_ptr<int>> taken = queue.take_all();
    ASSERT_EQ(taken.size(), 1u);
    EXPECT_EQ(*taken[0], 7);
}

TEST(MainThreadQueue, ManyThreadsPushWhileOneTakes) {
    constexpr int producers = 4;
    constexpr int per_producer = 2000;
    MainThreadQueue<int> queue;

    std::vector<int> received;
    {
        std::vector<std::jthread> threads;
        for (int p = 0; p < producers; ++p) {
            threads.emplace_back([&queue, p] {
                for (int i = 0; i < per_producer; ++i) {
                    queue.push(p * per_producer + i);
                }
            });
        }
        // The consumer takes while the producers are still pushing, as the UI thread would.
        while (received.size() < static_cast<std::size_t>(producers * per_producer)) {
            for (int v : queue.take_all()) {
                received.push_back(v);
            }
            std::this_thread::yield();
        }
    }  // joins the producers

    EXPECT_TRUE(queue.take_all().empty()) << "nothing arrives twice";
    ASSERT_EQ(received.size(), static_cast<std::size_t>(producers * per_producer));
    // Each producer's values arrive in the order it pushed them, and none is lost.
    std::vector<int> next(producers, 0);
    for (int v : received) {
        const int p = v / per_producer;
        EXPECT_EQ(v % per_producer, next[p]) << "producer " << p << " out of order";
        next[p] = v % per_producer + 1;
    }
    for (int p = 0; p < producers; ++p) {
        EXPECT_EQ(next[p], per_producer);
    }
}
