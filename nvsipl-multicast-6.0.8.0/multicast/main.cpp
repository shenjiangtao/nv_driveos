/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/* STL Headers */
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <iostream>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

#include "CMaster.hpp"
#include "CCmdLineParser.hpp"

constexpr uint32_t MAJOR_VER = 2U; /**< Indicates the major revision. */
constexpr uint32_t MINOR_VER = 9U; /**< Indicates the minor revision. */
constexpr uint32_t PATCH_VER = 0U; /**< Indicates the patch revision. */

std::atomic<bool> g_bQuit{ false };
SIPLStatus g_status{ NVSIPL_STATUS_OK };

void GraceQuit(SIPLStatus status)
{
    g_bQuit = true;
    g_status = status;
}

/** Signal handler.*/
static void SigHandler(int signum)
{
    LOG_WARN("Received signal: %u. Quitting\n", signum);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    g_bQuit = true;

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
}

/** Sets up signal handler.*/
static void SigSetup(void)
{
    struct sigaction action
    {
    };
    action.sa_handler = SigHandler;

    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGQUIT, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);
}

static void InputEventThreadFunc(CMaster *pMaster)
{
    SIPLStatus status = NVSIPL_STATUS_OK;
    int max_fd = 0;
    fd_set master_set;
    fd_set read_set;
    FD_ZERO(&master_set);
    struct timeval timeout;
    char line[256];

    LOG_DBG("Enter: InputEventThreadFunc()\n");

    if (pMaster == nullptr) {
        LOG_ERR("Invalid thread data\n");
        g_bQuit = true;
        g_status = NVSIPL_STATUS_ERROR;
        return;
    }

    pthread_setname_np(pthread_self(), "InputEventThreadFunc");

    cout << "Enter 'q' to quit the application\n";
    cout << "Enter 's to suspend application\n";
    cout << "Enter 'r to resume application\n";
    if (pMaster->IsProducerResident()) {
        cout << "Enter 'at' to attach consumer\n";
        cout << "Enter 'de' to detach consumer\n";
    }
    cout << "-\n";

    FD_SET(STDIN_FILENO, &master_set);
    max_fd = STDIN_FILENO;

    while (!g_bQuit) {
        read_set = master_set;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ret = select(max_fd + 1, &read_set, nullptr, nullptr, &timeout);
        if (ret == -1) {
            std::cerr << "Error selecting cin.\n";
            g_bQuit = true;
            g_status = NVSIPL_STATUS_ERROR;
            break;
        } else if (ret == 0)
            continue;

        if (FD_ISSET(STDIN_FILENO, &read_set)) {
            cin.getline(line, 256);
            if (line[0] == 'q') {
                g_bQuit = true;
            } else if (line[0] == 's') {
                status = pMaster->Suspend();
                if (status != NVSIPL_STATUS_OK) {
                    g_bQuit = true;
                    g_status = status;
                } else
                    cout << "Application suspended\n";
            } else if (line[0] == 'r') {
                status = pMaster->Resume();
                if (status != NVSIPL_STATUS_OK) {
                    g_bQuit = true;
                    g_status = status;
                } else
                    cout << "Application Resumed\n";
            }
            if (pMaster->IsProducerResident() && (line[0] == 'a') && (line[1] == 't')) {
                cout << "Attach consumer" << endl;
                pMaster->AttachConsumer();
                continue;
            }
            if (pMaster->IsProducerResident() && (line[0] == 'd') && (line[1] == 'e')) {
                cout << "Detach consumer" << endl;
                pMaster->DetachConsumer();
                continue;
            }
        }
    }
    LOG_DBG("Exit: InputEventThreadFunc()\n");
}

