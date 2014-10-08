
#include "DmpExt.hpp"

//
// EXT_DECLARE_GLOBALS must be used to instantiate
// the framework's assumed globals.
//
EXT_DECLARE_GLOBALS();

static ULONG g_OutCtl = DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_OVERRIDE_MASK | 
                      DEBUG_OUTCTL_NOT_LOGGED;

/*!
    @brief  Constructor
 */
DmpExt::DmpExt()
{

}

/*!
    @brief  Destructor
 */
DmpExt::~DmpExt()
{

}

/*!
 
    @brief  Initializes the extension
 
    @return HRESULT value
 
 */
HRESULT
DmpExt::Initialize (
    VOID
    )
{   
    m_Control = NULL;
    m_Client = NULL;

    //
    // We are not fully initialized until the debugger
    // calls our OnSessionActive callback.
    //
    m_Initialized = FALSE;

    return S_OK;
}

/*!
 
    @brief  Uninitializes the extension
 
    @return HRESULT value
 
 */
void
DmpExt::Uninitialize (
    VOID
    )
{
    if (m_Control != NULL)
    {
        m_Control->Release();
    }

    if (m_Client != NULL)
    {
        m_Client->SetOutputCallbacks(NULL);
        m_Client->Release();
    }

    m_Control = NULL;
    m_Client = NULL;
    m_Initialized = FALSE;

    return;
}

/*!
 
    @brief  Called when a debug session is available for our 
            extension to use.

    @details This function is implemented so that we can cache
             session information such as debugger control instance.

    @param[in] Argument - Reserved; always 0 (not used).
 
    @return nothing
 
*/
void
DmpExt::OnSessionActive (
    __in ULONG64 Argument
    )
{
    HRESULT result;
    ULONG dontcare;

    DBG_UNREFERENCED_PARAMETER(Argument);
    
    //
    // We need these instances so that functions outside of
    // our extension methods (eg, !bus) can access the necessary
    // functions to print to the debugger and other things.
    //
    result = DebugCreate(__uuidof(IDebugClient5), (void **)&m_Client);

    if (result != S_OK)
    {
        return;
    }

    result = m_Client->QueryInterface(__uuidof(IDebugControl4), (void **)&m_Control);

    if (result != S_OK)
    {
        return;
    }

    if ((result = m_Client->SetOutputCallbacks(
            (PDEBUG_OUTPUT_CALLBACKS)this)) != S_OK)
    {
        return;
    }

    //
    // Save some basic information about crash dump stack
    //
    SaveDumpDrivers();
    
    if (m_Control->GetSystemVersionValues(&dontcare, 
                                          &m_OsBuildNumberMajor,
                                          &m_OsBuildNumberMinor,
                                          &dontcare,
                                          &dontcare) != S_OK)
    {
        Out("Failed to get system information.\n");
        return;
    }

    m_Initialized = TRUE;

    return;
}

/*!
 
    @brief  Called when a debug session is no longer available for our 
            extension to use.

    @details We release any session-specific objects we cached.

    @param[in] Argument - Reserved;always 0 (not used).
 
    @return nothing
 
*/
void
DmpExt::OnSessionInactive (
    __in ULONG64 Argument
    )
{
    DBG_UNREFERENCED_PARAMETER(Argument);

    Uninitialize();

    return;
}


/*!
 
    @brief  Implements the extension !DumpExt
 
    @return HRESULT value
 
 */
