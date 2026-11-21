 




















#ifndef __CONSOLE_CONTROL_H__
#define __CONSOLE_CONTROL_H__

#define EFI_CONSOLE_CONTROL_PROTOCOL_GUID \
  { 0xf42f7782, 0x12e, 0x4c12, { 0x99, 0x56, 0x49, 0xf9, 0x43, 0x4, 0xf7, 0x21 } }

struct _EFI_CONSOLE_CONTROL_PROTOCOL;


typedef enum {
  EfiConsoleControlScreenText,
  EfiConsoleControlScreenGraphics,
  EfiConsoleControlScreenMaxValue
} EFI_CONSOLE_CONTROL_SCREEN_MODE;


typedef
EFI_STATUS
(EFIAPI *EFI_CONSOLE_CONTROL_PROTOCOL_GET_MODE) (
  IN  struct _EFI_CONSOLE_CONTROL_PROTOCOL *This,
  OUT EFI_CONSOLE_CONTROL_SCREEN_MODE   *Mode,
  OUT BOOLEAN                           *UgaExists,   OPTIONAL  
  OUT BOOLEAN                           *StdInLocked  OPTIONAL
  )
 
















;


typedef
EFI_STATUS
(EFIAPI *EFI_CONSOLE_CONTROL_PROTOCOL_SET_MODE) (
  IN  struct _EFI_CONSOLE_CONTROL_PROTOCOL *This,
  OUT EFI_CONSOLE_CONTROL_SCREEN_MODE   Mode
  )
 













;


typedef
EFI_STATUS
(EFIAPI *EFI_CONSOLE_CONTROL_PROTOCOL_LOCK_STD_IN) (
  IN struct _EFI_CONSOLE_CONTROL_PROTOCOL *This,
  IN CHAR16                             *Password
  )
 













;



typedef struct _EFI_CONSOLE_CONTROL_PROTOCOL {
  EFI_CONSOLE_CONTROL_PROTOCOL_GET_MODE           GetMode;
  EFI_CONSOLE_CONTROL_PROTOCOL_SET_MODE           SetMode;
  EFI_CONSOLE_CONTROL_PROTOCOL_LOCK_STD_IN        LockStdIn;
} EFI_CONSOLE_CONTROL_PROTOCOL;

 

#endif
