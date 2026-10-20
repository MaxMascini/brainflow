#include <algorithm>
#include <ctype.h>
#include <fstream>
#include <stdlib.h>

#include "brainflow_constants.h"
#include "serial_ports.h"

#ifdef _WIN32
#include <windows.h>

#include <setupapi.h>
#pragma comment(lib, "Setupapi.lib")
#endif

#ifdef __linux__
#include <dirent.h>
#include <limits.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE
#define BRAINFLOW_MACOS_SERIAL
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <limits.h>
#endif
#endif


static bool compare_ports (const SerialPortInfo &a, const SerialPortInfo &b)
{
    return a.port < b.port;
}

static bool parse_hex_after (const std::string &str, const std::string &prefix, int &value)
{
    size_t pos = str.find (prefix);
    if ((pos == std::string::npos) || (pos + prefix.size () + 4 > str.size ()))
    {
        return false;
    }
    std::string digits = str.substr (pos + prefix.size (), 4);
    char *end = NULL;
    long res = strtol (digits.c_str (), &end, 16);
    if (*end != '\0')
    {
        return false;
    }
    value = (int)res;
    return true;
}

bool parse_hardware_id (const std::string &hw_id, int &vid, int &pid)
{
    std::string upper = hw_id;
    std::transform (upper.begin (), upper.end (), upper.begin (), ::toupper);
    return parse_hex_after (upper, "VID_", vid) && parse_hex_after (upper, "PID_", pid);
}