EXT_COMMAND (
    dmpext,
    "The !dmpext extension displays information about the crash dump infrastructure.",
    "{stack;b;stack;Display dump stack drivers}"
    "{crashdmp;b;crashdmp;Display crashdmp.sys information}"
    "{filters;b;filters;Display dump filter information}"
    )
{   
    Out("\n");

    if (HasArg("stack"))
    {
        if (m_OsBuildNumberMajor != 6)
        {
            Out("This version of windows doesn't have crashdmp.sys!\n");
            return;
        }

        //
        // This information was retrieved during class init
        //
        Out("Crash dump stack drivers:\n");
        Out("%s", m_DumpDrivers.c_str());
    }
    else if (HasArg("crashdmp"))
    {
        Out("Crashdmp.sys information:\n");
        DisplayCrashdmpInformation();
    }
    else if (HasArg("filters"))
    {
        //
        // Until I have time to get this in IDA for each OS...
        //
        if (m_OsBuildNumberMajor != 6 || m_OsBuildNumberMinor < 2 ||
            IsCurMachine64())
        {
            Out("Temporarily, this feature is only available for x86 Windows 8!\n");
            return;
        }

        Out("Installed dump filters:\n");
        DisplayDumpFilters();
    }
    else
    {
        Out("DmpExt 1.0\n");
        Out("-----------\n");
        Out("Target OS build:  Win NT %i.%i\n", m_OsBuildNumberMajor, m_OsBuildNumberMinor);
        Out("Available commands:\n");
        Out("\t-stack:     Display the crash dump driver stack\n");
        Out("\t-crashdmp:  Display details about crashdmp.sys\n");
        Out("\t-filters:  Display details about crash dump filters\n");
    }

    return;
}

/*!
 
    @brief  Saves dump drivers present in the class buffer after a call to 'lm'
 
    @return none
 
 */
VOID 
DmpExt::SaveDumpDrivers (
    VOID
    )
{
    std::string item;
    int i=0;

    m_OutputBuffer.erase();
    m_Control->Execute(g_OutCtl, "lmo1m m dump_*", DEBUG_EXECUTE_DEFAULT);
    m_DumpDrivers = m_OutputBuffer;
    std::stringstream ss(m_OutputBuffer);

    while (std::getline(ss, item, '\n')) 
    {
        //
        // Dump drivers are guaranteed to be loaded in this order.
        //
        if (i == 0)
        {
            m_DumpPortDriverName = item;
        }
        else if (i == 1)
        {
            m_DumpMiniportDriverName = item;
        }

        i++;
    }    
}

/*!
 
    @brief  Displays information about crashdmp.sys driver
 
    @return none
 
 */
