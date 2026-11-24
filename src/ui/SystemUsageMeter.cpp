#include "ui/SystemUsageMeter.h"
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#elif JUCE_MAC
 #include <mach/mach.h>
 #include <sys/sysctl.h>
#endif

SystemUsageMeter::SystemUsageMeter() {
    setTitle(TRANS("System usage"));
    setComponentID("system-usage");
    timerCallback();
    startTimer(1000);
}

void SystemUsageMeter::timerCallback() {
    uint64_t total = 0, idle = 0, bytes = 0, used = 0;
#if JUCE_WINDOWS
    FILETIME idleTime{}, kernel{}, user{};
    if (GetSystemTimes(&idleTime, &kernel, &user)) {
        auto ticks = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
        total = ticks(kernel) + ticks(user);
        idle = ticks(idleTime);
    }
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        bytes = status.ullTotalPhys;
        used = bytes - status.ullAvailPhys;
    }
#elif JUCE_MAC
    const auto host = mach_host_self();
    host_cpu_load_info_data_t load{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(host, HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&load), &count) == KERN_SUCCESS) {
        for (auto ticks : load.cpu_ticks) total += ticks;
        idle = load.cpu_ticks[CPU_STATE_IDLE];
    }
    vm_statistics64_data_t vm{};
    count = HOST_VM_INFO64_COUNT;
    vm_size_t page = 0;
    size_t length = sizeof(bytes);
    if (host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) == KERN_SUCCESS
        && host_page_size(host, &page) == KERN_SUCCESS && sysctlbyname("hw.memsize", &bytes, &length, nullptr, 0) == 0)
        used = (uint64_t(vm.active_count) + vm.wire_count + vm.compressor_page_count) * page;
    mach_port_deallocate(mach_task_self(), host);
#endif
    cpu = -1;
    if (previousTotal && total > previousTotal && idle >= previousIdle && idle - previousIdle <= total - previousTotal)
        cpu = juce::roundToInt(100. * (1. - double(idle - previousIdle) / double(total - previousTotal)));
    previousTotal = total;
    previousIdle = idle;
    memory = bytes ? juce::roundToInt(100. * std::min(used, bytes) / bytes) : -1;
    juce::String tip = TRANS("System CPU and physical memory");
    if (bytes) tip += "   " + juce::String(used / 1073741824., 1) + " / " + juce::String(bytes / 1073741824., 1) + " GiB";
    setTooltip(tip);
    repaint();
}

void SystemUsageMeter::paint(juce::Graphics& g) {
    auto area = getLocalBounds();
    auto meter = [&](juce::Rectangle<int> box, const juce::String& name, int value) {
        box.reduce(5, 1);
        const auto bar = box.removeFromBottom(3);
        g.setColour(juce::Colour(0xff31434e)); g.fillRect(bar);
        g.setColour(value >= 90 ? juce::Colour(0xffffbf72) : juce::Colour(0xff69c9bd));
        if (value >= 0) g.fillRect(bar.withWidth(juce::roundToInt(bar.getWidth() * value / 100.)));
        g.setColour(juce::Colour(0xffe5eef1));
        g.setFont(juce::FontOptions(13.f));
        g.drawText(name + "  " + (value < 0 ? juce::String::charToString(0x2014) : juce::String(value) + "%"),
                   box, juce::Justification::centredLeft);
    };
    meter(area.removeFromLeft(area.getWidth() / 2), "CPU", cpu);
    meter(area, "RAM", memory);
}
