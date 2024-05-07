# mdo simple plot
# python v3.x, pyvisa v1.8
# should work with MSO70k, DPO7k, MSO5k, MDO4k, MDO3k, and MSO2k series
# 5/6 Series MSO 

# incompatible with TDS2k and TBS1k series (see tbs simple plot)

import pyvisa as visa # http://github.com/hgrecco/pyvisa

class MSO64B_API:
    rm = visa.ResourceManager()

    def __init__(self):
        pass

    def connect(self, address):
        visa_addr = "TCPIP::" + address + "::INSTR"
        try:
            self.scope = self.rm.open_resource(visa_addr)
            self.scope.timeout = 10000 # ms
            self.scope.encoding = 'latin_1'
            self.scope.read_termination = '\n'
            self.scope.write_termination = None
            self.scope.write('*cls') # clear ESR
            return True
        except:
            return False
    
    def send_query(self, cmd):
        return self.scope.query(cmd)
    
    def send_write(self, cmd):
        return self.scope.write(cmd)

    def get_info(self):
        return self.scope.query('*idn?')

    def clear_measures(self):
        self.scope.write('DISPLAY:PERSISTENCE:RESET')

    def get_mean(self, meas_nb):
        return self.scope.query("measu:meas{}:mean?".format(meas_nb))
    
    def get_min(self, meas_nb):
        return self.scope.query("measu:meas{}:min?".format(meas_nb))

    def get_max(self, meas_nb):
        return self.scope.query("measu:meas{}:max?".format(meas_nb))
    
    def get_peak2peak(self, meas_nb):
        return self.scope.query("measu:meas{}:PK2PK?".format(meas_nb))

    def get_sdev(self, meas_nb):
        return self.scope.query("measu:meas{}:STDdev?".format(meas_nb))

    def get_nsamples(self, meas_nb):
        return self.scope.query("measu:meas{}:popu?".format(meas_nb))

    def get_ch_vscale(self, ch):
        return self.scope.query("ch{}:scale?".format(ch))

    def get_ch_termination(self, ch):
        return self.scope.query("ch{}:termination?".format(ch))

    def get_ch_bw(self, ch):
        return self.scope.query("ch{}:bandwidth?".format(ch))
    
    def get_ch_offset(self, ch):
        return self.scope.query("ch{}:offset?".format(ch))

    def get_hscale(self):
        return self.scope.query("HOR:SCA?")

    def get_meas_type(self, meas_nb):
        return self.scope.query("MEASU:MEAS{}:TYPE?".format(meas_nb))

    def get_meas_source(self, meas_nb, src_nb):
        return self.scope.query("MEASU:MEAS{}:SOU{}?".format(meas_nb, src_nb))

    def get_abs_rhlevel(self):
        return self.scope.query("MEASUrement:REFLevels:ABSolute:RISEHigh?")
    
    def get_abs_rmlevel(self):
        return self.scope.query("MEASUrement:REFLevels:ABSolute:RISEMid?")
    
    def get_abs_rllevel(self):
        return self.scope.query("MEASUrement:REFLevels:ABSolute:RISELow?")
    
    def get_abs_hysteresis(self):
        return self.scope.query("MEASUrement:REFLevels:ABSolute:HYST?")

    def get_trigger_mode(self):
        return self.scope.query("TRIGger:A:MODe?")

    def get_trigger_src(self):
        return self.scope.query("TRIGger:A:EDGE:SOUrce?")

    def get_trigger_level(self, ch):
        return self.scope.query("TRIGger:A:LEVel:CH{}?".format(ch))

    def get_trigger_slope(self):
        return self.scope.query("TRIGger:A:EDGE:SLOpe?")
    
    def get_acq_ref_src(self):
        return self.scope.query("ROSc:SOUrce?")
    
    def set_ch_state(self, ch, state):
        if(state):
            self.scope.write("DISplay:GLObal:CH{}:STATE ON".format(ch))
        else:
            self.scope.write("DISplay:GLObal:CH{}:STATE OFF".format(ch)) 

    def set_ch_vscale(self, ch, scale):
        self.scope.write("ch{}:scale {}".format(ch, scale))
    
    def set_ch_termination(self, ch, term):
        self.scope.write("ch{}:termination {}".format(ch, term))
    
    def set_ch_bw(self, ch, bw):
        self.scope.write("ch{}:bandwidth {}".format(ch, bw))
    
    def set_ch_offset(self, ch, offset):
        self.scope.write("ch{}:offset {}".format(ch, offset))
    
    def set_ch_position(self, ch, pos):
        self.scope.write("ch{}:position {}".format(ch, pos))

    def set_ch(self, ch, offset, scale):
        self.scope.write("DISplay:GLObal:CH{}:STATE ON".format(ch))
        self.scope.write("CH{}:termination 50".format(ch))
        self.scope.write("CH{}:OFFSet {}".format(ch, offset))
        self.scope.write("CH{}:scale {}".format(ch, scale))
        self.scope.write("CH{}:BANdwidth FULl".format(ch))
    
    def set_hscale(self, scale):
        self.scope.write("HORizontal:SCAle {}".format(scale))

    def set_hpos(self, pos):
        self.scope.write("HORizontal:POSition {}".format(pos))

    def set_hmode_man(self, man):
        if(man == True):
            mode = "MANual"
        else:
            mode = "AUTO"
        self.scope.write("HORizontal:MODE {}".format(mode))

    def set_sample_rate(self, rate):
        self.scope.write("HORizontal:MODE:SAMPLERate {}".format(rate))

    def set_measure(self, meas_nb, meas_type, src1_ch, src2_ch, rh, rm, rl):
        #print("set_delay_measure: src1={}, src2={}, rh={}, rm={}, rl={}".format(src1_ch, src2_ch, rh, rm, rl))
        self.scope.write("MEASUrement:DELete \"MEAS{}\"".format(meas_nb))
        self.scope.write("MEASUrement:ADDNew \"MEAS{}\"".format(meas_nb))
        self.scope.write("MEASUREMENT:MEAS{}:TYPE {}".format(meas_nb, meas_type))
        self.scope.write("MEASUrement:MEAS{}:SOUrce1 {}".format(meas_nb, src1_ch))
        self.scope.write("MEASUrement:MEAS{}:SOUrce2 {}".format(meas_nb, src2_ch))
        if(meas_type == "DELAY"):
            self.scope.write("MEASUrement:MEAS{}:DELay:EDGE1 RISe".format(meas_nb))
            self.scope.write("MEASUrement:MEAS{}:DELay:EDGE2 RISe".format(meas_nb))
        self.scope.write("MEASUrement:MEAS{}:GLOBalref ON".format(meas_nb))
        self.scope.write("MEASUrement:REFLevels:METHod ABSolute")
        self.scope.write("MEASUrement:REFLevels:ABSolute:TYPE SAME")
        self.scope.write("MEASUrement:REFLevels:ABSolute:RISEHigh {}".format(rh))
        self.scope.write("MEASUrement:REFLevels:ABSolute:RISEMid {}".format(rm))
        self.scope.write("MEASUrement:REFLevels:ABSolute:RISELow {}".format(rl))
        self.scope.write("MEASUrement:REFLevels:ABSolute:HYSTeresis 30E-3")

    def set_trigger_mode(self, mode):
        # Mode: NORMal, AUTO
        self.scope.write("TRIGger:A:MODe {}".format(mode))

    def set_trigger_src(self, ch):
        # Source: 'CHx'
        self.scope.write("TRIGger:A:EDGE:SOUrce {}".format(ch))

    def set_trigger_slope(self, slope):
        # Slope: RISe, FALl
        self.scope.write("TRIGger:A:EDGE:SLOpe {}".format(slope))

    def set_trigger_level(self, ch, level):
        self.scope.write("TRIGger:A:LEVel:{} {}".format(ch, level))

    def set_acq_ref_src(self, ref):
        # Source: INTERnal, EXTernal
        self.scope.write("ROSc:SOUrce {}".format(ref))

    def set_display_overlay(self):
        self.scope.write("DISplay:WAVEView1:VIEWStyle OVERLAY")
