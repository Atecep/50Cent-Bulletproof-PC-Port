#include "core/vblank.h"
#include <chrono>
#include <future>
#include <iostream>
#include <cstdlib>
using namespace std::chrono_literals;
static void Check(bool ok,const char* why) { if(!ok){std::cerr<<why<<'\n';std::exit(1);} }
int main() {
    gpu::VBlankSequence blank;
    const auto first=blank.Generation();
    auto waiter1=std::async(std::launch::async,[&]{return blank.WaitAfter(first);});
    auto waiter2=std::async(std::launch::async,[&]{return blank.WaitAfter(first);});
    Check(waiter1.wait_for(0ms)!=std::future_status::ready && waiter2.wait_for(0ms)!=std::future_status::ready,
          "no completed interrupt before first blank");
    const auto second=blank.Notify();
    Check(waiter1.get()==second && waiter2.get()==second,"same interrupt releases both waiters");
    // A fast caller starts waiting for the following blank while another old
    // waiter has not run yet. Its new wait cannot consume/clear the old edge.
    auto fast=std::async(std::launch::async,[&]{return blank.WaitAfter(second);});
    auto late=std::async(std::launch::async,[&]{return blank.WaitAfter(first);});
    Check(late.wait_for(1s)==std::future_status::ready && late.get()==second,"late waiter retains the broadcast edge");
    Check(fast.wait_for(0ms)!=std::future_status::ready,"current blank cannot satisfy a new wait");
    const auto third=blank.Notify();
    Check(fast.get()==third,"following blank completes the new wait");
    std::cout<<"PASS: VBlank edge, shared release, late waiters and no stale completion\n";
}
