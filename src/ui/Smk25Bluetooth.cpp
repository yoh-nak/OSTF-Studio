#include "ui/Smk25Bluetooth.h"
#if JUCE_WINDOWS && defined(_MSC_VER)
#include "core/Smk25BleProtocol.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <winrt/base.h>
#ifndef __cpp_impl_coroutine
// The 19041 projection defines this function after using its qualified name.
namespace winrt::impl {
template <typename Async> auto wait_for(Async const&, Windows::Foundation::TimeSpan const&);
}
#endif
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>

namespace ostfstudio::smkble {
namespace {
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Storage::Streams;
using namespace std::chrono_literals;
struct Issue { juce::String text; };
void check(bool ok, const juce::String& message) { if (!ok) throw Issue{message}; }
void checkRunning() {
    auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
    check(!job || !job->shouldExit(), TRANS("Transfer cancelled."));
}
template<class Op> auto finish(Op op) {
    const auto deadline = std::chrono::steady_clock::now() + 12s;
    while (op.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
        try {
            checkRunning();
            check(std::chrono::steady_clock::now() < deadline, TRANS("Bluetooth request timed out."));
        } catch (...) { op.Cancel(); throw; }
        std::this_thread::sleep_for(20ms);
    }
    return op.GetResults();
}
struct Apartment {
    Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~Apartment() { winrt::uninit_apartment(); }
};
winrt::guid uuid(std::uint32_t shortId) {
    return {shortId, 0, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb}};
}
bool matchesName(juce::String name) {
    auto s = name.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
    return s.contains("smk25ii") || s.contains("smk25mk2") || s.contains("smk252");
}
void addDevice(std::vector<Device>& devices, juce::String name, std::uint64_t address) {
    if (address && matchesName(name) && std::none_of(devices.begin(), devices.end(), [address](const auto& d){ return d.address == address; }))
        devices.push_back({name, address});
}
struct Inbox {
    std::mutex lock;
    std::condition_variable changed;
    Bytes bytes;
    bool overflow = false;
};
class Link {
public:
    explicit Link(std::uint64_t address) {
        const auto unavailable = TRANS("SMK-25 II Bluetooth settings service is unavailable.");
        device = finish(BluetoothLEDevice::FromBluetoothAddressAsync(address));
        check(bool(device), unavailable);
        const auto services = finish(device.GetGattServicesForUuidAsync(uuid(0xae40), BluetoothCacheMode::Uncached));
        check(services.Status() == GattCommunicationStatus::Success && services.Services().Size() == 1, unavailable);
        service = services.Services().GetAt(0);
        auto characteristic = [&](unsigned key) {
            auto r = finish(service.GetCharacteristicsForUuidAsync(uuid(key), BluetoothCacheMode::Uncached));
            check(r.Status() == GattCommunicationStatus::Success && r.Characteristics().Size() == 1, unavailable);
            return r.Characteristics().GetAt(0);
        };
        tx = characteristic(0xae41);
        rx = characteristic(0xae42);
        const auto flags = static_cast<unsigned>(tx.CharacteristicProperties());
        if (flags & static_cast<unsigned>(GattCharacteristicProperties::Write)) writeMode = GattWriteOption::WriteWithResponse;
        else check((flags & static_cast<unsigned>(GattCharacteristicProperties::WriteWithoutResponse)) != 0, unavailable);
        const auto mtu = service.Session().MaxPduSize();
        check(mtu >= 23, unavailable);
        payloadLimit = mtu - 3u - 15u;
        auto inbox = incoming;
        notification = rx.ValueChanged(winrt::auto_revoke, [inbox](const auto&, const GattValueChangedEventArgs& event) {
            try {
                auto reader = DataReader::FromBuffer(event.CharacteristicValue());
                Bytes chunk(reader.UnconsumedBufferLength());
                reader.ReadBytes(chunk);
                std::lock_guard guard(inbox->lock);
                if (chunk.size() + inbox->bytes.size() <= 8192)
                    inbox->bytes.insert(inbox->bytes.end(), chunk.begin(), chunk.end());
                else inbox->overflow = true;
                inbox->changed.notify_all();
            } catch (const winrt::hresult_error&) {}
        });
        check(finish(rx.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue::Notify)) == GattCommunicationStatus::Success, unavailable);
    }
    Bytes exchange(const Bytes& request) {
        checkRunning();
        {
            std::lock_guard guard(incoming->lock);
            incoming->bytes.clear();
            incoming->overflow = false;
        }
        DataWriter writer;
        writer.WriteBytes(request);
        check(finish(tx.WriteValueAsync(writer.DetachBuffer(), writeMode)) == GattCommunicationStatus::Success,
              TRANS("Could not send the Bluetooth request."));
        std::unique_lock lock(incoming->lock);
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        for (;;) {
            const auto& bytes = incoming->bytes;
            if (incoming->overflow) throw TransferError(Failure::protocol);
            if (bytes.size() >= 6) {
                const auto size = 7 + little(std::span(bytes).subspan(3, 3));
                if (bytes[0] != 0 || bytes[1] != 0x59 || size > 4096 || bytes.size() > size)
                    throw TransferError(Failure::protocol);
                if (bytes.size() == size) return bytes;
            }
            checkRunning();
            check(std::chrono::steady_clock::now() < deadline, TRANS("No settings reply from the controller."));
            incoming->changed.wait_for(lock, 50ms);
        }
    }
    unsigned payloadLimit = 0;
private:
    BluetoothLEDevice device{nullptr};
    GattDeviceService service{nullptr};
    GattCharacteristic tx{nullptr}, rx{nullptr};
    GattWriteOption writeMode = GattWriteOption::WriteWithoutResponse;
    std::shared_ptr<Inbox> incoming = std::make_shared<Inbox>();
    GattCharacteristic::ValueChanged_revoker notification;
};
juce::String describe(const winrt::hresult_error& e) {
    return TRANS("Bluetooth error") + " (" + juce::String::toHexString(int(e.code())) + ") " + juce::String(e.message().c_str());
}
}

