#!/bin/sh

check_linker()
{
        [ ! -L "$2" ] && ln -sf $1 $2
}

post_chk()
{
	#TODO: ensure /userdata mount done
	cnt=0
	while [ $cnt -lt 30 ];
	do
		cnt=$(( cnt + 1 ))
		if mount | grep -w userdata; then
			break
		fi
		sleep .1
	done

	udhcpc -i eth0 &
	wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf &
	check_linker /userdata   /usr/www/userdata
	check_linker /media/usb0 /usr/www/usb0
	check_linker /mnt/sdcard /usr/www/sdcard

	# if /data/rkipc not exist, cp /usr/share
	rkipc_ini=/userdata/rkipc.ini
	default_rkipc_ini=/usr/share/rkipc.ini
	if [ ! -f "$default_rkipc_ini" ];then
		default_rkipc_ini=/oem/share/rkipc.ini
	fi
	if [ ! -f "$default_rkipc_ini" ];then
		echo "Error: not found rkipc.ini !!!"
		exit -1
	fi
	if [ ! -f "$rkipc_ini" ]; then
		cp $default_rkipc_ini $rkipc_ini -f
	fi
	rkipc &
}

ulimit -c unlimited
echo "/data/core-%p-%e" > /proc/sys/kernel/core_pattern

# cpu
echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
echo userspce > /sys/devices/system/cpu/cpufreq/policy4/scaling_governor
echo 1896000 > /sys/devices/system/cpu/cpufreq/policy4/scaling_setspeed
echo userspce > /sys/devices/system/cpu/cpufreq/policy6/scaling_governor
echo 1896000 > /sys/devices/system/cpu/cpufreq/policy6/scaling_setspeed
# gpu
# echo 900000 > /sys/kernel/debug/regulator/vdd_gpu_mem_s0/voltage
# echo 900000 > /sys/kernel/debug/regulator/vdd_gpu_s0/voltage
# echo 1000000000 > /sys/kernel/debug/clk/clk_gpu_coregroup/clk_rate
echo userspace > /sys/class/devfreq/fb000000.gpu/governor
echo 1000000000 > /sys/class/devfreq/fb000000.gpu/max_freq
echo 1000000000 > /sys/class/devfreq/fb000000.gpu/min_freq
cat /sys/class/devfreq/fb000000.gpu/cur_freq

post_chk &
