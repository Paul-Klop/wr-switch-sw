from wrs import WRS
from scope import Scope

class Config:
    # Section names in config file
    cfg_file: str
    scope: Scope
    gm: WRS
    bc: WRS
    meas_number = 20
    meas_duration = 60

    # Init: open the config file provided as argument 
    def __init__(self, scope, wrs_gm, wrs_bc):
        self.scope = scope
        self.gm = wrs_gm
        self.bc = wrs_bc

    def set_file(self, cfg_file):
        self.cfg_file = cfg_file

    # Get all lines from config file, remove empty lines, comments and white spaces
    def get_file_lines(self, file):
        lines = []
        tmp = file.readlines()
        for line in tmp:
            # Ignore empty lines (start with line feed) or comments (start with #)
            if(line[0] != "\n" and line[0] != "#"):
                # Get only the part of the line before the comment
                line = line.split("#")[0]
                # Remove all whitespaces
                line = line.replace(" ", "")
                # Remove all line feeds
                line = line.replace("\n", "")
                # Add the line to the lines list
                lines += [ line ]
        return lines

    # Get all lines contained in a section
    def get_section_lines(self, lines, first_line_index) -> [str]:
        section_lines = []
        for line in lines[first_line_index:]:
            # If there is ':' in a line, that means that we are reading the title of another section
            if(line.find(':') > 0):
                break
            else:
                section_lines += [ line ]
        return section_lines

    # Parse a section describing a scope
    def parse_scope_section(self, scope, section_lines, first_line_nb) -> Scope:
        line_nb=first_line_nb
        for sline in section_lines:
            line_nb += 1
            #print("parse_scope_section: parsing line {}: {}".format(line_nb, sline))
            # If '=' char is in the line, then split line into property / value
            if(sline.find('=') > 0):
                prop = sline.split('=')[0]
                value = sline.split('=')[1]
                #print("Parsing line {}, prop={}, value={}".format(line_nb, prop, value))
                if(prop == "name"):
                    scope.name = value
                elif(prop == "model"):
                        scope.model = value
                elif(prop == "address"):
                        scope.address = value
                elif(prop.find("ch") > -1):
                    for i in range(4):
                        if(prop.find("ch{:01d}".format(i+1)) > -1):
                            #print("ch" + str(i+1) + " detected in config: " + prop + " = " + value)
                            if(prop.find("label") > -1):
                                scope.ch_label[i] = value
                                # Channel label is not "", there is a label, we assume channel is enable
                                if(len(value) > 0):
                                    scope.nb_ch += 1
                            elif(prop.find("vscale") > -1):
                                scope.ch_vscale[i] = value
                            elif(prop.find("termination") > -1):
                                scope.ch_termination[i] = value
                            elif(prop.find("bw") > -1):
                                scope.ch_bw[i] = value
                            elif(prop.find("offset") > -1):
                                scope.ch_offset[i] = value
                            else:
                                print("WARNING: CH{}, prop: {} not recognized".format(i+1, prop))
                            break
                elif(prop == "h_scale"):
                    scope.h_scale = value
                elif(prop == "trigger_mode"):
                    scope.trigger_mode = value
                elif(prop == "trigger_src"):
                    scope.trigger_src = value
                elif(prop == "trigger_level"):
                    scope.trigger_level = value
                elif(prop == "trigger_slope"):
                    scope.trigger_slope = value
                elif(prop.find("meas") > -1):
                    for i in range(3):
                        if(prop.find("meas{:01d}".format(i+1)) > -1):
                            #print("meas" + str(i+1) + " detected in config: " + prop + " = " + value)
                            if(prop.find("type") > -1):
                                scope.meas_type[i] = value
                            elif(prop.find("src1") > -1):
                                scope.meas_src1[i] = value
                            elif(prop.find("src2") > -1):
                                scope.meas_src2[i] = value
                            elif(prop.find("riseh") > -1):
                                scope.meas_riseh[i] = value
                            elif(prop.find("risem") > -1):
                                scope.meas_risem[i] = value
                            elif(prop.find("risel") > -1):
                                scope.meas_risel[i] = value
                            else:
                                print("WARNING: meas" + str(i+1) + ", prop: " + prop + " not recognized ")
                            break
                elif(prop == "ref_src"):
                    scope.ref_src = value
                else:
                    print("WARNING: line {}: property \"{}\" not recognized".format(line_nb, prop))
            # No '=' char in line, this can't be parsed
            else:
                print("WARNING: line {}: \"{}\" does not assign a property".format(line_nb, sline))

        return scope

    # Parse a section describing a WRS
    def parse_switch_section(self, switch, section_lines, first_line_nb) -> WRS:
        line_nb=first_line_nb
        for sline in section_lines:
            line_nb += 1
            # If '=' char is in the line, then split line into property / value
            if(sline.find('=') > 0):
                prop = sline.split('=')[0]
                value = sline.split('=')[1]
                if(prop == "name"):
                    switch.name = value
                elif(prop == "address"):
                    switch.address = value
                elif(prop == "password"):
                    switch.password = value
                elif(prop == "sfp_port"):
                    switch.sfp_port = int(value)
                elif(prop == "golden"):
                    if(value == "true"):
                        switch.golden = True
                    else:
                        switch.golden = False
                elif(prop == "reboot"):
                    if(value == "true"):
                        switch.reboot_en = True
                    else:
                        switch.reboot_en = False
                elif(prop == "ifdownup"):
                    if(value == "true"):
                        switch.ifdownup_en = True
                    else:
                        switch.ifdownup_en = False
                else:
                    print("WARNING: line {}: property \"{}\" not recognized".format(line_nb, prop))
            # No '=' char in line, this can't be parsed
            else:
                print("WARNING: line {}: \"{}\" does not assign a property".format(line_nb, sline))

        return switch
    
    # Parse file
    def parse(self):
        # Try to open config file in read only
        print("INFO: trying to open config file \"{}\": ".format(self.cfg_file), end="", flush=True)
        try:
            f = open(self.cfg_file, "r")
            print("OK")
        except:
            print("ERROR (file does not exist)")
            quit()
        
        lines = self.get_file_lines(f)
        f.close()

        line_index = 0
        section_lines = []
        section_found = False

        # For each line in the config file
        for line in lines:
            # Add 1 because it is and index (starting @ 0) and we want line number (starting @ 1)
            line_index = lines.index(line) + 1
            section_found = False

            # If there is ':' in the line, this is a section
            if(line.find(':') > 0):
                section_found = True
                section = line.split(":")[0]

                if(section == "scope1"):
                    section_lines = self.get_section_lines(lines, line_index)
                    self.scope = self.parse_scope_section(self.scope, section_lines, line_index)
                
                elif(section == "gm"):
                    section_lines = self.get_section_lines(lines, line_index)
                    self.gm = self.parse_switch_section(self.gm, section_lines, line_index)

                elif(section == "bc"):
                    section_lines = self.get_section_lines(lines, line_index)
                    self.bc = self.parse_switch_section(self.bc, section_lines, line_index)

                elif(section == "config"):
                    section_lines = self.get_section_lines(lines, line_index)
                    line_nb = line_index
                    for sline in section_lines:
                        line_nb += 1
                        # If '=' char is in the line, then split line into property / value
                        if(sline.find('=') > 0):
                            prop = sline.split('=')[0]
                            value = sline.split('=')[1]
                            if(prop == "meas_number"):
                                self.meas_number = int(value)
                            elif(prop == "meas_duration"):
                                self.meas_duration = int(value)
                            else:
                                print("WARNING: line {}: property \"{}\" not recognized".format(line_nb, prop))
                        # No '=' char in line, this can't be parsed
                        elif(sline[0] != "#"):
                            print("WARNING: line {}: \"{}\" does not assign a property".format(line_nb, sline))
                
                else:
                    print("WARNING: line {}: section \"{}\" does not exist".format(line_index, line.split(':')[0]))

                if(section_found == True):
                    print("INFO: Found section: " + section)
