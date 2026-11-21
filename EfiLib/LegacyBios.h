/*
 * UEFI Legacy BIOS protocol, BBS table and CSM data structures. Binary layout
 * and calling convention match platform firmware interfaces; do not change
 * field widths, packing or protocol signatures for cosmetic consistency.
 */

#ifndef _EFI_LEGACY_BIOS_H_
#define _EFI_LEGACY_BIOS_H_

#pragma pack(1)

typedef UINT8                       SERIAL_MODE;
typedef UINT8                       PARALLEL_MODE;
/*
 * The legacy firmware table begins with a signature, checksum and length.
 * Packing is part of the data contract since both firmware components must
 * agree on byte offsets, including the segment and offset fields used for
 * 16-bit calls.
 */
#define EFI_COMPATIBILITY16_TABLE_SIGNATURE SIGNATURE_32 ('I', 'F', 'E', '$')

typedef struct {

  UINT32                            Signature;

  UINT8                             TableChecksum;

  UINT8                             TableLength;

  UINT8                             EfiMajorRevision;

  UINT8                             EfiMinorRevision;

  UINT8                             TableMajorRevision;

  UINT8                             TableMinorRevision;

  UINT16                            Reserved;

  UINT16                            Compatibility16CallSegment;

  UINT16                            Compatibility16CallOffset;

  UINT16                            PnPInstallationCheckSegment;

  UINT16                            PnPInstallationCheckOffset;

  UINT32                            EfiSystemTable; 

  UINT32                            OemIdStringPointer;

  UINT32                            AcpiRsdPtrPointer;

  UINT16                            OemRevision;

  UINT32                            E820Pointer;

  UINT32                            E820Length;

  UINT32                            IrqRoutingTablePointer;

  UINT32                            IrqRoutingTableLength;

  UINT32                            MpTablePtr;

  UINT32                            MpTableLength;

  UINT16                            OemIntSegment;

  UINT16                            OemIntOffset;

  UINT16                            Oem32Segment;

  UINT16                            Oem32Offset;

  UINT16                            Oem16Segment;

  UINT16                            Oem16Offset;

  UINT16                            TpmSegment;

  UINT16                            TpmOffset;

  UINT32                            IbvPointer;

  UINT32                            PciExpressBase;

  UINT8                             LastPciBus;
} EFI_COMPATIBILITY16_TABLE;

typedef enum {

  Legacy16InitializeYourself    = 0x0000,

  Legacy16UpdateBbs             = 0x0001,

  Legacy16PrepareToBoot         = 0x0002,

  Legacy16Boot                  = 0x0003,

  Legacy16RetrieveLastBootDevice = 0x0004,

  Legacy16DispatchOprom         = 0x0005,

  Legacy16GetTableAddress       = 0x0006,

  Legacy16SetKeyboardLeds       = 0x0007,

  Legacy16InstallPciHandler     = 0x0008
} EFI_COMPATIBILITY_FUNCTIONS;

typedef struct {
  UINT16  PnPInstallationCheckSegment;   
  UINT16  PnPInstallationCheckOffset;    
  UINT16  OpromSegment;                  
  UINT8   PciBus;                        
  UINT8   PciDeviceFunction;             
  UINT8   NumberBbsEntries;              

  UINT32  BbsTablePointer;               
  UINT16  RuntimeSegment;                

} EFI_DISPATCH_OPROM_TABLE;

typedef struct {

  UINT32                            BiosLessThan1MB;

  UINT32                            HiPmmMemory;

  UINT32                            HiPmmMemorySizeInBytes;

  UINT16                            ReverseThunkCallSegment;

  UINT16                            ReverseThunkCallOffset;

  UINT32                            NumberE820Entries;

  UINT32                            OsMemoryAbove1Mb;

  UINT32                            ThunkStart;

  UINT32                            ThunkSizeInBytes;

  UINT32                            LowPmmMemory;

  UINT32                            LowPmmMemorySizeInBytes;
} EFI_TO_COMPATIBILITY16_INIT_TABLE;

typedef struct {
  UINT16                            Address;     
  UINT8                             Irq;         
  SERIAL_MODE                       Mode;        
} DEVICE_PRODUCER_SERIAL;

