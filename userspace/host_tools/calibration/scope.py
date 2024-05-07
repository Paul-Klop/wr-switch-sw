from mso64b_api import MSO64B_API
from waverunner_8104_api import WAVERUNNER_8104_API
import os
import time

# Generic class to describe and oscilloscope
class Scope:
    model_list = ["mso64b", "waverunner_8104"]
    api_list = [MSO64B_API(), WAVERUNNER_8104_API()]
    name=""
    model=""
    address=""
    ch_label=["", "", "", ""]
    ch_vscale=["10E-3", "10E-3", "10E-3", "10E-3"] 
    ch_termination=["50", "50", "50", "50"] 
    ch_bw=["FULL", "FULL", "FULL", "FULL"]
    ch_offset=["0.00", "0.00", "0.00", "0.00"]
    h_scale="4E-8"
    trigger_mode="NORMAL"
    trigger_src="CH1"
    trigger_level="850E-3"
    trigger_slope="rise"
    meas_type = ["", "", ""]
    meas_src1 = ["", "", ""]
    meas_src2 = ["", "", ""]
    meas_riseh = ["", "", ""]
    meas_risem = ["", "", ""]
    meas_risel = ["", "", ""]
    ref_src = "INTERnal"
    nb_ch = 0
    api_linked = False
    connected = False

    def __init__(self, name):
        self.name = name

    # Check that an address has been provided
    def check_address(self) -> bool: 
        if(len(self.address) > 0):
            return True
        else:
            return False

    # Check that a scope model has been provided
    def check_model(self) -> bool:
        if(len(self.model) > 0):
            return True
        else:
            return False

    # Check that the scope is up (ping once wait 1s)
    def is_up(self) -> bool:
        response = os.system("ping -W 1 -c 1 " + self.address + " > /dev/null")
        if(response == 0):
            return True
        else:
            return False
    
    # Link the API if the scope model exists in the API list, then try to connect to the scope
    def link_api(self) -> bool:
        self.api_linked = False
        # Check if API exists for the current model
        for mod in self.model_list:
            if(self.model == mod):
                self.api = self.api_list[self.model_list.index(mod)]
                self.api_linked = True

        if(self.api_linked == False):
            print("ERROR: scope \"{}\", no API available for model \"{}\"".format(self.name, self.model))
        
        # The API exists and has been linked and address string is not empty, try to connect
        if(self.api_linked and self.check_address()):
            if(self.api.connect(self.address)):
                self.connected = True
            else:
                print("ERROR: scope \"{}\", failed to connect".format(self.name))
                self.connected = False
        # Returns true only if API exists and scope is connected
        return (self.api_linked and self.connected)

    # Set scope configuration
    def set_config(self) -> None:
        first_ch = 0
        sec_ch = 0
        self.api.set_display_overlay()
        self.api.set_acq_ref_src(self.ref_src)
        for i in range(4):
            if(self.ch_label[i] != ""):
                if(first_ch == 0):
                    first_ch = i+1
                elif(sec_ch == 0):
                    sec_ch = i+1
                self.api.set_ch_state(i+1, True)
                self.api.set_ch_vscale(i+1, self.ch_vscale[i])
                self.api.set_ch_termination(i+1, self.ch_termination[i])
                self.api.set_ch_bw(i+1, self.ch_bw[i])
                self.api.set_ch_offset(i+1, self.ch_offset[i])
                self.api.set_ch_position(i+1, 0)
                #print("set_config, ch={}, vscale={}, term={}), bw={}".format(i+1, self.ch_vscale[i], self.ch_termination[i], self.ch_bw[i]))
            else:
                self.api.set_ch_state(i+1, False)
        self.api.set_hscale(self.h_scale)
        self.api.set_hmode_man(True)
        self.api.set_sample_rate(2.5e12)
        self.api.set_trigger_mode(self.trigger_mode)
        self.api.set_trigger_src(self.trigger_src)
        self.api.set_trigger_slope(self.trigger_slope)
        self.api.set_trigger_level(self.trigger_src, self.trigger_level)
        for i in range(3):
            if(self.meas_type[i]):
                self.api.set_measure(i+1, self.meas_type[i], self.meas_src1[i], self.meas_src2[i], self.meas_riseh[i], self.meas_risem[i], self.meas_risel[i])
        #print("set_config(ctd): h_scale={}, trigger_mode={}, trigger_src={}, trigger_slope={}, trigger_level={}".format(self.h_scale, self.trigger_mode, self.trigger_src, self.trigger_slope, self.trigger_level))

    def get_general_config(self):
        config = "hscale;" + self.api.get_hscale() + "\n"
        config += "Trigger mode;" + self.api.get_trigger_mode() + "\n"
        config += "Trigger source;" + self.api.get_trigger_src() + "\n"
        config += "Trigger level;" + self.api.get_trigger_level(1) + "\n"
        config += "Trigger slope;" + self.api.get_trigger_slope() + "\n"
        #config += "Timebase reference source;" + self.api.get_acq_ref_src() + "\n"
        return config
    
    def get_ch_config(self, ch):
        config = "CH" + str(ch) + " vscale;" + self.api.get_ch_vscale(ch) + "\n"
        config += "CH" + str(ch) + " termination;" + self.api.get_ch_termination(ch) + "\n"
        config += "CH" + str(ch) + " BW;" + self.api.get_ch_bw(ch) + "\n"
        config += "CH" + str(ch) + " offset;" + self.api.get_ch_offset(ch) + "\n"
        return config

    def get_meas_config(self, meas):
        config = "meas{} type;".format(meas) + self.api.get_meas_type(meas) + "\n"
        config += "meas{} source 1;".format(meas) + self.api.get_meas_source(meas, 1) + "\n"
        config += "meas{} source 2;".format(meas) + self.api.get_meas_source(meas, 2) + "\n"
        config += "meas{} abs rise high level;".format(meas) + self.api.get_abs_rhlevel() + "\n"
        config += "meas{} abs rise mid level;".format(meas) + self.api.get_abs_rmlevel() + "\n"
        config += "meas{} abs rise low level;".format(meas) + self.api.get_abs_rllevel() + "\n"
        config += "meas{} abs hysteresis;".format(meas) + self.api.get_abs_hysteresis() + "\n"
        return config
       
    # Clear all measures on this scope
    def clear_measures(self) -> None:
        self.api.clear_measures()
    
    # Get all measures from this scope, in the correct order
    def get_measures(self, meas_nb) -> [str]:
        meas_list = []
        meas_list += [self.api.get_mean(meas_nb)]
        meas_list += [self.api.get_min(meas_nb)]
        meas_list += [self.api.get_max(meas_nb)]
        meas_list += [self.api.get_peak2peak(meas_nb)]
        meas_list += [self.api.get_sdev(meas_nb)]
        meas_list += [self.api.get_nsamples(meas_nb)]
        return meas_list
    
    def auto_adjust(self):
        # Set horizontal scale to 20 ns/div: +-100 ns from center
        self.api.set_hscale(20e-9)
        # Set trigger position to center of screen
        self.api.set_hpos(50)
        self.api.clear_measures()
        time.sleep(1)
        # Wait to get at least 2 measures
        while True:
            time.sleep(0.25)
            nb_meas = int(self.api.get_nsamples(1))
            if(nb_meas >= 2):
                break
        # Get the delay
        delay = float(self.api.get_mean(1))
        #print("Delay = {}".format(delay))
        if(delay > 0):
            hpos = 10
        else:
            hpos = 90
        hscale = abs(round(delay/7.0, 12))
        if(abs(delay) < 120e-12):
            hscale = 40e-12
            hpos = 50
        elif(hscale < 40e-12):
            hscale = 40e-12
        #print("hscale = {}".format(hscale))
        self.api.set_hscale(hscale)
        self.api.set_hpos(hpos)

    # Print information about this scope
    def print_info(self) -> None:
        print("INFO: Scope \"{}\" parameters: ".format(self.name))
        print("  - name...........: " + self.name)
        print("  - model..........: " + self.model)
        print("  - address........: " + self.address)
        if(self.is_up()):
            print("  - up.............: YES")
            if(self.api_linked and self.connected):
                print("  - info...........: " + self.api.get_info())
        else:
            print("  - up.............: NO")
        print("  - h scale........: {}".format(self.h_scale))
        print("  - trigger mode...: {}".format(self.trigger_mode))
        print("  - trigger source.: {}".format(self.trigger_src))
        print("  - trigger level..: {}".format(self.trigger_level))
        print("  - trigger slope..: {}".format(self.trigger_slope))
        print("  - ref. source....: {}".format(self.ref_src))
        print("  - channels nb....: {}".format(self.nb_ch))
        for i in range(4):
            if(self.ch_label[i] != ""):
                print("  - CH{} label......: {}".format(i+1, self.ch_label[i]))
                print("  - CH{} v_scale....: {}".format(i+1, self.ch_vscale[i]))
                print("  - CH{} termination: {}".format(i+1, self.ch_termination[i]))
                print("  - CH{} bw.........: {}".format(i+1, self.ch_bw[i]))
                print("  - CH{} offset.....: {}".format(i+1, self.ch_offset[i]))
