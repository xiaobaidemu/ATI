#include "test.h"

#include <sendrecv.h>
#include <thread>
#include <vector>
#include <mutex>

#define SOCKET_FILE         ("/tmp/test_socket_echo_multi_thread_uds.sock")
#define ECHO_DATA_LENGTH    ((size_t)1024 * 1024 * 16)  // 16MB
#define ECHO_DATA_ROUND     ((size_t)16)
#define THREAD_COUNT        (32)

static char dummy_data[ECHO_DATA_LENGTH];

static size_t client_receive_bytes[THREAD_COUNT];
static size_t client_send_bytes[THREAD_COUNT];
static std::atomic_int_fast64_t server_send_bytes;
static std::atomic_int_fast64_t server_receive_bytes;
static std::mutex client_close[THREAD_COUNT];
static std::mutex listener_close;
static std::mutex server_all_close;
static std::atomic_int server_alive_connections(THREAD_COUNT);

static void set_server_connection_callbacks(connection* server_conn)
{
    const unsigned long long TRADEMARK = 0xdeedbeef19960513;
    server_conn->OnClose = [&](connection*) {
        if (--server_alive_connections == 0) {
            server_all_close.unlock();
        }
        SUCC("[ServerConnection] OnClose\n");
    };
    server_conn->OnHup = [&](connection* conn, const int error) {
        if (error == 0) {
            SUCC("[ServerConnection] OnHup: %d (%s)\n", error, strerror(error));
        }
        else {
            ERROR("[ServerConnection] OnHup: %d (%s)\n", error, strerror(error));
            TEST_FAIL();
        }
        conn->async_close();
    };
    server_conn->OnConnect = [&](connection*) {
        FATAL("[ServerConnection] OnConnect: BUG - unexpected\n");
        TEST_FAIL();
    };
    server_conn->OnConnectError = [&](connection*, const int error) {
        FATAL("[ServerConnection] OnConnectError: %d BUG - unexpected\n", error);
        TEST_FAIL();
    };
    server_conn->OnReceive = [&](connection* conn, const void* buffer, const size_t length) {

        const long long recvd = (server_receive_bytes += length);
        SUCC("[ServerConnection] OnReceive: %lld (total: %lld)\n", (long long)length, (long long)recvd);
        if (recvd == ECHO_DATA_LENGTH * ECHO_DATA_ROUND * THREAD_COUNT) {
            INFO("[ServerConnection] All echo data received. (%lld)\n", recvd);
        }

        void* tmp_buf = malloc(length + sizeof(TRADEMARK));
        TEST_ASSERT(tmp_buf != nullptr);
        *(std::remove_const<decltype(TRADEMARK)>::type*)tmp_buf = TRADEMARK;
        memcpy((char*)tmp_buf + sizeof(TRADEMARK), buffer, length);
        
        bool success = conn->async_send((char*)tmp_buf + sizeof(TRADEMARK), length);
        TEST_ASSERT(success);
    };
    server_conn->OnSend = [&](connection* conn, const void* buffer, const size_t length) {

        const long long sent = (server_send_bytes += length);
        SUCC("[ServerConnection] OnSend: %lld (total: %lld)\n", (long long)length, (long long)sent);
        if (sent == ECHO_DATA_LENGTH * ECHO_DATA_ROUND * THREAD_COUNT) {
            INFO("[ServerConnection] All echo back data sent. (%lld)\n", sent);
        }

        void* org_buf = (char*)buffer - sizeof(TRADEMARK);
        TEST_ASSERT(*(decltype(TRADEMARK)*)org_buf == TRADEMARK);
        free(org_buf);
    };
    server_conn->OnSendError = [&](connection*, const void* buffer, const size_t length, const size_t sent_length, const int error) {
        ERROR("[ServerConnection] OnSendError: %d (%s). all %lld, sent %lld\n", error, strerror(error), (long long)length, (long long)sent_length);

        void* org_buf = (char*)buffer - sizeof(TRADEMARK);
        TEST_ASSERT(*(decltype(TRADEMARK)*)org_buf == TRADEMARK);
        free(org_buf);
    };
}


