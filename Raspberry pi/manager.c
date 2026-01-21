#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <mosquitto.h>
#include <curl/curl.h>

/* ---------- CONFIG ---------- */
#define MQTT_HOST "localhost"
#define MQTT_PORT 1883
#define SUB_TOPIC "rfid/uid"
#define PUB_TOPIC "rfid/response"

#define STUD_FILE "students.csv"
#define UNK_FILE "unknown.csv"
#define PENDING_FILE "pending_upload.csv"

/* ---------- GLOBALS ---------- */
struct mosquitto *mosq = NULL;
volatile int running = 1;

/* ---------- CTRL+C ---------- */
void handle_sigint(int sig)
{
    running = 0;
    if (mosq)
    {
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
    }
    mosquitto_lib_cleanup();
    curl_global_cleanup();
    printf("\nServer stopped\n");
    exit(0);
}

/* ---------- INTERNET CHECK ---------- */
int internet_available()
{
    CURL *curl = curl_easy_init();
    if (!curl) return 0;

    curl_easy_setopt(curl, CURLOPT_URL, "https://www.google.com");
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}

/* ---------- DAILY CSV ---------- */
void get_attendance_filename(char *filename)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(filename, 64, "attendance_%Y-%m-%d.csv", t);
}

void log_attendance(char *uid, char *name, char *roll, char *cls)
{
    char file[64];
    get_attendance_filename(file);

    FILE *fp = fopen(file, "a");
    if (!fp) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    char date[20], timebuf[20];
    strftime(date, sizeof(date), "%Y-%m-%d", t);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", t);

    fprintf(fp, "%s,%s,%s,%s,%s,%s\n",
            date, timebuf, uid, name, roll, cls);

    fclose(fp);
}

/* ---------- STORE PENDING ---------- */
void store_pending(char *name, char *roll, char *cls, char *uid)
{
    FILE *fp = fopen(PENDING_FILE, "a");
    if (!fp) return;

    fprintf(fp, "%s,%s,%s,%s\n", name, roll, cls, uid);
    fclose(fp);
}

/* ---------- SEND TO GOOGLE ---------- */
int send_to_google(char *name, char *roll, char *cls, char *uid)
{
    CURL *curl = curl_easy_init();
    if (!curl) return 0;

    char *e_name = curl_easy_escape(curl, name, 0);
    char *e_roll = curl_easy_escape(curl, roll, 0);
    char *e_cls  = curl_easy_escape(curl, cls, 0);
    char *e_uid  = curl_easy_escape(curl, uid, 0);

    char url[1024];
    snprintf(url, sizeof(url),
        "https://script.google.com/macros/s/AKfycbxnt84fZdcjHcNCD_JH9fn3ub-3au5j3NoDauMONyPIjU2kizWmY0c__nYUT-WWbayhug/exec"
        "?data=%s%%20%s%%20%s%%20%s",
        e_name, e_roll, e_cls, e_uid);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);

    curl_free(e_name);
    curl_free(e_roll);
    curl_free(e_cls);
    curl_free(e_uid);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}

/* ---------- SYNC PENDING ---------- */
void sync_pending()
{
    if (!internet_available()) return;

    FILE *fp = fopen(PENDING_FILE, "r");
    if (!fp) return;

    FILE *temp = fopen("pending_tmp.csv", "w");
    if (!temp) { fclose(fp); return; }

    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        char *name = strtok(line, ",");
        char *roll = strtok(NULL, ",");
        char *cls  = strtok(NULL, ",");
        char *uid  = strtok(NULL, "\n");

        if (!send_to_google(name, roll, cls, uid))
            fprintf(temp, "%s,%s,%s,%s\n", name, roll, cls, uid);
    }

    fclose(fp);
    fclose(temp);

    remove(PENDING_FILE);
    rename("pending_tmp.csv", PENDING_FILE);
}

/* ---------- FIND STUDENT ---------- */
int find_student(char *uid, char *out)
{
    FILE *fp = fopen(STUD_FILE, "r");
    if (!fp) return 0;

    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        char *f_uid = strtok(line, ",");
        char *name  = strtok(NULL, ",");
        char *roll  = strtok(NULL, ",");
        char *cls   = strtok(NULL, "\n");

        if (f_uid && strcmp(uid, f_uid) == 0)
        {
            store_pending(name, roll, cls, uid);   // ALWAYS queue
            log_attendance(uid, name, roll, cls);
            sprintf(out, "%s,%s,%s", name, roll, cls);
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

/* ---------- MQTT CALLBACK ---------- */
void on_message(struct mosquitto *m, void *u,
                const struct mosquitto_message *msg)
{
    char uid[64] = {0};
    strncpy(uid, msg->payload, sizeof(uid) - 1);

    char response[128];

    if (find_student(uid, response))
        mosquitto_publish(m, NULL, PUB_TOPIC,
                          strlen(response), response, 0, false);
    else
    {
        FILE *fp = fopen(UNK_FILE, "a");
        if (fp) { fprintf(fp, "%s\n", uid); fclose(fp); }

        mosquitto_publish(m, NULL, PUB_TOPIC,
                          7, "UNKNOWN", 0, false);
    }
}

/* ---------- MAIN ---------- */
int main()
{
    signal(SIGINT, handle_sigint);

    mosquitto_lib_init();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    mosq = mosquitto_new(NULL, true, NULL);
    mosquitto_message_callback_set(mosq, on_message);

    mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, 60);
    mosquitto_subscribe(mosq, NULL, SUB_TOPIC, 0);

    printf("RFID Attendance Server (FINAL CORRECT ARCHITECTURE)\n");

    time_t last_sync = 0;

    while (running)
    {
        mosquitto_loop(mosq, 1000, 1);

        time_t now = time(NULL);
        if (now - last_sync >= 30)
        {
            sync_pending();
            last_sync = now;
        }
    }

    return 0;
}