static void SocketEventThreadFunc(CMaster *pMaster)
{
    SIPLStatus status = NVSIPL_STATUS_OK;
    int max_fd = 0;
    fd_set master_set;
    fd_set read_set;
    FD_ZERO(&master_set);
    struct timeval timeout;
    char msg[256];

    LOG_DBG("Enter: SocketEventThreadFunc()\n");

    if (pMaster == nullptr) {
        LOG_ERR("Invalid thread data\n");
        g_bQuit = true;
        g_status = NVSIPL_STATUS_ERROR;
        return;
    }

    pthread_setname_np(pthread_self(), "SocketEventThreadFunc");

    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        LOG_ERR("Socket API failed with errno : %d\n", errno);
        g_bQuit = true;
        g_status = NVSIPL_STATUS_ERROR;
        return;
    }

    sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/socket_pm", sizeof(addr.sun_path) - 1);

    if (connect(sock_fd, (sockaddr *)&addr, sizeof(addr)) == -1) {
        LOG_ERR("Socket connect failed with errno : %d\n", errno);
        g_bQuit = true;
        g_status = NVSIPL_STATUS_ERROR;
        return;
    }

    FD_SET(sock_fd, &master_set);
    max_fd = sock_fd;

    cout << "nvsipl_multicast: Wait event from pm_service socket.\n";
    while (!g_bQuit) {
        read_set = master_set;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        msg[0] = '\0';

        int ret = select(max_fd + 1, &read_set, nullptr, nullptr, &timeout);
        if (ret == -1) {
            std::cerr << "Error selecting sockets.\n";
            g_bQuit = true;
            g_status = NVSIPL_STATUS_ERROR;
            break;
        } else if (ret == 0)
            continue;

        if (FD_ISSET(sock_fd, &read_set)) {
            int len = read(sock_fd, msg, sizeof(msg) - 1);

            if (len == -1) {
                LOG_ERR("Socket read failed with errno : %d\n", errno);
                g_bQuit = true;
                g_status = NVSIPL_STATUS_ERROR;
                return;
            } else if (len >= 0) {
                msg[len] = '\0';
            }

            if (strcmp(msg, "Suspend Request") == 0) {
                status = pMaster->Suspend();
                if (status != NVSIPL_STATUS_OK) {
                    g_bQuit = true;
                    g_status = status;
                } else {
                    write(sock_fd, "Suspend Response", strlen("Suspend Response"));
                    cout << "Application suspended\n";
                }
            } else if (strcmp(msg, "Resume Request") == 0) {
                write(sock_fd, "Resume Response", strlen("Resume Response"));
                status = pMaster->Resume();
                if (status != NVSIPL_STATUS_OK) {
                    g_bQuit = true;
                    g_status = status;
                } else
                    cout << "Application Resumed\n";
            } else {
                g_bQuit = true;
            }
        }
    }

    close(sock_fd);
    LOG_DBG("Exit: SocketEventThreadFunc()\n");
}

int main(int argc, char *argv[])
{
    pthread_setname_np(pthread_self(), "Main");

    SIPLStatus status = NVSIPL_STATUS_OK;
    std::unique_ptr<CAppConfig> upAppConfig = make_unique<CAppConfig>();

    if (argc > 1) {
        CCmdLineParser cmdline;
        status = cmdline.Parse(argc, argv, upAppConfig.get());
        CHK_STATUS_AND_RETURN(status, "CCmdLineParser::Parse");

        if (upAppConfig->IsVersionShown()) {
            cout << "nvsipl_multicast " << MAJOR_VER << "." << MINOR_VER << "." << PATCH_VER << endl;
            return 0;
        }
    }

    std::unique_ptr<CMaster> upMaster = make_unique<CMaster>();
    upMaster->SetLogLevel(upAppConfig->GetVerbosity());

    LOG_INFO("Setting up signal handler\n");
    SigSetup();

    status = upMaster->PreInit(upAppConfig.get());
    CHK_STATUS_AND_RETURN(status, "CMaster::PreInit");

    if (!upAppConfig->IsSC7BootEnabled()) {
        status = upMaster->Resume();
        CHK_STATUS_AND_RETURN(status, "CMaster::Resume");
        std::thread inputEventThread(InputEventThreadFunc, upMaster.get());
        inputEventThread.join();
    } else {
        std::thread socketEventThread(SocketEventThreadFunc, upMaster.get());
        socketEventThread.join();
    }

    status = upMaster->Suspend();
    CHK_STATUS_AND_RETURN(status, "CMaster::Suspend");

    status = upMaster->PostDeInit();
    CHK_STATUS_AND_RETURN(status, "CMaster::PostDeInit");

    if (g_status == NVSIPL_STATUS_OK) {
        LOG_MSG("SUCCESS\n");
        return 0;
    } else {
        LOG_MSG("FAILURE\n");
        return -1;
    }
}
