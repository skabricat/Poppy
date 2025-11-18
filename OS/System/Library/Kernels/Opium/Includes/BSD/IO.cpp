#pragma once

namespace BSD::IO {
    using DeviceID = uint32_t;

    struct BlockDeviceSwitch {};  // TODO

    struct CharacterDeviceSwitch {
        function<int(DeviceID)> open;
        function<int(DeviceID)> close;
        function<string(DeviceID)> read;
        function<int(DeviceID, const string&)> write;
    };

    unordered_map<int, shared_ptr<BlockDeviceSwitch>> blockDeviceSwitches;
    unordered_map<int, shared_ptr<CharacterDeviceSwitch>> characterDeviceSwitches;

    int getMajorID(DeviceID d) {
        return (d >> 8) & 0xFF;
    }

    int getMinorID(DeviceID d) {
        return d & 0xFF;
    }

    DeviceID createDeviceID(int majorID, int minorID) {
        return ((majorID & 0xFF) << 8) | (minorID & 0xFF);
    }

    void addBlockDeviceSwitch(int majorID, shared_ptr<BlockDeviceSwitch> sw) {
        blockDeviceSwitches[majorID] = sw;
    }

    void addCharacterDeviceSwitch(int majorID, shared_ptr<CharacterDeviceSwitch> sw) {
        characterDeviceSwitches[majorID] = sw;
    }
}