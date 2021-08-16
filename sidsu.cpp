#include <iostream>
#include <string>
#include <steam_api.h>
#include <fstream>
#include <chrono>
#include <thread>
#include <vector>
#include <map>
#include <filesystem>
//#include <locale>
//#include <codecvt>

#if _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

typedef int socklen_t;
#elif __linux__
#include <sys/socket.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <sys/wait.h>
#include <netdb.h>
#include <fcntl.h>

typedef int SOCKET;
const SOCKET INVALID_SOCKET = -1;
const ssize_t SOCKET_ERROR = -1;
#endif

uint32_t crc32(const std::byte message[], unsigned long long msglen){
    uint32_t crc = 0xFFFFFFFF;

    for (unsigned long long i = 0; i < msglen; i++){
        crc = crc ^ (uint8_t)message[i];
        for (int j = 0; j < 8; j++){
            crc = (crc & 0x01) ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
        }
    }

    return ~crc;

}

struct subscription{
    uint32_t clientid = 0;
    sockaddr_in clientaddr;
    std::chrono::steady_clock::time_point lastrequest;
    uint8_t slot;
};

#if _WIN32
std::string w32ToUtf(wchar_t* utf16cstr){
    size_t cstrlen = WideCharToMultiByte(CP_UTF8, 0, utf16cstr, -1, NULL, 0, NULL, NULL);
    char* translated = (char*)malloc(sizeof(char)*cstrlen);
    WideCharToMultiByte(CP_UTF8, 0, utf16cstr, -1, translated, cstrlen, NULL, NULL);
    std::string returned = std::string(translated);
    free(translated);
    return returned;
}

std::wstring utfToW32(std::string input){
    size_t wstrlen = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, NULL, 0);
    wchar_t* translated = (wchar_t*)malloc(sizeof(wchar_t)*wstrlen);
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, translated, wstrlen);
    std::wstring returned = std::wstring(translated);
    free(translated);
    return returned;
}
#endif

std::string getCurrentExec(){
    #if _WIN32
        wchar_t selfExePath[MAX_PATH];
        GetModuleFileNameW(NULL, selfExePath, MAX_PATH-1);
        return w32ToUtf(selfExePath);
    #elif __linux__
        char result[8000];
        ssize_t written = readlink("/proc/self/exe", result, 7999);
        if (written != -1 && written != 7999){
            result[written] = '\0';
            return std::string(result);
        }
        else {
            return std::string();
        }
    #endif
}

bool steamProfileActive = true;
bool getDigitalState(InputHandle_t controller, InputDigitalActionHandle_t action){
    if (steamProfileActive){
        return SteamInput()->GetDigitalActionData(controller, action).bState;
    }
    else{
        return false;
    }
}

struct analogData {
    float x;
    float y;
};

struct analogData getAnalogState(InputHandle_t controller, InputAnalogActionHandle_t action){
    struct analogData result;
    if (steamProfileActive){
        InputAnalogActionData_t readRes = SteamInput()->GetAnalogActionData(controller, action);
        result.x = readRes.x;
        result.y = readRes.y;
    }
    else{
        result.x = 0.0;
        result.y = 0.0;
    }
    return result;
}
#if _WIN32
STARTUPINFOW taskinfo;
PROCESS_INFORMATION newprocinfo;
#elif __linux__
pid_t childPid;
#endif

