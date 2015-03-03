#!/bin/bash
# Initially developed at Ghent University as part of a masters thesis promoted
# by prof. dr. ir. Bjorn De Sutter of the Computer Systems Lab in cooperation with ir. Daan Raman from NVISO.
# Author: Christophe Beauval
# Version: 20140502
# Description: Dumps the contents of the flashchip or partition of an Android device.
#              Only tested on hammerhead (LG Nexus 5 Android 4.4)
# Instructions: needed binaries: adb, fastboot, netstat (, pv, nc (, gzip))
# Usage: $0 <config-file> <output imagefile> <forwarding-port> [device-serial]

tooldir=$(dirname "$0")
### CONFIG BEGIN ###
# Location of the adb and fastboot binaries, use binname if within $PATH

#adb="$tooldir/aosp/adb"
#fb="$tooldir/aosp/fastboot"

adb="adb"
fb="fastboot"

### CONFIG END ###

# Verify we have working binaries
[[ -z "$(which "$adb")" ]] && echo "Could not find adb-binary $adb, check config in script" && exit 2
[[ -z "$(which "$fb")" ]] && echo "Could not find fastboot-binary $fb, check config in script" && exit 2

conf="$1"
output="$2"
port="$3"
serial="$4"

# Verify arguments
[[ $# -lt 3 ]] && echo "Usage: $0 <config-file> <output imagefile> <forwarding-port> [device-serial]" && exit 2

# Verify a port is given
( [[ ! "$port" =~ ^[0-9][0-9]*$ ]] || [[ $port -lt 1 ]] || [[ $port -gt 65535 ]] ) &&
 echo "Given port $port is not a valid one, should be between 1 and 65535 including." && exit 2

# Verify port isn't used yet
prtprog="$(netstat -anp 2>/dev/null | grep ":$port.*LISTEN" | cut -d'/' -f2)"
[[ ! -z "$portprog" ]] && echo "Port $port is already used by $prtprog, choose a different one." && exit 2

# Verify outputfile is availible for writing...
( ( [[ -f "$output" ]] && [[ ! -w "$output" ]] ) ||
 ( [[ ! -e "$output" ]] && [[ ! -w "$(dirname "$output")" ]] ) ) &&
 echo "Cannot write to file $output" && exit 2
# ... and is ok to overwrite if existing
[[ -f "$output" ]] && {
  read -p "File $output exists, ok to overwrite? [y/N]: "
  [[ ! "$REPLY" =~ ^[yY]$ ]] && echo "Not overwriting $output, aborting." && exit 2
}

# Verify and load config
[[ ! -f "$conf" ]] && echo "Given config $conf does not exist." && exit 2
. "$conf"
[[ -z "$devdump" ]] && echo "No blockdevice or partition specified to dump, check config." && exit 2
# Verify recovery image exists and the method used
( [[ -z "$recfile" ]] || [[ ! -f "$recfile" ]] ) && echo "Could not find recoveryimage $recfile, check config" && exit 2
( [[ -z "$recmethod" ]] || [[ ! "$recmethod" =~ ^(simple|feedback|compressed)$ ]] ) &&
 echo "Could not find a valid dump method: $recmethod not one of simple, feedback or compressed. Check config." && exit 2

# Check if device is off, in normal mode or in fastboot
status="unauthorized"
if [[ -z "$serial" ]]; then
  status="$("$adb" devices | tail -n2 | head -n1 | awk '{print $2}')"
  if [[ "$status" == "of" ]]; then
    # Check fastboot
    status="$("$fb" devices | awk '{print $2}')"
    [[ -z "$status" ]] && echo "No device found through adb or fastboot." && exit 2
  fi
else
  status="$("$adb" devices | sed -n "s/^$serial\s*//p")"
  if [[ -z "$status" ]]; then
    # Check fastboot
    status="$("$fb" devices | sed -n "s/$serial\s*//p")"
    [[ -z "$status" ]] && echo "No device with serial $serial found through adb or fastboot." && exit 2
  fi
fi

# Verify adb access is granted (to be able to reboot) or if we're in fastboot
[[ "$status" == "unauthorized" ]] && echo "Authorize adb access on the smartphone first." && exit 2
if [[ "$status" == "fastboot" ]]; then
  [[ -z "$serial" ]] && serial="$("$fb" devices | tail -n1 | awk '{print $1}')"
else
  # Reboot smartphone into fastboot first
  [[ -z "$serial" ]] && serial="$("$adb" devices | tail -n2 | head -n1 | awk '{print $1}')"
  echo "Rebooting device with serial $serial into bootloader."
  "$adb" -s $serial reboot bootloader

  # Wait for device to enter bootloader mode for max 30secs
  echo -n "Waiting for device with serial $serial to enter bootloader."
  secs=0;
  while [[ "$("$fb" devices | sed -n "s/^$serial\s*//p")" != "fastboot" ]] && [[ $secs -lt 30 ]]; do
    echo -n "."
    secs=$(($secs + 1))
    sleep 1
  done
  echo ""
  [[ $secs -eq 30 ]] && echo "Device failed to enter bootloader, aborting." && exit 2
fi

# Verify device is unlocked
[[ -z "$("$fb" -s $serial oem unlock 2>&1 | grep "Already Unlocked")" ]] && echo "Device is not unlocked, aborting." && exit 2

# Boot into recovery with custom image
"$fb" -s $serial boot "$recfile"

# Wait for device to enter recovery mode for max 30secs
echo -n "Waiting for device with serial $serial to enter recovery."
secs=0;
while [[ "$("$adb" devices | sed -n "s/^$serial\s*//p")" != "recovery" ]] && [[ $secs -lt 30 ]]; do
  echo -n "."
  secs=$(($secs + 1))
  sleep 1
done
echo ""
[[ $secs -eq 30 ]] && echo "Device failed to enter recovery, aborting." && exit 2

# Start dump on device in new shell, 3 methods to choose from (add arg switch later)
dump_simple() {
  echo "Transfer started..."
  # Could send to bg and add loop checking size and if transfer is done...
  "$adb" -s $serial pull $devdump "$output"
}

dump_w_output() {
  # Prepare for dump: forward port
  "$adb" -s $serial forward tcp:$port tcp:$port

  # Includes feedback about current size and speed, needs receiving end
  "$adb" -s $serial shell "/sbin/busybox dd if=$devdump | /sbin/busybox nc -l -p $port" 2>/dev/null &
  # Accept dump in this shell, wait 2s to connect
  echo "Waiting for transfer to start..."
  sleep 2
  nc 127.0.0.1 $port | pv > "$output"
}

dump_compressed() {
  # Prepare for dump: forward port
  "$adb" -s $serial forward tcp:$port tcp:$port

  # We use gzip as it's faster, even though bzip2 compresses better,
  # the difference does not translate in a faster send
  "$adb" -s $serial shell "/sbin/busybox gzip -c $devdump | /sbin/busybox nc -l -p $port" 2>/dev/null &
  # Accept dump in this shell, wait 2s to connect
  echo "Waiting for transfer to start..."
  sleep 2
  nc 127.0.0.1 $port | pv | gunzip -c > "$output"
}

if [[ "$recmethod" == "simple" ]]; then
  dump_simple
elif [[ "$recmethod" == "compressed" ]]; then
  dump_compressed
else
  # with feedback
  dump_w_output
fi

# Reboot phone like normal
"$adb" -s $serial reboot bootloader
echo -n "Rebooting and waiting for device with serial $serial to enter fastboot boot"
secs=0;
while [[ "$("$fb" devices | sed -n "s/^$serial\s*//p")" != "fastboot" ]] && [[ $secs -lt 30 ]]; do
  echo -n "."
  secs=$(($secs + 1))
  sleep 1
done
echo ""
[[ $secs -eq 30 ]] && echo "Device failed to enter bootloader, aborting." && exit 2
echo "Finished."
