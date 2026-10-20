#include <algorithm>
#include <fstream>
#include <stdlib.h>

#include "brainflow_constants.h"
#include "serial_ports.h"

#ifdef __linux__
#include <dirent.h>
#include <limits.h>
#endif


std::vector<SerialPortInfo> list_serial_ports ()
{
    return std::vector<SerialPortInfo> ();
}

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

static bool compare_ports (const SerialPortInfo &a, const SerialPortInfo &b)
{
    return a.port < b.port;
}

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
