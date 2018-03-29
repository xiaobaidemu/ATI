#pragma once

#include <string>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>

union endpoint
{
public:
    endpoint()
    {
        memset(this, 0x00, sizeof(endpoint));
    }

    endpoint(const char* ip, const uint16_t port)
    {
        ASSERT(ip);

        if (inet_pton(AF_INET, ip, &sa_ipv4.sin_addr) == 1) {
            sa_ipv4.sin_family = AF_INET;
            sa_ipv4.sin_port = htons(port);
        }
        else if (inet_pton(AF_INET6, ip, &sa_ipv6.sin6_addr) == 1) {
            sa_ipv6.sin6_family = AF_INET6;
            sa_ipv6.sin6_port = htons(port);
        }
        else {
            ERROR("Not valid IPv4/IPv6 address: %s\n", ip);
            ASSERT(0);
        }
    }

    endpoint(const char* socket_file)
    {
        ASSERT(socket_file);

        const size_t len = strlen(socket_file);
        if (len >= sizeof(sa_unix.sun_path)) {
            ERROR("socket_file path too long: max = %lld, current = %lld\n", 
                (long long)sizeof(sa_unix.sun_path) - 1, (long long)len);
            ASSERT(0);
        }

        memcpy(sa_unix.sun_path, socket_file, len + 1);
        sa_unix.sun_family = AF_UNIX;
    }

    sockaddr* data()
    {
        return (sockaddr*)this;
    }

    sa_family_t family() const
    {
        return ((sockaddr*)this)->sa_family;
    }

    socklen_t socklen() const
    {
        const sa_family_t family = ((sockaddr*)this)->sa_family;
        switch (family) {
            case AF_INET: {
                return sizeof(sockaddr_in);
            }
            case AF_INET6: {
                return sizeof(sockaddr_in6);
            }
            case AF_UNIX: {
                return sizeof(sockaddr_un);
            }
            default: {
                FATAL("Unknown sa_family: %d\n", (int)family);
                ASSERT(0);
            }
        }
    }

    std::string to_string() const
    {
        std::string result;
        result.reserve(64);
        switch (family()) {
            case AF_INET: {
                char str_ipv4[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sa_ipv4.sin_addr, str_ipv4, sizeof(str_ipv4));
                result.append(str_ipv4);
                result.append(":");
                const uint16_t port = ntohs(sa_ipv4.sin_port);
                result.append(std::to_string(port));
                break;
            }
            case AF_INET6: {
                char str_ipv6[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &sa_ipv6.sin6_addr, str_ipv6, sizeof(str_ipv6));
                result.append("[");
                result.append(str_ipv6);
                result.append("]:");
                const uint16_t port = ntohs(sa_ipv6.sin6_port);
                result.append(std::to_string(port));
                break;
            }
            case AF_UNIX: {
                result.append("unix://");
                result.append(sa_unix.sun_path);
                break;
            }
            default: {
                result = "<unknown>";
            }
        }

        return result;
    }

private:
    sockaddr_in sa_ipv4;
    sockaddr_in6 sa_ipv6;
    sockaddr_un sa_unix;
    char* endpoint_ip;
    int   endpoint_port;
};
