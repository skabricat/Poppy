#pragma once

#include "Std.cpp"
#include "EventHandler.cpp"

namespace BSD::IO {
    using DeviceID = uint64_t;
    using MajorID = uint32_t;
    using MinorID = uint32_t;

    MajorID nextMajorID = 1;
    unordered_map<MajorID, MinorID> nextMinorForMajor;
    EventHandler<DeviceID, bool> deviceEventHandler;  // isCharacter

    DeviceID createDeviceID(MajorID majorID, MinorID minorID) {
        return (static_cast<uint64_t>(majorID) << 32) | minorID;
    }

    MajorID getMajorID(DeviceID deviceID) {
        return deviceID >> 32;
    }

    MinorID getMinorID(DeviceID deviceID) {
        return deviceID & 0xFFFFFFFF;
    }

    DeviceID allocateDeviceID() {
        MajorID majorID = nextMajorID++;
        MinorID minorID = 0;
        nextMinorForMajor[majorID] = 1;

        return createDeviceID(majorID, minorID);
    }

    DeviceID allocateDeviceID(MajorID majorID) {
        MinorID minorID = nextMinorForMajor[majorID]++;

        return createDeviceID(majorID, minorID);
    }

    // ----------------------------------------------------------------

    struct BlockDeviceSwitch {
        function<int(DeviceID)> open;
        function<int(DeviceID)> close;
        function<int(DeviceID, char*, size_t)> strategy;
    };

    struct BlockDevice {
        DeviceID ID;
        shared_ptr<BlockDeviceSwitch> sw;
        string name;
    };

    unordered_map<MajorID, shared_ptr<BlockDeviceSwitch>> blockDeviceSwitches;
    unordered_map<DeviceID, shared_ptr<BlockDevice>> blockDevices;

    void addBlockDeviceSwitch(int majorID, shared_ptr<BlockDeviceSwitch> sw) {
        blockDeviceSwitches[majorID] = sw;
    }

    shared_ptr<BlockDevice> addBlockDevice(DeviceID ID, const string& name) {
        MajorID majorID = getMajorID(ID);

        auto it = blockDeviceSwitches.find(majorID);
        if(it == blockDeviceSwitches.end())
            throw runtime_error("Device switch is not registered");

        auto device = make_shared<BlockDevice>(BlockDevice { ID, it->second, name });
        blockDevices[ID] = device;
        deviceEventHandler.notify(ID, false);
        return device;
    }

    shared_ptr<BlockDevice> getBlockDevice(DeviceID ID) {
        auto it = blockDevices.find(ID);
        return (it != blockDevices.end()) ? it->second : nullptr;
    }

    int open(const BlockDevice& device) {
        return device.sw->open(device.ID);
    }

    int close(const BlockDevice& device) {
        return device.sw->close(device.ID);
    }

    int strategy(const BlockDevice& device, char* buf, size_t len) {
        return device.sw->strategy(device.ID, buf, len);
    }

    // ----------------------------------------------------------------

    struct CharacterDeviceSwitch {
        function<int(DeviceID)> open;
        function<int(DeviceID)> close;
        function<string(DeviceID)> read;
        function<int(DeviceID, const string&)> write;
    };

    struct CharacterDevice {
        DeviceID ID;
        shared_ptr<CharacterDeviceSwitch> sw;
        string name;
    };

    unordered_map<MajorID, shared_ptr<CharacterDeviceSwitch>> characterDeviceSwitches;
    unordered_map<DeviceID, shared_ptr<CharacterDevice>> characterDevices;

    void addCharacterDeviceSwitch(MajorID majorID, shared_ptr<CharacterDeviceSwitch> sw) {
        characterDeviceSwitches[majorID] = sw;
    }

    shared_ptr<CharacterDevice> addCharacterDevice(DeviceID ID, const string& name) {
        MajorID majorID = getMajorID(ID);

        auto it = characterDeviceSwitches.find(majorID);
        if(it == characterDeviceSwitches.end())
            throw runtime_error("Device switch is not registered");

        auto device = make_shared<CharacterDevice>(CharacterDevice { ID, it->second, name });
        characterDevices[ID] = device;
        deviceEventHandler.notify(ID, true);
        return device;
    }

    shared_ptr<CharacterDevice> getCharacterDevice(DeviceID ID) {
        auto it = characterDevices.find(ID);
        return (it != characterDevices.end()) ? it->second : nullptr;
    }

    int open(const CharacterDevice& device) {
        return device.sw->open(device.ID);
    }

    int close(const CharacterDevice& device) {
        return device.sw->close(device.ID);
    }

    string read(const CharacterDevice& device) {
        return device.sw->read(device.ID);
    }

    int write(const CharacterDevice& device, const string& s) {
        return device.sw->write(device.ID, s);
    }
}