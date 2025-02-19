#include <Windows.h>
#include <conio.h>
#include <iostream>  
#include "Definitions.h"
#include "Configuration.h"
#include <fstream>
#include <string>
#include <vector>

using namespace std;
BOOLEAN IsVmxOffProcessStart; // Show whether the vmxoff process start or not

string GetCpuid()
{
	char SysType[13]; // Array consisting of 13 single bytes/characters
	string CpuID; // The string that will be used to add all the characters toStarting coding in assembly language 

	_asm
	{
		//Execute CPUID with EAX = 0 to get the CPU producer
		xor eax, eax
		cpuid

		//MOV EBX to EAX and get the characters one by one by using shift out right bitwise operation.
		mov eax, ebx
		mov SysType[0], al
		mov SysType[1], ah
		shr eax, 16
		mov SysType[2], al
		mov SysType[3], ah

		//Get the second part the same way but these values are stored in EDX
		mov eax, edx
		mov SysType[4], al
		mov SysType[5], ah
		shr EAX, 16
		mov SysType[6], al
		mov SysType[7], ah

		//Get the third part
		mov eax, ecx
		mov SysType[8], al
		mov SysType[9], ah
		SHR EAX, 16
		mov SysType[10], al
		mov SysType[11], ah
		mov SysType[12], 00
	}

	CpuID.assign(SysType, 12);

	return CpuID;
}


bool VmxSupportDetection()
{

	bool VMX;

	VMX = false;

	__asm {

		xor eax, eax
		inc    eax
		cpuid
		bt     ecx, 0x5
		jc     VMXSupport

		VMXNotSupport :
		jmp     NopInstr

			VMXSupport :
		mov    VMX, 0x1

			NopInstr :
			nop
	}

	return VMX;
}

void PrintAppearance() {

	printf("\n"


		"    _   _                             _                  _____                      ____                 _       _     \n"
		"   | | | |_   _ _ __   ___ _ ____   _(_)___  ___  _ __  |  ___| __ ___  _ __ ___   / ___|  ___ _ __ __ _| |_ ___| |__  \n"
		"   | |_| | | | | '_ \\ / _ \\ '__\\ \\ / / / __|/ _ \\| '__| | |_ | '__/ _ \\| '_ ` _ \\  \\___ \\ / __| '__/ _` | __/ __| '_ \\ \n"
		"   |  _  | |_| | |_) |  __/ |   \\ V /| \\__ \\ (_) | |    |  _|| | | (_) | | | | | |  ___) | (__| | | (_| | || (__| | | |\n"
		"   |_| |_|\\__, | .__/ \\___|_|    \\_/ |_|___/\\___/|_|    |_|  |_|  \\___/|_| |_| |_| |____/ \\___|_|  \\__,_|\\__\\___|_| |_|\n"
		"          |___/|_|                                                                                                     \n"
		"\n\n");
}

#if !UseDbgPrintInsteadOfUsermodeMessageTracking 

static int hookType;
static std::vector<string> addresses;
static int addressCount = 0;
bool appendToFile(const std::string& filePath) {
	std::ofstream file(filePath);  // Open file in append mode
	if (!file.is_open()) {
		return false;  // Failed to open the file
	}
	for (int i = 0; i < addresses.size(); i++)
	{
		file << addresses[i] << std::endl;   // Append content to file
	}
	file.close();      // Close the file
	return true;       // Indicate success
}

void ReadIrpBasedBuffer(HANDLE  Device) {

	BOOL    Status;
	ULONG   ReturnedLength;
	REGISTER_EVENT RegisterEvent;
	UINT32 OperationCode;

	printf(" =============================== Kernel-Mode Logs (Driver) ===============================\n");
	RegisterEvent.hEvent = NULL;
	RegisterEvent.Type = IRP_BASED;
	// allocate buffer for transfering messages
	char* OutputBuffer = (char*)malloc(UsermodeBufferSize);

	try
	{

		while (TRUE) {
			if (!IsVmxOffProcessStart)
			{
				ZeroMemory(OutputBuffer, UsermodeBufferSize);

				Sleep(100);							// we're not trying to eat all of the CPU ;)

				Status = DeviceIoControl(
					Device,							// Handle to device
					IOCTL_REGISTER_EVENT,			// IO Control code
					&RegisterEvent,					// Input Buffer to driver.
					SIZEOF_REGISTER_EVENT * 2,		// Length of input buffer in bytes. (x 2 is bcuz as the driver is x64 and has 64 bit values)
					OutputBuffer,					// Output Buffer from driver.
					UsermodeBufferSize,				// Length of output buffer in bytes.
					&ReturnedLength,				// Bytes placed in buffer.
					NULL							// synchronous call
				);

				if (!Status) {
					printf("Ioctl failed with code %d\n", GetLastError());
					break;
				}
				// printf("\n========================= Kernel Mode (Buffer) =========================\n");

				OperationCode = 0;
				memcpy(&OperationCode, OutputBuffer, sizeof(UINT32));

				// printf("Returned Length : 0x%x \n", ReturnedLength);
				// printf("Operation Code : 0x%x \n", OperationCode);

				switch (OperationCode)
				{
				case OPERATION_LOG_MY_ADDRESS:
					 addresses.push_back(OutputBuffer + sizeof(UINT32));
					 appendToFile("D:\\hacking\\hypervisor\\aimbot\\myAddress.txt");
					 exit(0);
					break;
				case OPERATION_LOG_PLAYERS_ADDRESSES:
					addresses.push_back(OutputBuffer + sizeof(UINT32));
					/*addressCount++;
					if (addressCount >= 50) {*/
						appendToFile("D:\\hacking\\hypervisor\\aimbot\\playersAddresses.txt");
						exit(0);
					//}
					break;
				
				case OPERATION_LOG_NON_IMMEDIATE_MESSAGE:
					printf("A buffer of messages (OPERATION_LOG_NON_IMMEDIATE_MESSAGE) :\n");
					printf("%s", OutputBuffer + sizeof(UINT32));
					break;
				case OPERATION_LOG_INFO_MESSAGE:
					//printf("Information log (OPERATION_LOG_INFO_MESSAGE) :\n");
					printf("%s\n", OutputBuffer + sizeof(UINT32));
					break;
				case OPERATION_LOG_ERROR_MESSAGE:
					printf("Error log (OPERATION_LOG_ERROR_MESSAGE) :\n");
					printf("%s", OutputBuffer + sizeof(UINT32));
					break;
				case OPERATION_LOG_WARNING_MESSAGE:
					printf("Warning log (OPERATION_LOG_WARNING_MESSAGE) :\n");
					printf("%s", OutputBuffer + sizeof(UINT32));
					break;

				default:
					break;
				}
				// printf("\n========================================================================\n");

			}
			else
			{
				// the thread should not work anymore
				return;
			}
		}
	}
	catch (const std::exception&)
	{
		printf("\n Exception !\n");
	}
}

