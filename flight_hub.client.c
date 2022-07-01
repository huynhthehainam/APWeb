#include "flight_hub_client.h"
#include "curl/curl.h"
#include "json-c/json.h"

const char *first_str = "{\"deviceToken\": \"";
const char *end_str = "\"}";
char *device_token;

bool is_valid = false;

struct flight_hub_packets
{
    struct flight_hub_packets *next;
    mavlink_message_t high_latency_msg;
    uint32_t receive_ms;
};

static struct flight_hub_packets *flight_hub_packets;

void add_flight_hub_record(mavlink_message_t *high_latency_msg)
{
    struct flight_hub_packets *p;
    p = talloc_size(NULL, sizeof(*p));
    if (p == NULL)
    {
        return;
    }
    p->next = flight_hub_packets;
    memcpy(&p->high_latency_msg, high_latency_msg, sizeof(mavlink_message_t));
    p->receive_ms = get_time_boot_ms();
    flight_hub_packets = p;
}

struct memory_struct
{
    char *memory;
    size_t size;
};
static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t real_size = size * nmemb;
    struct memory_struct *mem = (struct memory_struct *)userp;

    mem->memory = realloc(mem->memory, mem->size + real_size + 1);
    if (mem->memory == NULL)
    {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->memory[mem->size] = 0;

    return real_size;
}

void send_to_flight_hub()
{
    if (is_valid && device_token)
    {
        // collect data
        json_object *root = json_object_new_object();
        json_object *data = json_object_new_array();

        struct flight_hub_packets *p;

        for (p = flight_hub_packets; p; p = p->next)
        {
            mavlink_high_latency_t highLatency;
            mavlink_msg_high_latency_decode(&p->high_latency_msg, &highLatency);
            double lat = highLatency.latitude / (double)1E7;
            double lon = highLatency.longitude / (double)1E7;
            double heading = (double)highLatency.heading * 2.0;

            json_object *item = json_object_new_object();
            json_object_object_add(item, "longitude", json_object_new_double(lon));
            json_object_object_add(item, "latitude", json_object_new_double(lat));
            json_object_object_add(item, "direction", json_object_new_double(heading));
            json_object_array_add(data, item);
        }

        json_object_object_add(root, "data", data);
        char *req_body = json_object_to_json_string(root);

        // console_printf("send from flight hub\n");
        console_printf("%s\n", req_body);
        // send telemetry to flight hub
        // CURL *curl;
        // CURLcode res;
        // struct memory_struct chunk;
        // chunk.memory = malloc(1);
        // chunk.size = 0;
        // curl_global_init(CURL_GLOBAL_ALL);
        // curl = curl_easy_init();
        // if (curl)
        // {
        //     struct curl_slist *headers = NULL;
        //     headers = curl_slist_append(headers, "Accept: application/json");
        //     headers = curl_slist_append(headers, "Content-Type: application/json");
        //     headers = curl_slist_append(headers, "charset: utf-8");

        //     curl_easy_setopt(curl, CURLOPT_URL, "https://dronehub.api.mismart.ai/auth/GenerateDeviceToken");
        //     curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        //     curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        //     curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        // }

        json_object_put(root);
        // free(req_body);
    }
    // MiSmart remove flight hub data
    struct flight_hub_packets *current = flight_hub_packets;
    struct flight_hub_packets *next = NULL;
    while (current != NULL)
    {
        next = current->next;
        free(current);
        current = next;
    }
    flight_hub_packets = NULL;
}

bool connect_to_flight_hub()
{
    console_printf("connect to flight hub \n");
    char *token = load_device_token();
    if (token)
    {
        CURL *curl;
        CURLcode res;
        struct memory_struct chunk;

        chunk.memory = malloc(1);
        chunk.size = 0;
        curl_global_init(CURL_GLOBAL_ALL);
        curl = curl_easy_init();
        if (curl)
        {
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Accept: application/json");
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, "charset: utf-8");

            curl_easy_setopt(curl, CURLOPT_URL, "https://dronehub.api.mismart.ai/auth/GenerateDeviceToken");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            json_object *root = json_object_new_object();

            json_object_object_add(root, "deviceToken", json_object_new_string(token));

            char *req_body = json_object_to_json_string(root);

            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req_body);

            res = curl_easy_perform(curl);
            if (res != CURLE_OK)
            {
                fprintf(stderr, "curl_easy_perform() failed: %s\n",
                        curl_easy_strerror(res));
                is_valid = false;
            }
            else
            {
                printf("%lu bytes retrieved\n", (long)chunk.size);
                console_printf("fuck off %s \n", chunk.memory);

                struct json_object *parsed_json;

                parsed_json = json_tokener_parse(chunk.memory);
                if (parsed_json)
                {
                    struct json_object *data;
                    data = json_object_object_get(parsed_json, "data");
                    if (data)
                    {

                        json_object *access_token;
                        access_token = json_object_object_get(data, "accessToken");
                        if (access_token)
                        {

                            char *access_token_str = json_object_to_json_string(access_token);

                            console_printf("access_token %s\n", access_token_str);
                            free(device_token);
                            device_token = access_token_str;
                            is_valid = true;
                        }
                        else
                        {
                            is_valid = false;
                        }

                        free(access_token);
                    }
                    else
                    {
                        is_valid = false;
                    }
                    free(data);
                }
                else
                {
                    is_valid = false;
                }
                // free variable

                free(parsed_json);
            }
            // free variable
            json_object_put(root);
            curl_slist_free_all(headers);
            headers = NULL;
        }
        else
        {
            is_valid = false;
        }

        free(token);
        if (chunk.memory)
            free(chunk.memory);
        curl_easy_cleanup(curl);
        curl = NULL;
        curl_global_cleanup();
    }
    else
    {
        is_valid = false;
    }
    return is_valid;
}

char *load_device_token()
{
    FILE *fp = fopen("token.json", "rb");
    if (fp == NULL)
    {
        console_printf("file can't be opened\n");
        return NULL;
    }
    char *buffer = 0;
    long length;
    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buffer = malloc(length);
    if (buffer)
    {
        fread(buffer, 1, length, fp);
    }
    fclose(fp);
    if (!buffer)
    {
        console_printf("file can't be opened\n");
        return NULL;
    }
    return buffer;
}

int count_records()
{
    int i = 0;
    struct flight_hub_packets *p;
    for (p = flight_hub_packets; p; p = p->next)
    {
        i++;
    }
    console_printf("count: %d\n", i);
    return i;
}
