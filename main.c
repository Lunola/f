#define MY_EFI_USE_MS_ABI 1
#define MyCallingType __attribute__((ms_abi))

#include <efi.h>
#include <efilib.h>
#include "dummy.h"

#define BASE_CMD_OP 0x2561
#define MY_VARIABLE_NAME L"Neirox0z"
#define COMMAND_IDENT BASE_CMD_OP * 0x4351

typedef struct _MyDummyProtocolData {
    UINTN unused;
} MyDummyProtocolData;

typedef unsigned long long my_ptr64;

typedef struct _MyMemoryCommand {
    int identifier;
    int action;
    my_ptr64 parameters[10];
} MyMemoryCommand;

// Function types (Windows specific)
typedef int (MyCallingType *MyProcessLookupById)(void* ProcessId, void* OutProcess);
typedef void* (MyCallingType *MyGetProcessBaseAddr)(void* Process);
typedef int (MyCallingType *MyVirtualMemCopy)(void* SourceProcess, void* SourceAddress, void* TargetProcess, void* TargetAddress, my_ptr64 BufferSize, char Mode, void* ReturnSize);

static const EFI_GUID MyProtocolGuid = { 0x4f123456, 0xfd5e, 0x2038, {0x8d, 0x9e, 0x20, 0xa7, 0xaf, 0x9c, 0x32, 0xf1} };
static const EFI_GUID MyVirtualGuid = { 0x13FA1234, 0xC831, 0x49C7, { 0x87, 0xEA, 0x8F, 0x43, 0xFC, 0xC2, 0x51, 0x96 }};
static const EFI_GUID MyExitGuid = { 0x27ABF055, 0xB1B8, 0x4C26, { 0x80, 0x48, 0x74, 0x8F, 0x37, 0xBA, 0xA2, 0xDF }};

static EFI_SET_VARIABLE MyOriginalSetVariable = NULL;
static EFI_EVENT MyNotifyEvent = NULL;
static EFI_EVENT MyExitEvent = NULL;
static BOOLEAN MyVirtual = FALSE;
static BOOLEAN MyRuntime = FALSE;

static MyProcessLookupById MyProcessLookup = (MyProcessLookupById)0;
static MyGetProcessBaseAddr MyGetBaseAddr = (MyGetProcessBaseAddr)0;
static MyVirtualMemCopy MyCopyVirtualMemory = (MyVirtualMemCopy)0;

EFI_STATUS ExecuteMyCommand(MyMemoryCommand* cmd) {
    if (cmd->identifier != COMMAND_IDENT) return EFI_ACCESS_DENIED;

    switch (cmd->action) {
        case BASE_CMD_OP * 0x987: {
            void* source_pid = (void*)cmd->parameters[0];
            void* source_addr = (void*)cmd->parameters[1];
            void* target_pid = (void*)cmd->parameters[2];
            void* target_addr = (void*)cmd->parameters[3];
            my_ptr64 size = cmd->parameters[4];
            my_ptr64* resultAddr = (my_ptr64*)cmd->parameters[5];

            if (source_pid == (void*)4ULL) {
                CopyMem(target_addr, source_addr, size);
            } else {
                void* srcProc = 0, * tgtProc = 0;
                my_ptr64 out_size = 0;

                if (MyProcessLookup(source_pid, &srcProc) >= 0 && 
                    MyProcessLookup(target_pid, &tgtProc) >= 0) {
                    *resultAddr = MyCopyVirtualMemory(srcProc, source_addr, tgtProc, target_addr, size, 1, &out_size);
                } else {
                    *resultAddr = EFI_NOT_FOUND;  // Process not found
                }
            }
            return EFI_SUCCESS;
        }
        case BASE_CMD_OP * 0x612:
            MyProcessLookup = (MyProcessLookupById)cmd->parameters[0];
            MyGetBaseAddr = (MyGetProcessBaseAddr)cmd->parameters[1];
            MyCopyVirtualMemory = (MyVirtualMemCopy)cmd->parameters[2];
            *(my_ptr64*)cmd->parameters[3] = 1;
            return EFI_SUCCESS;

        case BASE_CMD_OP * 0x289: {
            void* pid = (void*)cmd->parameters[0];
            my_ptr64* resultAddr = (my_ptr64*)cmd->parameters[1];
            void* processPtr = 0;

            if (MyProcessLookup(pid, &processPtr) < 0 || processPtr == 0) {
                *resultAddr = 0; // Process not found
            } else {
                *resultAddr = (my_ptr64)MyGetBaseAddr(processPtr);
            }
            return EFI_SUCCESS;
        }
        default:
            return EFI_UNSUPPORTED;
    }
}