DWORD WINAPI ThreadFunc(void* Data) {
	// Do stuff.  This will be the first function called on the new thread.
	// When this function returns, the thread goes away.  See MSDN for more details.
	// Test Irp Based Notifications
	ReadIrpBasedBuffer(Data);

	return 0;
}
#endif

#include <string>  
int main(int argc, char* argv[]) {
	string CpuID;
	DWORD ErrorNum;
	HANDLE Handle;
	BOOL    Status;

	// Check for correct number of arguments
    if (argc != 4) {
		printf("Wrong arguments\n");
        return 1;
    }

    PROCESS_VADDR process_vaddr;
	process_vaddr.processID = atoi(argv[1]);
    try {
        string va_str = argv[2];
        if (va_str.find("0x") == 0) {
            process_vaddr.VirtualAddress = stoull(va_str.substr(2), nullptr, 16);
        }
        else {
            process_vaddr.VirtualAddress = stoull(va_str, nullptr, 16); // Assuming hex input
        }
    }
    catch (const std::exception&) {
		printf("Wrong arguments\n");
        return 1;
    }
	process_vaddr.type = hookType = atoi(argv[3]);

	// Print Hypervisor From Scratch Message
	PrintAppearance();

	CpuID = GetCpuid();

	printf("[*] The CPU Vendor is : %s \n", CpuID.c_str());

	if (CpuID == "GenuineIntel")
	{
		printf("[*] The Processor virtualization technology is VT-x. \n");
	}
	else
	{
		printf("[*] This program is not designed to run in a non-VT-x environemnt !\n");
		return 1;
	}

	if (VmxSupportDetection())
	{
		printf("[*] VMX Operation is supported by your processor .\n");
	}
	else
	{
		printf("[*] VMX Operation is not supported by your processor .\n");
		return 1;
	}
	
	Handle = CreateFileA("\\\\.\\MyHypervisorDevice",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ |
		FILE_SHARE_WRITE,
		NULL, /// lpSecurityAttirbutes
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL |
		FILE_FLAG_OVERLAPPED,
		NULL); /// lpTemplateFile 

	if (Handle == INVALID_HANDLE_VALUE)
	{
		ErrorNum = GetLastError();
		printf("[*] CreateFile failed : %d\n", ErrorNum);
		return 1;

	}

	// Send IOCTL to start pass the process id and virtual address
	Status = DeviceIoControl(
		Handle,
		MY_START,
		&process_vaddr,
        sizeof(PROCESS_VADDR) * 2,
        NULL,
        0,
        NULL,
        NULL
	);

#if !UseDbgPrintInsteadOfUsermodeMessageTracking 

	HANDLE Thread = CreateThread(NULL, 0, ThreadFunc, Handle, 0, NULL);
	if (Thread) {
		printf("[*] Thread Created successfully !!!");
	}
#endif

	printf("\n[*] Press any key to terminate the VMX operation...\n");

	_getch();

	printf("[*] Terminating VMX !\n");

	// Indicate that the finish process start or not
	IsVmxOffProcessStart = TRUE;

	// Send IOCTL to mark complete all IRP Pending 
	Status = DeviceIoControl(
		Handle,															// Handle to device
		IOCTL_RETURN_IRP_PENDING_PACKETS_AND_DISALLOW_IOCTL,			// IO Control code
		NULL,															// Input Buffer to driver.
		0,																// Length of input buffer in bytes. (x 2 is bcuz as the driver is x64 and has 64 bit values)
		NULL,															// Output Buffer from driver.
		0,																// Length of output buffer in bytes.
		NULL,															// Bytes placed in buffer.
		NULL															// synchronous call
	);
	// wait to make sure we don't use an invalid handle in another Ioctl
	if (!Status) {
		printf("Ioctl failed with code %d\n", GetLastError());
	}
	// Send IRP_MJ_CLOSE to driver to terminate Vmxs
	CloseHandle(Handle);

	printf("\nError : 0x%x\n", GetLastError());

	printf("[*] You're not on hypervisor anymore !");

	exit(0);

	return 0;
}

