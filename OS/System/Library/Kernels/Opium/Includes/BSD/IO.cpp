#pragma once

#include "Std.cpp"

namespace BSD::IO {
    using DeviceID = uint32_t;

    int nextMajorID = 1;
    unordered_map<int, int> nextMinorForMajor;
    EventHandler<DeviceID, bool> deviceEventHandler;  // isCharacter

    DeviceID createDeviceID(int majorID, int minorID) {
        return ((majorID & 0xFF) << 8) | (minorID & 0xFF);
    }

    int getMajorID(DeviceID d) {
        return (d >> 8) & 0xFF;
    }

    int getMinorID(DeviceID d) {
        return d & 0xFF;
    }

    DeviceID allocateDeviceID() {
        int majorID = nextMajorID++;
        int minorID = 0;
        nextMinorForMajor[majorID] = 1;

        return createDeviceID(majorID, minorID);
    }

    DeviceID allocateDeviceID(int majorID) {
        int minorID = nextMinorForMajor[majorID]++;

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

    unordered_map<int, shared_ptr<BlockDeviceSwitch>> blockDeviceSwitches;
    unordered_map<DeviceID, shared_ptr<BlockDevice>> blockDevices;

    void addBlockDeviceSwitch(int majorID, shared_ptr<BlockDeviceSwitch> sw) {
        blockDeviceSwitches[majorID] = sw;
    }

    shared_ptr<BlockDevice> addBlockDevice(DeviceID ID, const string& name) {
        int majorID = getMajorID(ID);

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

    unordered_map<int, shared_ptr<CharacterDeviceSwitch>> characterDeviceSwitches;
    unordered_map<DeviceID, shared_ptr<CharacterDevice>> characterDevices;

    void addCharacterDeviceSwitch(int majorID, shared_ptr<CharacterDeviceSwitch> sw) {
        characterDeviceSwitches[majorID] = sw;
    }

    shared_ptr<CharacterDevice> addCharacterDevice(DeviceID ID, const string& name) {
        int majorID = getMajorID(ID);

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