#include <gmock/gmock.h>
#include <string>
#include <vector>

#include "brainflow_constants.h"
#include "serial_ports.h"

#ifdef __linux__
#include <fstream>
#include <ftw.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace testing;


TEST (SerialPortsTest, FilterSerialPorts_MatchingVidAndPid_ReturnsMatchingPortsInOrder)
{
    std::vector<SerialPortInfo> ports = {
        {"COM3", 0x0403, 0x6015}, {"COM4", 0x2458, 0x0001}, {"COM7", 0x0403, 0x6015}};

    std::vector<std::string> res = filter_serial_ports (ports, {{0x0403, 0x6015}});

    EXPECT_THAT (res, ElementsAre ("COM3", "COM7"));
}

TEST (SerialPortsTest, FilterSerialPorts_SameVidDifferentPid_IsExcluded)
{
    std::vector<SerialPortInfo> ports = {{"COM3", 0x0403, 0x6001}, {"COM4", 0x1234, 0x6015}};

    std::vector<std::string> res = filter_serial_ports (ports, {{0x0403, 0x6015}});

    EXPECT_THAT (res, IsEmpty ());
}

TEST (SerialPortsTest, FilterSerialPorts_SeveralIds_MatchesAnyOfThem)
{
    std::vector<SerialPortInfo> ports = {{"COM3", 0x0403, 0x6015}, {"COM4", 0x2458, 0x0001}};

    std::vector<std::string> res =
        filter_serial_ports (ports, {{0x2458, 0x0001}, {0x0403, 0x6015}});

    EXPECT_THAT (res, ElementsAre ("COM3", "COM4"));
}

TEST (SerialPortsTest, GetBoardUsbIds_CytonAndGanglion_SelectTheirOwnDongles)
{
    std::vector<SerialPortInfo> ports = {
        {"/dev/ttyUSB0", 0x0403, 0x6015}, {"/dev/ttyACM0", 0x2458, 0x0001}};

    EXPECT_THAT (filter_serial_ports (ports, get_board_usb_ids ((int)BoardIds::CYTON_BOARD)),
        ElementsAre ("/dev/ttyUSB0"));
    EXPECT_THAT (filter_serial_ports (ports, get_board_usb_ids ((int)BoardIds::CYTON_DAISY_BOARD)),
        ElementsAre ("/dev/ttyUSB0"));
    EXPECT_THAT (filter_serial_ports (ports, get_board_usb_ids ((int)BoardIds::GANGLION_BOARD)),
        ElementsAre ("/dev/ttyACM0"));
}

TEST (SerialPortsTest, GetBoardUsbIds_BoardWithoutSerialPort_ReturnsEmpty)
{
    EXPECT_THAT (get_board_usb_ids ((int)BoardIds::SYNTHETIC_BOARD), IsEmpty ());
}

TEST (SerialPortsTest, PickSerialPort_SingleCandidate_ReturnsOkAndSetsPort)
{
    std::string port;

    int res = pick_serial_port ({"COM3"}, port);

    EXPECT_EQ (res, (int)BrainFlowExitCodes::STATUS_OK);
    EXPECT_EQ (port, "COM3");
}

