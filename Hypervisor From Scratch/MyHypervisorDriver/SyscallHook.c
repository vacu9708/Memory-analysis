#include <ntddk.h>
#include <Windef.h>
#include "Hooks.h"
#include "Common.h"
#include "Logging.h"

/* Get the kernel base and Image size */
PVOID SyscallHookGetKernelBase(PULONG pImageSize)
{
	NTSTATUS status;
	ZWQUERYSYSTEMINFORMATION ZwQSI = 0;
	UNICODE_STRING routineName;
	PVOID pModuleBase = NULL;
	PSYSTEM_MODULE_INFORMATION pSystemInfoBuffer = NULL;
	ULONG SystemInfoBufferSize = 0;


	RtlInitUnicodeString(&routineName, L"ZwQuerySystemInformation");
	ZwQSI = (ZWQUERYSYSTEMINFORMATION)MmGetSystemRoutineAddress(&routineName);
	if (!ZwQSI)
		return NULL;


	status = ZwQSI(SystemModuleInformation,
		&SystemInfoBufferSize,
		0,
		&SystemInfoBufferSize);

	if (!SystemInfoBufferSize)
	{
		LogInfo("ZwQuerySystemInformation (1) failed");
		return NULL;
	}

	pSystemInfoBuffer = (PSYSTEM_MODULE_INFORMATION)ExAllocatePool(NonPagedPool, SystemInfoBufferSize * 2);

	if (!pSystemInfoBuffer)
	{
		LogInfo("ExAllocatePool failed");
		return NULL;
	}

	memset(pSystemInfoBuffer, 0, SystemInfoBufferSize * 2);

	status = ZwQSI(SystemModuleInformation,
		pSystemInfoBuffer,
		SystemInfoBufferSize * 2,
		&SystemInfoBufferSize);

	if (NT_SUCCESS(status))
	{
		pModuleBase = pSystemInfoBuffer->Module[0].ImageBase;
		if (pImageSize)
			*pImageSize = pSystemInfoBuffer->Module[0].ImageSize;
	}
	else {
		LogInfo("ZwQuerySystemInformation (2) failed");
		return NULL;
	}

	ExFreePool(pSystemInfoBuffer);
	return pModuleBase;
}

/* Find SSDT address of Nt fucntions and W32Table */
BOOLEAN SyscallHookFindSsdt(PUINT64 NtTable, PUINT64 Win32kTable)
{
	ULONG kernelSize = 0;
	ULONG_PTR kernelBase;
	const unsigned char KiSystemServiceStartPattern[] = { 0x8B, 0xF8, 0xC1, 0xEF, 0x07, 0x83, 0xE7, 0x20, 0x25, 0xFF, 0x0F, 0x00, 0x00 };
	const ULONG signatureSize = sizeof(KiSystemServiceStartPattern);
	BOOLEAN found = FALSE;
	LONG relativeOffset = 0;
	ULONG_PTR addressAfterPattern;
	ULONG_PTR address;
	SSDTStruct* shadow;
	PVOID ntTable;
	PVOID win32kTable;

	//x64 code
	kernelBase = (ULONG_PTR)SyscallHookGetKernelBase(&kernelSize);

	if (kernelBase == 0 || kernelSize == 0)
		return FALSE;

	// Find KiSystemServiceStart

	ULONG KiSSSOffset;
	for (KiSSSOffset = 0; KiSSSOffset < kernelSize - signatureSize; KiSSSOffset++)
	{
		if (RtlCompareMemory(((unsigned char*)kernelBase + KiSSSOffset), KiSystemServiceStartPattern, signatureSize) == signatureSize)
		{
			found = TRUE;
			break;
		}
	}

	if (!found)
		return FALSE;

	addressAfterPattern = kernelBase + KiSSSOffset + signatureSize;
	address = addressAfterPattern + 7; // Skip lea r10,[nt!KeServiceDescriptorTable]
	// lea r11, KeServiceDescriptorTableShadow
	if ((*(unsigned char*)address == 0x4c) &&
		(*(unsigned char*)(address + 1) == 0x8d) &&
		(*(unsigned char*)(address + 2) == 0x1d))
	{
		relativeOffset = *(LONG*)(address + 3);
	}

	if (relativeOffset == 0)
		return FALSE;

	shadow = (SSDTStruct*)(address + relativeOffset + 7);

	ntTable = (PVOID)shadow;
	win32kTable = (PVOID)((ULONG_PTR)shadow + 0x20);    // Offset showed in Windbg

	*NtTable = ntTable;
	*Win32kTable = win32kTable;

	return TRUE;
}