#define DEVICE_SERIAL_MODE_NORMAL               0x00
#define DEVICE_SERIAL_MODE_IRDA                 0x01
#define DEVICE_SERIAL_MODE_ASK_IR               0x02
#define DEVICE_SERIAL_MODE_DUPLEX_HALF          0x00
#define DEVICE_SERIAL_MODE_DUPLEX_FULL          0x10

typedef struct {
  UINT16                            Address;   
  UINT8                             Irq;       
  UINT8                             Dma;       
  PARALLEL_MODE                     Mode;      
} DEVICE_PRODUCER_PARALLEL;

#define DEVICE_PARALLEL_MODE_MODE_OUTPUT_ONLY   0x00
#define DEVICE_PARALLEL_MODE_MODE_BIDIRECTIONAL 0x01
#define DEVICE_PARALLEL_MODE_MODE_EPP           0x02
#define DEVICE_PARALLEL_MODE_MODE_ECP           0x03

typedef struct {
  UINT16                            Address;           
  UINT8                             Irq;               
  UINT8                             Dma;               
  UINT8                             NumberOfFloppy;    
} DEVICE_PRODUCER_FLOPPY;

typedef struct {
  UINT32                            A20Kybd : 1;       
  UINT32                            A20Port90 : 1;     
  UINT32                            Reserved : 30;     
} LEGACY_DEVICE_FLAGS;

typedef struct {
  DEVICE_PRODUCER_SERIAL            Serial[4];       
  DEVICE_PRODUCER_PARALLEL          Parallel[3];     
  DEVICE_PRODUCER_FLOPPY            Floppy;          
  UINT8                             MousePresent;    
  LEGACY_DEVICE_FLAGS               Flags;           
} DEVICE_PRODUCER_DATA_HEADER;

typedef struct {
  UINT16                            Raw[256];      
} ATAPI_IDENTIFY;

typedef struct {

  UINT16                            Status;   

  UINT32                            Bus;

  UINT32                            Device;

  UINT32                            Function;

  UINT16                            CommandBaseAddress;

  UINT16                            ControlBaseAddress;

  UINT16                            BusMasterAddress;

  UINT8                             HddIrq;

  ATAPI_IDENTIFY                    IdentifyDrive[2];
} HDD_INFO;

#define HDD_PRIMARY               0x01
#define HDD_SECONDARY             0x02
#define HDD_MASTER_ATAPI_CDROM    0x04
#define HDD_SLAVE_ATAPI_CDROM     0x08
#define HDD_MASTER_IDE            0x20
#define HDD_SLAVE_IDE             0x40
#define HDD_MASTER_ATAPI_ZIPDISK  0x10
#define HDD_SLAVE_ATAPI_ZIPDISK   0x80

typedef struct {
  UINT16                            OldPosition : 4;     
  UINT16                            Reserved1 : 4;       
  UINT16                            Enabled : 1;         
  UINT16                            Failed : 1;          

  UINT16                            MediaPresent : 2;
  UINT16                            Reserved2 : 4;       
} BBS_STATUS_FLAGS;

typedef struct {

  UINT16                            BootPriority;

  UINT32                            Bus;

  UINT32                            Device;

  UINT32                            Function;

  UINT8                             Class;

  UINT8                             SubClass;

  UINT16                            MfgStringOffset;

  UINT16                            MfgStringSegment;

  UINT16                            DeviceType;

  BBS_STATUS_FLAGS                  StatusFlags;

  UINT16                            BootHandlerOffset;

  UINT16                            BootHandlerSegment;

  UINT16                            DescStringOffset;

  UINT16                            DescStringSegment;

  UINT32                            InitPerReserved;

  UINT32                            AdditionalIrq13Handler;

  UINT32                            AdditionalIrq18Handler;

  UINT32                            AdditionalIrq19Handler;

  UINT32                            AdditionalIrq40Handler;
  UINT8                             AssignedDriveNumber;
  UINT32                            AdditionalIrq41Handler;
  UINT32                            AdditionalIrq46Handler;
  UINT32                            IBV1;
  UINT32                            IBV2;
} BBS_TABLE;

#define BBS_FLOPPY              0x01
#define BBS_HARDDISK            0x02
#define BBS_CDROM               0x03
#define BBS_PCMCIA              0x04
#define BBS_USB                 0x05
#define BBS_EMBED_NETWORK       0x06
#define BBS_BEV_DEVICE          0x80
#define BBS_UNKNOWN             0xff