int spawnProgram(std::string executable, std::vector<std::string> params, std::string workingdir){
    #if _WIN32
    std::string commandline = "\"" + executable + "\"";
    for (int i = 0; i < params.size(); i++){
        commandline += (" \"" + params[i] + "\"");
    }
    std::wstring utf16CL = utfToW32(commandline);
    std::wstring utf16WD = utfToW32(workingdir);

    wchar_t winCommandLine[MAX_PATH+33000];
    wchar_t winWorkingDir[MAX_PATH+1];
    wcscpy(winCommandLine, utf16CL.c_str());
    wcscpy(winWorkingDir, utf16WD.c_str());

    ZeroMemory(&taskinfo, sizeof(taskinfo));
    taskinfo.cb = sizeof(taskinfo);
    ZeroMemory(&newprocinfo, sizeof(newprocinfo));

    return CreateProcessW(NULL, winCommandLine, NULL, NULL, FALSE, 0, NULL, winWorkingDir, &taskinfo, &newprocinfo);
    #elif __linux__
        pid_t forkres = fork();
        if (forkres == 0){
            std::filesystem::current_path(workingdir);

            char** myargsarray = (char**)malloc(sizeof(char*)*(params.size()+2));

            myargsarray[0] = (char*)malloc(sizeof(char)*(executable.size()+1));
            memcpy(myargsarray[0], executable.c_str(), executable.size()+1);
            for (int i = 0 ; i < params.size(); i++){
                ssize_t arglen = params[i].size() + 1;
                myargsarray[i+1] = (char*)malloc(sizeof(char)*arglen);
                memcpy(myargsarray[i+1], params[i].c_str(), arglen);
            }
            myargsarray[params.size()+1] = NULL;
            execv(executable.c_str(), myargsarray);
        }
        childPid = forkres;
        return (forkres <= 0);
    #endif
}

bool checkSpawnedAlive(){
    #if _WIN32
        DWORD procstat = STILL_ACTIVE;
        GetExitCodeProcess(newprocinfo.hProcess, &procstat);
        return (procstat == STILL_ACTIVE);
    #elif __linux__
        int procstat;
        pid_t returned = waitpid(childPid, &procstat, WNOHANG);
        return !(returned > 0);
    #endif
}

bool compareNetworkAddr(sockaddr_in &first, sockaddr_in &second){
    #if _WIN32
        bool result = true;
        result &= (first.sin_port == second.sin_port);
        result &= (first.sin_addr.S_un.S_addr == second.sin_addr.S_un.S_addr);
    #elif __linux__
        bool result = true;
        result &= (first.sin_port == second.sin_port);
        result &= (first.sin_addr.s_addr == second.sin_addr.s_addr);
    #endif
    return result;
}