#ifdef _WIN32
static std::vector<SerialPortInfo> list_serial_ports_windows ()
{
    std::vector<SerialPortInfo> ports;
    // guid of ports device class
    static const GUID ports_guid = {
        0x4d36e978, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
    HDEVINFO dev_info = SetupDiGetClassDevsA (&ports_guid, NULL, NULL, DIGCF_PRESENT);
    if (dev_info == INVALID_HANDLE_VALUE)
    {
        return ports;
    }
    SP_DEVINFO_DATA data;
    data.cbSize = sizeof (data);
    for (DWORD i = 0; SetupDiEnumDeviceInfo (dev_info, i, &data); i++)
    {
        char hw_id[1024] = {0};
        SerialPortInfo info;
        if ((!SetupDiGetDeviceRegistryPropertyA (
                dev_info, &data, SPDRP_HARDWAREID, NULL, (PBYTE)hw_id, sizeof (hw_id) - 1, NULL)) ||
            (!parse_hardware_id (hw_id, info.vid, info.pid)))
        {
            continue;
        }
        HKEY key = SetupDiOpenDevRegKey (dev_info, &data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE)
        {
            continue;
        }
        char name[256] = {0};
        DWORD size = sizeof (name) - 1;
        DWORD type = 0;
        LONG res = RegQueryValueExA (key, "PortName", NULL, &type, (LPBYTE)name, &size);
        RegCloseKey (key);
        if ((res != ERROR_SUCCESS) || (type != REG_SZ))
        {
            continue;
        }
        info.port = name;
        ports.push_back (info);
    }
    SetupDiDestroyDeviceInfoList (dev_info);
    return ports;
}
#endif

#ifdef BRAINFLOW_MACOS_SERIAL
static bool get_usb_property (io_object_t service, CFStringRef key, int &value)
{
    // usb ids are stored in parent usb device
    CFTypeRef ref = IORegistryEntrySearchCFProperty (service, kIOServicePlane, key,
        kCFAllocatorDefault, kIORegistryIterateRecursively | kIORegistryIterateParents);
    if (ref == NULL)
    {
        return false;
    }
    bool res = (CFGetTypeID (ref) == CFNumberGetTypeID ()) &&
        CFNumberGetValue ((CFNumberRef)ref, kCFNumberIntType, &value);
    CFRelease (ref);
    return res;
}

static std::vector<SerialPortInfo> list_serial_ports_macos ()
{
    std::vector<SerialPortInfo> ports;
    io_iterator_t iter;
    // IOServiceGetMatchingServices releases matching dict
    if (IOServiceGetMatchingServices (
            MACH_PORT_NULL, IOServiceMatching (kIOSerialBSDServiceValue), &iter) != KERN_SUCCESS)
    {
        return ports;
    }
    io_object_t service;
    while ((service = IOIteratorNext (iter)) != 0)
    {
        SerialPortInfo info;
        char path[PATH_MAX] = {0};
        // use callout device /dev/cu.*, /dev/tty.* blocks on open
        CFTypeRef ref = IORegistryEntryCreateCFProperty (
            service, CFSTR (kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
        bool has_path = (ref != NULL) && (CFGetTypeID (ref) == CFStringGetTypeID ()) &&
            CFStringGetCString ((CFStringRef)ref, path, sizeof (path), kCFStringEncodingUTF8);
        if (ref != NULL)
        {
            CFRelease (ref);
        }
        if (has_path && get_usb_property (service, CFSTR ("idVendor"), info.vid) &&
            get_usb_property (service, CFSTR ("idProduct"), info.pid))
        {
            info.port = path;
            ports.push_back (info);
        }
        IOObjectRelease (service);
    }
    IOObjectRelease (iter);
    return ports;
}
#endif

std::vector<SerialPortInfo> list_serial_ports ()
{
    std::vector<SerialPortInfo> ports;
#if defined(_WIN32)
    ports = list_serial_ports_windows ();
#elif defined(BRAINFLOW_MACOS_SERIAL)
    ports = list_serial_ports_macos ();
#elif defined(__linux__)
    ports = list_serial_ports_sysfs ("/sys/class/tty");
#endif
    std::sort (ports.begin (), ports.end (), compare_ports);
    return ports;
}

#ifdef __linux__
static bool read_hex_file (const std::string &path, int &value)
{
    std::ifstream file (path);
    std::string content;
    if (!(file >> content))
    {
        return false;
    }
    value = (int)strtol (content.c_str (), NULL, 16);
    return true;
}
#endif

std::vector<SerialPortInfo> list_serial_ports_sysfs (const std::string &tty_dir)
{
    std::vector<SerialPortInfo> ports;
#ifdef __linux__
    DIR *dir = opendir (tty_dir.c_str ());
    if (dir == NULL)
    {
        return ports;
    }
    struct dirent *entry;
    while ((entry = readdir (dir)) != NULL)
    {
        std::string name = entry->d_name;
        if (name == "." || name == "..")
        {
            continue;
        }
        char resolved[PATH_MAX];
        if (realpath ((tty_dir + "/" + name + "/device").c_str (), resolved) == NULL)
        {
            continue;
        }
        // usb ids are in the usb device dir, 1 level above for cdc acm, 2 for usb serial
        std::string path = resolved;
        for (int level = 0; level <= 3; level++)
        {
            SerialPortInfo info;
            if (read_hex_file (path + "/idVendor", info.vid) &&
                read_hex_file (path + "/idProduct", info.pid))
            {
                info.port = "/dev/" + name;
                ports.push_back (info);
                break;
            }
            size_t pos = path.find_last_of ('/');
            if (pos == std::string::npos || pos == 0)
            {
                break;
            }
            path = path.substr (0, pos);
        }
    }
    closedir (dir);
    std::sort (ports.begin (), ports.end (), compare_ports);
#endif
    return ports;
}

std::vector<std::string> filter_serial_ports (
    const std::vector<SerialPortInfo> &ports, const std::vector<UsbId> &ids)
{
    std::vector<std::string> res;
    for (const SerialPortInfo &info : ports)
    {
        for (const UsbId &id : ids)
        {
            if ((info.vid == id.vid) && (info.pid == id.pid))
            {
                res.push_back (info.port);
                break;
            }
        }
    }
    return res;
}

std::vector<UsbId> get_board_usb_ids (int board_id)
{
    switch (static_cast<BoardIds> (board_id))
    {
        // ftdi ft231x dongle
        case BoardIds::CYTON_BOARD:
        case BoardIds::CYTON_DAISY_BOARD:
            return {{0x0403, 0x6015}};
        // silicon labs bled112 dongle
        case BoardIds::GANGLION_BOARD:
        case BoardIds::BRAINBIT_BLED_BOARD:
        case BoardIds::MUSE_S_BLED_BOARD:
        case BoardIds::MUSE_2_BLED_BOARD:
        case BoardIds::MUSE_2016_BLED_BOARD:
            return {{0x2458, 0x0001}};
        default:
            return {};
    }
}

int pick_serial_port (const std::vector<std::string> &candidates, std::string &port)
{
    if (candidates.size () != 1)
    {
        return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
    }
    port = candidates[0];
    return (int)BrainFlowExitCodes::STATUS_OK;
}