Result discover() {
    Result result;
    try {
        Apartment apartment;
        auto properties = winrt::single_threaded_vector<winrt::hstring>({L"System.Devices.Aep.DeviceAddress"});
        auto paired = finish(DeviceInformation::FindAllAsync(BluetoothLEDevice::GetDeviceSelectorFromPairingState(true),
                                                             properties, DeviceInformationKind::AssociationEndpoint));
        for (const auto& info : paired) {
            auto value = info.Properties().TryLookup(L"System.Devices.Aep.DeviceAddress");
            if (!value) continue;
            const juce::String address(winrt::unbox_value<winrt::hstring>(value).c_str());
            addDevice(result.devices, info.Name().c_str(), static_cast<std::uint64_t>(address.retainCharacters("0123456789abcdefABCDEF").getHexValue64()));
        }
        using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
        struct Found { std::mutex lock; std::vector<Device> devices; };
        auto found = std::make_shared<Found>();
        BluetoothLEAdvertisementWatcher watcher;
        watcher.ScanningMode(BluetoothLEScanningMode::Active);
        auto subscription = watcher.Received(winrt::auto_revoke, [found](const auto&, const BluetoothLEAdvertisementReceivedEventArgs& e) {
            try {
                std::lock_guard guard(found->lock);
                addDevice(found->devices, e.Advertisement().LocalName().c_str(), e.BluetoothAddress());
            } catch (const winrt::hresult_error&) {}
        });
        watcher.Start();
        for (int i = 0; i < 30; ++i) { checkRunning(); std::this_thread::sleep_for(100ms); }
        watcher.Stop();
        std::lock_guard guard(found->lock);
        for (const auto& d : found->devices) addDevice(result.devices, d.name, d.address);
        if (result.devices.empty()) result.message = TRANS("No SMK-25 II found. Turn on its Bluetooth and scan again.");
    } catch (const Issue& e) { result.message = e.text; }
      catch (const winrt::hresult_error& e) { result.message = describe(e); }
    return result;
}
Result transfer(const Device& device, int slot, const std::optional<Smk25Preset>& edited, bool studioSetup) {
    Result result;
    try {
        Apartment apartment;
        Link link(device.address);
        const Exchange exchange = [&](const auto& data) { return link.exchange(data); };
        identify(exchange);
        const auto previous = readPreset(exchange, slot);
        result.preset = Smk25Preset::read(previous);
        check(result.preset.has_value(), TRANS("The controller returned an unrecognised preset."));
        const auto toWrite = studioSetup ? std::optional(result.preset->forStudioSlot(slot)) : edited;
        if (toWrite && previous != toWrite->bytes()) {
            auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("OSTF Studio").getChildFile("Controller Backups");
            check(folder.createDirectory().wasOk(), TRANS("Could not save the backup of the device preset."));
            auto backup = folder.getNonexistentChildFile("SMK25II-" + juce::String::toHexString(juce::int64(device.address))
                + "-" + juce::String(slot + 1) + "-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"), ".mkcII", false);
            check(backup.replaceWithData(previous.data(), previous.size()), TRANS("Could not save the backup of the device preset."));
            result.backup = backup;
            writePreset(exchange, slot, previous, toWrite->bytes(), link.payloadLimit);
            result.preset = toWrite;
        }
    } catch (const TransferError& e) {
        result.message = e.reason == Failure::model ? TRANS("The device did not identify itself as an SMK-25 II.")
            : e.reason == Failure::verify ? TRANS("The preset read back from the device did not match.")
            : TRANS("Unexpected reply from the controller.");
    } catch (const Issue& e) { result.message = e.text; }
      catch (const winrt::hresult_error& e) { result.message = describe(e); }
    return result;
}
}
#endif
