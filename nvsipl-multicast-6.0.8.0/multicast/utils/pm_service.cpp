/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/netlink.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <thread>
#include <memory>
#include <pthread.h>
#include <mutex>
#include <condition_variable>
#include <nvos_s3_tegra_log.h>

#define SOCKET_PATH "/tmp/socket_pm"
#define MAX_CLIENTS 64
#define BUFFER_SIZE 256
#define INVALID_FD -1
class CPMSocketServer
{
  public:
    int Init();
    int Start();
    int DeInit();
    int Notify(const char *str);

  private:
    int server_fd;
    std::vector<int> m_vclient_fds;
    sockaddr_un address;
    std::condition_variable m_conditionVar;
    std::mutex m_mutex;
    bool m_responce = false;
};

int CPMSocketServer::Init()
{
    unlink(SOCKET_PATH);
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Error creating server socket.\n";
        return 1;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, SOCKET_PATH, sizeof(address.sun_path) - 1);

    if (bind(server_fd, (sockaddr *)&address, sizeof(address)) == -1) {
        std::cerr << "Error binding server socket.\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, MAX_CLIENTS) == -1) {
        std::cerr << "Error listening for connections.\n";
        close(server_fd);
        return 1;
    }
    std::cout << "pm_service: Listening on " << SOCKET_PATH << "\n";

    return 0;
}

int CPMSocketServer::Start()
{
    int max_fd = 0;
    fd_set master_set;

    FD_ZERO(&master_set);
    FD_SET(server_fd, &master_set);
    max_fd = server_fd;

    while (true) {
        fd_set read_set = master_set;

        if (select(max_fd + 1, &read_set, nullptr, nullptr, nullptr) == -1) {
            std::cerr << "Error selecting sockets.\n";
            break;
        }

        if (FD_ISSET(server_fd, &read_set)) {
            int new_fd = accept(server_fd, nullptr, nullptr);
            if (new_fd == -1) {
                std::cerr << "Error accepting new connection.\n";
            } else {
                std::cout << "pm_service: New client fd " << new_fd << " connected.\n";
                if (m_vclient_fds.size() < MAX_CLIENTS) {
                    m_vclient_fds.push_back(new_fd);
                    FD_SET(new_fd, &master_set);
                    if (new_fd > max_fd)
                        max_fd = new_fd;
                } else {
                    std::cout << "pm_service: Max client limit reached. Connection rejected.\n";
                    close(new_fd);
                }
            }
        }

        for (unsigned long i = 0; i < m_vclient_fds.size(); i++) {
            if (m_vclient_fds[i] != INVALID_FD && FD_ISSET(m_vclient_fds[i], &read_set)) {
                char buffer[BUFFER_SIZE];
                int len = recv(m_vclient_fds[i], buffer, BUFFER_SIZE, 0);
                if (len <= 0) {
                    std::cout << "pm_service: Client fd " << m_vclient_fds[i] << " disconnected.\n";
                    close(m_vclient_fds[i]);
                    FD_CLR(m_vclient_fds[i], &master_set);
                    m_vclient_fds[i] = INVALID_FD;
                } else {
                    buffer[len] = '\0';
                    std::cout << "pm_service: client fd " << m_vclient_fds[i] << ":" << buffer << std::endl;
                    if (!m_responce) {
                        m_responce = true;
                        m_conditionVar.notify_one();
                    }
                }
            }
        }
    }

    return 0;
}

int CPMSocketServer::DeInit()
{
    for (unsigned long i = 0; i < m_vclient_fds.size(); i++) {
        close(m_vclient_fds[i]);
    }

    close(server_fd);
    unlink(SOCKET_PATH);

    return 0;
}

int CPMSocketServer::Notify(const char *str)
{
    for (unsigned long j = 0; j < m_vclient_fds.size(); j++) {
        if (m_vclient_fds[j] != INVALID_FD) {
            send(m_vclient_fds[j], str, strlen(str), 0);
            m_responce = false;
            std::unique_lock<std::mutex> lk(m_mutex);
            m_conditionVar.wait(lk, [this] { return m_responce; });
        }
    }

    return 0;
}

