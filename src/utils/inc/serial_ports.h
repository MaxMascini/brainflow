#pragma once

#include <string>
#include <vector>


struct UsbId
{
    int vid;
    int pid;
};

struct SerialPortInfo
{
    std::string port;
    int vid;
    int pid;
};

// list usb serial ports of this machine with their usb ids
std::vector<SerialPortInfo> list_serial_ports ();
// parses vid and pid from windows hardware id like USB\VID_0403&PID_6015
bool parse_hardware_id (const std::string &hw_id, int &vid, int &pid);
// linux implementation, tty_dir is usually /sys/class/tty
std::vector<SerialPortInfo> list_serial_ports_sysfs (const std::string &tty_dir);
std::vector<std::string> filter_serial_ports (
    const std::vector<SerialPortInfo> &ports, const UsbId &id);
// usb id of the serial device used by board, {-1, -1} if board cant be autodetected
UsbId get_board_usb_id (int board_id);
// returns OK and sets port if there is exactly one candidate
int pick_serial_port (const std::vector<std::string> &candidates, std::string &port);