#if _WIN32
int wmain(int argc, wchar_t* clArguments[]){
    std::vector<std::string> argv;
    for (int i = 0; i < argc; i++){
        argv.push_back(w32ToUtf(clArguments[i]));
    }
#elif __linux__
int main(int argc, char* clArguments[]){
    std::vector<std::string> argv;
    for (int i = 0; i < argc; i++){
        argv.push_back(std::string(clArguments[i]));
    }
#endif

    std::filesystem::path paramspath = (std::filesystem::temp_directory_path() /= std::string("dsuparams.txt"));
    std::cerr << "Path to parameter file " << paramspath << std::endl;

    if (argc > 1){
        if ( argv[1] == "-dsumode" ){
            std::cerr << "Launched with " << argc-1 << " arguments" << std::endl;

            std::filesystem::remove(paramspath);
            std::ofstream paramstore(paramspath);

            for (int i = 1; i < argc; i++){
                paramstore << argv[i] << '\n';
            }
            paramstore.close();

            std::string myExePath = getCurrentExec();
            std::vector<std::string> emptyArgs;
            std::string myDirectory = (std::filesystem::current_path()).u8string();

            spawnProgram(myExePath, emptyArgs, myDirectory);
            return 1;

        }
    }

    
    std::ifstream settingsfile("dsusettings.txt");
    std::string cemuexec = "";
    std::string idtext = "";
    int fakeappid = 480;
    std::cerr << "Trying to load settings \n";
    if (settingsfile.good()){
        std::getline(settingsfile, cemuexec);
        std::getline(settingsfile, idtext);
        fakeappid = std::stoi(idtext);
    }
    settingsfile.close();

    std::ifstream paramfile(paramspath);
    std::vector<std::string> fileargs;
    if (paramfile.good()){
        std::string readparam;
        while (std::getline(paramfile, readparam)){
            fileargs.push_back(readparam);
        }
    }
    paramfile.close();
    std::filesystem::remove(paramspath);
    std::cerr << "loaded " << fileargs.size() << " parameters from file\n";

    bool dsuMode = false;
    bool dsuSteamBind = true;
    bool dsuCustomEmu = false;
    std::vector<std::string> emuParams;
    std::string emuCustomExe = "";

    bool clientargs = false;
    for (int i = 0; i < fileargs.size(); i++){
        dsuMode = true;
        if (!clientargs){
            if (fileargs[i] == "-dsumode"){
                continue;
            }
            else if (fileargs[i] == "-dsumotiononly"){
                dsuSteamBind = false;
                steamProfileActive = false;
                continue;
            }
            else if (fileargs[i] == "-dsuclientexe"){
                if (i+1 < fileargs.size()){
                    dsuCustomEmu = true;
                    emuCustomExe = fileargs[i+1];
                    i++;
                    continue;
                }
            }
            else{
                clientargs = true;
            }
        }
        emuParams.push_back(fileargs[i]);
    }

    if (dsuSteamBind){
        if ( SteamAPI_RestartAppIfNecessary(fakeappid) ){
            return 1;
        }
        std::cerr << "Steam API restart not necessary\n";
    }

    if (!dsuMode){
        std::filesystem::path myExeName = std::filesystem::path(getCurrentExec()).filename();
        std::filesystem::path myDirectory = std::filesystem::current_path();
        std::string gameExe = (myDirectory / "dsu" / myExeName).u8string();
        std::vector<std::string> gameArgs;

        for (int i = 1; i < argc; i++){
            gameArgs.push_back(argv[i]);
        }

        spawnProgram(gameExe, gameArgs, myDirectory.u8string());
        return 1;
    }
    
    #if _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);
    #endif

    struct addrinfo servhints;
    struct addrinfo *servresult = NULL;
    memset(&servhints, 0, sizeof(servhints));
    servhints.ai_family = AF_INET;
    servhints.ai_socktype = SOCK_DGRAM;
    servhints.ai_protocol = IPPROTO_UDP;
    servhints.ai_flags = AI_PASSIVE;
    getaddrinfo("localhost", "26760", &servhints, &servresult);
    SOCKET DSUSocket = INVALID_SOCKET;
    DSUSocket = socket(servresult->ai_family, servresult->ai_socktype, servresult->ai_protocol);
    if (DSUSocket == INVALID_SOCKET){
        std::cerr << "CANOT CREATE SOCKET" << std::endl;
        return 1;
    }
    #if _WIN32
        unsigned long nonblock = 1;
        ioctlsocket(DSUSocket, FIONBIO, &nonblock);
    #elif __linux__
        int curopts = fcntl(DSUSocket, F_GETFL);
        fcntl(DSUSocket, F_SETFL, curopts | O_NONBLOCK);
    #endif
    if (bind( DSUSocket, servresult->ai_addr, (int)servresult->ai_addrlen) == SOCKET_ERROR){
        std::cerr << "CANOT BIND SOCKET" << std::endl;
        return 1;
    }
    freeaddrinfo(servresult);
    
    std::string cemucommand = cemuexec;
    if (dsuCustomEmu){
        cemucommand = emuCustomExe;
    }
    std::string cemudir = std::filesystem::path(cemucommand).remove_filename().u8string();

    std::cerr << "Starting target emulator\n";
    if (spawnProgram(cemucommand, emuParams, cemudir) == 0){
        std::cerr << "Failed to launch client\n";
        return 1;
    }
    
    std::cerr << "Starting SteamInput\n";

    SteamAPI_Init();
    SteamInput()->Init();

    std::vector<subscription> subs;

    std::vector<std::string> digitalActions = {"DpadLeft", "DpadDown", "DpadRight", "DpadUp", "Start", "RJoystickPress", "LJoystickPress", "Select",
                                "X", "A", "B", "Y", "R1", "L1", "Home", "Touch", "TPActive"};

    std::vector<std::string> analogActions = {"LeftJoystick", "RightJoystick", "LeftTrigger", "RightTrigger", "TPPosition"};

    std::map<std::string, InputDigitalActionHandle_t> digitalhandles;
    std::map<std::string, InputAnalogActionHandle_t> analoghandles;
    ControllerActionSetHandle_t actset;
    if (dsuSteamBind){
        for (int i = 0; i < digitalActions.size(); i++){
            digitalhandles[digitalActions[i]] = SteamInput()->GetDigitalActionHandle(digitalActions[i].c_str());
        }
        for (int i = 0; i < analogActions.size(); i++){
            analoghandles[analogActions[i]] = SteamInput()->GetAnalogActionHandle(analogActions[i].c_str());
        }
        actset = SteamInput()->GetActionSetHandle("DSUControls");
    }

    InputHandle_t controllers[STEAM_INPUT_MAX_COUNT];
    int controllers_num;

    std::chrono::steady_clock::time_point lastcemucheck = std::chrono::steady_clock::now();

    std::byte UDPrecv[2000];

    uint32_t reports = 0;

    uint8_t touchpad_id[STEAM_INPUT_MAX_COUNT];
    bool last_touchpad_state[STEAM_INPUT_MAX_COUNT];
    uint16_t touchpad_x_adj[STEAM_INPUT_MAX_COUNT];
    uint16_t touchpad_y_adj[STEAM_INPUT_MAX_COUNT];
    for (int handle_idx = 0; handle_idx < STEAM_INPUT_MAX_COUNT; handle_idx++){
        touchpad_id[handle_idx] = 0;
        last_touchpad_state[handle_idx] = false;
        touchpad_x_adj[handle_idx] = 0;
        touchpad_y_adj[handle_idx] = 0;
    }

    bool clientRunning = true;
    while(clientRunning){
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (dsuSteamBind){
            SteamInput()->ActivateActionSet(STEAM_INPUT_HANDLE_ALL_CONTROLLERS, actset);
        }
        SteamInput()->RunFrame();
        uint64_t datacapture = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        reports++;
        controllers_num = SteamInput()->GetConnectedControllers(controllers);

        if ( (std::chrono::steady_clock::now() - lastcemucheck) > std::chrono::milliseconds(500) ){
            lastcemucheck = std::chrono::steady_clock::now();
            clientRunning = checkSpawnedAlive();
        }

        sockaddr_in UDPClientAddr;
        socklen_t udpcaddrsize = sizeof(UDPClientAddr);
        memset(&UDPClientAddr, 0, sizeof(UDPClientAddr));
        while (true){
            ssize_t packetbytes = recvfrom(DSUSocket, (char*)UDPrecv, 2000, 0, (sockaddr*)&UDPClientAddr, &udpcaddrsize);

            if (packetbytes == SOCKET_ERROR){
                //int sockError = WSAGetLastError();
                //std::cerr << "SOCKET ERROR " << sockError << std::endl;
                break;
            }

            if (packetbytes < 20){
                std::cerr << "PACKET TOO SHORT < 20B" << std::endl;
                continue;
            }

            if ( strcmp("DSUC", std::string((char*)UDPrecv, 4).c_str()) != 0){
                std::cerr << "WRONG PACKET MAGIC" << std::endl;
                continue;
            }
            if ( *(uint16 *)(UDPrecv+4) != 1001){
                std::cerr << "INCORRECT PROTOCOL" << std::endl;
                continue;
            }
            
            if ( (*(uint16 *)(UDPrecv+6)) + 16 < packetbytes){
                packetbytes = (*(uint16 *)(UDPrecv+6)) + 16;
                std::cerr << "TRUNCATING PACKET" << std::endl;
            }
            else if ( (*(uint16 *)(UDPrecv+6)) + 16 > packetbytes){
                std::cerr << "PACKET TOO SHORT" << std::endl;
                continue;
            }

            uint32_t expectedcrc = ( *(uint32_t*)(UDPrecv+8) );
            *(uint32*)(UDPrecv+8) = 0;
            uint32_t realcrc = crc32(UDPrecv, packetbytes);

            if (expectedcrc != realcrc){
                std::cerr << "PACKET CRC MISMATCH" << std::endl;
                continue;
            }

            uint32_t DSUclientID = *(uint32_t*)(UDPrecv+12);
            
            uint32_t DSUmessType = *(uint32_t*)(UDPrecv+16);

            if (DSUmessType == 0x100000){
                std::byte response[22];
                const char* magic = "DSUS";
                memcpy(response, magic, 4);

                *(uint16_t *)(response+4) = 1001;
                *(uint16_t *)(response+6) = 6;
                *(uint32_t *)(response+12) = 0xB16B00B5;
                *(uint32_t *)(response+16) = 0x100000;
                *(uint16_t *)(response+20) = 1001;

                memset(response+8, 0, 4);
                *(uint32_t *)(response+8) = crc32(response, 22);

                sendto(DSUSocket, (char *)(response), 22, 0, (sockaddr *)(&UDPClientAddr), sizeof(UDPClientAddr) );
            }

            if (DSUmessType == 0x100001){
                int32_t portsAmmount = *(int32_t*)(UDPrecv+20);
                for (int i = 0; i < portsAmmount; i++){
                    uint8_t slotID = *(uint8_t*)(UDPrecv + 24 + i);

                    std::byte response[32];
                    const char* magic = "DSUS";
                    memcpy(response, magic, 4);

                    *(uint16_t *)(response+4) = 1001;
                    *(uint16_t *)(response+6) = 16;
                    *(uint32_t *)(response+12) = 0xB16B00B5;
                    *(uint32_t *)(response+16) = 0x100001;
                    *(uint8_t *)(response+20) = slotID;

                    if (slotID + 1 > controllers_num){
                        memset(response+21, 0, 11);
                    }
                    else{
                        *(uint8_t *)(response+21) = 2;
                        *(uint8_t *)(response+22) = 2;
                        *(uint8_t *)(response+23) = 1;
                        memset(response+24, 0, 6);
                        *(uint8_t *)(response+30) = 0xEF;
                        *(uint8_t *)(response+31) = 0;
                    }

                    memset(response+8, 0, 4);
                    *(uint32_t *)(response+8) = crc32(response, 32);

                    sendto(DSUSocket, (char *)(response), 32, 0, (sockaddr *)(&UDPClientAddr), sizeof(UDPClientAddr) );

                }
            }

            if (DSUmessType == 0x100002){
                bool slotregistr = *(uint8_t*)(UDPrecv+20) & 1;
                bool macregistr  = *(uint8_t*)(UDPrecv+20) & 2;
                bool registerall = *(uint8_t*)(UDPrecv+20) == 0;

                uint8_t slottoreport = *(uint8_t*)(UDPrecv+21);
                if (registerall){
                    slottoreport = 5;
                }
                std::byte mactoreport[6];
                memcpy(mactoreport, UDPrecv+22, 6);

                if (slotregistr || registerall){
                    
                    bool alreadyexists = false;

                    for (int i = 0; i < subs.size(); i++){
                        if (compareNetworkAddr(subs[i].clientaddr, UDPClientAddr) && subs[i].slot == slottoreport){
                            alreadyexists = true;
                            subs[i].lastrequest = std::chrono::steady_clock::now();
                            break;
                        }
                    }

                    if (!alreadyexists){
                        subscription newsub;
                        memcpy(&newsub.clientaddr, &UDPClientAddr, sizeof(UDPClientAddr));
                        newsub.lastrequest = std::chrono::steady_clock::now();
                        newsub.clientid = DSUclientID;
                        newsub.slot = slottoreport;

                        subs.push_back(newsub);
                    }

                }
            }

            udpcaddrsize = sizeof(UDPClientAddr);
            memset(&UDPClientAddr, 0, sizeof(UDPClientAddr));

        }

        for (int handle_idx = 0; handle_idx < controllers_num; handle_idx++){

            std::byte response[100];

            const char* magic = "DSUS";
            memcpy(response, magic, 4);

            *(uint16_t *)(response+4) = 1001;
            *(uint16_t *)(response+6) = 84;
            *(uint32_t *)(response+12) = 0xB16B00B5;
            *(uint32_t *)(response+16) = 0x100002;

            *(uint8_t *)(response+20) = handle_idx;
            *(uint8_t *)(response+21) = 2;
            *(uint8_t *)(response+22) = 2;
            *(uint8_t *)(response+23) = 1;
            memset(response+24, 0, 6);
            *(uint8_t *)(response+30) = 0xEF;

            *(uint8_t *)(response+31) = 1;
            *(uint32_t *)(response+32) = reports;

            uint8_t digitals1 = 0;
            uint8_t digitals2 = 0;
            digitals1 = (digitals1 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["DpadLeft"]);
            digitals1 = (digitals1 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["DpadDown"]);
            digitals1 = (digitals1 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["DpadRight"]);
            digitals1 = (digitals1 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["DpadUp"]);
            digitals1 = (digitals1 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["Start"]);
            digitals1 = (digitals1 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["RJoystickPress"]);
            digitals1 = (digitals1 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["LJoystickPress"]);
            digitals1 = (digitals1 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["Select"]);
            digitals2 = (digitals2 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["X"]);
            digitals2 = (digitals2 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["A"]);
            digitals2 = (digitals2 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["B"]);
            digitals2 = (digitals2 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["Y"]);
            digitals2 = (digitals2 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["R1"]);
            digitals2 = (digitals2 << 1) | (uint8_t)getDigitalState(controllers[handle_idx], digitalhandles["L1"]);
            digitals2 = (digitals2 << 1) | (uint8_t)( getAnalogState(controllers[handle_idx], analoghandles["RightTrigger"]).x == 1.00);
            digitals2 = (digitals2 << 1) | (uint8_t)( getAnalogState(controllers[handle_idx], analoghandles["LeftTrigger"]).x == 1.00);

            *(uint8_t *)(response+36) = digitals1;
            *(uint8_t *)(response+37) = digitals2;

            *(uint8_t *)(response+38) = getDigitalState(controllers[handle_idx], digitalhandles["Home"]) ? 0xFF : 0x00;
            *(uint8_t *)(response+39) = getDigitalState(controllers[handle_idx], digitalhandles["Touch"]) ? 0xFF : 0x00;

            
            *(uint8_t *)(response+40) = std::min(std::max<int>(128 + (getAnalogState(controllers[handle_idx], analoghandles["LeftJoystick"]).x * 128.0),0),255);
            *(uint8_t *)(response+41) = std::min(std::max<int>(128 + (getAnalogState(controllers[handle_idx], analoghandles["LeftJoystick"]).y * 128.0),0),255);
            *(uint8_t *)(response+42) = std::min(std::max<int>(128 + (getAnalogState(controllers[handle_idx], analoghandles["RightJoystick"]).x * 128.0),0),255);
            *(uint8_t *)(response+43) = std::min(std::max<int>(128 + (getAnalogState(controllers[handle_idx], analoghandles["RightJoystick"]).y * 128.0),0),255);
            
            *(uint8_t *)(response+44) = getDigitalState(controllers[handle_idx], digitalhandles["DpadLeft"]) * 255;
            *(uint8_t *)(response+45) = getDigitalState(controllers[handle_idx], digitalhandles["DpadDown"]) * 255;
            *(uint8_t *)(response+46) = getDigitalState(controllers[handle_idx], digitalhandles["DpadRight"]) * 255;
            *(uint8_t *)(response+47) = getDigitalState(controllers[handle_idx], digitalhandles["DpadUp"]) * 255;
            *(uint8_t *)(response+48) = getDigitalState(controllers[handle_idx], digitalhandles["X"]) * 255;
            *(uint8_t *)(response+49) = getDigitalState(controllers[handle_idx], digitalhandles["A"]) * 255;
            *(uint8_t *)(response+50) = getDigitalState(controllers[handle_idx], digitalhandles["B"]) * 255;
            *(uint8_t *)(response+51) = getDigitalState(controllers[handle_idx], digitalhandles["Y"]) * 255;
            *(uint8_t *)(response+52) = getDigitalState(controllers[handle_idx], digitalhandles["R1"]) * 255;
            *(uint8_t *)(response+53) = getDigitalState(controllers[handle_idx], digitalhandles["L1"]) * 255;

            *(uint8_t *)(response+54) = std::min(std::max<int>((getAnalogState(controllers[handle_idx], analoghandles["RightTrigger"]).x * 255.0),0),255);
            *(uint8_t *)(response+55) = std::min(std::max<int>((getAnalogState(controllers[handle_idx], analoghandles["LeftTrigger"]).x * 255.0),0),255);

            bool touchpad_active = getDigitalState(controllers[handle_idx], digitalhandles["TPActive"]);
            float touchpad_x = getAnalogState(controllers[handle_idx], analoghandles["TPPosition"]).x;
            float touchpad_y = getAnalogState(controllers[handle_idx], analoghandles["TPPosition"]).y;


            if (touchpad_active){
                if (last_touchpad_state[handle_idx] == false){
                    touchpad_id[handle_idx]++;
                }
                touchpad_x_adj[handle_idx] = std::min(std::max<int>((960 + (touchpad_x * 960)),0),1919);
                touchpad_y_adj[handle_idx] = std::min(std::max<int>((471 + (touchpad_y * 471)),0),942);
            }
            *(uint8_t *)(response+56) = (touchpad_active ? 1 : 0);
            *(uint8_t *)(response+57) = touchpad_id[handle_idx];
            *(uint16_t*)(response+58) = touchpad_x_adj[handle_idx];
            *(uint16_t*)(response+60) = touchpad_y_adj[handle_idx];

            last_touchpad_state[handle_idx] = touchpad_active;

            memset(response+62, 0, 6);

            *(uint64_t *)(response+68) = datacapture;

            ESteamInputType controllerType = SteamInput()->GetInputTypeForHandle(controllers[handle_idx]);
            if (controllerType == k_ESteamInputType_SteamController){
                *(float *)(response+76) = -(float)(SteamInput()->GetMotionData(controllers[handle_idx]).posAccelX) / 16384.0;
                *(float *)(response+80) = -(float)(SteamInput()->GetMotionData(controllers[handle_idx]).posAccelZ) / 16384.0;
                *(float *)(response+84) = (float)(SteamInput()->GetMotionData(controllers[handle_idx]).posAccelY) / 16384.0;
                *(float *)(response+88) = (float)(SteamInput()->GetMotionData(controllers[handle_idx]).rotVelX) / 16.0;
                *(float *)(response+92) = -(float)(SteamInput()->GetMotionData(controllers[handle_idx]).rotVelZ) / 16.0;
                *(float *)(response+96) = -(float)(SteamInput()->GetMotionData(controllers[handle_idx]).rotVelY) / 16.0;
            }
            else {
                *(float *)(response+76) = -(float)(SteamInput()->GetMotionData(controllers[handle_idx]).posAccelX) / 16384.0;
                *(float *)(response+80) = -(float)(SteamInput()->GetMotionData(controllers[handle_idx]).posAccelY) / 16384.0;
                *(float *)(response+84) = -(float)(SteamInput()->GetMotionData(controllers[handle_idx]).posAccelZ) / 16384.0;
                *(float *)(response+88) = (float)(SteamInput()->GetMotionData(controllers[handle_idx]).rotVelX) / 32.0;
                *(float *)(response+92) = -(float)(SteamInput()->GetMotionData(controllers[handle_idx]).rotVelZ) / 32.0;
                *(float *)(response+96) = -(float)(SteamInput()->GetMotionData(controllers[handle_idx]).rotVelY) / 32.0;
            }

            *(uint32_t *)(response+8) = 0;
            *(uint32_t *)(response+8) = crc32(response, 100);

            for (int i = 0; i < subs.size(); i++){
                if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - subs[i].lastrequest).count() < 5){
                    if (subs[i].slot == 5 || subs[i].slot == handle_idx){
                        sendto(DSUSocket, (char *)(response), 100, 0, (sockaddr *)(&subs[i].clientaddr), sizeof(subs[i].clientaddr));
                    }
                }
                else{
                    subs.erase(subs.begin() + i);
                    i--;
                }
            }

        }
        
    }
    #if _WIN32
    closesocket(DSUSocket);
    WSACleanup();
    #elif __linux__
    close(DSUSocket);
    #endif

    SteamInput()->Shutdown();
    SteamAPI_Shutdown();

    std::cerr << "Exiting\n";
    
}