EFI_STATUS EFIAPI MyHookedSetVariable(IN CHAR16 *VarName, IN EFI_GUID *VendorGuid, IN UINT32 Attributes, IN UINTN DataSize, IN VOID *Data) {
    if (MyVirtual && MyRuntime) {       
        if (VarName != NULL && VarName[0] != CHAR_NULL && VendorGuid != NULL) {                     
            if (StrnCmp(VarName, MY_VARIABLE_NAME, (sizeof(MY_VARIABLE_NAME) / sizeof(CHAR16)) - 1) == 0) {              
                if (DataSize == 0 && Data == NULL) {
                    return EFI_SUCCESS;
                }
                if (DataSize == sizeof(MyMemoryCommand)) {
                    return ExecuteMyCommand((MyMemoryCommand*)Data);
                }
            }
        }
    }
    return MyOriginalSetVariable(VarName, VendorGuid, Attributes, DataSize, Data);
}

VOID EFIAPI MySetVirtualAddressMapEvent(IN EFI_EVENT Event, IN VOID* Context) {  
    RT->ConvertPointer(0, (VOID**)&MyOriginalSetVariable);
    RT->ConvertPointer(0, (VOID**)&oGetTime);
    RT->ConvertPointer(0, (VOID**)&oSetTime);
    RT->ConvertPointer(0, (VOID**)&oGetWakeupTime);
    RT->ConvertPointer(0, (VOID**)&oSetWakeupTime);
    RT->ConvertPointer(0, (VOID**)&oSetVirtualAddressMap);
    RT->ConvertPointer(0, (VOID**)&oConvertPointer);
    RT->ConvertPointer(0, (VOID**)&oGetVariable);
    RT->ConvertPointer(0, (VOID**)&oGetNextVariableName);
    RT->ConvertPointer(0, (VOID**)&oGetNextHighMonotonicCount);
    RT->ConvertPointer(0, (VOID**)&oResetSystem);
    RT->ConvertPointer(0, (VOID**)&oUpdateCapsule);
    RT->ConvertPointer(0, (VOID**)&oQueryCapsuleCapabilities);
    RT->ConvertPointer(0, (VOID**)&oQueryVariableInfo);
    
    RtLibEnableVirtualMappings();
    MyNotifyEvent = NULL;
    MyVirtual = TRUE;
}

VOID EFIAPI MyExitBootServicesEvent(IN EFI_EVENT Event, IN VOID* Context) {
    BS->CloseEvent(MyExitEvent);
    MyExitEvent = NULL;
    BS = NULL;
    MyRuntime = TRUE;

    ST->ConOut->SetAttribute(ST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE);
    ST->ConOut->ClearScreen(ST->ConOut);
    Print(L"My driver is operational! Booting Windows...\n");
}

VOID* MySetServicePointer(IN OUT EFI_TABLE_HEADER *ServiceTableHeader, IN OUT VOID **ServiceTableFunction, IN VOID *NewFunction) {
    if (ServiceTableFunction == NULL || NewFunction == NULL) return NULL;
    ASSERT(BS != NULL && BS->CalculateCrc32 != NULL);

    CONST EFI_TPL Tpl = BS->RaiseTPL(TPL_HIGH_LEVEL);
    VOID* OriginalFunction = *ServiceTableFunction;
    *ServiceTableFunction = NewFunction;
    ServiceTableHeader->CRC32 = 0;
    BS->CalculateCrc32((UINT8*)ServiceTableHeader, ServiceTableHeader->HeaderSize, &ServiceTableHeader->CRC32);
    BS->RestoreTPL(Tpl);
    return OriginalFunction;
}

static EFI_STATUS EFI_FUNCTION MyDriverUnload(IN EFI_HANDLE ImageHandle) {
    return EFI_ACCESS_DENIED;
}