VOID 
DmpExt::DisplayCrashdmpInformation (
    VOID
    )
{
    ULONG64 offset;
    ULONG result;
    ExtRemoteData data;
    GUID guid;
    ULONG major, minor;
    ULONG_PTR pointer;
    int i;
    CHAR name[128];

    if ( (result = m_Symbols->GetOffsetByName("nt!CrashdmpImageEntry", &offset)) != S_OK)
    {
        Out("Failed to resolve symbol:  %08x\n", result);
    }
    else
    {
        Out("\tImage entry:  0x%p\n", offset);
    }

    if ( (result = m_Symbols->GetOffsetByName("nt!CrashdmpImageBase", &offset)) != S_OK)
    {
        Out("Failed to resolve symbol:  %08x\n", result);
    }
    else
    {
        if (offset != 0)
        {
            data.Set(offset, sizeof(ULONG_PTR));
            Out("\tImage base:  0x%p\n", data.GetUlongPtr());
        }
    }

    if ( (result = m_Symbols->GetOffsetByName("nt!CrashdmpInitialized", &offset)) != S_OK)
    {
        Out("Failed to resolve symbol:  %08x\n", result);
    }
    else
    {
        if (offset != 0)
        {
            data.Set(offset, sizeof(UCHAR));
            Out("\tInitialized:  %i\n", data.GetUchar());
        }
    }

    if ( (result = m_Symbols->GetOffsetByName("nt!CrashdmpGuid", &offset)) != S_OK)
    {
        Out("Failed to resolve symbol:  %08x\n", result);
    }
    else
    {
        if (offset != 0)
        {
            data.Set(offset, sizeof(GUID));
            data.ReadBuffer(&guid, sizeof(guid), true);
            Out("\tCrashdmp GUID:  " GUID_FMT "\n", GUID_ARGS(&guid));
        }
    }
    
    if ( (result = m_Symbols->GetOffsetByName("nt!CrashdmpDumpBlock", &offset)) != S_OK)
    {
        Out("Failed to resolve symbol:  %08x\n", result);
    }
    else
    {
        if (offset != 0)
        {
            data.Set(offset, sizeof(ULONG_PTR));
            Out("\tDump block:  0x%p\n", data.GetUlongPtr());
        }
    }

    if ( (result = m_Symbols->GetOffsetByName("crashdmp!Context", &offset)) != S_OK)
    {
        Out("Failed to resolve symbol:  %08x\n", result);
    }
    else
    {
        Out("\tContext:  0x%p\n", offset);
    }

    if ( (result = m_Symbols->GetOffsetByName("crashdmp!GUID_DEVICEDUMP_CRASHSTORAGE_DEVICE", 
            &offset)) != S_OK)
    {
        Out("Failed to resolve symbol:  %08x\n", result);
    }
    else
    {
        if (offset != 0)
        {
            data.Set(offset, sizeof(GUID));
            data.ReadBuffer(&guid, sizeof(guid), true);
            Out("\tCrashdmp device GUID:  " GUID_FMT "\n", GUID_ARGS(&guid));
        }
    }

    if ( (result = m_Symbols->GetOffsetByName("nt!CrashdmpCallTable", 
            &offset)) != S_OK)
    {
        Out("Failed to resolve symbol:  %08x\n", result);
    }
    else
    {
        Out("\tCrashdmp call table (0x%p):\n", offset);

        data.Set(offset, sizeof(ULONG));
        major = data.GetUlong();
        offset += sizeof(ULONG);
        data.Set(offset, sizeof(ULONG));
        minor = data.GetUlong();
        offset += sizeof(ULONG);
        Out("\t\tVersion %i.%i\n", major, minor);

        //
        // Dump the call table
        //
        switch (m_OsBuildNumberMinor)
        {
        case 1: // Windows 7
            {
                for (i = 0; i < 8; i++)
                {
                    data.Set(offset, sizeof(ULONG_PTR));
                    pointer = data.GetPtr();
                    if (m_Symbols->GetNameByOffset(data.GetPtr(), name, 128, NULL, NULL) != S_OK)
                    {
                        Out("\t\t%i => 0x%p\n", i, pointer);
                    }
                    else
                    {
                        Out("\t\t%i => %s\n", i, name);
                    }
                    offset += sizeof(ULONG_PTR);
                }

                break;
            }
        case 2: // Windows 8
        case 3: // Windows 8.1
            {
                for (i = 0; i < 12; i++)
                {
                    data.Set(offset, sizeof(ULONG_PTR));
                    pointer = data.GetPtr();
                    if (m_Symbols->GetNameByOffset(data.GetPtr(), name, 128, NULL, NULL) != S_OK)
                    {
                        Out("\t\t%i => 0x%p\n", i, pointer);
                    }
                    else
                    {
                        Out("\t\t%i => %s\n", i, name);
                    }
                    offset += sizeof(ULONG_PTR);
                }

                break;
            }
        default:
            {
                Out("OS beyond Windows 8.1 not supported!\n");
                break;
            }
        }
    }
}


/*!
 
    @brief  Displays information about crash dump filter drivers
 
    @return none
 
 */
