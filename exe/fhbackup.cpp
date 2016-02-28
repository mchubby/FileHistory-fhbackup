// The MIT License (MIT)
//
// Copyright (c) 2016 mchubby https://github.com/mchubby
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "fhbackup.h"


static const FH_TARGET_PROPERTY_TYPE _properties[] = { FH_TARGET_NAME, FH_TARGET_URL, FH_TARGET_DRIVE_TYPE };
static const WCHAR * const _names[] = { L"FH_TARGET_NAME", L"FH_TARGET_URL", L"FH_TARGET_DRIVE_TYPE" };

HRESULT StartBackups()
{
	HRESULT hr = S_OK;
	CComPtr<IFhConfigMgr> configMgr;
	CComPtr<IFhTarget> targetPtr;
	FH_BACKUP_STATUS backupStatus;
	CComBSTR targetPath;
	FH_DEVICE_VALIDATION_RESULT validationResult;
	HRESULT hrPipe = S_OK;
	FH_SERVICE_PIPE_HANDLE pipe = NULL;
	DWORD ProtectionState;
	BSTR ProtectedUntilTimeOut;

	// The configuration manager is used to create and load configuration 
	// files, get/set the backup status, validate a target, etc
	hr = configMgr.CoCreateInstance(CLSID_FhConfigMgr);
	if (FAILED(hr))
	{
		wprintf(L"Error: CoCreateInstance(CLSID_FhConfigMgr) failed (hr=0x%X)\n", hr);
		goto Cleanup;
	}

	wprintf(L"Loading configuration\n");
	hr = configMgr->LoadConfiguration();
	if (FAILED(hr))
	{
		wprintf(L"Error: LoadConfiguration failed (hr=0x%X)\n", hr);
		goto Cleanup;
	}

	// Check the backup status
	// If File History is disabled by group policy, quit
	wprintf(L"Getting backup status\n");
	hr = configMgr->GetBackupStatus(&backupStatus);
	if (FAILED(hr))
	{
		wprintf(L"Error: GetBackupStatus failed (hr=0x%X)\n", hr);
		goto Cleanup;
	}
	if (backupStatus != FH_STATUS_ENABLED &&
		backupStatus != FH_STATUS_REHYDRATING)
	{
		wprintf(L"Error: File History is not enabled (status 0x%X not any of FH_STATUS_ENABLED,FH_STATUS_REHYDRATING)\n", backupStatus);
		hr = E_FAIL;
		goto Cleanup;
	}

	// Make sure the target is valid to be used for File History
	wprintf(L"Getting current target\n");
	hr = configMgr->GetDefaultTarget(&targetPtr);
	if (FAILED(hr))
	{
		wprintf(L"Error: GetDefaultTarget failed (hr=0x%X)\n", hr);
		goto Cleanup;
	}
	
	for (size_t i = 0; i < sizeof(_properties) / sizeof(_properties[0]); ++i)
	{
		BSTR propValueOut;
		if (SUCCEEDED(targetPtr->GetStringProperty(_properties[i], &propValueOut))) {
			CComBSTR propValue;
			propValue.Attach(propValueOut);
			wprintf(L" - %s : %s\n", _names[i], propValue);
			if (_properties[i] == FH_TARGET_URL)
			{
				targetPath = propValue;
			}
		}
	}
	if (targetPath.Length() == 0)
	{
		wprintf(L"Error: No target path found.\n");
		hr = E_FAIL;
		goto Cleanup;
	}
		

	// Make sure the target is valid to be used for File History
	wprintf(L"Validating target\n");
	hr = configMgr->ValidateTarget(targetPath, &validationResult);
	if (FAILED(hr))
	{
		wprintf(L"Error: ValidateTarget failed (hr=0x%X)\n", hr);
		goto Cleanup;
	}
	if (validationResult != FH_CURRENT_DEFAULT &&
		validationResult != FH_NAMESPACE_EXISTS &&
		validationResult != FH_VALID_TARGET)
	{
		wprintf(L"Error: %ws is not a valid target (validationResult 0x%X not any of FH_CURRENT_DEFAULT,FH_NAMESPACE_EXISTS,FH_VALID_TARGET )\n", targetPath.m_str, validationResult);
		hr = E_FAIL;
		goto Cleanup;
	}

	wprintf(L"Looking up existing backups\n");
	hr = configMgr->QueryProtectionStatus(&ProtectionState, &ProtectedUntilTimeOut);
	if (FAILED(hr))
	{
		wprintf(L"Warning: QueryProtectionStatus failed (hr=0x%X)\n", hr);
	}
	else
	{
		CComBSTR ProtectedUntilTime;
		ProtectedUntilTime.Attach(ProtectedUntilTimeOut);
		wprintf(L"ProtectionStatus: %s\n", ProtectedUntilTime);
	}

	wprintf(L"Contacting File History service (starting if necessary)\n");
	hrPipe = FhServiceOpenPipe(TRUE, &pipe);
	if (FAILED(hrPipe))
	{
		hr = hrPipe;
		wprintf(L"Error: FhServiceOpenPipe failed (hr=0x%X)\n", hr);
		goto Cleanup;
	}

	wprintf(L"Manually starting backup\n");
	hr = FhServiceStartBackup(pipe, TRUE);
	if (FAILED(hr))
	{
		wprintf(L"Error: FhServiceStartBackup failed (hr=0x%X)\n", hr);
		goto Cleanup;
	}

Cleanup:
	if (SUCCEEDED(hrPipe) && pipe != NULL)
	{
		FhServiceClosePipe(pipe);
	}
	return hr;
}


int __cdecl wmain(
    _In_ int Argc,
    _In_reads_(Argc) PWSTR Argv[]
    )
/*++

Routine Description:

    This is the main entry point of the console application.

Arguments:

    Argc - the number of command line arguments
    Argv - command line arguments

Return Value:

    exit code

--*/
{
	UNREFERENCED_PARAMETER(Argc);
	UNREFERENCED_PARAMETER(Argv);
	HRESULT hr = S_OK;
    BOOL comInitialized = FALSE;

    wprintf(L"\nFile History Sample Setup Tool\n");
    wprintf(L"Copyright (C) Microsoft Corporation. All rights reserved.\n\n");

	// COM is needed to use the Config Manager
    wprintf(L"Initializing COM...\n");
    hr = CoInitialize(NULL);
    if (FAILED(hr))
    {
        wprintf(L"Error: CoInitialize failed (0x%X)\n", hr);
        goto Cleanup;
    }
    comInitialized = TRUE;

	hr = StartBackups();
    if (FAILED(hr))
    {
        wprintf(L"File History configuration failed (0x%X)\n", hr);
        goto Cleanup;
    }

Cleanup:
    // If COM was initialized, make sure it is uninitialized
    if (comInitialized)
    {
        CoUninitialize();
        comInitialized = FALSE;
    }

    return 0;
}
