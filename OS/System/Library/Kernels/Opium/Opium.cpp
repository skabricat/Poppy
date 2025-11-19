#include "Std.cpp"
#include "KPL/KPL.cpp"
#include "Mach/Mach.cpp"
#include "BSD/BSD.cpp"
#include "IOKit/IOKit.cpp"
#include "KEM/KEM.cpp"

namespace Opium {};

int main() {
    auto& console = IOKit::addService<IOKit::Services::IOConsole>();
    auto& fb0     = IOKit::addService<IOKit::Services::IOFramebuffer>();
    auto& serial  = IOKit::addService<IOKit::Services::IOSerial>();
    IOKit::matchAndStartDevices();
    IOKit::dumpRegistry(IOKit::rootService);

    auto& terminal = BSD::TTY::createTerminal(100);
    auto serialID = IOKit::getBSDDeviceID(&serial);
    terminal.attachDevice(serialID);
    serial.onDataReceived.push_back([&](const string& data) { terminal.pushInput(data); });
    serial.onDataSent.push_back([&](const string& data) { console.write("[serial TX] " + data); });

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