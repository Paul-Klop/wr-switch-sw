import os
import sys
from wrs import WRS
from scope import Scope
from customthread import CustomThread

class Logger:
    scope: Scope
    gm: WRS
    bc: WRS
    log_file: str = "measures.csv"

    def __init__(self, scope, wrs_gm, wrs_bc) -> None:
        self.scope = scope
        self.gm = wrs_gm
        self.bc = wrs_bc

    def set_file_name(self, log_file):
        self.log_file = log_file

    def open(self):
        # Try to create the file if it does not exist
        try:
            self.f = open(self.log_file, "x")
            print("INFO: creating log file \"" + self.log_file + "\"")
        # Open the log file in overwrite mode if it exists
        except:
            print("INFO: log file \"" + self.log_file + "\" already exists and will be overwritten. Saving copy \"" + self.log_file + ".old\"")
            os.system("cp " + self.log_file + " " + self.log_file + ".old")
            self.f = open(self.log_file, "w")

    def write_header(self):
        # Get scope configurtion and write it to log file
        if(self.scope.connected == True):
            self.f.write("Scope Info:\n")
            self.f.write("Scope name;" + self.scope.name + "\n")
            self.f.write("Scope address;" + self.scope.address + "\n")
            self.f.write(self.scope.get_general_config())
            for i in range(0, 4):
                if(self.scope.ch_label[i]):
                    self.f.write("CH{} label;{}\n".format(i+1, self.scope.ch_label[i]))
                    self.f.write(self.scope.get_ch_config(i+1))
            for i in range(0, 3):
                if(self.scope.meas_type[i]):
                    self.f.write(self.scope.get_meas_config(i+1))
            self.f.write("\n")

        # Get GM info and write it to log file
        self.f.write("GM Info:\n")
        self.f.write("Hostname:;" + self.gm.address + "\n")
        self.f.write("Name:;" + self.gm.name + "\n")
        self.f.write("MAC Addr:;" + self.gm.get_mac_addr() + "\n")
        if(self.gm.reboot):
            self.f.write("Test mode:;reboot\n")
        elif(self.gm.ifdownup):
            self.f.write("Test mode:;if down-up\n")
        else:
            self.f.write("Test mode:;none\n")
        self.f.write("Test port:;" + str(self.gm.sfp_port) + "\n")
        self.f.write(self.gm.get_version() + "\n")

        # Get BC info and write it to log file
        self.f.write("BC Info:\n")
        self.f.write("Hostname:;" + self.bc.address + "\n")
        self.f.write("Name:;" + self.bc.name + "\n")
        self.f.write("MAC Addr:;" + self.bc.get_mac_addr() + "\n")
        if(self.bc.reboot):
            self.f.write("Test mode:;reboot\n")
        elif(self.bc.ifdownup):
            self.f.write("Test mode:;if down-up\n")
        else:
            self.f.write("Test mode:;none\n")
        self.f.write("Test port:;" + str(self.bc.sfp_port) + "\n")
        self.f.write(self.bc.get_version() + "\n")

        # SFPs information
        self.f.write("SFPs Info:\n")
        # First column of labels is "WRS Mode"
        self.f.write("WRS;Port;")

        # Write info labels at the begining
        for label in WRS.sfp_info_list:
            if(label != WRS.sfp_info_list[-1]):
                label += ";"
            self.f.write(label)
        self.f.write('\n')

        # Get SFP info from GM and write it after labels
        self.f.write("GM - {};{};".format(self.gm.name, self.gm.sfp_port))
        gm_sfp_info_list = self.gm.get_sfp_info(self.gm.sfp_port)
        for info in gm_sfp_info_list:
            if(info != gm_sfp_info_list[-1]):
                info += ";"
            self.f.write(info)
        self.f.write('\n')

        # Get SFP info from BC
        self.f.write("BC - {};{};".format(self.bc.name, self.bc.sfp_port))
        bc_sfp_info_list = self.bc.get_sfp_info(self.bc.sfp_port)
        for info in bc_sfp_info_list:
            if(info != bc_sfp_info_list[-1]):
                info += ";"
            self.f.write(info)
        self.f.write("\n\n")
        self.f.flush()

    def write_rawDelayMM_header(self):
        self.f.write("\nPort {} rawDelayMM:\n".format(self.bc.sfp_port))
        self.f.write("Meas #;rawDelayMM;")
        self.f.write("GM T° FPGA (°C);GM T° PLL (°C);GM T° PSL (°C);GM T° PSR (°C);GM T° SFP (°C);")
        self.f.write("BC T° FPGA (°C);BC T° PLL (°C);BC T° PSL (°C);BC T° PSR (°C);BC T° SFP (°C)\n")
        self.f.flush()

    def write_rawDelayMM(self, meas_nb, meas):
        # Get temperatures from the switches using thread to reduce duration
        def t_gm(gm):
            temps_gm = gm.get_temperatures()
            temps_gm += [gm.get_sfp_temperature(gm.sfp_port)]
            return temps_gm

        def t_bc(bc):
            temps_bc = bc.get_temperatures()
            temps_bc += [bc.get_sfp_temperature(bc.sfp_port)]
            return temps_bc

        t1 = CustomThread(target=t_gm, args=(self.gm,))
        t2 = CustomThread(target=t_bc, args=(self.bc,))

        t1.start()
        t2.start()

        temps_gm = t1.join()
        temps_bc = t2.join()

        line = str(meas_nb) + ";"
        line += str(meas) + ";"

        for temp in temps_gm:
            line += str(temp) + ";"
        
        for temp in temps_bc:
            if(temp != temps_bc[-1]):
                line += str(temp) + ";"
            else:
                line += str(temp) + "\n"
        
        # Write and flush buffer to be able to tail -f 
        self.f.write(line)
        self.f.flush()

    def write_meas_header(self):
        # Write column titles for measures
        self.f.write("\nPort {} scope measures:\n".format(self.bc.sfp_port))
        self.f.write("Meas #;")
        for i in range(0, 3):
            if(self.scope.meas_type[i]):
                self.f.write("{} vs {} - Mean delay (s);".format(self.scope.meas_src1[i], self.scope.meas_src2[i]))
                self.f.write("{} vs {} - Min delay (s);".format(self.scope.meas_src1[i], self.scope.meas_src2[i]))
                self.f.write("{} vs {} - Max delay (s);".format(self.scope.meas_src1[i], self.scope.meas_src2[i]))
                self.f.write("{} vs {} - Peak to peak (s);".format(self.scope.meas_src1[i], self.scope.meas_src2[i]))
                self.f.write("{} vs {} - Std dev (s);".format(self.scope.meas_src1[i], self.scope.meas_src2[i]))
                self.f.write("{} vs {} - Samples;".format(self.scope.meas_src1[i], self.scope.meas_src2[i]))

        self.f.write("GM T° FPGA;GM T° PLL;GM T° PSL;GM T° PSR;GM T° SFP;")
        self.f.write("BC T° FPGA;BC T° PLL;BC T° PSL;BC T° PSR;BC T° SFP\n")
        self.f.flush()

    def write_measure(self, meas_nb):
        # Get actual values from the scope (always measure 1, should be configurable ?)
        meas_scope = []
        for i in range(0, 3):
            if(self.scope.meas_type[i]):
                meas_scope += [self.scope.get_measures(i+1)]

        # Get temperatures from the switches
        temps_gm = self.gm.get_temperatures()
        temps_gm += [self.gm.get_sfp_temperature(self.gm.sfp_port)]
        temps_bc = self.bc.get_temperatures()
        temps_bc += [self.bc.get_sfp_temperature(self.bc.sfp_port)]
        
        # Write measures in CSV file
        line = str(meas_nb) + ";"
        for meas in meas_scope:
            for current_meas in meas:
                line += str(current_meas) + ";"

        for temp in temps_gm:
            line += str(temp) + ";"
        
        for temp in temps_bc:
            if(temp != temps_bc[-1]):
                line += str(temp) + ";"
            else:
                line += str(temp) + "\n"

        # Write and flush buffer to be able to tail -f 
        self.f.write(line)
        self.f.flush()

    def write_measure_average(self, average):
        line = "Average (s);{}\n".format(average)
        self.f.write(line)
        self.f.flush()

    def write_egress_ingress(self, egress, ingress):
        self.f.write("\nPort {}:\n".format(self.bc.sfp_port))
        self.f.write("Egress latency:;{}\n".format(egress))
        self.f.write("Ingress latency:;{}\n".format(ingress))
        self.f.flush()

    def write_t24p_header(self):
        self.f.write("T24P for {}\n".format(self.bc.address))
        self.f.write("Port;T24P\n")
        self.f.flush()

    def write_t24p(self, port, t24p):
        self.f.write("{};{}\n".format(port, t24p))
        self.f.flush()

    def write_sfp_correction_header(self):
        self.f.write("SFP Vendor;SFP PN;Tx wavelength;SFP SN;Tx correction;Rx correction\n")
        self.f.flush()

    def write_sfp_correction(self, tx, rx):
        sfp_info = self.bc.get_sfp_info(1)
        self.f.write("{};{};{};{};{};{}\n".format(sfp_info[0], sfp_info[1], sfp_info[2], sfp_info[3], tx, rx))
        self.f.flush()
