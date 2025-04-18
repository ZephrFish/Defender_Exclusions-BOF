#include <windows.h>
#include <wbemidl.h>
#include "headers/beacon.h"
#include "headers/win32.h"

// Updated namespace and query for modern Defender, works on both W10 and W11 with MDE and Standard Defender
#define WQL                     L"WQL"
#define WQLNAMESPACE            L"ROOT\\Microsoft\\Windows\\Defender"
#define DEFENDER_WQL            L"SELECT * FROM MSFT_MpPreference"
#define FOLDER_EXCLUSIONS       L"ExclusionPath"
#define PROCESS_EXCLUSIONS      L"ExclusionProcess"
#define EXTENSION_EXCLUSIONS    L"ExclusionExtension"
#define IP_EXCLUSIONS           L"ExclusionIpAddress"

static const wchar_t* options[] = { FOLDER_EXCLUSIONS, PROCESS_EXCLUSIONS, EXTENSION_EXCLUSIONS, IP_EXCLUSIONS };
static unsigned short int step  = 1;

extern "C" void dumpFormatAllocation(formatp* formatAllocationData)
{
    char*   outputString = NULL;
    int     sizeOfObject = 0;

    outputString = BeaconFormatToString(formatAllocationData, &sizeOfObject);
    BeaconOutput(CALLBACK_OUTPUT, outputString, sizeOfObject);
    BeaconFormatFree(formatAllocationData);

    return;
}