EFI_STATUS MyDriverEntry(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    EFI_LOADED_IMAGE *LoadedImage = NULL;
    EFI_STATUS status = BS->OpenProtocol(ImageHandle, &LoadedImageProtocol, (void**)&LoadedImage, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    
    if (EFI_ERROR(status)) {
        Print(L"Failed to open protocol: %d\n", status);
        return status;
    }

    MyDummyProtocolData dummy = { 0 };
    status = LibInstallProtocolInterfaces(&ImageHandle, &MyProtocolGuid, &dummy, NULL);
    if (EFI_ERROR(status)) {
        Print(L"Failed to register interface: %d\n", status);
        return status;
    }

    LoadedImage->Unload = (EFI_IMAGE_UNLOAD)MyDriverUnload;

    status = BS->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, MySetVirtualAddressMapEvent, NULL, &MyVirtualGuid, &MyNotifyEvent);
    if (EFI_ERROR(status)) {
        Print(L"Failed to create event (MySetVirtualAddressMapEvent): %d\n", status);
        return status;
    }

    status = BS->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, MyExitBootServicesEvent, NULL, &MyExitGuid, &MyExitEvent);
    if (EFI_ERROR(status)) {
        Print(L"Failed to create event (MyExitBootServicesEvent): %d\n", status);
        return status;
    }

    MyOriginalSetVariable = (EFI_SET_VARIABLE)MySetServicePointer(&RT->Hdr, (VOID**)&RT->SetVariable, (VOID**)&MyHookedSetVariable);
    // Hooking other services...
    oGetTime = (EFI_GET_TIME)MySetServicePointer(&RT->Hdr, (VOID**)&RT->GetTime, (VOID**)&HookedGetTime);
    oSetTime = (EFI_SET_TIME)MySetServicePointer(&RT->Hdr, (VOID**)&RT->SetTime, (VOID**)&HookedSetTime);
    oGetWakeupTime = (EFI_GET_WAKEUP_TIME)MySetServicePointer(&RT->Hdr, (VOID**)&RT->GetWakeupTime, (VOID**)&HookedGetWakeupTime);
    oSetWakeupTime = (EFI_SET_WAKEUP_TIME)MySetServicePointer(&RT->Hdr, (VOID**)&RT->SetWakeupTime, (VOID**)&HookedSetWakeupTime);
    oSetVirtualAddressMap = (EFI_SET_VIRTUAL_ADDRESS_MAP)MySetServicePointer(&RT->Hdr, (VOID**)&RT->SetVirtualAddressMap, (VOID**)&HookedSetVirtualAddressMap);
    oConvertPointer = (EFI_CONVERT_POINTER)MySetServicePointer(&RT->Hdr, (VOID**)&RT->ConvertPointer, (VOID**)&HookedConvertPointer);
    oGetVariable = (EFI_GET_VARIABLE)MySetServicePointer(&RT->Hdr, (VOID**)&RT->GetVariable, (VOID**)&HookedGetVariable);
    oGetNextVariableName = (EFI_GET_NEXT_VARIABLE_NAME)MySetServicePointer(&RT->Hdr, (VOID**)&RT->GetNextVariableName, (VOID**)&HookedGetNextVariableName);
    oGetNextHighMonotonicCount = (EFI_GET_NEXT_HIGH_MONO_COUNT)MySetServicePointer(&RT->Hdr, (VOID**)&RT->GetNextHighMonotonicCount, (VOID**)&HookedGetNextHighMonotonicCount);
    oResetSystem = (EFI_RESET_SYSTEM)MySetServicePointer(&RT->Hdr, (VOID**)&RT->ResetSystem, (VOID**)&HookedResetSystem);
    oUpdateCapsule = (EFI_UPDATE_CAPSULE)MySetServicePointer(&RT->Hdr, (VOID**)&RT->UpdateCapsule, (VOID**)&HookedUpdateCapsule);
    oQueryCapsuleCapabilities = (EFI_QUERY_CAPSULE_CAPABILITIES)MySetServicePointer(&RT->Hdr, (VOID**)&RT->QueryCapsuleCapabilities, (VOID**)&HookedQueryCapsuleCapabilities);
    oQueryVariableInfo = (EFI_QUERY_VARIABLE_INFO)MySetServicePointer(&RT->Hdr, (VOID**)&RT->QueryVariableInfo, (VOID**)&HookedQueryVariableInfo);

    Print(L"Driver initialized successfully.\n");
    return EFI_SUCCESS;
}
