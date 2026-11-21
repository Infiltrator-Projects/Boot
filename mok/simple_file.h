/*
 * EFI file open/read/close helper declarations for the security layer.
 * The returned file buffer and file-handle lifetimes must be released on
 * all success and failure paths; raw file bytes are not trusted metadata.
 */
EFI_STATUS
simple_file_open (EFI_HANDLE image, CHAR16 *name, EFI_FILE_PROTOCOL **file, UINT64 mode);
EFI_STATUS
simple_file_open_by_handle(EFI_HANDLE device, CHAR16 *name, EFI_FILE_PROTOCOL **file, UINT64 mode);
EFI_STATUS
simple_file_read_all(EFI_FILE_PROTOCOL *file, UINTN *size, void **buffer);
void
simple_file_close(EFI_FILE_PROTOCOL *file);