#define BBS_DO_NOT_BOOT_FROM    0xFFFC
#define BBS_LOWEST_PRIORITY     0xFFFD
#define BBS_UNPRIORITIZED_ENTRY 0xFFFE
#define BBS_IGNORE_ENTRY        0xFFFF

typedef struct {

  UINT16                            Type : 3;

  UINT16                            PortGranularity : 3;

  UINT16                            DataGranularity : 3;

  UINT16                            Reserved : 7;
} SMM_ATTRIBUTES;

#define STANDARD_IO       0x00
#define STANDARD_MEMORY   0x01

#define PORT_SIZE_8       0x00
#define PORT_SIZE_16      0x01
#define PORT_SIZE_32      0x02
#define PORT_SIZE_64      0x03

#define DATA_SIZE_8       0x00
#define DATA_SIZE_16      0x01
#define DATA_SIZE_32      0x02
#define DATA_SIZE_64      0x03

typedef struct {
  UINT16                            Function : 15;
  UINT16                            Owner : 1;
} SMM_FUNCTION;

#define INT15_D042        0x0000
#define GET_USB_BOOT_INFO 0x0001
#define DMI_PNP_50_57     0x0002

#define STANDARD_OWNER    0x0
#define OEM_OWNER         0x1

typedef struct {

  SMM_ATTRIBUTES                    SmmAttributes;

  SMM_FUNCTION                      SmmFunction;

  UINT8                             SmmPort;

  UINT8                             SmmData;
} SMM_ENTRY;

typedef struct {
  UINT16                            NumSmmEntries;     
  SMM_ENTRY                         SmmEntry;          
} SMM_TABLE;

typedef struct {

  UINT8                             DirectoryServiceValidity : 1;

  UINT8                             RabcaUsedFlag : 1;

  UINT8                             ExecuteHddDiagnosticsFlag : 1;

  UINT8                             Reserved : 5;
} UDC_ATTRIBUTES;

typedef struct {

  UDC_ATTRIBUTES                    Attributes;

  UINT8                             DeviceNumber;

  UINT8                             BbsTableEntryNumberForParentDevice;

  UINT8                             BbsTableEntryNumberForBoot;

  UINT8                             BbsTableEntryNumberForHddDiag;

  UINT8                             BeerData[128];

  UINT8                             ServiceAreaData[64];
} UD_TABLE;

#define EFI_TO_LEGACY_MAJOR_VERSION 0x02
#define EFI_TO_LEGACY_MINOR_VERSION 0x00
#define MAX_IDE_CONTROLLER          8

typedef struct {
  UINT16                            MajorVersion;                  
  UINT16                            MinorVersion;                  
  UINT32                            AcpiTable;                     
  UINT32                            SmbiosTable;                   
  UINT32                            SmbiosTableLength;

  DEVICE_PRODUCER_DATA_HEADER       SioData;                       
  UINT16                            DevicePathType;                
  UINT16                            PciIrqMask;                    
  UINT32                            NumberE820Entries;             

  HDD_INFO                          HddInfo[MAX_IDE_CONTROLLER];   
  UINT32                            NumberBbsEntries;              
  UINT32                            BbsTable;                      
  UINT32                            SmmTable;                      
  UINT32                            OsMemoryAbove1Mb;              

  UINT32                            UnconventionalDeviceTable;     

} EFI_TO_COMPATIBILITY16_BOOT_TABLE;

typedef struct {
  UINT8                             PciBus;              
  UINT8                             PciDeviceFun;        
  UINT8                             PciSegment;          
  UINT8                             PciClass;            
  UINT8                             PciSubclass;         
  UINT8                             PciInterface;        

  UINT8                             PrimaryIrq;          
  UINT8                             PrimaryReserved;     
  UINT16                            PrimaryControl;      
  UINT16                            PrimaryBase;         
  UINT16                            PrimaryBusMaster;    

  UINT8                             SecondaryIrq;        
  UINT8                             SecondaryReserved;   
  UINT16                            SecondaryControl;    
  UINT16                            SecondaryBase;       
  UINT16                            SecondaryBusMaster;  
} EFI_LEGACY_INSTALL_PCI_HANDLER;

#pragma pack()

#define EFI_LEGACY_BIOS_PROTOCOL_GUID \
  { \
    0xdb9a1e3d, 0x45cb, 0x4abb, {0x85, 0x3b, 0xe5, 0x38, 0x7f, 0xdb, 0x2e, 0x2d } \
  }