/* Find entry from SSDT table of Nt fucntions and W32Table syscalls */
PVOID SyscallHookGetFunctionAddress(INT32 ApiNumber, BOOLEAN GetFromWin32k)
{
	SSDTStruct* SSDT;
	BOOLEAN Result;
	ULONG_PTR SSDTbase;
	ULONG ReadOffset;
	UINT64 NtTable, Win32kTable;

	// Read the address og SSDT
	Result = SyscallHookFindSsdt(&NtTable, &Win32kTable);

	if (!Result)
	{
		LogInfo("SSDT not found");
		return 0;
	}

	if (!GetFromWin32k)
	{
		SSDT = NtTable;
	}
	else
	{
		// Win32k APIs start from 0x1000
		ApiNumber = ApiNumber - 0x1000;
		SSDT = Win32kTable;
	}

	SSDTbase = (ULONG_PTR)SSDT->pServiceTable;

	if (!SSDTbase)
	{
		LogInfo("ServiceTable not found");
		return 0;
	}
	return (PVOID)((SSDT->pServiceTable[ApiNumber] >> 4) + SSDTbase);

}


/* Hook function that hooks NtCreateFile */
NTSTATUS NtCreateFileHook(
	PHANDLE            FileHandle,
	ACCESS_MASK        DesiredAccess,
	POBJECT_ATTRIBUTES ObjectAttributes,
	PIO_STATUS_BLOCK   IoStatusBlock,
	PLARGE_INTEGER     AllocationSize,
	ULONG              FileAttributes,
	ULONG              ShareAccess,
	ULONG              CreateDisposition,
	ULONG              CreateOptions,
	PVOID              EaBuffer,
	ULONG              EaLength
)
{
	HANDLE kFileHandle;
	NTSTATUS ConvertStatus;
	UNICODE_STRING kObjectName;
	ANSI_STRING FileNameA;

	kObjectName.Buffer = NULL;

	__try
	{

		ProbeForRead(FileHandle, sizeof(HANDLE), 1);
		ProbeForRead(ObjectAttributes, sizeof(OBJECT_ATTRIBUTES), 1);
		ProbeForRead(ObjectAttributes->ObjectName, sizeof(UNICODE_STRING), 1);
		ProbeForRead(ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length, 1);

		kFileHandle = *FileHandle;
		kObjectName.Length = ObjectAttributes->ObjectName->Length;
		kObjectName.MaximumLength = ObjectAttributes->ObjectName->MaximumLength;
		kObjectName.Buffer = ExAllocatePoolWithTag(NonPagedPool, kObjectName.MaximumLength, 0xA);
		RtlCopyUnicodeString(&kObjectName, ObjectAttributes->ObjectName);

		ConvertStatus = RtlUnicodeStringToAnsiString(&FileNameA, ObjectAttributes->ObjectName, TRUE);
		LogInfo("NtCreateFile called for : %s", FileNameA.Buffer);

	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}

	if (kObjectName.Buffer)
	{
		ExFreePoolWithTag(kObjectName.Buffer, 0xA);
	}


	return NtCreateFileOrig(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes,
		ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}


/* Make examples for testing hidden hooks */
VOID SyscallHookTest() {

	// Note that this syscall number is only valid for Windows 10 1909, you have to find the syscall number of NtCreateFile based on
	// Your Windows version, please visit https://j00ru.vexillium.org/syscalls/nt/64/ for finding NtCreateFile's Syscall number for your Windows.
	
	INT32 ApiNumberOfNtCreateFile = 0x0055;
	PVOID ApiLocationFromSSDTOfNtCreateFile = SyscallHookGetFunctionAddress(ApiNumberOfNtCreateFile, FALSE);

	if (!ApiLocationFromSSDTOfNtCreateFile)
	{
		LogInfo("Error in finding base address.");
		return FALSE;
	}

	if (EptPageHook(ApiLocationFromSSDTOfNtCreateFile, NtCreateFileHook, (PVOID*)&NtCreateFileOrig, FALSE, FALSE, TRUE))
	{
		LogInfo("Hook appkied to address of API Number : 0x%x at %llx\n", ApiNumberOfNtCreateFile, ApiLocationFromSSDTOfNtCreateFile);
	}
}