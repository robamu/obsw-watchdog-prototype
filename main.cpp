#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

#include <iostream>
#include <thread>
#include <cstring>
#include <filesystem>

static const std::string FIFO_NAME = "/tmp/obsw-watchdog";
static constexpr int APP_RUNTIME = 10.0;

class ObswTask {
public:
    ObswTask() {

    }

    virtual ~ObswTask() {

    }

    void performOperation() {
        using namespace std::chrono_literals;
        auto start = std::chrono::system_clock::now();
        std::cout << "OBSW Task started" << std::endl;
        uint8_t cancelIdx = 0;
        while(not std::filesystem::exists(FIFO_NAME)) {
            std::this_thread::sleep_for(5ms);
            cancelIdx ++;
            if(cancelIdx >= 10) {
                std::cerr << FIFO_NAME << " not created after " << 10 * 5 / 1000 << " seconds.."
                        << std::endl;
                return;
            }
        }
        std::cout << "ObswTask: Opening FIFO write-only.." << std::endl;
        // Open FIFO write only and non-blocking
        fd = open(FIFO_NAME.c_str(), O_WRONLY);
        if(fd < 0) {
            std::cerr << "Opening pipe " << FIFO_NAME << "write-only failed with " << errno <<
                    ": " << strerror(errno) << std::endl;
        }
        std::cout << "ObswTask: FIFO opened successfully" << std::endl;

        auto now = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = now - start;
        while(elapsed_seconds.count() < APP_RUNTIME) {
            const char* writeChar = "a";
            std::cout << "Writing to FIFO.." << std::endl;
            ssize_t writtenBytes = write(fd, &writeChar, 1);
            if (writtenBytes < 0) {
                std::cerr << "Write error on pipe " << FIFO_NAME << ", error " << errno <<
                        ": " << strerror(errno) << std::endl;
            }
            else {
                std::cout << "Written to FIFO " << FIFO_NAME << " successfully" << std::endl;
            }
            now = std::chrono::system_clock::now();
            elapsed_seconds = now - start;
            std::this_thread::sleep_for(2000ms);
        }
        if (close(fd) < 0) {
            std::cerr << "Closing named pipe at " << FIFO_NAME << "failed, error " << errno <<
                    ": " << strerror(errno) << std::endl;
        }
        std::cout << "ObswTask: Finished" << std::endl;
    }

private:
    int fd = 0;
};

class WatchdogTask {
public:
    WatchdogTask(): fd(0) {

    }

    virtual ~WatchdogTask() {

    }

    void performOperation() {
        auto start = std::chrono::system_clock::now();
        std::cout << "Watchdog Task started" << std::endl;
        int result = 0;
        // Only create the FIFO if it does not exist yet
        if(not std::filesystem::exists(FIFO_NAME)) {
            // Permission  666 or rw-rw-rw-
            mode_t mode = DEFFILEMODE;
            result = mkfifo(FIFO_NAME.c_str(), mode);
            if(result != 0) {
                std::cerr << "Could not created named pipe at " << FIFO_NAME << ", error " << errno <<
                        ": " << strerror(errno) << std::endl;
                return;
            }
            std::cout << "Pipe at " << FIFO_NAME << " created successfully" << std::endl;
        }

        // Open FIFO read only and non-blocking
        fd = open(FIFO_NAME.c_str(), O_RDONLY | O_NONBLOCK);
        if(fd < 0) {
            std::cerr << "Opening pipe " << FIFO_NAME << "read-only failed with " << errno <<
                    ": " << strerror(errno) << std::endl;
        }

        auto now = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = now - start;
        while(elapsed_seconds.count() < APP_RUNTIME) {
            watchdogLoop();
            now = std::chrono::system_clock::now();
            elapsed_seconds = now - start;
        }
        if (close(fd) < 0) {
            std::cerr << "Closing named pipe at " << FIFO_NAME << "failed, error " << errno <<
                    ": " << strerror(errno) << std::endl;
        }
        std::cout << "WatchdogTask: Finished" << std::endl;
    }

    void watchdogLoop() {
        char readChar;
        struct pollfd waiter = {.fd = fd, .events = POLLIN};

        // 10 seconds timeout, only poll one file descriptor
        switch(poll(&waiter, 1, TIMEOUT_MS)) {
        case(0): {
            std::cout << "The FIFO timed out!" << std::endl;
            return;
        }
        case(1): {
            if (waiter.revents & POLLIN) {
                ssize_t readLen = read(fd, buf.data(), buf.size());
                if (readLen < 0) {
                    std::cerr << "Read error on pipe " << FIFO_NAME << ", error " << errno <<
                            ": " << strerror(errno) << std::endl;
                }
                std::cout << "Read " << readLen << " byte(s) on the pipe " << FIFO_NAME
                        << std::endl;
            }
            else if(waiter.revents & POLLERR) {
                std::cerr << "Poll error error on pipe " << FIFO_NAME << std::endl;
            }
            else if (waiter.revents & POLLHUP) {
                // Writer closed its end
            }
            break;
        }
        default: {
            std::cerr << "Unknown poll error at " << FIFO_NAME << ", error " << errno <<
                    ": " << strerror(errno) << std::endl;
            break;
        }
        }
    }
private:
    int fd;
    static constexpr int TIMEOUT_MS = 10 * 1000;
    std::array<uint8_t, 128> buf;
};

/**
 * @brief   This code only works on Linux systems
 */
int main() {
    std::cout << "OBSW Watchdog Prototyping Application" << std::endl;
    WatchdogTask watchdogTask;
    std::thread watchdogThread(&WatchdogTask::performOperation, watchdogTask);
    ObswTask obswTask;
    std::thread obswThread(&ObswTask::performOperation, obswTask);
    obswThread.join();
    watchdogThread.join();
    return 0;
}