static void set_client_connection_callbacks(connection* client_conn, const int tid)
{
    client_conn->OnClose = [tid](connection*) {
        SUCC("[Client:%d] OnClose\n", tid);
        client_close[tid].unlock();
    };
    client_conn->OnHup = [tid](connection*, const int error) {
        if (error == 0) {
            SUCC("[Client:%d] OnHup: %d (%s)\n", tid, error, strerror(error));
        }
        else {
            ERROR("[Client:%d] OnHup: %d (%s)\n", tid, error, strerror(error));
        }
    };
    client_conn->OnConnect = [tid](connection* conn) {
        SUCC("[Client:%d] OnConnect\n", tid);
        conn->start_receive();

        bool success = conn->async_send(dummy_data, ECHO_DATA_LENGTH);
        TEST_ASSERT(success);
    };
    client_conn->OnConnectError = [tid](connection*, const int error) {
        ERROR("[Client:%d] OnConnectError: %d (%s)\n", tid, error, strerror(error));
        TEST_FAIL();
    };
    client_conn->OnReceive = [tid](connection* conn, const void* buffer, const size_t length) {
        const size_t start = client_receive_bytes[tid] % ECHO_DATA_LENGTH;
        if (start + length <= ECHO_DATA_LENGTH) {
            ASSERT(memcmp(buffer, dummy_data + start, length) == 0);
        }
        else {
            // start + length > ECHO_DATA_LENGTH
            const size_t tmp = ECHO_DATA_LENGTH - start;
            ASSERT(memcmp(buffer, dummy_data + start, tmp) == 0);
            ASSERT(memcmp((char*)buffer + tmp, dummy_data + 0, length - tmp) == 0);
        }

        client_receive_bytes[tid] += length;
        SUCC("[Client:%d] OnReceive: %lld (total: %lld)\n", tid, (long long)length, (long long)client_receive_bytes[tid]);
        if (client_receive_bytes[tid] == ECHO_DATA_LENGTH * ECHO_DATA_ROUND) {
            INFO("[Client:%d] All echo back data received. now client async_close()\n", tid);
            conn->async_close();
        }
    };
    client_conn->OnSend = [tid](connection* conn, const void* buffer, const size_t length) {
        ASSERT(length == ECHO_DATA_LENGTH);
        client_send_bytes[tid] += length;

        SUCC("[Client:%d] OnSend: %lld (%lld rounds)\n", tid, (long long)length, (long long)client_send_bytes[tid] / ECHO_DATA_LENGTH);
        //fprintf(stderr, "[Client:%d] OnSend: %lld (%lld rounds)\n", tid, (long long)length, (long long)client_send_bytes[tid] / ECHO_DATA_LENGTH);

        if (client_send_bytes[tid] < ECHO_DATA_LENGTH * ECHO_DATA_ROUND) {
            const bool success = conn->async_send(dummy_data, ECHO_DATA_LENGTH);
            TEST_ASSERT(success);
        }
    };
    client_conn->OnSendError = [tid](connection*, const void* buffer, const size_t length, const size_t sent_length, const int error) {
        ERROR("[Client:%d] OnSendError: %d (%s). all %lld, sent %lld\n", tid, error, strerror(error), (long long)length, (long long)sent_length);
        TEST_FAIL();
    };
}

void test_echo_multi_thread_uds()
{
    unlink(SOCKET_FILE);  // may fail

    for (size_t i = 0; i < ECHO_DATA_LENGTH; ++i) {
        dummy_data[i] = (char)(unsigned char)i;
    }

    for (int tid = 0; tid < THREAD_COUNT; ++tid) {        
        client_close[tid].lock();
    }
    listener_close.lock();
    server_all_close.lock();

    socket_environment env;
    socket_listener* lis = env.create_listener(SOCKET_FILE);
    lis->OnAccept = [&](listener*, connection* conn) {
        SUCC("[ServerListener] OnAccept\n");
        set_server_connection_callbacks(conn);
        conn->start_receive();
    };
    lis->OnAcceptError = [&](listener*, const int error) {
        ERROR("[ServerListener] OnAcceptError: %d (%s)\n", error, strerror(error));
        TEST_FAIL();
    };
    lis->OnClose = [&](listener*) {
        SUCC("[ServerListener] OnClose\n");
        listener_close.unlock();
    };
    bool success = lis->start_accept();
    TEST_ASSERT(success);

    std::thread threads[THREAD_COUNT];
    for (int tid = 0; tid < THREAD_COUNT; ++tid) {
        threads[tid] = std::thread([tid, &env]() {
            socket_connection* client = env.create_connection(SOCKET_FILE);
            set_client_connection_callbacks(client, tid);

            // Connect to server every 4 ms (just give server a break)
            std::this_thread::sleep_for(std::chrono::milliseconds(tid * 4));
            const bool success = client->async_connect();
            TEST_ASSERT(success);

            client_close[tid].lock();
        });
    }

    for (int tid = 0; tid < THREAD_COUNT; ++tid) {
        threads[tid].join();
    }

    lis->async_close();
    listener_close.lock();

    env.dispose();

    server_all_close.lock();
    ASSERT(server_alive_connections == 0);

    unlink(SOCKET_FILE);
}


BEGIN_TESTS_DECLARATION(test_socket_echo_multi_thread_uds)
DECLARE_TEST(test_echo_multi_thread_uds)
END_TESTS_DECLARATION
