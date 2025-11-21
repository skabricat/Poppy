#include "KPL/KPL.cpp"
#include "Mach/Mach.cpp"
#include "BSD/BSD.cpp"
#include "IOKit/IOKit.cpp"
#include "KEM/KEM.cpp"

int main() {
    auto& console = IOKit::addService<IOKit::Services::IOConsole>();
    auto& fb0     = IOKit::addService<IOKit::Services::IOFramebuffer>();
    auto& serial  = IOKit::addService<IOKit::Services::IOSerial>();
    IOKit::matchAndStartDevices();

    auto& terminal = BSD::TTY::createTerminal(100);
    auto serialID = IOKit::getBSDDeviceID(&serial);
    terminal.attachDevice(serialID);
    serial.onDataReceived.push_back([&](const string& data) { terminal.pushInput(data); });
    serial.onDataSent.push_back([&](const string& data) { console.write("[serial TX] "+data); });

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

    console.write("Single-user mode started\n");
    string inputLine;

    while(true) {
        serial.write("shell> ");
        getline(cin, inputLine);
        if(inputLine == "exit") {
            break;
        } else
        if(inputLine == "help") {
            serial.pushFromHardware("help, exit, iotree, ls\n");
        } else
        if(inputLine == "iotree") {
            IOKit::dumpRegistry(IOKit::rootService);
            continue;
        } else
        if(inputLine.starts_with("ls ") && inputLine.size() > 3) {
            string path = inputLine.substr(3);
            serial.pushFromHardware("Listing: "+path+"\n");
            for(auto& dirent : BSD::VFS::readdir(path)) {
                serial.pushFromHardware(dirent.name+"\n");
            }
        } else {
            serial.pushFromHardware(inputLine+"\n");
        }
        string output = terminal.read();
        serial.write(output);
    }

    return 0;
}