TEST (SerialPortsTest, PickSerialPort_NoCandidates_ReturnsErrorAndKeepsPortEmpty)
{
    std::string port;

    int res = pick_serial_port ({}, port);

    EXPECT_EQ (res, (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR);
    EXPECT_EQ (port, "");
}

TEST (SerialPortsTest, PickSerialPort_SeveralCandidates_ReturnsErrorAndKeepsPortEmpty)
{
    std::string port;

    int res = pick_serial_port ({"COM3", "COM7"}, port);

    EXPECT_EQ (res, (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR);
    EXPECT_EQ (port, "");
}

TEST (SerialPortsTest, ParseHardwareId_UsbStyle_ExtractsIds)
{
    int vid = -1;
    int pid = -1;

    EXPECT_TRUE (parse_hardware_id ("USB\\VID_0403&PID_6015&REV_1000", vid, pid));
    EXPECT_EQ (vid, 0x0403);
    EXPECT_EQ (pid, 0x6015);
}

TEST (SerialPortsTest, ParseHardwareId_FtdiBusStyle_ExtractsIds)
{
    int vid = -1;
    int pid = -1;

    EXPECT_TRUE (parse_hardware_id ("FTDIBUS\\COMPORT&VID_0403&PID_6015", vid, pid));
    EXPECT_EQ (vid, 0x0403);
    EXPECT_EQ (pid, 0x6015);
}

TEST (SerialPortsTest, ParseHardwareId_LowerCaseHex_ExtractsIds)
{
    int vid = -1;
    int pid = -1;

    EXPECT_TRUE (parse_hardware_id ("usb\\vid_2458&pid_000a", vid, pid));
    EXPECT_EQ (vid, 0x2458);
    EXPECT_EQ (pid, 0x000a);
}

TEST (SerialPortsTest, ParseHardwareId_NoIds_ReturnsFalse)
{
    int vid = -1;
    int pid = -1;

    EXPECT_FALSE (parse_hardware_id ("ACPI\\PNP0501", vid, pid));
}

TEST (SerialPortsTest, ListSerialPorts_RealMachine_EveryEntryHasUsbIds)
{
    std::vector<SerialPortInfo> ports = list_serial_ports ();

    for (const SerialPortInfo &info : ports)
    {
        EXPECT_FALSE (info.port.empty ());
        EXPECT_GE (info.vid, 0);
        EXPECT_GE (info.pid, 0);
    }
}

#ifdef __APPLE__
TEST (SerialPortsTest, ListSerialPorts_OnMac_ReturnsCalloutDevices)
{
    std::vector<SerialPortInfo> ports = list_serial_ports ();

    for (const SerialPortInfo &info : ports)
    {
        EXPECT_EQ (info.port.rfind ("/dev/cu.", 0), 0u) << info.port;
    }
}
#endif

#ifdef __linux__

static int remove_entry (const char *path, const struct stat *, int, struct FTW *)
{
    return remove (path);
}

class SysfsTest : public Test
{
protected:
    std::string root;

    void SetUp () override
    {
        char tmpl[] = "/tmp/brainflow_sysfs_XXXXXX";
        root = mkdtemp (tmpl);
    }

    void TearDown () override
    {
        nftw (root.c_str (), remove_entry, 16, FTW_DEPTH | FTW_PHYS);
    }

    void make_dirs (const std::string &rel)
    {
        std::string path = root;
        size_t start = 0;
        while (start < rel.size ())
        {
            size_t end = rel.find ('/', start);
            if (end == std::string::npos)
            {
                end = rel.size ();
            }
            path += "/" + rel.substr (start, end - start);
            mkdir (path.c_str (), 0755);
            start = end + 1;
        }
    }

    void write_file (const std::string &rel, const std::string &content)
    {
        std::ofstream (root + "/" + rel) << content;
    }

    void add_tty (const std::string &name, const std::string &device_rel)
    {
        make_dirs ("class/tty/" + name);
        if (!device_rel.empty ())
        {
            ASSERT_EQ (0,
                symlink ((root + "/" + device_rel).c_str (),
                    (root + "/class/tty/" + name + "/device").c_str ()));
        }
    }
};

TEST_F (SysfsTest, ListSerialPortsSysfs_UsbSerialConverter_ReadsIdsTwoLevelsUp)
{
    make_dirs ("devices/usb1/1-1/1-1:1.0/ttyUSB0");
    write_file ("devices/usb1/1-1/idVendor", "0403\n");
    write_file ("devices/usb1/1-1/idProduct", "6015\n");
    add_tty ("ttyUSB0", "devices/usb1/1-1/1-1:1.0/ttyUSB0");

    std::vector<SerialPortInfo> res = list_serial_ports_sysfs (root + "/class/tty");

    ASSERT_EQ (res.size (), 1u);
    EXPECT_EQ (res[0].port, "/dev/ttyUSB0");
    EXPECT_EQ (res[0].vid, 0x0403);
    EXPECT_EQ (res[0].pid, 0x6015);
}

TEST_F (SysfsTest, ListSerialPortsSysfs_CdcAcmDevice_ReadsIdsOneLevelUp)
{
    make_dirs ("devices/usb1/1-2/1-2:1.0");
    write_file ("devices/usb1/1-2/idVendor", "2458\n");
    write_file ("devices/usb1/1-2/idProduct", "0001\n");
    add_tty ("ttyACM0", "devices/usb1/1-2/1-2:1.0");

    std::vector<SerialPortInfo> res = list_serial_ports_sysfs (root + "/class/tty");

    ASSERT_EQ (res.size (), 1u);
    EXPECT_EQ (res[0].port, "/dev/ttyACM0");
    EXPECT_EQ (res[0].vid, 0x2458);
    EXPECT_EQ (res[0].pid, 0x0001);
}

TEST_F (SysfsTest, ListSerialPortsSysfs_NonUsbAndVirtualTtys_AreSkipped)
{
    make_dirs ("devices/platform/serial8250");
    add_tty ("ttyS0", "devices/platform/serial8250");
    add_tty ("tty0", "");

    std::vector<SerialPortInfo> res = list_serial_ports_sysfs (root + "/class/tty");

    EXPECT_THAT (res, IsEmpty ());
}

TEST_F (SysfsTest, ListSerialPortsSysfs_SeveralPorts_SortedByPortName)
{
    make_dirs ("devices/usb1/1-1/1-1:1.0/ttyUSB1");
    write_file ("devices/usb1/1-1/idVendor", "0403\n");
    write_file ("devices/usb1/1-1/idProduct", "6015\n");
    make_dirs ("devices/usb1/1-2/1-2:1.0/ttyUSB0");
    write_file ("devices/usb1/1-2/idVendor", "0403\n");
    write_file ("devices/usb1/1-2/idProduct", "6015\n");
    add_tty ("ttyUSB1", "devices/usb1/1-1/1-1:1.0/ttyUSB1");
    add_tty ("ttyUSB0", "devices/usb1/1-2/1-2:1.0/ttyUSB0");

    std::vector<SerialPortInfo> res = list_serial_ports_sysfs (root + "/class/tty");

    ASSERT_EQ (res.size (), 2u);
    EXPECT_EQ (res[0].port, "/dev/ttyUSB0");
    EXPECT_EQ (res[1].port, "/dev/ttyUSB1");
}

#endif
