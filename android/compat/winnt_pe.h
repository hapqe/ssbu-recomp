/* The PE image structures of winnt.h, for the Android port. Android modules are ELF: code that walks PE headers is
 * guarded by SSBU_ANDROID (or checks e_magic first, which an ELF image fails), so these types only let it compile. */
#ifndef ANDROID_COMPAT_WINNT_PE_H
#define ANDROID_COMPAT_WINNT_PE_H
#define IMAGE_DOS_SIGNATURE 0x5A4D
#define IMAGE_NT_SIGNATURE 0x00004550
#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16
#define IMAGE_DIRECTORY_ENTRY_EXPORT 0
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION 3
#define IMAGE_SIZEOF_SHORT_NAME 8
#define IMAGE_SCN_CNT_CODE 0x20
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000
#define IMAGE_SCN_MEM_WRITE 0x80000000
typedef struct _IMAGE_DOS_HEADER { WORD e_magic, e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc, e_ss, e_sp, e_csum, e_ip, e_cs, e_lfarlc, e_ovno, e_res[4], e_oemid, e_oeminfo, e_res2[10]; LONG e_lfanew; } IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;
typedef struct _IMAGE_FILE_HEADER { WORD Machine, NumberOfSections; DWORD TimeDateStamp, PointerToSymbolTable, NumberOfSymbols; WORD SizeOfOptionalHeader, Characteristics; } IMAGE_FILE_HEADER, *PIMAGE_FILE_HEADER;
typedef struct _IMAGE_DATA_DIRECTORY { DWORD VirtualAddress, Size; } IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;
typedef struct _IMAGE_OPTIONAL_HEADER64 { WORD Magic; BYTE MajorLinkerVersion, MinorLinkerVersion; DWORD SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData, AddressOfEntryPoint, BaseOfCode; ULONGLONG ImageBase; DWORD SectionAlignment, FileAlignment; WORD MajorOperatingSystemVersion, MinorOperatingSystemVersion, MajorImageVersion, MinorImageVersion, MajorSubsystemVersion, MinorSubsystemVersion; DWORD Win32VersionValue, SizeOfImage, SizeOfHeaders, CheckSum; WORD Subsystem, DllCharacteristics; ULONGLONG SizeOfStackReserve, SizeOfStackCommit, SizeOfHeapReserve, SizeOfHeapCommit; DWORD LoaderFlags, NumberOfRvaAndSizes; IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES]; } IMAGE_OPTIONAL_HEADER64, IMAGE_OPTIONAL_HEADER, *PIMAGE_OPTIONAL_HEADER64;
typedef struct _IMAGE_NT_HEADERS64 { DWORD Signature; IMAGE_FILE_HEADER FileHeader; IMAGE_OPTIONAL_HEADER64 OptionalHeader; } IMAGE_NT_HEADERS64, IMAGE_NT_HEADERS, *PIMAGE_NT_HEADERS64, *PIMAGE_NT_HEADERS;
typedef struct _IMAGE_SECTION_HEADER { BYTE Name[IMAGE_SIZEOF_SHORT_NAME]; union { DWORD PhysicalAddress, VirtualSize; } Misc; DWORD VirtualAddress, SizeOfRawData, PointerToRawData, PointerToRelocations, PointerToLinenumbers; WORD NumberOfRelocations, NumberOfLinenumbers; DWORD Characteristics; } IMAGE_SECTION_HEADER, *PIMAGE_SECTION_HEADER;
typedef struct _IMAGE_EXPORT_DIRECTORY { DWORD Characteristics, TimeDateStamp; WORD MajorVersion, MinorVersion; DWORD Name, Base, NumberOfFunctions, NumberOfNames, AddressOfFunctions, AddressOfNames, AddressOfNameOrdinals; } IMAGE_EXPORT_DIRECTORY, *PIMAGE_EXPORT_DIRECTORY;
#define IMAGE_FIRST_SECTION(nt) ((PIMAGE_SECTION_HEADER)((ULONG_PTR)(nt) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + ((nt))->FileHeader.SizeOfOptionalHeader))
#endif
