#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "cfg.h"
#include "printf.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "net.h"
#include "mqtt.h"
#include "mqttpub.h"

static uint8_t sendbuf[65536]; /* sendbuf should be large enough to hold multiple whole mqtt messages */
static uint8_t recvbuf[1024]; /* recvbuf should be large enough any whole mqtt message expected to be received */

static const char* mqtt_server;
static int mqtt_port;
static const char* mqtt_user;
static const char* mqtt_password;
static char mqtt_client_id[64];

static int mqtt_socket = -1;
static lock_t mqtt_lock;
static struct mqtt_client mqtt_client;

static int open_nb_socket(const char* addr, const int port) {
    struct addrinfo hints = { 0 };

    hints.ai_family = AF_UNSPEC;     /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* Must be TCP */
    int sockfd = -1;
    int rv;
    struct addrinfo *p, *servinfo;
    char port_str[6];

    /* get address information */
    snprintf(port_str, sizeof(port_str), "%d", port);
    do {
        rv = getaddrinfo(addr, port_str, &hints, &servinfo);
        if (rv == EAI_AGAIN) {
            printf("[MQTT] Failed to open socket (getaddrinfo): %s\n", gai_strerror(rv));
            usleep(100000U);
        }
    } while (rv == EAI_AGAIN);
    if (rv != 0) {
        printf("[MQTT] Failed to open socket (getaddrinfo): %s\n", gai_strerror(rv));
        return -1;
    }

    /* open the first possible socket */
    for (p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;

        /* connect to server */
        rv = connect(sockfd, p->ai_addr, p->ai_addrlen);
        if (rv == -1) {
            close(sockfd);
            sockfd = -1;
            continue;
        }
        break;
    }

    /* free servinfo */
    freeaddrinfo(servinfo);

    /* make non-blocking */
    if (sockfd != -1) fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK);

    /* return the new socket fd */
    return sockfd;
}

static void publish_callback(void** unused, struct mqtt_response_publish* published) {
    /* not used */
}

static void* client_refresher(void* client) {
    while (mqtt_socket != -1) {
        lock_enter(&mqtt_lock);
        /* send keep alive packets */
        mqtt_sync((struct mqtt_client*)client);
        lock_leave(&mqtt_lock);
        usleep(100000U);
    }

    return NULL;
}

void mqtt_init(void) {
    mqtt_server = admcfg_string("mqtt_server", NULL, CFG_OPTIONAL);
    if (mqtt_server != NULL) {
        mqtt_port = admcfg_int("mqtt_port", NULL, CFG_OPTIONAL);
        mqtt_user = admcfg_string("mqtt_user", NULL, CFG_OPTIONAL);
        mqtt_password = admcfg_string("mqtt_password", NULL, CFG_OPTIONAL);

        sprintf(mqtt_client_id, "%08x%08x", PRINTF_U64_ARG(net.dna));
    } else {
        return;
    }

    printf("[MQTT] client id: %s\n", mqtt_client_id);

    lock_init(&mqtt_lock);

    /* open the non-blocking TCP socket (connecting to the broker) */
    int sockfd = open_nb_socket(mqtt_server, mqtt_port);

    if (sockfd == -1) {
        mqtt_socket = -1;
        return;
    }

    mqtt_socket = sockfd;
    /* setup a client */
    mqtt_init(&mqtt_client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);
    /* Ensure we have a clean session */
    uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
    /* Send connection request to the broker. */
    mqtt_connect(&mqtt_client, mqtt_client_id, NULL, NULL, 0, mqtt_user, mqtt_password, connect_flags, 400);

    /* check that we don't have any errors */
    if (mqtt_client.error != MQTT_OK) {
        printf("[MQTT] error: %s\n", mqtt_error_str(mqtt_client.error));
        close(mqtt_socket);
        mqtt_socket = -1;
        return;
    }

    /* start the refresher thread */
    pthread_t thread;
    if (pthread_create(&thread, NULL, client_refresher, &mqtt_client) != 0) {
        printf("[MQTT] Failed to create MQTT refresher thread\n");
        close(mqtt_socket);
        mqtt_socket = -1;
    }
    pthread_detach(thread);

    printf("[MQTT] client connected to %s:%d\n", mqtt_server, mqtt_port);
}

bool mqtt_is_connected() {
    return mqtt_socket != -1;
}


void mqtt_publish(const char* topic, const char* payload, ...) {
    if (mqtt_socket == -1) return;

    va_list args;
    va_start(args, payload);
    char payload_buf[2048];
    char body_buf[1024];
    char topic_buf[128];
    snprintf(topic_buf, sizeof(topic_buf), "web888/%s/%s", mqtt_client_id, topic);
    vsnprintf(body_buf, sizeof(body_buf), payload, args);
    va_end(args);

    char timestamp_buf[64];
    snprintf(timestamp_buf, sizeof(timestamp_buf), "%d", (int)time(NULL));

    snprintf(payload_buf, sizeof(payload_buf), "{\"timestamp\": \"%s\", \"server\": \"%s\", %s}", timestamp_buf, mqtt_client_id, body_buf);
    lock_enter(&mqtt_lock);
    /* publish a message */
    mqtt_publish(&mqtt_client, topic_buf, payload_buf, strlen(payload_buf), MQTT_PUBLISH_QOS_0);

    /* check for errors */
    if (mqtt_client.error != MQTT_OK) {
        printf("[MQTT] publish failed: %s\n", mqtt_error_str(mqtt_client.error));
        close(mqtt_socket);
        mqtt_socket = -1;
    }

    lock_leave(&mqtt_lock);
}
