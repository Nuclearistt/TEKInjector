#include <vector>
#include <Windows.h>
#include "detours/detours.h"

using namespace std;

//Windows NT API type definitions
typedef struct _UNICODE_STRING
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, * PUNICODE_STRING;
typedef const UNICODE_STRING* PCUNICODE_STRING;
typedef struct _LDR_DLL_LOADED_NOTIFICATION_DATA
{
    ULONG Flags;
    PCUNICODE_STRING FullDllName;
    PCUNICODE_STRING BaseDllName;
    PVOID DllBase;
    ULONG SizeOfImage;
} LDR_DLL_LOADED_NOTIFICATION_DATA, * PLDR_DLL_LOADED_NOTIFICATION_DATA;
//Steam API type definitions
typedef unsigned __int32 uint32;
typedef unsigned __int64 uint64, PublishedFileId_t;
typedef void* HServerListRequest;
enum class EResult
{
    k_EResultOK = 1,
    k_EResultFail = 2
};
struct ISteamApps {};
struct ISteamMatchmakingServers {};
struct ISteamUGC {};
struct ISteamUtils {};
struct ISteamUser
{
    virtual void Mock1() = 0;
    virtual void Mock2() = 0;
    virtual uint64* GetSteamID(uint64*) = 0;
};
struct MatchMakingKeyValuePair_t
{
    char m_szKey[256];
    char m_szValue[256];
};
#pragma pack (push, 8)
struct RemoteStorageSubscribePublishedFileResult_t
{
    enum { k_iCallback = 1313 };
    EResult m_eResult;
    PublishedFileId_t m_nPublishedFileId;
};
#pragma pack (pop)
//TEK Injector types
struct DownloadProgress
{
    uint64 Current;
    uint64 Total;
    bool Available;
};

//Windows NT API function definitions
typedef VOID(CALLBACK* PLDR_DLL_NOTIFICATION_FUNCTION)(ULONG, PLDR_DLL_LOADED_NOTIFICATION_DATA, PVOID);
typedef NTSTATUS(NTAPI* LdrRegisterDllNotification_t)(ULONG, PLDR_DLL_NOTIFICATION_FUNCTION, PVOID, PVOID*);
typedef NTSTATUS(NTAPI* LdrUnregisterDllNotification_t)(PVOID);
//Steam API function definitions
typedef bool (*SteamAPI_Init_t)();
typedef ISteamApps* (*SteamApps_t)();
typedef ISteamMatchmakingServers* (*SteamMatchmakingServers_t)();
typedef ISteamUGC* (*SteamUGC_t)();
typedef ISteamUser* (*SteamUser_t)();
typedef ISteamUtils* (*SteamUtils_t)();
typedef HServerListRequest(*RequestServerList_t)(ISteamMatchmakingServers*, uint32, MatchMakingKeyValuePair_t**, uint32, void*);
typedef bool (*IsAPICallCompleted_t)(ISteamUtils*, uint64, bool*);
typedef bool (*GetAPICallResult_t)(ISteamUtils*, uint64, void*, int, int, bool*);

//Global variables
static LdrUnregisterDllNotification_t LdrUnregisterDllNotification; //LdrUnregisterDllNotification function pointer
static PVOID Cookie; //DLL notification cookie for unregistering
//Steam API original function pointers
static SteamAPI_Init_t SteamAPI_Init_o;
static SteamApps_t SteamApps_o;
static SteamMatchmakingServers_t SteamMatchmakingServers_o;
static SteamUGC_t SteamUGC_o;
static SteamUser_t SteamUser_o;
static SteamUtils_t SteamUtils_o;
static RequestServerList_t RequestInternetServerList_o;
static RequestServerList_t RequestFriendsServerList_o;
static RequestServerList_t RequestFavoritesServerList_o;
static RequestServerList_t RequestHistoryServerList_o;
static IsAPICallCompleted_t IsAPICallCompleted_o;
static GetAPICallResult_t GetAPICallResult_o;
static char pchBaseModsPath[MAX_PATH]; //Path to the folder where mods are loaded from
static DWORD cchBaseModsPath; //Index of * character in pchBaseModsPath for mod IDs to be inserted at
static uint64 SteamID; //Current user's Steam ID for use in GetAppOwner function
static PublishedFileId_t DownloadingMod; //ID of the mod currently being downloaded by the launcher
static DownloadProgress Progress; //Mod download progress buffer
static HANDLE Pipe = INVALID_HANDLE_VALUE; //Client named pipe handle, this pipe is connected to TEK Launcher to get download progress from it
static vector<PublishedFileId_t> pvecModIDs; //Installed mod IDs