#define NETLINK_USERSPACE_PM 30
#define MAX_PAYLOAD 1024

char *sock_recv(int sock_fd, struct nlmsghdr *nlh);
void sock_send(int sock_fd, struct nlmsghdr *nlh, const char *string);

static void prepare_msg(
    struct msghdr *msg, struct nlmsghdr *nlh, struct sockaddr_nl *dest_addr, struct iovec *iov, const char *string)
{
    memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
    memset(dest_addr, 0, sizeof(*dest_addr));
    memset(iov, 0, sizeof(*iov));
    memset(msg, 0, sizeof(*msg));

    if (string != NULL)
        strncpy((char *)NLMSG_DATA(nlh), string, MAX_PAYLOAD);

    nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
    nlh->nlmsg_pid = getpid();

    dest_addr->nl_family = AF_NETLINK;
    dest_addr->nl_pid = 0;    /* self pid */
    dest_addr->nl_groups = 0; /* unicast */

    iov->iov_base = (void *)nlh;
    iov->iov_len = nlh->nlmsg_len;
    msg->msg_name = (void *)dest_addr;
    msg->msg_namelen = sizeof(*dest_addr);
    msg->msg_iov = iov;
    msg->msg_iovlen = 1;
}

char *sock_recv(int sock_fd, struct nlmsghdr *nlh)
{
    struct sockaddr_nl dest_addr;
    struct iovec iov;
    struct msghdr msg;

    prepare_msg(&msg, nlh, &dest_addr, &iov, NULL);
    recvmsg(sock_fd, &msg, 0);

    std::cout << "pm_service: kernel: " << (char *)NLMSG_DATA(nlh) << "\n";
    return (char *)NLMSG_DATA(nlh);
}

void sock_send(int sock_fd, struct nlmsghdr *nlh, const char *string)
{
    struct sockaddr_nl dest_addr;
    struct iovec iov;
    struct msghdr msg;

    prepare_msg(&msg, nlh, &dest_addr, &iov, string);
    sendmsg(sock_fd, &msg, 0);
}

static void KernelEventHandleFunc(CPMSocketServer *pPMServer)
{
    pthread_setname_np(pthread_self(), "EventHandleThread");

    auto sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_USERSPACE_PM);
    if (sock_fd < 0) {
        std::cerr << "Socket API failed with errno :" << errno << "\n";
        return;
    }

    struct sockaddr_nl src_addr;
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid(); /* self pid */
    src_addr.nl_groups = 0;     /* unicast */

    if (bind(sock_fd, (struct sockaddr *)&src_addr, sizeof(src_addr)) < 0) {
        std::cerr << "Socket bind failed with errno :" << errno << "\n";
        return;
    }

    struct nlmsghdr *nlh;
    nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
    sock_send(sock_fd, nlh, "PM Register");

    while (true) {
        char *recv_str = sock_recv(sock_fd, nlh);

        if (strcmp(recv_str, "Suspend Request") == 0) {
            pPMServer->Notify("Suspend Request");
            usleep(500000); //WAR for kernel crash issue.
            sock_send(sock_fd, nlh, "Suspend Response");
        } else if (strcmp(recv_str, "Resume Request") == 0) {
            sock_send(sock_fd, nlh, "Resume Response");
            pPMServer->Notify("Resume Request");
        } else if (!recv_str)
            std::cerr << "nsupport command:" << recv_str << "\n";
        else
            std::cerr << "recv timeout\n";
    }
}

int main()
{
    std::unique_ptr<CPMSocketServer> upPMSocketServer = std::make_unique<CPMSocketServer>();

    upPMSocketServer->Init();

    std::thread KernelEventThread(KernelEventHandleFunc, upPMSocketServer.get());
    upPMSocketServer->Start();
    upPMSocketServer->DeInit();
    KernelEventThread.join();

    return 0;
}
