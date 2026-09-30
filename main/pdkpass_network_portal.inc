// Private implementation: included by the owning service, with its existing lock and worker.
static esp_err_t root_get(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, _binary_pdkpass_setup_page_html_start);
}

static esp_err_t nearby_get(httpd_req_t *request)
{
    char json[512];
    if (!pdkpass_wifi_nearby_json(&s_setup_nearby, s_setup_scan_failed,
                                   json, sizeof(json))) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Nearby network list unavailable");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, json);
}

static esp_err_t save_post(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > FORM_BODY_LIMIT) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid form size");
        return ESP_FAIL;
    }

    char body[FORM_BODY_LIMIT + 1];
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        int result = httpd_req_recv(request, body + received,
                                    request->content_len - received);
        if (result <= 0) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                "Incomplete form");
            return ESP_FAIL;
        }
        received += (size_t)result;
    }
    body[received] = '\0';

    char ssid[33];
    char password[65];
    if (!pdkpass_wifi_form_parse(body, received, ssid, sizeof(ssid),
                                  password, sizeof(password))) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                            "Check Wi-Fi name and password");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(s_candidate_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Please try again");
        return ESP_FAIL;
    }
    if (s_candidate_busy) {
        xSemaphoreGive(s_candidate_lock);
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "A connection is already being tested. Please wait.");
    }
    s_candidate_busy = true;
    memcpy(s_candidate_ssid, ssid, sizeof(s_candidate_ssid));
    memcpy(s_candidate_password, password, sizeof(s_candidate_password));
    xSemaphoreGive(s_candidate_lock);
    xEventGroupSetBits(s_events, EVENT_CANDIDATE);

    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, _binary_pdkpass_setup_status_html_start);
}

static esp_err_t start_http_server(void)
{
    if (s_http) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.stack_size = 6144;

    esp_err_t err = httpd_start(&s_http, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get,
    };
    const httpd_uri_t nearby = {
        .uri = "/networks", .method = HTTP_GET, .handler = nearby_get,
    };
    const httpd_uri_t save = {
        .uri = "/save", .method = HTTP_POST, .handler = save_post,
    };
    err = httpd_register_uri_handler(s_http, &root);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_http, &nearby);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_http, &save);
    if (err != ESP_OK) {
        httpd_stop(s_http);
        s_http = NULL;
    }
    return err;
}

static void stop_http_server(void)
{
    if (!s_http) return;
    httpd_stop(s_http);
    s_http = NULL;
}
