#include "http_server.h"
#include "worker_pool.h"
#include "queue.h"

static SOCKET g_server_socket = INVALID_SOCKET;
static pthread_t g_http_thread;
static int g_server_port = 8080;

/* Helper to parse JSON string fields */
static void parse_json_str(const char* json, const char* key, char* out, size_t max_len) {
    out[0] = '\0';
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p == '\"') {
        p++;
        size_t idx = 0;
        while (*p && *p != '\"' && idx < max_len - 1) {
            out[idx++] = *p++;
        }
        out[idx] = '\0';
    }
}

/* Helper to parse JSON numeric fields */
static double parse_json_double(const char* json, const char* key, double def_val) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return def_val;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\"') p++;
    char* endptr = NULL;
    double v = strtod(p, &endptr);
    return (endptr == p) ? def_val : v;
}

/* Helper to locate local frontend file in ./frontend or ../frontend */
static FILE* open_frontend_file(const char* rel_path) {
    char path1[512];
    char path2[512];
    snprintf(path1, sizeof(path1), "frontend/%s", rel_path);
    FILE* f = fopen(path1, "rb");
    if (f) return f;
    snprintf(path2, sizeof(path2), "../frontend/%s", rel_path);
    return fopen(path2, "rb");
}

/* Send full HTTP response */
static void send_response(SOCKET client_sock, int status_code, const char* status_text,
                          const char* content_type, const char* body, size_t body_len) {
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text, content_type, body_len);

    send(client_sock, header, header_len, 0);
    if (body_len > 0 && body) {
        send(client_sock, body, (int)body_len, 0);
    }
}

