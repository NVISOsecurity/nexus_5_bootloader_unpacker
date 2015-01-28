# Nexus 5 bootloader unpacker and imgdata tool

An unpacker for the bootloader.img file included in Google's factory images and a tool to unpack, update, create and view the contents of the imgdata.img file contained in bootloader.img files for the Nexus 5 and present on it as partition #17.

## The programs
**bootloader_unpacker**: Unpacks the bootloader.img file included in the factory images provided by Google. Outputs them in the working directory.

Instructions for compilation: 
```
gcc bootloader_unpacker.c -o bunp
```

Usage: 
```
./bunp <bootloader.img>
```

**imgdata_tool**: Tool to work with the Android imgdata.img present in the bootloader.img for the LG Nexus 5 and listed as partition number 17. It can list the contents and stored options, unpack to PNG, change any of the stored options and change any packed image with a given PNG image. Can also create a new imgdata.img or add images to an existing imgdata.img blob.

Instructions for compilation include two options to compile: 

```
dynamic: gcc -o iunp imgdata_unpacker.c -lpng
static: gcc -o iunp imgdata_unpacker.c -lpng -lz -static
```

Usage:

```
./iunp  -l <imgdata.img> : list info and contents
        -x <imgdata.img> : extract contents in working dir
        -u <imgdata.img> <file1:X[:Y[:W[:H]]]> [...] : update "file1" in <imgdata.img> with given coordinates and size, use - to keep existing value
        -r <imgdata.img> <file1.png>[:X[:Y]] [...] : replace "file1" in <imgdata.img> with given file and optionally new coordinates
        -c <imgdata.img> <file1.png:X:Y> [...] : creates a new imgdata.img (overwriting any existing!).
		
		Arguments X, Y, W, H are 32bit positive integers and can be given as 0x<HEX> and 0<OCT> as well. "file1" name should not be longer than 16 chars, excluding extension, and be in current dir.
```
## Included scripts
**bootldr.sh**: Unpacks the bootloader.img and adds zeroes to the extracted images to have the same size as their corresponding partitions. Output is every processed partition on a newline. This facilitates comparing dumped partitions with those extracted from a bootloader.img file.

It needs the bootloader_unpacker, so compile bootloader_unpacker: 

```
gcc bootloader_unpacker.c -o bunp
```

Usage:
```
./bootldr.sh <bootloader.img>
```

**writer.sh**: Writes the contents of an image to the flashchip of an Android device. Only tested on hammerhead (LG Nexus 5 Android 4.4)
Needed binaries are: adb, fastboot, netstat and depending on the write method also nc and gzip. See config for options.

Usage: 
```
./writer.sh <config-file> <input imagefile> <forwarding-port> [device-serial]
```

**dumper.sh**: Dumps the contents of the flashchip or a partition of an Android device. Only tested on hammerhead (LG Nexus 5 Android 4.4)
Needed binaries: adb, fastboot, netstat and depending on the dump method also pv, nc and gzip. See config for options.

Usage:
```
./dumper.sh <config-file> <output imagefile> <forwarding-port> [device-serial]
```


## Example
So you unlocked your Nexus 5 and want to get rid of the unlocked symbol when you boot your phone. As the factory image is rather large to download just to disable the symbol, you want to dump the imgdata.img partition first:

```
./dumper.sh ./etc/hammerhead.conf imgdata.img 5555
```

We list the contents of the dumped partition and note the original Width and Height of the unlocked image:
```
./iunp -l imgdata.img
```
Then we disable the unlocked symbol (compile the imgdata_tool first!):
```
./iunp -u imgdata.img unlocked:-:-:0:0
```

We verify that the Width and Height are set to 0 for the unlocked image:
```
./iunp -l imgdata.img
```
And then we write the changes to our phone:
```
./writer.sh ./etc/hammerhead.conf imgdata.img 5555
```
