#include <ntddk.h>
#include <wdf.h>
#include "Msr.h"
#include "Common.h"
#include "Vmx.h"

/* Power function in order to computer address for MSR bitmaps */
int MathPower(int Base, int Exp) {

	int result;

	result = 1;

	for (;;)
	{
		if (Exp & 1)
		{
			result *= Base;
		}
		Exp >>= 1;
		if (!Exp)
		{
			break;
		}
		Base *= Base;
	}
	return result;
}

// This function is deprecated as we want to supporrt more than 32 processors.
/* Broadcast a function to all logical cores */
BOOLEAN BroadcastToProcessors(ULONG ProcessorNumber, RunOnLogicalCoreFunc Routine)
{

	KIRQL OldIrql;

	KeSetSystemAffinityThread((KAFFINITY)(1 << ProcessorNumber));

	OldIrql = KeRaiseIrqlToDpcLevel();

	Routine(ProcessorNumber);

	KeLowerIrql(OldIrql);

	KeRevertToUserAffinityThread();

	return TRUE;
}

// Note : Because of deadlock and synchronization problem, no longer use this instead use Vmcall with VMCALL_VMXOFF
/* Broadcast a 0x41414141 - 0x42424242 message to CPUID Handler of all logical cores in order to turn off VMX in VMX root-mode */
/*BOOLEAN BroadcastToProcessorsToTerminateVmx(ULONG ProcessorNumber)
{
	KIRQL OldIrql;

	KeSetSystemAffinityThread((KAFFINITY)(1 << ProcessorNumber));

	OldIrql = KeRaiseIrqlToDpcLevel();

	INT32 cpu_info[4];
	__cpuidex(cpu_info, 0x41414141, 0x42424242);

	KeLowerIrql(OldIrql);

	KeRevertToUserAffinityThread();

	return TRUE;
}
*/

/* Set Bits for a special address (used on MSR Bitmaps) */
void SetBit(PVOID Addr, UINT64 bit, BOOLEAN Set) {

	UINT64 byte;
	UINT64 n;
	BYTE* Addr2;

	byte = bit / 8;
	n = bit % 8;

	Addr2 = Addr;

	if (Set)
	{
		Addr2[byte] |= (1 << n);
	}
	else
	{
		Addr2[byte] &= ~(1 << n);
	}
}

/* Get Bits of a special address (used on MSR Bitmaps) */
void GetBit(PVOID Addr, UINT64 bit) {

	UINT64 byte, k;
	BYTE* Addr2;

	byte = 0;
	k = 0;
	byte = bit / 8;
	k = 7 - bit % 8;

	Addr2 = Addr;

	return Addr2[byte] & (1 << k);
}

/* Converts Virtual Address to Physical Address */
UINT64 myVirtualAddressToPhysicalAddress(PVOID VirtualAddress)
{
	KAPC_STATE ApcState;
	PEPROCESS TargetProcess;
	UINT64 physicalAddress;

	if (!NT_SUCCESS(PsLookupProcessByProcessId(myProcessID, &TargetProcess)))
	{
		return 0;
	}
	KeStackAttachProcess(TargetProcess, &ApcState);

	physicalAddress = MmGetPhysicalAddress(VirtualAddress).QuadPart;

	KeUnstackDetachProcess(&ApcState);
	ObDereferenceObject(TargetProcess);
	return physicalAddress;
}

UINT64 VirtualAddressToPhysicalAddress(PVOID VirtualAddress)
{
	return MmGetPhysicalAddress(VirtualAddress).QuadPart;
}

/* Converts Physical Address to Virtual Address */
UINT64 PhysicalAddressToVirtualAddress(UINT64 PhysicalAddress)
{
	KAPC_STATE ApcState;
	PEPROCESS TargetProcess;
	UINT64 physicalAddress;
	PVOID virtualAddress;

	// if (!NT_SUCCESS(PsLookupProcessByProcessId(processID, &TargetProcess)))
	// {
	// 	return 0;
	// }
	// KeStackAttachProcess(TargetProcess, &ApcState);

	PHYSICAL_ADDRESS PhysicalAddr;
	PhysicalAddr.QuadPart = PhysicalAddress;
	virtualAddress = MmGetVirtualForPhysical(PhysicalAddr);

	// KeUnstackDetachProcess(&ApcState);
	// ObDereferenceObject(TargetProcess);
	return (UINT64)virtualAddress;
}

/* Find cr3 of system process*/
UINT64 FindSystemDirectoryTableBase()
{
	// Return CR3 of the system process.
	NT_KPROCESS* SystemProcess = (NT_KPROCESS*)(PsInitialSystemProcess);
	return SystemProcess->DirectoryTableBase;
}

#include <ntddk.h>
#include <ntstrsafe.h> // For RtlStringCchPrintfA
#define MAX_MESSAGE_LENGTH 256 // Define a maximum message length

VOID writeToFile(const char* format, ...) {
    HANDLE fileHandle;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatusBlock;
    UNICODE_STRING fileName;
    NTSTATUS status;
    char* message = NULL;

    // Allocate memory for the message in the non-paged pool
    message = (char*)ExAllocatePool(NonPagedPool, MAX_MESSAGE_LENGTH);
    if (message == NULL) {
        return; // Handle memory allocation failure if necessary
    }

    // Specify the path to the file (ensure the path is correct)
    PCWSTR _fileName = L"\\??\\D:\\hacking\\driver\\my_debugger.txt";
    RtlInitUnicodeString(&fileName, _fileName);

    InitializeObjectAttributes(&objAttr, &fileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    // Create or open the file
    status = ZwCreateFile(&fileHandle, FILE_APPEND_DATA, &objAttr, &ioStatusBlock, NULL,
        FILE_ATTRIBUTE_NORMAL, 0, FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (NT_SUCCESS(status)) {
        ULONG bytesWritten;

        // Initialize the variable argument list
        va_list args;
        va_start(args, format);

        // Format the message using RtlStringCbPrintf
        RtlStringCbVPrintfA(message, MAX_MESSAGE_LENGTH, format, args);

        // Write the formatted message to the file
        status = ZwWriteFile(fileHandle, NULL, NULL, NULL, &ioStatusBlock, (PVOID)message, strlen(message), NULL, NULL);
        if (!NT_SUCCESS(status)) {
            // Handle write error if necessary
        }

        va_end(args); // Clean up the variable argument list
        ZwClose(fileHandle);
    }

    // Free the allocated memory
    ExFreePool(message);
}