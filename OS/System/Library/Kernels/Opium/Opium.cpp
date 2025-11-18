#include "Std.cpp"
#include "KPL/KPL.cpp"
#include "IOKit/IOKit.cpp"
#include "Mach/Mach.cpp"
#include "BSD/BSD.cpp"
#include "KEM/KEM.cpp"

namespace Opium {};

int main() {
    auto& console = IOKit::registerDevice<IOKit::IOConsole>(1, "console0");
    auto& fb0 = IOKit::registerDevice<IOKit::IOFrameBuffer>(2, "fb0");
    int serialMajorID = 3;
    int serialMinorID = 0;
    auto deviceID = BSD::IO::createDeviceID(serialMajorID, serialMinorID);
    auto& serial = IOKit::registerDevice<IOKit::IOSerialDevice>(serialMajorID, "tty.serial0");
    serial.connect();
    console.addTarget(fb0.ID);

    auto serialDeviceSwitch = make_shared<BSD::IO::CharacterDeviceSwitch>();
    serialDeviceSwitch->write = [&](BSD::IO::DeviceID device, const string& s){ serial.write(s); return 0; };
    serialDeviceSwitch->read = [&](BSD::IO::DeviceID device){ return serial.read(); };
    BSD::IO::addCharacterDeviceSwitch(serialMajorID, serialDeviceSwitch);
    BSD::IO::addCharacterDevice(deviceID, "tty0");

    auto& terminal = BSD::TTY::createTerminal(100);
    terminal.attachDevice(deviceID);
    serial.onDataReceived.push_back([&](const string& data) {
        terminal.pushInput(data);
    });
    serial.onDataSent.push_back([&](const string& data) {
        console.write("[serial TX] " + data);
    });

    console.write("Kernel boot OK\n");
    terminal.write("shell> ls -la\n");
    serial.pushFromHardware("result: file1 file2\n");
    cout << "TTY read(): " << terminal.read() << endl;
    cout << "\nFramebuffer content:\n" << fb0.read() << endl;

    BSD::VFS::addVirtualFileSystem(BSD::VFS::DeviceFS::deviceFileSystem);
    BSD::VFS::mount("/dev", "devfs");

    cout << "Writing to /dev/tty0 via VFS...\n";
    BSD::VFS::write("/dev/tty0", "echo test\n");
    serial.pushFromHardware("ok\n");
    cout << "Reading from TTY buffer: " << terminal.read() << endl;
    cout << "Reading directly from /dev/tty0 via VFS: "
         << BSD::VFS::read("/dev/tty0") << endl;

    return 0;
}