typedef struct _EFI_LEGACY_BIOS_PROTOCOL EFI_LEGACY_BIOS_PROTOCOL;

#define NO_ROM            0x00
#define ROM_FOUND         0x01
#define VALID_LEGACY_ROM  0x02
#define ROM_WITH_CONFIG   0x04      

#define EFI_SEGMENT(_Adr)     (UINT16) ((UINT16) (((UINTN) (_Adr)) >> 4) & 0xf000)
#define EFI_OFFSET(_Adr)      (UINT16) (((UINT16) ((UINTN) (_Adr))) & 0xffff)

#define CARRY_FLAG            0x01

typedef struct {
  UINT32 CF:1;
  UINT32 Reserved1:1;
  UINT32 PF:1;
  UINT32 Reserved2:1;
  UINT32 AF:1;
  UINT32 Reserved3:1;
  UINT32 ZF:1;
  UINT32 SF:1;
  UINT32 TF:1;
  UINT32 IF:1;
  UINT32 DF:1;
  UINT32 OF:1;
  UINT32 IOPL:2;
  UINT32 NT:1;
  UINT32 Reserved4:2;
  UINT32 VM:1;
  UINT32 Reserved5:14;
} EFI_EFLAGS_REG;

typedef struct {
    UINT32           EAX;
    UINT32           EBX;
    UINT32           ECX;
    UINT32           EDX;
    UINT32           ESI;
    UINT32           EDI;
    EFI_EFLAGS_REG   EFlags;
    UINT16           ES;
    UINT16           CS;
    UINT16           SS;
    UINT16           DS;
    UINT16           FS;
    UINT16           GS;
    UINT32           EBP;
    UINT32           ESP;
} EFI_DWORD_REGS;

typedef struct {
  UINT16     CF:1;
  UINT16     Reserved1:1;
  UINT16     PF:1;
  UINT16     Reserved2:1;
  UINT16     AF:1;
  UINT16     Reserved3:1;
  UINT16     ZF:1;
  UINT16     SF:1;
  UINT16     TF:1;
  UINT16     IF:1;
  UINT16     DF:1;
  UINT16     OF:1;
  UINT16     IOPL:2;
  UINT16     NT:1;
  UINT16     Reserved4:1;
} EFI_FLAGS_REG;

typedef struct {
    UINT16           AX;
    UINT16           ReservedAX;
    UINT16           BX;
    UINT16           ReservedBX;
    UINT16           CX;
    UINT16           ReservedCX;
    UINT16           DX;
    UINT16           ReservedDX;
    UINT16           SI;
    UINT16           ReservedSI;
    UINT16           DI;
    UINT16           ReservedDI;
    EFI_FLAGS_REG    Flags;
    UINT16           ReservedFlags;
    UINT16           ES;
    UINT16           CS;
    UINT16           SS;
    UINT16           DS;
    UINT16           FS;
    UINT16           GS;
    UINT16           BP;
    UINT16           ReservedBP;
    UINT16           SP;
    UINT16           ReservedSP;
} EFI_WORD_REGS;

typedef struct {
    UINT8   AL, AH;
    UINT16  ReservedAX;
    UINT8   BL, BH;
    UINT16  ReservedBX;
    UINT8   CL, CH;
    UINT16  ReservedCX;
    UINT8   DL, DH;
    UINT16  ReservedDX;
} EFI_BYTE_REGS;

typedef union {
  EFI_DWORD_REGS  E;
  EFI_WORD_REGS   X;
  EFI_BYTE_REGS   H;
} EFI_IA32_REGISTER_SET;

typedef
BOOLEAN
(EFIAPI *EFI_LEGACY_BIOS_INT86)(
  IN     EFI_LEGACY_BIOS_PROTOCOL  *This,
  IN     UINT8                     BiosInt,
  IN OUT EFI_IA32_REGISTER_SET     *Regs
  );

