#!/bin/bash
# Initially developed at Ghent University as part of a masters thesis promoted
# by prof. dr. ir. Bjorn De Sutter of the Computer Systems Lab in cooperation with ir. Daan Raman from NVISO.
# Author: Christophe Beauval
# Version: 20140220
# Description: Unpacks the bootloader.img and adds zeroes to the extracted
#              images to have the same size as their corresponding partitions.
# Instructions: compile bootldr_unpacker: gcc bootloader_unpacker.c -o bunp
#
### CONFIG BEGIN ###
bunp="./bunp"

# Keep every partition on a newline, format: partitionname:size_in_bytes
ptable="
aboot:524288
rpm:524288
tz:524288
sbl1:1048576
sdi:524288
imgdata:3145728
"
### CONFIG END ###

[[ ! -f "$1" ]] && echo "Usage: $0 <bootloader.img>" && exit 2
[[ ! -f "$bunp" ]] && gcc bootloader_unpacker.c -o bunp

# Unpack with own unpacker, gives partition names on a new line
parts="$("$bunp" "$1")"

# Add zeroes according to ptable
for part in $parts; do
  psize=$(stat -c %s "$part.img")
  tsize=$(echo "$ptable" | sed -n "s/^$part://p")
  towrite=$(($tsize - $psize))
  dd status=noxfer if=/dev/zero of="$part.img" seek=$psize obs=1 ibs=$towrite count=1 2>/dev/null
done