VOID 
DmpExt::DisplayDumpFilters (
    VOID
    )
{
    ULONG64 commonContext, crashDmpContext;
    ULONG result;
    ULONG listHeadOffset, listEntryOffset, commonContextOffset;
    ULONG64 listHead;
    ExtRemoteData data;
    FILTER_INITIALIZATION_DATA filterInitData;
    CHAR name[128];

    //
    // Get common context ptr by symbol
    //
    if ( (result = m_Symbols->GetOffsetByName("crashdmp!Context", &crashDmpContext)) != S_OK)
    {
        Out("\tFailed to resolve symbol:  %08x\n", result);
        return;
    }

    //
    // We need to get:
    //      a) DumpContext pointer relative to CommonContext 
    //      b) filter list head pointer, relative to DumpContext
    //      c) ListEntry offset from start of a structure in the list
    //
    switch (m_OsBuildNumberMinor)
    {
    case 2:  // windows 8
    case 3:  // windows 8.1
        {
            if (IsCurMachine64())
            {
                commonContextOffset = 0x2c;
                listHeadOffset = 0x250;
                listEntryOffset = 0x94;
                break;
            }
            commonContextOffset = 0x20;
            listHeadOffset = 0x19c;
            listEntryOffset = 0x78;
            break;
        }
    default:
        {
            Out("\tUnsupported OS.\n");
            return;
        }
    }

    //
    // read pointer to the common context structure
    //
    commonContext = crashDmpContext + commonContextOffset;
    data.Set(commonContext, sizeof(ULONG_PTR));
    commonContext = data.GetPtr();
    
    listHead = commonContext + listHeadOffset;

    Out("\tListhead at 0x%p\n", listHead);
    
    ExtRemoteList list(listHead, listEntryOffset, true);
    list.StartHead();

    while (list.HasNode())
    {
        Out("\tFilter at 0x%p:\n", list.GetNodeOffset());
        data.Set(list.GetNodeOffset(), sizeof(FILTER_INITIALIZATION_DATA));
        data.ReadBuffer(&filterInitData, sizeof(filterInitData), true);
        Out("\t\tVersion: %i.%i\n", filterInitData.MajorVersion, filterInitData.MinorVersion);

        if (m_Symbols->GetNameByOffset((ULONG64)filterInitData.DumpStart, name, 128, NULL, NULL) != S_OK)
        {
            Out("\t\tDumpStart: %p\n", filterInitData.DumpStart);
        }
        else
        {
            Out("\t\tDumpStart: %s (%p)\n", name, filterInitData.DumpStart);
        }

        if (m_Symbols->GetNameByOffset((ULONG64)filterInitData.DumpFinish, name, 128, NULL, NULL) != S_OK)
        {
            Out("\t\tDumpFinish: %p\n", filterInitData.DumpFinish);
        }
        else
        {
            Out("\t\tDumpFinish: %s (%p)\n", name, filterInitData.DumpFinish);
        }

        if (m_Symbols->GetNameByOffset((ULONG64)filterInitData.DumpUnload, name, 128, NULL, NULL) != S_OK)
        {
            Out("\t\tDumpUnload: %p\n", filterInitData.DumpUnload);
        }
        else
        {
            Out("\t\tDumpUnload: %s (%p)\n", name, filterInitData.DumpUnload);
        }

        if (m_Symbols->GetNameByOffset((ULONG64)filterInitData.DumpWrite, name, 128, NULL, NULL) != S_OK)
        {
            Out("\t\tDumpWrite: %p\n", filterInitData.DumpWrite);
        }
        else
        {
            Out("\t\tDumpWrite: %s (%p)\n", name, filterInitData.DumpWrite);
        }

        if (m_Symbols->GetNameByOffset((ULONG64)filterInitData.DumpRead, name, 128, NULL, NULL) != S_OK)
        {
            Out("\t\tDumpRead: %p\n", filterInitData.DumpRead);
        }
        else
        {
            Out("\t\tDumpRead: %s (%p)\n", name, filterInitData.DumpRead);
        }
        
        Out("\t\tDump data: %p\n", filterInitData.DumpData);

        list.Next();
    }
}

//
// IDebugOutputCallbacks methods
//

STDMETHODIMP
DmpExt::QueryInterface (
    __in REFIID InterfaceId,
    __out PVOID* Interface
    )
{
    *Interface = NULL;

    if (IsEqualIID(InterfaceId, __uuidof(IUnknown)) ||
        IsEqualIID(InterfaceId, __uuidof(IDebugOutputCallbacks)))
    {
        *Interface = (IDebugOutputCallbacks *)this;
        AddRef();
        return S_OK;
    }
    else
    {
        return E_NOINTERFACE;
    }
}

STDMETHODIMP_(ULONG)
DmpExt::AddRef (
    VOID
    )
{
    return 1;
}

STDMETHODIMP_(ULONG)
DmpExt::Release (
    VOID
    )
{
    return 0;
}

STDMETHODIMP
DmpExt::Output (
    __in ULONG Mask, 
    __in PCSTR Text
    )
{
    UNREFERENCED_PARAMETER(Mask);

    m_OutputBuffer += Text;
    return S_OK;
}