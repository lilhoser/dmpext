
#pragma once

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>

#define EXT_CLASS DmpExt
#include <engextcpp.hpp>

#define GUID_FMT "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x"
#define GUID_ARGS(guid)                                                          \
        (guid)->Data1, (guid)->Data2, (guid)->Data3,                             \
        (guid)->Data4[0], (guid)->Data4[1], (guid)->Data4[2], (guid)->Data4[3],  \
        (guid)->Data4[4], (guid)->Data4[5], (guid)->Data4[6], (guid)->Data4[7]

//
// Documented in DDK ntdddump.h
//
typedef struct _FILTER_INITIALIZATION_DATA {
  ULONG        MajorVersion;
  ULONG        MinorVersion;
  PVOID  DumpStart;
  PVOID  DumpWrite;
  PVOID DumpFinish;
  PVOID DumpUnload;
  PVOID        DumpData;
  ULONG        MaxPagesPerWrite;
  ULONG        Flags;
  PVOID   DumpRead;
} FILTER_INITIALIZATION_DATA, *PFILTER_INITIALIZATION_DATA;

//
// Primary extension class
//
class DmpExt : public ExtExtension,
               public IDebugOutputCallbacks
{

public:
    DmpExt();

    ~DmpExt();

    EXT_COMMAND_METHOD(dmpext);

    //
    // ExtExtension interface
    //
    virtual
    HRESULT
    Initialize (
        VOID
        );

    virtual
    void
    Uninitialize (
        VOID
        );

    virtual
    void
    OnSessionActive (
        __in ULONG64 Argument
        );

    virtual
    void
    OnSessionInactive (
        __in ULONG64 Argument
        );
    
    //
    // IDebugOutputCallbacks interface
    //
    STDMETHODIMP
    QueryInterface (
        __in REFIID InterfaceId,
        __out PVOID* Interface
        );

    STDMETHODIMP_(ULONG)
    AddRef (
        VOID
        );

    STDMETHODIMP_(ULONG)
    Release (
        VOID
        );

    STDMETHODIMP
    Output (
        __in ULONG Mask, 
        __in PCSTR Text
        );

private:

    VOID
    SaveDumpDrivers (
        VOID
        );

    VOID
    DisplayCrashdmpInformation (
        VOID
        );
    
    VOID
    DisplayDumpFilters (
        VOID
        );

    BOOLEAN m_Initialized;
    IDebugClient5* m_Client;
    IDebugControl4* m_Control;
    std::string m_OutputBuffer;

    //
    // Saved debugger output    
    //
    std::string m_DumpDrivers;
    std::string m_DumpPortDriverName;
    std::string m_DumpMiniportDriverName;

    ULONG m_OsBuildNumberMajor;
    ULONG m_OsBuildNumberMinor;
};