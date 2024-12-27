## Purpose

Fills USB drive on Windows with specific letter with garbage data until the disk is full.

## Usage

1. Compile project.
2. Run `usb_filler.exe`
3. Answer prompts and process will launch.

## How it works

Creates files called `filldata_x.dat` and fills them with random data. New file is created if for some reason we can't write to the current one (i.e. 4 GB max file size for FAT32).