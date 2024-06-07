# Author: Adam Wujek 2024


converter_standalone: ../../scripts/kconfig/conf
	mkdir -p dot-config_converter_standalone/kconfigs/v7.0
	mkdir -p dot-config_converter_standalone/bin
	cp dot-config_converter_6_x_to_7_0_script.sh dot-config_converter_standalone/bin/
	cp dot-config_converter_6_x_to_7_0_standalone.sh dot-config_converter_standalone/dot-config_converter_6_x_to_7_0.sh
	cp ../../Kconfig dot-config_converter_standalone/kconfigs/v7.0
	cp ../../Kconfig_port_timing.in dot-config_converter_standalone/kconfigs/v7.0
	cp ../../Kconfig_sfp_fiber.in dot-config_converter_standalone/kconfigs/v7.0
	cp ../../Kconfig_vlans.in dot-config_converter_standalone/kconfigs/v7.0
	cp ../../scripts/kconfig/conf dot-config_converter_standalone/bin
	git describe --dirty --always --tag > dot-config_converter_standalone/bin/version_info.txt

../../scripts/kconfig/conf:
	# conf not available
	@echo "scripts/kconfig/conf not available for host system."
	@echo "Please trigger compilation of it with \"make listnewconfig\" in the root of wr-switch-sw repo."
	@echo ""
	@false
