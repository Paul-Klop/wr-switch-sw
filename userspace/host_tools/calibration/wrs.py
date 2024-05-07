# Author: Quentin GENOUD DUVILLARET (quentin.genoud@cern.ch)
# Class to used to control and read data from a White Rabbit Switch v3

import os
import time
from paramiko import SSHClient

# Class describing a White Rabbit Switch
class WRS:
    obj_name=""
    name=""
    mode_GM=False
    address=""
    password=""
    golden=False
    reboot_en=False
    ifdownup_en=False
    restart_hald=False
    restart_ppsi=False
    sfp_port=0
    sfp_info_list = ["Vendor Name", "Vendor Part Number", "TX Wavelength", "Vendor Serial"]
    temp_name_list = ["HAL.temp.fpga", "HAL.temp.pll", "HAL.temp.psl", "HAL.temp.psr"]
    ssh_client: SSHClient

    def __init__(self, name):
        self.obj_name = name

    # Check that an address has been provided
    def check_address(self) -> bool:
        if(self.golden == True):
            return True
        if(len(self.address) > 0):
            return True
        else:
            return False

    # Check that an SFP port has been provided
    def check_sfp_port(self) -> bool:
        if(self.golden == True):
            return True
        if(self.sfp_port > 0 and self.sfp_port <= 18):
            return True
        else:
            return False

    # Check that the switch is up (ping once, wait 1s)
    def is_up(self) -> bool:
        if(self.golden == True):
            return True
        response = os.system("ping -W 1 -c 1 " + self.address + " > /dev/null")
        if(response == 0):
            return True
        else:
            return False

    # Connect to ssh using provided address
    def ssh_connect(self) -> bool:
        if(self.golden == True):
            return True
        self.ssh_client = SSHClient()
        self.ssh_client.load_system_host_keys()
        try:
            self.ssh_client.connect(self.address, username="root", password=self.password)
            connected = self.ssh_client.get_transport().is_authenticated()
        except:
            connected = False
        return connected

    # Execute command over ssh and return result
    def ssh_command(self, command) -> str:
        if(self.golden == True):
            return ""
        try:
            #self.ssh_client.get_transport().is_authenticated()
            self.ssh_stdin, self.ssh_stdout, self.ssh_stderr = self.ssh_client.exec_command(command)
            res = self.ssh_stdout.read().decode("utf8")
        except:
            res = ""
        return res

    # Get WRS information (version HW/GW/SW, commit, date)
    def get_version(self) -> str:
        if(self.golden == True):
            return ""
        ver_str = self.ssh_command("/wr/bin/wrs_version -t")
        res = ""
        for line in ver_str.splitlines():
            split = line.split(": ")
            res += split[0].rstrip() + ":;\"\"\"" + split[1].rstrip() + "\"\"\"\n"
        return res

    # Get MAC address of eth0 (management port)
    def get_mac_addr(self) -> str:
        if(self.golden == True):
            return ""
        stdout = self.ssh_command("ifconfig eth0 | grep HWaddr")
        return stdout.split("HWaddr ")[1].rstrip()

    # Get SFP information from port specified in config file
    def get_sfp_info(self, port):
        ret_list = []
        if(self.golden == True):
            return ret_list
        stdout = self.ssh_command("/wr/bin/wrs_sfp_dump -L -d -p " + str(port))
        for line in stdout.splitlines():
            for info in self.sfp_info_list:
                if(line.find(info) > -1):
                    ret_list += [line.split((info + ": "))[1].rstrip()]
        return ret_list

    # Get SFP temperature from port specified in config file
    def get_sfp_temperature(self, port) -> str:
        if(self.golden == True):
            return ""
        stdout = self.ssh_command("/wr/bin/wrs_sfp_dump -L -d -p " + str(port))
        for line in stdout.splitlines():
            if(line.find("Temperature: ") > -1):
                sfp_temp = line.split("Temperature: ")[1].rstrip().split(" C")[0]
                return sfp_temp

    # Get all temperatures of the switch (FPGA, PLL, PSL, PSR)
    def get_temperatures(self):
        ret_list = []
        if(self.golden == True):
            return ["", "", "", ""]
        stdout = self.ssh_command("/wr/bin/wrs_dump_shmem -L | grep -e HAL.temp.fpga: -e HAL.temp.pll: -e HAL.temp.psl: -e HAL.temp.psr:")
        stdout = stdout.replace(" ", "")
        for line in stdout.splitlines():
            for temp_name in self.temp_name_list:
                if(line.find(temp_name) > -1):
                    ret_list += [(stdout.split(temp_name + ":")[1]).split("\n", 1)[0]]
        return ret_list
    
    def get_rawDelayMM(self, port):
        if(self.golden == True):
            return 0.00
        delay = float(self.ssh_command("/wr/bin/wrs_dump_shmem -P | grep ppsi.inst.{}.servo.wr.rawDelayMM".format(port-1)).rstrip().split(":")[1].strip())
        return delay

    def get_rawDelayMM_mean(self, port, meas_nb):
        if(self.golden == True):
            return 0.00
        curr_meas_nb = 0
        delayMM_sum = 0.00
        while(curr_meas_nb < meas_nb):
            curr_meas_nb += 1
            tstart = time.time()
            delayMM = self.get_rawDelayMM(port)
            delayMM_sum += delayMM
            #print("Meas {}: {}".format(curr_meas_nb, delayMM))
            tdiff = (time.time() - tstart)
            twait = 1 - tdiff
            if(twait > 0):
                #print("Duration: " + str(tdiff) + ", Time to wait: " + str(twait))
                time.sleep(twait)
            print(".", end="", flush=True)
        return (delayMM_sum / meas_nb)

    def get_masterDelays(self, port):
        delayTx = 0.00
        delayRx = 0.00
        if(self.golden == True):
            return delayTx, delayRx
        res = self.ssh_command("/wr/bin/wrs_dump_shmem -P | grep ppsi.inst.{}.servo.wr.delta_ | grep -e txm -e rxm".format(port-1))
        for line in res.splitlines():
            value = float(line.rstrip().split(":")[1])
            if(line.find("txm") > -1):
                delayTx = float(value)
            elif(line.find("rxm") > -1):
                delayRx = float(value)
        return delayTx, delayRx

    def get_bitslide(self, port):
        bitslide = 0.00
        if(self.golden == True):
            return bitslide
        res = self.ssh_command("/wr/bin/wrs_dump_shmem -L | grep HAL.port.{}.calib.bitslide_ps:".format(port)).split(":")[1].strip()
        res = int(res)
        bitslide = float(res * 1e-12)
        return bitslide

    def get_port_t24p(self, port):
        if(self.golden == True):
            return 0
        res = self.ssh_command("/usr/wr/bin/wr_phytool {} t24p | grep transition@".format(port))
        res = res.split(", ")
        for s in res:
            if(s.find("transition@") > -1):
                s = s.split("transition@")[1].split("ps")[0].strip()
                return int(s)

    def get_alpha(self):
        if(self.golden == True):
            return 0.00
        res = self.ssh_command("cat /usr/wr/etc/dot-config | grep FIBER00 | grep alpha")
        res = res.replace("\"", "").split("alpha_1310_1490=")[1].strip()
        return float(res)

    # Dump shmem and grep for specific information to determine if the PLL is locked in both GM and BC modes
    def shmem_dump(self) -> str:
        if(self.golden == True):
            return ""
        return self.ssh_command("/wr/bin/wrs_dump_shmem -S -P | grep -e SoftPll.mode -e SoftPll.align_state -e ppsi.inst.*.servo.servo_state_name -e ppsi.inst.*.servo.servo_state_name -e ppsi.inst.*.servo.servo_locked")

    # Returns true is the current switch mode is GM
    def is_gm(self) -> bool:
        if(self.golden == True):
            self.mode_GM = True
            return True
        # This loop is needed here because we know there are 2 possible configuration: GM or BC
        # Just after restarting the switch, the value of "SoftPll.mode" could be different, ie: FreeRunningMaster
        # So we need to wait until the response is either "GrandMaster(1)" or "Slave(3)", then compare and return
        while(True):
            shmem = self.shmem_dump()
            for line in shmem.splitlines():
                if(line.find("SoftPll.mode:") > -1):
                    if(line.find("Grand Master(1)") > -1 or line.find("Slave(3)") > -1):
                        if(line.find("Grand Master(1)") > -1):
                            self.mode_GM = True
                            return True
                        else:
                            return False
                    else:
                        break
            print(".", end="", flush=True)
            time.sleep(1)

    # Returns true if the current switch mode is BC
    def is_bc(self) -> bool:
        if(self.golden == True):
            self.mode_GM = True
            return True
        # This loop is needed here because we know there are 2 possible configuration: GM or BC
        # Just after restarting the switch, the value of "SoftPll.mode" could be different, ie: FreeRunningMaster
        # So we need to wait until the response is either "GrandMaster(1)" or "Slave(3)", then compare and return
        while(True):
            shmem = self.shmem_dump()
            for line in shmem.splitlines():
                if(line.find("SoftPll.mode:") > -1):
                    if( (line.find("Grand Master(1)") > -1) or (line.find("Slave(3)") > -1) ):
                        if(line.find("Slave(3)") > -1):
                            self.mode_GM = False
                            return True
                        else:
                            return False
                    else:
                        break
            print(".", end="", flush=True)
            time.sleep(1)

    # Returns true if the switch PLL is locked
    def is_PLL_locked(self) -> bool:
        if(self.golden == True):
            return True
        shmem = self.shmem_dump()
        if(self.mode_GM):
            for line in shmem.splitlines():
                if( (line.find("SoftPll.align_state:") > -1) and (line.find("locked(6)") > -1) ):
                    return True
        else:
            for line in shmem.splitlines():
                if( (line.find("servo_locked:") > -1) and (line.find("1 (yes)") > -1) ):
                    return True
        return False

    # Returns only when PLL is locked
    def wait_until_locked(self, timeout) -> None:
        if(self.golden == True):
            return
        timeout_time = timeout + int(time.time())
        #print("INFO: waiting for {} PLL to lock (timeout = {}s)".format(self.obj_name, timeout), end="", flush=True)
        while(self.is_PLL_locked() == False):
            print(".", end="", flush=True)
            current_time = int(time.time())
            if(current_time >= timeout_time):
                #print(" TIMEOUT")
                #print("WARNING: {} PLL lock timeout, rebooting...".format(self.obj_name))
                while(self.is_up() == True):
                    self.ssh_command("reboot")
                    time.sleep(1)
                #print("INFO: waiting for {} to boot".format(self.obj_name), end="", flush=True)
                while(self.is_up() == False):
                    #print(".", end="", flush=True)
                    time.sleep(1)
                while(self.ssh_connect() == False):
                    #print(".", end="", flush=True)
                    time.sleep(1)
                #print(" OK")
                timeout_time = timeout + int(time.time())
                #print("INFO: waiting for {} PLL to lock (timeout = {}s)".format(self.obj_name, timeout), end="", flush=True)
            time.sleep(1)
        #print(" OK")

    # Perform the operation specified in the config file (either reboot, ifupdown of specified port, or nothing)
    def reboot_ifdownup(self) -> None:
        if(self.reboot_en):
            print("INFO: rebooting " + self.obj_name + " @ \"" + self.address + "\"")
            while(self.is_up() == True):
                self.ssh_command("reboot")
                time.sleep(1)
        elif(self.ifdownup_en):
            print("INFO: ifdownup " + self.obj_name + " @ \"" + self.address + "\", port " + str(self.sfp_port))
            self.ssh_command("ifconfig wri{} down".format(self.sfp_port))
            time.sleep(2)
            self.ssh_command("ifconfig wri{} up".format(self.sfp_port))

    def set_mode_GM(self):
        self.ssh_command("sed -i 's/CONFIG_TIME_BC=y/CONFIG_TIME_GM=y/g' /usr/wr/etc/dot-config")
        self.ssh_command("sed -i 's/CONFIG_PTP_OPT_CLOCK_ACCURACY=.*/CONFIG_PTP_OPT_CLOCK_ACCURACY=33/g' /usr/wr/etc/dot-config")
        self.ssh_command("sed -i 's/CONFIG_PTP_OPT_CLOCK_ALLAN_VARIANCE=.*/CONFIG_PTP_OPT_CLOCK_ALLAN_VARIANCE=47360/g' /usr/wr/etc/dot-config")
        self.ssh_command("sed -i 's/CONFIG_PTP_OPT_TIME_SOURCE=.*/CONFIG_PTP_OPT_TIME_SOURCE=32/g' /usr/wr/etc/dot-config")
        self.ssh_command("sed -i 's/CONFIG_PTP_OPT_CLOCK_CLASS=.*/CONFIG_PTP_OPT_CLOCK_CLASS=6/g' /usr/wr/etc/dot-config")

    def set_mode_BC(self):
        self.ssh_command("sed -i 's/CONFIG_TIME_GM=y/CONFIG_TIME_BC=y/g' /usr/wr/etc/dot-config")
        self.ssh_command("sed -i 's/CONFIG_PTP_OPT_CLOCK_ACCURACY=.*/CONFIG_PTP_OPT_CLOCK_ACCURACY=254/g' /usr/wr/etc/dot-config")
        self.ssh_command("sed -i 's/CONFIG_PTP_OPT_CLOCK_ALLAN_VARIANCE=.*/CONFIG_PTP_OPT_CLOCK_ALLAN_VARIANCE=65535/g' /usr/wr/etc/dot-config")
        self.ssh_command("sed -i 's/CONFIG_PTP_OPT_TIME_SOURCE=.*/CONFIG_PTP_OPT_TIME_SOURCE=160/g' /usr/wr/etc/dot-config")
        self.ssh_command("sed -i 's/CONFIG_PTP_OPT_CLOCK_CLASS=.*/CONFIG_PTP_OPT_CLOCK_CLASS=248/g' /usr/wr/etc/dot-config")

    def set_alpha(self, alpha):
        self.ssh_command("sed -i 's/alpha_1310_1490=.*/alpha_1310_1490={}\"/g' /usr/wr/etc/dot-config".format(str(alpha)))

    def set_port_delays(self, port, egress, ingress):
        self.ssh_command("sed -i 's/CONFIG_PORT{:02d}_INST01_EGRESS_LATENCY=.*/CONFIG_PORT{:02d}_INST01_EGRESS_LATENCY={}/g' /usr/wr/etc/dot-config".format(port, port, str(egress)))
        self.ssh_command("sed -i 's/CONFIG_PORT{:02d}_INST01_INGRESS_LATENCY=.*/CONFIG_PORT{:02d}_INST01_INGRESS_LATENCY={}/g' /usr/wr/etc/dot-config".format(port, port, str(ingress)))

    def set_port_master(self, port):
        # Configure port as master
        self.ssh_command("sed -i 's/CONFIG_PORT{:02d}_INST01_DESIRADE_STATE_SLAVE=y/CONFIG_PORT{:02d}_INST01_DESIRADE_STATE_MASTER=y/g' /usr/wr/etc/dot-config".format(port, port))

    def set_port_slave(self, port):
        # Configure port as slave
        self.ssh_command("sed -i 's/CONFIG_PORT{:02d}_INST01_DESIRADE_STATE_MASTER=y/CONFIG_PORT{:02d}_INST01_DESIRADE_STATE_SLAVE=y/g' /usr/wr/etc/dot-config".format(port, port))

    def set_port_t24p(self, port, t24p):
        self.ssh_command("sed -i 's/CONFIG_PORT{:02d}_INST01_T24P_TRANS_POINT=.*/CONFIG_PORT{:02d}_INST01_T24P_TRANS_POINT={}/g' /usr/wr/etc/dot-config".format(port, port, t24p))

    def set_sfp_param(self, index, tx, rx):
        self.ssh_command("sed -i '/CONFIG_SFP{:02d}_PARAMS=/ s/\(vn=[^,]*,pn=[^,]*,tx=\)[^,]*/\\1{}/; /CONFIG_SFP{:02d}_PARAMS=/ s/\(rx=\)[^,]*/\\1{}/' /usr/wr/etc/dot-config".format(index, tx, index, rx))

    def get_sfp_pn(self, port):
        pn = ""
        res = self.ssh_command("/wr/bin/wrs_sfp_dump -L -p{}".format(port))
        sfp_dump = res.splitlines()
        for line in sfp_dump:
            if(line.find("Vendor Part Number:") > -1):
                pn = line.split("Vendor Part Number:")[1].strip()
                break
        return pn
    
    def get_sfp_db_index(self, port):
        index = -1
        res = self.ssh_command("grep CONFIG_SFP /usr/wr/etc/dot-config")
        sfp_db = res.splitlines()
        pn = self.get_sfp_pn(port)
        for line in sfp_db:
            if(line.find(pn) > -1):
                index = int(line.split("CONFIG_SFP")[1].strip()[0:2])
                break
        return index
    
    def check_sfp_index(self, index):
        ret = False
        db_lines = self.ssh_command("grep CONFIG_SFP /usr/wr/etc/dot-config").splitlines()
        for line in db_lines:
            if(line.find("CONFIG_SFP{:02d}".format(index)) > -1):
                ret = True
                break
        return ret

    def ppsi_restart(self):
        self.ssh_command("/wr/bin/apply_dot-config 1>/dev/null 2>/dev/null")
        self.ssh_command("/etc/init.d/ppsi.sh restart 1>/dev/null 2>/dev/null")
    
    def hald_restart(self):
        self.ssh_command("/wr/bin/apply_dot-config 1>/dev/null 2>/dev/null")
        self.ssh_command("/etc/init.d/hald.sh restart 1>/dev/null 2>/dev/null")

    def apply_config(self):
        if(self.restart_hald):
            self.hald_restart()
            self.restart_hald = False
            self.restart_ppsi = False
        elif(self.restart_ppsi):
            self.ppsi_restart()
            self.restart_ppsi = False

    def reboot(self):
        self.ssh_command("reboot")

    def ifdownup(self):
        self.ssh_command("ifconfig wri{} down".format(self.sfp_port))
        time.sleep(2)
        self.ssh_command("ifconfig wri{} up".format(self.sfp_port))

    # Print all the information about this switch
    def print_info(self) -> None:
        print("INFO: {} Switch parameters from config file: ".format(self.obj_name))
        print("  - name....: " + self.name)
        print("  - address.: " + self.address)
        print("  - password: " + self.password)
        print("  - SFP port: " + str(self.sfp_port))
        print("  - golden..: " + str(self.golden))
        print("  - reboot..: " + str(self.reboot_en))
        print("  - ifdownup: " + str(self.ifdownup_en))
        if(self.is_up()):
            print("  - up......: YES")
            print("  - MAC.....: " + self.get_mac_addr())
        else:
            print("  - up......: NO")
