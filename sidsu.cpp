#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include <iostream>
#include <string>
#include <steam_api.h>
#include <fstream>
#include <chrono>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <vector>
#include <locale>
#include <codecvt>
#include <map>
#include <filesystem>

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


int wmain(int argc, wchar_t* argv[]){

    std::filesystem::path paramspath = (std::filesystem::temp_directory_path() /= std::string("dsuparams.txt"));
    std::cerr << "Path to parameter file " << paramspath << std::endl;

    if (argc > 1){
        if ( wcscmp(argv[1], L"-dsumode") == 0 ){
            std::cerr << "Launched with " << argc-1 << " arguments" << std::endl;

            std::filesystem::remove(paramspath);
            std::wofstream paramstore(paramspath);
            paramstore.imbue(std::locale(paramstore.getloc(), new std::codecvt_utf8_utf16<wchar_t, 0x10FFFF, std::generate_header>));

            for (int i = 1; i < argc; i++){
                paramstore << argv[i] << L'\n';
            }
            paramstore.close();

            wchar_t selfExeName[MAX_PATH*10];
            GetModuleFileNameW(NULL, selfExeName, MAX_PATH*10);

            wchar_t quotedfile[MAX_PATH*10];
            wcscpy(quotedfile, L"\"");
            wcscat(quotedfile, selfExeName);
            wcscat(quotedfile, L"\"");

            STARTUPINFOW taskinfo;
            PROCESS_INFORMATION newprocinfo;
            ZeroMemory(&taskinfo, sizeof(taskinfo));
            taskinfo.cb = sizeof(taskinfo);
            ZeroMemory(&newprocinfo, sizeof(newprocinfo));
            
            CreateProcessW(NULL, quotedfile, NULL, NULL, FALSE, 0, NULL, NULL, &taskinfo, &newprocinfo);
            return 1;

        }
    }

    
    std::wifstream settingsfile("dsusettings.txt");
    settingsfile.imbue(std::locale(settingsfile.getloc(), new std::codecvt_utf8_utf16<wchar_t, 0x10FFFF, std::consume_header>));

    std::wstring cemuexec = L"";
    std::wstring idtext = L"";
    int fakeappid = 480;
    std::cerr << "Trying to load settings \n";
    if (settingsfile.good()){
        std::getline(settingsfile, cemuexec);
        std::getline(settingsfile, idtext);
        fakeappid = std::stoi(idtext);
    }
    settingsfile.close();

    if ( SteamAPI_RestartAppIfNecessary(fakeappid) ){
        return 1;
    }
    std::cerr << "Steam API restart not necessary\n";

    std::vector<std::wstring> fileargs;

    std::wifstream paramfile(paramspath);
    paramfile.imbue(std::locale(paramfile.getloc(), new std::codecvt_utf8_utf16<wchar_t, 0x10FFFF, std::consume_header>));

    if (paramfile.good()){
        std::wstring readparam;
        while (std::getline(paramfile, readparam)){
            fileargs.push_back(readparam);
        }
    }
    paramfile.close();
    std::filesystem::remove(paramspath);

    std::cerr << "loaded " << fileargs.size() << " parameters from file\n";

    if (fileargs.size() == 0){
        wchar_t selfExeName[MAX_PATH*10];
        wchar_t selfDirName[MAX_PATH*10];
        GetModuleFileNameW(NULL, selfExeName, MAX_PATH*10);
        GetCurrentDirectoryW(MAX_PATH*10, selfDirName);

        std::wstring commname(selfExeName);
        commname.insert(commname.rfind(L"\\"), L"\\dsu");
        commname = L"\"" + commname + L"\"";

        for (int i = 1; i < argc; i++){
            commname.append(L" \"");
            commname.append(std::wstring(argv[i]));
            commname.append(L"\"");
        }

        wcscpy(selfExeName, commname.c_str());
        wcscat(selfDirName, L"\\dsu");

        STARTUPINFOW taskinfo;
        PROCESS_INFORMATION newprocinfo;
        ZeroMemory(&taskinfo, sizeof(taskinfo));
        taskinfo.cb = sizeof(taskinfo);
        ZeroMemory(&newprocinfo, sizeof(newprocinfo));
        
        CreateProcessW(NULL, selfExeName, NULL, NULL, FALSE, 0, NULL, selfDirName, &taskinfo, &newprocinfo);
        return 1;
    }
    

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);

    struct addrinfo *servresult = NULL, servhints;
    ZeroMemory( &servhints, sizeof(servhints));
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
    if (bind( DSUSocket, servresult->ai_addr, (int)servresult->ai_addrlen) == SOCKET_ERROR){
        std::cerr << "CANOT BIND SOCKET" << std::endl;
        return 1;
    }
    freeaddrinfo(servresult);
    

    
    STARTUPINFOW cemustartinfo;
    PROCESS_INFORMATION cemuprocessinfo;
    ZeroMemory( &cemustartinfo, sizeof(cemustartinfo) );
    cemustartinfo.cb = sizeof(cemustartinfo);
    ZeroMemory( &cemuprocessinfo, sizeof(cemuprocessinfo) );

    wchar_t CEMUEXESTR[MAX_PATH*10];
    wchar_t CEMUFOLDER[MAX_PATH*10];

    int firstclientargidx = 1;
    if (fileargs.size() >= 3){
        if (fileargs[1] == L"-dsuclientexe"){
            cemuexec = fileargs[2];
            firstclientargidx = 3;
        }
    }

    std::wstring cemucommand = cemuexec;
    std::wstring cemudir = cemucommand;
    cemucommand = L"\"" + cemucommand + L"\"";
    for (int i = firstclientargidx; i < fileargs.size(); i++){
        cemucommand.append(L" ");
        cemucommand.append(L"\"" + fileargs[i] + L"\"" );
    }
    cemudir.erase( cemudir.rfind(L"\\") );
    
    wcscpy(CEMUEXESTR, cemucommand.c_str() );
    wcscpy(CEMUFOLDER, cemudir.c_str() );

    std::cerr << "Starting target emulator\n";

    if (CreateProcessW(NULL, CEMUEXESTR, NULL, NULL, FALSE, 0, NULL, CEMUFOLDER, &cemustartinfo, &cemuprocessinfo) == 0){
        std::cerr << "Failed to launch client\n";
        return 1;
    }
    
    std::cerr << "Starting SteamInput\n";

    SteamAPI_Init();
    
    SteamInput()->Init();

    /*
    InputHandle_t wcontrollers[STEAM_INPUT_MAX_COUNT];
    SteamInput()->GetConnectedControllers(wcontrollers);

    SteamInput()->ShowBindingPanel(wcontrollers[0]);
    */
   

    std::vector<subscription> subs;

    std::vector<std::string> digitalActions = {"DpadLeft", "DpadDown", "DpadRight", "DpadUp", "Start", "RJoystickPress", "LJoystickPress", "Select",
                                "X", "A", "B", "Y", "R1", "L1", "Home", "Touch", "TPActive"};

    std::vector<std::string> analogActions = {"LeftJoystick", "RightJoystick", "LeftTrigger", "RightTrigger", "TPPosition"};

    std::map<std::string, InputDigitalActionHandle_t> digitalhandles;
    std::map<std::string, InputAnalogActionHandle_t> analoghandles;
    for (int i = 0; i < digitalActions.size(); i++){
        digitalhandles[digitalActions[i]] = SteamInput()->GetDigitalActionHandle(digitalActions[i].c_str());
    }
    for (int i = 0; i < analogActions.size(); i++){
        analoghandles[analogActions[i]] = SteamInput()->GetAnalogActionHandle(analogActions[i].c_str());
    }
    ControllerActionSetHandle_t actset = SteamInput()->GetActionSetHandle("DSUControls");

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

    DWORD procstat = STILL_ACTIVE;
    while(procstat == STILL_ACTIVE){
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        SteamInput()->ActivateActionSet(STEAM_INPUT_HANDLE_ALL_CONTROLLERS, actset);
        SteamInput()->RunFrame();
        uint64_t datacapture = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        reports++;
        controllers_num = SteamInput()->GetConnectedControllers(controllers);

        if ( (std::chrono::steady_clock::now() - lastcemucheck) > std::chrono::milliseconds(500) ){
            lastcemucheck = std::chrono::steady_clock::now();
            GetExitCodeProcess(cemuprocessinfo.hProcess, &procstat);
        }


        unsigned long bytesinbuffer;
        if (ioctlsocket(DSUSocket, FIONREAD, &bytesinbuffer) == SOCKET_ERROR){
            std::cerr << "CANNOT GET BUFFER SIZE" << std::endl;
            bytesinbuffer = 0;
        }
        while (bytesinbuffer > 0){
            sockaddr_in UDPClientAddr;
            int udpcaddrsize = sizeof(UDPClientAddr);
            ZeroMemory(&UDPClientAddr, sizeof(UDPClientAddr));

            int packetbytes = recvfrom(DSUSocket, (char *)UDPrecv, 2000, 0, (SOCKADDR *)&UDPClientAddr, &udpcaddrsize);
            int sockError = WSAGetLastError();

            if (ioctlsocket(DSUSocket, FIONREAD, &bytesinbuffer) == SOCKET_ERROR){
                std::cerr << "CANNOT GET BUFFER SIZE" << std::endl;
                break;
            }

            if (packetbytes == SOCKET_ERROR){
                //std::cerr << "SOCKET ERROR " << sockError << std::endl;
                continue;
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
                char* magic = "DSUS";
                memcpy(response, magic, 4);

                *(uint16_t *)(response+4) = 1001;
                *(uint16_t *)(response+6) = 6;
                *(uint32_t *)(response+12) = 0xB16B00B5;
                *(uint32_t *)(response+16) = 0x100000;
                *(uint16_t *)(response+20) = 1001;

                memset(response+8, 0, 4);
                *(uint32_t *)(response+8) = crc32(response, 22);

                sendto(DSUSocket, (char *)(response), 22, 0, (SOCKADDR *)(&UDPClientAddr), sizeof(UDPClientAddr) );
            }

            if (DSUmessType == 0x100001){
                int32_t portsAmmount = *(int32_t*)(UDPrecv+20);
                for (int i = 0; i < portsAmmount; i++){
                    uint8_t slotID = *(uint8_t*)(UDPrecv + 24 + i);

                    std::byte response[32];
                    char* magic = "DSUS";
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

                    sendto(DSUSocket, (char *)(response), 32, 0, (SOCKADDR *)(&UDPClientAddr), sizeof(UDPClientAddr) );

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
                        if (subs[i].clientaddr.sin_port == UDPClientAddr.sin_port && subs[i].clientaddr.sin_addr.S_un.S_addr == UDPClientAddr.sin_addr.S_un.S_addr && subs[i].slot == slottoreport){
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

        }

        for (int handle_idx = 0; handle_idx < controllers_num; handle_idx++){

            std::byte response[100];

            char* magic = "DSUS";
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
            digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["DpadLeft"]).bState;
            digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["DpadDown"]).bState;
            digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["DpadRight"]).bState;
            digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["DpadUp"]).bState;
            digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["Start"]).bState;
            digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["RJoystickPress"]).bState;
            digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["LJoystickPress"]).bState;
            digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["Select"]).bState;
            digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["X"]).bState;
            digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["A"]).bState;
            digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["B"]).bState;
            digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["Y"]).bState;
            digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["R1"]).bState;
            digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["L1"]).bState;
            digitals2 = (digitals2 << 1) | (uint8_t)( SteamInput()->GetAnalogActionData(controllers[handle_idx], analoghandles["RightTrigger"]).x == 1.00);
            digitals2 = (digitals2 << 1) | (uint8_t)( SteamInput()->GetAnalogActionData(controllers[handle_idx], analoghandles["LeftTrigger"]).x == 1.00);

            *(uint8_t *)(response+36) = digitals1;
            *(uint8_t *)(response+37) = digitals2;

            *(uint8_t *)(response+38) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["Home"]).bState ? 0xFF : 0x00;
            *(uint8_t *)(response+39) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["Touch"]).bState ? 0xFF : 0x00;

            
            *(uint8_t *)(response+40) = min(max(128 + (SteamInput()->GetAnalogActionData(controllers[handle_idx], analoghandles["LeftJoystick"]).x * 128.0),0),255);
            *(uint8_t *)(response+41) = min(max(128 + (SteamInput()->GetAnalogActionData(controllers[handle_idx], analoghandles["LeftJoystick"]).y * 128.0),0),255);
            *(uint8_t *)(response+42) = min(max(128 + (SteamInput()->GetAnalogActionData(controllers[handle_idx], analoghandles["RightJoystick"]).x * 128.0),0),255);
            *(uint8_t *)(response+43) = min(max(128 + (SteamInput()->GetAnalogActionData(controllers[handle_idx], analoghandles["RightJoystick"]).y * 128.0),0),255);
            
            *(uint8_t *)(response+44) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["DpadLeft"]).bState * 255;
            *(uint8_t *)(response+45) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["DpadDown"]).bState * 255;
            *(uint8_t *)(response+46) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["DpadRight"]).bState * 255;
            *(uint8_t *)(response+47) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["DpadUp"]).bState * 255;
            *(uint8_t *)(response+48) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["X"]).bState * 255;
            *(uint8_t *)(response+49) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["A"]).bState * 255;
            *(uint8_t *)(response+50) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["B"]).bState * 255;
            *(uint8_t *)(response+51) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["Y"]).bState * 255;
            *(uint8_t *)(response+52) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["R1"]).bState * 255;
            *(uint8_t *)(response+53) = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["L1"]).bState * 255;

            *(uint8_t *)(response+54) = min(max((SteamInput()->GetAnalogActionData(controllers[handle_idx], analoghandles["RightTrigger"]).x * 255.0),0),255);
            *(uint8_t *)(response+55) = min(max((SteamInput()->GetAnalogActionData(controllers[handle_idx], analoghandles["LeftTrigger"]).x * 255.0),0),255);

            bool touchpad_active = SteamInput()->GetDigitalActionData(controllers[handle_idx], digitalhandles["TPActive"]).bState;
            float touchpad_x = SteamInput()->GetAnalogActionData(controllers[handle_idx], analoghandles["TPPosition"]).x;
            float touchpad_y = SteamInput()->GetAnalogActionData(controllers[handle_idx], analoghandles["TPPosition"]).y;


            if (touchpad_active){
                if (last_touchpad_state[handle_idx] == false){
                    touchpad_id[handle_idx]++;
                }
                touchpad_x_adj[handle_idx] = min(max((960 + (touchpad_x * 960)),0),1919);
                touchpad_y_adj[handle_idx] = min(max((471 + (touchpad_y * 471)),0),942);
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
                        sendto(DSUSocket, (char *)(response), 100, 0, (SOCKADDR *)(&subs[i].clientaddr), sizeof(subs[i].clientaddr));
                    }
                }
                else{
                    subs.erase(subs.begin() + i);
                    i--;
                }
            }


            /*
            InputMotionData_t wynik;
            wynik = SteamInput()->GetMotionData(controllers[handle_idx]);
            std::cout << wynik.posAccelX << " " << wynik.posAccelY << " " << wynik.posAccelZ << " " << wynik.rotVelX << " " << wynik.rotVelY << " " << wynik.rotVelZ << "\n" ;
            */

        }
        
    }

    closesocket(DSUSocket);
    WSACleanup();

    SteamInput()->Shutdown();
    SteamAPI_Shutdown();

    std::cerr << "Exiting\n";
    
}