#ifndef DEBUGBUILD
extern "C" void go(char* argc, int len)
{
    formatp fpObject;
    formatp fpExclusionObject;
    datap   dpParser;

    BeaconFormatAlloc(&fpObject, 64 * 1024);
    BeaconFormatAlloc(&fpExclusionObject, 64 * 1024);
    BeaconFormatPrintf(&fpExclusionObject, "Excluded Items:\n");

    BeaconDataParse(&dpParser, argc, len);

    int iEnumerationOption = BeaconDataInt(&dpParser);

    HRESULT hres;

    // Initialize COM
    hres = OLE32$CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hres))
    {
        dumpFormatAllocation(&fpObject);
        BeaconPrintf(CALLBACK_ERROR, "[%02hu]CoInitializeEx has failed: %08lx", step, hres);
        return;
    }
    else
    {
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tCoInitializeEx\n", step);
        step++;
    }

    // Set security levels
    hres = OLE32$CoInitializeSecurity(
        NULL,
        -1,                          
        NULL,                        
        NULL,                        
        RPC_C_AUTHN_LEVEL_DEFAULT,   
        RPC_C_IMP_LEVEL_IMPERSONATE,  
        NULL,                        
        EOAC_NONE,                    
        NULL
    );

    if (FAILED(hres))
    {
        dumpFormatAllocation(&fpObject);
        dumpFormatAllocation(&fpExclusionObject);

        BeaconPrintf(CALLBACK_ERROR, "[%02hu] CoInitializeSecurity has failed: %08lx", step, hres);
        OLE32$CoUninitialize();
        
        return;
    }
    else
    {
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tCoInitializeSecurity\n", step);
        step++;
    }

    IWbemLocator* pLoc = NULL;

    // Create WMI locator
    hres = OLE32$CoCreateInstance(
        g_CLSID_WbemLocator,
        0,
        CLSCTX_INPROC_SERVER,
        g_IID_IWbemLocator, (LPVOID*)&pLoc);

    if (FAILED(hres))
    {
        dumpFormatAllocation(&fpObject);
        dumpFormatAllocation(&fpExclusionObject);

        BeaconPrintf(CALLBACK_ERROR, "[%02hu] CoCreateInstance has failed: %08lx", step, hres);
        OLE32$CoUninitialize();

        return;
    }
    else
    {
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tCoCreateInstance\n", step);
        step++;
    }

    IWbemServices* pSvc = NULL;
    BSTR bstrDefenderRootWMI = OLEAUT32$SysAllocString(WQLNAMESPACE);

    if (bstrDefenderRootWMI == NULL)
    {
        dumpFormatAllocation(&fpObject);
        dumpFormatAllocation(&fpExclusionObject);

        BeaconPrintf(CALLBACK_ERROR, "[%02hu] SysAllocString for \"%S\" has failed.", step, WQLNAMESPACE);
        OLE32$CoUninitialize();

        return;
    }
    else
    {
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tAllocated \"%S\"\n", step, (wchar_t*)WQLNAMESPACE);
        step++;
    }
    
    // Connect to WMI
    hres = pLoc->ConnectServer(
        bstrDefenderRootWMI, 
        NULL,                    
        NULL,                    
        0,                       
        0,                    
        0,                       
        0,
        &pSvc
    );

    if (FAILED(hres))
    {
        dumpFormatAllocation(&fpObject);
        dumpFormatAllocation(&fpExclusionObject);

        BeaconPrintf(CALLBACK_ERROR, "[%02hu] Server connection to \"%S\" failed: %08x", step, (wchar_t*)WQLNAMESPACE, hres);
        
        // Cleanup
        pLoc->Release();
        OLE32$CoUninitialize();

        // Ensure we free the binary "string"
        OLEAUT32$SysFreeString(bstrDefenderRootWMI);

        return;
    }
    else
    {
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tConnect method\n", step);
        step++;
    }

    // Set security levels on the proxy
    hres = OLE32$CoSetProxyBlanket(
        (IUnknown *)pSvc,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        NULL,
        RPC_C_AUTHN_LEVEL_CALL,      
        RPC_C_IMP_LEVEL_IMPERSONATE, 
        NULL,                        
        EOAC_NONE                   
    );

    if (FAILED(hres))
    {
        dumpFormatAllocation(&fpObject);
        dumpFormatAllocation(&fpExclusionObject);

        BeaconPrintf(CALLBACK_ERROR, "[%02hu] CoSetProxyBlanket failed: %08x\n", step, hres);

        // Cleanup
        pSvc->Release();
        pLoc->Release();
        OLE32$CoUninitialize();

        // Ensure we free the binary "string"
        OLEAUT32$SysFreeString(bstrDefenderRootWMI);
        
        return;
    }
    else
    {
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tCoSetProxyBlanket\n", step);
        step++;
    }

    IEnumWbemClassObject* pEnumerator = NULL;
    
    BSTR bstrWQL = OLEAUT32$SysAllocString(WQL);
    if (bstrWQL == NULL)
    {
        dumpFormatAllocation(&fpObject);
        dumpFormatAllocation(&fpExclusionObject);

        BeaconPrintf(CALLBACK_ERROR, "[%02hu] SysAllocString for \"%S\" has failed.", step, WQL);

        // Free allocated strings
        OLEAUT32$SysFreeString(bstrDefenderRootWMI);

        // Release used memory
        if (pSvc != NULL)
        {
            pSvc->Release();
        }
        
        if (pLoc != NULL)
        {
            pLoc->Release();
        }
        
        // Uninitialize OLE environment
        OLE32$CoUninitialize();
        return;
    }
    else
    {
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tAllocated \"%S\"\n", step, (wchar_t*)WQL);
        step++;
    }

    BSTR bstrQuery = OLEAUT32$SysAllocString(DEFENDER_WQL);
    if (bstrQuery == NULL)
    {
        dumpFormatAllocation(&fpObject);
        dumpFormatAllocation(&fpExclusionObject);

        BeaconPrintf(CALLBACK_ERROR, "[%02hu] SysAllocString for \"%S\" has failed.", step, DEFENDER_WQL);

        // Free allocated strings
        OLEAUT32$SysFreeString(bstrWQL);
        OLEAUT32$SysFreeString(bstrDefenderRootWMI);

        // Release used memory
        if (pSvc != NULL)
        {
            pSvc->Release();
        }
        
        if (pLoc != NULL)
        {
            pLoc->Release();
        }
        
        // Uninitialize OLE environment
        OLE32$CoUninitialize();
        return;
    }
    else
    {
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tAllocated \"%S\"\n", step, (wchar_t*)DEFENDER_WQL);
        step++;
    }

    // Execute WMI query
    hres = pSvc->ExecQuery(
        bstrWQL,
        bstrQuery,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL,
        &pEnumerator);
    
    if (FAILED(hres))
    {
        dumpFormatAllocation(&fpObject);
        dumpFormatAllocation(&fpExclusionObject);

        BeaconPrintf(CALLBACK_ERROR, "[%02hu]Query failed: %08x\n", step, hres);
        
        if (pEnumerator != NULL)
        {
            pEnumerator->Release();
        }

        if (pSvc != NULL)
        {
            pSvc->Release();
        }

        if (pLoc != NULL)
        {
            pLoc->Release();
        }

        // Destroy our COM
        OLE32$CoUninitialize();

        // Ensure we free our binary "strings"
        OLEAUT32$SysFreeString(bstrQuery);
        BeaconPrintf(CALLBACK_OUTPUT, "[%02hu] Freed SysAllocString: \"%S\"\n", step, DEFENDER_WQL);
        step++;

        OLEAUT32$SysFreeString(bstrWQL);
        BeaconPrintf(CALLBACK_OUTPUT, "[%02hu] Freed SysAllocString: \"%S\"\n", step, WQL);
        step++;

        OLEAUT32$SysFreeString(bstrDefenderRootWMI);
        BeaconPrintf(CALLBACK_OUTPUT, "[%02hu] Freed SysAllocString: \"%S\"\n", step, WQLNAMESPACE);
        step++;

        return;
    }
    else
    {
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tExecQuery\n", step);
        step++;
    }

    // Process query results
    IWbemClassObject* pclsObj = NULL;
    ULONG uReturn = 0;
    unsigned long ulIndex = 0;

    while (pEnumerator)
    {
        HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);

        if (0 == uReturn)
        {
            break;
        }

        VARIANT vtProp;
        OLEAUT32$VariantInit(&vtProp);
        
        // Make sure we're using a valid option index
        if (iEnumerationOption >= 1 && iEnumerationOption <= 4) {
            hr = pclsObj->Get(options[iEnumerationOption - 1], 0, &vtProp, 0, 0);
            BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tProperty retrieved\n", step);
            step++;

            if (!FAILED(hr))
            {
                if (!((vtProp.vt == VT_NULL) || (vtProp.vt == VT_EMPTY)))
                {
                    if ((vtProp.vt & VT_ARRAY))
                    {
                        long lower, upper;
                        BSTR Element;

                        SAFEARRAY* pSafeArray = vtProp.parray;

                        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tPointer (0x%p)\n", step, vtProp.parray);
                        step++;

                        hres = OLEAUT32$SafeArrayGetLBound(pSafeArray, 1, &lower);
                        if (FAILED(hres))
                        {
                            BeaconFormatPrintf(&fpObject, "[%02hu] WARNING:\tFailed to get lower bound\n", step);
                            step++;
                        }
                        else
                        {
                            BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\t%02d lower bound\n", step, lower);
                            step++;
                        }

                        hres = OLEAUT32$SafeArrayGetUBound(pSafeArray, 1, &upper);
                        if (FAILED(hres))
                        {
                            BeaconFormatPrintf(&fpObject, "[%02hu] WARNING:\tFailed to get upper bound\n", step);
                            step++;
                        }
                        else
                        {
                            BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\t%02d upper bound\n", step, upper);
                            step++;
                        }
                        
                        for (long i = lower; i <= upper; i++)
                        {
                            hres = OLEAUT32$SafeArrayGetElement(pSafeArray, &i, &Element);

                            if (FAILED(hres))
                            {
                                BeaconFormatPrintf(&fpObject, "[%02hu] WARNING:\tFailed to get array element %ld\n", step, i);
                                step++;
                                break;
                            }
                            else
                            {
                                ++ulIndex;
                                BeaconFormatPrintf(&fpExclusionObject, "[%02lu]\t\"%S\"\n", ulIndex, (wchar_t*)Element);
                                OLEAUT32$SysFreeString(Element);
                            }
                        }
                    }
                    else if (vtProp.vt == VT_BSTR) {
                        // Handle single string value
                        ++ulIndex;
                        BeaconFormatPrintf(&fpExclusionObject, "[%02lu]\t\"%S\"\n", ulIndex, vtProp.bstrVal);
                    }
                }
                else {
                    BeaconFormatPrintf(&fpExclusionObject, "No exclusions found for this category.\n");
                }
            }
            
            OLEAUT32$VariantClear(&vtProp);
        }
        else {
            BeaconFormatPrintf(&fpObject, "[%02hu] ERROR:\tInvalid enumeration option: %d\n", step, iEnumerationOption);
            step++;
        }
        
        if (pclsObj != NULL) {
            pclsObj->Release();
            pclsObj = NULL;
        }
    }
        
    // Cleanup
    if (pEnumerator != NULL)
    {
        pEnumerator->Release();
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tReleased enumerator\n", step);
        step++;
    }

    if (pSvc != NULL)
    {
        pSvc->Release();
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tReleased IWbemServices\n", step);
        step++;
    }

    if (pLoc != NULL)
    {
        pLoc->Release();
        BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tReleased IWbemLocator\n", step);
        step++;
    }

    OLE32$CoUninitialize();
    BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tUninitialized CoInitialize\n", step);
    step++;

    // Free allocated strings
    OLEAUT32$SysFreeString(bstrQuery);
    BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tFreed SysAllocString: \"%S\"\n", step, DEFENDER_WQL);
    step++;

    OLEAUT32$SysFreeString(bstrWQL);
    BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tFreed SysAllocString: \"%S\"\n", step, WQL);
    step++;

    OLEAUT32$SysFreeString(bstrDefenderRootWMI);
    BeaconFormatPrintf(&fpObject, "[%02hu] SUCCESS:\tFreed SysAllocString: \"%S\"\n", step, WQLNAMESPACE);
    step++;

    // Output results
    dumpFormatAllocation(&fpObject);
    dumpFormatAllocation(&fpExclusionObject);

    return;
}
#else
extern "C" void go(char* argc, int len)
{
    datap dpParser;
    BeaconDataParse(&dpParser, argc, len);
    int iEnumerationOption = BeaconDataInt(&dpParser);
    BeaconPrintf(CALLBACK_OUTPUT, "Received: %d\n", iEnumerationOption);
    return;
}
#endif
