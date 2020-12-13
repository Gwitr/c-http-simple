#define INIT_THEN_COLLECT_NAME 0
#define COLLECT_NAME 1
#define INIT_THEN_COLLECT_VALUE 2
#define COLLECT_VALUE 3

#define MAX_HEADERS 128

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int imin(int, int);
unsigned uimin(unsigned, unsigned);

int strcmp_ci(const char *s1, const char *s2) {
    char *new_s1 = calloc(strlen(s1) + 1, 1);
    char *new_s2 = calloc(strlen(s2) + 1, 1);
    char *p_new_s1 = new_s1;
    char *p_new_s2 = new_s2;
    for (; *s1 != 0; ) {
        *new_s1 = tolower(*s1);
        s1++; new_s1++;
    }
    for (; *s2 != 0; ) {
        *new_s2 = tolower(*s2);
        s2++; new_s2++;
    }
    int result = strcmp(p_new_s1, p_new_s2);
    free(p_new_s1);
    free(p_new_s2);
    return result;
}

int imin(int x, int y) {
    if (y > x) {
        return y;
    }
    return x;
}

unsigned uimin(unsigned x, unsigned y) {
    if (y > x) {
        return y;
    }
    return x;
}

struct http_response {
    char *response_headers[MAX_HEADERS][2];
    int n_response_headers;
    char *response;
};

static char *httpError;

void httpFree(struct http_response *r) {
    free(r->response);
    for (int i = 0; i < r->n_response_headers; i++) {
        if (i > 0)
            free((r->response_headers)[i][0]);
        free((r->response_headers)[i][1]);
    }
    free(r);
}

void httpSetError(char *error) {
    httpError = calloc(strlen(error) + 1, 1);
    strcpy(httpError, error);
}

void httpFreeError() {
    free(httpError);
    httpError = NULL;
}

struct http_response *httpGet(const char *hostname) {
    struct hostent* info;
    // printf("gethostbyname\n");
    info = gethostbyname(hostname);
    if (info == NULL) {
        httpSetError("gethostbyname() failed");
        return NULL;
    }
    struct sockaddr_in server;

    memset(&server, 0, sizeof(server));
    memcpy(&server.sin_addr, info->h_addr, info->h_length);
    server.sin_family = AF_INET;
    server.sin_port = htons(80);

    int s = socket(AF_INET, SOCK_STREAM, 0);

    // printf("connect\n");
    int result = connect(s, (struct sockaddr *) &server, sizeof(server));
    if (result < 0) {
        httpSetError("Couldn't connect");
        return NULL;
    }

    char *fmt = "GET /index.html HTTP/1.0\r\nMozilla/5.0 (Windows NT 6.1; Win64; x64; rv:47.0) Gecko/20100101 Firefox/47.0\r\nHost: %s\r\nAccept-Encoding: identity\r\nConnection: Keep-Alive\r\n\r\n";
    char *request = calloc(strlen(fmt) + strlen(hostname) + 1, 1);
    sprintf(request, fmt, hostname);

    // printf("send request\n");
    unsigned int current_char = 0;
    unsigned int slen = strlen(request);
    while (current_char < slen) {
        int nsent = write(s, request + current_char, slen - current_char);
        if (nsent < 0) {
            httpSetError("Error during write()\n");
            return NULL;
        }
        current_char += nsent;
    }

    // printf("read response headers\n");
    char *response_headers = calloc(1024 * 16, 1);
    current_char = 0;
    while (current_char < 16 * 1024) {
        if (read(s, response_headers + current_char, 1) < 0) {
            httpSetError("Error during read()");
            return NULL;
        }
        if (response_headers[imin(current_char, 3) - 3] == '\r')
            if (response_headers[imin(current_char, 3) - 2] == '\n')
                if (response_headers[imin(current_char, 3) - 1] == '\r')
                    if (response_headers[imin(current_char, 3) - 0] == '\n')
                        break;
        current_char++;
    }
    free(request);

    // printf("decode response headers\n");
    // NOTE: This can be rewritten to use way less memory
    char *buf = "";
    char *response = calloc(1024, 1);
    char *headers[MAX_HEADERS][2] = { 0 };
    int mode = INIT_THEN_COLLECT_VALUE;
    int current_header = 0;
    int current_buf_char = 0;
    slen = current_char - 1;
    current_char = 0;
    while ((current_char < slen) && (current_header < MAX_HEADERS) && (current_buf_char < 1024 - 1)) {
        if (mode == INIT_THEN_COLLECT_NAME) {
            if (buf != NULL) {
                headers[current_header][1] = buf;
                current_header++;
            }
            buf = calloc(1024, 1);
            current_buf_char = 0;
            mode = COLLECT_NAME;

        } else if (mode == INIT_THEN_COLLECT_VALUE) {
            headers[current_header][0] = buf;
            buf = calloc(1024, 1);
            current_buf_char = 0;
            mode = COLLECT_VALUE;

        } else if (mode == COLLECT_NAME) {
            if (response_headers[current_char] == ':') {
                current_char += 2;
                mode = INIT_THEN_COLLECT_VALUE;
            } else {
                buf[current_buf_char] = response_headers[current_char];
                current_char++;
                current_buf_char++;
            }
        } else if (mode == COLLECT_VALUE) {
            if (response_headers[current_char] == '\r') {
                mode = INIT_THEN_COLLECT_NAME;
                current_char += 2;
            } else {
                buf[current_buf_char] = response_headers[current_char];
                current_char++;
                current_buf_char++;
            }
        }
    }
    int nheaders = current_header;

    // for (int i = 0; i < nheaders; i++) {
        // printf("%s: %s\n", headers[i][0], headers[i][1]);
    // }

    // printf("Response is %s\n", headers[0][1]);
    int content_length = -1;
    // printf("Content-Length");
    for (int i = 0; i < nheaders; i++) {
        // printf("name: %s, value: %s\n", headers[i][0], headers[i][1]);
        if (strcmp_ci(headers[i][0], "Content-Length") == 0) {
            char *end;
            content_length = strtol(headers[i][1], &end, 10);
            break;
        }
    }
    if (content_length == -1) {
        httpSetError("Couldn't find Content-Length header, website might not be HTTP/1.0 compatible.");
        return NULL;
    }
    // printf(" = %d\n", content_length);

    char *final_result = calloc(content_length + 1, 1);
    current_char = 0;
    while (current_char < content_length) {
        int nread = read(s, final_result + current_char, content_length - current_char);
        if (nread < 0) {
            httpSetError("Error during read()");
            return NULL;
        }
        if (nread == 0) {
            httpSetError("Server closed connection");
            return NULL;
        }
        current_char += nread;
    }

    // printf("Result:\n%s", final_result);

    close(s);

    struct http_response *r = calloc(1, sizeof(struct http_response));
    r->response = final_result;
    r->n_response_headers = nheaders;
    for (int i = 0; i < nheaders; i++) {
        r->response_headers[i][0] = headers[i][0];
        r->response_headers[i][1] = headers[i][1];
    }

    return r;
}

int main() {
    struct http_response *r;
    for (int i = 0; i < 5; i++) {
        r = httpGet("example.com");
        if (r == NULL) {
            printf("Error: %s\n", httpError);
            httpFreeError();
            continue;
        }
        printf("%s\n", r->response);
        httpFree(r);
    }
}