/* Build JSON representation of simulation state */
static void handle_get_status(SOCKET client_sock) {
    /* Allocate 128KB buffer on heap for formatted JSON payload */
    size_t buf_size = 131072;
    char* buf = (char*)malloc(buf_size);
    if (!buf) {
        send_response(client_sock, 500, "Internal Server Error", "text/plain", "Out of memory", 13);
        return;
    }

    pthread_mutex_lock(&g_sim.sim_mutex);

    double wall_time = g_sim.sim_wall_clock_time;
    double seq_time = g_sim.sim_sequential_time;
    double speedup = (wall_time > 0.05 && seq_time > 0.05) ? (seq_time / wall_time) : 1.0;
    int q_cnt = queue_count();
    int active_workers = 0;
    for (int i = 0; i < g_sim.worker_count; i++) {
        if (g_workers[i].state == WORKER_WORKING) {
            active_workers++;
        }
    }

    size_t offset = 0;
    offset += snprintf(buf + offset, buf_size - offset,
        "{\n"
        "  \"is_running\": %s,\n"
        "  \"is_paused\": %s,\n"
        "  \"worker_count\": %d,\n"
        "  \"active_workers_count\": %d,\n"
        "  \"total_jobs_added\": %d,\n"
        "  \"total_jobs_completed\": %d,\n"
        "  \"pending_jobs_count\": %d,\n"
        "  \"sim_wall_clock_time\": %.2f,\n"
        "  \"sim_sequential_time\": %.2f,\n"
        "  \"speedup_ratio\": %.2f,\n",
        g_sim.is_running ? "true" : "false",
        g_sim.is_paused ? "true" : "false",
        g_sim.worker_count,
        active_workers,
        g_sim.total_jobs_added,
        g_sim.total_jobs_completed,
        q_cnt,
        wall_time,
        seq_time,
        speedup);

    /* Workers Array */
    offset += snprintf(buf + offset, buf_size - offset, "  \"workers\": [\n");
    for (int i = 0; i < g_sim.worker_count; i++) {
        Worker* w = &g_workers[i];
        const char* state_str = "IDLE";
        if (w->state == WORKER_WORKING) state_str = "WORKING";
        else if (w->state == WORKER_PAUSED) state_str = "PAUSED";

        offset += snprintf(buf + offset, buf_size - offset,
            "    {\n"
            "      \"id\": %d,\n"
            "      \"sys_thread_id\": \"0x%lX\",\n"
            "      \"state\": \"%s\",\n"
            "      \"has_job\": %s,\n"
            "      \"jobs_completed_count\": %d,\n"
            "      \"total_busy_time\": %.2f,\n",
            w->id + 1,
            w->sys_thread_id,
            state_str,
            w->has_job ? "true" : "false",
            w->jobs_completed_count,
            w->total_busy_time);

        if (w->has_job) {
            offset += snprintf(buf + offset, buf_size - offset,
                "      \"current_job\": {\n"
                "        \"id\": %d,\n"
                "        \"name\": \"%s\",\n"
                "        \"duration_sec\": %.2f,\n"
                "        \"elapsed_sec\": %.2f,\n"
                "        \"progress\": %.2f\n"
                "      }\n",
                w->current_job.id,
                w->current_job.name,
                w->current_job.duration_sec,
                w->current_job.elapsed_sec,
                w->current_job.progress);
        } else {
            offset += snprintf(buf + offset, buf_size - offset, "      \"current_job\": null\n");
        }

        offset += snprintf(buf + offset, buf_size - offset, "    }%s\n", (i == g_sim.worker_count - 1) ? "" : ",");
    }
    offset += snprintf(buf + offset, buf_size - offset, "  ],\n");

    /* Queue Array */
    offset += snprintf(buf + offset, buf_size - offset, "  \"queue\": [\n");
    for (int i = 0; i < q_cnt; i++) {
        Job* j = queue_peek_at(i);
        if (j) {
            offset += snprintf(buf + offset, buf_size - offset,
                "    {\"id\": %d, \"name\": \"%s\", \"duration_sec\": %.2f, \"created_at\": \"%s\"}%s\n",
                j->id, j->name, j->duration_sec, j->created_at, (i == q_cnt - 1) ? "" : ",");
        }
    }
    offset += snprintf(buf + offset, buf_size - offset, "  ],\n");

    /* Completed Array (last 20 for dashboard performance) */
    int comp_cnt = completed_count();
    int start_comp = (comp_cnt > 25) ? (comp_cnt - 25) : 0;
    offset += snprintf(buf + offset, buf_size - offset, "  \"completed\": [\n");
    for (int i = comp_cnt - 1; i >= start_comp; i--) {
        Job* j = completed_peek_at(i);
        if (j) {
            offset += snprintf(buf + offset, buf_size - offset,
                "    {\"id\": %d, \"name\": \"%s\", \"duration_sec\": %.2f, \"worker_id\": %d, \"completed_at\": \"%s\"}%s\n",
                j->id, j->name, j->duration_sec, j->assigned_worker_id + 1, j->completed_at, (i == start_comp) ? "" : ",");
        }
    }
    offset += snprintf(buf + offset, buf_size - offset, "  ],\n");

    /* Logs Array (last 50 for execution view) */
    int log_cnt = logs_count();
    int start_log = (log_cnt > 60) ? (log_cnt - 60) : 0;
    offset += snprintf(buf + offset, buf_size - offset, "  \"logs\": [\n");
    for (int i = log_cnt - 1; i >= start_log; i--) {
        LogEntry* l = logs_peek_at(i);
        if (l) {
            /* Escape quotes in log messages if any */
            char safe_msg[300];
            int s_idx = 0;
            for (int k = 0; l->message[k] && s_idx < 290; k++) {
                if (l->message[k] == '\"') {
                    safe_msg[s_idx++] = '\\';
                    safe_msg[s_idx++] = '\"';
                } else if (l->message[k] == '\n' || l->message[k] == '\r') {
                    safe_msg[s_idx++] = ' ';
                } else {
                    safe_msg[s_idx++] = l->message[k];
                }
            }
            safe_msg[s_idx] = '\0';

            offset += snprintf(buf + offset, buf_size - offset,
                "    {\"id\": %d, \"timestamp\": \"%s\", \"worker_id\": %d, \"tag\": \"%s\", \"message\": \"%s\"}%s\n",
                l->id, l->timestamp, (l->worker_id >= 0 ? l->worker_id + 1 : 0), l->tag, safe_msg, (i == start_log) ? "" : ",");
        }
    }
    offset += snprintf(buf + offset, buf_size - offset, "  ]\n");

    offset += snprintf(buf + offset, buf_size - offset, "}\n");

    pthread_mutex_unlock(&g_sim.sim_mutex);

    send_response(client_sock, 200, "OK", "application/json", buf, offset);
    free(buf);
}