typedef
BOOLEAN
(EFIAPI *EFI_LEGACY_BIOS_FARCALL86)(
  IN EFI_LEGACY_BIOS_PROTOCOL  *This,
  IN UINT16                    Segment,
  IN UINT16                    Offset,
  IN EFI_IA32_REGISTER_SET     *Regs,
  IN VOID                      *Stack,
  IN UINTN                     StackSize
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LEGACY_BIOS_CHECK_ROM)(
  IN  EFI_LEGACY_BIOS_PROTOCOL  *This,
  IN  EFI_HANDLE                PciHandle,
  OUT VOID                      **RomImage, OPTIONAL
  OUT UINTN                     *RomSize, OPTIONAL
  OUT UINTN                     *Flags
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LEGACY_BIOS_INSTALL_ROM)(
  IN  EFI_LEGACY_BIOS_PROTOCOL  *This,
  IN  EFI_HANDLE                PciHandle,
  IN  VOID                      **RomImage,
  OUT UINTN                     *Flags,
  OUT UINT8                     *DiskStart, OPTIONAL
  OUT UINT8                     *DiskEnd, OPTIONAL
  OUT VOID                      **RomShadowAddress, OPTIONAL
  OUT UINT32                    *ShadowedRomSize OPTIONAL
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LEGACY_BIOS_BOOT)(
  IN EFI_LEGACY_BIOS_PROTOCOL  *This,
  IN BBS_BBS_DEVICE_PATH       *BootOption,
  IN UINT32                    LoadOptionsSize,
  IN VOID                      *LoadOptions
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LEGACY_BIOS_UPDATE_KEYBOARD_LED_STATUS)(
  IN EFI_LEGACY_BIOS_PROTOCOL  *This,
  IN UINT8                     Leds
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LEGACY_BIOS_GET_BBS_INFO)(
  IN     EFI_LEGACY_BIOS_PROTOCOL  *This,
  OUT    UINT16                    *HddCount,
  OUT    HDD_INFO                  **HddInfo,
  OUT    UINT16                    *BbsCount,
  IN OUT BBS_TABLE                 **BbsTable
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LEGACY_BIOS_PREPARE_TO_BOOT_EFI)(
  IN  EFI_LEGACY_BIOS_PROTOCOL  *This,
  OUT UINT16                    *BbsCount,
  OUT BBS_TABLE                 **BbsTable
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LEGACY_BIOS_BOOT_UNCONVENTIONAL_DEVICE)(
  IN EFI_LEGACY_BIOS_PROTOCOL  *This,
  IN UDC_ATTRIBUTES            Attributes,
  IN UINTN                     BbsEntry,
  IN VOID                      *BeerData,
  IN VOID                      *ServiceAreaData
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LEGACY_BIOS_SHADOW_ALL_LEGACY_OPROMS)(
  IN EFI_LEGACY_BIOS_PROTOCOL  *This
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LEGACY_BIOS_GET_LEGACY_REGION)(
  IN  EFI_LEGACY_BIOS_PROTOCOL  *This,
  IN  UINTN                     LegacyMemorySize,
  IN  UINTN                     Region,
  IN  UINTN                     Alignment,
  OUT VOID                      **LegacyMemoryAddress
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LEGACY_BIOS_COPY_LEGACY_REGION)(
  IN EFI_LEGACY_BIOS_PROTOCOL  *This,
  IN UINTN                     LegacyMemorySize,
  IN VOID                      *LegacyMemoryAddress,
  IN VOID                      *LegacyMemorySourceAddress
  );

struct _EFI_LEGACY_BIOS_PROTOCOL {

  EFI_LEGACY_BIOS_INT86                       Int86;

  EFI_LEGACY_BIOS_FARCALL86                   FarCall86;

  EFI_LEGACY_BIOS_CHECK_ROM                   CheckPciRom;

  EFI_LEGACY_BIOS_INSTALL_ROM                 InstallPciRom;

  EFI_LEGACY_BIOS_BOOT                        LegacyBoot;

  EFI_LEGACY_BIOS_UPDATE_KEYBOARD_LED_STATUS  UpdateKeyboardLedStatus;

  EFI_LEGACY_BIOS_GET_BBS_INFO                GetBbsInfo;

  EFI_LEGACY_BIOS_SHADOW_ALL_LEGACY_OPROMS    ShadowAllLegacyOproms;

  EFI_LEGACY_BIOS_PREPARE_TO_BOOT_EFI         PrepareToBootEfi;

  EFI_LEGACY_BIOS_GET_LEGACY_REGION           GetLegacyRegion;

  EFI_LEGACY_BIOS_COPY_LEGACY_REGION          CopyLegacyRegion;

  EFI_LEGACY_BIOS_BOOT_UNCONVENTIONAL_DEVICE  BootUnconventionalDevice;
};

extern EFI_GUID gEfiLegacyBiosProtocolGuid;

#endif
