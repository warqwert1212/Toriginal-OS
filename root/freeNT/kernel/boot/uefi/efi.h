#ifndef _TRBLU_EFI_H
#define _TRBLU_EFI_H

/* =============================================================================
 * EFI.H — minimal UEFI type/protocol definitions for TRBLU
 *
 * TRBLU (Toriginal OS Runtime Boot Loader, UEFI variant) needs exactly
 * five UEFI protocols to do its job: console output (for progress
 * messages, matching TRBL's BIOS-path "no bullshit, tell you exactly
 * what happened" philosophy), Graphics Output (framebuffer, replacing
 * VBE/VESA int 0x10 calls), Block I/O (raw LBA disk reads, replacing
 * int 0x13), Loaded Image (to find which disk we booted from), and
 * Boot Services itself (to get all of the above and to exit boot
 * services before jumping to the kernel).
 *
 * There's no gnu-efi or any other UEFI headers available in this build
 * environment (no network access to fetch them), so this is a from-
 * scratch reimplementation of just the subset of struct layouts TRBLU
 * touches, written directly against the public UEFI specification's
 * documented ABI (struct field order/sizes/GUIDs are a wire format
 * every UEFI implementation and every UEFI toolchain agrees on -
 * that's the entire point of a firmware interface spec - not anyone's
 * copyrightable expression). Every GUID and struct field below exists
 * because TRBLU calls it; this is intentionally not a general-purpose
 * EFI headers package.
 * ========================================================================= */

#include <stdint.h>

/* UEFI x86_64 uses the Microsoft x64 calling convention for every
 * protocol call, not System V - every function pointer type below
 * must carry this attribute or the ABI will silently disagree with
 * firmware about which registers arguments arrive in. */
#define EFIAPI __attribute__((ms_abi))

typedef uint64_t UINTN;
typedef int64_t  INTN;
typedef uint64_t EFI_STATUS;
typedef void    *EFI_HANDLE;
typedef void    *EFI_EVENT;
typedef uint16_t CHAR16;
typedef uint8_t  BOOLEAN;
typedef void     VOID;

#define EFI_SUCCESS 0
#define EFI_ERROR_BIT (1ULL << 63)
#define EFI_ERROR(x) (((INTN)(x)) < 0)

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} EFI_GUID;

/* ── Simple Text Output Protocol (console messages) ─────────────────────── */
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void            *Reset_placeholder; /* EFI_TEXT_RESET, unused, kept for offset */
    EFI_TEXT_STRING  OutputString;
    void            *TestString;
    void            *QueryMode;
    void            *SetMode;
    void            *SetAttribute;
    void            *ClearScreen;
    void            *SetCursorPosition;
    void            *EnableCursor;
    void            *Mode;
};

/* ── Graphics Output Protocol (framebuffer) ──────────────────────────────── */
typedef struct {
    uint32_t RedMask, GreenMask, BlueMask, ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    uint64_t FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *This, uint32_t ModeNumber,
    UINTN *SizeOfInfo, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);
typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *This, uint32_t ModeNumber);
struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE   SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

/* ── Block I/O Protocol (raw LBA disk reads - same layout TRBL's BIOS
 * path already reads/writes: settings sector at LBA 2000, kernel.elf
 * spare copy at LBA 65-1088, see include/trbl_settings.h) ─────────────── */
typedef struct {
    uint32_t MediaId;
    BOOLEAN  RemovableMedia;
    BOOLEAN  MediaPresent;
    BOOLEAN  LogicalPartition;
    BOOLEAN  ReadOnly;
    BOOLEAN  WriteCaching;
    uint8_t  _pad[3];
    uint32_t BlockSize;
    uint32_t IoAlign;
    uint64_t LastBlock;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO_PROTOCOL EFI_BLOCK_IO_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_BLOCK_READ)(
    EFI_BLOCK_IO_PROTOCOL *This, uint32_t MediaId, uint64_t LBA,
    UINTN BufferSize, void *Buffer);
struct EFI_BLOCK_IO_PROTOCOL {
    uint64_t Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    void *Reset;
    EFI_BLOCK_READ ReadBlocks;
    void *WriteBlocks;
    void *FlushBlocks;
};

#define EFI_BLOCK_IO_PROTOCOL_GUID \
    { 0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

/* ── Loaded Image Protocol (find which device we booted from) ───────────── */
typedef struct {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    void *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    uint32_t LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    uint64_t ImageSize;
} EFI_LOADED_IMAGE_PROTOCOL;

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

/* ── Memory map (GetMemoryMap / ExitBootServices) ────────────────────────── */
typedef struct {
    uint32_t Type;
    uint32_t _pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* ── Boot Services ────────────────────────────────────────────────────────
 * Only the entries TRBLU actually calls are given real types; everything
 * before/after them in the table is a raw void* placeholder purely to
 * keep every real field at its correct byte offset (UEFI's tables are a
 * fixed ABI - skipping an entry without a placeholder would misalign
 * every field after it). */
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    UINTN Type, UINTN MemoryType, UINTN Pages, uint64_t *Memory);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    UINTN *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap, UINTN *MapKey,
    UINTN *DescriptorSize, uint32_t *DescriptorVersion);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    EFI_GUID *Protocol, void *Registration, void **Interface);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE Handle, EFI_GUID *Protocol, void **Interface);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE ImageHandle, UINTN MapKey);
typedef VOID (EFIAPI *EFI_STALL_T)(UINTN Microseconds);

typedef struct {
    uint8_t  _hdr[24];                      /* EFI_TABLE_HEADER */
    void    *_pad0[2];
    EFI_ALLOCATE_PAGES AllocatePages;
    void    *_pad1[3];
    EFI_GET_MEMORY_MAP GetMemoryMap;
    void    *_pad2[3];
    void    *_pad3[9];
    EFI_STALL_T Stall;
    void    *_pad4[7];
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void    *_pad5[6];
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void    *_pad6[5];
    EFI_LOCATE_PROTOCOL LocateProtocol;
} EFI_BOOT_SERVICES;

/* ── System Table ─────────────────────────────────────────────────────────
 * Field offsets verified against the public UEFI spec's documented
 * layout (Table Header, then Firmware Vendor/Revision, then ConsoleIn
 * handle+pointer, ConsoleOut handle+pointer, StdErr handle+pointer,
 * RuntimeServices, BootServices, ...). */
typedef struct {
    uint8_t  _hdr[24];
    CHAR16  *FirmwareVendor;
    uint32_t FirmwareRevision;
    uint32_t _pad0;
    EFI_HANDLE ConsoleInHandle;
    void    *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    void    *StdErr;
    void    *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
} EFI_SYSTEM_TABLE;

#endif /* _TRBLU_EFI_H */
