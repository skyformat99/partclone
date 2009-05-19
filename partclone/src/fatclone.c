/**
 fatclone.c - part of Partclone project
 *
 * Copyright (c) 2007~ Thomas Tsai <thomas at nchc org tw>
 *
 * read FAT12/16/32 super block and bitmap
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "partclone.h"
#include "fatclone.h"
#include "progress.h"

struct FatBootSector fat_sb;
struct FatFsInfo fatfs_info;
int ret;
int FS;
char *fat_type = "FATXX";
char *EXECNAME = "partclone.fat";

/// get fet type
static void get_fat_type(){

    /// fix, 1. make sure fat_sb; 2. the method shoud be check again
    if (fat_sb.u.fat16.ext_signature == 0x29){
	if (fat_sb.u.fat16.fat_name[4] == '6'){
	    FS = FAT_16;
	    fat_type = "FAT16";
	    log_mesg(2, 0, 0, 2, "FAT Type : FAT 16\n");
	} else if (fat_sb.u.fat16.fat_name[4] == '2'){
	    FS = FAT_12;
	    fat_type = "FAT12";
	    log_mesg(2, 0, 0, 2, "FAT Type : FAT 12\n");
	} else {
	    log_mesg(2, 1, 1, 2, "FAT Type : unknow\n");
	}
    } else if (fat_sb.u.fat32.fat_name[4] == '2'){
	FS = FAT_32;
	fat_type = "FAT32";
	log_mesg(2, 0, 0, 2, "FAT Type : FAT 32\n");
    } else {
	log_mesg(0, 1, 1, 2, "Unknown fat type!!\n");
    }
    log_mesg(2, 0, 0, 2, "FS = %i\n", FS);

}

/// return total sectors
unsigned long long get_total_sector()
{
    unsigned long long total_sector = 0;

    /// get fat sectors
    if (fat_sb.sectors != 0)
	total_sector = (unsigned long long)fat_sb.sectors;
    else
	total_sector = (unsigned long long)fat_sb.sector_count;

    return total_sector;

}

///return sec_per_fat
unsigned long long get_sec_per_fat()
{
    unsigned long long sec_per_fat = 0;
    /// get fat length
    if(fat_sb.fat_length != 0)
	sec_per_fat = fat_sb.fat_length;
    else
	sec_per_fat = fat_sb.u.fat32.fat_length;
    return sec_per_fat;

}

///return root sec
unsigned long long get_root_sec()
{
    unsigned long long root_sec = 0;
    root_sec = ((fat_sb.dir_entries * 32) + fat_sb.sector_size - 1) / fat_sb.sector_size;
    return root_sec;
}

/// return cluster count
unsigned long long get_cluster_count()
{
    unsigned long long data_sec = 0;
    unsigned long long cluster_count = 0;
    unsigned long long total_sector = get_total_sector();
    unsigned long long root_sec = get_root_sec();
    unsigned long long sec_per_fat = get_sec_per_fat();

    data_sec = total_sector - ( fat_sb.reserved + (fat_sb.fats * sec_per_fat) + root_sec);
    cluster_count = data_sec / fat_sb.cluster_size;
    return cluster_count;
}

/// check fat status
//return - 0 Filesystem is in valid state.
//return - 1 Filesystem isn't in valid state.
//return - 2 other error.
extern int check_fat_status(){
    int rd = 0;
    uint16_t Fat16_Entry;
    uint32_t Fat32_Entry;
    int fs_error = 2;
    int fs_good = 0;
    int fs_bad = 1;


    /// fix. 1.check ret; 

    if (FS == FAT_16){
	/// FAT[0] contains BPB_Media code
	rd = read(ret, &Fat16_Entry, sizeof(Fat16_Entry));
	log_mesg(2, 0, 0, 2, "Media %x\n", Fat16_Entry);
	if (rd == -1)
	    log_mesg(2, 0, 0, 2, "read Fat16_Entry error\n");
	/// FAT[1] is set for FAT16/FAT32 for dirty/error volume flag
	rd = read(ret, &Fat16_Entry, sizeof(Fat16_Entry));
	if (rd == -1)
	    log_mesg(2, 0, 0, 2, "read Fat16_Entry error\n");
	if (Fat16_Entry & 0x8000)
	    log_mesg(2, 0, 0, 2, "Volume clean!\n");
	else
	    return fs_bad;

	if (Fat16_Entry & 0x4000)
	    log_mesg(2, 0, 0, 2, "I/O correct!\n");
	else 
	    return fs_error;

    } else if (FS == FAT_32) {
	/// FAT[0] contains BPB_Media
	rd = read(ret, &Fat32_Entry, sizeof(Fat32_Entry));
	if (rd == -1)
	    log_mesg(2, 0, 0, 2, "read Fat32_Entry error\n");
	/// FAT[1] is set for FAT16/FAT32 for dirty volume flag
	rd = read(ret, &Fat32_Entry, sizeof(Fat32_Entry));
	if (rd == -1)
	    log_mesg(2, 0, 0, 2, "read Fat32_Entry error\n");
	if (Fat32_Entry & 0x08000000)
	    log_mesg(2, 0, 0, 2, "Volume clean!\n");
	else
	    return fs_bad;

	if (Fat32_Entry & 0x04000000)
	    log_mesg(2, 0, 0, 2, "I/O correct!\n");
	else
	    return fs_error;
    } else if (FS == FAT_12){
	/// FAT[0] contains BPB_Media code
	rd = read(ret, &Fat16_Entry, sizeof(Fat16_Entry));
	log_mesg(2, 0, 0, 2, "Media %x\n", Fat16_Entry);
	if (rd == -1)
	    log_mesg(2, 0, 0, 2, "read Fat12_Entry error\n");
	rd = read(ret, &Fat16_Entry, sizeof(Fat16_Entry));
    } else
        log_mesg(2, 0, 0, 2, "ERR_WRONG_FS\n");
    return fs_good;

}

/// mark reserved sectors as used
static unsigned long long mark_reserved_sectors(char* fat_bitmap, unsigned long long block)
{
    int i = 0;
    int j = 0;
    unsigned long long sec_per_fat = 0;
    unsigned long long root_sec = 0;
    sec_per_fat = get_sec_per_fat();
    root_sec = get_root_sec();

    /// A) the reserved sectors are used
    for (i=0; i < fat_sb.reserved; i++,block++)
        fat_bitmap[block] = 1;

    /// B) the FAT tables are on used sectors
    for (j=0; j < fat_sb.fats; j++)
	for (i=0; i < sec_per_fat ; i++,block++)
	    fat_bitmap[block] = 1;

    /// C) The rootdirectory is on used sectors
    if (root_sec > 0) /// no rootdir sectors on FAT32
        for (i=0; i < root_sec; i++,block++)
            fat_bitmap[block] = 1;
    return block;
}

/// open device
static void fs_open(char* device)
{
    int r = 0;
    char *buffer;

    log_mesg(2, 0, 0, 2, "open device\n");
    ret = open(device, O_RDONLY);

    buffer = (char*)malloc(sizeof(FatBootSector));
    r = read (ret, buffer, sizeof(FatBootSector));
    memcpy(&fat_sb, buffer, sizeof(FatBootSector));
    free(buffer);

    buffer = (char*)malloc(sizeof(FatFsInfo));
    r = read(ret, &fatfs_info, sizeof(FatFsInfo));
    memcpy(&fatfs_info, buffer, sizeof(FatFsInfo));
    free(buffer);

    log_mesg(2, 0, 0, 2, "open device down\n");

}

/// close device
static void fs_close()
{
    close(ret);
}

/// check per FAT32 entry
unsigned long long check_fat32_entry(char* fat_bitmap, unsigned long long block, unsigned long long* bfree, unsigned long long* bused, unsigned long long* DamagedClusters)
{
    uint32_t Fat32_Entry = 0;
    int rd = 0;
    int i = 0, j = 0;

    rd = read(ret, &Fat32_Entry, sizeof(Fat32_Entry));
    if (rd == -1)
	log_mesg(2, 0, 0, 2, "read Fat32_Entry error\n");
    if (Fat32_Entry  == 0x0FFFFFF7) { /// bad FAT32 cluster
	DamagedClusters++;
	log_mesg(2, 0, 0, 2, "bad sec %i\n", block);
	for (j=0; j < fat_sb.cluster_size; j++,block++)
	    fat_bitmap[block] = 0;
    } else if (Fat32_Entry == 0x0000){ /// free
	bfree++;
	for (j=0; j < fat_sb.cluster_size; j++,block++)
	    fat_bitmap[block] = 0;
    } else {
	bused++;
	for (j=0; j < fat_sb.cluster_size; j++,block++)
	    fat_bitmap[block] = 1;
    }

    return block;
}

/// check per FAT16 entry
unsigned long long check_fat16_entry(char* fat_bitmap, unsigned long long block, unsigned long long* bfree, unsigned long long* bused, unsigned long long* DamagedClusters)
{
    uint16_t Fat16_Entry = 0;
    int rd = 0;
    int i = 0, j = 0;
    rd = read(ret, &Fat16_Entry, sizeof(Fat16_Entry));
    if (rd == -1)
	log_mesg(2, 0, 0, 2, "read Fat16_Entry error\n");
    if (Fat16_Entry  == 0xFFF7) { /// bad FAT16 cluster
	DamagedClusters++;
	log_mesg(2, 0, 0, 2, "bad sec %i\n", block);
	for (j=0; j < fat_sb.cluster_size; j++,block++)
	    fat_bitmap[block] = 0;
    } else if (Fat16_Entry == 0x0000){ /// free
	bfree++;
	for (j=0; j < fat_sb.cluster_size; j++,block++)
	    fat_bitmap[block] = 0;
    } else {
	bused++;
	for (j=0; j < fat_sb.cluster_size; j++,block++)
	    fat_bitmap[block] = 1;
    }
    return block;
}

/// check per FAT12 entry
unsigned long long check_fat12_entry(char* fat_bitmap, unsigned long long block, unsigned long long* bfree, unsigned long long* bused, unsigned long long* DamagedClusters)
{
    uint16_t Fat16_Entry = 0;
    uint16_t Fat12_Entry = 0;
    int rd = 0;
    int i = 0, j = 0;
    rd = read(ret, &Fat16_Entry, sizeof(Fat16_Entry));
    if (rd == -1)
	log_mesg(2, 0, 0, 2, "read Fat12_Entry error\n");
    Fat12_Entry = Fat16_Entry>>4;
    if (Fat12_Entry  == 0xFFF7) { /// bad FAT12 cluster
	DamagedClusters++;
	log_mesg(2, 0, 0, 2, "bad sec %i\n", block);
	for (j=0; j < fat_sb.cluster_size; j++,block++)
	    fat_bitmap[block] = 0;
    } else if (Fat12_Entry == 0x0000){ /// free
	bfree++;
	for (j=0; j < fat_sb.cluster_size; j++,block++)
	    fat_bitmap[block] = 0;
    } else {
	bused++;
	for (j=0; j < fat_sb.cluster_size; j++,block++)
	    fat_bitmap[block] = 1;
    }
    return block;
}

/// read super block and write to image head
extern void initial_image_hdr(char* device, image_head* image_hdr)
{
    char sig;
    unsigned long long total_sector = 0;
    unsigned long long bused = 0;
    unsigned long long data_sec = 0;
    unsigned long long sec_per_fat = 0;
    unsigned long long cluster_count = 0;
    unsigned long long free_blocks = 0;

    log_mesg(2, 0, 0, 2, "initial_image start\n");
    fs_open(device);

    get_fat_type();

    total_sector = get_total_sector();

    bused = get_used_block();//so I need calculate by myself.

    memcpy(image_hdr->magic, IMAGE_MAGIC, IMAGE_MAGIC_SIZE);
    memcpy(image_hdr->fs, fat_type, FS_MAGIC_SIZE);
    image_hdr->block_size  = (int)fat_sb.sector_size;
    image_hdr->totalblock  = (unsigned long long)total_sector;
    image_hdr->device_size = (unsigned long long)(total_sector * image_hdr->block_size);
    image_hdr->usedblocks  = (unsigned long long)bused;
    log_mesg(2, 0, 0, 2, "Block Size:%i\n", image_hdr->block_size);
    log_mesg(2, 0, 0, 2, "Total Blocks:%i\n", image_hdr->totalblock);
    log_mesg(2, 0, 0, 2, "Used Blocks:%i\n", image_hdr->usedblocks);
    log_mesg(2, 0, 0, 2, "Device Size:%i\n", image_hdr->device_size);

    fs_close();
    log_mesg(2, 0, 0, 2, "initial_image down\n");
}

/// readbitmap - read and check bitmap
extern void readbitmap(char* device, image_head image_hdr, char* bitmap)
{
    int i = 0, j = 0;
    int rd = 0;
    unsigned long long block = 0, bfree = 0, bused = 0, DamagedClusters = 0;
    unsigned long long cluster_count = 0;
    unsigned long long total_sector = 0;
    int FatReservedBytes = 0;
    uint16_t Fat16_Entry = 0;
    uint32_t Fat32_Entry = 0;
    extern cmd_opt opt;
    int start, res, stop; /// start, range, stop number for progre

    fs_open(device);

    total_sector = get_total_sector();
    cluster_count = get_cluster_count();

    /// init progress
    progress_bar   prog;	/// progress_bar structure defined in progress.h
    start = 0;		    /// start number of progress bar
    stop = cluster_count;	/// get the end of progress number, only used block
    res = 100;		    /// the end of progress number
    progress_init(&prog, start, stop, res, 1);
    
    /// init bitmap
    for (i = 0 ; i < total_sector ; i++)
	bitmap[i] = 1;

    /// A) B) C)
    block = mark_reserved_sectors(bitmap, block);

    /// D) The clusters
    FatReservedBytes = fat_sb.sector_size * fat_sb.reserved;

    /// The first cluster will be seek
    lseek(ret, FatReservedBytes, SEEK_SET);

    /// The second used to check FAT status
    if (check_fat_status() == 1)
	log_mesg(0, 1, 1, 2, "Filesystem isn't in valid state. May be it is not cleanly unmounted.\n\n");
    else if (check_fat_status() == 2)
	log_mesg(0, 1, 1, 2, "I/O error! %X\n");

    for (i=0; i < cluster_count; i++){
        /// If FAT16
        if(FS == FAT_16){
	    block = check_fat16_entry(bitmap, block, &bfree, &bused, &DamagedClusters);
        } else if (FS == FAT_32){ /// FAT32
	    block = check_fat32_entry(bitmap, block, &bfree, &bused, &DamagedClusters);
        } else if (FS == FAT_12){ /// FAT12
	    block = check_fat12_entry(bitmap, block, &bfree, &bused, &DamagedClusters);
        } else 
            log_mesg(2, 0, 0, 2, "error fs\n");
	/// update progress
	progress_update(&prog, i, 0);
    }

    log_mesg(2, 0, 0, 2, "done\n");
    fs_close();

    /// update progress
    progress_update(&prog, 1, 1);
}

/// get_used_block - get FAT used blocks
static unsigned long long get_used_block()
{
    int i = 0;
    int rd = 0;
    unsigned long long block = 0, bfree = 0, bused = 0, DamagedClusters = 0;
    unsigned long long cluster_count = 0, total_sector = 0;
    unsigned long long real_back_block= 0;
    int FatReservedBytes = 0;
    char *fat_bitmap;

    log_mesg(2, 0, 0, 2, "get_used_block start\n");

    total_sector = get_total_sector();
    cluster_count = get_cluster_count();
    
    fat_bitmap = (char *)malloc(total_sector);
    if (fat_bitmap == NULL)
	log_mesg(2, 0, 0, 2, "bitmapalloc error\n");
    memset(fat_bitmap, 1, total_sector);

    /// A) B) C)
    block = mark_reserved_sectors(fat_bitmap, block);

    /// D) The clusters
    FatReservedBytes = fat_sb.sector_size * fat_sb.reserved;

    /// The first fat will be seek
    lseek(ret, FatReservedBytes, SEEK_SET);

    /// The second fat is used to check FAT status
    if (check_fat_status() == 1)
	log_mesg(0, 1, 1, 2, "Filesystem isn't in valid state. May be it is not cleanly unmounted.\n\n");
    else if (check_fat_status() == 2)
	log_mesg(0, 1, 1, 2, "I/O error! %X\n");

    for (i=0; i < cluster_count; i++){
        /// If FAT16
        if(FS == FAT_16){
	    block = check_fat16_entry(fat_bitmap, block, &bfree, &bused, &DamagedClusters);
        } else if (FS == FAT_32){ /// FAT32
	    block = check_fat32_entry(fat_bitmap, block, &bfree, &bused, &DamagedClusters);
        } else if (FS == FAT_12){ /// FAT12
	    block = check_fat12_entry(fat_bitmap, block, &bfree, &bused, &DamagedClusters);
        } else 
            log_mesg(2, 0, 0, 2, "error fs\n");
    }

    while(block < total_sector){
        fat_bitmap[block] = 1;
        block++;
    }


    for (block = 0; block < total_sector; block++)
    {
	if (fat_bitmap[block] == 1) {
	    real_back_block++;
	}
    }
    free(fat_bitmap);
    log_mesg(2, 0, 0, 2, "get_used_block down\n");

    return real_back_block;
}