/* Serve static frontend asset */
static void handle_static_file(SOCKET client_sock, const char* path, const char* content_type) {
    FILE* f = open_frontend_file(path);
    if (!f) {
        char err[128];
        int err_len = snprintf(err, sizeof(err), "File Not Found: %s", path);
        send_response(client_sock, 404, "Not Found", "text/plain", err, err_len);
        return;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* file_buf = (char*)malloc(file_size);
    if (!file_buf) {
        fclose(f);
        send_response(client_sock, 500, "Internal Server Error", "text/plain", "Out of memory", 13);
        return;
    }

    size_t read_bytes = fread(file_buf, 1, file_size, f);
    fclose(f);

    send_response(client_sock, 200, "OK", content_type, file_buf, read_bytes);
    free(file_buf);
}

/* Handle HTTP Client Request */
static void process_client(SOCKET client_sock) {
    char req_buf[4096];
    int bytes_read = recv(client_sock, req_buf, sizeof(req_buf) - 1, 0);
    if (bytes_read <= 0) {
        closesocket(client_sock);
        return;
    }
    req_buf[bytes_read] = '\0';

    /* Parse HTTP Method and Path */
    char method[16] = {0};
    char path[256] = {0};
    sscanf(req_buf, "%15s %255s", method, path);

    /* Locate body if POST */
    const char* body = strstr(req_buf, "\r\n\r\n");
    if (body) {
        body += 4;
    } else {
        body = "";
    }

    if (strcmp(method, "OPTIONS") == 0) {
        send_response(client_sock, 204, "No Content", "text/plain", "", 0);
    }
    else if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
            handle_static_file(client_sock, "index.html", "text/html");
        } else if (strcmp(path, "/css/style.css") == 0) {
            handle_static_file(client_sock, "css/style.css", "text/css");
        } else if (strcmp(path, "/js/app.js") == 0) {
            handle_static_file(client_sock, "js/app.js", "application/javascript");
        } else if (strcmp(path, "/api/status") == 0) {
            handle_get_status(client_sock);
        } else {
            send_response(client_sock, 404, "Not Found", "text/plain", "Not Found", 9);
        }
    }
    else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/jobs") == 0) {
            char name[64] = {0};
            parse_json_str(body, "name", name, sizeof(name));
            double dur = parse_json_double(body, "duration", 2.0);
            if (dur <= 0.0) dur = 1.0;

            int job_id = sim_add_job(strlen(name) > 0 ? name : "Factory Job", dur);
            char resp[128];
            int rlen = snprintf(resp, sizeof(resp), "{\"success\": true, \"job_id\": %d}", job_id);
            send_response(client_sock, 200, "OK", "application/json", resp, rlen);
        }
        else if (strcmp(path, "/api/control") == 0) {
            char action[32] = {0};
            parse_json_str(body, "action", action, sizeof(action));

            if (strcmp(action, "start") == 0) {
                sim_start();
            } else if (strcmp(action, "pause") == 0) {
                sim_pause();
            } else if (strcmp(action, "resume") == 0) {
                sim_resume();
            } else if (strcmp(action, "reset") == 0) {
                sim_reset();
            } else if (strcmp(action, "set_workers") == 0) {
                int count = (int)parse_json_double(body, "count", 4.0);
                worker_pool_set_count(count);
            }

            const char* resp = "{\"success\": true}";
            send_response(client_sock, 200, "OK", "application/json", resp, strlen(resp));
        }
        else if (strcmp(path, "/api/batch") == 0) {
            int count = (int)parse_json_double(body, "count", 4.0);
            int added = sim_add_preset_batch(count);
            char resp[128];
            int rlen = snprintf(resp, sizeof(resp), "{\"success\": true, \"added\": %d}", added);
            send_response(client_sock, 200, "OK", "application/json", resp, rlen);
        }
        else {
            send_response(client_sock, 404, "Not Found", "text/plain", "Unknown API endpoint", 20);
        }
    }
    else {
        send_response(client_sock, 405, "Method Not Allowed", "text/plain", "Method Not Allowed", 18);
    }

    closesocket(client_sock);
}

/* Main HTTP Server Thread */
static void* http_server_thread_func(void* arg) {
    (void)arg;
    
    while (g_sim.keep_alive) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET client_sock = accept(g_server_socket, (struct sockaddr*)&client_addr, &addr_len);

        if (client_sock == INVALID_SOCKET) {
            if (!g_sim.keep_alive) break;
            sleep_ms(10);
            continue;
        }

        process_client(client_sock);
    }

    return NULL;
}

int http_server_start(int port) {
    g_server_port = port;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return -1;
    }
#endif

    g_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_server_socket == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create server socket.\n");
        return -1;
    }

    int opt = 1;
    setsockopt(g_server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((unsigned short)port);

    if (bind(g_server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Failed to bind server socket to port %d.\n", port);
        closesocket(g_server_socket);
        return -1;
    }

    if (listen(g_server_socket, 16) == SOCKET_ERROR) {
        fprintf(stderr, "Failed to listen on socket.\n");
        closesocket(g_server_socket);
        return -1;
    }

    printf("[Thread Factory Server] Listening on http://localhost:%d\n", port);

    int rc = pthread_create(&g_http_thread, NULL, http_server_thread_func, NULL);
    if (rc != 0) {
        fprintf(stderr, "Failed to create HTTP server thread.\n");
        closesocket(g_server_socket);
        return -1;
    }

    return 0;
}

void http_server_stop(void) {
    if (g_server_socket != INVALID_SOCKET) {
        closesocket(g_server_socket);
        g_server_socket = INVALID_SOCKET;
    }
    pthread_join(g_http_thread, NULL);
#ifdef _WIN32
    WSACleanup();
#endif
}
