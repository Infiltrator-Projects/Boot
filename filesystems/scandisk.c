/*
 * EFI Disk I/O enumeration helper used by filesystem backend discovery.
 * Temporary dummy FSW volumes carry controller Disk I/O and media identity
 * for a callback; they are not automatically mounted filesystems.
 * The callback's result must remain separate from handle enumeration success.
 */

#include "fsw_efi.h"
#ifdef __MAKEWITH_GNUEFI
#include "edk2/DriverBinding.h"
#include "edk2/ComponentName.h"
extern EFI_GUID gMyEfiDiskIoProtocolGuid;
extern EFI_GUID gMyEfiBlockIoProtocolGuid;
#else
#define gMyEfiBlockIoProtocolGuid gEfiBlockIoProtocolGuid
#define gMyEfiDiskIoProtocolGuid gEfiDiskIoProtocolGuid
#endif
#include "../include/refit_call_wrapper.h"

extern struct fsw_host_table   fsw_efi_host_table;
static void dummy_volume_free(struct fsw_volume *vol) { }
static struct fsw_fstype_table   dummy_fstype = {
    { FSW_STRING_TYPE_UTF8, 4, 4, "dummy" },
    sizeof(struct fsw_volume),
    sizeof(struct fsw_dnode),

    NULL,  
    dummy_volume_free,  
    NULL,  
    NULL,  
    NULL,  
    NULL,  
    NULL,  
    NULL,  
    NULL,  
    NULL,  
};

/*
 * Creates a short-lived FSW host binding for one EFI Disk I/O interface.
 * The associated host data and volume allocations must be freed together.
 */
static struct fsw_volume *create_dummy_volume(EFI_DISK_IO *diskio, UINT32 mediaid)
{
    fsw_status_t err;
    struct fsw_volume *vol;
    FSW_VOLUME_DATA *Volume;

    err = fsw_alloc_zero(sizeof(struct fsw_volume), (void **)&vol);
    if(err)
        return NULL;
    err = fsw_alloc_zero(sizeof(FSW_VOLUME_DATA), (void **)&Volume);
    if(err) {
        fsw_free(vol);
        return NULL;
    }

    vol->fstype_table = &dummy_fstype;

    Volume->DiskIo = diskio;
    Volume->MediaId = mediaid;

    vol->host_data = Volume;
    vol->host_table = &fsw_efi_host_table;
    return vol;
}

static struct fsw_volume *clone_dummy_volume(struct fsw_volume *vol)
{
    FSW_VOLUME_DATA *Volume = (FSW_VOLUME_DATA *)vol->host_data;
    return create_dummy_volume(Volume->DiskIo, Volume->MediaId);
}

/*
 * Release the temporary Disk I/O identity before FSW unmount destroys the
 * wrapper volume. A hook receiving this probe may not retain vol/host_data
 * past the current scan iteration without copying the needed information.
 */
static void free_dummy_volume(struct fsw_volume *vol)
{
    fsw_free(vol->host_data);
    fsw_unmount(vol);
}

/*
 * Enumerates EFI Disk I/O handles and invokes a supplied hook on temporary
 * volume wrappers. Missing handles do not prove there are no disks elsewhere.
 * A successful LocateHandleBuffer allocates Handles; this function currently
 * does not release that buffer on the normal path, so repeated scans can leak
 * EFI pool allocations. Probe callback success counts are not mount counts.
 */
static int scan_disks(int (*hook)(struct fsw_volume *, struct fsw_volume *), struct fsw_volume *master)
{
    EFI_STATUS  Status;
    EFI_HANDLE *Handles;
    UINTN       HandleCount = 0;
    UINTN       i;
    UINTN       scanned = 0;

#if defined(__MAKEWITH_GNUEFI)
    Print(L" ");
#endif
    DPRINT(L"Scanning disks\n");
    Status = refit_call5_wrapper(BS->LocateHandleBuffer, ByProtocol, &gMyEfiDiskIoProtocolGuid, NULL, &HandleCount, &Handles);
    if (Status == EFI_NOT_FOUND)
        return -1;   
    for (i = 0; i < HandleCount; i++) {
        EFI_DISK_IO *diskio;
        EFI_BLOCK_IO *blockio;
        Status = refit_call3_wrapper(BS->HandleProtocol, Handles[i], &gMyEfiDiskIoProtocolGuid, (VOID **) &diskio);
        if (Status != 0)
            continue;
        Status = refit_call3_wrapper(BS->HandleProtocol, Handles[i], &gMyEfiBlockIoProtocolGuid, (VOID **) &blockio);
        if (Status != 0)
            continue;
        struct fsw_volume *vol = create_dummy_volume(diskio, blockio->Media->MediaId);
        if(vol) {
            DPRINT(L"Checking disk %d\n", i);
            /*
             * The hook borrows this temporary probe volume for one iteration.
             * A Btrfs member retained past this call needs independent state
             * because the scanner releases the probe immediately afterwards.
             */
            if(hook(master, vol) == FSW_SUCCESS)
                scanned++;
            free_dummy_volume(vol);
        }
    }
    return scanned;
}