//Local functions
bool GetAPICallResult(ISteamUtils* pThis, uint64 hSteamAPICall, void* pCallback, int cubCallback, int iCallbackExpected, bool* pbFailed)
{
    if (hSteamAPICall == DownloadingMod)
    {
        *pbFailed = false;
        RemoteStorageSubscribePublishedFileResult_t* callback = (RemoteStorageSubscribePublishedFileResult_t*)pCallback;
        callback->m_nPublishedFileId = DownloadingMod;
        callback->m_eResult = Pipe == INVALID_HANDLE_VALUE ? EResult::k_EResultFail : EResult::k_EResultOK;
        return true;
    }
    return GetAPICallResult_o(pThis, hSteamAPICall, pCallback, cubCallback, iCallbackExpected, pbFailed);
}
bool GetItemInstallInfo(ISteamUGC* pThis, PublishedFileId_t nPublishedFileID, uint64* punSizeOnDisk, char* pchFolder, uint32 cchFolderSize, bool* pbLegacyItem)
{
    *punSizeOnDisk = 0; //This is not used by the game so no need to bother about computing directory size
    *pbLegacyItem = false;
    strcpy(pchFolder, pchBaseModsPath);
    _ui64toa_s(nPublishedFileID, pchFolder + cchBaseModsPath, MAX_PATH - (size_t)cchBaseModsPath, 10);
    const DWORD dwAttributes = GetFileAttributesA(pchFolder);
    if (dwAttributes == INVALID_FILE_ATTRIBUTES || !(dwAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        *pchFolder = '\0';
        return false;
    }
    return true;
}
bool GetItemUpdateInfo(ISteamUGC* pThis, PublishedFileId_t nPublishedFileID, bool* pbNeedsUpdate, bool* pbIsDownloading, uint64* punBytesDownloaded, uint64* punBytesTotal)
{
    if (Progress.Available)
    {
        *pbNeedsUpdate = true;
        *pbIsDownloading = true;
        *punBytesDownloaded = Progress.Current;
        *punBytesTotal = Progress.Total;
        return true;
    }
    else
    {
        *pbNeedsUpdate = false;
        *pbIsDownloading = false;
        *punBytesDownloaded = 0;
        *punBytesTotal = 0;
        return false;
    }
}
bool ReturnTrue() { return true; } //One function to use for all ownership checks
bool IsAPICallCompleted(ISteamUtils* pThis, uint64 hSteamAPICall, bool* pbFailed)
{
    if (hSteamAPICall == DownloadingMod)
    {
        *pbFailed = false;
        return true;
    }
    return IsAPICallCompleted_o(pThis, hSteamAPICall, pbFailed);
}
uint32 GetAppID() { return 346110; } //This is needed for client to search servers with app ID 346110 instead of 480
uint32 GetNumSubscribedItems() //Every mod present in the Mods folder is treated as subscribed
{
    pvecModIDs.clear();
    WIN32_FIND_DATAA findData{};
    const HANDLE hFind = FindFirstFileExA(pchBaseModsPath, FindExInfoBasic, &findData, FindExSearchLimitToDirectories, NULL, 0);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;
    FindNextFileA(hFind, &findData); //Skip the .. so next iterations will return real directories
    while (FindNextFileA(hFind, &findData))
    {
        const PublishedFileId_t nModID = _strtoui64(findData.cFileName, NULL, 10);
        if (nModID && nModID != DownloadingMod)
            pvecModIDs.push_back(nModID);
    }
    FindClose(hFind);
    return (uint32)pvecModIDs.size() + (Pipe != INVALID_HANDLE_VALUE);
}
uint32 GetSubscribedItems(ISteamUGC* pThis, PublishedFileId_t* pvecPublishedFileID, uint32 cMaxEntries)
{
    memcpy(pvecPublishedFileID, pvecModIDs.data(), pvecModIDs.size() * sizeof(PublishedFileId_t));
    if (Pipe != INVALID_HANDLE_VALUE)
        pvecPublishedFileID[cMaxEntries - 1] = DownloadingMod;
    return cMaxEntries;
}
DWORD WINAPI PipeReadLoop(LPVOID lpThreadParameter)
{
    DWORD bytesRead;
    while (ReadFile(Pipe, &Progress, 17, &bytesRead, NULL))
        if (bytesRead == 0)
            break;
    Progress.Available = false;
    DownloadingMod = 0;
    CloseHandle(Pipe);
    Pipe = INVALID_HANDLE_VALUE;
    return 0;
}
uint64 SubscribeItem(ISteamUGC* pThis, PublishedFileId_t nPublishedFileID)
{
    DownloadingMod = nPublishedFileID;
    WCHAR tempFilePath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempFilePath);
    wcscat(tempFilePath, L"TEKLauncherModId"); //Use temp file to pass mod ID to the launcher, we cannot write to named pipe because TEKLauncher.exe is always an elevated process unlike the game
    HANDLE tempFile = CreateFileW(tempFilePath, GENERIC_ALL, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (tempFile == INVALID_HANDLE_VALUE)
        return nPublishedFileID;
    DWORD bytesWritten;
    WriteFile(tempFile, &nPublishedFileID, 8, &bytesWritten, NULL);
    Pipe = CreateFileW(L"\\\\.\\pipe\\TEKLauncher", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Pipe != INVALID_HANDLE_VALUE)
    {
        bool success = false;
        DWORD bytesRead;
        if (!ReadFile(Pipe, &success, 1, &bytesRead, NULL))
            success = false;
        if (success)
            CreateThread(NULL, 0, PipeReadLoop, NULL, 0, NULL);
        else
        {
            CloseHandle(Pipe);
            Pipe = INVALID_HANDLE_VALUE;
        }
    }
    CloseHandle(tempFile);
    return nPublishedFileID;
}
uint64* GetAppOwner(ISteamApps* pThis, uint64* pSteamId)
{
    *pSteamId = SteamID;
    return pSteamId;
}
HServerListRequest RequestInternetServerList(ISteamMatchmakingServers* pThis, uint32 iApp, MatchMakingKeyValuePair_t** ppchFilters, uint32 nFilters, void* pRequestServersResponse)
{
    strcat((*ppchFilters)[nFilters - 1].m_szValue, ",TEKWrapper:1");
    return RequestInternetServerList_o(pThis, iApp, ppchFilters, nFilters, pRequestServersResponse);
}
HServerListRequest RequestFriendsServerList(ISteamMatchmakingServers* pThis, uint32 iApp, MatchMakingKeyValuePair_t** ppchFilters, uint32 nFilters, void* pRequestServersResponse)
{
    strcat((*ppchFilters)[nFilters - 1].m_szValue, ",TEKWrapper:1");
    return RequestFriendsServerList_o(pThis, iApp, ppchFilters, nFilters, pRequestServersResponse);
}
HServerListRequest RequestFavoritesServerList(ISteamMatchmakingServers* pThis, uint32 iApp, MatchMakingKeyValuePair_t** ppchFilters, uint32 nFilters, void* pRequestServersResponse)
{
    strcat((*ppchFilters)[nFilters - 1].m_szValue, ",TEKWrapper:1");
    return RequestFavoritesServerList_o(pThis, iApp, ppchFilters, nFilters, pRequestServersResponse);
}
HServerListRequest RequestHistoryServerList(ISteamMatchmakingServers* pThis, uint32 iApp, MatchMakingKeyValuePair_t** ppchFilters, uint32 nFilters, void* pRequestServersResponse)
{
    strcat((*ppchFilters)[nFilters - 1].m_szValue, ",TEKWrapper:1");
    return RequestHistoryServerList_o(pThis, iApp, ppchFilters, nFilters, pRequestServersResponse);
}
bool SteamAPI_Init()
{
    LdrUnregisterDllNotification(Cookie); //Unregister DLL notification as we don't need it anymore, doing this in notification callback would crash the process so it's done here instead
    //Get Mods folder path from current directory
    cchBaseModsPath = GetCurrentDirectoryA(MAX_PATH, pchBaseModsPath);
    cchBaseModsPath -= 26;
    strcpy(pchBaseModsPath + cchBaseModsPath, "Mods\\*");
    cchBaseModsPath += 5;
    //Set Steam App ID to 480
    SetEnvironmentVariableW(L"SteamAppId", L"480");
    SetEnvironmentVariableW(L"GameAppId", L"480");
    //Initialize Steam API
    if (!SteamAPI_Init_o())
        return false;
    SteamUser_o()->GetSteamID(&SteamID); //Retrieve Steam ID for use in GetAppOwner function
    void** vfptr = *(void***)SteamApps_o(); //Get virtual function pointer array of interface to replace some of its methods with my own
    //But make vfptr's memory page first
    DWORD dwOldProtect;
    MEMORY_BASIC_INFORMATION mem;
    VirtualQuery(vfptr, &mem, sizeof(MEMORY_BASIC_INFORMATION));
    VirtualProtect(mem.BaseAddress, mem.RegionSize, PAGE_READWRITE, &dwOldProtect);
    vfptr[0] = ReturnTrue;
    vfptr[6] = ReturnTrue;
    vfptr[7] = ReturnTrue;
    vfptr[20] = GetAppOwner;
    vfptr = *(void***)SteamMatchmakingServers_o();
    //ISteamMatchmakingServers function overrides call original functions under the hood so their addresses need to be saved first
    RequestInternetServerList_o = (RequestServerList_t)vfptr[0];
    RequestFriendsServerList_o = (RequestServerList_t)vfptr[2];
    RequestFavoritesServerList_o = (RequestServerList_t)vfptr[3];
    RequestHistoryServerList_o = (RequestServerList_t)vfptr[4];
    vfptr[0] = RequestInternetServerList;
    vfptr[2] = RequestFriendsServerList;
    vfptr[3] = RequestFavoritesServerList;
    vfptr[4] = RequestHistoryServerList;
    vfptr = *(void***)SteamUGC_o();
    vfptr[25] = SubscribeItem;
    vfptr[27] = GetNumSubscribedItems;
    vfptr[28] = GetSubscribedItems;
    vfptr[29] = GetItemInstallInfo;
    vfptr[30] = GetItemUpdateInfo;
    vfptr = *(void***)SteamUtils_o();
    IsAPICallCompleted_o = (IsAPICallCompleted_t)vfptr[11];
    GetAPICallResult_o = (GetAPICallResult_t)vfptr[13];
    vfptr[9] = GetAppID;
    vfptr[11] = IsAPICallCompleted;
    vfptr[13] = GetAPICallResult;
    VirtualProtect(mem.BaseAddress, mem.RegionSize, dwOldProtect, &dwOldProtect); //Return old memory protection just in case
    return true;
}
VOID CALLBACK DllLoadNotification(ULONG NotificationReason, PLDR_DLL_LOADED_NOTIFICATION_DATA NotificationData, PVOID Context)
{
    if (NotificationReason != 1)
        return;
    if (!wcscmp(NotificationData->BaseDllName->Buffer, L"steam_api64.dll"))
    {
        //Load function addresses
        SteamAPI_Init_o = (SteamAPI_Init_t)GetProcAddress((HMODULE)NotificationData->DllBase, "SteamAPI_Init");
        SteamApps_o = (SteamApps_t)GetProcAddress((HMODULE)NotificationData->DllBase, "SteamApps");
        SteamMatchmakingServers_o = (SteamMatchmakingServers_t)GetProcAddress((HMODULE)NotificationData->DllBase, "SteamMatchmakingServers");
        SteamUGC_o = (SteamUGC_t)GetProcAddress((HMODULE)NotificationData->DllBase, "SteamUGC");
        SteamUser_o = (SteamUser_t)GetProcAddress((HMODULE)NotificationData->DllBase, "SteamUser");
        SteamUtils_o = (SteamUtils_t)GetProcAddress((HMODULE)NotificationData->DllBase, "SteamUtils");
        //Detour SteamAPI_Init with our own
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)SteamAPI_Init_o, SteamAPI_Init);
        DetourTransactionCommit();
    }
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        HMODULE hModule = GetModuleHandleW(L"ntdll.dll");
        if (!hModule)
            return FALSE;
        LdrRegisterDllNotification_t LdrRegisterDllNotification = (LdrRegisterDllNotification_t)GetProcAddress(hModule, "LdrRegisterDllNotification");
        LdrUnregisterDllNotification = (LdrUnregisterDllNotification_t)GetProcAddress(hModule, "LdrUnregisterDllNotification");
        LdrRegisterDllNotification(0, DllLoadNotification, NULL, &Cookie); //Set up notification that will proceed to modifying Steam API when it's loaded
    }
    return TRUE;
}