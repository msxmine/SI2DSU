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

uint32_t crc32(std::byte message[], unsigned long long msglen){
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
};


int main(int argc, char* argv[]){

    if (argc > 1){
        if ( strcmp(argv[1], "-dsumode") == 0 ){
            remove("dsuparams.txt");
            std::ofstream paramstore;
            paramstore.open("dsuparams.txt");
            for (int i = 1; i < argc; i++){
                paramstore << argv[i] << "\n";
            }
            paramstore.close();

            WCHAR selfExeName[MAX_PATH*10];
            WCHAR selfDirName[MAX_PATH*10];
            GetModuleFileNameW(NULL, selfExeName, MAX_PATH*10);
            GetCurrentDirectoryW(MAX_PATH*10, selfDirName);

            WCHAR quotedfile[MAX_PATH*10];
            wcscpy(quotedfile, L"\"");
            wcscat(quotedfile, selfExeName);
            wcscat(quotedfile, L"\"");

            STARTUPINFOW taskinfo;
            PROCESS_INFORMATION newprocinfo;
            ZeroMemory(&taskinfo, sizeof(taskinfo));
            taskinfo.cb = sizeof(taskinfo);
            ZeroMemory(&newprocinfo, sizeof(newprocinfo));
            
            CreateProcessW(NULL, quotedfile, NULL, NULL, FALSE, 0, NULL, selfDirName, &taskinfo, &newprocinfo);
            return 1;

        }
    }

    
    std::ifstream settingsfile;
    settingsfile.open("dsusettings.txt");
    std::string cemuexec = "";
    int fakeappid = 480;
    if (settingsfile.good()){
        settingsfile >> cemuexec;
        settingsfile >> fakeappid;
    }
    settingsfile.close();

    if ( SteamAPI_RestartAppIfNecessary(fakeappid) ){
        return 1;
    }

    std::vector<std::string> fileargs;

    std::ifstream paramfile;
    paramfile.open("dsuparams.txt");
    if (paramfile.good()){
        std::string readparam;
        while (std::getline(paramfile, readparam, '\n')){
            fileargs.push_back(readparam);
        }
    }
    paramfile.close();
    remove("dsuparams.txt");

    if (fileargs.size() == 0){
        WCHAR selfExeName[MAX_PATH*10];
        WCHAR selfDirName[MAX_PATH*10];
        GetModuleFileNameW(NULL, selfExeName, MAX_PATH*10);
        GetCurrentDirectoryW(MAX_PATH*10, selfDirName);

        std::wstring commname(selfExeName);
        size_t beg = commname.rfind(L"\\");
        commname.insert(beg, L"\\dsu");
        commname = L"\"" + commname + L"\"";
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        for (int i = 1; i < argc; i++){
            commname.append(L" ");
            commname.append(converter.from_bytes(std::string(argv[i])));
        }

        WCHAR quotedfile[MAX_PATH*10];
        wcscpy(quotedfile, commname.c_str());

        wcscat(selfDirName, L"\\dsu");

        STARTUPINFOW taskinfo;
        PROCESS_INFORMATION newprocinfo;
        ZeroMemory(&taskinfo, sizeof(taskinfo));
        taskinfo.cb = sizeof(taskinfo);
        ZeroMemory(&newprocinfo, sizeof(newprocinfo));
        
        CreateProcessW(NULL, quotedfile, NULL, NULL, FALSE, 0, NULL, selfDirName, &taskinfo, &newprocinfo);
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

    WCHAR CEMUEXESTR[MAX_PATH*10];
    WCHAR CEMUFOLDER[MAX_PATH*10];

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> utfshifter;

    std::wstring cemucommand = utfshifter.from_bytes(cemuexec);
    std::wstring cemudir = cemucommand;
    cemucommand = L"\"" + cemucommand + L"\"";
    for (int i = 1; i < fileargs.size(); i++){
        cemucommand.append(L" ");
        cemucommand.append(L"\"" + utfshifter.from_bytes(fileargs[i]) + L"\"" );
    }
    cemudir.erase(cemudir.rfind(L"\\") + 1 );
    
    wcscpy(CEMUEXESTR, cemucommand.c_str() );
    wcscpy(CEMUFOLDER, cemudir.c_str() );

    CreateProcessW(NULL, CEMUEXESTR, NULL, NULL, FALSE, 0, NULL, CEMUFOLDER, &cemustartinfo, &cemuprocessinfo);
    


    SteamAPI_Init();
    
    SteamInput()->Init();

    /*
    InputHandle_t wcontrollers[STEAM_INPUT_MAX_COUNT];
    SteamInput()->GetConnectedControllers(wcontrollers);

    SteamInput()->ShowBindingPanel(wcontrollers[0]);
    */
   

    std::vector<subscription> subs;

    std::vector<std::string> digitalActions = {"DpadLeft", "DpadDown", "DpadRight", "DpadUp", "Start", "RJoystickPress", "LJoystickPress", "Select",
                                "X", "A", "B", "Y", "R1", "L1", "Home", "Touch"};

    std::vector<std::string> analogActions = {"LeftJoystick", "RightJoystick", "LeftTrigger", "RightTrigger"};

    std::map<std::string, InputDigitalActionHandle_t> digitalhandles;
    std::map<std::string, InputAnalogActionHandle_t> analoghandles;
    for (int i = 0; i < digitalActions.size(); i++){
        digitalhandles[digitalActions[i]] = SteamInput()->GetDigitalActionHandle(digitalActions[i].c_str());
    }
    for (int i = 0; i < analogActions.size(); i++){
        analoghandles[analogActions[i]] = SteamInput()->GetAnalogActionHandle(analogActions[i].c_str());
    }


    std::chrono::steady_clock::time_point lastcemucheck = std::chrono::steady_clock::now();

    std::byte UDPrecv[2000];

    uint32_t reports = 0;

    DWORD procstat = STILL_ACTIVE;
    while(procstat == STILL_ACTIVE){
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

                    if (slotID != 0){
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
                std::byte mactoreport[6];
                memcpy(mactoreport, UDPrecv+22, 6);

                if ((slotregistr && slottoreport == 0)|| registerall){
                    
                    bool alreadyexists = false;

                    for (int i = 0; i < subs.size(); i++){
                        if (subs[i].clientaddr.sin_port == UDPClientAddr.sin_port && subs[i].clientaddr.sin_addr.S_un.S_addr == UDPClientAddr.sin_addr.S_un.S_addr){
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

                        subs.push_back(newsub);
                    }

                }
            }

        }

        InputHandle_t controllers[STEAM_INPUT_MAX_COUNT];
        if (SteamInput()->GetConnectedControllers(controllers) == 0){
            //continue;
        }

        ESteamInputType controllerType = SteamInput()->GetInputTypeForHandle(controllers[0]);

        ControllerActionSetHandle_t actset = SteamInput()->GetActionSetHandle("DSUControls");
        SteamInput()->ActivateActionSet(controllers[0], actset);

        
        SteamInput()->RunFrame();
        uint64_t datacapture = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();



        std::byte response[100];

        char* magic = "DSUS";
        memcpy(response, magic, 4);

        *(uint16_t *)(response+4) = 1001;
        *(uint16_t *)(response+6) = 84;
        *(uint32_t *)(response+12) = 0xB16B00B5;
        *(uint32_t *)(response+16) = 0x100002;

        *(uint8_t *)(response+20) = 0;
        *(uint8_t *)(response+21) = 2;
        *(uint8_t *)(response+22) = 2;
        *(uint8_t *)(response+23) = 1;
        memset(response+24, 0, 6);
        *(uint8_t *)(response+30) = 0xEF;

        *(uint8_t *)(response+31) = 1;
        *(uint32_t *)(response+32) = reports++;

        uint8_t digitals1 = 0;
        uint8_t digitals2 = 0;
        digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["DpadLeft"]).bState;
        digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["DpadDown"]).bState;
        digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["DpadRight"]).bState;
        digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["DpadUp"]).bState;
        digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["Start"]).bState;
        digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["RJoystickPress"]).bState;
        digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["LJoystickPress"]).bState;
        digitals1 = (digitals1 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["Select"]).bState;
        digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["X"]).bState;
        digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["A"]).bState;
        digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["B"]).bState;
        digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["Y"]).bState;
        digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["R1"]).bState;
        digitals2 = (digitals2 << 1) | (uint8_t)SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["L1"]).bState;
        digitals2 = (digitals2 << 1) | (uint8_t)( SteamInput()->GetAnalogActionData(controllers[0], analoghandles["RightTrigger"]).x == 1.00);
        digitals2 = (digitals2 << 1) | (uint8_t)( SteamInput()->GetAnalogActionData(controllers[0], analoghandles["LeftTrigger"]).x == 1.00);

        *(uint8_t *)(response+36) = digitals1;
        *(uint8_t *)(response+37) = digitals2;

        *(uint8_t *)(response+38) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["Home"]).bState ? 0xFF : 0x00;
        *(uint8_t *)(response+39) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["Touch"]).bState ? 0xFF : 0x00;

        
        *(uint8_t *)(response+40) = min(max(128 + (SteamInput()->GetAnalogActionData(controllers[0], analoghandles["LeftJoystick"]).x * 128.0),0),255);
        *(uint8_t *)(response+41) = min(max(128 + (SteamInput()->GetAnalogActionData(controllers[0], analoghandles["LeftJoystick"]).y * 128.0),0),255);
        *(uint8_t *)(response+42) = min(max(128 + (SteamInput()->GetAnalogActionData(controllers[0], analoghandles["RightJoystick"]).x * 128.0),0),255);
        *(uint8_t *)(response+43) = min(max(128 + (SteamInput()->GetAnalogActionData(controllers[0], analoghandles["RightJoystick"]).y * 128.0),0),255);
        
        *(uint8_t *)(response+44) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["DpadLeft"]).bState * 255;
        *(uint8_t *)(response+45) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["DpadDown"]).bState * 255;
        *(uint8_t *)(response+46) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["DpadRight"]).bState * 255;
        *(uint8_t *)(response+47) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["DpadUp"]).bState * 255;
        *(uint8_t *)(response+48) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["X"]).bState * 255;
        *(uint8_t *)(response+49) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["A"]).bState * 255;
        *(uint8_t *)(response+50) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["B"]).bState * 255;
        *(uint8_t *)(response+51) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["Y"]).bState * 255;
        *(uint8_t *)(response+52) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["R1"]).bState * 255;
        *(uint8_t *)(response+53) = SteamInput()->GetDigitalActionData(controllers[0], digitalhandles["L1"]).bState * 255;

        *(uint8_t *)(response+54) = min(max((SteamInput()->GetAnalogActionData(controllers[0], analoghandles["RightTrigger"]).x * 255.0),0),255);
        *(uint8_t *)(response+55) = min(max((SteamInput()->GetAnalogActionData(controllers[0], analoghandles["LeftTrigger"]).x * 255.0),0),255);

        memset(response+56, 0, 12);

        *(uint64_t *)(response+68) = datacapture;

        if (controllerType == k_ESteamInputType_SteamController){
            *(float *)(response+76) = -(float)(SteamInput()->GetMotionData(controllers[0]).posAccelX) / 16384.0;
            *(float *)(response+80) = -(float)(SteamInput()->GetMotionData(controllers[0]).posAccelZ) / 16384.0;
            *(float *)(response+84) = (float)(SteamInput()->GetMotionData(controllers[0]).posAccelY) / 16384.0;
            *(float *)(response+88) = (float)(SteamInput()->GetMotionData(controllers[0]).rotVelX) / 16.0;
            *(float *)(response+92) = -(float)(SteamInput()->GetMotionData(controllers[0]).rotVelZ) / 16.0;
            *(float *)(response+96) = -(float)(SteamInput()->GetMotionData(controllers[0]).rotVelY) / 16.0;
        }
        else if (controllerType == k_ESteamInputType_PS4Controller || controllerType == k_ESteamInputType_PS3Controller){
            *(float *)(response+76) = -(float)(SteamInput()->GetMotionData(controllers[0]).posAccelX) / 16384.0;
            *(float *)(response+80) = -(float)(SteamInput()->GetMotionData(controllers[0]).posAccelY) / 16384.0;
            *(float *)(response+84) = -(float)(SteamInput()->GetMotionData(controllers[0]).posAccelZ) / 16384.0;
            *(float *)(response+88) = (float)(SteamInput()->GetMotionData(controllers[0]).rotVelX) / 32.0;
            *(float *)(response+92) = -(float)(SteamInput()->GetMotionData(controllers[0]).rotVelZ) / 32.0;
            *(float *)(response+96) = -(float)(SteamInput()->GetMotionData(controllers[0]).rotVelY) / 32.0;
        }
        else if (controllerType == k_ESteamInputType_SwitchJoyConSingle || controllerType == k_ESteamInputType_SwitchJoyConPair || controllerType == k_ESteamInputType_SwitchProController){
            *(float *)(response+76) = -(float)(SteamInput()->GetMotionData(controllers[0]).posAccelX) / 16384.0;
            *(float *)(response+80) = -(float)(SteamInput()->GetMotionData(controllers[0]).posAccelY) / 16384.0;
            *(float *)(response+84) = -(float)(SteamInput()->GetMotionData(controllers[0]).posAccelZ) / 16384.0;
            *(float *)(response+88) = (float)(SteamInput()->GetMotionData(controllers[0]).rotVelX) / 32.0;
            *(float *)(response+92) = -(float)(SteamInput()->GetMotionData(controllers[0]).rotVelZ) / 32.0;
            *(float *)(response+96) = -(float)(SteamInput()->GetMotionData(controllers[0]).rotVelY) / 32.0;
        }
        else if (controllerType == k_ESteamInputType_AndroidController || controllerType == k_ESteamInputType_AppleMFiController || controllerType == k_ESteamInputType_MobileTouch){ //PLACEHOLDER
            *(float *)(response+76) = (float)(SteamInput()->GetMotionData(controllers[0]).posAccelX);
            *(float *)(response+80) = (float)(SteamInput()->GetMotionData(controllers[0]).posAccelY);
            *(float *)(response+84) = (float)(SteamInput()->GetMotionData(controllers[0]).posAccelZ);
            *(float *)(response+88) = (float)(SteamInput()->GetMotionData(controllers[0]).rotVelX);
            *(float *)(response+92) = (float)(SteamInput()->GetMotionData(controllers[0]).rotVelY);
            *(float *)(response+96) = (float)(SteamInput()->GetMotionData(controllers[0]).rotVelZ);
        }
        else {
            *(float *)(response+76) = 0.0;
            *(float *)(response+80) = -1.0;
            *(float *)(response+84) = 0.0;
            *(float *)(response+88) = 0.0;
            *(float *)(response+92) = 0.0;
            *(float *)(response+96) = 0.0;
        }

        *(uint32_t *)(response+8) = 0;
        *(uint32_t *)(response+8) = crc32(response, 100);

        for (int i = 0; i < subs.size(); i++){
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - subs[i].lastrequest).count() < 5){
                sendto(DSUSocket, (char *)(response), 100, 0, (SOCKADDR *)(&subs[i].clientaddr), sizeof(subs[i].clientaddr));
            }
            else{
                subs.erase(subs.begin() + i);
                i--;
            }
        }


        /*
        InputMotionData_t wynik;
        wynik = SteamInput()->GetMotionData(controllers[0]);
        std::cout << wynik.posAccelX << " " << wynik.posAccelY << " " << wynik.posAccelZ << " " << wynik.rotVelX << " " << wynik.rotVelY << " " << wynik.rotVelZ << "\n" ;
        */
        
    }

    closesocket(DSUSocket);
    WSACleanup();

    SteamInput()->Shutdown();
    SteamAPI_Shutdown();
    
}


