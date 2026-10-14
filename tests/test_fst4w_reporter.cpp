#include "FST4WReporter.h"

#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>

static std::string upload_reply(const char *body, int status = 200)
{
    int server = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(server, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0);
    socklen_t len = sizeof(addr);
    assert(getsockname(server, reinterpret_cast<struct sockaddr *>(&addr), &len) == 0);
    assert(listen(server, 1) == 0);
    std::thread responder([server, body, status]() {
        int client = accept(server, NULL, NULL);
        assert(client >= 0);
        char request[1024];
        ssize_t n = recv(client, request, sizeof(request) - 1, 0);
        assert(n > 0);
        request[n] = '\0';
        assert(strstr(request, "GET /post/?function=wspr&mode=3 HTTP/1.1"));
        char response[1024];
        int size = snprintf(response, sizeof(response),
            "HTTP/1.1 %d Test\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            status, strlen(body), body);
        assert(send(client, response, size, MSG_NOSIGNAL) == size);
        close(client);
        close(server);
    });

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/post/?function=wspr&mode=3",
        ntohs(addr.sin_port));
    fst4w_upload_t upload;
    upload.rx_chan = 2;
    upload.spot_log = true;
    upload.urls.push_back(url);

    int output[2];
    assert(pipe(output) == 0);
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    assert(saved >= 0 && dup2(output[1], STDERR_FILENO) >= 0);
    close(output[1]);
    fst4w_report_upload(&upload);
    fflush(stderr);
    assert(dup2(saved, STDERR_FILENO) >= 0);
    close(saved);
    responder.join();
    char log[1024];
    ssize_t count = read(output[0], log, sizeof(log));
    assert(count > 0);
    close(output[0]);
    return std::string(log, count);
}

int main()
{
    struct tm slot = {};
    slot.tm_year = 126;
    slot.tm_mon = 9;
    slot.tm_mday = 14;
    slot.tm_hour = 23;
    slot.tm_min = 58;
    std::string url;
    assert(fst4w_report_url("RX1%2FP", "AA00aa", 136000, 120, slot,
        1512.5f, -27.6f, 0.25f, "G4ABC IO91 37", url));
    assert(url ==
        "http://wsprnet.org/post/?function=wspr&rcall=RX1%2FP&rgrid=AA00AA&rqrg=0.137500&"
        "date=261014&time=2358&sig=-28&dt=0.2&drift=0&tqrg=0.137513&"
        "tcall=G4ABC&tgrid=IO91&dbm=37&version=web-888&mode=3");
    const int periods[] = { 120, 300, 900, 1800 };
    const char *modes[] = { "&mode=3", "&mode=5", "&mode=15", "&mode=30" };
    for (int i = 0; i < 4; i++) {
        assert(fst4w_report_url("RX1", "AA00", 474200, periods[i], slot,
            1500, -30, 0, "G4ABC IO91 37", url));
        assert(url.find("rqrg=0.475700") != std::string::npos);
        assert(url.find(modes[i]) != std::string::npos);
    }
    for (int period : { 15, 30, 60 })
        assert(!fst4w_report_url("RX1", "AA00", 136000, period, slot,
            1500, -30, 0, "G4ABC IO91 37", url));
    assert(fst4w_report_url("RX1", "AA00", 136000, 120, slot,
        1500, -30, 0, "G4ABC/P 37", url));
    assert(url.find("tcall=G4ABC/P&tgrid=&dbm=37") != std::string::npos);
    assert(fst4w_report_url("RX1", "AA00", 136000, 120, slot,
        1500, -30, 0, "<G4ABC/P> IO91ab", url));
    assert(url.find("tcall=G4ABC/P&tgrid=IO91AB&dbm=") != std::string::npos);
    for (const char *text : { "<...> IO91ab", "G4ABC ZZ99 37", "G4ABC IO91 103",
        "G4ABC IO91 37 extra", "G4ABC IO91 bad", "CQ IO91 37", "G4ABC" }) {
        assert(!fst4w_report_url("RX1", "AA00", 136000, 120, slot,
            1500, -30, 0, text, url));
        assert(url.empty());
    }
    assert(!fst4w_report_url("", "AA00", 136000, 120, slot,
        1500, -30, 0, "G4ABC IO91 37", url));
    assert(!fst4w_report_url("RX1", "ZZ00", 136000, 120, slot,
        1500, -30, 0, "G4ABC IO91 37", url));

    assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    assert(upload_reply("1 spot(s) added").find("upload rejected") == std::string::npos);
    assert(upload_reply("10 spot(s) added").find("upload rejected") == std::string::npos);
    assert(upload_reply("Spot report: 1 out of 1 spot(s) added").find("upload rejected") == std::string::npos);
    assert(upload_reply("0 spot(s) added").find("upload rejected") != std::string::npos);
    assert(upload_reply("Spot report: 0 out of 1 spot(s) added").find("upload rejected") != std::string::npos);
    assert(upload_reply("invalid mode").find("upload rejected") != std::string::npos);
    assert(upload_reply("server error", 500).find("upload failed") != std::string::npos);
    curl_global_cleanup();
    puts("FST4W reporter tests passed");
}
