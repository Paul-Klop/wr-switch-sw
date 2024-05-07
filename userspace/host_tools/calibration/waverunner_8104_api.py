# Information on VB commands from:
# https://github.com/SengerM/TeledyneLeCroyPy/blob/main/TeledyneLeCroyPy/__init__.py
# https://ohwr.org/project/wr-abscal-el-auto/tree/master/lib

import pyvisa as visa

class WAVERUNNER_8104_API:
    rm = visa.ResourceManager()
    vicp_addr = ""

    def __init__(self):
        pass

    def connect(self, address):
        vicp_addr = "VICP::" + address + "::INSTR"
        try:
            self.scope = self.rm.open_resource(vicp_addr)
            self.scope.timeout = 10000 # ms
            return True
        except:
            return False
        
    def get_info(self):
        return self.scope.query("*IDN?").split("*IDN ")[1].rstrip()

    def clear_measures(self):
        self.scope.write("VBS? 'app.Measure.clearsweeps'")

    def get_mean(self, meas_nb):
        return self.scope.query("VBS? 'return=app.Measure.P{}.mean.Result.Value'".format(meas_nb)).split("VBS ")[1].rstrip()
    
    def get_min(self, meas_nb):
        return self.scope.query("VBS? 'return=app.Measure.P{}.min.Result.Value'".format(meas_nb)).split("VBS ")[1].rstrip()

    def get_max(self, meas_nb):
        return self.scope.query("VBS? 'return=app.Measure.P{}.max.Result.Value'".format(meas_nb)).split("VBS ")[1].rstrip()
    
    def get_peak2peak(self, meas_nb):
        return self.scope.query("VBS? 'return=app.Measure.P{}.pkpk.Result.Value'".format(meas_nb)).split("VBS ")[1].rstrip()

    def get_sdev(self, meas_nb):
        return self.scope.query("VBS? 'return=app.Measure.P{}.sdev.Result.Value'".format(meas_nb)).split("VBS ")[1].rstrip()

    def get_nsamples(self, meas_nb):
        return self.scope.query("VBS? 'return=app.Measure.P{}.histo.Result.Sweeps'".format(meas_nb)).split("VBS ")[1].rstrip()
    
    def get_ch_vscale(self, ch):
        return ""

    def get_ch_termination(self, ch):
        return ""

    def get_ch_bw(self, ch):
        return ""
    
    def get_ch_offset(self, ch):
        return ""

    def get_hscale(self):
        return ""

    def get_meas_type(self, meas_nb):
        return ""

    def get_meas_source(self, meas_nb, src_nb):
        return ""

    def get_abs_rhlevel(self):
        return ""
    
    def get_abs_rmlevel(self):
        return ""
    
    def get_abs_rllevel(self):
        return ""
    
    def get_abs_hysteresis(self):
        return ""

    def get_trigger_mode(self):
        return ""

    def get_trigger_src(self):
        return ""

    def get_trigger_level(self, ch):
        return ""
    
    def get_trigger_slope(self):
        return ""
    
    def set_ch_state(self, ch, state):
        if(state):
            pass
        else:
            pass

    def set_ch_vscale(self, ch, scale):
        pass
    
    def set_ch_termination(self, ch, term):
        pass
    
    def set_ch_bw(self, ch, bw):
        pass
    
    def set_ch_offset(self, ch, offset):
        pass

    def set_ch(self, ch, offset, scale):
        pass
    
    def set_hscale(self, scale):
        pass

    def set_hpos(self, pos):
        pass

    def set_hmode_man(self, man):
        if(man == True):
            mode = "MANual"
        else:
            mode = "AUTO"
        pass

    def set_sample_rate(self, rate):
        pass

    def set_measure(self, meas_nb, meas_type, src1_ch, src2_ch, rh, rm, rl):
        pass

    def set_trigger_mode(self, mode):
        pass

    def set_trigger_src(self, ch):
        pass

    def set_trigger_slope(self, slope):
        pass

    def set_trigger_level(self, ch, level):
        pass

    def set_acq_ref_src(self):
        pass

    def set_display_overlay(self):
        pass
