#include "Std.cpp"
#include "KPL/KPL.cpp"
#include "IOKit.cpp"
#include "VFS.cpp"
#include "KEM/KEM.cpp"

namespace Opium {};

using namespace IOKit;
using namespace VFS;
using namespace KEM;

string sys_read(VFSManager& vfs, const string& path) {
    auto vnode = vfs.lookup(path);
    if (!vnode) return "";
    if (vnode->type == VType::VCHR || vnode->type == VType::VBLK) {
        auto dev = vnode->dev;
        return dev->ops->read(dev->minor);
    }
    if (vnode->ops && vnode->ops->read) return vnode->ops->read();
    return "";
}

void sys_write(VFSManager& vfs, const string& path, const string& data) {
    auto vnode = vfs.lookup(path);
    if (!vnode) return;
    if (vnode->type == VType::VCHR || vnode->type == VType::VBLK) {
        auto dev = vnode->dev;
        dev->ops->write(dev->minor, data);
    } else if (vnode->ops && vnode->ops->write) {
        vnode->ops->write(data);
    }
}

int main() {
    // === 1. Создаём устройства ===
    auto& console = registerDevice<IOConsole>(1, "console0");
    auto& fb0 = registerDevice<IOFrameBuffer>(2, "fb0");
    auto& serial = registerDevice<IOSerialDevice>(3, "tty.serial0");
    serial.connect();

    // Консоль выводит на framebuffer
    console.addTarget(fb0.id);

    // === 2. Создаём TTY ===
    TTY tty;
    tty.ID = 100;

    // === 3. Подключаем уведомления ===
    // Устройство -> TTY (ввод с устройства в tty)
    serial.onDataReceived.push_back([&](const string& data) {
        tty.pushInput(data);
    });

    // TTY -> Устройство (когда процесс пишет в tty)
    tty.onOutput.push_back([&](const string& data) {
        serial.write(data);
    });

    // Также можно логировать всё, что уходит в serial
    serial.onDataSent.push_back([&](const string& data) {
        console.write("[serial TX] " + data);
    });

    // === 4. Демонстрация ===
    // Система пишет что-то на консоль
    console.write("Kernel boot OK\n");

    // Процесс пишет в свой tty (это автоматически вызывает serial.write)
    tty.write("shell> ls -la\n");

    // Устройство “принимает” ответ с линии (например, echo)
    serial.pushFromHardware("result: file1 file2\n");

    // Теперь процесс читает, что ему пришло в tty:
    cout << "TTY read(): " << tty.read() << endl;

    // Смотрим, что накопилось на framebuffer:
    cout << "\nFramebuffer content:\n" << fb0.read() << endl;



    // === 2. Создаём таблицу функций драйвера ===
    auto serialOps = make_shared<DevSW>();
    serialOps->read = [&](int minor) -> string {
        return getDevice(3)->read();
    };
    serialOps->write = [&](int minor, const string& s) -> int {
        getDevice(3)->write(s);
        return 0;
    };

    // === 3. Создаём и монтируем DevFS ===
    auto devfs = make_shared<DevFS>();
    devfs->registerCDev(4, 0, "tty0", serialOps);

    VFSManager vfs;
    vfs.mountFS("/dev", devfs);

    // === 5. Демонстрация ===
    cout << "Writing to /dev/tty0 via VFS...\n";
    sys_write(vfs, "/dev/tty0", "echo test\n");

    serial.pushFromHardware("ok\n");

    cout << "Reading from TTY buffer: " << tty.read() << endl;

    cout << "Reading directly from /dev/tty0 via VFS: "
         << sys_read(vfs, "/dev/tty0") << endl;

    return 0;
}